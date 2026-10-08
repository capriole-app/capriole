// Editing a paragraph, laid out anew inside it (PLAN.md phase 9a).

#include "capriole/edit/paragraph_edit.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
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

// One page 400 × 400 (as shown), content given; Helvetica as /F1, Helvetica-Bold as /F2.
std::string page_pdf(const std::string& content) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R/F2 6 0 R>>>>>>");
  b.add_stream(4, content);
  b.add_object(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  b.add_object(6, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica-Bold>>");
  b.finish_table("/Root 1 0 R");
  return b.str();
}

// Three lines of 10 pt Helvetica, 12 pt apart, from (20, 360) as page space: as shown, baselines at 40,
// 52 and 64.
const std::string kParagraph =
    "BT /F1 10 Tf 12 TL 20 360 Td (The quick brown fox jumps over the lazy dog and runs) Tj T* (far away into the deep dark woods where) Tj T* "
    "(nobody can find it.) Tj ET\n";

text::PageText page_text(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  auto list = content::interpret_page(document, (*pages)[0], {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value());
  return text::extract(*list, text::Order::kLayout);
}

std::string plain(Document& document) { return page_text(document).plain(); }

const text::Word* word(const text::PageText& t, const std::u32string& w) {
  for (const auto& x : t.words) {
    if (x.text == w) {
      return &x;
    }
  }
  return nullptr;
}

// The lines whose text contains a word, as laid out.
std::vector<const text::Line*> lines_of(const text::PageText& t, double y0, double y1) {
  std::vector<const text::Line*> out;
  for (const auto& line : t.lines) {
    if (line.box.y0 >= y0 && line.box.y1 <= y1) {
      out.push_back(&line);
    }
  }
  return out;
}

const graphics::Rect kArea{15, 30, 390, 70};

TEST(ParagraphEdit, ReflowsInsideTheMarginsOnTheSameBaselines) {
  Document document = open(page_pdf(kParagraph + "BT /F1 10 Tf 20 100 Td (Footer stays) Tj ET"));
  const auto before = page_text(document);
  const double right = before.lines[0].box.x1;
  auto result = replace_paragraph(document, 0, kArea,
                                  "The quick brown fox, much quicker than ever before, jumps over the lazy dog and runs far away into the deep dark "
                                  "woods where nobody at all can find it, and where it stays for a very long time.");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->font, "Helvetica");
  EXPECT_EQ(result->old_lines, 3U);
  EXPECT_EQ(result->lines, 4U);
  EXPECT_EQ(result->alignment, ParagraphAlignment::kLeft);
  EXPECT_DOUBLE_EQ(result->scale, 1);
  EXPECT_NE(result->replaced.find("nobody can find it."), std::string::npos);
  const auto after = page_text(document);
  const auto lines = lines_of(after, 25, 90);
  ASSERT_EQ(lines.size(), 4U);
  for (std::size_t k = 0; k < lines.size(); ++k) {
    SCOPED_TRACE(k);
    EXPECT_NEAR(lines[k]->box.x0, 20, 0.5);
    EXPECT_LE(lines[k]->box.x1, right + 1);
    if (k > 0) {
      EXPECT_NEAR(lines[k]->box.y0 - lines[k - 1]->box.y0, 12, 0.01);
    }
  }
  const std::string text = after.plain();
  EXPECT_NE(text.find("much quicker"), std::string::npos) << text;
  EXPECT_EQ(text.find("nobody can find"), std::string::npos) << text;
  // The rest of the page as it was.
  const auto* footer = word(after, U"Footer");
  ASSERT_NE(footer, nullptr);
  EXPECT_NEAR(footer->box.x0, word(before, U"Footer")->box.x0, 0.01);
}

TEST(ParagraphEdit, AJustifiedParagraphStaysJustified) {
  // Words placed to end every line but the last at x = 135 (as a justifying program writes them).
  const std::string justified =
      "BT /F1 10 Tf 20 360 Td [(alpha) -535 (beta) -535 (gamma) -535 (delta)] TJ ET\n"
      "BT /F1 10 Tf 20 348 Td [(epsilon) -943 (zeta) -943 (eta) -943 (theta)] TJ ET\n"
      "BT /F1 10 Tf 20 336 Td (iota kappa) Tj ET\n";
  Document document = open(page_pdf(justified));
  const auto before = page_text(document);
  ASSERT_GE(before.lines.size(), 3U);
  const double right = std::max(before.lines[0].box.x1, before.lines[1].box.x1);
  ASSERT_NEAR(before.lines[0].box.x1, before.lines[1].box.x1, 1.5);
  auto result = replace_paragraph(document, 0, kArea, "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->alignment, ParagraphAlignment::kJustified);
  const auto after = page_text(document);
  const auto lines = lines_of(after, 25, 120);
  ASSERT_GE(lines.size(), 3U);
  for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
    SCOPED_TRACE(k);
    EXPECT_NEAR(lines[k]->box.x1, right, 1);
    EXPECT_NEAR(lines[k]->box.x0, 20, 0.5);
  }
  EXPECT_LT(lines.back()->box.x1, right - 5);  // the last line is not spread
}

TEST(ParagraphEdit, NoRoomIsRefusedUnlessShrinkingIsAllowed) {
  // Text right below the paragraph: no line can be added.
  Document document = open(page_pdf(kParagraph + "BT /F1 10 Tf 20 320 Td (Next paragraph, close below) Tj ET"));
  const auto start = page_text(document);
  double before_bottom = 0;
  for (const auto& line : start.lines) {
    if (line.box.y1 < 75) {
      before_bottom = std::max(before_bottom, line.box.y1);
    }
  }
  const std::string longer =
      "The quick brown fox, much quicker than ever before and somewhat faster too, jumps over the lazy dog and runs far away into the deep "
      "dark woods where nobody can find it.";
  auto refused = replace_paragraph(document, 0, kArea, longer);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error().code, ErrorCode::kUnsupported);
  EXPECT_NE(refused.error().message.find("no room: the new text needs 4 lines, 3 fit"), std::string::npos) << refused.error().message;
  EXPECT_NE(plain(document).find("nobody can find it."), std::string::npos);  // unchanged

  auto shrunk = replace_paragraph(document, 0, kArea, longer, {.shrink = true});
  ASSERT_TRUE(shrunk.has_value()) << shrunk.error().message;
  EXPECT_LT(shrunk->scale, 1);
  EXPECT_GE(shrunk->scale, kMinShrink);
  EXPECT_NEAR(shrunk->size, 10 * shrunk->scale, 1e-9);
  // Shrunk, it keeps above the next paragraph the space it had.
  const auto after = page_text(document);
  const auto* next = word(after, U"Next");
  ASSERT_NE(next, nullptr);
  const double gap_before = next->box.y0 - before_bottom;
  for (const auto* line : lines_of(after, 25, next->box.y0 - 0.01)) {
    EXPECT_LE(line->box.y1, next->box.y0 - gap_before + 0.01);
  }
  EXPECT_NE(after.plain().find("it.\n\nNext"), std::string::npos) << after.plain();
}

// Shrunk to fit, a paragraph keeps above the text below it the space it had: it ran into that text and
// read as one paragraph with it (found by the app's e2e test, on this page).
TEST(ParagraphEdit, AShrunkParagraphStaysApartFromTheTextBelow) {
  Document document =
      open(page_pdf("BT /F1 12 Tf 14 TL 30 250 Td (Il secondo paragrafo ha) Tj T* (subito sotto un altro) Tj T* (testo che non si sposta.) Tj ET\n"
                    "BT /F1 12 Tf 30 200 Td (Testo che sta sotto.) Tj ET"));
  const auto before = page_text(document);
  ASSERT_EQ(before.blocks.size(), 2U);
  const double gap = before.blocks[1].box.y0 - before.blocks[0].box.y1;
  auto result = replace_paragraph(document, 0, graphics::Rect{25, 140, 200, 185},
                                  "Il secondo paragrafo ha subito sotto un altro testo che non si sposta. Anche questo.", {.shrink = true});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_LT(result->scale, 1);
  const auto after = page_text(document);
  ASSERT_EQ(after.blocks.size(), 2U) << after.plain();
  EXPECT_GE(after.blocks[1].box.y0 - after.blocks[0].box.y1, gap - 0.5);
}

TEST(ParagraphEdit, FallsBackOnASimilarFontAndKeepsLineBreaks) {
  Document document = open(page_pdf(kParagraph));
  // Helvetica's standard encoding has no euro sign.
  auto result = replace_paragraph(document, 0, kArea, "Total: 10 \xE2\x82\xAC\nPaid in full.");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
  EXPECT_EQ(result->font, "Helvetica");
  EXPECT_EQ(result->lines, 2U);
  const std::string text = plain(document);
  EXPECT_NE(text.find("Total: 10 \xE2\x82\xAC\nPaid in full."), std::string::npos) << text;
}

// The style of each word of a new text: kept words keep theirs, a word in place of an old one takes that
// one's, an added word the previous one's (phase 9b).
std::optional<model::ObjectRef> font_of(const text::PageText& t, const std::u32string& w) {
  const auto* found = word(t, w);
  return found != nullptr ? t.chars[found->first].provenance.font : std::nullopt;
}

TEST(ParagraphEdit, WordsKeepTheirStyles) {
  // "brown" in bold (/F2, object 6), "lazy" in red; the rest in Helvetica (/F1, object 5).
  const std::string mixed =
      "BT /F1 10 Tf 12 TL 20 360 Td (The quick ) Tj /F2 10 Tf (brown) Tj /F1 10 Tf ( fox jumps over the) Tj T* (very ) Tj 1 0 0 rg (lazy) Tj 0 g "
      "( dog and runs far away into) Tj T* (the deep dark woods.) Tj ET\n";
  const model::ObjectRef regular{5, 0};
  const model::ObjectRef bold{6, 0};
  {
    // A word kept keeps its style; a word changed elsewhere takes the one it replaces.
    Document document = open(page_pdf(mixed));
    auto result = replace_paragraph(document, 0, kArea, "The slow brown fox jumps over the very lazy dog and runs far away into the deep dark woods.");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->method, TextEditMethod::kSameFont);
    EXPECT_EQ(result->styles, 3U);
    EXPECT_EQ(result->same_font_styles, 3U);
    const auto after = page_text(document);
    EXPECT_EQ(font_of(after, U"brown"), bold);
    EXPECT_EQ(font_of(after, U"slow"), regular);
    EXPECT_EQ(font_of(after, U"fox"), regular);
    auto pages = document.page_refs();
    const auto content = content::page_content(document, (*pages)[0]);
    ASSERT_TRUE(content.has_value());
    EXPECT_NE(
        std::string(reinterpret_cast<const char*>(content->data()), content->size()).find(" 1 0 0 rg"),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        std::string::npos);                                                                              // "lazy", red still
  }
  {
    // The bold word replaced: its replacement is bold; a word added after it, too.
    Document document = open(page_pdf(mixed));
    auto result = replace_paragraph(document, 0, kArea, "The quick black cat jumps over the very lazy dog and runs far away into the deep dark woods.");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto after = page_text(document);
    EXPECT_EQ(font_of(after, U"black"), bold);
    EXPECT_EQ(font_of(after, U"cat"), regular);  // in place of "fox"
    EXPECT_EQ(font_of(after, U"quick"), regular);
  }
  {
    Document document = open(page_pdf(mixed));
    auto result =
        replace_paragraph(document, 0, kArea, "The quick brown and grey fox jumps over the very lazy dog and runs far away into the deep dark woods.");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto after = page_text(document);
    EXPECT_EQ(font_of(after, U"and"), bold);  // added after "brown"
    EXPECT_EQ(font_of(after, U"grey"), bold);
    EXPECT_EQ(font_of(after, U"fox"), regular);
  }
}

// A style whose font lacks a letter goes to the standard font nearest it — bold stays bold — while the
// others keep their own; a word broken at a line's end is one word of the old text.
TEST(ParagraphEdit, EachStyleItsOwnFontOrTheNearest) {
  const std::string mixed =
      "BT /F1 10 Tf 12 TL 20 360 Td (Pay the ) Tj /F2 10 Tf (whole) Tj /F1 10 Tf ( amount as soon as possible, with all the infor-) Tj T* "
      "(mation needed to find it.) Tj ET\n";
  Document document = open(page_pdf(mixed));
  auto result = replace_paragraph(document, 0, kArea,
                                  "Pay the \xE2\x82\xAC"
                                  "10 amount as soon as possible, with all the information needed to find it.");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_NE(result->replaced.find("information needed"), std::string::npos) << result->replaced;
  EXPECT_EQ(result->styles, 2U);
  EXPECT_EQ(result->same_font_styles, 1U);  // the euro sign: the bold style, which Helvetica-Bold's standard encoding lacks
  EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
  const auto after = page_text(document);
  const auto euro = font_of(after, U"\u20AC10");
  ASSERT_TRUE(euro.has_value());
  auto font = document.get(*euro);
  ASSERT_TRUE(font.has_value() && (*font)->as_dict() != nullptr);
  const model::Object* base = (*font)->as_dict()->find("BaseFont");
  ASSERT_NE(base, nullptr);
  EXPECT_EQ(base->as_name()->bytes, "Helvetica-Bold");
  EXPECT_EQ(font_of(after, U"amount"), (model::ObjectRef{5, 0}));
}

// Only the letters a style's font cannot write go to the similar one: the rest of its letters stay in
// its own font (a Greek paragraph lost every Greek letter to a Latin "h" its subset lacked, found by
// corpus-tools/check_paragraph_edit.py --mixed).
TEST(ParagraphEdit, OnlyTheLettersAFontLacksGoToTheSimilarOne) {
  Document document = open(page_pdf(kParagraph));
  auto result = replace_paragraph(document, 0, kArea, "The quick brown fox costs 10\xE2\x82\xAC and runs far away into the deep dark woods.");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
  EXPECT_EQ(result->styles, 1U);
  EXPECT_EQ(result->same_font_styles, 0U);
  const auto after = page_text(document);
  EXPECT_EQ(font_of(after, U"10\u20AC"), (model::ObjectRef{5, 0}));  // "1" and "0": Helvetica's own
  EXPECT_EQ(font_of(after, U"quick"), (model::ObjectRef{5, 0}));
}

// Two lines say nothing of justification: the first ends at the margin because the margin is where the
// longer ends. A two-line paragraph was laid out justified (found by the C API's test).
TEST(ParagraphEdit, TwoLinesAreNotTakenForJustified) {
  Document document = open(page_pdf("BT /F1 10 Tf 12 TL 20 360 Td (A first line that is long enough) Tj T* (short) Tj ET"));
  auto result = replace_paragraph(document, 0, kArea, "A first line that is long enough and a second one");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->alignment, ParagraphAlignment::kLeft);
}

TEST(ParagraphEdit, DeletesAndRefuses) {
  Document document = open(page_pdf(kParagraph + "BT /F1 10 Tf 20 200 Td (E = mc) Tj 4 Ts (2) Tj ET\n"
                                                 "BT 3 Tr /F1 10 Tf 20 100 Td (hidden layer) Tj ET"));
  // A superscript; invisible text; nothing there; two paragraphs.
  auto raised = replace_paragraph(document, 0, graphics::Rect{15, 190, 390, 205}, "x");
  ASSERT_FALSE(raised.has_value());
  EXPECT_NE(raised.error().message.find("superscripts"), std::string::npos) << raised.error().message;
  auto hidden = replace_paragraph(document, 0, graphics::Rect{15, 290, 390, 305}, "x");
  ASSERT_FALSE(hidden.has_value());
  EXPECT_NE(hidden.error().message.find("invisible"), std::string::npos);
  auto nothing = replace_paragraph(document, 0, graphics::Rect{300, 380, 390, 395}, "x");
  ASSERT_FALSE(nothing.has_value());
  EXPECT_EQ(nothing.error().code, ErrorCode::kOutOfRange);
  auto two = replace_paragraph(document, 0, graphics::Rect{15, 30, 390, 205}, "x");
  ASSERT_FALSE(two.has_value());
  EXPECT_NE(two.error().message.find("one paragraph at a time"), std::string::npos) << two.error().message;

  auto deleted = replace_paragraph(document, 0, kArea, "");
  ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
  EXPECT_EQ(deleted->lines, 0U);
  const std::string text = plain(document);
  EXPECT_EQ(text.find("quick"), std::string::npos) << text;
  EXPECT_NE(text.find("mc"), std::string::npos) << text;
}

// A code beyond the font's /LastChar is written with its width in /Widths, /LastChar widened: readers that
// keep to the standard take /MissingWidth (0 here) for it, and drew the letter after it on top of it (found
// by corpus-tools/check_paragraph_move.py: MuPDF read "owrds").
TEST(ParagraphEdit, ACodeBeyondTheLastCharGetsItsWidth) {
  for (const bool indirect : {false, true}) {
    SCOPED_TRACE(indirect ? "/Widths indirect" : "/Widths direct");
    test::PdfBuilder b;
    b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
    b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
    b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
    b.add_stream(4, "BT /F1 10 Tf 12 TL 20 360 Td (aaa aaa aaa aaa aaa aaa aaa aaa aaa aaa) Tj T* (aaa aaa aaa) Tj ET");
    // Widths for "a" to "c" only (97 to 99).
    b.add_object(5, std::string("<</Type/Font/Subtype/TrueType/BaseFont/Arial/Encoding/WinAnsiEncoding/FirstChar 97/LastChar 99/Widths") +
                        (indirect ? " 7 0 R" : "[556 556 500]") + "/FontDescriptor 6 0 R>>");
    b.add_object(6,
                 "<</Type/FontDescriptor/FontName/Arial/Flags 32/FontBBox[0 -200 1000 900]/ItalicAngle 0/Ascent 900/Descent -200/CapHeight 700"
                 "/StemV 80/MissingWidth 0>>");
    b.add_object(7, "[556 556 500]");
    b.finish_table("/Root 1 0 R");
    Document document = open(b.str());
    auto result = replace_paragraph(document, 0, kArea, "aaa www aaa aaa aaa aaa aaa aaa aaa aaa aaa aaa aaa");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->method, TextEditMethod::kSameFont);
    auto font = document.get(model::ObjectRef{5, 0});
    ASSERT_TRUE(font.has_value());
    const model::Dict& dict = *(*font)->as_dict();
    EXPECT_EQ(dict.find("FirstChar")->as_integer(), 97);  // the words' gaps are no spaces
    EXPECT_EQ(dict.find("LastChar")->as_integer(), 119);
    const auto* widths = dict.find("Widths")->as_array();
    ASSERT_NE(widths, nullptr);
    ASSERT_EQ(widths->size(), 23U);
    EXPECT_GT((*widths)[119 - 97].as_number().value_or(0), 500);  // w
    EXPECT_EQ((*widths)[0].as_number().value_or(0), 556);         // a, as it was
    EXPECT_EQ((*widths)[100 - 97].as_number().value_or(-1), 0);   // d, not written: /MissingWidth
  }
}

}  // namespace
}  // namespace capriole::edit
