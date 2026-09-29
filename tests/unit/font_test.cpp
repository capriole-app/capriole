// Fonts (font/): glyph names and encodings, CMaps, FreeType programs, the standard substitutes, and PDF
// fonts of every kind as the interpreter uses them. Fixture programs: tests/data/fonts/make_fixtures.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/render/raster.hpp"
#include "font/cmap.hpp"
#include "font/glyph_tables.hpp"
#include "font/program.hpp"
#include "font/standard_fonts.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::font {
namespace {

using detail::BaseEncoding;
using detail::CMap;
using detail::FontProgram;
using detail::StandardFont;
using graphics::DisplayList;

std::string fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/fonts/" + name, std::ios::binary);
  EXPECT_TRUE(file.good()) << name;
  return {std::istreambuf_iterator<char>(file), {}};
}

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::unique_ptr<FontProgram> program(const std::string& data) {
  auto opened = FontProgram::open(std::make_shared<const std::vector<std::byte>>(bytes(data)));
  EXPECT_TRUE(opened.has_value());
  return opened ? std::move(*opened) : nullptr;
}

graphics::Rect bounds(const graphics::Path& path) {
  graphics::Rect r{1e9, 1e9, -1e9, -1e9};
  path.for_each([&](graphics::SegmentKind kind, const graphics::Point* p) {
    for (std::size_t i = 0; i < graphics::Path::points_of(kind); ++i) {
      r = {std::min(r.x0, p[i].x), std::min(r.y0, p[i].y), std::max(r.x1, p[i].x), std::max(r.y1, p[i].y)};
    }
  });
  return r;
}

// ── Glyph names and encodings ───────────────────────────────────────────────────────────────

TEST(GlyphNames, AdobeGlyphListAndUniNames) {
  EXPECT_EQ(detail::glyph_unicode("A"), U'A');
  EXPECT_EQ(detail::glyph_unicode("Euro"), U'€');
  EXPECT_EQ(detail::glyph_unicode("uni20AC"), U'€');
  EXPECT_EQ(detail::glyph_unicode("u1F600"), U'\U0001F600');
  EXPECT_EQ(detail::glyph_unicode("a.sc"), U'a');             // a suffix is dropped
  EXPECT_EQ(detail::glyph_unicode("a1"), U'✁');               // ZapfDingbats names
  EXPECT_EQ(detail::glyph_unicode("uniD800"), std::nullopt);  // a surrogate is not a character
  EXPECT_EQ(detail::glyph_unicode("uni20ac"), std::nullopt);  // lower-case digits are not the AGL's
  EXPECT_EQ(detail::glyph_unicode("g123"), std::nullopt);
}

TEST(GlyphNames, StandardEncodings) {
  const auto win = detail::encoding_table(BaseEncoding::kWinAnsi);
  EXPECT_EQ(win[0x41], "A");
  EXPECT_EQ(win[0x80], "Euro");
  EXPECT_EQ(win[0x81], "bullet");  // unused codes are bullets (Annex D)
  EXPECT_EQ(win[0xA0], "space");
  EXPECT_EQ(win[0xAD], "hyphen");
  EXPECT_EQ(win[0x27], "quotesingle");
  const auto mac = detail::encoding_table(BaseEncoding::kMacRoman);
  EXPECT_EQ(mac[0x80], "Adieresis");
  EXPECT_EQ(mac[0xDB], "currency");
  const auto standard = detail::encoding_table(BaseEncoding::kStandard);
  EXPECT_EQ(standard[0x27], "quoteright");
  EXPECT_EQ(standard[0xE1], "AE");
  EXPECT_EQ(standard[0x80], "");
  EXPECT_EQ(detail::encoding_code(BaseEncoding::kMacRoman, "Adieresis"), 0x80);
  EXPECT_EQ(detail::encoding_code(BaseEncoding::kMacRoman, "nosuchglyph"), std::nullopt);
}

// ── CMaps ─────────────────────────────────────────────────────────────────────────────────────

constexpr std::string_view kMixedCMap = R"(%!PS-Adobe-3.0 Resource-CMap
/CIDInit /ProcSet findresource begin
12 dict begin begincmap
/CMapName /Test-H def
2 begincodespacerange
<00> <80>
<8140> <9FFC>
endcodespacerange
2 begincidrange
<20> <7E> 1
<8140> <817E> 633
endcidrange
1 begincidchar
<8150> 999
endcidchar
1 beginnotdefrange
<9000> <90FF> 7
endnotdefrange
endcmap CMapName currentdict /CMap defineresource pop end end
)";

TEST(CMaps, CodespaceRangesSplitMixedLengths) {
  auto cmap = CMap::parse(kMixedCMap, nullptr);
  ASSERT_TRUE(cmap.has_value()) << cmap.error().message;
  const std::string bytes = "A\x81\x41\x81\x50\x90\x50";
  std::size_t at = 0;
  const auto a = cmap->next(bytes, at);
  EXPECT_EQ(a.length, 1);
  EXPECT_EQ(a.cid, 1U + ('A' - 0x20));
  const auto b = cmap->next(bytes, at);
  EXPECT_EQ(b.length, 2);
  EXPECT_EQ(b.code, 0x8141U);
  EXPECT_EQ(b.cid, 634U);
  EXPECT_EQ(cmap->next(bytes, at).cid, 999U);  // cidchar after the range it overlaps
  EXPECT_EQ(cmap->next(bytes, at).cid, 7U);    // notdef range
  EXPECT_EQ(at, bytes.size());
  EXPECT_FALSE(cmap->vertical());
}

TEST(CMaps, BytesOutsideTheCodespaceTakeTheRangeLengthAndCidZero) {
  auto cmap = CMap::parse(kMixedCMap, nullptr);
  ASSERT_TRUE(cmap.has_value());
  const std::string bytes = "\x85\x20\xF0";  // 0x85 starts a two-byte range but 0x8520 is outside it; 0xF0 fits nothing
  std::size_t at = 0;
  const auto partial = cmap->next(bytes, at);
  EXPECT_EQ(partial.length, 2);
  EXPECT_EQ(partial.cid, 0U);
  const auto stray = cmap->next(bytes, at);
  EXPECT_EQ(stray.length, 1);  // the shortest range
  EXPECT_EQ(at, bytes.size());
}

TEST(CMaps, UseCMapChainsAndVerticalMode) {
  const auto resolve = [](std::string_view name, std::size_t depth) -> std::shared_ptr<const CMap> {
    if (name != "Test-H") {
      return nullptr;
    }
    auto parent = CMap::parse(kMixedCMap, nullptr, {}, depth);
    return parent ? std::make_shared<const CMap>(std::move(*parent)) : nullptr;
  };
  auto cmap = CMap::parse("/WMode 1 def /Test-H usecmap 1 begincidchar <41> 5000 endcidchar", resolve);
  ASSERT_TRUE(cmap.has_value());
  EXPECT_TRUE(cmap->vertical());
  std::size_t at = 0;
  EXPECT_EQ(cmap->next("AB", at).cid, 5000U);              // its own mapping first
  EXPECT_EQ(cmap->next("AB", at).cid, 1U + ('B' - 0x20));  // then the parent's
}

TEST(CMaps, UseCMapCyclesStop) {
  // A CMap that uses itself: the resolver is called with a growing depth and the chain is cut.
  std::size_t calls = 0;
  CMap::Resolver resolve;
  resolve = [&](std::string_view, std::size_t depth) -> std::shared_ptr<const CMap> {
    ++calls;
    auto again = CMap::parse("/Loop usecmap", resolve, {}, depth);
    return again ? std::make_shared<const CMap>(std::move(*again)) : nullptr;
  };
  auto cmap = CMap::parse("/Loop usecmap 1 begincodespacerange <00> <FF> endcodespacerange", resolve);
  EXPECT_TRUE(cmap.has_value());
  EXPECT_LT(calls, 10U);
}

TEST(CMaps, ToUnicodeRangesAndCharacters) {
  auto cmap = CMap::parse(
      "1 begincodespacerange <00> <FF> endcodespacerange 2 beginbfchar <01> <0041> <02> <D83DDE00> endbfchar "
      "2 beginbfrange <10> <12> <0061> <20> <21> [<0066006C> /Euro] endbfrange",
      nullptr);
  ASSERT_TRUE(cmap.has_value());
  EXPECT_EQ(cmap->text(0x01, 1), U"A");
  EXPECT_EQ(cmap->text(0x02, 1), U"\U0001F600");  // a surrogate pair
  EXPECT_EQ(cmap->text(0x12, 1), U"c");           // a range increases its last character
  EXPECT_EQ(cmap->text(0x20, 1), U"fl");          // a ligature
  EXPECT_EQ(cmap->text(0x21, 1), U"€");           // a glyph name
  EXPECT_EQ(cmap->text(0x30, 1), U"");
}

TEST(CMaps, NothingUsableIsAnError) {
  EXPECT_FALSE(CMap::parse("%!PS just a comment", nullptr).has_value());
  EXPECT_FALSE(CMap::parse("", nullptr).has_value());
}

TEST(CMaps, MappingsAreBounded) {
  std::string text = "1 begincodespacerange <0000> <FFFF> endcodespacerange 1000 begincidchar";
  for (int i = 0; i < 1000; ++i) {
    text += std::format(" <{:04X}> {}", i, i);
  }
  text += " endcidchar";
  auto cmap = CMap::parse(text, nullptr, {.max_mappings = 10, .max_depth = 8, .max_text_units = 256});
  ASSERT_TRUE(cmap.has_value());
  std::size_t at = 0;
  EXPECT_EQ(cmap->next(std::string("\x00\x05", 2), at).cid, 5U);
  at = 0;
  EXPECT_EQ(cmap->next(std::string("\x00\x20", 2), at).cid, 0U);  // past the bound: not mapped
}

// ── Font programs ─────────────────────────────────────────────────────────────────────────────

TEST(FontPrograms, EveryFixtureFormat) {
  for (const std::string name : {"square.ttf", "square.cff", "square.pfa"}) {
    auto p = program(fixture(name));
    ASSERT_NE(p, nullptr) << name;
    const std::uint32_t a = p->glyph_named("A");
    ASSERT_NE(a, 0U) << name;
    const auto outline = p->outline(a);
    ASSERT_TRUE(outline.has_value()) << name;
    const graphics::Rect box = bounds(*outline);
    EXPECT_NEAR(box.x0, 100, 0.2) << name;  // glyph space, 1000 units per em (square.ttf has 2048)
    EXPECT_NEAR(box.x1, 600, 0.2) << name;
    EXPECT_NEAR(box.y1, 500, 0.2) << name;
    EXPECT_NEAR(p->advance(a).value_or(0), 700, 0.5) << name;
  }
  auto ttf = program(fixture("square.ttf"));
  EXPECT_TRUE(ttf->is_truetype());
  EXPECT_EQ(ttf->glyph_for(detail::Charmap::kUnicode, 'B'), 2U);
  EXPECT_EQ(ttf->glyph_for(detail::Charmap::kMacRoman, 'A'), 1U);
  // The triangle's apex is a quadratic control point: as a cubic, the curve stays under it.
  const graphics::Rect triangle = bounds(*ttf->outline(2));
  EXPECT_NEAR(triangle.y1, 933.3, 0.5);  // cubic control points at 2/3 of the way to it
}

TEST(FontPrograms, SymbolCmapAndCidKeyedCff) {
  auto symbol = program(fixture("symbol.ttf"));
  EXPECT_FALSE(symbol->has_charmap(detail::Charmap::kUnicode));
  EXPECT_EQ(symbol->glyph_for(detail::Charmap::kMicrosoftSymbol, 0xF042), 2U);
  auto cid = program(fixture("square_cid.cff"));
  EXPECT_TRUE(cid->is_cid_keyed());
  ASSERT_TRUE(cid->outline(200).has_value());  // glyphs are selected by CID
  EXPECT_NEAR(bounds(*cid->outline(200)).y1, 700, 0.1);
  EXPECT_FALSE(cid->outline(2).has_value());
}

TEST(FontPrograms, GarbageAndBudget) {
  EXPECT_FALSE(FontProgram::open(std::make_shared<const std::vector<std::byte>>(bytes("not a font at all"))).has_value());
  EXPECT_FALSE(FontProgram::open(std::make_shared<const std::vector<std::byte>>()).has_value());
  // A budget too small for FreeType itself: refused, not a crash.
  EXPECT_FALSE(FontProgram::open(std::make_shared<const std::vector<std::byte>>(bytes(fixture("square.ttf"))), {.max_memory = 1024}).has_value());
}

TEST(StandardFonts, AllBuiltInAndWithAdobeMetrics) {
  for (int i = 0; i <= static_cast<int>(StandardFont::kDingbats); ++i) {
    const auto data = detail::standard_font_data(static_cast<StandardFont>(i));
    std::vector<std::byte> copy(data.size());
    std::ranges::transform(data, copy.begin(), [](unsigned char c) { return std::byte{c}; });
    auto p = FontProgram::open(std::make_shared<const std::vector<std::byte>>(std::move(copy)));
    ASSERT_TRUE(p.has_value()) << i;
  }
  auto helvetica = program(
      std::string(reinterpret_cast<const char*>(detail::standard_font_data(StandardFont::kSans).data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                  detail::standard_font_data(StandardFont::kSans).size()));
  EXPECT_DOUBLE_EQ(helvetica->advance(helvetica->glyph_named("a")).value_or(0), 556);
  EXPECT_DOUBLE_EQ(helvetica->advance(helvetica->glyph_named("W")).value_or(0), 944);
}

TEST(StandardFonts, NamesAndSubstitutes) {
  EXPECT_EQ(detail::standard_font_named("Helvetica-BoldOblique"), StandardFont::kSansBoldItalic);
  EXPECT_EQ(detail::standard_font_named("ABCDEF+Arial,Bold"), StandardFont::kSansBold);
  EXPECT_EQ(detail::standard_font_named("TimesNewRomanPS-ItalicMT"), StandardFont::kSerifItalic);
  EXPECT_EQ(detail::standard_font_named("CourierNewPSMT"), StandardFont::kFixed);
  EXPECT_EQ(detail::standard_font_named("Symbol"), StandardFont::kSymbol);
  EXPECT_EQ(detail::standard_font_named("ZapfDingbats"), StandardFont::kDingbats);
  EXPECT_EQ(detail::standard_font_named("Calibri"), std::nullopt);
  EXPECT_EQ(detail::substitute_font("Georgia-Bold", 0, 0, 400), StandardFont::kSerifBold);
  EXPECT_EQ(detail::substitute_font("Consolas", 0, 0, 400), StandardFont::kFixed);
  EXPECT_EQ(detail::substitute_font("NotoSans", 2, -12, 700), StandardFont::kSansBoldItalic);  // the name says sans over the Serif flag
  EXPECT_EQ(detail::substitute_font("X", 2, 0, 400), StandardFont::kSerif);                    // the Serif flag
  EXPECT_EQ(detail::without_subset_tag("ABCDEF+Name"), "Name");
  EXPECT_EQ(detail::without_subset_tag("ABCDEf+Name"), "ABCDEf+Name");
}

TEST(Resources, AddFindAndDirectories) {
  Resources resources;
  resources.add("cmap/X", bytes("data"));
  ASSERT_NE(resources.find("cmap/X"), nullptr);
  EXPECT_EQ(resources.find("cmap/X")->size(), 4U);
  EXPECT_EQ(resources.find("cmap/Y"), nullptr);
  auto loaded = Resources::load_directory(std::string(CAPRIOLE_TEST_DATA_DIR) + "/fonts");
  ASSERT_TRUE(loaded.has_value());
  EXPECT_NE(loaded->find("square.ttf"), nullptr);
  EXPECT_FALSE(Resources::load_directory("/nonexistent/capriole").has_value());
}

// ── PDF fonts, through the interpreter ────────────────────────────────────────────────────────

struct Page {
  std::string content;
  std::string resources;
  std::vector<std::pair<std::uint32_t, std::string>> objects = {};
};

std::string stream_object(const std::string& dict, const std::string& data) {
  return "<<" + dict + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream";
}

DisplayList interpret(const Page& page, const Resources* resources = nullptr) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 200 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<<" + page.resources + ">>>>");
  b.add_stream(4, page.content);
  for (const auto& [number, body] : page.objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 40/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = content::interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0}, {}, resources);
  EXPECT_TRUE(list.has_value()) << list.error().message;
  return std::move(*list);
}

template <typename T>
std::vector<T> all(const DisplayList& list) {
  std::vector<T> out;
  for (const auto& command : list.commands) {
    if (const T* c = graphics::command_as<T>(command)) {
      out.push_back(*c);
    }
  }
  return out;
}

// The glyph's box in text space for size 1 (glyph space / 1000).
graphics::Rect glyph_box(const graphics::Glyph& glyph) {
  EXPECT_NE(glyph.outline, nullptr);
  return glyph.outline ? bounds(*glyph.outline) : graphics::Rect{};
}

bool is_square(const graphics::Glyph& glyph) { return glyph.outline && std::abs(glyph_box(glyph).y1 - 0.5) < 0.001; }
bool is_triangle(const graphics::Glyph& glyph) { return glyph.outline && glyph_box(glyph).y1 > 0.69; }

bool has_warning(const DisplayList& list, std::string_view part) {
  return std::ranges::any_of(list.warnings, [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

TEST(PdfFonts, TrueTypeByNameAndSymbolic) {
  const auto list = interpret(
      {.content = "BT /F1 10 Tf (AB) Tj /F2 10 Tf (AB) Tj ET",
       .resources = "/Font<</F1 10 0 R/F2 12 0 R>>",
       .objects = {{10, "<</Type/Font/Subtype/TrueType/BaseFont/Sq/FirstChar 65/LastChar 66/Widths[700 700]/Encoding/WinAnsiEncoding/FontDescriptor 11 0 R>>"},
                   {11, "<</Type/FontDescriptor/FontName/Sq/Flags 32/FontFile2 13 0 R>>"},
                   {13, stream_object("", fixture("square.ttf"))},
                   // Symbolic, with /Differences that name other glyphs: the (3,0) cmap by code wins.
                   {12,
                    "<</Type/Font/Subtype/TrueType/BaseFont/Sy/FirstChar 65/LastChar 66/Widths[700 700]/Encoding<</Differences[65/B/A]>>"
                    "/FontDescriptor 14 0 R>>"},
                   {14, "<</Type/FontDescriptor/FontName/Sy/Flags 4/FontFile2 15 0 R>>"},
                   {15, stream_object("", fixture("symbol.ttf"))}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(is_square(runs[0].glyphs[0]));
  EXPECT_TRUE(is_triangle(runs[0].glyphs[1]));
  EXPECT_TRUE(is_square(runs[1].glyphs[0]));
  EXPECT_TRUE(is_triangle(runs[1].glyphs[1]));
  EXPECT_TRUE(list.warnings.empty()) << list.warnings.front();
}

TEST(PdfFonts, Type1AndCffWithDifferences) {
  const auto list = interpret({.content = "BT /F1 10 Tf (ab) Tj /F2 10 Tf (AB) Tj ET",
                               .resources = "/Font<</F1 10 0 R/F2 12 0 R>>",
                               .objects = {{10,
                                            "<</Type/Font/Subtype/Type1/BaseFont/Sq/FirstChar 97/LastChar 98/Widths[700 700]"
                                            "/Encoding<</Differences[97/B/A]>>/FontDescriptor 11 0 R>>"},
                                           {11, "<</Type/FontDescriptor/FontName/Sq/Flags 4/FontFile3 13 0 R>>"},
                                           {13, stream_object("/Subtype/Type1C", fixture("square.cff"))},
                                           // Type 1 without /Encoding: the font's own (standard).
                                           {12, "<</Type/Font/Subtype/Type1/BaseFont/Sq1/FirstChar 65/LastChar 66/Widths[700 700]/FontDescriptor 14 0 R>>"},
                                           {14, "<</Type/FontDescriptor/FontName/Sq1/Flags 32/FontFile 15 0 R>>"},
                                           {15, stream_object("", "junk before the header " + fixture("square.pfa"))}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(is_triangle(runs[0].glyphs[0]));  // a → /B
  EXPECT_TRUE(is_square(runs[0].glyphs[1]));
  EXPECT_TRUE(is_square(runs[1].glyphs[0]));
  EXPECT_TRUE(is_triangle(runs[1].glyphs[1]));
  EXPECT_FALSE(runs[0].latin_codes);  // /Differences: codes are not plain Latin text
}

TEST(PdfFonts, CompositeTrueTypeWithCidToGidMap) {
  // Identity-H: CID = code; CIDToGIDMap sends CID 5 to glyph 2 and CID 6 to glyph 1.
  std::string map(14, '\0');
  map[11] = 2;
  map[13] = 1;
  const auto list = interpret({.content = "BT /F1 10 Tf <00050006> Tj ET",
                               .resources = "/Font<</F1 10 0 R>>",
                               .objects = {{10, "<</Type/Font/Subtype/Type0/BaseFont/Sq/Encoding/Identity-H/DescendantFonts[11 0 R]>>"},
                                           {11,
                                            "<</Type/Font/Subtype/CIDFontType2/BaseFont/Sq/CIDSystemInfo<</Registry(Adobe)/Ordering(Identity)/Supplement 0>>"
                                            "/DW 700/W[6[350]]/CIDToGIDMap 13 0 R/FontDescriptor 12 0 R>>"},
                                           {12, "<</Type/FontDescriptor/FontName/Sq/Flags 4/FontFile2 14 0 R>>"},
                                           {13, stream_object("", map)},
                                           {14, stream_object("", fixture("square.ttf"))}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  ASSERT_EQ(runs[0].glyphs.size(), 2U);
  EXPECT_EQ(runs[0].glyphs[0].cid, 5U);
  EXPECT_TRUE(is_triangle(runs[0].glyphs[0]));
  EXPECT_TRUE(is_square(runs[0].glyphs[1]));
  EXPECT_DOUBLE_EQ(runs[0].glyphs[0].advance, 7);    // /DW
  EXPECT_DOUBLE_EQ(runs[0].glyphs[1].advance, 3.5);  // /W
}

TEST(PdfFonts, CidKeyedCffWithAnEmbeddedCMap) {
  // A one-byte embedded CMap: A → CID 100 (the square), B → CID 200 (the triangle).
  const std::string cmap =
      "/CIDInit /ProcSet findresource begin 12 dict begin begincmap 1 begincodespacerange <00> <FF> endcodespacerange "
      "2 begincidchar <41> 100 <42> 200 endcidchar endcmap end end";
  const auto list = interpret({.content = "BT /F1 10 Tf (BA) Tj ET",
                               .resources = "/Font<</F1 10 0 R>>",
                               .objects = {{10, "<</Type/Font/Subtype/Type0/BaseFont/Sq/Encoding 13 0 R/DescendantFonts[11 0 R]>>"},
                                           {11,
                                            "<</Type/Font/Subtype/CIDFontType0/BaseFont/Sq/CIDSystemInfo<</Registry(Adobe)/Ordering(Identity)/Supplement 0>>"
                                            "/FontDescriptor 12 0 R>>"},
                                           {12, "<</Type/FontDescriptor/FontName/Sq/Flags 4/FontFile3 14 0 R>>"},
                                           {13, stream_object("/Type/CMap", cmap)},
                                           {14, stream_object("/Subtype/CIDFontType0C", fixture("square_cid.cff"))}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  ASSERT_EQ(runs[0].glyphs.size(), 2U);
  EXPECT_EQ(runs[0].glyphs[0].cid, 200U);
  EXPECT_TRUE(is_triangle(runs[0].glyphs[0]));
  EXPECT_TRUE(is_square(runs[0].glyphs[1]));
}

TEST(PdfFonts, VerticalWriting) {
  // Identity-V: glyphs go down by w1 (DW2 [880 -1000] by default), each moved to its vertical origin.
  const auto list = interpret({.content = "BT /F1 10 Tf <00010002> Tj ET",
                               .resources = "/Font<</F1 10 0 R>>",
                               .objects = {{10, "<</Type/Font/Subtype/Type0/BaseFont/Sq/Encoding/Identity-V/DescendantFonts[11 0 R]>>"},
                                           {11,
                                            "<</Type/Font/Subtype/CIDFontType2/BaseFont/Sq/CIDSystemInfo<</Registry(Adobe)/Ordering(Identity)/Supplement 0>>"
                                            "/DW 700/W2[2[-500 350 900]]/FontDescriptor 12 0 R>>"},
                                           {12, "<</Type/FontDescriptor/FontName/Sq/Flags 4/FontFile2 13 0 R>>"},
                                           {13, stream_object("", fixture("square.ttf"))}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  const auto& g = runs[0].glyphs;
  ASSERT_EQ(g.size(), 2U);
  EXPECT_DOUBLE_EQ(g[0].advance, -10);  // DW2
  EXPECT_DOUBLE_EQ(g[1].origin.y, -10);
  EXPECT_DOUBLE_EQ(g[1].advance, -5);  // W2
  // The square (100..600 × 0..500) moved by −(vx, vy) = −(350, 880) from DW2 and w0/2.
  const graphics::Rect box = glyph_box(g[0]);
  EXPECT_NEAR(box.x0, -0.25, 1e-3);
  EXPECT_NEAR(box.y0, -0.88, 1e-3);
}

TEST(PdfFonts, SubstitutesFitTheirGlyphsToTheWidths) {
  // Helvetica is not embedded: the substitute's H (722 wide) is stretched to the 1444 /Widths gives.
  const auto list = interpret({.content = "BT /F1 10 Tf (H) Tj /F2 10 Tf (H) Tj ET",
                               .resources = "/Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica/FirstChar 72/LastChar 72/Widths[1444]>>"
                                            "/F2<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>"});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 2U);
  const graphics::Rect wide = glyph_box(runs[0].glyphs[0]);
  const graphics::Rect plain = glyph_box(runs[1].glyphs[0]);
  EXPECT_NEAR(wide.width(), 2 * plain.width(), 1e-6);
  EXPECT_NEAR(wide.height(), plain.height(), 1e-9);
}

TEST(PdfFonts, Type3GlyphsRunAsContent) {
  // A Type 3 font whose glyph /a is a 1000 × 1000 square in glyph space, with d1: its own colour is ignored.
  const auto list = interpret({.content = "BT 0 0 1 rg /F1 10 Tf 20 30 Td (aa) Tj ET",
                               .resources = "/Font<</F1 10 0 R>>",
                               .objects = {{10,
                                            "<</Type/Font/Subtype/Type3/FontBBox[0 0 1000 1000]/FontMatrix[0.001 0 0 0.001 0 0]"
                                            "/CharProcs<</a 11 0 R>>/Encoding<</Differences[97/a]>>/FirstChar 97/LastChar 97/Widths[1000]>>"},
                                           {11, stream_object("", "1000 0 0 0 1000 1000 d1 1 0 0 rg 0 0 1000 1000 re f")}}});
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_TRUE(runs[0].glyph_procedures);
  const auto fills = all<graphics::FillPath>(list);
  ASSERT_EQ(fills.size(), 2U);
  EXPECT_EQ(fills[0].ctm, (graphics::Matrix{0.01, 0, 0, 0.01, 20, 30}));
  EXPECT_EQ(fills[1].ctm, (graphics::Matrix{0.01, 0, 0, 0.01, 30, 30}));  // advanced by 1000 × 0.001 × 10
  EXPECT_EQ(list.colors[fills[0].color].rgb, (graphics::Rgb{0, 0, 1}));   // the text's colour, not the glyph's
}

TEST(PdfFonts, TextRenderingModes) {
  const std::string font = "/Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>";
  // Mode 7: nothing painted, the glyphs become the clip at ET.
  const auto clip = interpret({.content = "BT 7 Tr /F1 40 Tf 10 10 Td (HI) Tj ET 0 0 200 100 re f", .resources = font});
  ASSERT_EQ(all<graphics::ClipPath>(clip).size(), 1U);
  EXPECT_FALSE(all<graphics::ClipPath>(clip)[0].path.empty());
  // Mode 7 with nothing shown sets no clip.
  EXPECT_TRUE(all<graphics::ClipPath>(interpret({.content = "BT 7 Tr ET", .resources = font})).empty());
  // Drawn: mode 0 fills the glyphs, mode 3 draws nothing, mode 1 only strokes.
  const auto pixels = [&](const std::string& content) {
    auto bitmap = render::rasterize(interpret({.content = content, .resources = font}), 1);
    EXPECT_TRUE(bitmap.has_value());
    return bitmap ? std::ranges::count_if(bitmap->rgba, [](std::uint8_t v) { return v < 128; }) : 0;
  };
  const auto filled = pixels("BT /F1 60 Tf 10 20 Td (HI) Tj ET");
  EXPECT_GT(filled, 300);
  EXPECT_EQ(pixels("BT 3 Tr /F1 60 Tf 10 20 Td (HI) Tj ET"), 0);
  const auto stroked = pixels("BT 1 Tr 0.5 w /F1 60 Tf 10 20 Td (HI) Tj ET");
  EXPECT_GT(stroked, 50);
  EXPECT_LT(stroked, filled);
}

TEST(PdfFonts, CjkSubstitutesComeFromTheResources) {
  // A CMap and a "CJK font" supplied by the host (the fixture square stands in for Noto): the code
  // selects a CID, the collection's UTF-16 CMap gives its character, the font its glyph.
  Resources resources;
  resources.add("cmap/UniJIS-UCS2-H", bytes("1 begincodespacerange <0000> <FFFF> endcodespacerange 1 begincidchar <0041> 34 endcidchar"));
  resources.add("cmap/UniJIS-UTF16-H", bytes("1 begincodespacerange <0000> <FFFF> endcodespacerange 1 begincidchar <0042> 34 endcidchar"));
  resources.add("font/NotoSansJP-Regular.otf", bytes(fixture("square.ttf")));
  const Page page{.content = "BT /F1 10 Tf <0041> Tj ET",
                  .resources =
                      "/Font<</F1<</Type/Font/Subtype/Type0/BaseFont/MS-Mincho/Encoding/UniJIS-UCS2-H/DescendantFonts[<</Type/Font"
                      "/Subtype/CIDFontType0/BaseFont/MS-Mincho/CIDSystemInfo<</Registry(Adobe)/Ordering(Japan1)/Supplement 4>>>>]>>>>"};
  const auto list = interpret(page, &resources);
  EXPECT_TRUE(list.missing_resources.empty());
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_EQ(runs[0].glyphs[0].cid, 34U);
  EXPECT_TRUE(is_triangle(runs[0].glyphs[0]));  // CID 34 is U+0042, the fixture's B
  // Without them: nothing drawn, and what was missing is listed.
  const auto without = interpret(page);
  EXPECT_EQ(without.missing_resources, (std::vector<std::string>{"cmap/UniJIS-UCS2-H", "font/NotoSansJP-Regular.otf"}));
  EXPECT_EQ(all<graphics::ShowText>(without)[0].glyphs[0].outline, nullptr);
}

TEST(PdfFonts, BrokenFontsFallBack) {
  // An embedded program that is not a font: a substitute, with a warning; the text still draws.
  const auto list = interpret({.content = "BT /F1 10 Tf (H) Tj ET",
                               .resources = "/Font<</F1 10 0 R>>",
                               .objects = {{10, "<</Type/Font/Subtype/TrueType/BaseFont/Arial/FirstChar 72/LastChar 72/Widths[722]/FontDescriptor 11 0 R>>"},
                                           {11, "<</Type/FontDescriptor/FontName/Arial/Flags 32/FontFile2 12 0 R>>"},
                                           {12, stream_object("", "garbage garbage garbage")}}});
  EXPECT_TRUE(has_warning(list, "embedded program unusable"));
  const auto runs = all<graphics::ShowText>(list);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_NE(runs[0].glyphs[0].outline, nullptr);
}

}  // namespace
}  // namespace capriole::font
