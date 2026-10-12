#pragma once

// The small standard filters (ISO 32000-2 §7.4.2, §7.4.3, §7.4.4, §7.4.5).
//
// All are strict: data that violates the filter's format — including a missing end-of-data marker —
// is ErrorCode::kCorruptData. Output is capped at max_output bytes (kLimitExceeded), since LZW and
// RunLength expand just like Flate does. lenient (repair layer only): a missing end-of-data marker or
// invalid data ends the output there, keeping what decoded so far.

#include <cstddef>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::filters {

// ASCIIHexDecode: hex digit pairs, whitespace ignored, '>' ends the data; an odd final digit is
// followed by an implied 0.
[[nodiscard]] Result<std::vector<std::byte>> ascii_hex_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient = false);

// ASCII85Decode: base-85 groups of '!'..'u', 'z' for four zero bytes, whitespace ignored, "~>" ends
// the data; a final partial group of n characters yields n - 1 bytes.
[[nodiscard]] Result<std::vector<std::byte>> ascii85_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient = false);

// LZWDecode: variable-width codes (9 to 12 bits), 256 = clear table, 257 = end of data. early_change
// is /EarlyChange (default 1): widen the code one entry early, as most encoders do.
[[nodiscard]] Result<std::vector<std::byte>> lzw_decode(std::span<const std::byte> input, bool early_change, std::size_t max_output, bool lenient = false);

// RunLengthDecode: length byte 0..127 copies the next n + 1 bytes, 129..255 repeats the next byte
// 257 - n times, 128 ends the data.
[[nodiscard]] Result<std::vector<std::byte>> run_length_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient = false);

}  // namespace capriole::filters
