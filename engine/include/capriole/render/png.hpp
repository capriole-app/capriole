#pragma once

// PNG output of a rendered page (ISO/IEC 15948): 8-bit RGBA, not interlaced, every row filtered with
// "Up" (pages are mostly flat colour, which that filter turns into runs of zeros for deflate).

#include <cstddef>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/render/raster.hpp"

namespace capriole::render {

// kOutOfRange when the bitmap's size does not match its pixels; kLimitExceeded when zlib cannot get
// its memory.
[[nodiscard]] Result<std::vector<std::byte>> encode_png(const Bitmap& bitmap);

}  // namespace capriole::render
