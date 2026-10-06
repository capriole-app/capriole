// Editing a page's text (PLAN.md phase 8j).

#include "capriole/edit/text_edit.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"
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

// One page 300 × 200 (as shown), content given, Helvetica as /F1 (no encoding: the standard one).
// annotation: a free-text annotation at 20 20 120 40 whose appearance shows that text.
std::string page_pdf(const std::string& content, const std::string& annotation = "",
                     const std::string& font = "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>") {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, std::string("<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>") +
                      (annotation.empty() ? "" : "/Annots[6 0 R]") + ">>");
  b.add_stream(4, content);
  b.add_object(5, font);
  if (!annotation.empty()) {
    b.add_object(6, "<</Type/Annot/Subtype/FreeText/Rect[20 20 120 40]/DA(/F1 12 Tf 0 g)/AP<</N 7 0 R>>>>");
    b.add_stream(7, "BT /F1 12 Tf 2 5 Td (" + annotation + ") Tj ET", "/Type/XObject/Subtype/Form/BBox[0 0 100 20]/Resources<</Font<</F1 5 0 R>>>>");
  }
  b.finish_table("/Root 1 0 R");
  return b.str();
}

text::PageText page_text(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  auto list = content::interpret_page(document, (*pages)[0], {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value());
  return text::extract(*list, text::Order::kContent);
}

const text::Word* word(const text::PageText& t, const std::u32string& w) {
  for (const auto& x : t.words) {
    if (x.text == w) {
      return &x;
    }
  }
  return nullptr;
}

graphics::Rect around(const text::Word& w) { return {w.box.x0 - 0.5, w.box.y0 - 0.5, w.box.x1 + 0.5, w.box.y1 + 0.5}; }

std::string content_of(Document& document) {
  auto pages = document.page_refs();
  auto data = content::page_content(document, (*pages)[0]);
  EXPECT_TRUE(data.has_value());
  return std::string(crypto::der::chars_of(*data));
}

TEST(TextEdit, WritesInTheSameFontWhenItCan) {
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello world) Tj ET\n0 0 1 rg 10 10 50 50 re f"));
  auto before = page_text(document);
  const auto* world = word(before, U"world");
  ASSERT_NE(world, nullptr);
  auto result = replace_text(document, 0, around(*world), "planet");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->font, "Helvetica");
  EXPECT_EQ(result->replaced, "world");
  auto after = page_text(document);
  const auto* planet = word(after, U"planet");
  ASSERT_NE(planet, nullptr);
  EXPECT_NEAR(planet->box.x0, world->box.x0, 0.01);
  EXPECT_NE(word(after, U"Hello"), nullptr);
  EXPECT_EQ(word(after, U"world"), nullptr);
  // Nothing else of the content changed: the rectangle is there as it was.
  EXPECT_NE(content_of(document).find("0 0 1 rg 10 10 50 50 re f"), std::string::npos);
}

TEST(TextEdit, SplicesATJArray) {
  // Kerning inside the word replaced goes with it; the rest keeps its own.
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td [(Wo) 50 (rd) -200 (after)] TJ ET"));
  auto before = page_text(document);
  const auto* w = word(before, U"Word");
  ASSERT_NE(w, nullptr);
  auto result = replace_text(document, 0, around(*w), "Text");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  auto after = page_text(document);
  EXPECT_NE(word(after, U"Text"), nullptr);
  EXPECT_NE(word(after, U"after"), nullptr);
  const std::string content = content_of(document);
  EXPECT_NE(content.find("-200"), std::string::npos);
  EXPECT_EQ(content.find(" 50 "), std::string::npos);
}

TEST(TextEdit, FallsBackOnASimilarFont) {
  // Helvetica's standard encoding has no Euro sign: the old glyphs go, the new text is drawn where they began.
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (Price 10 Euro) Tj ET"));
  auto before = page_text(document);
  const auto* euro = word(before, U"Euro");
  ASSERT_NE(euro, nullptr);
  auto result = replace_text(document, 0, around(*euro), "\xE2\x82\xAC");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
  EXPECT_EQ(result->font, "Helvetica");
  auto after = page_text(document);
  const auto* sign = word(after, U"€");
  ASSERT_NE(sign, nullptr);
  EXPECT_NEAR(sign->box.x0, euro->box.x0, 0.5);
  EXPECT_EQ(word(after, U"Euro"), nullptr);
  const auto* price = word(after, U"Price");
  ASSERT_NE(price, nullptr);
  EXPECT_NEAR(price->box.x0, word(before, U"Price")->box.x0, 0.01);
}

TEST(TextEdit, DeletesAndRefuses) {
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (one two) Tj 0 -20 Td (three) Tj ET BT 3 Tr /F1 12 Tf 20 80 Td (hidden) Tj ET"));
  auto before = page_text(document);
  const auto* two = word(before, U"two");
  ASSERT_NE(two, nullptr);
  auto deleted = replace_text(document, 0, around(*two), "");
  ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
  EXPECT_EQ(word(page_text(document), U"two"), nullptr);
  EXPECT_NE(word(page_text(document), U"one"), nullptr);
  // Two lines; invisible text; nothing there; a line break in the new text.
  auto lines = replace_text(document, 0, graphics::Rect{0, 0, 300, 200}, "x");
  ASSERT_FALSE(lines.has_value());
  EXPECT_NE(lines.error().message.find("one line"), std::string::npos);
  const auto* hidden = word(before, U"hidden");
  ASSERT_NE(hidden, nullptr);
  auto invisible = replace_text(document, 0, around(*hidden), "x");
  ASSERT_FALSE(invisible.has_value());
  EXPECT_NE(invisible.error().message.find("invisible"), std::string::npos);
  auto nothing = replace_text(document, 0, graphics::Rect{200, 10, 250, 20}, "x");
  ASSERT_FALSE(nothing.has_value());
  EXPECT_EQ(nothing.error().code, ErrorCode::kOutOfRange);
  const auto* three = word(before, U"three");
  EXPECT_FALSE(replace_text(document, 0, around(*three), "a\nb").has_value());
}

// Helvetica with /Widths from 32 to 126, all 500 but Q's (81), 0: a code the document never used, or
// one a damaged font draws overlapped.
std::string helvetica_without_q() {
  std::string widths;
  for (int code = 32; code <= 126; ++code) {
    widths += code == 81 ? "0 " : "500 ";
  }
  return "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/FirstChar 32/LastChar 126/Widths[" + widths + "]>>";
}

// A code the font's /Widths left at 0 was written with no width, its glyphs overlapped: it is given the
// program's — unless the page shows it so already, where that would move the old text (both found by
// corpus-tools/check_text_edit.py).
TEST(TextEdit, CodesOfNoWidthAreGivenOneWhenUnused) {
  Document unused = open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello world) Tj ET", "", helvetica_without_q()));
  const auto unused_before = page_text(unused);
  const auto* world = word(unused_before, U"world");
  ASSERT_NE(world, nullptr);
  auto result = replace_text(unused, 0, around(*world), "Quiet");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  const auto unused_after = page_text(unused);
  const auto* quiet = word(unused_after, U"Quiet");
  ASSERT_NE(quiet, nullptr);
  EXPECT_GT(quiet->box.width(), 5 * 6.0);  // Q has its width: wider than the four other letters

  Document used = open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello world) Tj 0 -40 Td (QQ) Tj ET", "", helvetica_without_q()));
  auto before = page_text(used);
  const auto* qq = word(before, U"QQ");
  ASSERT_NE(qq, nullptr);
  auto fallback = replace_text(used, 0, around(*word(before, U"world")), "Quiet");
  ASSERT_TRUE(fallback.has_value()) << fallback.error().message;
  EXPECT_EQ(fallback->method, TextEditMethod::kSimilarFont);
  const auto used_after = page_text(used);
  const auto* still = word(used_after, U"QQ");
  ASSERT_NE(still, nullptr);
  EXPECT_NEAR(still->box.x1, qq->box.x1, 0.01);
}

// The text of an annotation's appearance (a signature's, here a free-text annotation's) is not the page's
// content: refused, where the old text stayed and the new was drawn over it (found by
// corpus-tools/check_text_edit.py, on signature widgets).
TEST(TextEdit, AnnotationTextIsRefused) {
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (page text) Tj ET", "signed"));
  auto before = page_text(document);
  const auto* signed_word = word(before, U"signed");
  ASSERT_NE(signed_word, nullptr);
  auto result = replace_text(document, 0, around(*signed_word), "x");
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("annotation"), std::string::npos);
  EXPECT_NE(word(page_text(document), U"signed"), nullptr);
}

}  // namespace
}  // namespace capriole::edit
