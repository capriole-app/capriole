#pragma once

// The blend functions and the compositing formula of ISO 32000-2 §11.3, on colours with components in
// 0..1. The rasterizer uses Blend2D's own operators for the separable modes when nothing else is in
// the way, and these wherever it composites itself (the non-separable modes, soft masks, groups).

#include "capriole/graphics/display_list.hpp"

namespace capriole::render {

struct Rgbf {
  float r = 0;
  float g = 0;
  float b = 0;
  constexpr Rgbf() = default;
  constexpr Rgbf(float red, float green, float blue) noexcept : r(red), g(green), b(blue) {}
};

// Premultiplied: each colour component already multiplied by a.
struct PremultipliedRgba {
  float r = 0;
  float g = 0;
  float b = 0;
  float a = 0;
};

// B(Cb, Cs): the blend of a source colour over a backdrop colour (§11.3.5).
[[nodiscard]] Rgbf blend(graphics::BlendMode mode, Rgbf backdrop, Rgbf source) noexcept;

// The source composited over the backdrop with the blend mode (§11.3.6, for a single object):
//   αr = αs + αb − αs·αb,   Cr·αr = (1 − αs)·Cb·αb + (1 − αb)·Cs·αs + αs·αb·B(Cb, Cs)
[[nodiscard]] PremultipliedRgba composite(graphics::BlendMode mode, PremultipliedRgba backdrop, PremultipliedRgba source) noexcept;

// The luminosity of a colour (§11.3.5.3): 0.30 R + 0.59 G + 0.11 B.
[[nodiscard]] constexpr float luminosity(Rgbf c) noexcept { return (0.3F * c.r) + (0.59F * c.g) + (0.11F * c.b); }

}  // namespace capriole::render
