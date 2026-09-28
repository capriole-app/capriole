#include "capriole/filters/decode.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "support/pdf_builder.hpp"

namespace capriole::filters {
namespace {

using model::Array;
using model::Dict;
using model::Name;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    out[i] = static_cast<std::byte>(text[i]);
  }
  return out;
}

Dict with_filter(model::Object filter) {
  Dict dict;
  dict.set("Filter", std::move(filter));
  return dict;
}

TEST(Decode, NoFilterReturnsDataAsIs) {
  const auto out = decode(bytes("raw"), Dict{}, kDefaultParseLimits);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, bytes("raw"));
}

TEST(Decode, FlateDecode) {
  const auto out = decode(bytes(test::zlib_compress("content")), with_filter(Name{"FlateDecode"}), kDefaultParseLimits);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, bytes("content"));
}

TEST(Decode, FilterChainRunsInOrder) {
  const std::string twice = test::zlib_compress(test::zlib_compress("layers"));
  const auto out = decode(bytes(twice), with_filter(Array{Name{"FlateDecode"}, Name{"FlateDecode"}}), kDefaultParseLimits);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, bytes("layers"));
}

TEST(Decode, DecodeParmsApplyThePredictor) {
  Dict dict = with_filter(Name{"FlateDecode"});
  Dict parms;
  parms.set("Predictor", 12);
  parms.set("Columns", 2);
  dict.set("DecodeParms", parms);
  const std::string rows = std::string("\x02\x01\x02", 3) + std::string("\x02\x01\x01", 3);  // Up, Up
  const auto out = decode(bytes(test::zlib_compress(rows)), dict, kDefaultParseLimits);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, bytes(std::string("\x01\x02\x02\x03", 4)));
}

TEST(Decode, AllBasicFiltersChained) {
  // RunLength("xxxx") -> ASCII85 "rHNh~>" -> ASCIIHex; decoding runs the filters in the listed order.
  const auto out =
      decode(bytes("72484E687E3E>"), with_filter(Array{Name{"ASCIIHexDecode"}, Name{"ASCII85Decode"}, Name{"RunLengthDecode"}}), kDefaultParseLimits);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, bytes("xxxx"));
}

TEST(Decode, LzwWithEarlyChangeParameter) {
  Dict dict = with_filter(Name{"LZWDecode"});
  Dict parms;
  parms.set("EarlyChange", 2);
  dict.set("DecodeParms", parms);
  EXPECT_EQ(decode(bytes("x"), dict, kDefaultParseLimits).error().code, ErrorCode::kSyntax);
}

TEST(Decode, DecodeParmsArrayWithNull) {
  Dict dict = with_filter(Array{Name{"FlateDecode"}});
  dict.set("DecodeParms", Array{model::Null{}});
  EXPECT_TRUE(decode(bytes(test::zlib_compress("x")), dict, kDefaultParseLimits).has_value());
}

TEST(Decode, Errors) {
  const auto unsupported = decode(bytes("\xFF\xD8"), with_filter(Name{"JPXDecode"}), kDefaultParseLimits);
  ASSERT_FALSE(unsupported.has_value());
  EXPECT_EQ(unsupported.error().code, ErrorCode::kUnsupported);
  EXPECT_NE(unsupported.error().message.find("JPXDecode"), std::string::npos);

  EXPECT_EQ(decode(bytes("x"), with_filter(5), kDefaultParseLimits).error().code, ErrorCode::kSyntax);
  EXPECT_EQ(decode(bytes("x"), with_filter(Array{5}), kDefaultParseLimits).error().code, ErrorCode::kSyntax);

  Dict mismatch = with_filter(Array{Name{"FlateDecode"}, Name{"FlateDecode"}});
  mismatch.set("DecodeParms", Array{model::Null{}});
  EXPECT_EQ(decode(bytes("x"), mismatch, kDefaultParseLimits).error().code, ErrorCode::kSyntax);

  Dict external;
  external.set("F", model::String{.bytes = "data.bin"});
  EXPECT_EQ(decode(bytes("x"), external, kDefaultParseLimits).error().code, ErrorCode::kUnsupported);
}

TEST(ReadEncoded, ReadsTheStreamSpan) {
  auto source = MemoryByteSource::from_string("0123456789");
  const auto data = read_encoded(source, model::Stream{.dict = {}, .data_offset = 3, .data_length = 4, .memory_data = nullptr, .origin = std::nullopt});
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(*data, bytes("3456"));
  EXPECT_EQ(read_encoded(source, model::Stream{.dict = {}, .data_offset = 8, .data_length = 4, .memory_data = nullptr, .origin = std::nullopt}).error().code,
            ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace capriole::filters
