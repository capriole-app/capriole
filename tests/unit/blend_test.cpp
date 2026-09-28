#include "capriole/render/blend.hpp"

#include <gtest/gtest.h>

namespace capriole::render {
namespace {

using graphics::BlendMode;

void expect_near(Rgbf actual, Rgbf expected) {
  constexpr float kTolerance = 1e-4F;
  EXPECT_NEAR(actual.r, expected.r, kTolerance);
  EXPECT_NEAR(actual.g, expected.g, kTolerance);
  EXPECT_NEAR(actual.b, expected.b, kTolerance);
}

TEST(Blend, SeparableModes) {
  const Rgbf half{0.5F, 0.5F, 0.5F};
  expect_near(blend(BlendMode::kNormal, half, {0.2F, 0.4F, 0.6F}), {0.2F, 0.4F, 0.6F});
  expect_near(blend(BlendMode::kMultiply, half, half), {0.25F, 0.25F, 0.25F});
  expect_near(blend(BlendMode::kScreen, half, half), {0.75F, 0.75F, 0.75F});
  expect_near(blend(BlendMode::kDarken, {0.2F, 0.8F, 0.5F}, half), {0.2F, 0.5F, 0.5F});
  expect_near(blend(BlendMode::kLighten, {0.2F, 0.8F, 0.5F}, half), {0.5F, 0.8F, 0.5F});
  expect_near(blend(BlendMode::kDifference, {0.2F, 0.8F, 0.5F}, half), {0.3F, 0.3F, 0.0F});
  expect_near(blend(BlendMode::kExclusion, {1, 0, 0.5F}, half), {0.5F, 0.5F, 0.5F});
  // Overlay is HardLight with the operands swapped.
  expect_near(blend(BlendMode::kOverlay, {0.25F, 0.75F, 0}, {0.6F, 0.6F, 0.6F}), blend(BlendMode::kHardLight, {0.6F, 0.6F, 0.6F}, {0.25F, 0.75F, 0}));
  expect_near(blend(BlendMode::kHardLight, half, {0.25F, 0.75F, 1}), {0.25F, 0.75F, 1});
  // The edge cases of §11.3.5.2.
  expect_near(blend(BlendMode::kColorDodge, {0, 0.5F, 0.5F}, {1, 1, 0.5F}), {0, 1, 1});
  expect_near(blend(BlendMode::kColorBurn, {1, 0.5F, 0.5F}, {0, 0, 0.5F}), {1, 0, 0});
  // SoftLight with Cb ≤ 0.25 uses the polynomial D(Cb): D(0.25) = 0.5.
  expect_near(blend(BlendMode::kSoftLight, {0.25F, 0.64F, 0.5F}, {0.75F, 0.75F, 0.25F}), {0.375F, 0.72F, 0.375F});
}

TEST(Blend, NonSeparableModes) {
  const Rgbf red{1, 0, 0};
  const Rgbf gray{0.5F, 0.5F, 0.5F};
  // Luminosity: the backdrop's hue and saturation with the source's luminosity, clipped into gamut.
  expect_near(blend(BlendMode::kLuminosity, red, gray), {1, 0.5F - (0.3F * 0.5F / 0.7F), 0.5F - (0.3F * 0.5F / 0.7F)});
  EXPECT_NEAR(luminosity(blend(BlendMode::kLuminosity, red, gray)), 0.5F, 1e-4F);
  // Color: the source's hue and saturation with the backdrop's luminosity.
  EXPECT_NEAR(luminosity(blend(BlendMode::kColor, gray, red)), 0.5F, 1e-4F);
  // Saturation from a gray source: no saturation at all, the backdrop's luminosity.
  expect_near(blend(BlendMode::kSaturation, red, gray), {0.3F, 0.3F, 0.3F});
  // Hue onto a gray backdrop: a gray has no saturation to give it.
  expect_near(blend(BlendMode::kHue, gray, red), gray);
}

TEST(Blend, CompositeIsSourceOverForNormal) {
  // Half-transparent red (premultiplied) over opaque white.
  const PremultipliedRgba out = composite(BlendMode::kNormal, {1, 1, 1, 1}, {0.5F, 0, 0, 0.5F});
  EXPECT_NEAR(out.r, 1, 1e-5F);
  EXPECT_NEAR(out.g, 0.5F, 1e-5F);
  EXPECT_NEAR(out.b, 0.5F, 1e-5F);
  EXPECT_NEAR(out.a, 1, 1e-5F);
  // Over nothing, the source is kept as it is, whatever the mode.
  const PremultipliedRgba alone = composite(BlendMode::kMultiply, {0, 0, 0, 0}, {0.2F, 0.1F, 0, 0.4F});
  EXPECT_NEAR(alone.r, 0.2F, 1e-5F);
  EXPECT_NEAR(alone.a, 0.4F, 1e-5F);
  // Opaque multiply: B(Cb, Cs).
  const PremultipliedRgba multiplied = composite(BlendMode::kMultiply, {0.5F, 1, 1, 1}, {0.5F, 0.5F, 1, 1});
  EXPECT_NEAR(multiplied.r, 0.25F, 1e-5F);
  EXPECT_NEAR(multiplied.g, 0.5F, 1e-5F);
  EXPECT_NEAR(multiplied.b, 1, 1e-5F);
}

}  // namespace
}  // namespace capriole::render
