#pragma once

// Rendering a display list to pixels with Blend2D (ADR 0002). What Blend2D lacks is added here:
// clipping to paths (a layer and an A8 mask, composited when the state is restored; axis-aligned
// rectangles are clipped natively), dashes (graphics/dash), the thin-line rule of the reference
// readers (nothing thinner than one device pixel), and transparency (§11): the separable blend modes
// are Blend2D's; the non-separable ones, soft masks and transparency groups are layers composited here
// with the formulas of render/blend. Groups are composed isolated (the same result as non-isolated
// unless blend modes inside them act on the backdrop), knockout is not applied.
//
// Text is drawn with its glyph outlines (in the display list). Shadings and patterns: not yet.

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
  // Layers alive at once (clips, transparency groups, soft masks, paints composited by hand); past it
  // clips fall back to their bounding box and groups are drawn straight onto what is below.
  std::size_t max_layers = 64;
  // All layers alive together, in pages: their pixels are the memory rendering takes beyond the page.
  double max_layer_area = 4;
  // Dashes per stroke (graphics/dash).
  std::size_t max_dash_segments = 1'000'000;
};

// The page at `scale` pixels per point (dpi / 72). kLimitExceeded when the bitmap would be too large
// or cannot be allocated.
[[nodiscard]] Result<Bitmap> rasterize(const graphics::DisplayList& list, double scale, const RenderLimits& limits = {});

}  // namespace capriole::render
