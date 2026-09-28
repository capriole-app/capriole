#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "capriole/graphics/dash.hpp"
#include "capriole/render/blend.hpp"
#include "capriole/render/raster.hpp"

namespace capriole::render {
namespace {

using namespace capriole::graphics;

Path rect(double x, double y, double w, double h) {
  Path p;
  p.move_to({x, y});
  p.line_to({x + w, y});
  p.line_to({x + w, y + h});
  p.line_to({x, y + h});
  p.close();
  return p;
}

// A circle of four cubics (the usual 0.5523 control distance).
Path circle(double cx, double cy, double r) {
  const double k = 0.5523 * r;
  Path p;
  p.move_to({cx + r, cy});
  p.cubic_to({cx + r, cy + k}, {cx + k, cy + r}, {cx, cy + r});
  p.cubic_to({cx - k, cy + r}, {cx - r, cy + k}, {cx - r, cy});
  p.cubic_to({cx - r, cy - k}, {cx - k, cy - r}, {cx, cy - r});
  p.cubic_to({cx + k, cy - r}, {cx + r, cy - k}, {cx + r, cy});
  p.close();
  return p;
}

DisplayList page(double w, double h) {
  DisplayList list;
  list.media_box = list.crop_box = Rect{0, 0, w, h};
  list.colors = {Color{.family = ColorSpaceFamily::kRGB, .components = {1, 0, 0}, .pattern = {}, .rgb = {1, 0, 0}},
                 Color{.family = ColorSpaceFamily::kRGB, .components = {0, 0, 1}, .pattern = {}, .rgb = {0, 0, 1}},
                 Color{.family = ColorSpaceFamily::kGray, .components = {0}, .pattern = {}, .rgb = {0, 0, 0}}};
  list.styles = {StrokeStyle{}};
  return list;
}

constexpr ColorIndex kRed = 0;
constexpr ColorIndex kBlue = 1;
constexpr ColorIndex kBlack = 2;

// The pixel at page coordinates (x, y) — the bitmap grows downwards, the page upwards.
std::array<std::uint8_t, 4> at(const Bitmap& b, double x, double y) {
  const int col = static_cast<int>(x);
  const int row = b.height - 1 - static_cast<int>(y);
  const std::size_t i = (static_cast<std::size_t>(row) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(col)) * 4;
  return {b.rgba[i], b.rgba[i + 1], b.rgba[i + 2], b.rgba[i + 3]};
}

constexpr std::array<std::uint8_t, 4> kWhite{255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> kRedPixel{255, 0, 0, 255};
constexpr std::array<std::uint8_t, 4> kBluePixel{0, 0, 255, 255};

Bitmap render(const DisplayList& list, double scale = 1) {
  auto bitmap = rasterize(list, scale);
  EXPECT_TRUE(bitmap.has_value()) << (bitmap ? "" : bitmap.error().message);
  return bitmap ? std::move(*bitmap) : Bitmap{};
}

TEST(Raster, FillsWithThePageUpsideUp) {
  DisplayList list = page(100, 50);
  list.commands.emplace_back(FillPath{.path = rect(10, 10, 20, 20), .ctm = {}, .rule = FillRule::kNonZero, .color = kRed, .alpha = 1});
  const Bitmap b = render(list);
  EXPECT_EQ(b.width, 100);
  EXPECT_EQ(b.height, 50);
  EXPECT_EQ(at(b, 20, 20), kRedPixel);
  EXPECT_EQ(at(b, 20, 40), kWhite);  // above the square on the page
  EXPECT_EQ(at(b, 5, 5), kWhite);
}

TEST(Raster, EvenOddLeavesTheHole) {
  DisplayList list = page(100, 100);
  Path ring = rect(0, 0, 100, 100);
  ring.move_to({25, 25});
  ring.line_to({75, 25});
  ring.line_to({75, 75});
  ring.line_to({25, 75});
  ring.close();
  list.commands.emplace_back(FillPath{.path = ring, .ctm = {}, .rule = FillRule::kEvenOdd, .color = kBlue, .alpha = 1});
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 50, 50), kWhite);
  EXPECT_EQ(at(b, 10, 10), kBluePixel);
}

TEST(Raster, ClipToACurveAndToARectangle) {
  DisplayList list = page(100, 100);
  list.commands.emplace_back(Save{});
  list.commands.emplace_back(ClipPath{.path = circle(50, 50, 30), .ctm = {}, .rule = FillRule::kNonZero});  // a mask layer
  list.commands.emplace_back(FillPath{.path = rect(0, 0, 100, 100), .ctm = {}, .rule = FillRule::kNonZero, .color = kBlue, .alpha = 1});
  list.commands.emplace_back(Restore{});
  list.commands.emplace_back(Save{});
  list.commands.emplace_back(ClipPath{.path = rect(0, 0, 10, 10), .ctm = {}, .rule = FillRule::kNonZero});  // a native rectangle clip
  list.commands.emplace_back(FillPath{.path = rect(0, 0, 100, 100), .ctm = {}, .rule = FillRule::kNonZero, .color = kRed, .alpha = 1});
  list.commands.emplace_back(Restore{});
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 50, 50), kBluePixel);  // inside the circle
  EXPECT_EQ(at(b, 90, 90), kWhite);      // outside it: the corner
  EXPECT_EQ(at(b, 5, 5), kRedPixel);     // inside the rectangle clip
  EXPECT_EQ(at(b, 15, 50), kWhite);      // outside both
}

TEST(Raster, ClipEndsWithItsState) {
  DisplayList list = page(100, 100);
  list.commands.emplace_back(Save{});
  list.commands.emplace_back(ClipPath{.path = circle(20, 20, 10), .ctm = {}, .rule = FillRule::kNonZero});
  list.commands.emplace_back(Restore{});
  list.commands.emplace_back(FillPath{.path = rect(60, 60, 20, 20), .ctm = {}, .rule = FillRule::kNonZero, .color = kRed, .alpha = 1});
  EXPECT_EQ(at(render(list), 70, 70), kRedPixel);
}

TEST(Raster, AlphaBlendsOverTheWhitePage) {
  DisplayList list = page(20, 20);
  list.commands.emplace_back(FillPath{.path = rect(0, 0, 20, 20), .ctm = {}, .rule = FillRule::kNonZero, .color = kBlack, .alpha = 0.5});
  const auto pixel = at(render(list), 10, 10);
  EXPECT_NEAR(pixel[0], 128, 2);
  EXPECT_EQ(pixel[3], 255);
}

TEST(Raster, ThinLinesStayVisibleAndDashesLeaveGaps) {
  DisplayList list = page(100, 40);
  Path line;
  line.move_to({0, 10.5});
  line.line_to({100, 10.5});
  list.commands.emplace_back(StrokePath{.path = line, .ctm = {}, .style = 0, .color = kBlack, .alpha = 1});
  StrokeStyle dashed;
  dashed.width = 4;
  dashed.dash = {10, 10};
  list.styles.push_back(dashed);
  Path top;
  top.move_to({0, 30});
  top.line_to({100, 30});
  list.commands.emplace_back(StrokePath{.path = top, .ctm = Matrix::scale(1, 1), .style = 1, .color = kBlack, .alpha = 1});
  // The thin line: width 1 at scale 0.1 would be a tenth of a pixel — drawn at one pixel instead.
  const Bitmap b = render(list);
  EXPECT_LT(at(b, 50, 10.5)[0], 128);
  EXPECT_LT(at(b, 5, 30)[0], 64);    // a dash
  EXPECT_EQ(at(b, 15, 30), kWhite);  // a gap
  EXPECT_LT(at(b, 25, 30)[0], 64);   // the next dash
  DisplayList thin = page(100, 40);
  thin.styles[0].width = 0.1;
  thin.commands.emplace_back(StrokePath{.path = line, .ctm = {}, .style = 0, .color = kBlack, .alpha = 1});
  EXPECT_LT(at(render(thin), 50, 10.5)[0], 200);
}

TEST(Raster, RotatedPagesSwapTheirSides) {
  DisplayList list = page(200, 100);
  list.rotate = 90;
  const Bitmap b = render(list, 0.5);
  EXPECT_EQ(b.width, 50);
  EXPECT_EQ(b.height, 100);
}

TEST(Raster, LimitsRefuseHugeBitmaps) {
  DisplayList list = page(100000, 100000);
  EXPECT_EQ(rasterize(list, 1).error().code, ErrorCode::kLimitExceeded);
  EXPECT_EQ(rasterize(page(10, 10), 0).error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(rasterize(page(10, 10), std::numeric_limits<double>::infinity()).error().code, ErrorCode::kOutOfRange);
}

// ── Dashes ──────────────────────────────────────────────────────────────────────────────────

std::size_t subpaths(const Path& p) {
  std::size_t n = 0;
  for (const SegmentKind k : p.verbs()) {
    n += k == SegmentKind::kMove ? 1 : 0;
  }
  return n;
}

TEST(Dash, SplitsALineByThePattern) {
  Path line;
  line.move_to({0, 0});
  line.line_to({100, 0});
  const std::vector<double> pattern{10, 5};
  EXPECT_EQ(subpaths(dash_path(line, pattern, 0, 0.1).path), 7U);   // 0-10, 15-25, ..., 90-100
  EXPECT_EQ(subpaths(dash_path(line, pattern, 12, 0.1).path), 7U);  // starts in a gap
  const std::vector<double> odd{10};                                // odd: [10 10]
  EXPECT_EQ(subpaths(dash_path(line, odd, 0, 0.1).path), 5U);
  const std::vector<double> zeros{0, 0};
  EXPECT_EQ(dash_path(line, zeros, 0, 0.1).path.size(), line.size());  // solid
  const std::vector<double> negative{5, -1};
  EXPECT_EQ(dash_path(line, negative, 0, 0.1).path.size(), line.size());
}

// ── Transparency ─────────────────────────────────────────────────────────────────────────────

FillPath fill_of(const Path& path, ColorIndex color, double alpha = 1, BlendMode blend = BlendMode::kNormal) {
  return FillPath{.path = path, .ctm = {}, .rule = FillRule::kNonZero, .blend = blend, .color = color, .alpha = alpha};
}

bool near(const std::array<std::uint8_t, 4>& actual, const std::array<std::uint8_t, 4>& expected, int tolerance = 2) {
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::abs(actual.at(i) - expected.at(i)) > tolerance) {
      return false;
    }
  }
  return true;
}

std::string show(const std::array<std::uint8_t, 4>& p) { return std::format("({}, {}, {}, {})", p[0], p[1], p[2], p[3]); }

TEST(Raster, GroupAlphaAppliesToTheGroupAsAWhole) {
  DisplayList list = page(100, 100);
  list.commands.emplace_back(BeginGroup{.alpha = 0.5, .blend = BlendMode::kNormal, .isolated = false, .knockout = false});
  list.commands.emplace_back(fill_of(rect(0, 0, 60, 100), kRed));
  list.commands.emplace_back(fill_of(rect(40, 0, 60, 100), kBlue));
  list.commands.emplace_back(EndGroup{});
  const Bitmap b = render(list);
  // The overlap is the blue of the group at half strength: the red under it inside the group is gone.
  EXPECT_TRUE(near(at(b, 50, 50), {128, 128, 255, 255})) << show(at(b, 50, 50));
  EXPECT_TRUE(near(at(b, 20, 50), {255, 128, 128, 255})) << show(at(b, 20, 50));
  // Each shape at half alpha instead: the red shows through the blue.
  DisplayList separate = page(100, 100);
  separate.commands.emplace_back(fill_of(rect(0, 0, 60, 100), kRed, 0.5));
  separate.commands.emplace_back(fill_of(rect(40, 0, 60, 100), kBlue, 0.5));
  EXPECT_FALSE(near(at(render(separate), 50, 50), {128, 128, 255, 255}, 20));
}

// A soft mask that lets everything through: painting under it goes through the compositor of
// render/blend instead of Blend2D's operators.
void full_mask(DisplayList& list) {
  list.commands.emplace_back(BeginSoftMask{.kind = SoftMaskKind::kLuminosity, .backdrop = Rgb{1, 1, 1}, .transfer = nullptr});
  list.commands.emplace_back(EndSoftMask{});
}

TEST(Raster, SeparableModesMatchTheFormulas) {
  for (int m = 1; m < 12; ++m) {
    const auto mode = static_cast<BlendMode>(m);
    const auto draw = [&](bool through_mask) {
      DisplayList list = page(20, 20);
      list.colors.push_back(Color{.family = ColorSpaceFamily::kRGB, .components = {}, .pattern = {}, .rgb = {0.2, 0.6, 0.9}});
      list.colors.push_back(Color{.family = ColorSpaceFamily::kRGB, .components = {}, .pattern = {}, .rgb = {0.7, 0.3, 0.45}});
      list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), 3));
      if (through_mask) {
        full_mask(list);
      }
      list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), 4, 0.8, mode));
      return at(render(list), 10, 10);
    };
    const auto native = draw(false);
    const auto ours = draw(true);
    // And the formula itself.
    const PremultipliedRgba expected = composite(mode, {0.2F, 0.6F, 0.9F, 1}, {0.7F * 0.8F, 0.3F * 0.8F, 0.45F * 0.8F, 0.8F});
    const std::array<std::uint8_t, 4> formula = {static_cast<std::uint8_t>(std::lround(expected.r * 255)),
                                                 static_cast<std::uint8_t>(std::lround(expected.g * 255)),
                                                 static_cast<std::uint8_t>(std::lround(expected.b * 255)), 255};
    EXPECT_TRUE(near(ours, formula)) << blend_mode_name(mode) << ": ours " << show(ours) << ", formula " << show(formula);
    EXPECT_TRUE(near(native, formula, 3)) << blend_mode_name(mode) << ": Blend2D " << show(native) << ", formula " << show(formula);
  }
}

TEST(Raster, NonSeparableModes) {
  DisplayList list = page(20, 20);
  list.colors.push_back(Color{.family = ColorSpaceFamily::kGray, .components = {}, .pattern = {}, .rgb = {0.5, 0.5, 0.5}});
  list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), kRed));
  list.commands.emplace_back(fill_of(rect(0, 0, 10, 20), 3, 1, BlendMode::kLuminosity));
  const Bitmap b = render(list);
  // Red with the luminosity of 50% gray: SetLum clips it back into gamut, keeping the hue.
  EXPECT_TRUE(near(at(b, 5, 10), {255, 73, 73, 255})) << show(at(b, 5, 10));
  EXPECT_EQ(at(b, 15, 10), kRedPixel);
}

TEST(Raster, BlendModesActOnThePaintNotOnThePaper) {
  // The page is composed on a transparent backdrop, then onto white (as MuPDF and Poppler do): Hue over
  // bare paper leaves the blue as it is (over white it would turn white: white has no saturation).
  DisplayList list = page(20, 20);
  list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), kBlue, 1, BlendMode::kHue));
  list.commands.emplace_back(fill_of(rect(0, 0, 10, 20), kBlue, 1, BlendMode::kDifference));  // on the blue: black
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 15, 10), kBluePixel);
  EXPECT_TRUE(near(at(b, 5, 10), {0, 0, 0, 255})) << show(at(b, 5, 10));
}

TEST(Raster, SoftMasksAlphaAndLuminosity) {
  for (const SoftMaskKind kind : {SoftMaskKind::kAlpha, SoftMaskKind::kLuminosity}) {
    DisplayList list = page(100, 100);
    list.colors.push_back(Color{.family = ColorSpaceFamily::kGray, .components = {}, .pattern = {}, .rgb = {1, 1, 1}});
    list.commands.emplace_back(Save{});
    // The mask's group: the left half, opaque white (alpha 1, luminosity 1) on a black backdrop.
    list.commands.emplace_back(BeginSoftMask{.kind = kind, .backdrop = Rgb{}, .transfer = nullptr});
    list.commands.emplace_back(Save{});
    list.commands.emplace_back(fill_of(rect(0, 0, 50, 100), 3));
    list.commands.emplace_back(Restore{});
    list.commands.emplace_back(EndSoftMask{});
    list.commands.emplace_back(fill_of(rect(0, 0, 100, 100), kRed));
    list.commands.emplace_back(Restore{});
    // The mask ended with its state: the blue is not masked.
    list.commands.emplace_back(fill_of(rect(0, 80, 100, 20), kBlue));
    const Bitmap b = render(list);
    EXPECT_EQ(at(b, 25, 50), kRedPixel) << static_cast<int>(kind);
    EXPECT_EQ(at(b, 75, 50), kWhite) << static_cast<int>(kind);
    EXPECT_EQ(at(b, 75, 90), kBluePixel) << static_cast<int>(kind);
  }
}

TEST(Raster, SoftMaskClearedAndGroupsMaskedAsAWhole) {
  DisplayList list = page(100, 100);
  list.colors.push_back(Color{.family = ColorSpaceFamily::kGray, .components = {}, .pattern = {}, .rgb = {0.5, 0.5, 0.5}});
  // A uniform 50% luminosity mask over a group of two overlapping opaque squares: masked as one.
  list.commands.emplace_back(BeginSoftMask{.kind = SoftMaskKind::kLuminosity, .backdrop = Rgb{0.5, 0.5, 0.5}, .transfer = nullptr});
  list.commands.emplace_back(EndSoftMask{});
  list.commands.emplace_back(BeginGroup{.alpha = 1, .blend = BlendMode::kNormal, .isolated = false, .knockout = false});
  list.commands.emplace_back(fill_of(rect(0, 0, 60, 50), kRed));
  list.commands.emplace_back(fill_of(rect(40, 0, 60, 50), kBlue));
  list.commands.emplace_back(EndGroup{});
  list.commands.emplace_back(ClearSoftMask{});
  list.commands.emplace_back(fill_of(rect(0, 60, 100, 40), kBlue));
  const Bitmap b = render(list);
  EXPECT_TRUE(near(at(b, 50, 25), {128, 128, 255, 255})) << show(at(b, 50, 25));  // blue over white at half: no red inside
  EXPECT_EQ(at(b, 50, 80), kBluePixel);                                           // after ClearSoftMask
}

TEST(Raster, KnockoutGroupObjectsReplaceEachOther) {
  const auto draw = [](bool knockout) {
    DisplayList list = page(100, 100);
    list.commands.emplace_back(BeginGroup{.alpha = 1, .blend = BlendMode::kNormal, .isolated = true, .knockout = knockout});
    list.commands.emplace_back(fill_of(rect(0, 0, 60, 100), kRed, 0.5));
    list.commands.emplace_back(fill_of(rect(40, 0, 60, 100), kBlue, 0.5));
    list.commands.emplace_back(EndGroup{});
    return at(render(list), 50, 50);
  };
  // Knockout: where the blue overlaps it, the red is gone — blue at half over the paper.
  EXPECT_TRUE(near(draw(true), {128, 128, 255, 255})) << show(draw(true));
  // Without: the blue over the red.
  EXPECT_TRUE(near(draw(false), {128, 64, 191, 255}, 3)) << show(draw(false));
}

TEST(Raster, NonIsolatedGroupsBlendWithTheBackdropAndRemoveIt) {
  const auto draw = [](bool isolated) {
    DisplayList list = page(20, 20);
    list.colors.push_back(Color{.family = ColorSpaceFamily::kGray, .components = {}, .pattern = {}, .rgb = {0.5, 0.5, 0.5}});
    list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), 3));
    list.commands.emplace_back(BeginGroup{.alpha = 0.5, .blend = BlendMode::kNormal, .isolated = isolated, .knockout = false});
    list.commands.emplace_back(fill_of(rect(0, 0, 20, 20), kRed, 1, BlendMode::kMultiply));
    list.commands.emplace_back(EndGroup{});
    return at(render(list), 10, 10);
  };
  // Non-isolated: red multiplies the gray inside the group (0.5 0 0), then half of it over the gray.
  EXPECT_TRUE(near(draw(false), {128, 64, 64, 255})) << show(draw(false));
  // Isolated: nothing to multiply inside the group — plain red, then half of it over the gray.
  EXPECT_TRUE(near(draw(true), {191, 64, 64, 255})) << show(draw(true));
}

TEST(Raster, BlendModesInsideAPathClipSeeTheBackdrop) {
  DisplayList list = page(100, 100);
  list.commands.emplace_back(fill_of(rect(0, 0, 100, 100), kRed));
  list.commands.emplace_back(Save{});
  list.commands.emplace_back(ClipPath{.path = circle(50, 50, 30), .ctm = {}, .rule = FillRule::kNonZero});  // a layer, not a rectangle
  list.commands.emplace_back(fill_of(rect(0, 0, 100, 100), kBlue, 1, BlendMode::kMultiply));
  list.commands.emplace_back(Restore{});
  const Bitmap b = render(list);
  EXPECT_TRUE(near(at(b, 50, 50), {0, 0, 0, 255})) << show(at(b, 50, 50));  // red × blue
  EXPECT_EQ(at(b, 5, 5), kRedPixel);                                        // outside the clip
}

TEST(Raster, SoftMaskTransferFunction) {
  DisplayList list = page(100, 100);
  list.colors.push_back(Color{.family = ColorSpaceFamily::kGray, .components = {}, .pattern = {}, .rgb = {1, 1, 1}});
  auto inverted = std::make_shared<std::vector<std::uint8_t>>(256);
  for (std::size_t v = 0; v < 256; ++v) {
    (*inverted)[v] = static_cast<std::uint8_t>(255 - v);
  }
  list.commands.emplace_back(BeginSoftMask{.kind = SoftMaskKind::kAlpha, .backdrop = Rgb{}, .transfer = inverted});
  list.commands.emplace_back(fill_of(rect(0, 0, 50, 100), 3));
  list.commands.emplace_back(EndSoftMask{});
  list.commands.emplace_back(fill_of(rect(0, 0, 100, 100), kRed));
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 25, 50), kWhite);     // the mask's opaque half, inverted: nothing
  EXPECT_EQ(at(b, 75, 50), kRedPixel);  // its empty half, inverted: everything
}

std::shared_ptr<ImagePixels> image_of(int w, int h, const std::vector<std::uint8_t>& rgba) {
  return std::make_shared<ImagePixels>(ImagePixels{.width = w, .height = h, .rgba = rgba});
}

Boxed<DrawImage> draw_image(const Matrix& ctm, std::shared_ptr<const ImagePixels> pixels, double alpha = 1) {
  DrawImage image;
  image.ctm = ctm;
  image.alpha = alpha;
  image.pixels = std::move(pixels);
  return {std::move(image)};
}

TEST(Raster, ImagesFillTheirUnitSquareFirstRowOnTop) {
  DisplayList list = page(100, 100);
  // 1 x 2: a red row above a blue one, stretched over the whole page.
  list.commands.emplace_back(draw_image(Matrix{100, 0, 0, 100, 0, 0}, image_of(1, 2, {255, 0, 0, 255, 0, 0, 255, 255})));
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 50, 90), kRedPixel);
  EXPECT_EQ(at(b, 50, 10), kBluePixel);
  // Transparent pixels and the image's alpha let the page through.
  DisplayList faded = page(100, 100);
  faded.commands.emplace_back(draw_image(Matrix{100, 0, 0, 100, 0, 0}, image_of(2, 1, {255, 0, 0, 0, 0, 0, 255, 255}), 0.5));
  const Bitmap f = render(faded);
  EXPECT_EQ(at(f, 25, 50), kWhite);
  EXPECT_TRUE(near(at(f, 75, 50), {128, 128, 255, 255})) << show(at(f, 75, 50));
}

TEST(Raster, ImagesDrawnMuchSmallerAreAveraged) {
  // A 400 x 400 black-and-white checkerboard of single pixels in a 10 x 10 square: gray, not a moiré of
  // whichever pixels bilinear sampling happens to pick.
  std::vector<std::uint8_t> checks;
  for (int y = 0; y < 400; ++y) {
    for (int x = 0; x < 400; ++x) {
      const std::uint8_t v = (x + y) % 2 == 0 ? 0 : 255;
      checks.insert(checks.end(), {v, v, v, 255});
    }
  }
  DisplayList list = page(10, 10);
  list.commands.emplace_back(draw_image(Matrix{10, 0, 0, 10, 0, 0}, image_of(400, 400, checks)));
  const Bitmap b = render(list);
  for (const auto& [x, y] : {std::pair{2.0, 2.0}, std::pair{5.0, 5.0}, std::pair{8.0, 3.0}}) {
    EXPECT_TRUE(near(at(b, x, y), {128, 128, 128, 255}, 3)) << show(at(b, x, y));
  }
}

TEST(Raster, UndecodedImagesInASoftMaskDrawNothing) {
  // A luminosity mask on black made of an image the engine cannot decode: no placeholder, so the mask
  // stays black and hides what it masks, rather than showing it through a solid box.
  DisplayList list = page(100, 100);
  list.commands.emplace_back(BeginSoftMask{.kind = SoftMaskKind::kLuminosity, .backdrop = Rgb{}, .transfer = nullptr});
  list.commands.emplace_back(draw_image(Matrix{100, 0, 0, 100, 0, 0}, nullptr));
  list.commands.emplace_back(EndSoftMask{});
  list.commands.emplace_back(fill_of(rect(0, 0, 100, 100), kRed));
  EXPECT_EQ(at(render(list), 50, 50), kWhite);
}

TEST(Raster, UnbalancedTransparencyCommandsDoNotBreakTheRendering) {
  // A list cut short (the command limit), or ends without beginnings.
  DisplayList list = page(50, 50);
  list.commands.emplace_back(EndGroup{});
  list.commands.emplace_back(EndSoftMask{});
  list.commands.emplace_back(BeginGroup{.alpha = 0.5, .blend = BlendMode::kHue, .isolated = true, .knockout = true});
  list.commands.emplace_back(BeginSoftMask{.kind = SoftMaskKind::kAlpha, .backdrop = Rgb{}, .transfer = nullptr});
  list.commands.emplace_back(fill_of(rect(0, 0, 50, 50), kRed));
  const Bitmap b = render(list);
  EXPECT_EQ(at(b, 25, 25), kWhite);  // the mask's group, never finished, draws nothing on the page
}

TEST(Dash, CurvesAndTooManyDashes) {
  Path curve;
  curve.move_to({0, 0});
  curve.cubic_to({0, 50}, {100, 50}, {100, 0});
  const std::vector<double> pattern{5, 5};
  const DashedPath dashed = dash_path(curve, pattern, 0, 0.05);
  EXPECT_GT(subpaths(dashed.path), 10U);
  EXPECT_FALSE(dashed.too_many);
  const std::vector<double> tiny{1e-6, 1e-6};
  const DashedPath exploding = dash_path(curve, tiny, 0, 0.05, 1000);
  EXPECT_TRUE(exploding.too_many);
  EXPECT_EQ(exploding.path.size(), curve.size());  // solid rather than half-dashed
}

}  // namespace
}  // namespace capriole::render
