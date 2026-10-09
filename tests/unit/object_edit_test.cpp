#include "capriole/edit/object_edit.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/layout/page_model.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using layout::Kind;
using layout::Role;

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

// One page 400 × 400, Helvetica as /F1, a form /Fm1 (a 50 × 10 box); a link when asked.
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

layout::PageModel model_of(Document& document) {
  auto model = layout::page_model(document, 0);
  EXPECT_TRUE(model.has_value());
  if (!model) {
    std::abort();
  }
  return std::move(*model);
}

// The element of a kind (and role) whose box starts nearest (x, y), as shown.
std::optional<std::size_t> find(const layout::PageModel& m, Kind kind, double x, double y) {
  std::optional<std::size_t> best;
  double distance = 1e9;
  for (std::size_t i = 0; i < m.elements.size(); ++i) {
    const auto& e = m.elements[i];
    const double d = std::abs(e.box.x0 - x) + std::abs(e.box.y0 - y);
    if (e.kind == kind && d < distance) {
      best = i;
      distance = d;
    }
  }
  return best;
}

std::string content_of(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  auto content = content::page_content(document, (*pages)[0]);
  EXPECT_TRUE(content.has_value());
  return content ? std::string(reinterpret_cast<const char*>(content->data()), content->size())
                 : std::string();  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

const std::string kText = "BT /F1 10 Tf 20 360 Td (Some text that stays) Tj ET\n";

TEST(ObjectEdit, ARuleMovesItsCoordinatesTheRestStays) {
  Document document = open(page_pdf(kText + "0.5 w 20 100 m 380 100 l S\n"));
  const auto before = model_of(document);
  const auto rule = find(before, Kind::kDrawing, 20, 300);
  ASSERT_TRUE(rule.has_value());
  auto result = edit_object(document, 0, {.element = *rule, .action = ObjectAction::kMove, .by = {10, 20}, .box = {}});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->operators, 1U);
  EXPECT_NEAR(result->box.x0, before.elements[*rule].box.x0 + 10, 0.01);
  const auto after = model_of(document);
  const auto moved = find(after, Kind::kDrawing, 30, 320);
  ASSERT_TRUE(moved.has_value());
  EXPECT_NEAR(after.elements[*moved].box.y0, before.elements[*rule].box.y0 + 20, 0.01);
  EXPECT_EQ(after.elements[*moved].role, Role::kRule);
  const std::string content = content_of(document);
  EXPECT_NE(content.find("30 80 m"), std::string::npos) << content;
  EXPECT_NE(content.find("(Some text that stays) Tj"), std::string::npos);
  // The text where it was.
  const auto text = find(after, Kind::kParagraph, 20, 30);
  ASSERT_TRUE(text.has_value());
  EXPECT_NEAR(after.elements[*text].box.x0, before.elements[*find(before, Kind::kParagraph, 20, 30)].box.x0, 0.01);
}

TEST(ObjectEdit, AnImageIsResizedThroughAMatrixAroundIt) {
  Document document = open(page_pdf("q 20 0 0 20 300 300 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q\n"));
  const auto before = model_of(document);
  const auto image = find(before, Kind::kImage, 300, 80);
  ASSERT_TRUE(image.has_value());
  auto result = edit_object(document, 0, {.element = *image, .action = ObjectAction::kResize, .by = {}, .box = {200, 100, 260, 130}});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto after = model_of(document);
  const auto resized = find(after, Kind::kImage, 200, 100);
  ASSERT_TRUE(resized.has_value());
  const auto& box = after.elements[*resized].box;
  EXPECT_NEAR(box.x0, 200, 0.01);
  EXPECT_NEAR(box.y0, 100, 0.01);
  EXPECT_NEAR(box.x1, 260, 0.01);
  EXPECT_NEAR(box.y1, 130, 0.01);
  EXPECT_NE(content_of(document).find(" cm\nBI"), std::string::npos);
}

TEST(ObjectEdit, ADeletedBoxLeavesTheContentTheOthersStay) {
  Document document = open(page_pdf(kText + "1 0 0 rg 20 100 60 40 re f 0 g\n0.5 w 200 100 m 380 100 l S\n"));
  const auto before = model_of(document);
  const auto box = find(before, Kind::kDrawing, 20, 260);
  ASSERT_TRUE(box.has_value());
  ASSERT_EQ(before.elements[*box].role, Role::kBox);
  auto result = edit_object(document, 0, {.element = *box, .action = ObjectAction::kDelete, .by = {}, .box = {}});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto after = model_of(document);
  EXPECT_EQ(after.elements.size(), before.elements.size() - 1);
  const std::string content = content_of(document);
  EXPECT_EQ(content.find(" re"), std::string::npos) << content;
  EXPECT_NE(content.find("200 100 m"), std::string::npos);
}

TEST(ObjectEdit, AFormAllTheObjectsMovesByItsDo) {
  Document document = open(page_pdf("q 1 0 0 1 20 50 cm /Fm1 Do Q\n"));
  const auto before = model_of(document);
  const auto form = find(before, Kind::kDrawing, 20, 340);
  ASSERT_TRUE(form.has_value());
  auto result = edit_object(document, 0, {.element = *form, .action = ObjectAction::kMove, .by = {100, -40}, .box = {}});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const std::string content = content_of(document);
  EXPECT_NE(content.find("q 1 0 0 1 100 40 cm\n/Fm1 Do\nQ"), std::string::npos) << content;
  const auto after = model_of(document);
  const auto moved = find(after, Kind::kDrawing, 120, 300);
  ASSERT_TRUE(moved.has_value());
  EXPECT_NEAR(after.elements[*moved].box.x0, 120, 0.01);
  EXPECT_NEAR(after.elements[*moved].box.y0, 300, 0.01);
}

TEST(ObjectEdit, AClipOfTheObjectAloneGoesWithItAFramesDoesNot) {
  // An image clipped by a path that clips it alone: both move.
  {
    Document document = open(page_pdf("q 290 290 40 40 re W n q 20 0 0 20 300 300 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q Q\n"));
    const auto before = model_of(document);
    const auto image = find(before, Kind::kImage, 300, 80);
    ASSERT_TRUE(image.has_value());
    auto result = edit_object(document, 0, {.element = *image, .action = ObjectAction::kMove, .by = {-200, 0}, .box = {}});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_NE(content_of(document).find("90 290 40 40 re"), std::string::npos) << content_of(document);
  }
  // A frame's clip that holds a line of text too: the image may not leave it.
  {
    Document document = open(page_pdf("q 10 250 380 140 re W n " + kText + "q 20 0 0 20 300 300 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q Q\n"));
    const auto model = model_of(document);
    const auto image = find(model, Kind::kImage, 300, 80);
    ASSERT_TRUE(image.has_value());
    auto out = edit_object(document, 0, {.element = *image, .action = ObjectAction::kMove, .by = {0, 200}, .box = {}});
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("leave what clips it"), std::string::npos) << out.error().message;
    auto in = edit_object(document, 0, {.element = *image, .action = ObjectAction::kMove, .by = {-100, 10}, .box = {}});
    EXPECT_TRUE(in.has_value()) << in.error().message;
  }
}

TEST(ObjectEdit, APathThatClipsWhatFollowsKeepsClippingWhenDeleted) {
  Document document = open(page_pdf("q 20 100 200 100 re W f " + kText + "Q\n"));
  const auto before = model_of(document);
  const auto box = find(before, Kind::kDrawing, 20, 200);
  ASSERT_TRUE(box.has_value());
  auto moved = edit_object(document, 0, {.element = *box, .action = ObjectAction::kMove, .by = {5, 5}, .box = {}});
  ASSERT_FALSE(moved.has_value());
  EXPECT_NE(moved.error().message.find("also clips"), std::string::npos) << moved.error().message;
  auto deleted = edit_object(document, 0, {.element = *box, .action = ObjectAction::kDelete, .by = {}, .box = {}});
  ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
  const std::string content = content_of(document);
  EXPECT_NE(content.find("20 100 200 100 re\nW\nn"), std::string::npos) << content;
}

TEST(ObjectEdit, TextAnnotationsAndLeavingThePageAreRefused) {
  Document document = open(page_pdf(kText + "0.5 w 20 100 m 380 100 l S\n", true));
  const auto model = model_of(document);
  const auto text = find(model, Kind::kParagraph, 20, 30);
  const auto link = find(model, Kind::kAnnotation, 20, 370);
  const auto rule = find(model, Kind::kDrawing, 20, 300);
  ASSERT_TRUE(text && link && rule);
  for (const std::size_t e : {*text, *link}) {
    auto out = edit_object(document, 0, {.element = e, .action = ObjectAction::kMove, .by = {1, 1}, .box = {}});
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().code, ErrorCode::kUnsupported);
  }
  auto away = edit_object(document, 0, {.element = *rule, .action = ObjectAction::kMove, .by = {0, 500}, .box = {}});
  ASSERT_FALSE(away.has_value());
  EXPECT_NE(away.error().message.find("leave the page"), std::string::npos);
  auto empty = edit_object(document, 0, {.element = *rule, .action = ObjectAction::kResize, .by = {}, .box = {50, 50, 50, 60}});
  ASSERT_FALSE(empty.has_value());
  auto none = edit_object(document, 0, {.element = 99, .action = ObjectAction::kDelete, .by = {}, .box = {}});
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error().code, ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace capriole::edit
