#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "capriole/writer/save.hpp"
#include "capriole/writer/serializer.hpp"
#include "save_support.hpp"

namespace capriole::writer {

namespace {

using model::Object;

// One entry of the new cross-reference section.
struct Entry {
  bool in_use = true;
  std::uint64_t value = 0;  // in use: byte offset; free: number of the next free object
  std::uint16_t generation = 0;
};

// Copies the source to the sink in chunks (the file is never held in memory as a whole). Returns the
// last byte copied, to know whether the file ends with an end-of-line.
Result<std::byte> copy_source(ByteSource& source, ByteSink& sink) {
  constexpr std::size_t kChunk = std::size_t{64} * 1024;
  std::vector<std::byte> buffer(kChunk);
  std::byte last{'\n'};
  for (std::uint64_t offset = 0; offset < source.size();) {
    auto read = source.read(offset, buffer);
    if (!read) {
      return std::unexpected(std::move(read.error()));
    }
    if (*read == 0) {
      return make_error(ErrorCode::kUnexpectedEof, "the source ended before its size", offset);
    }
    const std::span<const std::byte> chunk(buffer.data(), *read);
    if (auto ok = sink.write(chunk); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    last = chunk.back();
    offset += *read;
  }
  return last;
}

// Runs of consecutive object numbers: (first, count) pairs, for the subsections (§7.5.4) or /Index.
std::vector<std::pair<std::uint32_t, std::uint32_t>> runs(const std::map<std::uint32_t, Entry>& entries) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  for (const auto& [number, entry] : entries) {
    if (!result.empty() && result.back().first + result.back().second == number) {
      ++result.back().second;
    } else {
      result.emplace_back(number, 1);
    }
  }
  return result;
}

std::string xref_table(const std::map<std::uint32_t, Entry>& entries) {
  std::string text = "xref\n";
  auto it = entries.begin();
  for (const auto& [first, count] : runs(entries)) {
    text += std::format("{} {}\n", first, count);
    for (std::uint32_t i = 0; i < count; ++i, ++it) {
      text += std::format("{:010} {:05} {} \n", it->second.value, it->second.generation, it->second.in_use ? 'n' : 'f');
    }
  }
  return text;
}

// The data of a cross-reference stream (§7.5.8.3) with field widths [1 width 2], unfiltered.
std::vector<std::byte> xref_stream_data(const std::map<std::uint32_t, Entry>& entries, int width) {
  std::vector<std::byte> data;
  const auto put = [&](std::uint64_t value, int bytes) {
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
      data.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
  };
  for (const auto& [number, entry] : entries) {
    put(entry.in_use ? 1 : 0, 1);
    put(entry.value, width);
    put(entry.generation, 2);
  }
  return data;
}

int bytes_for(std::uint64_t value) {
  int bytes = 1;
  while (bytes < 8 && (value >> (bytes * 8)) != 0) {
    ++bytes;
  }
  return bytes;
}

}  // namespace

Result<SaveReport> save_incremental(Document& document, ByteSink& output, const SaveOptions& options) {
  // Page tree repairs are found when the pages are first read: read them now, so that the decision
  // does not depend on what the caller did before. (A tree that cannot be read at all is left as it is.)
  [[maybe_unused]] const auto pages = document.page_refs();
  if (!document.repairs().empty()) {
    return make_error(ErrorCode::kUnsupported, "the document needed repairs: an incremental save would keep them unrepaired (use a full save)");
  }
  const auto& sections = document.xref().sections;
  if (sections.empty() || sections.front().form == parser::XrefForm::kReconstructed) {
    return make_error(ErrorCode::kUnsupported, "the document has no cross-reference section to append to (use a full save)");
  }

  SaveReport report;
  const std::vector<std::uint32_t> changed = document.changed_objects();
  if (changed.empty() && !document.trailer_modified() && !options.modification_date) {
    if (auto copied = copy_source(document.source(), output); !copied) {
      return std::unexpected(std::move(copied.error()));
    }
    return report;  // nothing to append: the file as it is
  }

  // What the update holds: the changed objects, and /Info when the modification date goes in it.
  model::Dict trailer = document.trailer();
  std::uint32_t size = document.next_object_number();  // grows with the objects the save adds
  std::map<std::uint32_t, Object> objects;
  std::vector<std::uint32_t> deleted;
  for (const std::uint32_t number : changed) {
    if (document.state(number) == ObjectState::kDeleted) {
      deleted.push_back(number);
      continue;
    }
    auto object = document.get(number);
    if (!object) {
      return std::unexpected(std::move(object.error()));
    }
    objects.emplace(number, **object);
  }
  if (options.modification_date) {
    const Object* info_entry = trailer.find("Info");
    const auto info = info_entry != nullptr ? info_entry->as_ref() : std::nullopt;
    auto info_object = info ? document.get(*info) : Result<const Object*>(nullptr);
    if (info && info_object && (*info_object)->as_dict() != nullptr) {
      model::Dict dict = *(*info_object)->as_dict();
      dict.set("ModDate", model::String{.bytes = *options.modification_date});
      objects.insert_or_assign(info->number, Object(std::move(dict)));
    } else {
      model::Dict dict;
      dict.set("ModDate", model::String{.bytes = *options.modification_date});
      const std::uint32_t number = size++;
      objects.emplace(number, Object(std::move(dict)));
      trailer.set("Info", model::ObjectRef{.number = number, .generation = 0});
    }
  }

  detail::HashingSink sink(output);
  auto last = copy_source(document.source(), sink);
  if (!last) {
    return std::unexpected(std::move(last.error()));
  }
  if (*last != std::byte{'\n'} && *last != std::byte{'\r'}) {
    if (auto ok = sink.write(std::string_view("\n")); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
  }

  std::map<std::uint32_t, Entry> entries;
  for (const auto& [number, object] : objects) {
    const std::uint16_t generation = document.state(number) == ObjectState::kNew ? 0 : document.generation(number);
    const std::uint64_t offset = sink.position();
    if (auto ok = detail::write_indirect(document, sink, model::ObjectRef{.number = number, .generation = generation}, object); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    entries[number] = Entry{.in_use = true, .value = offset, .generation = generation};
    ++report.objects_written;
  }
  // Deleted objects: free, with the generation up by one so that old references stay dead (§7.5.4),
  // chained from object 0 in ascending order.
  if (!deleted.empty()) {
    entries[0] = Entry{.in_use = false, .value = deleted.front(), .generation = 65535};
    for (std::size_t i = 0; i < deleted.size(); ++i) {
      const std::uint32_t next = i + 1 < deleted.size() ? deleted[i + 1] : 0;
      const auto generation = static_cast<std::uint16_t>(std::min<std::uint32_t>(document.generation(deleted[i]) + 1U, 65535));
      entries[deleted[i]] = Entry{.in_use = false, .value = next, .generation = generation};
    }
  }

  // The new section, in the form of the file's last one (a hybrid file's last section is a table).
  const auto previous = static_cast<std::int64_t>(document.xref().startxref);
  if (sections.front().form == parser::XrefForm::kStream) {
    const std::uint32_t number = size++;
    const std::uint64_t offset = sink.position();
    entries[number] = Entry{.in_use = true, .value = offset, .generation = 0};
    std::uint64_t widest = 0;
    for (const auto& [n, entry] : entries) {
      widest = std::max(widest, entry.value);
    }
    const int width = bytes_for(widest);
    model::Array index;
    for (const auto& [first, count] : runs(entries)) {
      index.emplace_back(static_cast<std::int64_t>(first));
      index.emplace_back(static_cast<std::int64_t>(count));
    }
    model::Stream stream;
    stream.dict = trailer;
    stream.dict.set("Type", model::Name{.bytes = "XRef"});
    stream.dict.set("Size", static_cast<std::int64_t>(size));
    stream.dict.set("Index", std::move(index));
    stream.dict.set("W", model::Array{1, width, 2});
    stream.dict.set("Prev", previous);
    stream.dict.set("ID", detail::file_identifier(trailer, sink.digest(), document.security() != nullptr));
    stream.memory_data = std::make_shared<const std::vector<std::byte>>(xref_stream_data(entries, width));
    if (auto ok = detail::write_indirect(document, sink, model::ObjectRef{.number = number, .generation = 0}, Object(std::move(stream))); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    if (auto ok = sink.write(std::format("startxref\n{}\n%%EOF\n", offset)); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    return report;
  }

  const std::uint64_t offset = sink.position();
  trailer.set("Size", static_cast<std::int64_t>(size));
  trailer.set("Prev", previous);
  std::string text = xref_table(entries);
  // The identifier hashes everything up to the trailer, the new cross-reference section included.
  if (auto ok = sink.write(text); !ok) {
    return std::unexpected(std::move(ok.error()));
  }
  trailer.set("ID", detail::file_identifier(trailer, sink.digest(), document.security() != nullptr));
  text = "trailer\n";
  serialize(Object(std::move(trailer)), text);
  text += std::format("\nstartxref\n{}\n%%EOF\n", offset);
  if (auto ok = sink.write(text); !ok) {
    return std::unexpected(std::move(ok.error()));
  }
  return report;
}

}  // namespace capriole::writer
