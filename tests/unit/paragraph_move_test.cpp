// A longer paragraph given room by moving what follows it down the page (PLAN.md phase 9d).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/paragraph_edit.hpp"
#include "capriole/text/text.hpp"
#include "edit/common.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  std::memcpy(out.data(), text.data(), text.size());
  return out;
}

Document open(const std::string& pdf) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(pdf)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

// One page 400 × 400, Helvetica as /F1; a form /Fm1 (a 50 × 10 box) and a link (as page space, from y 200
// to 210) when asked.
std::string page_pdf(const std::string& content, bool link = false) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, std::string("<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>/XObject<</Fm1 6 0 R>>>>") +
                      (link ? "/Annots[7 0 R]" : "") + ">>");
  b.add_stream(4, content);
  b.add_object(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  b.add_stream(6, "0 0 50 10 re f", "/Type/XObject/Subtype/Form/BBox[0 0 50 10]");
  b.add_object(7, "<</Type/Annot/Subtype/Link/Rect[20 200 120 210]/Border[0 0 0]>>");
  b.finish_table("/Root 1 0 R");
  return b.str();
}

// Three lines of 10 pt Helvetica, 12 pt apart, from (20, 360) as page space (as shown, baselines at 40, 52
// and 64), and in the same BT a line below them (as shown at 94).
const std::string kParagraph =
    "BT /F1 10 Tf 12 TL 20 360 Td (The quick brown fox jumps over the lazy dog and runs) Tj T* (far away into the deep dark woods where) Tj T* "
    "(nobody can find it.) Tj 0 -30 Td (Belowtext follows here.) Tj ET\n";
const std::string kLonger =
    "The quick brown fox jumps over the lazy dog and runs far away into the deep dark woods where nobody can find it, not even "
    "the hunters with their dogs, who look for it all day long and then go back home.";
const graphics::Rect kArea{15, 30, 390, 70};

text::PageText page_text(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  auto list = content::interpret_page(document, (*pages)[0], {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value());
  return text::extract(*list, text::Order::kLayout);
}

std::optional<graphics::Rect> box_of(const text::PageText& t, const std::u32string& w) {
  for (const auto& x : t.words) {
    if (x.text == w) {
      return x.box;
    }
  }
  return std::nullopt;
}

graphics::DisplayList draw(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  auto list = content::interpret_page(document, (*pages)[0]);
  EXPECT_TRUE(list.has_value());
  return std::move(*list);
}

// The bottom (page space) of the filled paths as wide as width.
std::vector<double> fills(const graphics::DisplayList& list, double width) {
  std::vector<double> out;
  for (const auto& command : list.commands) {
    if (const auto* fill = std::get_if<graphics::FillPath>(&command)) {
      const auto b = detail::path_bounds(fill->path);
      if (b) {
        const graphics::Rect r = detail::transformed(*b, fill->ctm);
        if (std::abs(r.width() - width) < 0.01) {
          out.push_back(r.y0);
        }
      }
    }
  }
  return out;
}

TEST(ParagraphMove, WhatFollowsMovesDownAsFarAsTheParagraphGrows) {
  const std::string content = kParagraph +
                              "20 280 200 4 re f\n"
                              "q 20 0 0 20 20 250 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q\n"
                              "q 1 0 0 1 20 220 cm /Fm1 Do Q\n"
                              "BT /F1 10 Tf 300 300 Td (Beside) Tj ET\n"
                              "BT /F1 10 Tf 200 20 Td (Seven) Tj ET\n";
  Document document = open(page_pdf(content, true));
  const auto before = page_text(document);
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->lines, 4U) << "the new text takes one line more";
  EXPECT_DOUBLE_EQ(result->moved, 12);
  EXPECT_EQ(result->moved_items, 4U);  // the line below, the rule, the image, the form
  EXPECT_EQ(result->moved_annotations, 1U);
  const auto after = page_text(document);
  const auto below = box_of(after, U"Belowtext");
  ASSERT_TRUE(below.has_value());
  EXPECT_NEAR(below->y0, box_of(before, U"Belowtext")->y0 + 12, 0.01);
  EXPECT_NEAR(below->x0, box_of(before, U"Belowtext")->x0, 0.01);
  // Beside the paragraph, and the footer: where they were.
  EXPECT_NEAR(box_of(after, U"Beside")->y0, box_of(before, U"Beside")->y0, 0.01);
  EXPECT_NEAR(box_of(after, U"Seven")->y0, box_of(before, U"Seven")->y0, 0.01);
  // The rule, the form's box, the image, the link: 12 lower.
  const auto list = draw(document);
  EXPECT_EQ(fills(list, 200), std::vector<double>{268});
  EXPECT_EQ(fills(list, 50), std::vector<double>{208});
  bool image = false;
  for (const auto& command : list.commands) {
    if (const auto* drawn = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      EXPECT_NEAR((*drawn)->ctm.f, 238, 0.01);
      image = true;
    }
  }
  EXPECT_TRUE(image);
  auto link = document.get(model::ObjectRef{7, 0});
  ASSERT_TRUE(link.has_value());
  const auto* rect = (*link)->as_dict()->find("Rect")->as_array();
  EXPECT_NEAR((*rect)[1].as_number().value_or(0), 188, 0.01);
  EXPECT_NEAR((*rect)[3].as_number().value_or(0), 198, 0.01);
  // The paragraph's text, then what follows, in order.
  const std::string plain = after.plain();
  EXPECT_LT(plain.find("home."), plain.find("Belowtext"));
}

// A line below drawn by two operators, the second beyond the paragraph's width: it moves whole (found by
// corpus-tools/check_paragraph_move.py on a form whose lines' ends stayed behind, halfway through a word).
TEST(ParagraphMove, ALineBelowMovesWholeBeyondTheParagraphsWidth) {
  Document document =
      open(page_pdf(kParagraph + "BT /F1 10 Tf 20 280 Td (A line below which goes on and on and on beyond the) Tj 242 0 Td (graph's width.) Tj ET\n"));
  const auto before = page_text(document);
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto after = page_text(document);
  for (const std::u32string word : {U"goes", U"width."}) {
    ASSERT_TRUE(box_of(after, word).has_value());
    EXPECT_NEAR(box_of(after, word)->y0, box_of(before, word)->y0 + result->moved, 0.01);
  }
}

// A table below whose right border lies beyond the paragraph's width: the border moves with the table
// (found by corpus-tools/check_paragraph_move.py: a table's right edge stayed behind).
TEST(ParagraphMove, ATableBelowMovesWithItsBorderBeyondTheParagraphsWidth) {
  Document document = open(page_pdf(kParagraph + "200 250 m 300 250 l S\n300 250 m 300 200 l S\n200 200 m 300 200 l S\n200 250 m 200 200 l S\n"));
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_DOUBLE_EQ(result->moved, 12);
  std::vector<double> bottoms;  // of the four sides, in page space
  for (const auto& command : draw(document).commands) {
    if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
      const auto b = detail::path_bounds(stroke->path);
      ASSERT_TRUE(b.has_value());
      bottoms.push_back(detail::transformed(*b, stroke->ctm).y0);
    }
  }
  std::ranges::sort(bottoms);
  EXPECT_EQ(bottoms, (std::vector<double>{188, 188, 188, 238}));
}

TEST(ParagraphMove, WithoutTheChoiceThereIsNoRoom) {
  Document document = open(page_pdf(kParagraph));
  auto result = replace_paragraph(document, 0, kArea, kLonger);
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("no room"), std::string::npos);
}

// Refused — and the document left as it was.
void expect_refused(const std::string& content, const std::string& text, const std::string& why) {
  Document document = open(page_pdf(content));
  const std::string before = page_text(document).plain();
  auto result = replace_paragraph(document, 0, kArea, text, {.move = true});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find(why), std::string::npos) << result.error().message;
  EXPECT_EQ(page_text(document).plain(), before);
}

TEST(ParagraphMove, ABoxAroundTheParagraphIsRefusedSomethingAcrossItsBottomToo) {
  expect_refused(kParagraph + "15 300 370 75 re S\n", kLonger, "inside a box");
  // A rule from the middle of its last line down.
  expect_refused(kParagraph + "100 300 2 40 re f\n", kLonger, "across the paragraph's bottom");
}

// A frame that holds the paragraph and what follows, with room (a page's border): it stays, what follows
// moves inside it (found by corpus-tools/check_paragraph_move.py: pages with a border were refused).
TEST(ParagraphMove, AFrameWithRoomStaysWhatFollowsMovesInsideIt) {
  Document document = open(page_pdf("5 5 390 390 re S\n" + kParagraph));
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_DOUBLE_EQ(result->moved, 12);
  for (const auto& command : draw(document).commands) {
    if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
      const auto b = detail::path_bounds(stroke->path);
      ASSERT_TRUE(b.has_value());
      EXPECT_NEAR(detail::transformed(*b, stroke->ctm).y0, 5, 0.01);
    }
  }
}

// A page-sized image is a background only where it is of one colour below the paragraph: a scanned form
// under the text would be left behind by what moves (found by corpus-tools/check_paragraph_move.py: the
// numbers of a form moved off their boxes). A plain one is not in the way.
TEST(ParagraphMove, AScannedPageUnderTheTextIsRefusedAPlainBackgroundIsNot) {
  // 2 × 2 pixels over the page: the top row white, the bottom one black (a form's boxes).
  const std::string scan = "q 400 0 0 400 0 0 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \xff\xff\x01\x01 EI Q\n";
  const std::string plain = "q 400 0 0 400 0 0 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \xff\xff\xff\xff EI Q\n";
  expect_refused(scan + kParagraph, kLonger, "an image lies across the paragraph's bottom");
  Document document = open(page_pdf(plain + kParagraph));
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_DOUBLE_EQ(result->moved, 12);
}

TEST(ParagraphMove, WhatWouldGoPastThePageIsRefused) {
  std::string longest;
  for (int i = 0; i < 12; ++i) {
    longest += kLonger + " ";
  }
  expect_refused(kParagraph, longest, "past the bottom of the page");
}

TEST(ParagraphMove, WhatWouldRunIntoTheOtherColumnIsRefused) {
  // A rule across the page below the paragraph moves; the text of the other column under it stays.
  expect_refused(kParagraph + "20 300 360 1 re f\nBT /F1 10 Tf 300 290 Td (Beside) Tj ET\n", kLonger, "run into what is beside it");
}

TEST(ParagraphMove, AFooterStaysAndIsNotRunInto) {
  // The last line of the body, moved, would reach the footer.
  const std::string content =
      "BT /F1 10 Tf 12 TL 20 360 Td (The quick brown fox jumps over the lazy dog and runs) Tj T* (far away into the deep dark woods where) Tj T* "
      "(nobody can find it.) Tj ET\nBT /F1 10 Tf 20 300 Td (A line below.) Tj ET\nBT /F1 10 Tf 20 100 Td (Last line of the body.) Tj ET\nBT /F1 10 Tf 200 20 "
      "Td (Seven) Tj ET\n";
  expect_refused(content, kLonger + " " + kLonger + " " + kLonger, "reach the footer");
}

// Found by going through the moves corpus-tools/check_paragraph_move.py saw refused: what follows the
// paragraph though it starts above its bottom.
TEST(ParagraphMove, WhatStartsBesideTheLastLineOrIsTheNextLineSetTightFollows) {
  // A drawing beside the last line, touching none of its glyphs, down below it.
  {
    Document document = open(page_pdf(kParagraph + "200 300 60 40 re f\n"));
    auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(fills(draw(document), 60), std::vector<double>{300 - result->moved});
  }
  // The next line, in a larger size and set tight: its glyphs' boxes reach above the paragraph's bottom.
  {
    Document document = open(page_pdf(kParagraph + "BT /F1 20 Tf 20 324 Td (Tight) Tj ET\n"));
    const auto before = page_text(document);
    auto result = replace_paragraph(document, 0, graphics::Rect{15, 30, 390, 64}, kLonger, {.move = true});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_NEAR(box_of(page_text(document), U"Tight")->y0, box_of(before, U"Tight")->y0 + result->moved, 0.01);
  }
}

TEST(ParagraphMove, WhatLiesBetweenThingsThatMoveMovesWithThem) {
  // A mark left of the paragraph's width, between two lines below it that move (they start further left).
  Document document = open(page_pdf(kParagraph + "BT /F1 10 Tf 10 290 Td (One line below) Tj 0 -30 Td (Another one) Tj ET\n12 274 3 3 re f\n"));
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(fills(draw(document), 3), std::vector<double>{274 - result->moved});
}

TEST(ParagraphMove, TheParagraphsOwnLinkIsNotInTheWay) {
  // A link over its last line, a little lower than its glyphs.
  std::string pdf = page_pdf(kParagraph + "BT /F1 10 Tf 20 300 Td (Below) Tj ET\n", true);
  pdf.replace(pdf.find("/Rect[20 200 120 210]"), 21, "/Rect[20 332 100 345]");
  Document document = open(pdf);
  auto result = replace_paragraph(document, 0, kArea, kLonger, {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->moved_annotations, 0U);
}

TEST(ParagraphMove, AFullPageInABorderIsSaidToBeFull) {
  std::string longest;
  for (int i = 0; i < 12; ++i) {
    longest += kLonger + " ";
  }
  expect_refused("5 5 390 390 re S\n" + kParagraph, longest, "past the bottom of the page");
}

// The other column's lines stay, even close beside lines that move (a narrow gutter): only more of a line,
// or a small mark beside it, goes with it.
TEST(ParagraphMove, TheOtherColumnStaysAMarkGoes) {
  // A narrow paragraph; below it a line of the first column that ends 12.5 points before the second column's.
  Document document =
      open(page_pdf("BT /F1 10 Tf 12 TL 20 360 Td (The quick brown fox jumps) Tj T* (over the lazy dog and runs) Tj T* (far away into the woods.) Tj ET\n"
                    "BT /F1 10 Tf 20 306 Td (Below in the first column a long line here) Tj ET\n"
                    "BT /F1 10 Tf 212 320 Td (The second column) Tj 0 -14 Td (goes on beside it) Tj ET\n"
                    "BT /F1 10 Tf 10 306 Td (7) Tj ET\n"));
  const auto before = page_text(document);
  auto result =
      replace_paragraph(document, 0, kArea, "The quick brown fox jumps over the lazy dog and runs far away into the deep and dark woods.", {.move = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_GT(result->moved, 0);
  const auto after = page_text(document);
  EXPECT_NEAR(box_of(after, U"Below")->y0, box_of(before, U"Below")->y0 + result->moved, 0.01);
  EXPECT_NEAR(box_of(after, U"beside")->y0, box_of(before, U"beside")->y0, 0.01);        // the second column
  EXPECT_NEAR(box_of(after, U"7")->y0, box_of(before, U"7")->y0 + result->moved, 0.01);  // a mark
}

}  // namespace
}  // namespace capriole::edit
