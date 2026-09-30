#include "capriole/text/text.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/text_string.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::text {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

// A font whose glyphs are all 500 units wide (half an em): at size 10 each character is 5 points.
const std::string kMonospace = [] {
  std::string widths;
  for (int c = 32; c <= 126; ++c) {
    widths += "500 ";
  }
  return "<</Type/Font/Subtype/Type1/BaseFont/Courier/FirstChar 32/LastChar 126/Widths[" + widths + "]>>";
}();

// A one-page document (200 × 100 points) with font /F1 (kMonospace, object 12), the content, extra
// resources and objects.
struct Page {
  std::string content = {};
  std::string resources = {};
  std::vector<std::pair<std::uint32_t, std::string>> objects = {};
  std::string page_extra = {};
};

PageText extract_page(const Page& page, Order order = Order::kContent) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 200 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<</Font<</F1 12 0 R>>" + page.resources + ">>" + page.page_extra + ">>");
  b.add_stream(4, page.content);
  b.add_object(12, kMonospace);
  for (const auto& [number, body] : page.objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 30/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = content::interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0}, {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value()) << list.error().message;
  return extract(*list, order);
}

std::string text_of(const Word& word) { return model::to_utf8(word.text); }

TEST(TextString, DecodesTheThreeEncodings) {
  // UTF-16BE with a surrogate pair (U+1D11E), PDFDocEncoding (0x80 bullet, 0x93 fi ligature, 0xE9 é),
  // UTF-8 with its mark.
  EXPECT_EQ(model::decode_text_string(std::string("\xFE\xFF\x00\x41\xD8\x34\xDD\x1E", 8)), (std::u32string{U'A', 0x1D11E}));
  EXPECT_EQ(model::decode_text_string("\x80\x93\xE9"), (std::u32string{0x2022, 0xFB01, 0xE9}));
  EXPECT_EQ(model::decode_text_string("\xEF\xBB\xBF\xC3\xA8"), std::u32string(1, 0xE8));
  // Unpaired surrogates and truncated UTF-8 become U+FFFD.
  EXPECT_EQ(model::decode_text_string(std::string("\xFE\xFF\xD8\x00", 4)), std::u32string(1, 0xFFFD));
  EXPECT_EQ(model::decode_text_string("\xEF\xBB\xBF\xE2\x82"), (std::u32string{0xFFFD, 0xFFFD}));
  EXPECT_EQ(model::to_utf8(std::u32string{U'a', 0xE9, 0x20AC, 0x1D11E, 0xD800}), "a\xC3\xA9\xE2\x82\xAC\xF0\x9D\x84\x9E\xEF\xBF\xBD");
}

TEST(Text, WordsBoxesAndProvenance) {
  // "Hello world" at size 10 from (10, 50): 5 points per character, the space included.
  const std::string content = "BT /F1 10 Tf 10 50 Td (Hello world) Tj ET";
  const PageText page = extract_page({.content = content});
  ASSERT_EQ(page.words.size(), 2U);
  EXPECT_EQ(text_of(page.words[0]), "Hello");
  EXPECT_EQ(text_of(page.words[1]), "world");
  EXPECT_EQ(page.plain(), "Hello world\n");
  EXPECT_DOUBLE_EQ(page.width, 200);
  EXPECT_DOUBLE_EQ(page.height, 100);
  // Boxes: y down from the top of the page; "world" starts after "Hello " (6 characters).
  EXPECT_DOUBLE_EQ(page.words[0].box.x0, 10);
  EXPECT_DOUBLE_EQ(page.words[0].box.x1, 35);
  EXPECT_DOUBLE_EQ(page.words[1].box.x0, 40);
  EXPECT_LT(page.words[0].box.y0, 50);  // above the baseline (y = 100 − 50)
  EXPECT_GT(page.words[0].box.y1, 50);  // the descent below it
  // Provenance: the Tj operator (operation 3 counting from 0, at its offset in the content), the code's
  // byte.
  ASSERT_EQ(page.chars.size(), 10U);
  const Provenance& w = page.chars[page.words[1].first].provenance;
  EXPECT_FALSE(w.stream.has_value());
  EXPECT_EQ(w.offset, content.find("Tj"));
  EXPECT_EQ(w.operation, 3U);
  EXPECT_EQ(w.byte, 6U);
  ASSERT_TRUE(w.font.has_value());
  EXPECT_EQ(w.font->number, 12U);
}

TEST(Text, GapsMakeWordsCharacterSpacingDoesNot) {
  // TJ: -20 is kerning, -300 a word space (0.3 em). Tc 0.5 (letter spacing of 0.05 em at size 10)
  // keeps a word; Tc 4 (0.4 em) splits it into letters, as MuPDF and pdftotext do.
  EXPECT_EQ(extract_page({.content = "BT /F1 10 Tf 10 50 Td [(Hel)-20(lo)-300(world)] TJ ET"}).plain(), "Hello world\n");
  EXPECT_EQ(extract_page({.content = "BT /F1 10 Tf 0.5 Tc 10 50 Td (Hello) Tj ET"}).plain(), "Hello\n");
  EXPECT_EQ(extract_page({.content = "BT /F1 10 Tf 4 Tc 10 50 Td (ABC) Tj ET"}).plain(), "A B C\n");
  // Characters placed one by one (Td for each) still make one word.
  EXPECT_EQ(extract_page({.content = "BT /F1 10 Tf 10 50 Td (a) Tj 5 0 Td (b) Tj 5 0 Td (c) Tj ET"}).plain(), "abc\n");
}

TEST(Text, OverprintedTextIsReadOnceDoubleLettersStay) {
  // Fake bold: the word drawn twice, 0.3 points apart. "ll" in "allein" stays.
  const PageText bold = extract_page({.content = "BT /F1 10 Tf 10 50 Td (Bold) Tj ET BT /F1 10 Tf 10.3 50 Td (Bold) Tj ET"});
  EXPECT_EQ(bold.plain(), "Bold\n");
  EXPECT_EQ(extract_page({.content = "BT /F1 10 Tf 10 50 Td (allein) Tj ET"}).plain(), "allein\n");
}

TEST(Text, ToUnicodeLigaturesAndMissingText) {
  // A composite font: code 1 → "f", code 2 → the ligature U+FB01 (expanded to "fi"), code 3 unmapped (no
  // program to ask either): U+FFFD.
  const std::string cmap =
      "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /T def 1 begincodespacerange <0000> <FFFF> endcodespacerange "
      "2 beginbfchar <0001> <0066> <0002> <FB01> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end";
  const PageText page = extract_page({.content = "BT /F2 10 Tf 10 50 Td <000100020003> Tj ET",
                                      .resources = "/Font<</F2 20 0 R>>",
                                      .objects = {{20, "<</Type/Font/Subtype/Type0/BaseFont/X/Encoding/Identity-H/DescendantFonts[21 0 R]/ToUnicode 22 0 R>>"},
                                                  {21, "<</Type/Font/Subtype/CIDFontType2/BaseFont/X/DW 500>>"},
                                                  {22, "<</Length " + std::to_string(cmap.size()) + ">>\nstream\n" + cmap + "\nendstream"}}});
  EXPECT_EQ(page.plain(), "ffi\xEF\xBF\xBD\n");
}

TEST(Text, ActualTextReplacesItsGlyphs) {
  // /ActualText (§14.9.4): inline and named in /Properties, UTF-16 — it replaces every glyph of its
  // sequence, the space included; an empty one hides its glyphs.
  const PageText page = extract_page({.content = "BT /F1 10 Tf 10 50 Td /Span<</ActualText(Aa)>>BDC (xx) Tj EMC (b) Tj "
                                                 "/Span/P1 BDC ( z) Tj EMC /Span<</ActualText()>>BDC (hidden) Tj EMC ET",
                                      .resources = "/Properties<</P1<</ActualText<FEFF00E9>>>>>"});
  EXPECT_EQ(page.plain(), "Aab\xC3\xA9\n");
}

TEST(Text, LinesAndBlocksInContentOrder) {
  // Two lines of a paragraph (pitch 12 at size 10), then a block far below: two blocks.
  const PageText page = extract_page({.content = "BT /F1 10 Tf 10 80 Td (one two) Tj 0 -12 Td (three) Tj 0 -50 Td (four) Tj ET"});
  ASSERT_EQ(page.blocks.size(), 2U);
  EXPECT_EQ(page.blocks[0].count, 2U);
  EXPECT_EQ(page.lines.size(), 3U);
  EXPECT_EQ(page.plain(), "one two\nthree\n\nfour\n");
}

TEST(Text, ColumnsInTheLayoutOrder) {
  // Two columns written across, line by line: the content order reads across; the layout order reads
  // the left column, then the right one.
  const std::string content = "BT /F1 10 Tf 10 80 Td (L1) Tj 100 0 Td (R1) Tj -100 -12 Td (L2) Tj 100 0 Td (R2) Tj -100 -12 Td (L3) Tj 100 0 Td (R3) Tj ET";
  EXPECT_EQ(extract_page({.content = content}).plain(), "L1\nR1\nL2\nR2\nL3\nR3\n");
  const PageText layout = extract_page({.content = content}, Order::kLayout);
  ASSERT_EQ(layout.blocks.size(), 2U);
  EXPECT_EQ(layout.plain(), "L1\nL2\nL3\n\nR1\nR2\nR3\n");
}

TEST(Text, RotatedPagesAndTheCropBox) {
  // /Rotate 90: the page is shown 100 × 200, the text runs downwards (angle 90). Text outside the
  // crop box is not on the page.
  const PageText page = extract_page({.content = "BT /F1 10 Tf 10 50 Td (Up) Tj 300 0 Td (Off) Tj ET", .page_extra = "/Rotate 90"});
  EXPECT_DOUBLE_EQ(page.width, 100);
  EXPECT_DOUBLE_EQ(page.height, 200);
  ASSERT_EQ(page.words.size(), 1U);
  EXPECT_EQ(text_of(page.words[0]), "Up");
  EXPECT_EQ(page.chars[0].angle, 90);
  EXPECT_NEAR(page.chars[0].origin.x, 50, 1e-9);  // x = y of the unrotated page (top 100 − 50)
  EXPECT_NEAR(page.chars[0].origin.y, 10, 1e-9);
}

TEST(Text, FormXObjectsAreTheirProvenance) {
  const std::string form = "BT /F1 10 Tf 0 0 Td (Form) Tj ET";
  const PageText page = extract_page({.content = "q 1 0 0 1 10 50 cm /Fm Do Q",
                                      .resources = "/XObject<</Fm 30 0 R>>",
                                      .objects = {{30, "<</Type/XObject/Subtype/Form/BBox[0 -20 100 20]/Resources<</Font<</F1 12 0 R>>>>/Length " +
                                                           std::to_string(form.size()) + ">>\nstream\n" + form + "\nendstream"}}});
  ASSERT_EQ(page.words.size(), 1U);
  const Provenance& p = page.chars[0].provenance;
  ASSERT_TRUE(p.stream.has_value());
  EXPECT_EQ(p.stream->number, 30U);
  EXPECT_EQ(p.offset, form.find("Tj"));
  EXPECT_EQ(p.operation, 3U);
}

TEST(Text, InvisibleTextIsExtractedAndMarked) {
  // Render mode 3 (the text layer over a scan): read, marked invisible.
  const PageText page = extract_page({.content = "BT /F1 10 Tf 3 Tr 10 50 Td (Scan) Tj ET"});
  ASSERT_EQ(page.chars.size(), 4U);
  EXPECT_TRUE(page.chars[0].invisible);
  EXPECT_EQ(page.plain(), "Scan\n");
}

}  // namespace
}  // namespace capriole::text
