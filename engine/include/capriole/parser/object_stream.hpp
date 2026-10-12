#pragma once

// Object streams (ISO 32000-2 §7.5.7): many non-stream objects compressed together in one stream.
// The decoded data starts with N pairs "object-number offset", followed at /First by the objects.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/model/object.hpp"
#include "capriole/parser/object_parser.hpp"

namespace capriole::parser {

class ObjectStream {
 public:
  // Parses the directory of an object stream from its decoded data and its dictionary (/N, /First).
  [[nodiscard]] static Result<ObjectStream> parse(std::vector<std::byte> decoded, const model::Dict& dict, const ParseLimits& limits = kDefaultParseLimits,
                                                  ParseMode mode = ParseMode::kStrict);

  [[nodiscard]] std::size_t size() const noexcept { return directory_.size(); }
  // Object number of the object at index (from the directory, without parsing the object).
  [[nodiscard]] std::uint32_t number_at(std::size_t index) const noexcept { return index < directory_.size() ? directory_[index].number : 0; }

  // Object number and value of the object at index.
  [[nodiscard]] Result<std::pair<std::uint32_t, model::Object>> object_at(std::size_t index) const;

 private:
  struct Entry {
    std::uint32_t number = 0;
    std::uint64_t offset = 0;  // relative to /First
  };

  ObjectStream(std::unique_ptr<MemoryByteSource> data, std::vector<Entry> directory, std::uint64_t first, const ParseLimits& limits, ParseMode mode);

  std::unique_ptr<MemoryByteSource> data_;
  std::vector<Entry> directory_;
  std::uint64_t first_ = 0;
  ParseLimits limits_;
  ParseMode mode_;
};

}  // namespace capriole::parser
