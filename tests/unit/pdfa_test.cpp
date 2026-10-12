// Converting documents to PDF/A (PLAN.md phase 8i). Conformance itself is veraPDF's to judge
// (corpus-tools/check_pdfa.py); here: what the conversion puts right, and what it refuses.

#include "capriole/edit/pdfa.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/attachments.hpp"
#include "capriole/model/object.hpp"
#include "font/program.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using model::Dict;
using model::Object;

constexpr const char* kDate = "D:20261004150000Z";

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  std::memcpy(out.data(), text.data(), text.size());
  return out;
}

Document open(std::vector<std::byte> data) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(std::move(data)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

// One page with text in font /F1 (object 5, given by font), content of extra, annotations of annots.
std::vector<std::byte> page_pdf(const std::string& font, const std::string& content = "BT /F1 12 Tf 20 150 Td (Hello) Tj ET",
                                const std::string& catalog_extra = "", const std::string& annots = "") {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R" + catalog_extra + ">>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>" + annots + ">>");
  b.add_stream(4, content);
  b.add_object(5, font);
  b.add_object(6, "<</Type/Annot/Subtype/Square/Rect[10 10 50 50]/C[1 0 0]/F 2>>");  // hidden
  b.add_object(7, "<</S/JavaScript/JS(app.alert\\(1\\))>>");
  b.add_object(8, "<</Type/Annot/Subtype/Square/Rect[60 10 100 50]/C[0 0 1]>>");  // shown, no appearance, not printable
  b.finish_table("/Root 1 0 R");
  return bytes(b.str());
}

Result<PdfaResult> convert(std::vector<std::byte> pdf, int part = 0) {
  Document document = open(std::move(pdf));
  return convert_to_pdfa(document, PdfaOptions{.part = part, .date = kDate});
}

const Dict& dict_of(Document& document, const Object& object) {
  auto resolved = document.resolve(object);
  EXPECT_TRUE(resolved && resolved->as_dict() != nullptr);
  static Dict kept;
  kept = resolved && resolved->as_dict() != nullptr ? *resolved->as_dict() : Dict();
  return kept;
}

TEST(Pdfa, PutsRightWhatItCan) {
  const std::string helvetica = "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>";
  auto result = convert(page_pdf(helvetica, "BT /F1 12 Tf 20 150 Td (Hello) Tj ET", "/OpenAction 7 0 R", "/Annots[6 0 R 8 0 R]"));
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->part, 2);
  Document out = open(result->file);
  // PDF 1.7 at most; written in full (one revision).
  EXPECT_LE(out.header().minor, 7);
  auto catalog = out.catalog();
  ASSERT_TRUE(catalog.has_value());
  EXPECT_EQ((*catalog)->find("OpenAction"), nullptr);
  // The output intent, with an sRGB profile of 3 components.
  const Object* intents = (*catalog)->find("OutputIntents");
  ASSERT_NE(intents, nullptr);
  auto list = out.resolve(*intents);
  ASSERT_TRUE(list && list->as_array() != nullptr && list->as_array()->size() == 1);
  const Dict intent = dict_of(out, list->as_array()->front());
  EXPECT_EQ(intent.find("S")->as_name()->bytes, "GTS_PDFA1");
  auto profile = out.resolve(*intent.find("DestOutputProfile"));
  ASSERT_TRUE(profile && profile->as_stream() != nullptr);
  EXPECT_EQ(profile->as_stream()->dict.find("N")->as_integer(), 3);
  // Dated 2026-01-01 00:00:00, not by the clock: LittleCMS dated it with the time it was made, and two
  // conversions a second apart differed (Pdfa.IsDeterministic failed now and then).
  auto icc = out.decode_stream(*profile->as_stream());
  ASSERT_TRUE(icc && icc->size() > 36);
  const std::vector<std::byte> created(icc->begin() + 24, icc->begin() + 36);
  EXPECT_EQ(created, (std::vector<std::byte>{std::byte{0x07}, std::byte{0xEA}, std::byte{0}, std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0},
                                             std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
  // XMP: uncompressed, saying PDF/A-2b.
  auto metadata = out.resolve(*(*catalog)->find("Metadata"));
  ASSERT_TRUE(metadata && metadata->as_stream() != nullptr);
  EXPECT_EQ(metadata->as_stream()->dict.find("Filter"), nullptr);
  auto xmp = out.decode_stream(*metadata->as_stream());
  ASSERT_TRUE(xmp.has_value());
  const std::string text(crypto::der::chars_of(*xmp));
  EXPECT_NE(text.find("<pdfaid:part>2</pdfaid:part>"), std::string::npos);
  EXPECT_NE(text.find("<pdfaid:conformance>B</pdfaid:conformance>"), std::string::npos);
  EXPECT_NE(text.find("<xmp:ModifyDate>2026-10-04T15:00:00Z</xmp:ModifyDate>"), std::string::npos);
  // The font: Helvetica embedded (CFF), its widths those of the program.
  auto page_refs = out.page_refs();
  ASSERT_TRUE(page_refs.has_value());
  const Dict page = dict_of(out, Object((*page_refs)[0]));
  const Dict resources = dict_of(out, *page.find("Resources"));
  const Dict fonts = dict_of(out, *resources.find("Font"));
  const Dict font = dict_of(out, *fonts.find("F1"));
  const Dict descriptor = dict_of(out, *font.find("FontDescriptor"));
  auto program = out.resolve(*descriptor.find("FontFile3"));
  ASSERT_TRUE(program && program->as_stream() != nullptr);
  EXPECT_EQ(program->as_stream()->dict.find("Subtype")->as_name()->bytes, "Type1C");
  auto widths = out.resolve(*font.find("Widths"));
  ASSERT_TRUE(widths && widths->as_array() != nullptr);
  // The whole encoding written: H among the codes, at Helvetica's width.
  const auto first = font.find("FirstChar")->as_integer().value_or(0);
  ASSERT_LE(first, 'H');
  const Object& h = widths->as_array()->at(static_cast<std::size_t>('H' - first));
  EXPECT_NEAR(h.as_number().value_or(0), 722, 1);
  // Annotations: the hidden one gone; the other printable, with an appearance.
  auto annots = out.resolve(*page.find("Annots"));
  ASSERT_TRUE(annots && annots->as_array() != nullptr);
  ASSERT_EQ(annots->as_array()->size(), 1U);
  const Dict square = dict_of(out, annots->as_array()->front());
  EXPECT_EQ(square.find("F")->as_integer().value_or(0) & 4, 4);
  EXPECT_NE(dict_of(out, *square.find("AP")).find("N"), nullptr);
  EXPECT_FALSE(result->changes.empty());
}

TEST(Pdfa, IsDeterministic) {
  const std::string helvetica = "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>";
  auto a = convert(page_pdf(helvetica));
  auto b = convert(page_pdf(helvetica));
  ASSERT_TRUE(a.has_value() && b.has_value());
  EXPECT_EQ(a->file, b->file);
  // A converted file converts again (what was checked is what was written).
  auto again = convert(a->file);
  ASSERT_TRUE(again.has_value()) << again.error().message;
  EXPECT_EQ(again->part, 2);
}

TEST(Pdfa, RefusesWhatItCannotPutRight) {
  // A font that is not embedded and has no substitute of its metrics.
  auto verdana = convert(page_pdf("<</Type/Font/Subtype/TrueType/BaseFont/Verdana/FirstChar 72/LastChar 72/Widths[748]>>"));
  ASSERT_FALSE(verdana.has_value());
  EXPECT_EQ(verdana.error().code, ErrorCode::kUnsupported);
  EXPECT_NE(verdana.error().message.find("Verdana"), std::string::npos) << verdana.error().message;
  // Arial Narrow: a standard-looking name, other metrics.
  auto narrow = convert(page_pdf("<</Type/Font/Subtype/TrueType/BaseFont/ArialNarrow/FirstChar 72/LastChar 72/Widths[592]>>"));
  ASSERT_FALSE(narrow.has_value());
  EXPECT_NE(narrow.error().message.find("metrics"), std::string::npos) << narrow.error().message;
  // DeviceCMYK colour, with every reason said.
  auto cmyk = convert(page_pdf("<</Type/Font/Subtype/Type1/BaseFont/Verdana>>", "0 0 0 1 k 10 10 50 50 re f BT /F1 12 Tf 20 150 Td (Hi) Tj ET"));
  ASSERT_FALSE(cmyk.has_value());
  EXPECT_NE(cmyk.error().message.find("DeviceCMYK"), std::string::npos);
  EXPECT_NE(cmyk.error().message.find("Verdana"), std::string::npos);
  // No date; PDF/A-2 asked for a document with embedded files.
  Document no_date = open(page_pdf("<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>"));
  EXPECT_FALSE(convert_to_pdfa(no_date, PdfaOptions{.part = 2, .date = ""}).has_value());
  Document with_file = open(page_pdf("<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>"));
  AttachmentSpec spec;
  spec.file_name = "note.txt";
  spec.mime_type = "text/plain";
  spec.relationship = "Supplement";
  spec.creation_date = kDate;
  spec.modification_date = kDate;
  ASSERT_TRUE(add_attachment(with_file, spec, crypto::der::bytes_of("hello")).has_value());
  EXPECT_FALSE(convert_to_pdfa(with_file, PdfaOptions{.part = 2, .date = kDate}).has_value());
}

TEST(Pdfa, EmbeddedFilesMakePdfa3) {
  Document document = open(page_pdf("<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>"));
  AttachmentSpec spec;
  spec.file_name = "note.txt";
  spec.mime_type = "text/plain";
  spec.relationship = "Supplement";
  spec.creation_date = kDate;
  spec.modification_date = kDate;
  ASSERT_TRUE(add_attachment(document, spec, crypto::der::bytes_of("hello")).has_value());
  auto result = convert_to_pdfa(document, PdfaOptions{.part = 0, .date = kDate});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->part, 3);
  Document out = open(result->file);
  auto catalog = out.catalog();
  ASSERT_TRUE(catalog.has_value());
  EXPECT_NE((*catalog)->find("AF"), nullptr);
  auto files = list_attachments(out);
  ASSERT_TRUE(files.has_value());
  ASSERT_EQ(files->size(), 1U);
  EXPECT_EQ((*files)[0].relationship, "Supplement");
}

TEST(Pdfa, PutsAnEmbeddedTrueTypeFontsWidthsRight) {
  // square.ttf (tests/data/fonts): "A" is glyph 1, 700/1000 of an em wide; the dictionary says 600, which is
  // where the text goes. The program is given 600.
  std::ifstream in(std::string(CAPRIOLE_TEST_DATA_DIR) + "/fonts/square.ttf", std::ios::binary);
  const std::string ttf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  b.add_stream(4, "BT /F1 12 Tf 20 150 Td (AA) Tj ET");
  b.add_object(5, "<</Type/Font/Subtype/TrueType/BaseFont/Square/FirstChar 65/LastChar 65/Widths[600]/Encoding/WinAnsiEncoding/FontDescriptor 6 0 R>>");
  b.add_object(
      6,
      "<</Type/FontDescriptor/FontName/Square/Flags 32/FontBBox[0 0 1000 1000]/ItalicAngle 0/Ascent 800/Descent -200/CapHeight 700/StemV 80/FontFile2 7 0 R>>");
  b.add_stream(7, ttf, "/Length1 " + std::to_string(ttf.size()));
  b.finish_table("/Root 1 0 R");
  auto result = convert(bytes(b.str()));
  ASSERT_TRUE(result.has_value()) << result.error().message;
  Document out = open(result->file);
  auto pages = out.page_refs();
  ASSERT_TRUE(pages.has_value());
  const Dict page = dict_of(out, Object((*pages)[0]));
  const Dict resources = dict_of(out, *page.find("Resources"));
  const Dict fonts = dict_of(out, *resources.find("Font"));
  const Dict font = dict_of(out, *fonts.find("F1"));
  const Dict descriptor = dict_of(out, *font.find("FontDescriptor"));
  auto file = out.resolve(*descriptor.find("FontFile2"));
  ASSERT_TRUE(file && file->as_stream() != nullptr);
  auto data = out.decode_stream(*file->as_stream());
  ASSERT_TRUE(data.has_value());
  auto program = font::detail::FontProgram::open(std::make_shared<const std::vector<std::byte>>(*data));
  ASSERT_TRUE(program.has_value());
  EXPECT_NEAR((*program)->advance(1).value_or(0), 600, 0.5);
  EXPECT_NEAR((*program)->advance(2).value_or(0), 700, 0.5);  // a glyph not shown keeps its own
}

TEST(Pdfa, RefusesEncryptedDocuments) {
  std::ifstream in(std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/r4_aes_128_empty_user.pdf", std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto result = convert(bytes(std::string(chars.begin(), chars.end())));
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("encrypted"), std::string::npos);
}

}  // namespace
}  // namespace capriole::edit
