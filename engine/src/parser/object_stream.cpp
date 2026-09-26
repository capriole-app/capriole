#include "capriole/parser/object_stream.hpp"

#include <array>
#include <limits>
#include <string>
#include <utility>

#include "capriole/core/source_reader.hpp"
#include "capriole/parser/lexer.hpp"
#include "capriole/parser/object_parser.hpp"

namespace capriole::parser {

ObjectStream::ObjectStream(std::unique_ptr<MemoryByteSource> data, std::vector<Entry> directory, std::uint64_t first, const ParseLimits& limits)
    : data_(std::move(data)), directory_(std::move(directory)), first_(first), limits_(limits) {}

Result<ObjectStream> ObjectStream::parse(std::vector<std::byte> decoded, const model::Dict& dict, const ParseLimits& limits) {
  const model::Object* n_object = dict.find("N");
  const model::Object* first_object = dict.find("First");
  const auto n = n_object != nullptr ? n_object->as_integer() : std::nullopt;
  const auto first = first_object != nullptr ? first_object->as_integer() : std::nullopt;
  if (!n || !first || *n < 0 || *first < 0) {
    return make_error(ErrorCode::kSyntax, "object stream needs non-negative integers /N and /First");
  }
  const auto size = decoded.size();
  if (std::cmp_greater(*first, size)) {
    return make_error(ErrorCode::kSyntax, "object stream /First is beyond its data");
  }
  // Each directory pair takes at least "n o" plus a separator: a cheap bound before reserving.
  if (static_cast<std::uint64_t>(*n) > static_cast<std::uint64_t>(*first) / 2) {
    return make_error(ErrorCode::kSyntax, "object stream /N does not fit before /First");
  }

  auto data = std::make_unique<MemoryByteSource>(std::move(decoded));
  SourceReader reader(*data);
  Lexer lexer(reader, limits);
  std::vector<Entry> directory;
  directory.reserve(static_cast<std::size_t>(*n));
  for (std::int64_t i = 0; i < *n; ++i) {
    std::array<std::int64_t, 2> values{};
    for (std::int64_t& value : values) {
      auto token = lexer.next();
      if (!token) {
        return std::unexpected(std::move(token.error()));
      }
      if (token->kind != TokenKind::kInteger || token->offset + token->length > static_cast<std::uint64_t>(*first)) {
        return make_error(ErrorCode::kSyntax, "malformed object stream directory", token->offset);
      }
      value = token->integer;
    }
    const auto [number, offset] = values;
    if (number < 1 || !std::in_range<std::uint32_t>(number) || offset < 0 || static_cast<std::uint64_t>(offset) >= size - static_cast<std::uint64_t>(*first)) {
      return make_error(ErrorCode::kSyntax, "invalid entry in object stream directory");
    }
    if (!directory.empty() && std::cmp_less_equal(offset, directory.back().offset)) {
      return make_error(ErrorCode::kSyntax, "object stream offsets must increase");
    }
    directory.push_back(Entry{.number = static_cast<std::uint32_t>(number), .offset = static_cast<std::uint64_t>(offset)});
  }
  return ObjectStream(std::move(data), std::move(directory), static_cast<std::uint64_t>(*first), limits);
}

Result<std::pair<std::uint32_t, model::Object>> ObjectStream::object_at(std::size_t index) const {
  if (index >= directory_.size()) {
    return make_error(ErrorCode::kOutOfRange, "index " + std::to_string(index) + " beyond an object stream of " + std::to_string(directory_.size()));
  }
  const Entry& entry = directory_[index];
  SourceReader reader(*data_);
  ObjectParser parser(reader, limits_);
  if (auto moved = parser.seek(first_ + entry.offset); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  auto object = parser.parse_object();
  if (!object) {
    return std::unexpected(std::move(object.error()));
  }
  return std::pair{entry.number, std::move(*object)};
}

}  // namespace capriole::parser
