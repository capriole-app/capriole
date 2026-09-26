#pragma once

// Error model of the engine.
//
// The engine reads untrusted files, so failure is an ordinary outcome, not an exceptional one:
// every fallible operation returns Result<T> (std::expected) instead of throwing. Exceptions are
// reserved for programming errors and allocation failure, and are disabled in the WASM build.

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace capriole {

enum class ErrorCode : std::uint8_t {
  kIo,             // The underlying byte source failed.
  kOutOfRange,     // An offset or length points outside the source.
  kUnexpectedEof,  // The input ended before a complete construct was read.
  kSyntax,         // The input violates the PDF grammar.
  kUnsupported,    // Valid input using a feature the engine does not implement yet.
  kLimitExceeded,  // A resource limit (depth, size, count) guarding against hostile input.
};

std::string_view to_string(ErrorCode code) noexcept;

struct Error {
  static constexpr std::uint64_t kNoOffset = std::numeric_limits<std::uint64_t>::max();

  ErrorCode code;
  std::string message;
  // Byte offset in the source where the problem was detected, or kNoOffset.
  std::uint64_t offset = kNoOffset;
};

template <typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> make_error(ErrorCode code, std::string message, std::uint64_t offset = Error::kNoOffset) {
  return std::unexpected(Error{code, std::move(message), offset});
}

}  // namespace capriole
