// Extending embedded TrueType subsets with glyphs of the full font (PLAN.md phase 9c): font/sfnt and
// edit/font_extend, through edit/text_edit and edit/paragraph_edit. The fonts are tests/data/fonts'
// CprSquare (make_fixtures.py): full.ttf (A, B, C), subset.ttf (A), other.ttf (another version),
// restricted.ttf (fsType 2).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/font_library.hpp"
#include "capriole/edit/paragraph_edit.hpp"
#include "capriole/edit/text_edit.hpp"
#include "capriole/text/text.hpp"
#include "font/program.hpp"
#include "font/sfnt.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

std::string fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/fonts/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

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

// A page showing "AA" in CprSquare, embedded as subset.ttf: composite (Identity-H, a /ToUnicode for CID
// 1, the subset's A) or simple (TrueType, WinAnsiEncoding). Below it, at y = 100 (as shown), nothing.
std::string page_pdf(bool composite, const std::string& below = "") {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R/F2 10 0 R>>>>>>");
  b.add_stream(4, (composite ? "BT /F1 20 Tf 20 150 Td <00010001> Tj ET" : "BT /F1 20 Tf 20 150 Td (AA) Tj ET") + below);
  if (composite) {
    b.add_object(5, "<</Type/Font/Subtype/Type0/BaseFont/ABCDEF+CprSquare/Encoding/Identity-H/DescendantFonts[6 0 R]/ToUnicode 9 0 R>>");
    b.add_object(6,
                 "<</Type/Font/Subtype/CIDFontType2/BaseFont/ABCDEF+CprSquare/CIDSystemInfo<</Registry(Adobe)/Ordering(Identity)/Supplement 0>>"
                 "/FontDescriptor 7 0 R/CIDToGIDMap/Identity/DW 1000/W[1[700]]>>");
  } else {
    b.add_object(5,
                 "<</Type/Font/Subtype/TrueType/BaseFont/ABCDEF+CprSquare/Encoding/WinAnsiEncoding/FirstChar 65/LastChar 65/Widths[700]"
                 "/FontDescriptor 7 0 R>>");
    b.add_object(6, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  }
  b.add_object(7,
               "<</Type/FontDescriptor/FontName/ABCDEF+CprSquare/Flags 32/FontBBox[0 0 900 1400]/ItalicAngle 0/Ascent 800/Descent -200"
               "/CapHeight 700/StemV 80/FontFile2 8 0 R>>");
  b.add_stream(8, fixture("subset.ttf"));
  b.add_stream(9,
               "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /CprSquare def 1 begincodespacerange <0000> <FFFF> "
               "endcodespacerange 1 beginbfchar <0001> <0041> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
  b.add_object(10, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  b.finish_table("/Root 1 0 R");
  return b.str();
}

text::PageText page_text(Document& document) {
  auto pages = document.page_refs();
  auto list = content::interpret_page(document, (*pages)[0], {}, nullptr, content::Purpose::kText);
  EXPECT_TRUE(list.has_value());
  return text::extract(*list, text::Order::kLayout);
}

const graphics::Rect kLine{15, 25, 100, 55};

// The embedded program now (object 8).
std::vector<std::byte> program(Document& document) {
  auto file = document.get(model::ObjectRef{8, 0});
  EXPECT_TRUE(file && (*file)->as_stream() != nullptr);
  auto data = document.decode_stream(*(*file)->as_stream());
  EXPECT_TRUE(data.has_value());
  return data ? *data : std::vector<std::byte>{};
}

FontLibrary library_of(const std::string& name) {
  FontLibrary library;
  auto added = library.add(bytes(fixture(name)));
  EXPECT_TRUE(added.has_value()) << (added ? "" : added.error().message);
  return library;
}

TEST(Sfnt, ExtendsAFontAndWritesItBackValid) {
  const auto subset_data = bytes(fixture("subset.ttf"));
  const auto full_data = bytes(fixture("full.ttf"));
  auto subset = font::detail::read_sfnt(subset_data);
  auto full = font::detail::read_sfnt(full_data);
  ASSERT_TRUE(subset && full);
  EXPECT_EQ(font::detail::postscript_name(*full), "CprSquare");
  EXPECT_EQ(font::detail::fs_type(*full), 0);
  EXPECT_EQ(font::detail::unicode_cmap(*full).at(U'C'), 3);
  // The subset's A is the full font's.
  EXPECT_TRUE(font::detail::same_outline(*font::detail::glyph_data(*subset, 1), *font::detail::glyph_data(*full, 1)));
  const std::vector<std::uint16_t> wanted{2, 3};  // B, C
  auto extended = font::detail::extend(*subset, *full, wanted);
  ASSERT_TRUE(extended.has_value()) << extended.error().message;
  EXPECT_EQ(extended->glyphs.at(2), 2);
  EXPECT_EQ(extended->glyphs.at(3), 3);
  EXPECT_EQ(font::detail::glyph_count(extended->font), 4);
  const auto written = font::detail::write_sfnt(extended->font);
  // FreeType reads it: four glyphs, the new ones the full font's.
  auto loaded = font::detail::FontProgram::open(std::make_shared<const std::vector<std::byte>>(written));
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_EQ((*loaded)->glyph_count(), 4U);
  EXPECT_NEAR(*(*loaded)->advance(3), 700, 1);
  auto reread = font::detail::read_sfnt(written);
  ASSERT_TRUE(reread.has_value());
  EXPECT_TRUE(font::detail::same_outline(*font::detail::glyph_data(*reread, 3), *font::detail::glyph_data(*full, 3)));
  // The whole file sums to the magic number (head's checkSumAdjustment).
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < written.size(); i += 4) {
    std::uint32_t word = 0;
    for (std::size_t k = 0; k < 4; ++k) {
      word = (word << 8U) | (i + k < written.size() ? std::to_integer<std::uint32_t>(written[i + k]) : 0U);
    }
    sum += word;
  }
  EXPECT_EQ(sum, 0xB1B0AFBAU);
  // A cmap with the new characters.
  auto cmap = font::detail::cmap_with(*reread, {{U'B', 2}, {U'C', 3}});
  ASSERT_TRUE(cmap.has_value());
  reread->tables["cmap"] = *cmap;
  EXPECT_EQ(font::detail::unicode_cmap(*reread).at(U'B'), 2);
  EXPECT_EQ(font::detail::unicode_cmap(*reread).at(U'A'), 1);
}

TEST(FontLibrary, ReadsFacesAndTheirPermissions) {
  FontLibrary library;
  auto full = library.add(bytes(fixture("full.ttf")));
  ASSERT_TRUE(full.has_value());
  ASSERT_EQ(full->size(), 1U);
  EXPECT_EQ(full->front().postscript_name, "CprSquare");
  EXPECT_TRUE(full->front().embeddable);
  auto restricted = library.add(bytes(fixture("restricted.ttf")));
  ASSERT_TRUE(restricted.has_value());
  EXPECT_FALSE(restricted->front().embeddable);
  EXPECT_EQ(library.faces().size(), 1U);  // the same name: replaced
  EXPECT_FALSE(library.add(bytes(fixture("square.cff"))).has_value());
  // A variable font (Ubuntu as installed): its glyphs are of no style in particular — refused.
  auto variable = font::detail::read_sfnt(bytes(fixture("full.ttf")));
  ASSERT_TRUE(variable.has_value());
  variable->tables["fvar"] = std::vector<std::byte>(16);
  auto refused = library.add(font::detail::write_sfnt(*variable));
  ASSERT_FALSE(refused.has_value());
  EXPECT_NE(refused.error().message.find("variable"), std::string::npos);
}

TEST(FontExtend, ACompositeSubsetGetsTheGlyphItLacks) {
  Document document = open(page_pdf(true));
  const FontLibrary library = library_of("full.ttf");
  auto result = replace_text(document, 0, kLine, "AB", {.fonts = &library});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->added_glyphs, 1U);
  EXPECT_EQ(page_text(document).plain(), "AB\n");
  // The program has B now (glyph 2 = CID 2); /W and /ToUnicode say what CID 2 is.
  auto loaded = font::detail::FontProgram::open(std::make_shared<const std::vector<std::byte>>(program(document)));
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ((*loaded)->glyph_count(), 3U);
  auto descendant = document.get(model::ObjectRef{6, 0});
  ASSERT_TRUE(descendant.has_value());
  const model::Object* w = (*descendant)->as_dict()->find("W");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->as_array()->size(), 4U);  // 1 [700] 2 [700]
}

// A CIDToGIDMap stream that already has an entry for every CID (as mPDF writes, CIDs being Unicode
// values): the new glyph takes a CID it maps to no glyph — the letter's Unicode value — where it took
// one past the map's end, beyond 0xFFFF, and was refused (found by corpus-tools/check_font_extend.py).
TEST(FontExtend, AFullCidToGidMapGivesTheNewGlyphAFreeCid) {
  std::string map(0x20000, '\0');
  map[0x41 * 2 + 1] = '\x01';  // CID 0x41 (A) → glyph 1
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  b.add_stream(4, "BT /F1 20 Tf 20 150 Td <00410041> Tj ET");
  b.add_object(5, "<</Type/Font/Subtype/Type0/BaseFont/MPDFAA+CprSquare/Encoding/Identity-H/DescendantFonts[6 0 R]/ToUnicode 9 0 R>>");
  b.add_object(6,
               "<</Type/Font/Subtype/CIDFontType2/BaseFont/MPDFAA+CprSquare/CIDSystemInfo<</Registry(Adobe)/Ordering(UCS)/Supplement 0>>"
               "/FontDescriptor 7 0 R/CIDToGIDMap 11 0 R/DW 1000/W[65[700]]>>");
  b.add_object(7,
               "<</Type/FontDescriptor/FontName/MPDFAA+CprSquare/Flags 32/FontBBox[0 0 900 1400]/ItalicAngle 0/Ascent 800/Descent -200"
               "/CapHeight 700/StemV 80/FontFile2 8 0 R>>");
  b.add_stream(8, fixture("subset.ttf"));
  b.add_stream(9,
               "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /CprSquare def 1 begincodespacerange <0000> <FFFF> "
               "endcodespacerange 1 beginbfchar <0041> <0041> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
  b.add_stream(11, map);
  b.finish_table("/Root 1 0 R");
  Document document = open(b.str());
  const FontLibrary library = library_of("full.ttf");
  auto result = replace_text(document, 0, kLine, "AB", {.fonts = &library});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->added_glyphs, 1U);
  EXPECT_EQ(page_text(document).plain(), "AB\n");
  auto stream = document.get(model::ObjectRef{11, 0});
  ASSERT_TRUE(stream.has_value());
  auto data = document.decode_stream(*(*stream)->as_stream());
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), 0x20000U);
  EXPECT_EQ(std::to_integer<int>((*data)[(0x42 * 2) + 1]), 2);  // CID 0x42 (B) → the glyph added
}

TEST(FontExtend, ASimpleSubsetGetsTheGlyphItsWinAnsiCodeNeeds) {
  Document document = open(page_pdf(false));
  const FontLibrary library = library_of("full.ttf");
  auto result = replace_text(document, 0, kLine, "ABC", {.fonts = &library});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->added_glyphs, 2U);
  EXPECT_EQ(page_text(document).plain(), "ABC\n");
  auto font = document.get(model::ObjectRef{5, 0});
  ASSERT_TRUE(font.has_value());
  EXPECT_EQ((*font)->as_dict()->find("LastChar")->as_integer(), 67);
}

// A symbolic subset without an encoding, its codes its own (code 1: A) and a /ToUnicode saying them — as
// LibreOffice embeds one: the new glyph takes a code the font does not use, after its last (found by
// corpus-tools/check_font_extend.py on LibreOffice's documents, which were not extended).
TEST(FontExtend, ASymbolicSubsetGetsAGlyphAtAFreeCode) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  b.add_stream(4, "BT /F1 20 Tf 20 150 Td <0101> Tj ET");
  b.add_object(5, "<</Type/Font/Subtype/TrueType/BaseFont/BAAAAA+CprSquare/FirstChar 0/LastChar 1/Widths[0 700]/FontDescriptor 7 0 R/ToUnicode 9 0 R>>");
  b.add_object(7,
               "<</Type/FontDescriptor/FontName/BAAAAA+CprSquare/Flags 4/FontBBox[0 0 900 1400]/ItalicAngle 0/Ascent 800/Descent -200"
               "/CapHeight 700/StemV 80/FontFile2 8 0 R>>");
  b.add_stream(8, fixture("symbolic.ttf"));
  b.add_stream(9,
               "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /CprSquare def 1 begincodespacerange <00> <FF> "
               "endcodespacerange 1 beginbfchar <01> <0041> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
  b.finish_table("/Root 1 0 R");
  Document document = open(b.str());
  const FontLibrary library = library_of("full.ttf");
  auto result = replace_text(document, 0, kLine, "AB", {.fonts = &library});
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->method, TextEditMethod::kSameFont);
  EXPECT_EQ(result->added_glyphs, 1U);
  EXPECT_EQ(page_text(document).plain(), "AB\n");
  auto font = document.get(model::ObjectRef{5, 0});
  ASSERT_TRUE(font.has_value());
  EXPECT_EQ((*font)->as_dict()->find("LastChar")->as_integer(), 2);  // B at code 2
  auto program_now = font::detail::read_sfnt(program(document));
  ASSERT_TRUE(program_now.has_value());
  EXPECT_EQ(font::detail::symbol_cmap(*program_now).at(2), 2);
}

TEST(FontExtend, AnotherVersionRestrictedPermissionsOrNoFontGoToTheSimilarFont) {
  for (const std::string name : {"other.ttf", "restricted.ttf", ""}) {
    SCOPED_TRACE(name);
    Document document = open(page_pdf(true));
    const FontLibrary library = name.empty() ? FontLibrary() : library_of(name);
    auto result = replace_text(document, 0, kLine, "AB", {.fonts = &library});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
    EXPECT_EQ(result->added_glyphs, 0U);
    EXPECT_EQ(result->missing_font, "CprSquare");
    auto loaded = font::detail::FontProgram::open(std::make_shared<const std::vector<std::byte>>(program(document)));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)->glyph_count(), 2U);  // the subset as it was
  }
}

// Only fonts a font given could extend are named as lacking: not a standard font that is not embedded.
TEST(FontExtend, AFontNoFontGivenCouldExtendIsNotNamedAsLacking) {
  const graphics::Rect below{15, 75, 100, 105};
  const FontLibrary library;
  {
    Document document = open(page_pdf(true, "\nBT /F2 20 Tf 20 100 Td (AA) Tj ET"));
    auto result = replace_text(document, 0, below, "A\u00E9", {.fonts = &library});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
    EXPECT_EQ(result->missing_font, "");
  }
  {
    Document document = open(page_pdf(true, "\nBT /F2 20 Tf 20 100 Td (AA) Tj ET"));
    auto result = replace_paragraph(document, 0, below, "A\u00E9", {.fonts = &library});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->method, TextEditMethod::kSimilarFont);
    EXPECT_TRUE(result->missing_fonts.empty());
  }
  {
    Document document = open(page_pdf(true));
    auto result = replace_paragraph(document, 0, kLine, "CB", {.fonts = &library});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->missing_fonts, std::vector<std::string>{"CprSquare"});
  }
}

TEST(FontExtend, AParagraphGetsTheGlyphsAndARefusedEditChangesNothing) {
  const FontLibrary library = library_of("full.ttf");
  {
    Document document = open(page_pdf(true));
    // The paragraph is as wide as "AA": "CB", two glyphs the subset lacks, fits it.
    auto result = replace_paragraph(document, 0, kLine, "CB", {.fonts = &library});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->method, TextEditMethod::kSameFont);
    EXPECT_EQ(result->added_glyphs, 2U);
    EXPECT_TRUE(result->missing_fonts.empty());
    EXPECT_EQ(page_text(document).plain(), "CB\n");
  }
  {
    // No room below (text right under it): refused — and the font is as it was.
    Document document = open(page_pdf(true, "\nBT /F2 20 Tf 20 120 Td (Below) Tj ET"));
    auto refused = replace_paragraph(document, 0, kLine, "ABBA CAB ABBA CAB ABBA CAB ABBA CAB ABBA CAB", {.fonts = &library});
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("no room"), std::string::npos) << refused.error().message;
    auto loaded = font::detail::FontProgram::open(std::make_shared<const std::vector<std::byte>>(program(document)));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)->glyph_count(), 2U);
  }
}

}  // namespace
}  // namespace capriole::edit
