#include "capriole/repair/reconstruct.hpp"

#include <array>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/core/source_reader.hpp"
#include "capriole/parser/lexer.hpp"
#include "capriole/parser/object_loader.hpp"
#include "capriole/parser/object_parser.hpp"
#include "capriole/parser/object_stream.hpp"

namespace capriole::repair {

namespace {

using model::Dict;
using model::Object;

// Enough history to look back over "<10 digits> <5 digits> obj" and some whitespace.
constexpr std::size_t kHistory = 48;
constexpr std::string_view kTrailer = "trailer";

struct Candidate {
  std::uint32_t number;
  std::uint16_t generation;
  std::uint64_t offset;
};

struct ScanResult {
  std::vector<Candidate> objects;
  std::vector<std::uint64_t> trailers;  // offsets just past the "trailer" keyword
};

// The bytes already read, addressable by file offset for the last kHistory positions.
class History {
 public:
  void push(std::uint64_t offset, int byte) noexcept { bytes_[offset % kHistory] = byte; }
  // Byte at offset, or -1 when it is before the file or out of the window.
  [[nodiscard]] int at(std::uint64_t current, std::int64_t offset) const noexcept {
    if (offset < 0 || std::cmp_greater(offset, current) || current - static_cast<std::uint64_t>(offset) >= kHistory) {
      return -1;
    }
    return bytes_[static_cast<std::size_t>(offset) % kHistory];
  }

 private:
  std::array<int, kHistory> bytes_{};
};

constexpr bool is_digit(int c) noexcept { return c >= '0' && c <= '9'; }

// "obj" just ended at `pos`: reads "<number> <generation>" backwards. nullopt if it is not a definition.
std::optional<Candidate> definition_ending_at(const History& history, std::uint64_t pos) {
  auto q = static_cast<std::int64_t>(pos) - 3;  // the byte before "obj"
  const auto byte = [&](std::int64_t offset) { return history.at(pos, offset); };
  if (!parser::is_whitespace(byte(q))) {
    return std::nullopt;  // "endobj", "xobj", ...
  }
  while (parser::is_whitespace(byte(q))) {
    --q;
  }
  std::uint64_t generation = 0;
  std::uint64_t scale = 1;
  int digits = 0;
  for (; is_digit(byte(q)) && digits < 5; --q, ++digits, scale *= 10) {
    generation += static_cast<std::uint64_t>(byte(q) - '0') * scale;
  }
  if (digits == 0 || !parser::is_whitespace(byte(q))) {
    return std::nullopt;
  }
  while (parser::is_whitespace(byte(q))) {
    --q;
  }
  std::uint64_t number = 0;
  scale = 1;
  digits = 0;
  for (; is_digit(byte(q)) && digits < 10; --q, ++digits, scale *= 10) {
    number += static_cast<std::uint64_t>(byte(q) - '0') * scale;
  }
  // The number must start the token: nothing regular right before it (or the start of the file).
  if (digits == 0 || (q >= 0 && parser::is_regular(byte(q))) || (q >= 0 && byte(q) < 0)) {
    return std::nullopt;
  }
  if (number == 0 || number > std::numeric_limits<std::uint32_t>::max() || generation > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  return Candidate{
      .number = static_cast<std::uint32_t>(number), .generation = static_cast<std::uint16_t>(generation), .offset = static_cast<std::uint64_t>(q + 1)};
}

bool keyword_ends_at(const History& history, std::uint64_t pos, std::string_view keyword) {
  const auto start = static_cast<std::int64_t>(pos) - static_cast<std::int64_t>(keyword.size()) + 1;
  for (std::size_t i = 0; i < keyword.size(); ++i) {
    if (history.at(pos, start + static_cast<std::int64_t>(i)) != static_cast<unsigned char>(keyword[i])) {
      return false;
    }
  }
  const int before = history.at(pos, start - 1);
  return !parser::is_regular(before);
}

Result<ScanResult> scan(ByteSource& source) {
  ScanResult result;
  SourceReader reader(source);
  History history;
  // A definition takes at least "1 0 obj" (7 bytes): more candidates than that is not a real file.
  const std::uint64_t max_candidates = (source.size() / 7) + 1;
  for (std::uint64_t pos = 0;; ++pos) {
    const int c = reader.next();
    if (c == SourceReader::kEof) {
      break;
    }
    history.push(pos, c);
    const bool followed_by_boundary = !parser::is_regular(reader.peek());
    if (c == 'j' && followed_by_boundary && keyword_ends_at(history, pos, "obj")) {
      if (const auto candidate = definition_ending_at(history, pos); candidate && result.objects.size() < max_candidates) {
        result.objects.push_back(*candidate);
      }
    } else if (c == 'r' && followed_by_boundary && keyword_ends_at(history, pos, kTrailer)) {
      result.trailers.push_back(pos + 1);
    }
  }
  if (reader.io_error()) {
    return std::unexpected(*reader.io_error());
  }
  return result;
}

// Trailer keys that describe the document (not a cross-reference section).
bool is_document_key(std::string_view key) { return key == "Root" || key == "Info" || key == "ID" || key == "Encrypt"; }

}  // namespace

Result<parser::XrefTable> reconstruct_xref(ByteSource& source, const ParseLimits& limits) {
  auto scanned = scan(source);
  if (!scanned) {
    return std::unexpected(std::move(scanned.error()));
  }
  if (scanned->objects.empty()) {
    return make_error(ErrorCode::kSyntax, "no object definitions found while rebuilding the cross-reference table");
  }

  parser::XrefTable table;
  table.entries[0] = parser::XrefEntry{.type = parser::XrefEntryType::kFree, .generation = 65535};
  for (const Candidate& candidate : scanned->objects) {
    table.entries[candidate.number] = parser::XrefEntry{.type = parser::XrefEntryType::kInUse, .generation = candidate.generation, .offset = candidate.offset};
  }

  // Trailers, merged in file order (later incremental updates override earlier ones).
  Dict trailer;
  for (const std::uint64_t offset : scanned->trailers) {
    SourceReader reader(source);
    parser::ObjectParser parser(reader, limits, parser::ParseMode::kTolerant);
    if (!parser.seek(offset).has_value()) {
      continue;
    }
    auto object = parser.parse_object();
    if (!object || object->as_dict() == nullptr) {
      continue;
    }
    for (const auto& entry : object->as_dict()->entries()) {
      if (entry.key != "Prev" && entry.key != "XRefStm") {
        trailer.set(entry.key, entry.value);
      }
    }
  }

  // Look at every object once: cross-reference streams (trailer keys), catalogs (/Root), object
  // streams (compressed objects).
  const bool encrypted = trailer.contains("Encrypt");
  parser::XrefTable probe_table = table;
  parser::ObjectLoader loader(source, std::move(probe_table), limits, parser::ParseMode::kTolerant);
  Dict from_xref_streams;
  std::optional<model::ObjectRef> last_catalog;
  struct Compressed {
    std::uint32_t object;
    std::uint32_t stream;
    std::uint32_t index;
    std::uint64_t stream_offset;  // where the object stream is defined: the "time" of these definitions
  };
  std::vector<Compressed> compressed;
  for (const auto& [number, entry] : loader.xref().entries) {
    if (entry.type != parser::XrefEntryType::kInUse) {
      continue;
    }
    auto object = loader.load(number);
    if (!object) {
      continue;
    }
    const Dict* dict = object->as_dict();
    const model::Stream* stream = object->as_stream();
    if (stream != nullptr) {
      dict = &stream->dict;
    }
    if (dict == nullptr) {
      continue;
    }
    const Object* type = dict->find("Type");
    if (type != nullptr && type->is_name("Catalog")) {
      last_catalog = model::ObjectRef{.number = number, .generation = entry.generation};
    }
    if (stream != nullptr && type != nullptr && type->is_name("XRef")) {
      for (const auto& item : dict->entries()) {
        if (is_document_key(item.key)) {
          from_xref_streams.set(item.key, item.value);
        }
      }
    }
    if (stream != nullptr && type != nullptr && type->is_name("ObjStm") && !encrypted) {
      auto data = loader.decode_stream(*stream);
      if (!data) {
        continue;
      }
      auto directory = parser::ObjectStream::parse(std::move(*data), *dict, limits, parser::ParseMode::kTolerant);
      if (!directory) {
        continue;
      }
      for (std::size_t i = 0; i < directory->size(); ++i) {
        compressed.push_back(
            Compressed{.object = directory->number_at(i), .stream = number, .index = static_cast<std::uint32_t>(i), .stream_offset = entry.offset});
      }
    }
  }
  // The definition that comes later in the file wins, as with incremental updates. A direct
  // definition counts from its own offset, a compressed one from its object stream's offset.
  std::map<std::uint32_t, std::uint64_t> defined_at;
  for (const auto& [number, entry] : table.entries) {
    if (entry.type == parser::XrefEntryType::kInUse) {
      defined_at[number] = entry.offset;
    }
  }
  for (const Compressed& item : compressed) {
    const auto previous = defined_at.find(item.object);
    if (item.object == item.stream || (previous != defined_at.end() && previous->second >= item.stream_offset)) {
      continue;
    }
    table.entries[item.object] = parser::XrefEntry{.type = parser::XrefEntryType::kCompressed, .stream_number = item.stream, .stream_index = item.index};
    defined_at[item.object] = item.stream_offset;
  }

  for (const auto& item : from_xref_streams.entries()) {
    if (!trailer.contains(item.key)) {
      trailer.set(item.key, item.value);
    }
  }
  if (!trailer.contains("Root") && last_catalog) {
    trailer.set("Root", *last_catalog);
  }
  if (!trailer.contains("Root")) {
    return make_error(ErrorCode::kSyntax, "no document catalog found while rebuilding the cross-reference table");
  }
  trailer.set("Size", static_cast<std::int64_t>(table.entries.rbegin()->first) + 1);

  table.trailer = std::move(trailer);
  table.sections.push_back(parser::XrefSection{.offset = 0, .form = parser::XrefForm::kReconstructed, .entry_count = table.entries.size()});
  return table;
}

}  // namespace capriole::repair
