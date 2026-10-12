#pragma once

// The pixels of a shading (ISO 32000-2 §8.7.4.5), for any output: the rasterizer paints them through the
// clip, the SVG preview embeds them. Each pixel is computed at its centre from the geometry — the
// position along the axis, the circle through the point, the point of the domain, the triangle it lies
// in — as the specification defines it, with no approximation beyond the colour ramp's sampling.

#include "capriole/graphics/display_list.hpp"

namespace capriole::graphics {

// width × height pixels of straight RGBA; to_pixels maps shading space to pixel coordinates (pixel
// (x, y) covers [x, x + 1] × [y, y + 1]). Where the shading paints nothing — past an axis or circles
// that are not extended, outside a function's domain or a mesh, outside /BBox — the pixel is transparent.
// /Background is not included: it belongs to pattern fills (PaintShading::background).
[[nodiscard]] ImagePixels shade(const Shading& shading, const Matrix& to_pixels, int width, int height);

}  // namespace capriole::graphics
