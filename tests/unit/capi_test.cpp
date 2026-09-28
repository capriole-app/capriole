#include <gtest/gtest.h>

#include <cstdint>
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
  EXPECT_EQ(capriole_document_page_render(document, 1, 72, &rgba, &width, &height), CAPRIOLE_ERROR_OUT_OF_RANGE);
  EXPECT_EQ(rgba, nullptr);
  EXPECT_EQ(capriole_document_page_render(document, 0, 0, &rgba, &width, &height), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_document_page_render(document, 0, 1e6, &rgba, &width, &height), CAPRIOLE_ERROR_LIMIT_EXCEEDED);
  EXPECT_EQ(capriole_document_page_render(document, 0, 72, nullptr, &width, &height), CAPRIOLE_ERROR_INVALID_ARGUMENT);
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
