#pragma once

// Phase 6c prototype (PLAN.md): one interface, two rasterizers (Skia, Blend2D) drawing the engine's
// display list, to compare them on speed, fidelity, WebAssembly size and build effort before choosing.
// Not part of the engine: built only with -DCAPRIOLE_RASTER_PROTO=ON.

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "capriole/graphics/display_list.hpp"

namespace capriole::experiments {

// Straight (not premultiplied) RGBA, row by row.
struct Bitmap {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

class RasterBackend {
 public:
  RasterBackend() = default;
  RasterBackend(const RasterBackend&) = delete;
  RasterBackend& operator=(const RasterBackend&) = delete;
  RasterBackend(RasterBackend&&) = delete;
  RasterBackend& operator=(RasterBackend&&) = delete;
  virtual ~RasterBackend() = default;

  [[nodiscard]] virtual std::string name() const = 0;
  // The page on white at `scale` pixels per point.
  [[nodiscard]] virtual Bitmap render(const graphics::DisplayList& list, double scale) = 0;
};

std::unique_ptr<RasterBackend> make_skia_backend();
std::unique_ptr<RasterBackend> make_blend2d_backend();

// ── Rules shared by both backends, so that they are compared on the same drawing ──────────────

// How much a matrix scales lengths (the square root of its determinant).
inline double length_scale(const graphics::Matrix& m) { return std::sqrt(std::abs(m.determinant())); }

// The stroke width to use in user space: never thinner than one device pixel, like the reference
// readers (a 0 width means the thinnest line, §8.4.3.2).
inline double stroke_width(double width, const graphics::Matrix& device) {
  const double scale = length_scale(device);
  if (scale <= 0) {
    return width;
  }
  return width * scale < 1 ? 1 / scale : width;
}

// The dash array with PDF semantics: an odd count repeats, all zeros means solid.
inline std::vector<double> dash_pattern(const std::vector<double>& dash) {
  bool any = false;
  for (const double d : dash) {
    any = any || d > 0;
  }
  if (!any) {
    return {};
  }
  std::vector<double> out = dash;
  if (out.size() % 2 == 1) {
    out.insert(out.end(), dash.begin(), dash.end());
  }
  return out;
}

// Text runs as boxes (the glyph outlines come with the fonts step): the box of each glyph, in the
// text element's space (x along the baseline, y up), and that space's matrix.
struct GlyphBox {
  double x = 0;
  double width = 0;
};

inline graphics::Matrix text_space(const graphics::ShowText& run) {
  const double scale = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
  const double rise = run.glyphs.empty() ? 0 : run.glyphs.front().origin.y;
  return graphics::Matrix{run.font_size * scale, 0, 0, run.font_size, 0, rise}.then(run.text_matrix);
}

inline std::vector<GlyphBox> glyph_boxes(const graphics::ShowText& run) {
  const double scale = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
  const double unit = run.font_size * scale;
  std::vector<GlyphBox> boxes;
  if (unit == 0) {
    return boxes;
  }
  for (const auto& glyph : run.glyphs) {
    boxes.push_back({glyph.origin.x / unit, std::max(glyph.advance / unit * 0.85, 0.05)});
  }
  return boxes;
}

inline bool visible_text(const graphics::ShowText& run) { return run.render_mode != 3 && run.render_mode != 7 && run.font_size != 0; }

}  // namespace capriole::experiments
