#include "capriole/filters/jbig2.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/filters/decode.hpp"
#include "filters/jbig2_huffman.hpp"

namespace capriole::filters {
namespace {

std::vector<std::byte> fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/jbig2/" + name, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(text.empty()) << name;
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

// The pixels of a P4 file (1 = black) turned into the filter's convention (0 = black).
std::vector<std::byte> expected_page(const std::string& name) {
  std::vector<std::byte> pbm = fixture(name);
  std::size_t newlines = 0;
  std::size_t at = 0;
  while (at < pbm.size() && newlines < 2) {
    newlines += pbm[at++] == std::byte{0x0A} ? 1U : 0U;
  }
  std::vector<std::byte> out(pbm.begin() + static_cast<std::ptrdiff_t>(at), pbm.end());
  for (std::byte& b : out) {
    b = ~b;
  }
  return out;
}

// Equal but for the padding bits of each row, which the reference leaves 0 (black here, once inverted).
void expect_same_page(const std::vector<std::byte>& actual, const std::vector<std::byte>& expected, std::size_t width, const std::string& name) {
  ASSERT_EQ(actual.size(), expected.size()) << name;
  const std::size_t row = (width + 7) / 8;
  const auto pad = static_cast<unsigned>((row * 8) - width);
  for (std::size_t i = 0; i < actual.size(); ++i) {
    const bool last = i % row == row - 1;
    const auto mask = static_cast<std::byte>(last ? (0xFFU << pad) & 0xFFU : 0xFFU);
    ASSERT_EQ(actual[i] & mask, expected[i] & mask) << name << ": row " << i / row << ", byte " << i % row;
  }
}

struct Case {
  const char* name;
  std::size_t width;
};

constexpr std::size_t kMax = std::size_t{1} << 24;

TEST(Jbig2, MatchesJbig2decOnEveryCodingPath) {
  // Streams from tests/data/jbig2/make_fixtures.py, each checked with jbig2dec when it was written.
  for (const Case& c :
       {Case{"generic_t1", 37}, Case{"generic_t2", 37}, Case{"generic_t3", 37}, Case{"generic_t0_at_tpgdon", 40}, Case{"generic_mmr", 45},
        Case{"refine_page", 30}, Case{"refine_page_t1", 30}, Case{"text_huffman", 30}, Case{"text_refine_transposed", 24}, Case{"halftone", 24}}) {
    auto decoded = jbig2_decode(fixture(std::string(c.name) + ".jb2"), {}, kMax);
    ASSERT_TRUE(decoded.has_value()) << c.name << ": " << decoded.error().message;
    expect_same_page(*decoded, expected_page(std::string(c.name) + ".pbm"), c.width, c.name);
  }
}

TEST(Jbig2, IntermediateRegionRefinedWithTpgron) {
  // Beyond what jbig2dec decodes (see make_fixtures.py): the standard's reading, as pdfium's.
  auto decoded = jbig2_decode(fixture("unchecked_refine_intermediate_t1_tpgron.jb2"), {}, kMax);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  expect_same_page(*decoded, expected_page("unchecked_refine_intermediate_t1_tpgron.pbm"), 40, "intermediate");
}

TEST(Jbig2, GlobalsThroughTheFilterChain) {
  // The same stream split: the symbol dictionary as globals (a string in /DecodeParms, as
  // Document::decode_stream passes it), the page and the text region in the stream.
  const std::vector<std::byte> whole = fixture("text_huffman.jb2");
  // Segments: page information (19 + 11 bytes header), then the dictionary (segment 1), then the text.
  const auto length = [&](std::size_t header_at, std::size_t referred) {
    std::size_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      v = (v << 8U) | std::to_integer<std::size_t>(whole[header_at + 7 + referred + i]);
    }
    return 11 + referred + v;
  };
  const std::size_t page_end = length(0, 0);
  const std::size_t dictionary_end = page_end + length(page_end, 0);
  const std::vector<std::byte> globals(whole.begin() + static_cast<std::ptrdiff_t>(page_end), whole.begin() + static_cast<std::ptrdiff_t>(dictionary_end));
  std::vector<std::byte> page(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(page_end));
  page.insert(page.end(), whole.begin() + static_cast<std::ptrdiff_t>(dictionary_end), whole.end());

  model::Dict parms;
  parms.set("JBIG2Globals", model::Object(model::String{.bytes = std::string(reinterpret_cast<const char*>(globals.data()),
                                                                             globals.size()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                                                        .form = model::StringForm::kHex}));
  model::Dict dict;
  dict.set("Filter", model::Object(model::Name{"JBIG2Decode"}));
  dict.set("DecodeParms", model::Object(std::move(parms)));
  auto decoded = decode(page, dict, kDefaultParseLimits);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  expect_same_page(*decoded, expected_page("text_huffman.pbm"), 30, "globals");
  // Without the globals, the text region has no symbols: a blank page, not a failure.
  auto without = jbig2_decode(page, {}, kMax);
  ASSERT_TRUE(without.has_value());
  EXPECT_EQ((*without)[0], std::byte{0xFF});
}

TEST(Jbig2, LimitsAndGarbage) {
  auto limited = jbig2_decode(fixture("generic_t1.jb2"), {}, 10);
  ASSERT_FALSE(limited.has_value());
  EXPECT_EQ(limited.error().code, ErrorCode::kLimitExceeded);
  const std::vector<std::byte> garbage(100, std::byte{0x42});
  EXPECT_FALSE(jbig2_decode(garbage, {}, kMax).has_value());  // no page information
  EXPECT_FALSE(jbig2_decode({}, {}, kMax).has_value());
  // Truncated: the page as far as it was drawn.
  std::vector<std::byte> cut = fixture("generic_t2.jb2");
  cut.resize(cut.size() - 20);
  EXPECT_TRUE(jbig2_decode(cut, {}, kMax).has_value());
}

TEST(Jbig2, StandardHuffmanTablesHaveTheirCodes) {
  // B.3 assigns the codes from the prefix lengths: a few of the codes the standard lists.
  const auto& b1 = jbig2::standard_table(1);
  EXPECT_EQ(b1.codes(), (std::vector<std::uint32_t>{0x0, 0x2, 0x6, 0x7}));
  const auto& b8 = jbig2::standard_table(8);
  EXPECT_EQ(b8.codes().front(), 0xFCU);  // -15..-8: 11111100
  EXPECT_EQ(b8.codes().back(), 0x1U);    // OOB: 01
  const auto& b15 = jbig2::standard_table(15);
  EXPECT_EQ(b15.codes()[b15.codes().size() - 2], 0x7EU);  // lower range
  // Decoding a value: B.2 "3" is 1110 then 3 bits (0) — 1110000.
  const std::vector<std::byte> bits = {std::byte{0xE0}};
  jbig2::BitReader in(bits);
  const auto v = jbig2::standard_table(2).decode(in);
  EXPECT_TRUE(v.ok);
  EXPECT_EQ(v.value, 3);
}

}  // namespace
}  // namespace capriole::filters
