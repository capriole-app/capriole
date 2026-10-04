#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include "capriole/capriole.h"
#include "support/pdf_builder.hpp"

namespace {

TEST(CApi, VersionMatchesAbi) {
  EXPECT_FALSE(std::string_view(capriole_version()).empty());
  EXPECT_EQ(capriole_abi_version(), CAPRIOLE_ABI_VERSION);
}

TEST(CApi, ProbeHeader) {
  constexpr std::string_view kPdf = "%PDF-1.6\n";
  int major = 0;
  int minor = 0;
  const auto* data = reinterpret_cast<const std::uint8_t*>(kPdf.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  EXPECT_EQ(capriole_probe_header(data, kPdf.size(), &major, &minor), CAPRIOLE_OK);
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 6);
  EXPECT_STREQ(capriole_last_error_message(), "");
}

TEST(CApi, ProbeHeaderReportsSyntaxError) {
  constexpr std::string_view kNotPdf = "hello";
  int major = 0;
  int minor = 0;
  const auto* data = reinterpret_cast<const std::uint8_t*>(kNotPdf.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  EXPECT_EQ(capriole_probe_header(data, kNotPdf.size(), &major, &minor), CAPRIOLE_ERROR_SYNTAX);
  EXPECT_STRNE(capriole_last_error_message(), "");
}

TEST(CApi, ProbeHeaderRejectsNullArguments) {
  int major = 0;
  int minor = 0;
  EXPECT_EQ(capriole_probe_header(nullptr, 4, &major, &minor), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_probe_header(nullptr, 0, nullptr, &minor), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  // An empty buffer is valid input, just not a PDF.
  EXPECT_EQ(capriole_probe_header(nullptr, 0, &major, &minor), CAPRIOLE_ERROR_SYNTAX);
}

// ── Documents ───────────────────────────────────────────────────────────────────────────────

const std::uint8_t* bytes_of(const std::string& text) {
  return reinterpret_cast<const std::uint8_t*>(text.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

std::string sample_pdf() {
  capriole::test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792.5]/Contents 4 0 R>>");
  b.add_stream(4, capriole::test::zlib_compress("BT (Hi) Tj ET"), "/Filter/FlateDecode");
  b.add_object(5, "<</Title(Caf\\351 \\\"q\\\")>>");
  b.finish_table("/Size 6/Root 1 0 R/Info 5 0 R");
  return b.str();
}

// Opens text and takes the handle; the test fails if it does not open.
capriole_document* open_document(const std::string& pdf, const char* password = nullptr, int repair = 1) {
  capriole_document* document = nullptr;
  EXPECT_EQ(capriole_document_open(bytes_of(pdf), pdf.size(), password, repair, &document), CAPRIOLE_OK) << capriole_last_error_message();
  return document;
}

std::string take_json(char* json) {
  std::string text = json != nullptr ? json : "";
  capriole_free(json);
  return text;
}

TEST(CApiDocument, SummaryDescribesTheDocument) {
  capriole_document* document = open_document(sample_pdf());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_summary_json(document, &json), CAPRIOLE_OK);
  const std::string summary = take_json(json);
  EXPECT_NE(summary.find(R"("version":"1.7")"), std::string::npos) << summary;
  EXPECT_NE(summary.find(R"("encryption":null)"), std::string::npos);
  EXPECT_NE(summary.find(R"("form":"table")"), std::string::npos);
  EXPECT_NE(summary.find(R"([3,"n",0])"), std::string::npos);
  EXPECT_NE(summary.find(R"("pages":[[3,0]])"), std::string::npos);
  EXPECT_NE(summary.find(R"("repairs":[])"), std::string::npos);
  EXPECT_NE(summary.find(R"(["Root",{"t":"ref","n":1,"g":0}])"), std::string::npos);
  capriole_document_close(document);
}

TEST(CApiDocument, ObjectsAsJson) {
  capriole_document* document = open_document(sample_pdf());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_object_json(document, 3, &json), CAPRIOLE_OK);
  const std::string page = take_json(json);
  EXPECT_NE(page.find(R"("number":3,"generation":0,"state":"unmodified")"), std::string::npos) << page;
  EXPECT_NE(page.find(R"(["Type",{"t":"name","v":"Page"}])"), std::string::npos);
  EXPECT_NE(page.find(R"({"t":"real","v":"792.5"})"), std::string::npos);
  // Bytes outside printable ASCII as JSON escapes of the same code point; quotes escaped.
  ASSERT_EQ(capriole_document_object_json(document, 5, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"({"t":"string","form":"literal","v":"Caf\u00e9 \"q\""})"), std::string::npos);
  ASSERT_EQ(capriole_document_object_json(document, 4, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("t":"stream")"), std::string::npos);
  ASSERT_EQ(capriole_document_object_json(document, 99, &json), CAPRIOLE_OK);  // absent: null
  EXPECT_NE(take_json(json).find(R"("object":{"t":"null"})"), std::string::npos);
  capriole_document_close(document);
}

TEST(CApiDocument, StreamData) {
  capriole_document* document = open_document(sample_pdf());
  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(capriole_document_stream_data(document, 4, 1, &data, &size), CAPRIOLE_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(data), size), "BT (Hi) Tj ET");  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  capriole_free(data);
  ASSERT_EQ(capriole_document_stream_data(document, 4, 0, &data, &size), CAPRIOLE_OK);  // still Flate
  EXPECT_NE(size, 13U);
  capriole_free(data);
  EXPECT_EQ(capriole_document_stream_data(document, 3, 1, &data, &size), CAPRIOLE_ERROR_INVALID_ARGUMENT);  // not a stream
  capriole_document_close(document);
}

TEST(CApiDocument, PageContent) {
  capriole_document* document = open_document(sample_pdf());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_page_content_json(document, 0, 100, &json), CAPRIOLE_OK) << capriole_last_error_message();
  const std::string content = take_json(json);
  EXPECT_NE(content.find(R"("page":0,"ref":[3,0])"), std::string::npos) << content;
  EXPECT_NE(content.find(R"("total":3,"recovered":0)"), std::string::npos);
  EXPECT_NE(content.find(R"("op":"Tj","category":"text_showing","operands":[{"t":"string","form":"literal","v":"Hi"}])"), std::string::npos);
  ASSERT_EQ(capriole_document_page_content_json(document, 0, 1, &json), CAPRIOLE_OK);  // capped: the total stays
  const std::string capped = take_json(json);
  EXPECT_NE(capped.find(R"("total":3)"), std::string::npos);
  EXPECT_EQ(capped.find(R"("op":"Tj")"), std::string::npos);
  EXPECT_EQ(capriole_document_page_content_json(document, 1, 100, &json), CAPRIOLE_ERROR_OUT_OF_RANGE);
  capriole_document_close(document);
}

TEST(CApiDocument, PagePreview) {
  capriole_document* document = open_document(sample_pdf());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_page_preview_json(document, 0, &json), CAPRIOLE_OK) << capriole_last_error_message();
  const std::string preview = take_json(json);
  EXPECT_NE(preview.find(R"("width":612,"height":792.5,"rotate":0)"), std::string::npos) << preview;
  EXPECT_NE(preview.find(R"("text":1,"group":0,"soft_mask":0})"), std::string::npos);
  EXPECT_NE(preview.find(R"("svg":"<svg xmlns=\"http://www.w3.org/2000/svg\")"), std::string::npos);
  EXPECT_EQ(capriole_document_page_preview_json(document, 5, &json), CAPRIOLE_ERROR_OUT_OF_RANGE);
  capriole_document_close(document);
}

TEST(CApiDocument, PageRender) {
  capriole::test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 50]/Contents 4 0 R>>");
  b.add_stream(4, "1 0 0 rg 0 0 50 50 re f");
  b.finish_table("/Size 5/Root 1 0 R");
  capriole_document* document = open_document(b.str());
  std::uint8_t* rgba = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  ASSERT_EQ(capriole_document_page_render(document, 0, 144, &rgba, &width, &height), CAPRIOLE_OK) << capriole_last_error_message();
  ASSERT_NE(rgba, nullptr);
  EXPECT_EQ(width, 200U);
  EXPECT_EQ(height, 100U);
  const std::span<const std::uint8_t> pixels(rgba, std::size_t{width} * height * 4);
  const auto pixel = [&](std::uint32_t x, std::uint32_t y) { return pixels.subspan((std::size_t{y} * width + x) * 4, 4); };
  EXPECT_EQ(pixel(20, 20)[0], 255);  // the red square, left half
  EXPECT_EQ(pixel(20, 20)[1], 0);
  EXPECT_EQ(pixel(150, 50)[1], 255);  // white page, right half
  EXPECT_EQ(pixel(150, 50)[3], 255);
  capriole_free(rgba);
  char* json = nullptr;
  ASSERT_EQ(capriole_last_missing_resources_json(&json), CAPRIOLE_OK);
  EXPECT_EQ(take_json(json), "[]");
  EXPECT_EQ(capriole_document_page_render(document, 1, 72, &rgba, &width, &height), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(rgba, nullptr);
  EXPECT_EQ(capriole_document_page_render(document, 0, 0, &rgba, &width, &height), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_page_render(document, 0, 1e6, &rgba, &width, &height), CAPRIOLE_ERROR_LIMIT_EXCEEDED);
  EXPECT_EQ(capriole_document_page_render(document, 0, 72, nullptr, &width, &height), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(document);
}

TEST(CApiDocument, PageText) {
  // "Café" and "€1" in Helvetica (WinAnsi: é = 0xE9, € = 0x80) at size 10 on a 100 × 50 page.
  capriole::test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 50]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  b.add_stream(4, "BT /F1 10 Tf 10 20 Td (Caf\xE9 \x80\x31) Tj ET");
  b.add_object(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>");
  b.finish_table("/Size 6/Root 1 0 R");
  capriole_document* document = open_document(b.str());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_page_text_json(document, 0, CAPRIOLE_TEXT_ORDER_CONTENT, &json), CAPRIOLE_OK);
  const std::string text = take_json(json);
  EXPECT_NE(text.find(R"({"page":0,"width":100.00,"height":50.00,"order":"content","text":"Caf\u00e9 \u20ac1\n")"), std::string::npos) << text;
  EXPECT_NE(text.find(R"("blocks":[[)"), std::string::npos);
  EXPECT_NE(text.find(R"("words":[[)"), std::string::npos);
  // A character: its text, box, size, angle, invisible, stream (null: the page), operator offset and
  // ordinal, byte of its code, font.
  EXPECT_NE(text.find(R"(["\u00e9",)"), std::string::npos);
  EXPECT_NE(text.find(R"(,10.00,0,0,null,32,3,3,[5,0]])"), std::string::npos) << text;
  for (const char c : text) {
    EXPECT_LT(static_cast<unsigned char>(c), 0x80);
  }
  ASSERT_EQ(capriole_document_page_text_json(document, 0, CAPRIOLE_TEXT_ORDER_LAYOUT, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("order":"layout")"), std::string::npos);
  EXPECT_EQ(capriole_document_page_text_json(document, 0, 7, &json), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_page_text_json(document, 1, CAPRIOLE_TEXT_ORDER_CONTENT, &json), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_page_text_json(nullptr, 0, CAPRIOLE_TEXT_ORDER_CONTENT, &json), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(document);
}

TEST(CApiDocument, PageOperations) {
  // Two documents of one page each (the sample has a content stream): import, duplicate, rotate,
  // delete, then a compact save that reads back with the expected pages.
  capriole_document* document = open_document(sample_pdf());
  capriole_document* source = open_document(sample_pdf());
  const std::uint32_t first = 0;
  ASSERT_EQ(capriole_document_pages_import(document, source, &first, 1, 1), CAPRIOLE_OK) << capriole_last_error_message();
  capriole_document_close(source);
  const std::array<std::uint32_t, 3> order = {1, 0, 0};
  ASSERT_EQ(capriole_document_pages_arrange(document, order.data(), order.size()), CAPRIOLE_OK);
  const std::uint32_t last = 2;
  ASSERT_EQ(capriole_document_pages_rotate(document, &last, 1, 270), CAPRIOLE_OK);
  EXPECT_EQ(capriole_document_pages_rotate(document, &last, 1, 30), CAPRIOLE_ERROR_UNSUPPORTED);
  const std::uint32_t past = 3;
  EXPECT_EQ(capriole_document_pages_rotate(document, &past, 1, 90), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_pages_arrange(document, order.data(), 0), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_pages_import(document, nullptr, &first, 1, 0), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  const std::array<std::uint32_t, 2> two = {0, 2};
  ASSERT_EQ(capriole_document_pages_arrange(document, two.data(), two.size()), CAPRIOLE_OK);

  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(capriole_document_save_compact(document, nullptr, &data, &size), CAPRIOLE_OK);
  const std::string saved(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  capriole_free(data);
  capriole_document_close(document);
  capriole_document* reopened = open_document(saved);
  char* json = nullptr;
  ASSERT_EQ(capriole_document_summary_json(reopened, &json), CAPRIOLE_OK);
  const std::string summary = take_json(json);
  EXPECT_NE(summary.find(R"("repairs":[])"), std::string::npos) << summary;
  ASSERT_EQ(capriole_document_page_content_json(reopened, 1, 10, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find("Hi"), std::string::npos);
  EXPECT_EQ(capriole_document_page_content_json(reopened, 2, 10, &json), CAPRIOLE_ERROR_OUT_OF_RANGE);
  capriole_document_close(reopened);
}

TEST(CApiDocument, Annotations) {
  capriole_document* document = open_document(sample_pdf());
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  ASSERT_EQ(capriole_document_annotation_add(
                document, 0, R"({"kind":"square","rect":[10,10,60,40],"color":[0,0,1],"width":2,"contents":"Citt\u00e0","author":"N"})", &number, &generation),
            CAPRIOLE_OK)
      << capriole_last_error_message();
  EXPECT_GT(number, 0U);
  char* json = nullptr;
  ASSERT_EQ(capriole_document_page_annotations_json(document, 0, &json), CAPRIOLE_OK);
  const std::string listed = take_json(json);
  EXPECT_NE(listed.find(std::format(R"({{"ref":[{},0],"subtype":"Square","rect":[10.00,10.00,60.00,40.00],"contents":"Citt\u00e0","author":"N")", number)),
            std::string::npos)
      << listed;
  EXPECT_NE(listed.find(R"("editable":true)"), std::string::npos);
  EXPECT_EQ(capriole_document_annotation_update(document, 0, number, generation, R"({"moveBy":[5,0],"color":[1,0,0]})"), CAPRIOLE_OK);
  ASSERT_EQ(capriole_document_page_annotations_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("rect":[15.00,10.00,65.00,40.00])"), std::string::npos);
  EXPECT_EQ(capriole_document_annotation_update(document, 0, number, generation, R"({"rect":[20,10,90,60]})"), CAPRIOLE_OK);
  ASSERT_EQ(capriole_document_page_annotations_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("rect":[20.00,10.00,90.00,60.00])"), std::string::npos);
  EXPECT_EQ(capriole_document_annotation_update(document, 0, number, generation, R"({"fontSize":"big"})"), CAPRIOLE_ERROR_SYNTAX);
  EXPECT_EQ(capriole_document_annotation_update(document, 0, number, generation, R"({"fontSize":14})"), CAPRIOLE_ERROR_UNSUPPORTED);  // a square
  EXPECT_EQ(capriole_document_annotation_remove(document, 0, number, generation), CAPRIOLE_OK);
  ASSERT_EQ(capriole_document_page_annotations_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_EQ(take_json(json), "[]");

  // Malformed or hostile specifications: refused, nothing added.
  for (const char* bad :
       {"", "{", R"({"kind":"square"})", R"({"kind":"spiral","rect":[0,0,1,1]})", R"({"kind":"square","rect":[0,0,1]})", R"({"kind":"square","rect":"no"})",
        R"({"kind":"note","rect":[0,0,9,9],"contents":"\ud800"})", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
        R"({"kind":"note","rect":[0,0,9,9]} x)", R"({"kind":"note","rect":[01,0,9,9]})", R"({"kind":"line","from":[0,0],"to":[1e999,0]})"}) {
    SCOPED_TRACE(bad);
    EXPECT_NE(capriole_document_annotation_add(document, 0, bad, &number, &generation), CAPRIOLE_OK);
  }
  ASSERT_EQ(capriole_document_page_annotations_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_EQ(take_json(json), "[]");
  EXPECT_EQ(capriole_document_annotation_add(document, 3, R"({"kind":"note","rect":[0,0,9,9]})", &number, &generation), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_annotation_remove(document, 0, 999, 0), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_annotation_update(document, 0, 1, 70000, "{}"), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(document);
}

TEST(CApiDocument, Forms) {
  capriole::test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[4 0 R 5 0 R 6 0 R]/DA(/Helv 0 Tf 0 g)>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Annots[4 0 R 5 0 R 6 0 R]>>");
  b.add_object(4, "<</FT/Tx/T(nome)/TU(Il tuo nome)/MaxLen 20/Subtype/Widget/Rect[10 70 110 90]>>");
  b.add_object(5, "<</FT/Ch/Ff 2097152/T(lingue)/Opt[[(it)(Italiano)][(en)(English)]]/Subtype/Widget/Rect[10 10 110 60]>>");
  b.add_object(6, "<</FT/Btn/T(ok)/V/Off/Subtype/Widget/Rect[150 70 164 84]>>");
  b.finish_table("/Size 7/Root 1 0 R");
  capriole_document* document = open_document(b.str());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_form_fields_json(document, &json), CAPRIOLE_OK);
  const std::string listed = take_json(json);
  EXPECT_NE(listed.find(R"({"ref":[4,0],"name":"nome","label":"Il tuo nome","type":"text","value":[],"options":[],"maxLength":20,"readOnly":false,)"
                        R"("required":false,"multiline":false,"password":false,"comb":false,"editable":false,"multiSelect":false,"fillable":true,)"
                        R"("widgets":[{"ref":[4,0],"page":0,"rect":[10.00,10.00,110.00,30.00],"onState":""}]})"),
            std::string::npos)
      << listed;
  EXPECT_NE(listed.find(R"("type":"listbox","value":[],"options":[{"value":"it","display":"Italiano"},{"value":"en","display":"English"}])"), std::string::npos)
      << listed;
  EXPECT_NE(listed.find(R"("type":"checkbox","value":["Off"])"), std::string::npos) << listed;

  EXPECT_EQ(capriole_document_form_field_fill(document, 4, 0, R"("Citt\u00e0")"), CAPRIOLE_OK) << capriole_last_error_message();
  EXPECT_EQ(capriole_document_form_field_fill(document, 5, 0, R"(["it","en"])"), CAPRIOLE_OK) << capriole_last_error_message();
  EXPECT_EQ(capriole_document_form_field_fill(document, 6, 0, R"("Yes")"), CAPRIOLE_OK) << capriole_last_error_message();
  ASSERT_EQ(capriole_document_form_fields_json(document, &json), CAPRIOLE_OK);
  const std::string filled = take_json(json);
  EXPECT_NE(filled.find(R"("value":["Citt\u00e0"])"), std::string::npos) << filled;
  EXPECT_NE(filled.find(R"("value":["it","en"])"), std::string::npos) << filled;
  EXPECT_NE(filled.find(R"("value":["Yes"])"), std::string::npos) << filled;
  EXPECT_EQ(capriole_document_form_field_fill(document, 4, 0, "null"), CAPRIOLE_OK);

  // Malformed values, values the field does not take, what is not a field.
  for (const char* bad : {"", "{", "1", "[1]", R"({"a":"b"})", R"("x" "y")"}) {
    SCOPED_TRACE(bad);
    EXPECT_EQ(capriole_document_form_field_fill(document, 4, 0, bad), CAPRIOLE_ERROR_SYNTAX);
  }
  EXPECT_EQ(capriole_document_form_field_fill(document, 5, 0, R"(["fr"])"), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_form_field_fill(document, 4, 0, R"("ventuno caratteri qui")"), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_form_field_fill(document, 3, 0, R"("x")"), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(capriole_document_form_field_fill(document, 4, 70000, R"("x")"), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_form_field_fill(nullptr, 4, 0, R"("x")"), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_form_fields_json(document, nullptr), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(document);
}

TEST(CApiDocument, AttachmentsAndFacturx) {
  capriole_document* document = open_document(sample_pdf());
  const std::string data = "ciao";
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  ASSERT_EQ(capriole_document_attachment_add(document, R"({"fileName":"nota \u00e8.txt","mimeType":"text/plain","relationship":"Source"})", bytes_of(data),
                                             data.size(), &number, &generation),
            CAPRIOLE_OK)
      << capriole_last_error_message();
  char* json = nullptr;
  ASSERT_EQ(capriole_document_attachments_json(document, &json), CAPRIOLE_OK);
  const std::string listed = take_json(json);
  EXPECT_NE(listed.find(std::format(R"({{"ref":[{},0],"name":"nota \u00e8.txt","fileName":"nota \u00e8.txt","description":"","mimeType":"text/plain",)"
                                    R"("size":4,"creationDate":"","modificationDate":"","relationship":"Source","page":null,"associated":true}})",
                                    number)),
            std::string::npos)
      << listed;
  std::uint8_t* out = nullptr;
  std::size_t size = 0;
  int checksum = -2;
  ASSERT_EQ(capriole_document_attachment_data(document, number, generation, &out, &size, &checksum), CAPRIOLE_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out), size), "ciao");  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  EXPECT_EQ(checksum, 1);
  capriole_free(out);

  ASSERT_EQ(capriole_document_facturx_json(document, &json), CAPRIOLE_OK);
  EXPECT_EQ(take_json(json), "null");
  const std::string invoice =
      R"(<rsm:CrossIndustryInvoice xmlns:rsm="urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100"><rsm:ExchangedDocumentContext>)"
      R"(<ram:GuidelineSpecifiedDocumentContextParameter><ram:ID>urn:factur-x.eu:1p0:minimum</ram:ID></ram:GuidelineSpecifiedDocumentContextParameter>)"
      R"(</rsm:ExchangedDocumentContext></rsm:CrossIndustryInvoice>)";
  EXPECT_EQ(capriole_document_facturx_embed(document, bytes_of(invoice), invoice.size(), nullptr, &json), CAPRIOLE_ERROR_UNSUPPORTED);  // no date
  ASSERT_EQ(capriole_document_facturx_embed(document, bytes_of(invoice), invoice.size(), "D:20261003120000Z", &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("conformanceLevel":"MINIMUM","pdfa3":false})"), std::string::npos);
  ASSERT_EQ(capriole_document_facturx_json(document, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("fileName":"factur-x.xml","conformanceLevel":"MINIMUM","version":"1.0","pdfa3":false})"), std::string::npos);

  EXPECT_EQ(capriole_document_attachment_remove(document, number, generation), CAPRIOLE_OK);
  EXPECT_EQ(capriole_document_attachment_remove(document, number, generation), CAPRIOLE_ERROR_OUT_OF_RANGE);
  for (const char* bad : {"", "[]", R"({"fileName":1})", R"({"description":"no name"})"}) {
    SCOPED_TRACE(bad);
    EXPECT_NE(capriole_document_attachment_add(document, bad, bytes_of(data), data.size(), &number, &generation), CAPRIOLE_OK);
  }
  EXPECT_EQ(capriole_document_attachment_add(document, R"({"fileName":"x"})", nullptr, 3, &number, &generation), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_attachment_add(document, R"({"fileName":"vuoto"})", nullptr, 0, &number, &generation), CAPRIOLE_OK);  // an empty file
  capriole_document_close(document);
}

TEST(CApiDocument, MissingResourcesAreReportedThenUsed) {
  // A Japanese font that is not embedded, with a predefined CMap: both come from the resource pack.
  capriole::test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 50]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  b.add_stream(4, "BT /F1 12 Tf <0041> Tj ET");
  b.add_object(5,
               "<</Type/Font/Subtype/Type0/BaseFont/X/Encoding/UniJIS-UCS2-H/DescendantFonts[<</Type/Font/Subtype/CIDFontType0/BaseFont/X"
               "/CIDSystemInfo<</Registry(Adobe)/Ordering(Japan1)/Supplement 4>>>>]>>");
  b.finish_table("/Size 6/Root 1 0 R");
  capriole_document* document = open_document(b.str());
  char* json = nullptr;
  ASSERT_EQ(capriole_document_page_preview_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("missing_resources":["cmap/UniJIS-UCS2-H","font/NotoSansJP-Regular.otf"])"), std::string::npos);
  ASSERT_EQ(capriole_last_missing_resources_json(&json), CAPRIOLE_OK);
  EXPECT_EQ(take_json(json), R"(["cmap/UniJIS-UCS2-H","font/NotoSansJP-Regular.otf"])");
  const std::string cmap = "1 begincodespacerange <0000> <FFFF> endcodespacerange 1 begincidchar <0041> 34 endcidchar";
  ASSERT_EQ(capriole_resource_add("cmap/UniJIS-UCS2-H", reinterpret_cast<const std::uint8_t*>(cmap.data()),
                                  cmap.size()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            CAPRIOLE_OK);
  ASSERT_EQ(capriole_document_page_preview_json(document, 0, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("missing_resources":["font/NotoSansJP-Regular.otf"])"), std::string::npos);
  EXPECT_EQ(capriole_resource_add(nullptr, nullptr, 0), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_last_missing_resources_json(nullptr), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(document);
}

TEST(CApiDocument, SaveFullAndIncremental) {
  const std::string pdf = sample_pdf();
  capriole_document* document = open_document(pdf);
  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(capriole_document_save(document, 1, "D:20260927Z", &data, &size), CAPRIOLE_OK) << capriole_last_error_message();
  const std::string incremental(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  capriole_free(data);
  EXPECT_EQ(incremental.substr(0, pdf.size()), pdf);
  EXPECT_NE(incremental.find("D:20260927Z", pdf.size()), std::string::npos);
  ASSERT_EQ(capriole_document_save(document, 0, nullptr, &data, &size), CAPRIOLE_OK);
  const std::string full(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  capriole_free(data);
  capriole_document_close(document);
  capriole_document* reopened = open_document(full, nullptr, 0);  // the output reads strictly
  capriole_document_close(reopened);
}

TEST(CApiDocument, EncryptedDocumentsNeedTheirPassword) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/r6_aes_256.pdf", std::ios::binary);
  const std::string pdf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  capriole_document* document = nullptr;
  EXPECT_EQ(capriole_document_open(bytes_of(pdf), pdf.size(), nullptr, 1, &document), CAPRIOLE_ERROR_PASSWORD_REQUIRED);
  EXPECT_EQ(document, nullptr);
  document = open_document(pdf, "user");
  char* json = nullptr;
  ASSERT_EQ(capriole_document_summary_json(document, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"("encryption":{"revision":6,"strings":"AES-256")"), std::string::npos);
  capriole_document_close(document);
}

TEST(CApiDocument, RepairsAreListedAndCanBeRefused) {
  std::string pdf = sample_pdf();
  pdf.resize(pdf.find("xref"));
  capriole_document* strict = nullptr;
  EXPECT_NE(capriole_document_open(bytes_of(pdf), pdf.size(), nullptr, 0, &strict), CAPRIOLE_OK);
  capriole_document* document = open_document(pdf);
  char* json = nullptr;
  ASSERT_EQ(capriole_document_summary_json(document, &json), CAPRIOLE_OK);
  EXPECT_NE(take_json(json).find(R"({"kind":"xref_reconstructed")"), std::string::npos);
  capriole_document_close(document);
}

TEST(CApiDocument, RejectsNullArguments) {
  capriole_document* document = nullptr;
  char* json = nullptr;
  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(capriole_document_open(nullptr, 3, nullptr, 1, &document), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_open(nullptr, 0, nullptr, 1, nullptr), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_summary_json(nullptr, &json), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_object_json(nullptr, 1, &json), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_stream_data(nullptr, 1, 1, &data, &size), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_save(nullptr, 0, nullptr, &data, &size), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  capriole_document_close(nullptr);
  capriole_free(nullptr);
}

}  // namespace
