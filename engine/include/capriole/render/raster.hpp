#pragma once

// Rendering a display list to pixels with Blend2D (ADR 0002). What Blend2D lacks is added here:
// clipping to paths (a layer and an A8 mask, composited when the state is restored; axis-aligned
// rectangles are clipped natively), dashes (graphics/dash), and the thin-line rule of the reference
// readers (nothing thinner than one device pixel).
//
// Until the fonts and images steps: text runs are drawn as the boxes of their glyphs and images as
// placeholders, as in the SVG preview. Shadings, patterns, blend modes and soft masks: not yet.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/graphics/display_list.hpp"

namespace capriole::render {

// Straight (not premultiplied) RGBA, row after row; the page is opaque white underneath.
struct Bitmap {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

struct RenderLimits {
  // The bitmap: pages can declare any size, and a caller any resolution.
  std::int64_t max_pixels = 100'000'000;
  int max_side = 32'768;
  // Clip layers alive at once; deeper clips fall back to their bounding box.
  std::size_t max_layers = 64;
  // Dashes per stroke (graphics/dash).
  std::size_t max_dash_segments = 1'000'000;
};

// The page at `scale` pixels per point (dpi / 72). kLimitExceeded when the bitmap would be too large
// or cannot be allocated.
[[nodiscard]] Result<Bitmap> rasterize(const graphics::DisplayList& list, double scale, const RenderLimits& limits = {});

}  // namespace capriole::render
