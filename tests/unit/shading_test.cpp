// Shadings and patterns (§8.7): the pixels of each kind of shading (graphics/shading), the seven shading
// types and the two pattern types as the interpreter prepares them, and how they are drawn.

#include "capriole/graphics/shading.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/svg.hpp"
#include "capriole/render/raster.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::graphics {
namespace {

// ── Pixels ────────────────────────────────────────────────────────────────────────────────────

std::vector<Rgb> ramp(const Rgb& from, const Rgb& to, int n = 256) {
  std::vector<Rgb> out;
  for (int i = 0; i < n; ++i) {
    const double f = static_cast<double>(i) / (n - 1);
    out.emplace_back(from.r + ((to.r - from.r) * f), from.g + ((to.g - from.g) * f), from.b + ((to.b - from.b) * f));
  }
  return out;
}

struct Pixel {
  int r, g, b, a;
};

Pixel at(const ImagePixels& p, int x, int y) {
  const auto* v = &p.rgba[((static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width)) + static_cast<std::size_t>(x)) * 4];
  return {v[0], v[1], v[2], v[3]};
}

TEST(ShadingPixels, AxialAlongItsAxisAndExtended) {
  Shading s;
  s.kind = Shading::Kind::kAxial;
  s.coords = {2, 0, 8, 0};
  s.ramp = ramp({1, 0, 0}, {0, 0, 1});
  const ImagePixels plain = shade(s, Matrix{}, 10, 1);
  EXPECT_EQ(at(plain, 0, 0).a, 0);  // before the axis, not extended
  EXPECT_EQ(at(plain, 9, 0).a, 0);
  EXPECT_EQ(at(plain, 2, 0).r, 234);  // x = 2.5: t = 1/12
  EXPECT_EQ(at(plain, 5, 0).r, 106);  // x = 5.5: t = 7/12
  EXPECT_GT(at(plain, 7, 0).b, 200);
  s.extend_start = true;
  s.extend_end = true;
  const ImagePixels extended = shade(s, Matrix{}, 10, 1);
  EXPECT_EQ(at(extended, 0, 0).r, 255);
  EXPECT_EQ(at(extended, 9, 0).b, 255);
  s.coords = {3, 3, 3, 3};  // an axis of no length: nothing
  EXPECT_EQ(at(shade(s, Matrix{}, 10, 1), 5, 0).a, 0);
}

TEST(ShadingPixels, RadialBetweenTwoCircles) {
  Shading s;
  s.kind = Shading::Kind::kRadial;
  s.coords = {10, 10, 0, 10, 10, 8};  // concentric, from a point to a circle of radius 8
  s.ramp = ramp({0, 0, 0}, {1, 1, 1});
  const ImagePixels plain = shade(s, Matrix{}, 20, 20);
  EXPECT_EQ(at(plain, 10, 10).r, 23);        // (10.5, 10.5): t = 0.707 / 8
  EXPECT_NEAR(at(plain, 14, 10).r, 143, 2);  // 4.5 from the centre... in x, 0.5 in y: t ≈ 0.56
  EXPECT_EQ(at(plain, 19, 10).a, 0);         // beyond the outer circle
  s.extend_end = true;
  EXPECT_EQ(at(shade(s, Matrix{}, 20, 20), 19, 10).r, 255);
  // A cone: the second circle outside the first. Only the points some circle passes through are painted.
  s.coords = {5, 10, 2, 15, 10, 4};
  s.extend_end = false;
  const ImagePixels cone = shade(s, Matrix{}, 20, 20);
  EXPECT_EQ(at(cone, 10, 10).a, 255);  // on the way between the circles
  EXPECT_EQ(at(cone, 10, 1).a, 0);     // above the cone
}

TEST(ShadingPixels, FunctionGridIsInterpolatedThroughItsMatrix) {
  Shading s;
  s.kind = Shading::Kind::kFunction;
  s.domain = {0, 0, 1, 1};
  s.matrix = Matrix::scale(10, 10);  // the domain covers 10 × 10 pixels
  s.grid_width = 2;
  s.grid_height = 2;
  s.grid = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
  const ImagePixels p = shade(s, Matrix{}, 12, 12);
  EXPECT_NEAR(at(p, 4, 4).r, 115, 2);  // (0.45, 0.45)
  EXPECT_NEAR(at(p, 4, 4).g, 115, 2);
  EXPECT_EQ(at(p, 11, 5).a, 0);  // outside the domain
}

TEST(ShadingPixels, MeshTrianglesAreGouraudShaded) {
  Shading s;
  s.kind = Shading::Kind::kMesh;
  s.triangles.push_back({Shading::Vertex{.point = {0, 0}, .color = {1, 0, 0}, .t = 0}, Shading::Vertex{.point = {30, 0}, .color = {0, 1, 0}, .t = 0},
                         Shading::Vertex{.point = {0, 30}, .color = {0, 0, 1}, .t = 0}});
  const ImagePixels p = shade(s, Matrix{}, 30, 30);
  const Pixel centroid = at(p, 9, 9);  // (9.5, 9.5): close to the centroid
  EXPECT_NEAR(centroid.r, 93, 3);
  EXPECT_NEAR(centroid.g, 81, 3);
  EXPECT_NEAR(centroid.b, 81, 3);
  EXPECT_EQ(at(p, 25, 25).a, 0);  // outside the triangle
  // With a ramp, corners carry t, interpolated before the lookup.
  s.ramp = ramp({0, 0, 0}, {1, 1, 1});
  s.t0 = 0;
  s.t1 = 1;
  for (auto& v : s.triangles[0]) {
    v.t = v.point.x == 30 ? 1 : 0;
  }
  EXPECT_NEAR(at(shade(s, Matrix{}, 30, 30), 15, 5).r, 132, 3);  // t = 15.5/30
}

TEST(ShadingPixels, BBoxLimitsThePaint) {
  Shading s;
  s.kind = Shading::Kind::kAxial;
  s.coords = {0, 0, 10, 0};
  s.extend_start = s.extend_end = true;
  s.ramp = ramp({1, 0, 0}, {0, 0, 1});
  s.bbox = Rect{0, 0, 5, 10};
  const ImagePixels p = shade(s, Matrix{}, 10, 10);
  EXPECT_EQ(at(p, 2, 2).a, 255);
  EXPECT_EQ(at(p, 7, 2).a, 0);
}

// ── Through the interpreter ───────────────────────────────────────────────────────────────────

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::string stream_object(const std::string& dict, const std::string& data) {
  return "<<" + dict + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream";
}

struct Page {
  std::string content;
  std::string resources;
  std::vector<std::pair<std::uint32_t, std::string>> objects = {};
};

DisplayList interpret(const Page& page) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 100 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<<" + page.resources + ">>>>");
  b.add_stream(4, page.content);
  for (const auto& [number, body] : page.objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 40/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = content::interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0});
  EXPECT_TRUE(list.has_value());
  return std::move(*list);
}

template <typename T>
std::vector<T> all(const DisplayList& list) {
  std::vector<T> out;
  for (const auto& command : list.commands) {
    if (const T* c = command_as<T>(command)) {
      out.push_back(*c);
    }
  }
  return out;
}

bool has_warning(const DisplayList& list, std::string_view part) {
  return std::ranges::any_of(list.warnings, [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

const std::string kRedToBlue = "/Function<</FunctionType 2/Domain[0 1]/C0[1 0 0]/C1[0 0 1]/N 1>>";

TEST(Shadings, AxialAndRadialFromTheirDictionaries) {
  const auto list = interpret({.content = "0.5 0 0 0.5 10 10 cm /A sh /R sh /Bad sh",
                               .resources = "/Shading<</A<</ShadingType 2/ColorSpace/DeviceRGB/Coords[0 0 100 0]/Extend[true false]" + kRedToBlue +
                                            ">>/R<</ShadingType 3/ColorSpace/DeviceRGB/Coords[50 50 0 50 50 40]/Domain[0.5 1]" + kRedToBlue +
                                            ">>/Bad<</ShadingType 2/ColorSpace/DeviceRGB/Coords[0 0 1 0]>>>>"});
  const auto shadings = all<PaintShading>(list);
  ASSERT_EQ(shadings.size(), 2U);
  const Shading& a = *shadings[0].shading;
  EXPECT_EQ(a.kind, Shading::Kind::kAxial);
  EXPECT_TRUE(a.extend_start);
  EXPECT_FALSE(a.extend_end);
  EXPECT_EQ(a.ramp.front(), (Rgb{1, 0, 0}));
  EXPECT_EQ(a.ramp.back(), (Rgb{0, 0, 1}));
  EXPECT_EQ(shadings[0].ctm, (Matrix{0.5, 0, 0, 0.5, 10, 10}));
  const Shading& r = *shadings[1].shading;
  EXPECT_EQ(r.kind, Shading::Kind::kRadial);
  EXPECT_NEAR(r.ramp.front().r, 0.5, 1e-9);  // the ramp covers /Domain
  EXPECT_TRUE(has_warning(list, "without /Function"));
}

TEST(Shadings, FunctionBased) {
  const auto list = interpret({.content = "/F sh",
                               .resources = "/Shading<</F<</ShadingType 1/ColorSpace/DeviceGray/Domain[0 2 0 1]/Matrix[10 0 0 10 0 0]"
                                            "/Function<</FunctionType 4/Domain[0 2 0 1]/Range[0 1]/Length 13>>>>>>",
                               .objects = {}});
  // The function above is a dictionary where a stream is needed: unusable, reported.
  EXPECT_TRUE(all<PaintShading>(list).empty());
  const auto ok = interpret({.content = "/F sh",
                             .resources = "/Shading<</F 10 0 R>>",
                             .objects = {{10, "<</ShadingType 1/ColorSpace/DeviceGray/Domain[0 2 0 1]/Matrix[10 0 0 10 0 0]/Function 11 0 R>>"},
                                         {11, stream_object("/FunctionType 4/Domain[0 2 0 1]/Range[0 1]", "{ pop 2 div }")}}});
  const auto shadings = all<PaintShading>(ok);
  ASSERT_EQ(shadings.size(), 1U);
  const Shading& f = *shadings[0].shading;
  EXPECT_EQ(f.kind, Shading::Kind::kFunction);
  EXPECT_EQ(f.domain, (Rect{0, 0, 2, 1}));
  EXPECT_EQ(f.matrix, Matrix::scale(10, 10));
  EXPECT_NEAR(f.grid.back().r, 1, 1e-9);  // x = 2: 2 / 2
  EXPECT_NEAR(f.grid.front().r, 0, 1e-9);
}

// Mesh data: bytes as written.
std::string packed(std::initializer_list<int> values) {
  std::string out;
  for (const int v : values) {
    out.push_back(static_cast<char>(v));
  }
  return out;
}

const std::string kMeshDict = "/ColorSpace/DeviceRGB/BitsPerCoordinate 8/BitsPerComponent 8/Decode[0 255 0 255 0 1 0 1 0 1]";

TEST(Shadings, FreeFormAndLatticeMeshes) {
  // Type 4: a triangle, then a vertex with flag 1 continuing from its edge vb–vc.
  const std::string free = packed({0, 0, 0, 255, 0, 0, 0, 100, 0, 0, 255, 0, 0, 0, 100, 0, 0, 255, 1, 100, 100, 255, 255, 255});
  // Type 5: two rows of two vertices, one cell.
  const std::string lattice = packed({0, 0, 255, 0, 0, 100, 0, 0, 255, 0, 0, 100, 0, 0, 255, 100, 100, 255, 255, 255});
  const auto list = interpret({.content = "/M4 sh /M5 sh",
                               .resources = "/Shading<</M4 10 0 R/M5 11 0 R>>",
                               .objects = {{10, stream_object("/ShadingType 4" + kMeshDict + "/BitsPerFlag 8", free)},
                                           {11, stream_object("/ShadingType 5" + kMeshDict + "/VerticesPerRow 2", lattice)}}});
  const auto shadings = all<PaintShading>(list);
  ASSERT_EQ(shadings.size(), 2U);
  const auto& t4 = shadings[0].shading->triangles;
  ASSERT_EQ(t4.size(), 2U);
  EXPECT_EQ(t4[0][0].color, (Rgb{1, 0, 0}));
  EXPECT_EQ(t4[1][0].point, (Point{100, 0}));  // flag 1: vb, vc, the new vertex
  EXPECT_EQ(t4[1][2].point, (Point{100, 100}));
  EXPECT_EQ(shadings[1].shading->triangles.size(), 2U);
}

TEST(Shadings, CoonsAndTensorPatches) {
  // A flat square Coons patch (12 boundary points, 4 corner colours) and a second one sharing its edge
  // p03–p33 (flag 1: 8 points and 2 colours more).
  std::string data = packed({0, 0, 0, 0, 30, 0, 60, 0, 90, 30, 90, 60, 90, 90, 90, 90, 60, 90, 30, 90, 0, 60, 0, 30, 0});
  data += packed({255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
  data += packed({1, 0, 120, 0, 150, 0, 180, 30, 180, 60, 180, 90, 180, 90, 150, 90, 120});
  data += packed({0, 0, 0, 255, 255, 0});
  // A tensor patch: the same square with its 4 interior points.
  std::string tensor = packed({0, 0, 0, 0, 30, 0, 60, 0, 90, 30, 90, 60, 90, 90, 90, 90, 60, 90, 30, 90, 0, 60, 0, 30, 0, 30, 30, 30, 60, 60, 60, 60, 30});
  tensor += packed({255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
  const auto list = interpret({.content = "/C sh /T sh",
                               .resources = "/Shading<</C 10 0 R/T 11 0 R>>",
                               .objects = {{10, stream_object("/ShadingType 6" + kMeshDict + "/BitsPerFlag 8", data)},
                                           {11, stream_object("/ShadingType 7" + kMeshDict + "/BitsPerFlag 8", tensor)}}});
  const auto shadings = all<PaintShading>(list);
  ASSERT_EQ(shadings.size(), 2U);
  const auto& coons = shadings[0].shading->triangles;
  ASSERT_EQ(coons.size() % 2, 0U);
  const std::size_t per_patch = coons.size() / 2;
  // The first patch's corner (0,0) is red; the second patch starts on the first's edge p03 (0, 90), whose
  // colour was c03, green.
  EXPECT_EQ(coons[0][0].point, (Point{0, 0}));
  EXPECT_EQ(coons[0][0].color, (Rgb{1, 0, 0}));
  EXPECT_EQ(coons[per_patch][0].point, (Point{0, 90}));
  EXPECT_EQ(coons[per_patch][0].color, (Rgb{0, 1, 0}));
  // A flat patch's surface is the square itself: every vertex inside it.
  for (const auto& triangle : shadings[1].shading->triangles) {
    for (const auto& v : triangle) {
      EXPECT_GE(v.point.x, -1e-9);
      EXPECT_LE(v.point.x, 90 + 1e-9);
      EXPECT_GE(v.point.y, -1e-9);
      EXPECT_LE(v.point.y, 90 + 1e-9);
    }
  }
}

TEST(Shadings, BrokenMeshesAreReported) {
  const auto list =
      interpret({.content = "/M sh /N sh",
                 .resources = "/Shading<</M 10 0 R/N 11 0 R>>",
                 .objects = {{10, stream_object("/ShadingType 4" + kMeshDict + "/BitsPerFlag 8", packed({0, 1, 2}))},
                             {11, stream_object("/ShadingType 6/ColorSpace/DeviceRGB/BitsPerCoordinate 99/BitsPerComponent 8/BitsPerFlag 8/Decode[]", "x")}}});
  EXPECT_TRUE(all<PaintShading>(list).empty());
  EXPECT_TRUE(has_warning(list, "without complete triangles"));
  EXPECT_TRUE(has_warning(list, "invalid /BitsPerCoordinate"));
}

TEST(Patterns, ShadingPatternFillsTheArea) {
  // In a form whose matrix scales by 2: pattern space is the form's (§8.7.3.1).
  const auto list = interpret({.content = "/Fm Do",
                               .resources = "/XObject<</Fm 10 0 R>>",
                               .objects = {{10, stream_object("/Type/XObject/Subtype/Form/BBox[0 0 50 50]/Matrix[2 0 0 2 0 0]"
                                                              "/Resources<</Pattern<</P<</PatternType 2/Matrix[1 0 0 1 5 0]/Shading<</ShadingType 2"
                                                              "/ColorSpace/DeviceRGB/Coords[0 0 10 0]/Background[0 1 0]" +
                                                                  kRedToBlue + ">>>>>>>>",
                                                              "/Pattern cs /P scn 0 0 10 10 re f")}}});
  const auto shadings = all<PaintShading>(list);
  ASSERT_EQ(shadings.size(), 1U);
  EXPECT_TRUE(shadings[0].background);
  EXPECT_EQ(shadings[0].ctm, (Matrix{2, 0, 0, 2, 10, 0}));
  EXPECT_EQ(*shadings[0].shading->background, (Rgb{0, 1, 0}));
  EXPECT_TRUE(all<FillPath>(list).empty());   // the rectangle became the clip
  EXPECT_EQ(all<ClipPath>(list).size(), 2U);  // the form's bounding box and the area
}

TEST(Patterns, TilingCellsAreRepeated) {
  // A coloured tiling pattern: a red 5 × 5 square in a 10 × 10 cell, filling the page.
  const auto list =
      interpret({.content = "/Pattern cs /P scn 0 0 100 100 re f",
                 .resources = "/Pattern<</P 10 0 R>>",
                 .objects = {{10, stream_object("/PatternType 1/PaintType 1/TilingType 1/BBox[0 0 10 10]/XStep 10/YStep 10", "1 0 0 rg 0 0 5 5 re f")}}});
  const auto tilings = all<PaintTiling>(list);
  ASSERT_EQ(tilings.size(), 1U);
  ASSERT_NE(tilings[0].cell, nullptr);
  EXPECT_EQ(all<FillPath>(*tilings[0].cell).size(), 1U);
  auto bitmap = render::rasterize(list, 1);
  ASSERT_TRUE(bitmap.has_value());
  const auto pixel = [&](int x, int y) { return &bitmap->rgba[((static_cast<std::size_t>(y) * 100) + static_cast<std::size_t>(x)) * 4]; };
  EXPECT_EQ(pixel(2, 97)[1], 0);    // a square (bottom-left of a cell; rows run downwards)
  EXPECT_EQ(pixel(52, 97)[1], 0);   // another, five cells along
  EXPECT_EQ(pixel(7, 97)[1], 255);  // the gap between them
  EXPECT_EQ(pixel(52, 47)[1], 0);   // and up the page
}

TEST(Patterns, UncolouredTilingTakesTheColourGiven) {
  const auto list =
      interpret({.content = "/Cs cs 0 0 1 /P scn 0 0 100 100 re f",
                 .resources = "/ColorSpace<</Cs[/Pattern/DeviceRGB]>>/Pattern<</P 10 0 R>>",
                 .objects = {{10, stream_object("/PatternType 1/PaintType 2/TilingType 1/BBox[0 0 10 10]/XStep 10/YStep 10", "1 0 0 rg 0 0 5 5 re f")}}});
  const auto tilings = all<PaintTiling>(list);
  ASSERT_EQ(tilings.size(), 1U);
  const auto fills = all<FillPath>(*tilings[0].cell);
  ASSERT_EQ(fills.size(), 1U);
  EXPECT_EQ(tilings[0].cell->colors[fills[0].color].rgb, (Rgb{0, 0, 1}));  // "1 0 0 rg" ignored
}

TEST(Patterns, HugeStepsDrawOneCopy) {
  // A step far longer than the page (as some producers write a background image): drawn once, not refused.
  const auto list =
      interpret({.content = "/Pattern cs /P scn 0 0 100 100 re f",
                 .resources = "/Pattern<</P 10 0 R>>",
                 .objects = {{10, stream_object("/PatternType 1/PaintType 1/TilingType 2/BBox[0 0 100 60]/XStep 99999/YStep 99999/Matrix[1 0 0 1 0 -20]",
                                                "0 1 0 rg 0 0 100 60 re f")}}});
  auto bitmap = render::rasterize(list, 2);
  ASSERT_TRUE(bitmap.has_value());
  const auto green = [&](int x, int y) { return bitmap->rgba[((static_cast<std::size_t>(y) * 200) + static_cast<std::size_t>(x)) * 4] == 0; };
  EXPECT_TRUE(green(100, 150));  // y = 25 in the page: inside the cell (−20 to 40)
  EXPECT_FALSE(green(100, 50));  // y = 75: above it, nothing repeats
}

TEST(Patterns, TextFilledWithAPattern) {
  const auto list = interpret({.content = "BT /Pattern cs /P scn /F1 40 Tf 10 10 Td (HI) Tj ET",
                               .resources = "/Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>/Pattern<</P<</PatternType 2/Shading<</ShadingType 2"
                                            "/ColorSpace/DeviceRGB/Coords[0 0 100 0]" +
                                            kRedToBlue + ">>>>>>"});
  const auto runs = all<ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_EQ(runs[0].render_mode, 3);  // the glyphs are the pattern's area; nothing else to paint
  ASSERT_EQ(all<ClipPath>(list).size(), 1U);
  EXPECT_FALSE(all<ClipPath>(list)[0].path.empty());
  EXPECT_EQ(all<PaintShading>(list).size(), 1U);
}

TEST(Patterns, SvgPreview) {
  const auto list = interpret(
      {.content = "/Pattern cs /P scn 0 0 100 100 re f /S sh",
       .resources = "/Pattern<</P 10 0 R>>/Shading<</S<</ShadingType 2/ColorSpace/DeviceRGB/Coords[0 0 100 0]" + kRedToBlue + ">>>>",
       .objects = {{10, stream_object("/PatternType 1/PaintType 1/TilingType 1/BBox[0 0 10 10]/XStep 10/YStep 10", "q 0 0 5 5 re W n 0 0 10 10 re f Q")}}});
  const std::string svg = to_svg(list);
  EXPECT_NE(svg.find(R"(<pattern id="p)"), std::string::npos);
  EXPECT_NE(svg.find(R"(clip-path="url(#p)"), std::string::npos);    // the cell's own ids are prefixed
  EXPECT_NE(svg.find("data:image/png;base64,"), std::string::npos);  // the shading's pixels
}

}  // namespace
}  // namespace capriole::graphics
