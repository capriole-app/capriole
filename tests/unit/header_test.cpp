#include "capriole/parser/header.hpp"

#include <gtest/gtest.h>

#include <string>

namespace capriole::parser {
namespace {

Result<PdfHeader> parse(const std::string& text) {
  auto source = MemoryByteSource::from_string(text);
  return find_header(source);
}

TEST(FindHeader, StandardHeader) {
  const auto header = parse("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n1 0 obj");
  ASSERT_TRUE(header.has_value()) << header.error().message;
  EXPECT_EQ(header->major, 1);
  EXPECT_EQ(header->minor, 7);
  EXPECT_EQ(header->offset, 0U);
}

TEST(FindHeader, Pdf20) {
  const auto header = parse("%PDF-2.0\r\n");
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->major, 2);
  EXPECT_EQ(header->minor, 0);
}

TEST(FindHeader, HeaderAtEndOfFile) {
  const auto header = parse("%PDF-1.4");
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->minor, 4);
}

TEST(FindHeader, LeadingJunkIsToleratedAndReported) {
  const auto header = parse("From: someone\r\n\r\n%PDF-1.5\n");
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->minor, 5);
  EXPECT_EQ(header->offset, 17U);
}

TEST(FindHeader, HeaderAtEdgeOfSearchWindow) {
  const auto header = parse(std::string(kHeaderSearchWindow, ' ') + "%PDF-1.3\n");
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->offset, kHeaderSearchWindow);
}

TEST(FindHeader, HeaderBeyondSearchWindowIsRejected) {
  const auto header = parse(std::string(kHeaderSearchWindow + 1, ' ') + "%PDF-1.3\n");
  ASSERT_FALSE(header.has_value());
  EXPECT_EQ(header.error().code, ErrorCode::kSyntax);
}

TEST(FindHeader, NotAPdf) {
  const auto header = parse("GIF89a");
  ASSERT_FALSE(header.has_value());
  EXPECT_EQ(header.error().code, ErrorCode::kSyntax);
}

TEST(FindHeader, EmptyInput) {
  const auto header = parse("");
  ASSERT_FALSE(header.has_value());
  EXPECT_EQ(header.error().code, ErrorCode::kSyntax);
}

TEST(FindHeader, MalformedVersions) {
  for (const char* text : {"%PDF-", "%PDF-1", "%PDF-1.", "%PDF-.7", "%PDF-a.b", "%PDF-1,7"}) {
    const auto header = parse(text);
    EXPECT_FALSE(header.has_value()) << text;
  }
}

}  // namespace
}  // namespace capriole::parser
