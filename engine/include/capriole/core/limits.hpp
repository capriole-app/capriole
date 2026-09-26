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
  // Deepest nesting of arrays and dictionaries.
  std::size_t max_nesting_depth = 256;
};

inline constexpr ParseLimits kDefaultParseLimits{};

}  // namespace capriole
