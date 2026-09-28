#include "capriole/filters/ccitt.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/filters/decode.hpp"

namespace capriole::filters {
namespace {

std::vector<std::byte> fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/ccitt/" + name, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(text.empty()) << name;
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

// The pixels of a P4 file (1 = black), header dropped.
std::vector<std::byte> pixels(const std::string& name) {
  std::vector<std::byte> pbm = fixture(name);
  std::size_t newlines = 0;
  std::size_t at = 0;
  while (at < pbm.size() && newlines < 2) {
    newlines += pbm[at++] == std::byte{0x0A} ? 1U : 0U;
  }
  return {pbm.begin() + static_cast<std::ptrdiff_t>(at), pbm.end()};
}

constexpr std::size_t kMax = std::size_t{1} << 24;

CcittParams params(std::int64_t k, bool end_of_line) {
  CcittParams p;
  p.k = k;
  p.end_of_line = end_of_line;
  p.columns = 300;
  p.rows = 97;
  p.black_is_1 = true;  // as the P4 reference
  return p;
}

// The first differing byte, for a readable failure.
std::string first_difference(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    if (a[i] != b[i]) {
      return "row " + std::to_string(i / 38) + ", byte " + std::to_string(i % 38);
    }
  }
  return a.size() == b.size() ? "none" : "sizes " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
}

TEST(Ccitt, MatchesLibtiff) {
  struct Case {
    const char* name;
    std::int64_t k;
    bool end_of_line;
  };
  for (const Case& c : {Case{"g4", -1, false}, Case{"g3_1d", 0, true}, Case{"g3_2d", 4, true}, Case{"g3_fill", 0, true}, Case{"mh", 0, false}}) {
    CcittParams p = params(c.k, c.end_of_line);
    p.encoded_byte_align = std::string(c.name) == "mh";  // Modified Huffman: rows start on byte boundaries
    auto decoded = ccitt_decode(fixture(std::string(c.name) + ".fax"), p, kMax);
    ASSERT_TRUE(decoded.has_value()) << c.name << ": " << decoded.error().message;
    const std::vector<std::byte> expected = pixels(std::string(c.name) + ".pbm");
    EXPECT_EQ(*decoded, expected) << c.name << ": first difference at " << first_difference(*decoded, expected);
  }
}

TEST(Ccitt, EolsAreToleratedWithoutEndOfLineAndRowsCanBeUnknown) {
  // Group 3 data with EOLs decoded without /EndOfLine and without /Rows: the rows the data holds.
  CcittParams p = params(0, false);
  p.rows = 0;
  auto decoded = ccitt_decode(fixture("g3_1d.fax"), p, kMax);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_EQ(*decoded, pixels("g3_1d.pbm"));
}

TEST(Ccitt, BlackIsZeroByDefault) {
  CcittParams p = params(-1, false);
  p.black_is_1 = false;
  auto decoded = ccitt_decode(fixture("g4.fax"), p, kMax);
  ASSERT_TRUE(decoded.has_value());
  const std::vector<std::byte> expected = pixels("g4.pbm");
  ASSERT_EQ(decoded->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    // The padding bits of each row (300 is not a multiple of 8) are white in both.
    const bool last_byte = i % 38 == 37;
    const auto mask = last_byte ? std::byte{0xF0} : std::byte{0xFF};
    ASSERT_EQ(((*decoded)[i] ^ std::byte{0xFF}) & mask, expected[i] & mask) << i;
  }
}

TEST(Ccitt, DamagedDataKeepsTheRowsDecoded) {
  std::vector<std::byte> data = fixture("g4.fax");
  data.resize(data.size() / 2);
  data.insert(data.end(), 64, std::byte{0x00});  // then garbage (no valid code)
  auto decoded = ccitt_decode(data, params(-1, false), kMax);
  ASSERT_TRUE(decoded.has_value());
  const std::vector<std::byte> expected = pixels("g4.pbm");
  ASSERT_EQ(decoded->size(), expected.size());  // /Rows known: the missing rows are white
  EXPECT_EQ((*decoded)[0], expected[0]);
  EXPECT_EQ(decoded->back(), std::byte{0x00});  // white with BlackIs1
}

TEST(Ccitt, ParamsAndLimits) {
  model::Dict parms;
  parms.set("K", model::Object(std::int64_t{-1}));
  parms.set("Columns", model::Object(std::int64_t{300}));
  parms.set("Rows", model::Object(std::int64_t{97}));
  parms.set("BlackIs1", model::Object(true));
  auto p = ccitt_params(&parms);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->k, -1);
  EXPECT_TRUE(p->black_is_1);
  parms.set("Columns", model::Object(std::int64_t{0}));
  EXPECT_FALSE(ccitt_params(&parms).has_value());
  auto limited = ccitt_decode(fixture("g4.fax"), params(-1, false), 100);
  ASSERT_FALSE(limited.has_value());
  EXPECT_EQ(limited.error().code, ErrorCode::kLimitExceeded);
  // Through the filter chain, with /DecodeParms.
  model::Dict dict;
  dict.set("Filter", model::Object(model::Name{"CCITTFaxDecode"}));
  model::Dict chain_parms;
  chain_parms.set("K", model::Object(std::int64_t{-1}));
  chain_parms.set("Columns", model::Object(std::int64_t{300}));
  chain_parms.set("Rows", model::Object(std::int64_t{97}));
  dict.set("DecodeParms", model::Object(std::move(chain_parms)));
  auto chained = decode(fixture("g4.fax"), dict, kDefaultParseLimits);
  ASSERT_TRUE(chained.has_value()) << chained.error().message;
  EXPECT_EQ(chained->size(), 38U * 97U);
}

}  // namespace
}  // namespace capriole::filters
