#include "capriole/filters/flate.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "support/pdf_builder.hpp"

namespace capriole::filters {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    out[i] = static_cast<std::byte>(text[i]);
  }
  return out;
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (const int v : values) {
    out.push_back(static_cast<std::byte>(v));
  }
  return out;
}

constexpr std::size_t kNoLimit = 1U << 30U;

// ── flate_decode ────────────────────────────────────────────────────────────────────────────

TEST(FlateDecode, RoundTrip) {
  const auto decoded = flate_decode(bytes(test::zlib_compress("Hello, PDF")), kNoLimit);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_EQ(*decoded, bytes("Hello, PDF"));
}

TEST(FlateDecode, EmptyPayload) {
  const auto decoded = flate_decode(bytes(test::zlib_compress("")), kNoLimit);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->empty());
}

TEST(FlateDecode, LargeOutputSpansManyChunks) {
  std::string text;
  for (int i = 0; i < 200000; ++i) {
    text += std::to_string(i % 97);
  }
  const auto decoded = flate_decode(bytes(test::zlib_compress(text)), kNoLimit);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, bytes(text));
}

TEST(FlateDecode, TrailingBytesAfterTheStreamAreIgnored) {
  const auto decoded = flate_decode(bytes(test::zlib_compress("abc") + "\r\n"), kNoLimit);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, bytes("abc"));
}

TEST(FlateDecode, CorruptAndTruncatedData) {
  std::string compressed = test::zlib_compress(std::string(1000, 'x') + "tail");
  EXPECT_EQ(flate_decode(bytes(compressed.substr(0, compressed.size() / 2)), kNoLimit).error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(flate_decode({}, kNoLimit).error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(flate_decode(bytes("not zlib at all"), kNoLimit).error().code, ErrorCode::kCorruptData);
  compressed[compressed.size() - 2] = static_cast<char>(compressed[compressed.size() - 2] ^ 0x55);  // break the Adler-32 checksum
  EXPECT_EQ(flate_decode(bytes(compressed), kNoLimit).error().code, ErrorCode::kCorruptData);
}

TEST(FlateDecode, OutputLimitStopsZipBombs) {
  const auto bomb = bytes(test::zlib_compress(std::string(1U << 20U, '\0')));  // 1 MiB of zeros in ~1 KiB
  EXPECT_LT(bomb.size(), 2000U);
  EXPECT_EQ(flate_decode(bomb, 4096).error().code, ErrorCode::kLimitExceeded);
  EXPECT_TRUE(flate_decode(bomb, 1U << 20U).has_value());  // exactly at the limit is fine
}

// ── Predictor parameters ────────────────────────────────────────────────────────────────────

TEST(PredictorParams, DefaultsAndValidation) {
  const auto defaults = predictor_params(nullptr);
  ASSERT_TRUE(defaults.has_value());
  EXPECT_EQ(defaults->predictor, 1);

  const auto make = [](int predictor, int colors, int bpc, int columns) {
    model::Dict dict;
    dict.set("Predictor", predictor);
    dict.set("Colors", colors);
    dict.set("BitsPerComponent", bpc);
    dict.set("Columns", columns);
    return dict;
  };
  const model::Dict good = make(12, 1, 8, 5);
  EXPECT_TRUE(predictor_params(&good).has_value());
  const model::Dict bad_predictor = make(3, 1, 8, 5);
  const model::Dict bad_bpc = make(12, 1, 3, 5);
  const model::Dict bad_colors = make(12, 0, 8, 5);
  EXPECT_EQ(predictor_params(&bad_predictor).error().code, ErrorCode::kSyntax);
  EXPECT_EQ(predictor_params(&bad_bpc).error().code, ErrorCode::kSyntax);
  EXPECT_EQ(predictor_params(&bad_colors).error().code, ErrorCode::kSyntax);
  model::Dict not_integer;
  not_integer.set("Columns", model::Name{"five"});
  EXPECT_EQ(predictor_params(&not_integer).error().code, ErrorCode::kSyntax);
}

// ── PNG predictors (hand-computed rows) ─────────────────────────────────────────────────────

TEST(PngPredictor, AllFilterTypes) {
  const PredictorParams params{.predictor = 12, .colors = 1, .bits_per_component = 8, .columns = 3};
  const auto out = apply_predictor(bytes({0, 1, 2, 3,    // None
                                          1, 1, 1, 1,    // Sub     -> 1 2 3
                                          2, 1, 1, 1,    // Up      -> 2 3 4
                                          3, 0, 0, 0,    // Average -> 1 2 3
                                          4, 0, 0, 0}),  // Paeth   -> 1 2 3
                                   params);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, bytes({1, 2, 3, 1, 2, 3, 2, 3, 4, 1, 2, 3, 1, 2, 3}));
}

TEST(PngPredictor, ArithmeticWrapsModulo256) {
  const PredictorParams params{.predictor = 10, .colors = 1, .bits_per_component = 8, .columns = 2};
  const auto out = apply_predictor(bytes({0, 255, 200, 2, 1, 100}), params);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, bytes({255, 200, 0, 44}));
}

TEST(PngPredictor, LeftNeighbourIsOnePixelAway) {
  // Two 8-bit colours per pixel: "Sub" adds the byte two positions back.
  const PredictorParams params{.predictor = 11, .colors = 2, .bits_per_component = 8, .columns = 2};
  const auto out = apply_predictor(bytes({1, 10, 20, 1, 2}), params);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, bytes({10, 20, 11, 22}));
}

TEST(PngPredictor, BadData) {
  const PredictorParams params{.predictor = 12, .colors = 1, .bits_per_component = 8, .columns = 3};
  EXPECT_EQ(apply_predictor(bytes({5, 1, 2, 3}), params).error().code, ErrorCode::kCorruptData);  // filter type 5
  EXPECT_EQ(apply_predictor(bytes({0, 1, 2}), params).error().code, ErrorCode::kCorruptData);     // partial row
  EXPECT_TRUE(apply_predictor({}, params)->empty());
}

// ── TIFF predictor 2 ────────────────────────────────────────────────────────────────────────

TEST(TiffPredictor, EightAndSixteenBits) {
  const PredictorParams eight{.predictor = 2, .colors = 1, .bits_per_component = 8, .columns = 4};
  EXPECT_EQ(*apply_predictor(bytes({1, 1, 1, 1, 5, 0, 0, 0}), eight), bytes({1, 2, 3, 4, 5, 5, 5, 5}));

  const PredictorParams sixteen{.predictor = 2, .colors = 1, .bits_per_component = 16, .columns = 2};
  EXPECT_EQ(*apply_predictor(bytes({0x00, 0xFF, 0x00, 0x01}), sixteen), bytes({0x00, 0xFF, 0x01, 0x00}));  // carry into the high byte

  const PredictorParams four{.predictor = 2, .colors = 1, .bits_per_component = 4, .columns = 4};
  EXPECT_EQ(apply_predictor(bytes({0, 0}), four).error().code, ErrorCode::kUnsupported);
}

TEST(Predictor, NoneLeavesDataUnchanged) {
  const PredictorParams none{};
  EXPECT_EQ(*apply_predictor(bytes("xyz"), none), bytes("xyz"));
}

}  // namespace
}  // namespace capriole::filters
