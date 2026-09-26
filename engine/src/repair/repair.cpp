#include "capriole/repair/repair.hpp"

#include "capriole/parser/header.hpp"

namespace capriole::repair {

std::string_view to_string(RepairKind kind) noexcept {
  switch (kind) {
    case RepairKind::kHeaderOffset:
      return "header_offset";
  }
  return "unknown";
}

bool may_help(const Error& error) noexcept {
  switch (error.code) {
    case ErrorCode::kSyntax:
    case ErrorCode::kOutOfRange:
    case ErrorCode::kUnexpectedEof:
    case ErrorCode::kCorruptData:
      return true;
    case ErrorCode::kIo:
    case ErrorCode::kUnsupported:
    case ErrorCode::kLimitExceeded:
    case ErrorCode::kPasswordRequired:
      return false;
  }
  return false;
}

std::optional<std::uint64_t> junk_before_header(ByteSource& source) {
  const auto header = parser::find_header(source);
  if (!header || header->offset == 0) {
    return std::nullopt;
  }
  return header->offset;
}

}  // namespace capriole::repair
