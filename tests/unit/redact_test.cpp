#include "capriole/edit/redact.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/forms.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"
#include "capriole/writer/save.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using graphics::Rect;
using model::Dict;
using model::Object;
using model::ObjectRef;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::string text_of(const std::vector<std::byte>& data) {
  std::string out;
  for (const std::byte b : data) {
    out.push_back(static_cast<char>(b));
  }
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

std::string save_full(Document& document) {
  writer::MemorySink sink;
  const auto report = writer::save_full(document, sink);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  return text_of(sink.data());
}

const std::string kFont = "/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>>>";

// One page 300 × 200 with content; extra: more resources (before the closing >>), objects: more objects.
std::string page_pdf(const std::string& content, const std::string& extra_resources = "",
                     const std::vector<std::pair<std::uint32_t, std::string>>& objects = {}, const std::string& page_extra = "", std::uint32_t pages = 1) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (std::uint32_t p = 0; p < pages; ++p) {
    kids += std::to_string(3 + (p * 10)) + " 0 R ";
  }
  b.add_object(2, "<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(pages) + ">>");
  for (std::uint32_t p = 0; p < pages; ++p) {
    b.add_object(3 + (p * 10), "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents " + std::to_string(4 + (p * 10)) + " 0 R" + kFont + extra_resources +
                                   ">>" + page_extra + ">>");
    b.add_stream(4 + (p * 10), content);
  }
  for (const auto& [number, body] : objects) {
    if (body.starts_with("stream:")) {
      const std::size_t bar = body.find('|');
      b.add_stream(number, body.substr(bar + 1), body.substr(7, bar - 7));
    } else {
      b.add_object(number, body);
    }
  }
  b.finish_table("/Size 60/Root 1 0 R");
  return b.str();
}

std::vector<text::Word> words(Document& document, std::uint32_t page = 0) {
  auto list = content::interpret_page(document, (*document.page_refs())[page], {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value());
  return list ? text::extract(*list).words : std::vector<text::Word>{};
}

std::string plain(Document& document, std::uint32_t page = 0) {
  std::string out;
  for (const text::Word& w : words(document, page)) {
    out += model::to_utf8(w.text) + " ";
  }
  return out;
}

const text::Word& word(const std::vector<text::Word>& list, const std::u32string& text) {
  const auto found = std::ranges::find_if(list, [&](const text::Word& w) { return w.text == text; });
  EXPECT_NE(found, list.end());
  if (found == list.end()) {
    std::abort();
  }
  return *found;
}

TEST(Redaction, TextLeavesItsStringsAndTheRestStaysInPlace) {
  Document document =
      open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello secret world) Tj ET\n"
                    "BT /F1 12 Tf 20 120 Td [(Kern) -250 (ed secret) -300 (after)] TJ ET\n"
                    "BT /F1 12 Tf 14 TL 20 100 Td (line) Tj (one secret two) ' 2 1 (aw secret ok) \" ET\n"
                    "BT 3 Tr /F1 12 Tf 20 40 Td (invisible secret layer) Tj ET"));
  const auto before = words(document);
  std::vector<Rect> areas;
  for (const text::Word& w : before) {
    if (w.text == U"secret") {
      areas.push_back(Rect{w.box.x0 - 1, w.box.y0, w.box.x1 + 1, w.box.y1});
    }
  }
  ASSERT_EQ(areas.size(), 5U);
  const auto report = redact(document, 0, areas);
  ASSERT_TRUE(report.has_value()) << report.error().message;
  EXPECT_GE(report->glyphs, 30U);  // the five words (and the spaces at their edges)
  const auto after = words(document);
  const std::string text = plain(document);
  EXPECT_EQ(text.find("secret"), std::string::npos) << text;
  for (const std::u32string w : {U"Hello", U"world", U"after", U"two", U"ok", U"layer", U"line", U"one", U"aw"}) {
    SCOPED_TRACE(model::to_utf8(w));
    const Rect a = word(before, w).box;
    const Rect b = word(after, w).box;
    EXPECT_NEAR(a.x0, b.x0, 0.01);
    EXPECT_NEAR(a.y0, b.y0, 0.01);
  }
  // The page's content no longer has the word, in any form; a full save does not keep the old stream.
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_EQ(content.find("secret"), std::string::npos) << content;
  const std::string saved = save_full(document);
  EXPECT_EQ(saved.find("secret"), std::string::npos);
  // An incremental save would keep the original bytes: refused.
  writer::MemorySink sink;
  EXPECT_EQ(writer::save_incremental(document, sink).error().code, ErrorCode::kUnsupported);
}

// A glyph of no area (no width) anywhere on the page was taken as in every area: a fifth of nothing is
// nothing, and nothing is ≥ it (found by corpus-tools/check_text_edit.py, 0000758.pdf's zero-width spaces).
TEST(Redaction, AGlyphOfNoWidthElsewhereStays) {
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello secret) Tj ET\nBT /F1 12 Tf 0 Tz 20 60 Td (zero) Tj ET"));
  const auto before = words(document);
  const Rect secret = word(before, U"secret").box;
  const auto report = redact(document, 0, std::vector<Rect>{Rect{secret.x0 - 1, secret.y0, secret.x1 + 1, secret.y1}});
  ASSERT_TRUE(report.has_value()) << report.error().message;
  EXPECT_LE(report->glyphs, 8U);  // the word and the spaces at its edges, not the four glyphs of no width
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_NE(content.find("(zero) Tj"), std::string::npos) << content;
}

TEST(Redaction, FormsAreCopiedOnlyWhereTheyChange) {
  // One form drawn on two pages, and twice on the first: only the first drawing on page 1 is redacted.
  const std::string resources = "/XObject<</Fm 50 0 R>>";
  Document document =
      open(page_pdf("q 1 0 0 1 0 0 cm /Fm Do Q q 1 0 0 1 0 -100 cm /Fm Do Q", resources,
                    {{50, "stream:/Type/XObject/Subtype/Form/BBox[0 0 300 200]" + kFont + ">>|BT /F1 12 Tf 20 150 Td (form secret) Tj ET"}}, "", 2));
  const auto before = words(document);
  const text::Word top = word(before, U"secret");  // the first drawing: y as shown about 40
  ASSERT_LT(top.box.y0, 100);
  ASSERT_TRUE(redact(document, 0, std::vector<Rect>{Rect{top.box.x0 - 1, top.box.y0, top.box.x1 + 1, top.box.y1}}).has_value());
  const auto after = words(document);
  EXPECT_EQ(std::ranges::count_if(after, [](const text::Word& w) { return w.text == U"secret"; }), 1);  // the second drawing keeps it
  EXPECT_GT(word(after, U"secret").box.y0, 100);
  EXPECT_NE(plain(document, 1).find("secret"), std::string::npos);  // the other page keeps its form
  // The original form is untouched.
  auto form = document.get(50);
  EXPECT_NE(text_of(*document.decode_stream(*(*form)->as_stream())).find("secret"), std::string::npos);
  // Page 1 still draws the form (its second drawing), so it keeps it in its resources; page 2 too.
}

TEST(Redaction, WhatIsNoLongerDrawnLeavesTheResources) {
  // A form drawn once: replaced by its redacted copy, it leaves the page's resources, and the file.
  Document document = open(page_pdf("/Fm Do", "/XObject<</Fm 50 0 R/Other 51 0 R>>",
                                    {{50, "stream:/Type/XObject/Subtype/Form/BBox[0 0 300 200]" + kFont + ">>|BT /F1 12 Tf 20 150 Td (form secret) Tj ET"},
                                     {51, "stream:/Type/XObject/Subtype/Form/BBox[0 0 10 10]|0 0 10 10 re f"}}));
  const text::Word secret = word(words(document), U"secret");
  ASSERT_TRUE(redact(document, 0, std::vector<Rect>{Rect{secret.box.x0 - 1, secret.box.y0, secret.box.x1 + 1, secret.box.y1}}).has_value());
  auto page = document.get((*document.page_refs())[0]);
  auto resources = document.resolve(*(*page)->as_dict()->find("Resources"));
  auto xobjects = document.resolve(*resources->as_dict()->find("XObject"));
  EXPECT_FALSE(xobjects->as_dict()->contains("Fm"));
  EXPECT_TRUE(xobjects->as_dict()->contains("Other"));  // not drawn, but not the redaction's to remove
  EXPECT_EQ(save_full(document).find("secret"), std::string::npos);
}

TEST(Redaction, ImagesArePaintedOverOrRemoved) {
  // A 4×4 grey image over [20 20 120 120] in page space (as shown: x 20–120, y 80–180), and another
  // over [200 20 240 60].
  const std::string pixels(48, '\x80');
  Document document = open(page_pdf("q 100 0 0 100 20 20 cm /Im1 Do Q q 40 0 0 40 200 20 cm /Im2 Do Q", "/XObject<</Im1 50 0 R/Im2 51 0 R>>",
                                    {{50, "stream:/Type/XObject/Subtype/Image/Width 4/Height 4/ColorSpace/DeviceRGB/BitsPerComponent 8|" + pixels},
                                     {51, "stream:/Type/XObject/Subtype/Image/Width 4/Height 4/ColorSpace/DeviceRGB/BitsPerComponent 8|" + pixels}}));
  // The left half of the first image, and all of the second.
  const auto report = redact(document, 0, std::vector<Rect>{Rect{10, 70, 70, 190}, Rect{190, 130, 250, 190}});
  ASSERT_TRUE(report.has_value()) << report.error().message;
  EXPECT_EQ(report->images_cleared, 1U);
  EXPECT_EQ(report->images_removed, 1U);
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_EQ(content.find("/Im2"), std::string::npos) << content;
  EXPECT_EQ(content.find("/Im1"), std::string::npos) << content;  // drawn anew under another name
  // The new image: the left two columns black (the fill), the right two as they were.
  auto page = document.get((*document.page_refs())[0]);
  auto resources = document.resolve(*(*page)->as_dict()->find("Resources"));
  auto xobjects = document.resolve(*resources->as_dict()->find("XObject"));
  auto image = document.resolve(*xobjects->as_dict()->find("CprRedacted1"));
  ASSERT_TRUE(image && image->as_stream() != nullptr);
  const auto data = *document.decode_stream(*image->as_stream());
  ASSERT_EQ(data.size(), 48U);
  EXPECT_EQ(data[0], std::byte{0});
  EXPECT_EQ(data[3], std::byte{0});
  EXPECT_EQ(data[6], std::byte{0x80});
  EXPECT_EQ(data[9], std::byte{0x80});
}

TEST(Redaction, SoftMasksAreWrittenAnewInTheirOwnSpace) {
  // A CMYK image with a soft mask, its left half redacted: both written anew in their own spaces — the
  // image's area black in CMYK, the mask's opaque —, the right halves as they were.
  const std::string cmyk(4 * 4 * 4, '\x40');
  const std::string alpha(16, '\x80');
  Document document = open(page_pdf("q 100 0 0 100 20 20 cm /Im1 Do Q", "/XObject<</Im1 50 0 R>>",
                                    {{50, "stream:/Type/XObject/Subtype/Image/Width 4/Height 4/ColorSpace/DeviceCMYK/BitsPerComponent 8/SMask 51 0 R|" + cmyk},
                                     {51, "stream:/Type/XObject/Subtype/Image/Width 4/Height 4/ColorSpace/DeviceGray/BitsPerComponent 8|" + alpha}}));
  ASSERT_TRUE(redact(document, 0, std::vector<Rect>{Rect{10, 70, 70, 190}}).has_value());
  auto page = document.get((*document.page_refs())[0]);
  auto resources = document.resolve(*(*page)->as_dict()->find("Resources"));
  auto xobjects = document.resolve(*resources->as_dict()->find("XObject"));
  auto image = document.resolve(*xobjects->as_dict()->find("CprRedacted1"));
  ASSERT_TRUE(image && image->as_stream() != nullptr);
  EXPECT_EQ(image->as_stream()->dict.find("ColorSpace")->as_name()->bytes, "DeviceCMYK");
  const auto data = *document.decode_stream(*image->as_stream());
  ASSERT_EQ(data.size(), 64U);
  EXPECT_EQ(data[3], std::byte{255});   // K of the first pixel: black
  EXPECT_EQ(data[8], std::byte{0x40});  // the third pixel, as it was
  auto mask = document.resolve(*image->as_stream()->dict.find("SMask"));
  ASSERT_TRUE(mask && mask->as_stream() != nullptr);
  const auto opacity = *document.decode_stream(*mask->as_stream());
  EXPECT_EQ(opacity[0], std::byte{255});
  EXPECT_EQ(opacity[2], std::byte{0x80});
}

TEST(Redaction, SmallPathsGoLargeOnesStay) {
  Document document = open(page_pdf("0.9 g 0 0 300 200 re f 0 g 30 150 20 10 re f 1 0 0 RG 25 140 m 60 140 l S"));
  const auto report = redact(document, 0, std::vector<Rect>{Rect{20, 30, 80, 70}});
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ(report->paths, 2U);  // the small rectangle and the line; not the background
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_NE(content.find("0 0 300 200 re\nf"), std::string::npos) << content;
  EXPECT_NE(content.find("W* n\n0 0 300 200 re\nf\nQ"), std::string::npos) << content;  // painted outside the area only
}

TEST(Redaction, AnnotationsAndActualTextGo) {
  Document document = open(page_pdf(
      "/Span <</ActualText (secret)>> BDC BT /F1 12 Tf 20 150 Td (s) Tj ET EMC BT /F1 12 Tf 200 150 Td (kept) Tj ET", "",
      {{30, "<</Type/Annot/Subtype/Text/Rect[15 140 35 160]/Contents(a note)>>"}, {31, "<</Type/Annot/Subtype/Text/Rect[250 10 270 30]/Contents(far)>>"}},
      "/Annots[30 0 R 31 0 R]"));
  ASSERT_TRUE(redact(document, 0, std::vector<Rect>{Rect{10, 30, 60, 70}}).has_value());
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_EQ(content.find("secret"), std::string::npos) << content;
  EXPECT_NE(content.find("/Span"), std::string::npos);  // the sequence stays, without its text
  auto page = document.get((*document.page_refs())[0]);
  auto annots = document.resolve(*(*page)->as_dict()->find("Annots"));
  ASSERT_EQ(annots->as_array()->size(), 1U);
  EXPECT_EQ((*annots->as_array())[0].as_ref()->number, 31U);
}

TEST(Redaction, StructureElementsLoseTheirText) {
  // A tagged page: the Span of MCID 0 says "secret" in /ActualText, its paragraph in /Alt.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/StructTreeRoot 40 0 R/MarkInfo<</Marked true>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/StructParents 0" + kFont + ">>>>");
  b.add_stream(4, "/Span <</MCID 0>> BDC BT /F1 12 Tf 20 150 Td (secret) Tj ET EMC BT /F1 12 Tf 200 150 Td (kept) Tj ET");
  b.add_object(40, "<</Type/StructTreeRoot/K 42 0 R/ParentTree<</Nums[0 [41 0 R]]>>>>");
  b.add_object(41, "<</S/Span/P 42 0 R/Pg 3 0 R/K 0/ActualText(secret)>>");
  b.add_object(42, "<</S/P/P 40 0 R/K[41 0 R]/Alt(a secret paragraph)>>");
  b.finish_table("/Size 43/Root 1 0 R");
  Document document = open(b.str());
  ASSERT_TRUE(redact(document, 0, std::vector<Rect>{Rect{10, 30, 70, 70}}).has_value());
  const auto dict = [&](std::uint32_t n) { return *(*document.get(n))->as_dict(); };
  EXPECT_FALSE(dict(41).contains("ActualText"));
  EXPECT_FALSE(dict(42).contains("Alt"));
  EXPECT_EQ(save_full(document).find("secret"), std::string::npos);
}

TEST(Redaction, RedactAnnotationsAreAppliedAndGo) {
  Document document =
      open(page_pdf("BT /F1 12 Tf 20 150 Td (Hello secret world) Tj ET", "",
                    {{30, "<</Type/Annot/Subtype/Redact/Rect[50 145 95 165]/QuadPoints[52 162 92 162 52 147 92 147]/IC[1 0 0]>>"}}, "/Annots[30 0 R]"));
  const auto report = apply_redactions(document, 0);
  ASSERT_TRUE(report.has_value()) << report.error().message;
  EXPECT_EQ(plain(document).find("secret"), std::string::npos);
  EXPECT_NE(plain(document).find("Hello"), std::string::npos);
  const std::string content = text_of(*content::page_content(document, (*document.page_refs())[0]));
  EXPECT_NE(content.find("1 0 0 rg"), std::string::npos);  // filled with its /IC
  auto page = document.get((*document.page_refs())[0]);
  EXPECT_EQ((*page)->as_dict()->find("Annots"), nullptr);  // the mark went, and with it the page's /Annots
  EXPECT_EQ(apply_redactions(document, 0).error().code, ErrorCode::kOutOfRange);
}

TEST(Redaction, RefusalsChangeNothing) {
  Document document = open(page_pdf("BT /F1 12 Tf 20 150 Td (x) Tj ET"));
  EXPECT_EQ(redact(document, 0, std::vector<Rect>{}).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(redact(document, 0, std::vector<Rect>{Rect{10, 10, 10, 50}}).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(redact(document, 3, std::vector<Rect>{Rect{10, 10, 50, 50}}).error().code, ErrorCode::kOutOfRange);
  EXPECT_FALSE(document.has_changes());
  EXPECT_FALSE(document.redacted());
}

}  // namespace
}  // namespace capriole::edit
