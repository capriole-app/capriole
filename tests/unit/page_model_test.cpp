#include "capriole/layout/page_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::layout {
namespace {

Document open(const std::string& pdf) {
  std::vector<std::byte> data(pdf.size());
  std::memcpy(data.data(), pdf.data(), pdf.size());
  auto document = Document::open(std::make_unique<MemoryByteSource>(std::move(data)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

// One page 400 × 400, Helvetica as /F1, a form /Fm1 (a 50 × 10 box); a link (as page space, from y 20 to
// 30) when asked.
std::string page_pdf(const std::string& content, bool link = false) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, std::string("<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>/XObject<</Fm1 6 0 R>>>>") +
                      (link ? "/Annots[7 0 R]" : "") + ">>");
  b.add_stream(4, content);
  b.add_object(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  b.add_stream(6, "0 0 50 10 re f", "/Type/XObject/Subtype/Form/BBox[0 0 50 10]");
  b.add_object(7, "<</Type/Annot/Subtype/Link/Rect[20 20 120 30]/Border[0 0 0]>>");
  b.finish_table("/Root 1 0 R");
  return b.str();
}

PageModel model_of(const std::string& content, bool link = false) {
  Document document = open(page_pdf(content, link));
  auto model = page_model(document, 0);
  EXPECT_TRUE(model.has_value()) << (model ? "" : model.error().message);
  if (!model) {
    std::abort();
  }
  return std::move(*model);
}

// Every command the content draws is in one element (a text run: in paragraphs only), every element's
// commands are drawn, and no character is in two paragraphs.
void expect_covered(const PageModel& m) {
  const std::set<std::size_t> drawn(m.drawn.begin(), m.drawn.end());
  std::map<std::size_t, std::vector<Kind>> owners;
  for (const Element& e : m.elements) {
    for (const std::size_t c : e.commands) {
      EXPECT_TRUE(drawn.contains(c)) << "command " << c;
      owners[c].push_back(e.kind);
    }
  }
  for (const std::size_t c : drawn) {
    const auto& kinds = owners[c];
    ASSERT_FALSE(kinds.empty()) << "command " << c << " is in no element";
    if (kinds.size() > 1) {
      EXPECT_TRUE(std::ranges::all_of(kinds, [](Kind k) { return k == Kind::kParagraph; })) << "command " << c;
    }
  }
  std::set<std::size_t> chars;
  for (const Paragraph& p : m.paragraphs) {
    for (const std::size_t c : p.chars) {
      EXPECT_TRUE(chars.insert(c).second) << "character " << c << " in two paragraphs";
    }
  }
}

std::vector<const Element*> of_kind(const PageModel& m, Kind kind) {
  std::vector<const Element*> out;
  for (const Element& e : m.elements) {
    if (e.kind == kind) {
      out.push_back(&e);
    }
  }
  return out;
}

std::u32string text_of(const Paragraph& p) {
  std::u32string out;
  for (const Word& w : p.words) {
    out += out.empty() ? U"" : U" ";
    out += w.text;
  }
  return out;
}

// Three lines of 10 pt Helvetica, 12 points apart, from (x, y) as page space.
std::string paragraph(int x, int y, const std::string& a, const std::string& b, const std::string& c) {
  return "BT /F1 10 Tf 12 TL " + std::to_string(x) + " " + std::to_string(y) + " Td (" + a + ") Tj T* (" + b + ") Tj T* (" + c + ") Tj ET\n";
}

TEST(PageModel, ParagraphsAreReadInColumns) {
  const PageModel m = model_of(paragraph(20, 360, "The first paragraph of the", "left column runs on for", "three lines of text.") +
                               paragraph(20, 300, "The second paragraph of the", "left column also runs on", "for three lines.") +
                               paragraph(220, 360, "The first paragraph of the", "right column runs on for", "three lines of text.") +
                               paragraph(220, 300, "The second paragraph of the", "right column also runs on", "for three lines."));
  expect_covered(m);
  ASSERT_EQ(m.paragraphs.size(), 4U);
  ASSERT_EQ(m.columns.size(), 2U);
  for (const Column& column : m.columns) {
    ASSERT_EQ(column.elements.size(), 2U);
    const Element& top = m.elements[column.elements[0]];
    const Element& bottom = m.elements[column.elements[1]];
    EXPECT_LT(top.box.y1, bottom.box.y0);
    EXPECT_NEAR(top.box.x0, bottom.box.x0, 0.01);
  }
  for (const Element* e : of_kind(m, Kind::kParagraph)) {
    const Paragraph& p = m.paragraphs[e->paragraph];
    EXPECT_EQ(p.lines.size(), 3U);
    EXPECT_NEAR(p.pitch, 12, 0.01);
    EXPECT_EQ(p.alignment, Alignment::kLeft);
    EXPECT_TRUE(p.refusal.empty()) << p.refusal;
    EXPECT_EQ(e->zone, Zone::kBody);
    EXPECT_EQ(e->sources.size(), 3U);  // its three Tj
  }
  EXPECT_EQ(text_of(m.paragraphs[0]), U"The first paragraph of the left column runs on for three lines of text.");
}

TEST(PageModel, DrawingsAreGroupedByWhatTheyTouchAndNamed) {
  const PageModel m = model_of(
      "1 1 0.9 rg 0 0 400 400 re f 0 g\n"                                     // a background
      "0.5 w 20 100 m 380 100 l S\n"                                          // a rule
      "1 w 20 200 m 200 200 l S 20 230 m 200 230 l S 20 260 m 200 260 l S\n"  // a table's lines
      "20 200 m 20 260 l S 110 200 m 110 260 l S 200 200 m 200 260 l S\n"
      "250 200 100 60 re S 0.5 w 260 230 m 340 230 l S\n"                   // a box, a rule clear inside it
      "q 20 0 0 20 300 300 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q\n");  // an image
  expect_covered(m);
  std::map<Role, std::size_t> roles;
  for (const Element* e : of_kind(m, Kind::kDrawing)) {
    ++roles[e->role];
    if (e->role == Role::kTable) {
      EXPECT_EQ(e->commands.size(), 6U);
      EXPECT_NEAR(e->box.x0, 20, 1);
      EXPECT_NEAR(e->box.y1, 200, 1);  // as shown: 400 - 200
    }
  }
  EXPECT_EQ(roles[Role::kBackground], 1U);
  EXPECT_EQ(roles[Role::kRule], 2U);
  EXPECT_EQ(roles[Role::kTable], 1U);
  EXPECT_EQ(roles[Role::kBox], 1U);
  EXPECT_EQ(roles.size(), 4U);
  const auto images = of_kind(m, Kind::kImage);
  ASSERT_EQ(images.size(), 1U);
  EXPECT_EQ(images[0]->role, Role::kFigure);
  EXPECT_NEAR(images[0]->box.x0, 300, 0.01);
  EXPECT_NEAR(images[0]->box.y0, 80, 0.01);
}

TEST(PageModel, AStrokesBoxHoldsItsWidth) {
  const PageModel m = model_of("10 w 100 100 m 200 100 l S\n");
  const auto drawings = of_kind(m, Kind::kDrawing);
  ASSERT_EQ(drawings.size(), 1U);
  EXPECT_LE(drawings[0]->box.y0, 295);
  EXPECT_GE(drawings[0]->box.y1, 305);
  EXPECT_EQ(drawings[0]->role, Role::kRule);  // a thick one: its path is a line
}

TEST(PageModel, AHeaderAndAFooterAreApartFromTheBody) {
  const PageModel m = model_of("BT /F1 9 Tf 20 385 Td (A running header) Tj ET\n" +
                               paragraph(20, 300, "The body of the page runs", "on for a few lines, well", "apart from the header.") +
                               "BT /F1 9 Tf 20 12 Td (Page 1 of 1) Tj ET\n");
  expect_covered(m);
  std::map<Zone, std::vector<std::u32string>> zones;
  for (const Element* e : of_kind(m, Kind::kParagraph)) {
    zones[e->zone].push_back(text_of(m.paragraphs[e->paragraph]));
  }
  EXPECT_EQ(zones[Zone::kHeader], std::vector<std::u32string>{U"A running header"});
  EXPECT_EQ(zones[Zone::kFooter], std::vector<std::u32string>{U"Page 1 of 1"});
  EXPECT_EQ(zones[Zone::kBody].size(), 1U);
  // A header and a footer are in no column.
  for (const Element* e : of_kind(m, Kind::kParagraph)) {
    EXPECT_EQ(e->column.has_value(), e->zone == Zone::kBody);
  }
}

TEST(PageModel, ASpaceShownAloneIsItsLinesAnEmptyLinesIsApart) {
  const PageModel m = model_of(
      "BT /F1 10 Tf 20 300 Td (Words here) Tj ( ) Tj (and more) Tj ET\n"
      "BT /F1 10 Tf 20 200 Td ( ) Tj ET\n");
  expect_covered(m);
  const auto paragraphs = of_kind(m, Kind::kParagraph);
  ASSERT_EQ(paragraphs.size(), 1U);
  EXPECT_EQ(paragraphs[0]->commands.size(), 3U);  // the space with its line
  const auto glyphs = of_kind(m, Kind::kGlyphs);
  ASSERT_EQ(glyphs.size(), 1U);
  EXPECT_NEAR(glyphs[0]->box.x0, 20, 0.01);
  EXPECT_GT(glyphs[0]->box.y0, 190);
}

TEST(PageModel, AnAnnotationAndAFormAreElementsWithTheirSources) {
  const PageModel m = model_of("q 1 0 0 1 20 50 cm /Fm1 Do Q\n", true);
  expect_covered(m);
  const auto annotations = of_kind(m, Kind::kAnnotation);
  ASSERT_EQ(annotations.size(), 1U);
  EXPECT_EQ(annotations[0]->annotation, (model::ObjectRef{7, 0}));
  EXPECT_EQ(annotations[0]->subtype, "Link");
  EXPECT_NEAR(annotations[0]->box.y0, 370, 0.01);
  const auto drawings = of_kind(m, Kind::kDrawing);
  ASSERT_EQ(drawings.size(), 1U);
  ASSERT_EQ(drawings[0]->sources.size(), 1U);
  const Source source = drawings[0]->sources[0];
  ASSERT_LT(source.invocation, m.list.invocations.size());
  EXPECT_EQ(m.list.invocations[source.invocation].kind, graphics::Invocation::Kind::kForm);
  EXPECT_EQ(m.list.invocations[source.invocation].form, (model::ObjectRef{6, 0}));
  EXPECT_EQ(source.operation, 1U);  // f, after re
  EXPECT_NEAR(drawings[0]->box.x0, 20, 0.01);
  EXPECT_NEAR(drawings[0]->box.y1, 350, 0.01);
}

TEST(PageModel, AParagraphThatCannotBeLaidOutAnewSaysWhy) {
  const PageModel m = model_of("BT /F1 10 Tf 12 TL 20 360 Td (A line of text with) Tj T* (an x) Tj 5 Ts (2) Tj 0 Ts ( in it) Tj ET\n");
  expect_covered(m);
  ASSERT_EQ(m.paragraphs.size(), 1U);
  EXPECT_NE(m.paragraphs[0].refusal.find("superscripts"), std::string::npos) << m.paragraphs[0].refusal;
  EXPECT_FALSE(m.paragraphs[0].chars.empty());
}

TEST(PageModel, AnEmptyPageHasNothing) {
  const PageModel m = model_of("");
  EXPECT_TRUE(m.elements.empty());
  EXPECT_TRUE(m.paragraphs.empty());
  EXPECT_TRUE(m.drawn.empty());
  EXPECT_DOUBLE_EQ(m.width, 400);
}

}  // namespace
}  // namespace capriole::layout
