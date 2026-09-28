#include "capriole/content/interpreter.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/graphics/svg.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::content {
namespace {

using graphics::DisplayList;
using graphics::Matrix;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

// A one-page document: content, resources (the inside of << >>), and extra objects from number 10.
struct Page {
  std::string content = {};
  std::string resources = {};
  std::vector<std::pair<std::uint32_t, std::string>> objects = {};
  std::string page_extra = {};
};

DisplayList interpret(const Page& page) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 200 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<<" + page.resources + ">>" + page.page_extra + ">>");
  b.add_stream(4, page.content);
  for (const auto& [number, body] : page.objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 30/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0});
  EXPECT_TRUE(list.has_value()) << list.error().message;
  return std::move(*list);
}

template <typename T>
std::vector<T> all(const DisplayList& list) {
  std::vector<T> out;
  for (const auto& command : list.commands) {
    if (const T* c = graphics::command_as<T>(command)) {
      out.push_back(*c);
    }
  }
  return out;
}

bool has_warning(const DisplayList& list, std::string_view part) {
  for (const auto& w : list.warnings) {
    if (w.find(part) != std::string::npos) {
      return true;
    }
  }
  return false;
}

TEST(Geometry, MatrixComposition) {
  const Matrix scale = Matrix::scale(2, 3);
  const Matrix move = Matrix::translate(10, 20);
  // Scale first, then move: (1, 1) → (2, 3) → (12, 23).
  EXPECT_EQ(scale.then(move).apply(graphics::Point{1, 1}), (graphics::Point{12, 23}));
  EXPECT_EQ(move.then(scale).apply(graphics::Point{1, 1}), (graphics::Point{22, 63}));
  const auto inverse = scale.then(move).inverse();
  ASSERT_TRUE(inverse.has_value());
  const graphics::Point back = inverse->apply(graphics::Point{12, 23});
  EXPECT_NEAR(back.x, 1, 1e-12);
  EXPECT_NEAR(back.y, 1, 1e-12);
  EXPECT_FALSE(Matrix::scale(0, 1).inverse().has_value());
}

TEST(Interpreter, PageBoxesAndRotation) {
  const auto list = interpret({.content = "", .page_extra = "/CropBox[10 10 150 90]/Rotate -90"});
  EXPECT_EQ(list.media_box, (graphics::Rect{0, 0, 200, 100}));  // inherited from the parent
  EXPECT_EQ(list.crop_box, (graphics::Rect{10, 10, 150, 90}));
  EXPECT_EQ(list.rotate, 270);
}

TEST(Interpreter, PathsColoursAndStateStack) {
  const auto list = interpret({.content = "q 2 0 0 2 10 10 cm 1 0 0 rg 0 0 10 10 re f Q 0 0 1 RG 3 w 1 J 0 0 m 50 50 l S"});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 1U);
  EXPECT_EQ(fills[0].ctm, (Matrix{2, 0, 0, 2, 10, 10}));
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{1, 0, 0}));
  EXPECT_EQ(fills[0].path.size(), 5U);  // re: m l l l h
  const auto strokes = all<graphics::StrokePath>(list);
  ASSERT_EQ(strokes.size(), 1U);
  EXPECT_EQ(strokes[0].ctm, Matrix{});  // Q restored the CTM
  EXPECT_EQ(list.styles[strokes[0].style].width, 3);
  EXPECT_EQ(list.styles[strokes[0].style].cap, graphics::LineCap::kRound);
  EXPECT_EQ(list.colors[strokes[0].color].rgb, (graphics::Rgb{0, 0, 1}));
  EXPECT_EQ(all<graphics::Save>(list).size(), 1U);
  EXPECT_EQ(all<graphics::Restore>(list).size(), 1U);
}

TEST(Interpreter, ClipTakesEffectAfterThePaintingOperator) {
  const auto list = interpret({.content = "0 0 50 50 re W* n 0 0 100 100 re f"});
  ASSERT_EQ(list.commands.size(), 2U);
  const auto* clip = std::get_if<graphics::ClipPath>(&list.commands[0]);
  ASSERT_NE(clip, nullptr);
  EXPECT_EQ(clip->rule, graphics::FillRule::kEvenOdd);
  EXPECT_NE(std::get_if<graphics::FillPath>(&list.commands[1]), nullptr);
}

TEST(Interpreter, UnbalancedSavesAreClosedAtTheEnd) {
  const auto list = interpret({.content = "q q 0 0 1 1 re f Q Q Q"});
  EXPECT_EQ(all<graphics::Save>(list).size(), all<graphics::Restore>(list).size());
  EXPECT_TRUE(has_warning(list, "Q without a matching q"));
  const auto open = interpret({.content = "q q 0 0 1 1 re f"});
  EXPECT_EQ(all<graphics::Save>(open).size(), all<graphics::Restore>(open).size());
}

TEST(Interpreter, ExtGStateAlphaAndLineStyle) {
  const auto list = interpret({.content = "/GS1 gs 0 0 1 1 re f 0 0 m 1 1 l S", .resources = "/ExtGState<</GS1<</ca 0.5/CA 0.25/LW 4/D[[3 1]0]>>>>"});
  EXPECT_EQ(all<graphics::FillPath>(list)[0].alpha, 0.5);
  const auto stroke = all<graphics::StrokePath>(list)[0];
  EXPECT_EQ(stroke.alpha, 0.25);
  EXPECT_EQ(list.styles[stroke.style].width, 4);
  EXPECT_EQ(list.styles[stroke.style].dash, (std::vector<double>{3, 1}));
}

TEST(Interpreter, ColourSpaces) {
  const auto list = interpret({.content = "/Idx cs 1 sc 0 0 1 1 re f 0 0 0 1 k 0 0 1 1 re f /Spot cs 1 sc 0 0 1 1 re f",
                               .resources = "/ColorSpace<</Idx[/Indexed/DeviceRGB 1<000000FF8000>]/Spot[/Separation/Gold/DeviceCMYK 12 0 R]>>",
                               .objects = {{12, "<</FunctionType 2/Domain[0 1]/C0[0 0 0 0]/C1[0 0.2 1 0]/N 1>>"}}});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 3U);
  EXPECT_EQ(list.colors[fills[0].color].family, graphics::ColorSpaceFamily::kIndexed);
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{1, 128 / 255.0, 0}));  // entry 1 of the table
  EXPECT_EQ(list.colors[fills[1].color].rgb, (graphics::Rgb{0, 0, 0}));            // CMYK 0 0 0 1
  EXPECT_EQ(list.colors[fills[2].color].family, graphics::ColorSpaceFamily::kSeparation);
  EXPECT_TRUE(has_warning(list, "spot colours"));
}

TEST(Interpreter, TextPositionsFromTheFontWidths) {
  // /Widths from code 65: A = 500, B = 700. Tc 1, Tz 50, a TJ adjustment of -1000 (one em forward).
  const auto list = interpret({.content = "BT /F1 10 Tf 1 Tc 50 Tz 100 50 Td [(AB) -1000 (A)] TJ (B) Tj ET",
                               .resources = "/Font<</F1 12 0 R>>",
                               .objects = {{12, "<</Type/Font/Subtype/Type1/BaseFont/Test/FirstChar 65/LastChar 66/Widths[500 700]>>"}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(runs[0].widths_known);
  EXPECT_EQ(runs[0].font, "F1");
  EXPECT_EQ(runs[0].text_matrix, Matrix::translate(100, 50));
  ASSERT_EQ(runs[0].glyphs.size(), 3U);
  // A: (0.5·10 + 1)·0.5 = 3; B: (0.7·10 + 1)·0.5 = 4; then -(-1000/1000)·10·0.5 = 5 forward.
  EXPECT_DOUBLE_EQ(runs[0].glyphs[0].advance, 3);
  EXPECT_DOUBLE_EQ(runs[0].glyphs[1].origin.x, 3);
  EXPECT_DOUBLE_EQ(runs[0].glyphs[2].origin.x, 3 + 4 + 5);
  // The second run starts where the first ended: 3 + 4 + 5 + 3 = 15.
  EXPECT_EQ(runs[1].text_matrix, Matrix::translate(115, 50));
}

TEST(Interpreter, CompositeFontWithIdentityCMap) {
  const auto list = interpret({.content = "BT /F0 20 Tf <00410042> Tj ET",
                               .resources = "/Font<</F0 12 0 R>>",
                               .objects = {{12, "<</Type/Font/Subtype/Type0/BaseFont/X/Encoding/Identity-H/DescendantFonts[13 0 R]>>"},
                                           {13, "<</Type/Font/Subtype/CIDFontType2/BaseFont/X/DW 1000/W[65 [250] 66 70 750]>>"}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  ASSERT_EQ(runs[0].glyphs.size(), 2U);
  EXPECT_EQ(runs[0].glyphs[0].code, 0x41U);
  EXPECT_DOUBLE_EQ(runs[0].glyphs[0].advance, 5);   // 250/1000 · 20
  EXPECT_DOUBLE_EQ(runs[0].glyphs[1].advance, 15);  // 750/1000 · 20, from the range 66–70
}

TEST(Interpreter, StandardFontsWithoutWidthsAreEstimated) {
  const auto list = interpret({.content = "BT /F1 10 Tf (Hi) Tj ET /F2 10 Tf BT (Hi) Tj ET",
                               .resources = "/Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>/F2<</Type/Font/Subtype/Type1/BaseFont/Courier>>>>"});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_FALSE(runs[0].widths_known);
  EXPECT_TRUE(runs[1].widths_known);  // Courier: 600 for every glyph
  EXPECT_DOUBLE_EQ(runs[1].glyphs[0].advance, 6);
  EXPECT_TRUE(has_warning(list, "Helvetica has no /Widths"));
}

TEST(Interpreter, FormXObjects) {
  // A form with a matrix and a bounding box, drawn twice; its own resources hold a colour space.
  const auto list = interpret({.content = "/Fm Do 1 0 0 1 50 0 cm /Fm Do",
                               .resources = "/XObject<</Fm 12 0 R>>",
                               .objects = {{12,
                                            "<</Type/XObject/Subtype/Form/BBox[0 0 10 10]/Matrix[2 0 0 2 0 0]/Resources<</ColorSpace<</C[/ICCBased 13 0 R]>>>>"
                                            "/Length 26>>\nstream\n/C cs 0 0 1 sc 0 0 5 5 re f\nendstream"},
                                           {13, "<</N 3/Length 0>>\nstream\n\nendstream"}}});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 2U);
  EXPECT_EQ(fills[0].ctm, Matrix::scale(2, 2));
  EXPECT_EQ(fills[1].ctm, (Matrix{2, 0, 0, 2, 50, 0}));
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{0, 0, 1}));  // the form's colour space (ICC with N 3: RGB)
  EXPECT_EQ(all<graphics::ClipPath>(list).size(), 2U);                   // the bounding box
  EXPECT_EQ(all<graphics::Save>(list).size(), 2U);
}

TEST(Interpreter, FormThatDrawsItselfIsCut) {
  const auto list = interpret(
      {.content = "/Fm Do",
       .resources = "/XObject<</Fm 12 0 R>>",
       .objects = {{12, "<</Subtype/Form/BBox[0 0 10 10]/Resources<</XObject<</Fm 12 0 R>>>>/Length 26>>\nstream\n0 0 1 1 re f /Fm Do     \nendstream"}}});
  EXPECT_EQ(all<graphics::FillPath>(list).size(), 1U);
  EXPECT_TRUE(has_warning(list, "draws itself"));
}

TEST(Interpreter, ImagesAndShadings) {
  const auto list =
      interpret({.content = "q 100 0 0 50 10 20 cm /Im Do Q BI /W 2 /H 1 /IM true ID \x80 EI /Sh sh",
                 .resources = "/XObject<</Im 12 0 R>>/Shading<</Sh<</ShadingType 2/ColorSpace/DeviceRGB/Coords[0 0 1 0]>>>>",
                 .objects = {{12, "<</Subtype/Image/Width 4/Height 3/ColorSpace/DeviceGray/BitsPerComponent 8/Length 12>>\nstream\n012345678901\nendstream"}}});
  const auto images = all<graphics::DrawImage>(list);
  ASSERT_EQ(images.size(), 2U);
  EXPECT_EQ(images[0].xobject->number, 12U);
  EXPECT_EQ(images[0].width, 4);
  EXPECT_EQ(images[0].ctm, (Matrix{100, 0, 0, 50, 10, 20}));
  EXPECT_TRUE(images[1].mask);
  EXPECT_EQ(images[1].width, 2);
  EXPECT_EQ(all<graphics::PaintShading>(list).size(), 1U);
}

TEST(Interpreter, BlendModesSoftMasksAndGroups) {
  const auto list =
      interpret({.content = "/G1 gs 0 0 10 10 re f /G2 gs 0 0 10 10 re S q /M gs 0 0 5 5 re f Q /Fg Do /N gs",
                 .resources = "/ExtGState<</G1<</BM/Multiply/ca 0.5>>/G2<</BM[/Foo/Screen]>>"
                              "/M<</SMask<</S/Luminosity/G 12 0 R/BC[1]>>>>/N<</SMask/None>>>>/XObject<</Fg 13 0 R>>",
                 .objects = {{12, "<</Subtype/Form/BBox[0 0 10 10]/Group<</S/Transparency/CS/DeviceGray>>/Length 18>>\nstream\n0.5 g 0 0 5 5 re f\nendstream"},
                             {13, "<</Subtype/Form/BBox[0 0 10 10]/Group<</S/Transparency/I true/K true>>/Length 12>>\nstream\n0 0 3 3 re f\nendstream"}}});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 4U);
  EXPECT_EQ(fills[0].blend, graphics::BlendMode::kMultiply);
  EXPECT_EQ(fills[0].alpha, 0.5);
  // The mask's group is drawn in the initial state (only the CTM carries over).
  EXPECT_EQ(fills[1].blend, graphics::BlendMode::kNormal);
  EXPECT_EQ(fills[1].alpha, 1);
  EXPECT_EQ(list.colors[fills[1].color].rgb, (graphics::Rgb{0.5, 0.5, 0.5}));
  // Under the mask: the state of the page (the first known name of the /BM array).
  EXPECT_EQ(fills[2].blend, graphics::BlendMode::kScreen);
  EXPECT_EQ(all<graphics::StrokePath>(list)[0].blend, graphics::BlendMode::kScreen);
  const auto masks = all<graphics::BeginSoftMask>(list);
  ASSERT_EQ(masks.size(), 1U);
  EXPECT_EQ(masks[0].kind, graphics::SoftMaskKind::kLuminosity);
  EXPECT_EQ(masks[0].backdrop, (graphics::Rgb{1, 1, 1}));  // /BC in the group's DeviceGray
  EXPECT_EQ(all<graphics::EndSoftMask>(list).size(), 1U);
  // The transparency group: composited with the page's alpha and blend mode, which restart inside.
  const auto groups = all<graphics::BeginGroup>(list);
  ASSERT_EQ(groups.size(), 1U);  // the mask's own /Group is the mask, not a group on the page
  EXPECT_EQ(groups[0].alpha, 0.5);
  EXPECT_EQ(groups[0].blend, graphics::BlendMode::kScreen);
  EXPECT_TRUE(groups[0].isolated);
  EXPECT_TRUE(groups[0].knockout);
  EXPECT_EQ(fills[3].alpha, 1);
  EXPECT_EQ(fills[3].blend, graphics::BlendMode::kNormal);
  EXPECT_EQ(all<graphics::EndGroup>(list).size(), 1U);
  EXPECT_EQ(all<graphics::ClearSoftMask>(list).size(), 1U);
  EXPECT_TRUE(has_warning(list, "knockout"));
  EXPECT_TRUE(has_warning(list, "unknown blend mode") == false);

  // The SVG: blend modes as mix-blend-mode, the mask as an SVG mask, the group with its opacity.
  const std::string svg = graphics::to_svg(list);
  EXPECT_NE(svg.find("mix-blend-mode:multiply"), std::string::npos);
  EXPECT_NE(svg.find("mix-blend-mode:screen;isolation:isolate"), std::string::npos);
  EXPECT_NE(svg.find(R"(style="mask-type:luminance")"), std::string::npos);
  EXPECT_NE(svg.find(R"(mask="url(#m)"), std::string::npos);
  EXPECT_NE(svg.find(R"(<g opacity="0.5")"), std::string::npos);
  EXPECT_EQ(svg.find("<mask"), svg.rfind("<mask"));
  EXPECT_NE(svg.find("</mask>"), std::string::npos);
}

TEST(Interpreter, SoftMaskWithoutAGroupIsIgnored) {
  const auto list = interpret({.content = "/M gs 0 0 5 5 re f", .resources = "/ExtGState<</M<</SMask<</S/Alpha>>/BM/Nonsense>>>>"});
  EXPECT_TRUE(all<graphics::BeginSoftMask>(list).empty());
  EXPECT_TRUE(has_warning(list, "soft mask without"));
  EXPECT_TRUE(has_warning(list, "unknown blend mode"));
}

TEST(Interpreter, MalformedOperationsAreSkippedWithAWarning) {
  const auto list = interpret({.content = "(x) w /A 1 2 3 cm 0 0 1 1 re foo f BX bar EX"});
  EXPECT_EQ(all<graphics::FillPath>(list).size(), 1U);
  EXPECT_TRUE(has_warning(list, "non-numeric operand"));
  EXPECT_TRUE(has_warning(list, "too few operands"));
  EXPECT_TRUE(has_warning(list, "unknown operator foo"));
  EXPECT_FALSE(has_warning(list, "unknown operator bar"));  // inside BX/EX: silent
}

TEST(Interpreter, HugeOperandsAreClampedBeforeConversion) {
  // Found by fuzz_render: 4000000000 j converted a double out of int range (undefined behaviour).
  const auto list = interpret({.content = "4000000000 j -4000000000 J 99999999999 Tr 0 0 m 1 1 l S"});
  const auto strokes = all<graphics::StrokePath>(list);
  ASSERT_EQ(strokes.size(), 1U);
  EXPECT_EQ(list.styles[strokes[0].style].join, graphics::LineJoin::kBevel);
  EXPECT_EQ(list.styles[strokes[0].style].cap, graphics::LineCap::kButt);
}

TEST(Interpreter, LimitsStopThePage) {
  std::string many;
  for (int i = 0; i < 100; ++i) {
    many += "0 0 1 1 re f ";
  }
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R>>");
  b.add_stream(4, many);
  b.finish_table("/Size 5/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  InterpreterLimits limits;
  limits.max_commands = 10;
  const auto list = interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0}, limits);
  EXPECT_EQ(list->commands.size(), 10U);
  EXPECT_TRUE(has_warning(*list, "too many drawing commands"));
  EXPECT_EQ(list->media_box, (graphics::Rect{0, 0, 612, 792}));  // no MediaBox: US Letter
}

TEST(DisplayList, CompactCommandsAndSharedPaint) {
  // Every command used to be as large as a text run; the large kinds are boxed now.
  EXPECT_LE(sizeof(graphics::Command), 128U);
  // Consecutive paintings with the same colour and style share one table entry.
  const auto list = interpret({.content = "1 0 0 rg 0 0 1 1 re f 2 2 1 1 re f 0 0 m 1 1 l S 0 0 m 2 2 l S 0 1 0 rg 0 0 1 1 re f"});
  EXPECT_EQ(list.colors.size(), 3U);  // red fill, black stroke, green fill
  EXPECT_EQ(list.styles.size(), 1U);
  graphics::Path path;
  path.move_to({0, 0});
  path.line_to({1, 0});
  path.cubic_to({1, 1}, {2, 2}, {3, 3});
  path.close();
  std::vector<graphics::SegmentKind> kinds;
  std::vector<graphics::Point> ends;
  path.for_each([&](graphics::SegmentKind kind, const graphics::Point* p) {
    kinds.push_back(kind);
    if (kind == graphics::SegmentKind::kCubic) {
      ends.push_back(p[2]);
    }
  });
  EXPECT_EQ(kinds.size(), 4U);
  EXPECT_EQ(ends, (std::vector<graphics::Point>{{3, 3}}));
}

TEST(Svg, WritesPathsClipsAndEscapedText) {
  const auto list = interpret({.content = "q 0 0 50 50 re W n 1 0 0 rg 0 0 10 10 re f Q BT /F1 12 Tf 10 10 Td (<a&b>) Tj ET",
                               .resources = "/Font<</F1<</Subtype/Type1/BaseFont/Courier>>>>"});
  const std::string svg = graphics::to_svg(list);
  EXPECT_EQ(svg.rfind("<svg", 0), 0U);
  EXPECT_NE(svg.find(R"(width="200" height="100")"), std::string::npos);
  EXPECT_NE(svg.find("<clipPath"), std::string::npos);
  EXPECT_NE(svg.find(R"(fill="#ff0000")"), std::string::npos);
  EXPECT_EQ(svg.find("<a&b>"), std::string::npos);                       // the document's text never becomes markup
  EXPECT_NE(svg.find("&#x25AF;a&#x25AF;b&#x25AF;"), std::string::npos);  // Courier, no /Encoding: Latin codes
  // A font with a custom encoding is drawn as boxes, not as made-up letters.
  const auto custom = interpret({.content = "BT /F1 12 Tf (ab) Tj ET",
                                 .resources = "/Font<</F1<</Subtype/Type1/BaseFont/X/FirstChar 97/LastChar 98/Widths[500 500]"
                                              "/Encoding<</Differences[97/c/d]>>>>>>"});
  const std::string boxes = graphics::to_svg(custom);
  EXPECT_EQ(boxes.find("<text"), std::string::npos);
  EXPECT_NE(boxes.find("v-0.68"), std::string::npos);
  // Balanced groups.
  std::size_t open = 0;
  std::size_t close = 0;
  for (std::size_t at = svg.find("<g"); at != std::string::npos; at = svg.find("<g", at + 1)) {
    ++open;
  }
  for (std::size_t at = svg.find("</g>"); at != std::string::npos; at = svg.find("</g>", at + 1)) {
    ++close;
  }
  EXPECT_EQ(open, close);
  for (const char c : svg) {
    EXPECT_LT(static_cast<unsigned char>(c), 0x80);  // ASCII only
  }
}

TEST(Svg, RotatedPagesSwapTheirSides) {
  const std::string svg = graphics::to_svg(interpret({.content = "", .page_extra = "/Rotate 90"}));
  EXPECT_NE(svg.find(R"(width="100" height="200")"), std::string::npos);
}

}  // namespace
}  // namespace capriole::content
