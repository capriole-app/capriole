// Annotations (§12.5): appearances placed on their rectangles, states, flags, opacity, and the
// appearances made for markup annotations and links that come without one.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/document/document.hpp"
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

std::string stream_object(const std::string& dict, const std::string& data) {
  return "<<" + dict + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream";
}

// A page whose /Annots is `annots` (objects from 10), with `content` as its content.
DisplayList interpret(const std::string& annots, const std::vector<std::pair<std::uint32_t, std::string>>& objects = {}, const std::string& content = "",
                      bool content_readable = true) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 200 200]>>");
  b.add_object(3, std::string("<</Type/Page/Parent 2 0 R/Contents ") + (content_readable ? "4 0 R" : "99 0 R") + "/Annots " + annots + ">>");
  b.add_stream(4, content);
  for (const auto& [number, body] : objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 40/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0});
  EXPECT_TRUE(list.has_value());
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
  return std::ranges::any_of(list.warnings, [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

TEST(Annotations, AppearanceIsPlacedOnTheRect) {
  // BBox [0 0 10 10] turned by /Matrix (a quarter turn): its box, [-10 0 0 10], is mapped onto the Rect.
  const auto list = interpret("[10 0 R]", {{10, "<</Type/Annot/Subtype/Stamp/Rect[100 100 120 140]/AP<</N 11 0 R>>>>"},
                                           {11, stream_object("/Type/XObject/Subtype/Form/BBox[0 0 10 10]/Matrix[0 1 -1 0 0 0]", "0 0 10 10 re f")}});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 1U);
  // Matrix then A = [2 0 0 4 120 100]: the form's point (0, 0) lands on (120, 100).
  EXPECT_EQ(fills[0].ctm, (Matrix{0, 4, -2, 0, 120, 100}));
  EXPECT_EQ(all<graphics::ClipPath>(list).size(), 1U);  // the form's bounding box
}

TEST(Annotations, StatesFlagsAndOpacity) {
  const std::string on = stream_object("/Type/XObject/Subtype/Form/BBox[0 0 10 10]", "1 0 0 rg 0 0 10 10 re f");
  const std::string off = stream_object("/Type/XObject/Subtype/Form/BBox[0 0 10 10]", "0 0 1 rg 0 0 10 10 re f");
  const auto list =
      interpret("[10 0 R 12 0 R 13 0 R 14 0 R 15 0 R]", {{10, "<</Type/Annot/Subtype/Widget/Rect[0 0 10 10]/AS/On/AP<</N<</On 20 0 R/Off 21 0 R>>>>>>"},
                                                         {12, "<</Type/Annot/Subtype/Widget/Rect[0 0 10 10]/F 2/AP<</N 20 0 R>>>>"},   // Hidden
                                                         {13, "<</Type/Annot/Subtype/Widget/Rect[0 0 10 10]/F 32/AP<</N 20 0 R>>>>"},  // NoView
                                                         {14, "<</Type/Annot/Subtype/Popup/Rect[0 0 10 10]/AP<</N 20 0 R>>>>"},
                                                         {15, "<</Type/Annot/Subtype/Stamp/Rect[0 0 10 10]/CA 0.5/AP<</N 21 0 R>>>>"},
                                                         {20, on},
                                                         {21, off}});
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 2U);
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{1, 0, 0}));  // /AS /On
  const auto groups = all<graphics::BeginGroup>(list);
  ASSERT_EQ(groups.size(), 1U);
  EXPECT_DOUBLE_EQ(groups[0].alpha, 0.5);
}

TEST(Annotations, GeneratedMarkupAppearances) {
  const auto list = interpret(
      "[<</Type/Annot/Subtype/Square/Rect[10 10 50 30]/C[1 0 0]/IC[0 0 1]/BS<</W 2>>>>"
      "<</Type/Annot/Subtype/Circle/Rect[60 10 100 30]/C[0 1 0]>>"
      "<</Type/Annot/Subtype/Line/Rect[0 0 200 200]/L[0 0 100 100]/C[0]>>"
      "<</Type/Annot/Subtype/Ink/Rect[0 0 200 200]/InkList[[1 1 5 5 9 1]]/C[0 0 0]>>"
      "<</Type/Annot/Subtype/Highlight/Rect[0 0 200 200]/QuadPoints[10 120 90 120 10 110 90 110]/C[1 1 0]>>"
      "<</Type/Annot/Subtype/StrikeOut/Rect[0 0 200 200]/QuadPoints[10 140 90 140 10 130 90 130]/C[1 0 0]>>"
      "<</Type/Annot/Subtype/Square/Rect[10 10 50 30]/Border[0 0 0]>>]");
  const auto fills = all<graphics::FillPath>(list);
  const auto strokes = all<graphics::StrokePath>(list);
  ASSERT_EQ(fills.size(), 2U);    // the square's interior, the highlight
  ASSERT_EQ(strokes.size(), 5U);  // square, circle, line, ink, strike-out
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{0, 0, 1}));
  EXPECT_EQ(fills[1].blend, graphics::BlendMode::kMultiply);  // a highlight keeps the text readable
  EXPECT_DOUBLE_EQ(list.styles[strokes[0].style].width, 2);
  EXPECT_EQ(list.styles[strokes[3].style].cap, graphics::LineCap::kRound);  // ink
}

TEST(Annotations, LinkBorders) {
  const auto list = interpret(
      "[<</Type/Annot/Subtype/Link/Rect[10 10 50 30]/Border[0 0 1]/C[0 1 0]>>"
      "<</Type/Annot/Subtype/Link/Rect[10 50 50 70]/BS<</W 1/S/U>>/C[0 0 1]>>"
      "<</Type/Annot/Subtype/Link/Rect[10 90 50 110]/BS<</W 1>>>>"  // no colour: black, as readers draw it
      "<</Type/Annot/Subtype/Link/Rect[10 130 50 150]/Border[0 0 0]/C[1 0 0]>>"
      "<</Type/Annot/Subtype/Link/Rect[10 170 50 190]>>]");  // no border given: nothing
  const auto strokes = all<graphics::StrokePath>(list);
  ASSERT_EQ(strokes.size(), 3U);
  EXPECT_EQ(list.colors[strokes[0].color].rgb, (graphics::Rgb{0, 1, 0}));
  EXPECT_EQ(strokes[1].path.size(), 2U);  // /U: one line along the bottom
  EXPECT_EQ(list.colors[strokes[2].color].rgb, (graphics::Rgb{0, 0, 0}));
}

TEST(Annotations, WithoutAnAppearanceOrAPageContent) {
  // A widget without an appearance is reported; a page without content still shows its annotations.
  const auto list = interpret("[<</Type/Annot/Subtype/Widget/Rect[0 0 10 10]/FT/Tx/V(x)>> 10 0 R]",
                              {{10, "<</Type/Annot/Subtype/Stamp/Rect[0 0 10 10]/AP<</N 11 0 R>>>>"},
                               {11, stream_object("/Type/XObject/Subtype/Form/BBox[0 0 10 10]", "0 0 10 10 re f")}},
                              "", false);
  EXPECT_TRUE(has_warning(list, "/Widget annotations without an appearance"));
  EXPECT_EQ(all<graphics::FillPath>(list).size(), 1U);
}

}  // namespace
}  // namespace capriole::content
