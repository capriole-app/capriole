#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capriole/capriole.h"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/version_config.hpp"
#include "capriole/parser/header.hpp"

namespace {

// Per-thread last error, the errno-style contract documented in capriole.h.
thread_local std::string g_last_error;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

capriole_status to_status(capriole::ErrorCode code) noexcept {
  switch (code) {
    case capriole::ErrorCode::kIo:
      return CAPRIOLE_ERROR_IO;
    case capriole::ErrorCode::kOutOfRange:
      return CAPRIOLE_ERROR_OUT_OF_RANGE;
    case capriole::ErrorCode::kUnexpectedEof:
      return CAPRIOLE_ERROR_UNEXPECTED_EOF;
    case capriole::ErrorCode::kSyntax:
      return CAPRIOLE_ERROR_SYNTAX;
    case capriole::ErrorCode::kUnsupported:
      return CAPRIOLE_ERROR_UNSUPPORTED;
    case capriole::ErrorCode::kLimitExceeded:
      return CAPRIOLE_ERROR_LIMIT_EXCEEDED;
    case capriole::ErrorCode::kCorruptData:
      return CAPRIOLE_ERROR_CORRUPT_DATA;
  }
  return CAPRIOLE_ERROR_INTERNAL;
}

capriole_status fail(capriole_status status, std::string message) {
  g_last_error = std::move(message);
  return status;
}

capriole_status fail(const capriole::Error& error) { return fail(to_status(error.code), error.message); }

}  // namespace

extern "C" {

const char* capriole_version(void) { return capriole::build::kVersionString.data(); }

int capriole_abi_version(void) { return CAPRIOLE_ABI_VERSION; }

const char* capriole_last_error_message(void) { return g_last_error.c_str(); }

capriole_status capriole_probe_header(const uint8_t* data, size_t size, int* major, int* minor) {
  g_last_error.clear();
  if ((data == nullptr && size != 0) || major == nullptr || minor == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    const auto bytes = std::as_bytes(std::span(data, size));
    capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
    const auto header = capriole::parser::find_header(source);
    if (!header) {
      return fail(header.error());
    }
    *major = header->major;
    *minor = header->minor;
    return CAPRIOLE_OK;
  } catch (...) {
    // Nothing may unwind across the C boundary; allocation failure is the realistic case here.
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

}  // extern "C"
