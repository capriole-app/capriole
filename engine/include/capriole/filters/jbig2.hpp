#pragma once

// JBIG2Decode (ISO 32000-2 §7.4.7): bilevel images coded with ITU-T T.88, embedded in PDF — segments
// without a file header, the page's in the stream and the shared ones in /JBIG2Globals. Our own
// decoder (the free ones are AGPL): generic regions (arithmetic with templates 0–3 and TPGDON, or MMR),
// generic refinement, symbol dictionaries and text regions (arithmetic or Huffman, with refinement and
// aggregation), pattern dictionaries and halftone regions, custom Huffman tables, striped pages.
//
// The output is the page, 1 bit per pixel, rows padded to a whole byte, 0 for black — the samples a
// 1-bit /DeviceGray image (or an image mask) reads, as PDF defines the filter's output.

#include <cstddef>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::filters {

// globals: the decoded /JBIG2Globals stream (empty when there is none). kLimitExceeded when the page,
// or the bitmaps decoded on the way, would exceed max_output bytes (at 1 bit per pixel).
[[nodiscard]] Result<std::vector<std::byte>> jbig2_decode(std::span<const std::byte> data, std::span<const std::byte> globals, std::size_t max_output);

}  // namespace capriole::filters
