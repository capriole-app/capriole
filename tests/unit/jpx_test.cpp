#include "capriole/filters/jpx.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace capriole::filters {
namespace {

std::vector<std::byte> fixture(const std::string& name) {
  std::ifstream file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/jpx/" + name, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(text.empty()) << name;
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::vector<std::uint8_t> raw(const std::string& name) {
  std::vector<std::uint8_t> out;
  for (const std::byte b : fixture(name)) {
    out.push_back(std::to_integer<std::uint8_t>(b));
  }
  return out;
}

constexpr std::int64_t kMax = 16'000'000;

TEST(Jpx, DecodesLosslessImagesExactly) {
  // Fixtures from tests/data/jpx/make_fixtures.py (reversible wavelet: the pixels are exact).
  struct Case {
    const char* file;
    const char* pixels;
    std::int32_t components;
    JpxImage::Space space;
    bool alpha;
  };
  for (const Case& c : {Case{"rgb.jp2", "rgb.raw", 3, JpxImage::Space::kRgb, false}, Case{"gray.jp2", "gray.raw", 1, JpxImage::Space::kGray, false},
                        Case{"rgba.jp2", "rgba.raw", 4, JpxImage::Space::kRgb, true}, Case{"rgb.j2k", "rgb_j2k.raw", 3, JpxImage::Space::kUnknown, false}}) {
    auto image = jpx_decode(fixture(c.file), kMax);
    ASSERT_TRUE(image.has_value()) << c.file << ": " << image.error().message;
    EXPECT_EQ(image->width, 48) << c.file;
    EXPECT_EQ(image->height, 32) << c.file;
    EXPECT_EQ(image->components, c.components) << c.file;
    EXPECT_EQ(image->has_alpha, c.alpha) << c.file;
    if (c.space != JpxImage::Space::kUnknown) {
      EXPECT_EQ(image->space, c.space) << c.file;  // a bare codestream says nothing of its colour space
    }
    EXPECT_EQ(image->samples, raw(c.pixels)) << c.file;
  }
}

TEST(Jpx, LargeImagesAreDecodedAtAReducedResolution) {
  auto full = jpx_decode(fixture("large.jp2"), kMax);
  ASSERT_TRUE(full.has_value()) << full.error().message;
  EXPECT_EQ(full->width, 512);
  EXPECT_EQ(full->reduction, 0U);
  // At most 70,000 pixels: one resolution level dropped (256 x 256 = 65,536), the gradient still there.
  auto reduced = jpx_decode(fixture("large.jp2"), 70'000);
  ASSERT_TRUE(reduced.has_value()) << reduced.error().message;
  EXPECT_EQ(reduced->reduction, 1U);
  EXPECT_EQ(reduced->width, 256);
  EXPECT_EQ(reduced->height, 256);
  const std::uint8_t* corner = &reduced->samples[((255 * 256) + 255) * 3];
  EXPECT_NEAR(corner[0], 255, 8);
  EXPECT_NEAR(corner[1], 255, 8);
  // Not even the smallest resolution fits.
  auto refused = jpx_decode(fixture("large.jp2"), 10);
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error().code, ErrorCode::kLimitExceeded);
}

TEST(Jpx, DamagedDataIsAnError) {
  std::vector<std::byte> cut = fixture("rgb.jp2");
  cut.resize(60);
  EXPECT_FALSE(jpx_decode(cut, kMax).has_value());
  const std::vector<std::byte> garbage(200, std::byte{0x5A});
  EXPECT_FALSE(jpx_decode(garbage, kMax).has_value());
  EXPECT_FALSE(jpx_decode({}, kMax).has_value());
}

}  // namespace
}  // namespace capriole::filters
