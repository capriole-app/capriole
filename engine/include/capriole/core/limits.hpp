#pragma once

// Resource limits applied while parsing untrusted input.
//
// Every quantity the file itself can make arbitrarily large (token length, string length, nesting
// depth) is capped here. Exceeding a cap is ErrorCode::kLimitExceeded, never a crash or an unbounded
// allocation. The defaults are far above anything a legitimate file needs (ISO 32000-2 Annex C lists
// much smaller implementation limits) and low enough that a hostile file cannot exhaust memory.

#include <cstddef>

namespace capriole {

struct ParseLimits {
  // Longest name, keyword or number, in bytes.
  std::size_t max_token_bytes = 4 * 1024;
  // Longest decoded string, in bytes.
  std::size_t max_string_bytes = 64 * 1024 * 1024;
  // Deepest nesting of arrays and dictionaries. Parsing recurses once per level, so this also bounds
  // stack use; the WebAssembly stack (1 MB, set in the root CMakeLists) must hold this depth, and the
  // WASM unit tests verify it with stack overflow checks enabled.
  std::size_t max_nesting_depth = 256;
  // Largest decoded stream, in bytes. Compression ratios can exceed 1000:1 (a "zip bomb" turns
  // kilobytes into gigabytes), so the output of every decoder is capped, not just its input.
  std::size_t max_decoded_stream_bytes = 512 * 1024 * 1024;
  // Longest chain of cross-reference sections followed through /Prev (one per incremental update).
  std::size_t max_xref_sections = 4096;
  // Deepest chain of indirect objects that must be loaded to load one object (e.g. an indirect
  // /Length whose object is itself a stream): stops hostile reference cycles.
  std::size_t max_load_depth = 16;
};

inline constexpr ParseLimits kDefaultParseLimits{};

}  // namespace capriole
