#include "capriole/parser/header.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>

namespace capriole::parser {

namespace {

constexpr std::string_view kMagic = "%PDF-";
// Longest header we accept after the magic: "<major>.<minor>" with up to 2 digits each.
constexpr std::size_t kMaxVersionChars = 5;

bool is_digit(std::byte b) noexcept { return b >= std::byte{'0'} && b <= std::byte{'9'}; }

int digit_value(std::byte b) noexcept { return static_cast<int>(b) - '0'; }

// Parses 1-2 decimal digits at pos, advancing it. Returns nullopt when no digit is present.
std::optional<int> parse_number(std::span<const std::byte> bytes, std::size_t& pos) {
  if (pos >= bytes.size() || !is_digit(bytes[pos])) {
    return std::nullopt;
  }
  int value = 0;
  for (std::size_t digits = 0; pos < bytes.size() && digits < 2 && is_digit(bytes[pos]); ++digits, ++pos) {
    value = value * 10 + digit_value(bytes[pos]);
  }
  return value;
}

}  // namespace

Result<PdfHeader> find_header(ByteSource& source) {
  std::array<std::byte, kHeaderSearchWindow + kMagic.size() + kMaxVersionChars> buffer{};
  auto read = source.read(0, buffer);
  if (!read) {
    return std::unexpected(read.error());
  }
  const auto bytes = std::span<const std::byte>(buffer).first(*read);

  const auto magic = std::as_bytes(std::span(kMagic));
  const auto found = std::ranges::search(bytes, magic);
  const auto header_offset = static_cast<std::size_t>(std::distance(bytes.begin(), found.begin()));
  if (found.empty() || header_offset > kHeaderSearchWindow) {
    return make_error(ErrorCode::kSyntax, "no %PDF- header in the first 1024 bytes", 0);
  }

  std::size_t pos = header_offset + kMagic.size();
  const auto major = parse_number(bytes, pos);
  if (!major || pos >= bytes.size() || bytes[pos] != std::byte{'.'}) {
    return make_error(ErrorCode::kSyntax, "malformed version in %PDF- header", header_offset);
  }
  ++pos;
  const auto minor = parse_number(bytes, pos);
  if (!minor) {
    return make_error(ErrorCode::kSyntax, "malformed version in %PDF- header", header_offset);
  }

  return PdfHeader{.major = *major, .minor = *minor, .offset = header_offset};
}

}  // namespace capriole::parser
