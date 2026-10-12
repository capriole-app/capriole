#pragma once

// Dashed lines (ISO 32000-2 §8.4.3.6), as paths: the rasterizer strokes the dashes like any path.
// Blend2D takes dash options but its stroker does not apply them (ADR 0002), so they are done here.

#include <cstddef>
#include <span>

#include "capriole/graphics/display_list.hpp"

namespace capriole::graphics {

struct DashedPath {
  Path path;
  // True when the dashes would have exceeded max_segments: the path is returned solid instead.
  bool too_many = false;
};

// The dashes of path: the pattern restarts at each subpath, shifted by phase; curves are flattened into
// lines no farther than tolerance (user space units) from the curve. An empty or all-zero pattern, or
// one with a negative entry, leaves the path solid. At most max_segments lines are produced — a tiny
// pattern over a long path would otherwise make millions of dashes.
[[nodiscard]] DashedPath dash_path(const Path& path, std::span<const double> pattern, double phase, double tolerance, std::size_t max_segments = 1'000'000);

}  // namespace capriole::graphics
