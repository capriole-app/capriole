#include "capriole/render/blend.hpp"

#include <algorithm>
#include <cmath>

namespace capriole::render {

namespace {

using graphics::BlendMode;

float multiply(float cb, float cs) { return cb * cs; }
float screen(float cb, float cs) { return cb + cs - (cb * cs); }

float hard_light(float cb, float cs) { return cs <= 0.5F ? multiply(cb, 2 * cs) : screen(cb, (2 * cs) - 1); }

float color_dodge(float cb, float cs) {
  if (cb <= 0) {
    return 0;
  }
  return cs < 1 ? std::min(1.0F, cb / (1 - cs)) : 1;
}

float color_burn(float cb, float cs) {
  if (cb >= 1) {
    return 1;
  }
  return cs > 0 ? 1 - std::min(1.0F, (1 - cb) / cs) : 0;
}

float soft_light(float cb, float cs) {
  if (cs <= 0.5F) {
    return cb - ((1 - (2 * cs)) * cb * (1 - cb));
  }
  const float d = cb <= 0.25F ? ((((16 * cb) - 12) * cb) + 4) * cb : std::sqrt(cb);
  return cb + (((2 * cs) - 1) * (d - cb));
}

float separable(BlendMode mode, float cb, float cs) {
  switch (mode) {
    case BlendMode::kMultiply:
      return multiply(cb, cs);
    case BlendMode::kScreen:
      return screen(cb, cs);
    case BlendMode::kOverlay:
      return hard_light(cs, cb);
    case BlendMode::kDarken:
      return std::min(cb, cs);
    case BlendMode::kLighten:
      return std::max(cb, cs);
    case BlendMode::kColorDodge:
      return color_dodge(cb, cs);
    case BlendMode::kColorBurn:
      return color_burn(cb, cs);
    case BlendMode::kHardLight:
      return hard_light(cb, cs);
    case BlendMode::kSoftLight:
      return soft_light(cb, cs);
    case BlendMode::kDifference:
      return std::abs(cb - cs);
    case BlendMode::kExclusion:
      return cb + cs - (2 * cb * cs);
    default:
      return cs;
  }
}

// The helpers of §11.3.5.3.
Rgbf clip_color(Rgbf c) {
  const float l = luminosity(c);
  const float n = std::min({c.r, c.g, c.b});
  const float x = std::max({c.r, c.g, c.b});
  const auto scale = [&](float v, float factor) { return l + ((v - l) * factor); };
  if (n < 0 && l - n > 0) {
    const float f = l / (l - n);
    c = {scale(c.r, f), scale(c.g, f), scale(c.b, f)};
  }
  if (x > 1 && x - l > 0) {
    const float f = (1 - l) / (x - l);
    c = {scale(c.r, f), scale(c.g, f), scale(c.b, f)};
  }
  return c;
}

Rgbf set_luminosity(Rgbf c, float l) {
  const float d = l - luminosity(c);
  return clip_color({c.r + d, c.g + d, c.b + d});
}

float saturation(Rgbf c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }

Rgbf set_saturation(Rgbf c, float s) {
  // Order the components, then stretch: the largest to s, the smallest to 0, the middle in proportion.
  float* max = &c.r;
  float* mid = &c.g;
  float* min = &c.b;
  if (*max < *mid) {
    std::swap(max, mid);
  }
  if (*mid < *min) {
    std::swap(mid, min);
  }
  if (*max < *mid) {
    std::swap(max, mid);
  }
  if (*max > *min) {
    *mid = ((*mid - *min) * s) / (*max - *min);
    *max = s;
  } else {
    *mid = 0;
    *max = 0;
  }
  *min = 0;
  return c;
}

}  // namespace

Rgbf blend(BlendMode mode, Rgbf backdrop, Rgbf source) noexcept {
  switch (mode) {
    case BlendMode::kNormal:
      return source;
    case BlendMode::kHue:
      return set_luminosity(set_saturation(source, saturation(backdrop)), luminosity(backdrop));
    case BlendMode::kSaturation:
      return set_luminosity(set_saturation(backdrop, saturation(source)), luminosity(backdrop));
    case BlendMode::kColor:
      return set_luminosity(source, luminosity(backdrop));
    case BlendMode::kLuminosity:
      return set_luminosity(backdrop, luminosity(source));
    default:
      return {separable(mode, backdrop.r, source.r), separable(mode, backdrop.g, source.g), separable(mode, backdrop.b, source.b)};
  }
}

PremultipliedRgba composite(BlendMode mode, PremultipliedRgba backdrop, PremultipliedRgba source) noexcept {
  const float as = source.a;
  const float ab = backdrop.a;
  if (as <= 0) {
    return backdrop;
  }
  PremultipliedRgba out{.r = 0, .g = 0, .b = 0, .a = as + ab - (as * ab)};
  Rgbf mixed{source.r / as, source.g / as, source.b / as};
  if (ab > 0 && mode != BlendMode::kNormal) {
    mixed = blend(mode, {backdrop.r / ab, backdrop.g / ab, backdrop.b / ab}, mixed);
  }
  // (1 − αs)·Cb·αb + (1 − αb)·Cs·αs + αs·αb·B, with B = Cs for Normal (the usual "over").
  out.r = ((1 - as) * backdrop.r) + ((1 - ab) * source.r) + (as * ab * mixed.r);
  out.g = ((1 - as) * backdrop.g) + ((1 - ab) * source.g) + (as * ab * mixed.g);
  out.b = ((1 - as) * backdrop.b) + ((1 - ab) * source.b) + (as * ab * mixed.b);
  return out;
}

}  // namespace capriole::render
