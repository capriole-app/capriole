#include "capriole/filters/dct.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/filters/decode.hpp"

namespace capriole::filters {
namespace {

// A fixture of tests/data/jpeg (made by make_fixtures.py there).
std::vector<std::byte> fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/jpeg/" + name, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  EXPECT_FALSE(out.empty()) << name;
  return out;
}

// Sample (x, y) of a 16-wide image with `components` per pixel.
std::vector<int> sample(const std::vector<std::byte>& data, int x, int y, int components) {
  std::vector<int> out;
  for (int c = 0; c < components; ++c) {
    out.push_back(std::to_integer<int>(data.at(static_cast<std::size_t>((((y * 16) + x) * components) + c))));
  }
  return out;
}

void expect_near(const std::vector<int>& actual, const std::vector<int>& expected, int tolerance = 6) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    EXPECT_LE(std::abs(actual[i] - expected[i]), tolerance) << "component " << i << ": " << actual[i] << " vs " << expected[i];
  }
}

constexpr std::size_t kMax = std::size_t{1} << 24;

TEST(Dct, GrayRgbAndProgressive) {
  auto gray = dct_decode(fixture("gray.jpg"), nullptr, kMax);
  ASSERT_TRUE(gray.has_value()) << gray.error().message;
  ASSERT_EQ(gray->size(), 16U * 8U);
  expect_near(sample(*gray, 2, 2, 1), {0x40});
  expect_near(sample(*gray, 13, 5, 1), {0xC0});
  for (const char* name : {"rgb.jpg", "progressive.jpg"}) {
    auto rgb = dct_decode(fixture(name), nullptr, kMax);
    ASSERT_TRUE(rgb.has_value()) << name << ": " << rgb.error().message;
    ASSERT_EQ(rgb->size(), 16U * 8U * 3U) << name;
    expect_near(sample(*rgb, 2, 2, 3), {255, 0, 0});  // YCbCr converted to RGB (JFIF)
    expect_near(sample(*rgb, 13, 5, 3), {0, 0, 255});
  }
}

TEST(Dct, ColorTransformZeroKeepsTheComponents) {
  // /ColorTransform 0 on YCbCr data: the components come out as stored, not converted to RGB.
  model::Dict parms;
  parms.set("ColorTransform", model::Object(std::int64_t{0}));
  auto raw = dct_decode(fixture("rgb.jpg"), &parms, kMax);
  ASSERT_TRUE(raw.has_value()) << raw.error().message;
  const std::vector<int> red = sample(*raw, 2, 2, 3);
  EXPECT_LT(red[0], 128);  // Y of red is about 76, not 255
  EXPECT_GT(red[2], 200);  // Cr of red is about 255
}

TEST(Dct, CmykAsStored) {
  auto cmyk = dct_decode(fixture("cmyk.jpg"), nullptr, kMax);
  ASSERT_TRUE(cmyk.has_value()) << cmyk.error().message;
  ASSERT_EQ(cmyk->size(), 16U * 8U * 4U);
  // Pillow writes Adobe CMYK inverted (255 = no ink), as Photoshop does; the samples come out as
  // stored — it is the PDF's /Decode array that says how to read them.
  expect_near(sample(*cmyk, 2, 2, 4), {0, 255, 255, 255});
  expect_near(sample(*cmyk, 13, 5, 4), {255, 255, 255, 255});
}

TEST(Dct, TruncatedDataDecodesInPart) {
  auto partial = dct_decode(fixture("truncated.jpg"), nullptr, kMax);
  ASSERT_TRUE(partial.has_value()) << partial.error().message;
  EXPECT_EQ(partial->size(), 16U * 64U * 3U);
  expect_near(sample(*partial, 2, 1, 3), {255, 0, 0});  // the first rows are there
}

TEST(Dct, LimitsAndGarbage) {
  auto big = dct_decode(fixture("rgb.jpg"), nullptr, 100);
  ASSERT_FALSE(big.has_value());
  EXPECT_EQ(big.error().code, ErrorCode::kLimitExceeded);
  const std::vector<std::byte> garbage(64, std::byte{0x42});
  auto broken = dct_decode(garbage, nullptr, kMax);
  ASSERT_FALSE(broken.has_value());
  EXPECT_EQ(broken.error().code, ErrorCode::kCorruptData);
  EXPECT_FALSE(dct_decode({}, nullptr, kMax).has_value());
}

TEST(Dct, ThroughTheFilterChain) {
  model::Dict dict;
  dict.set("Filter", model::Object(model::Name{"DCTDecode"}));
  auto decoded = decode(fixture("gray.jpg"), dict, kDefaultParseLimits);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_EQ(decoded->size(), 16U * 8U);
}

}  // namespace
}  // namespace capriole::filters
