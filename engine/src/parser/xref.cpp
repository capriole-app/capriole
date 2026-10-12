#include "capriole/parser/xref.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "capriole/core/source_reader.hpp"
#include "capriole/filters/decode.hpp"
#include "capriole/parser/lexer.hpp"
#include "capriole/parser/object_parser.hpp"

namespace capriole::parser {

namespace {

using model::Dict;
using model::Object;

// Classic xref entries are exactly 20 bytes: "nnnnnnnnnn ggggg n" + a two-byte EOL (§7.5.4).
constexpr std::size_t kTableEntryBytes = 20;
// startxref, the offset and %%EOF must sit in the final 1024 bytes; the margin lets the keyword
// itself start just before that window.
constexpr std::uint64_t kTailWindow = 1024 + 64;
constexpr std::size_t kMaxXrefFieldBytes = 8;

struct SectionData {
  XrefForm form = XrefForm::kTable;
  Dict trailer;
  std::map<std::uint32_t, XrefEntry> entries;
};

constexpr bool is_digit(int c) noexcept { return c >= '0' && c <= '9'; }

bool at(SourceReader& reader, std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (reader.peek(i) != static_cast<unsigned char>(text[i])) {
      return false;
    }
  }
  return true;
}

void skip_whitespace(SourceReader& reader) {
  while (is_whitespace(reader.peek())) {
    reader.advance();
  }
}

// Up to max_digits decimal digits; nullopt when there is none.
std::optional<std::uint64_t> read_digits(SourceReader& reader, std::size_t max_digits) {
  std::uint64_t value = 0;
  std::size_t count = 0;
  while (count < max_digits && is_digit(reader.peek())) {
    value = (value * 10) + static_cast<std::uint64_t>(reader.next() - '0');
    ++count;
  }
  if (count == 0) {
    return std::nullopt;
  }
  return value;
}

std::string at_offset(std::uint64_t offset) { return " (xref section at offset " + std::to_string(offset) + ")"; }

Result<std::int64_t> integer_entry(const Dict& dict, std::string_view key, bool required, std::int64_t fallback = 0) {
  const Object* value = dict.find(key);
  if (value == nullptr) {
    if (required) {
      return make_error(ErrorCode::kSyntax, "missing /" + std::string(key));
    }
    return fallback;
  }
  const auto integer = value->as_integer();
  if (!integer) {
    return make_error(ErrorCode::kSyntax, "/" + std::string(key) + " must be an integer");
  }
  return *integer;
}

// ── Classic table (§7.5.4) ──────────────────────────────────────────────────────────────────

Result<XrefEntry> parse_table_entry(const std::array<int, kTableEntryBytes>& e, std::uint64_t entry_offset) {
  const auto bad = [&](const char* what) { return make_error(ErrorCode::kSyntax, std::string("malformed xref entry: ") + what, entry_offset); };
  std::uint64_t first = 0;
  for (std::size_t i = 0; i < 10; ++i) {
    if (!is_digit(e[i])) {
      return bad("expected 10 digits");
    }
    first = (first * 10) + static_cast<std::uint64_t>(e[i] - '0');
  }
  std::uint64_t generation = 0;
  for (std::size_t i = 11; i < 16; ++i) {
    if (!is_digit(e[i])) {
      return bad("expected a 5-digit generation");
    }
    generation = (generation * 10) + static_cast<std::uint64_t>(e[i] - '0');
  }
  if (e[10] != ' ' || e[16] != ' ') {
    return bad("expected single spaces between fields");
  }
  const bool eol_ok = (e[18] == ' ' && (e[19] == '\r' || e[19] == '\n')) || (e[18] == '\r' && e[19] == '\n');
  if (!eol_ok) {
    return bad("entry must end with SP CR, SP LF or CR LF");
  }
  if (generation > std::numeric_limits<std::uint16_t>::max()) {
    return bad("generation above 65535");
  }
  XrefEntry entry;
  entry.generation = static_cast<std::uint16_t>(generation);
  if (e[17] == 'n') {
    entry.type = XrefEntryType::kInUse;
    entry.offset = first;
  } else if (e[17] == 'f') {
    entry.type = XrefEntryType::kFree;
  } else {
    return bad("type must be 'n' or 'f'");
  }
  return entry;
}

Result<SectionData> parse_table_section(ByteSource& source, std::uint64_t offset, const ParseLimits& limits) {
  SourceReader reader(source);
  if (auto moved = reader.seek(offset); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  reader.advance(4);  // "xref", checked by the caller
  if (!is_whitespace(reader.peek())) {
    return make_error(ErrorCode::kSyntax, "'xref' must be followed by an end-of-line", offset);
  }

  SectionData section;
  for (;;) {
    skip_whitespace(reader);
    if (at(reader, "trailer")) {
      break;
    }
    const std::uint64_t header_offset = reader.position();
    const auto start = read_digits(reader, 10);
    if (!start || reader.peek() != ' ') {
      return make_error(ErrorCode::kSyntax, "expected a subsection header 'start count' or 'trailer'", header_offset);
    }
    while (reader.peek() == ' ') {
      reader.advance();
    }
    const auto count = read_digits(reader, 10);
    if (!count) {
      return make_error(ErrorCode::kSyntax, "subsection header without an entry count", header_offset);
    }
    skip_whitespace(reader);
    if (*count > (reader.size() - reader.position()) / kTableEntryBytes) {
      return make_error(ErrorCode::kSyntax, "xref subsection is larger than the rest of the file", header_offset);
    }

    for (std::uint64_t i = 0; i < *count; ++i) {
      const std::uint64_t entry_offset = reader.position();
      std::array<int, kTableEntryBytes> bytes{};
      for (int& byte : bytes) {
        byte = reader.next();
      }
      auto entry = parse_table_entry(bytes, entry_offset);
      if (!entry) {
        return std::unexpected(std::move(entry.error()));
      }
      const std::uint64_t number = *start + i;
      if (number > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::kSyntax, "object number out of range in xref", entry_offset);
      }
      if (!section.entries.try_emplace(static_cast<std::uint32_t>(number), *entry).second) {
        return make_error(ErrorCode::kSyntax, "object " + std::to_string(number) + " listed twice in one xref section", entry_offset);
      }
    }
  }
  if (reader.io_error()) {
    return std::unexpected(*reader.io_error());
  }

  reader.advance(7);  // "trailer"
  ObjectParser parser(reader, limits);
  auto trailer = parser.parse_object();
  if (!trailer) {
    return std::unexpected(std::move(trailer.error()));
  }
  Dict* dict = trailer->as_dict();
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "'trailer' must be followed by a dictionary", offset);
  }
  section.trailer = std::move(*dict);
  return section;
}

// ── Cross-reference stream (§7.5.8) ─────────────────────────────────────────────────────────

Result<SectionData> parse_stream_section(ByteSource& source, std::uint64_t offset, const ParseLimits& limits) {
  SourceReader reader(source);
  ObjectParser parser(reader, limits);
  if (auto moved = parser.seek(offset); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  // All values in a cross-reference stream dictionary are direct (§7.5.8.2): no length resolver.
  auto object = parser.parse_indirect_object();
  if (!object) {
    return std::unexpected(std::move(object.error()));
  }
  const model::Stream* stream = object->object.as_stream();
  if (stream == nullptr || stream->dict.find("Type") == nullptr || !stream->dict.find("Type")->is_name("XRef")) {
    return make_error(ErrorCode::kSyntax, "expected a cross-reference stream (/Type /XRef)", offset);
  }
  const Dict& dict = stream->dict;

  auto size = integer_entry(dict, "Size", true);
  if (!size || *size < 0) {
    return make_error(ErrorCode::kSyntax, "cross-reference stream needs a non-negative /Size", offset);
  }

  const model::Array* w = dict.find("W") != nullptr ? dict.find("W")->as_array() : nullptr;
  if (w == nullptr || w->size() != 3) {
    return make_error(ErrorCode::kSyntax, "/W must be an array of three integers", offset);
  }
  std::array<std::size_t, 3> widths{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto width = (*w)[i].as_integer();
    if (!width || *width < 0 || std::cmp_greater(*width, kMaxXrefFieldBytes)) {
      return make_error(ErrorCode::kSyntax, "/W entries must be integers from 0 to 8", offset);
    }
    widths[i] = static_cast<std::size_t>(*width);
  }
  const std::size_t entry_size = widths[0] + widths[1] + widths[2];
  if (entry_size == 0) {
    return make_error(ErrorCode::kSyntax, "/W describes zero-byte entries", offset);
  }

  std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
  if (const Object* index = dict.find("Index")) {
    const model::Array* array = index->as_array();
    if (array == nullptr || array->size() % 2 != 0) {
      return make_error(ErrorCode::kSyntax, "/Index must be an array of pairs", offset);
    }
    for (std::size_t i = 0; i < array->size(); i += 2) {
      const auto first = (*array)[i].as_integer();
      const auto count = (*array)[i + 1].as_integer();
      if (!first || !count || *first < 0 || *count < 0) {
        return make_error(ErrorCode::kSyntax, "/Index pairs must be non-negative integers", offset);
      }
      ranges.emplace_back(*first, *count);
    }
  } else {
    ranges.emplace_back(0, *size);
  }

  auto encoded = filters::read_encoded(source, *stream);
  if (!encoded) {
    return std::unexpected(std::move(encoded.error()));
  }
  auto data = filters::decode(std::move(*encoded), dict, limits);
  if (!data) {
    return std::unexpected(std::move(data.error()));
  }

  // Counted with an early exit: /Index counts come from the file, and an overflowing product could
  // otherwise wrap around to exactly the data size.
  std::uint64_t total = 0;
  bool too_many = false;
  for (const auto& range : ranges) {
    total += static_cast<std::uint64_t>(range.second);
    if (total > data->size() / entry_size) {
      too_many = true;
      break;
    }
  }
  if (too_many || total * entry_size != data->size()) {
    return make_error(ErrorCode::kCorruptData, "cross-reference stream data does not match /W and /Index", offset);
  }

  SectionData section;
  section.form = XrefForm::kStream;
  std::size_t pos = 0;
  const auto field = [&](std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
      value = (value << 8U) | std::to_integer<std::uint64_t>((*data)[pos++]);
    }
    return value;
  };
  for (const auto& [first, count] : ranges) {
    for (std::int64_t j = 0; j < count; ++j) {
      const std::uint64_t number = static_cast<std::uint64_t>(first) + static_cast<std::uint64_t>(j);
      // A missing type field means type 1 (§7.5.8.3).
      const std::uint64_t type = widths[0] == 0 ? 1 : field(widths[0]);
      const std::uint64_t f2 = field(widths[1]);
      const std::uint64_t f3 = field(widths[2]);
      if (number > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::kSyntax, "object number out of range in cross-reference stream", offset);
      }
      XrefEntry entry;
      if (type == 1) {
        if (f3 > std::numeric_limits<std::uint16_t>::max()) {
          return make_error(ErrorCode::kSyntax, "generation above 65535 in cross-reference stream", offset);
        }
        entry.type = XrefEntryType::kInUse;
        entry.offset = f2;
        entry.generation = static_cast<std::uint16_t>(f3);
      } else if (type == 2) {
        if (f2 == 0 || f2 > std::numeric_limits<std::uint32_t>::max() || f3 > std::numeric_limits<std::uint32_t>::max()) {
          return make_error(ErrorCode::kSyntax, "invalid compressed-object entry in cross-reference stream", offset);
        }
        entry.type = XrefEntryType::kCompressed;
        entry.stream_number = static_cast<std::uint32_t>(f2);
        entry.stream_index = static_cast<std::uint32_t>(f3);
      } else {
        // Type 0 is free; any other type is a reference to the null object: both load as null.
        entry.type = XrefEntryType::kFree;
        entry.generation = type == 0 && f3 <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(f3) : 0;
      }
      if (!section.entries.try_emplace(static_cast<std::uint32_t>(number), entry).second) {
        return make_error(ErrorCode::kSyntax, "object " + std::to_string(number) + " listed twice in one cross-reference stream", offset);
      }
    }
  }
  section.trailer = dict;
  return section;
}

}  // namespace

const XrefEntry* XrefTable::find(std::uint32_t number) const {
  const auto it = entries.find(number);
  return it == entries.end() ? nullptr : &it->second;
}

Result<std::uint64_t> find_startxref(ByteSource& source) {
  const std::uint64_t size = source.size();
  const std::uint64_t window = std::min(size, kTailWindow);
  std::vector<std::byte> tail(static_cast<std::size_t>(window));
  auto read = source.read(size - window, tail);
  if (!read) {
    return std::unexpected(std::move(read.error()));
  }

  constexpr std::string_view kKeyword = "startxref";
  const auto keyword = std::as_bytes(std::span(kKeyword));
  const auto found = std::ranges::find_end(tail, keyword);
  if (found.empty()) {
    return make_error(ErrorCode::kSyntax, "no 'startxref' near the end of the file");
  }

  MemoryByteSource tail_source(std::vector<std::byte>(found.end(), tail.end()));
  SourceReader reader(tail_source);
  const std::uint64_t keyword_offset = size - window + static_cast<std::uint64_t>(found.begin() - tail.begin());
  skip_whitespace(reader);
  const auto offset = read_digits(reader, 19);
  if (!offset) {
    return make_error(ErrorCode::kSyntax, "'startxref' is not followed by an offset", keyword_offset);
  }
  skip_whitespace(reader);
  if (!at(reader, "%%EOF")) {
    return make_error(ErrorCode::kSyntax, "the startxref offset is not followed by %%EOF", keyword_offset);
  }
  if (*offset >= size) {
    return make_error(ErrorCode::kOutOfRange, "startxref points beyond the end of the file", keyword_offset);
  }
  return *offset;
}

Result<XrefTable> read_xref(ByteSource& source, const ParseLimits& limits) {
  auto startxref = find_startxref(source);
  if (!startxref) {
    return std::unexpected(std::move(startxref.error()));
  }

  XrefTable table;
  table.startxref = *startxref;
  std::set<std::uint64_t> visited;
  std::uint64_t offset = *startxref;
  for (;;) {
    if (!visited.insert(offset).second) {
      return make_error(ErrorCode::kSyntax, "cycle in the /Prev chain of cross-reference sections", offset);
    }
    if (table.sections.size() >= limits.max_xref_sections) {
      return make_error(ErrorCode::kLimitExceeded, "more cross-reference sections than max_xref_sections", offset);
    }
    if (offset >= source.size()) {
      return make_error(ErrorCode::kOutOfRange, "cross-reference section offset beyond the end of the file", offset);
    }

    SourceReader probe(source);
    const bool is_table = probe.seek(offset).has_value() && at(probe, "xref");
    auto section = is_table ? parse_table_section(source, offset, limits) : parse_stream_section(source, offset, limits);
    if (!section) {
      Error error = std::move(section.error());
      error.message += at_offset(offset);
      return std::unexpected(std::move(error));
    }

    // Hybrid file (§7.5.8.4): entries of the /XRefStm stream come after the table's own and before
    // the previous section's. The stream's own /Prev is ignored.
    if (is_table && section->trailer.contains("XRefStm")) {
      const auto stream_offset = section->trailer.find("XRefStm")->as_integer();
      if (!stream_offset || *stream_offset < 0) {
        return make_error(ErrorCode::kSyntax, "/XRefStm must be a non-negative integer" + at_offset(offset), offset);
      }
      auto hybrid = parse_stream_section(source, static_cast<std::uint64_t>(*stream_offset), limits);
      if (!hybrid) {
        Error error = std::move(hybrid.error());
        error.message += at_offset(static_cast<std::uint64_t>(*stream_offset));
        return std::unexpected(std::move(error));
      }
      for (const auto& [number, entry] : hybrid->entries) {
        section->entries.try_emplace(number, entry);
      }
      section->form = XrefForm::kHybrid;
    }

    for (const auto& [number, entry] : section->entries) {
      table.entries.try_emplace(number, entry);  // newer sections were merged first and win
    }
    if (table.sections.empty()) {
      table.trailer = section->trailer;
    }
    table.sections.push_back(XrefSection{.offset = offset, .form = section->form, .entry_count = section->entries.size()});

    const Object* prev = section->trailer.find("Prev");
    if (prev == nullptr) {
      return table;
    }
    const auto prev_offset = prev->as_integer();
    if (!prev_offset || *prev_offset < 0) {
      return make_error(ErrorCode::kSyntax, "/Prev must be a non-negative integer" + at_offset(offset), offset);
    }
    offset = static_cast<std::uint64_t>(*prev_offset);
  }
}

}  // namespace capriole::parser
