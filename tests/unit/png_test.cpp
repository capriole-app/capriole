#include "capriole/render/png.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capriole/filters/flate.hpp"

namespace capriole::render {
namespace {

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t v = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    v = (v << 8U) | std::to_integer<std::uint32_t>(bytes[at + i]);
  }
  return v;
}

std::string type_at(std::span<const std::byte> bytes, std::size_t at) {
  std::string type;
  for (std::size_t i = 0; i < 4; ++i) {
    type += static_cast<char>(bytes[at + i]);
  }
  return type;
}

TEST(Png, WritesChunksThatDecodeToThePixels) {
  Bitmap bitmap{.width = 3, .height = 2, .rgba = {}};
  for (std::uint8_t i = 0; i < 24; ++i) {
    bitmap.rgba.push_back(static_cast<std::uint8_t>(i * 10));
  }
  auto png = encode_png(bitmap);
  ASSERT_TRUE(png.has_value());
  const std::span<const std::byte> bytes(*png);
  ASSERT_GT(bytes.size(), 8U);
  EXPECT_EQ(std::to_integer<int>(bytes[0]), 0x89);
  EXPECT_EQ(type_at(bytes, 1), "PNG\r");

  // Walk the chunks: IHDR first, IDAT data concatenated, IEND last.
  std::vector<std::string> types;
  std::vector<std::byte> idat;
  std::size_t at = 8;
  while (at + 12 <= bytes.size()) {
    const std::uint32_t length = read_u32(bytes, at);
    types.push_back(type_at(bytes, at + 4));
    if (types.back() == "IHDR") {
      EXPECT_EQ(length, 13U);
      EXPECT_EQ(read_u32(bytes, at + 8), 3U);
      EXPECT_EQ(read_u32(bytes, at + 12), 2U);
      EXPECT_EQ(std::to_integer<int>(bytes[at + 16]), 8);  // bit depth
      EXPECT_EQ(std::to_integer<int>(bytes[at + 17]), 6);  // RGBA
    }
    if (types.back() == "IDAT") {
      const auto data = bytes.subspan(at + 8, length);
      idat.insert(idat.end(), data.begin(), data.end());
    }
    at += 12 + length;
  }
  EXPECT_EQ(at, bytes.size());
  ASSERT_EQ(types.size(), 3U);
  EXPECT_EQ(types.front(), "IHDR");
  EXPECT_EQ(types.back(), "IEND");

  // The same PNG row filters as FlateDecode's predictors: undoing them gives the pixels back.
  auto filtered = filters::flate_decode(idat, 1024);
  ASSERT_TRUE(filtered.has_value());
  auto pixels = filters::apply_predictor(*filtered, {.predictor = 12, .colors = 4, .bits_per_component = 8, .columns = 3});
  ASSERT_TRUE(pixels.has_value());
  ASSERT_EQ(pixels->size(), bitmap.rgba.size());
  for (std::size_t i = 0; i < pixels->size(); ++i) {
    EXPECT_EQ(std::to_integer<std::uint8_t>((*pixels)[i]), bitmap.rgba[i]) << i;
  }
}

TEST(Png, RejectsABitmapWhoseSizeDoesNotMatch) {
  EXPECT_FALSE(encode_png(Bitmap{}).has_value());
  EXPECT_FALSE(encode_png(Bitmap{.width = 2, .height = 2, .rgba = std::vector<std::uint8_t>(12)}).has_value());
}

}  // namespace
}  // namespace capriole::render
