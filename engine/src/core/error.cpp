#include "capriole/core/error.hpp"

namespace capriole {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kIo:
      return "io";
    case ErrorCode::kOutOfRange:
      return "out_of_range";
    case ErrorCode::kUnexpectedEof:
      return "unexpected_eof";
    case ErrorCode::kSyntax:
      return "syntax";
    case ErrorCode::kUnsupported:
      return "unsupported";
    case ErrorCode::kLimitExceeded:
      return "limit_exceeded";
    case ErrorCode::kCorruptData:
      return "corrupt_data";
    case ErrorCode::kPasswordRequired:
      return "password_required";
  }
  return "unknown";
}

}  // namespace capriole
