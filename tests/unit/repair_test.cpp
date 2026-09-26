#include "capriole/repair/repair.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "capriole/document/document.hpp"
#include "support/pdf_builder.hpp"

namespace capriole {
namespace {

std::unique_ptr<ByteSource> source_of(const std::string& text) {
  std::vector<std::byte> data;
  for (const char ch : text) {
    data.push_back(static_cast<std::byte>(ch));
  }
  return std::make_unique<MemoryByteSource>(std::move(data));
}

std::string two_page_pdf(const std::string& header = "%PDF-1.7\n") {
  test::PdfBuilder b(header);
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 5 0 R>>");
  b.add_object(4, "<</Type/Page/Parent 2 0 R>>");
  b.add_stream(5, "BT (x) Tj ET");
  b.finish_table("/Size 6/Root 1 0 R");
  return b.str();
}

constexpr std::string_view kJunk = "C:\\web\\docs\\publics\\";

// ── Offsets relative to a header preceded by junk ───────────────────────────────────────────

TEST(RepairHeaderOffset, JunkBeforeHeaderWithRelativeOffsetsIsRepaired) {
  // The builder computes offsets from its own start: prepending junk makes them header-relative,
  // exactly what web servers do when they prefix a PDF with a path or HTML.
  const std::string pdf = std::string(kJunk) + two_page_pdf();

  auto strict = Document::open(source_of(pdf), OpenOptions{.limits = kDefaultParseLimits, .password = "", .repair = false});
  ASSERT_FALSE(strict.has_value());

  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  ASSERT_EQ(document->repairs().size(), 1U);
  EXPECT_EQ(document->repairs()[0].kind, repair::RepairKind::kHeaderOffset);
  EXPECT_NE(document->repairs()[0].description.find(std::to_string(kJunk.size())), std::string::npos);

  // Everything reads through the shifted view: objects, page tree, stream data.
  EXPECT_EQ(document->page_refs()->size(), 2U);
  const auto content = document->get(5);
  ASSERT_TRUE(content.has_value());
  const auto data = document->decode_stream(*(*content)->as_stream());
  ASSERT_TRUE(data.has_value()) << data.error().message;
  EXPECT_EQ(data->size(), 12U);
}

TEST(RepairHeaderOffset, JunkBeforeHeaderWithAbsoluteOffsetsNeedsNoRepair) {
  // Here the junk is part of what the offsets count: the file reads strictly as it is.
  const std::string pdf = two_page_pdf(std::string(kJunk) + "%PDF-1.7\n");
  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_TRUE(document->repairs().empty());
  EXPECT_EQ(document->header().offset, kJunk.size());
  EXPECT_EQ(document->page_refs()->size(), 2U);
}

TEST(RepairHeaderOffset, ConformingFilesAreNotRepaired) {
  auto document = Document::open(source_of(two_page_pdf()));
  ASSERT_TRUE(document.has_value());
  EXPECT_TRUE(document->repairs().empty());
}

TEST(RepairHeaderOffset, UnrelatedDamageIsStillReportedAsTheStrictError) {
  // Junk before the header, but the xref is also broken: the repair does not help, and the error
  // reported is the one strict reading found.
  std::string pdf = std::string(kJunk) + two_page_pdf();
  pdf.replace(pdf.find("xref"), 4, "xreg");
  const auto document = Document::open(source_of(pdf));
  ASSERT_FALSE(document.has_value());
  EXPECT_EQ(document.error().code, ErrorCode::kSyntax);
}

// ── When repairs are not attempted ──────────────────────────────────────────────────────────

TEST(RepairPolicy, OnlyStructuralFailuresAreRepairable) {
  EXPECT_TRUE(repair::may_help(Error{.code = ErrorCode::kSyntax, .message = "", .offset = 0}));
  EXPECT_TRUE(repair::may_help(Error{.code = ErrorCode::kOutOfRange, .message = "", .offset = 0}));
  EXPECT_FALSE(repair::may_help(Error{.code = ErrorCode::kPasswordRequired, .message = "", .offset = 0}));
  EXPECT_FALSE(repair::may_help(Error{.code = ErrorCode::kUnsupported, .message = "", .offset = 0}));
  EXPECT_FALSE(repair::may_help(Error{.code = ErrorCode::kIo, .message = "", .offset = 0}));
  EXPECT_FALSE(repair::may_help(Error{.code = ErrorCode::kLimitExceeded, .message = "", .offset = 0}));
}

TEST(RepairPolicy, JunkDetection) {
  auto clean = source_of("%PDF-1.7\n");
  EXPECT_FALSE(repair::junk_before_header(*clean).has_value());
  auto junk = source_of("<html>%PDF-1.7\n");
  EXPECT_EQ(repair::junk_before_header(*junk), 6U);
  auto none = source_of("not a pdf");
  EXPECT_FALSE(repair::junk_before_header(*none).has_value());
}

TEST(OffsetByteSource, ReadsThroughTheView) {
  OffsetByteSource view(source_of("0123456789"), 4);
  EXPECT_EQ(view.size(), 6U);
  std::array<std::byte, 3> buffer{};
  ASSERT_EQ(*view.read(1, buffer), 3U);
  EXPECT_EQ(buffer[0], std::byte{'5'});
  EXPECT_EQ(view.read(7, buffer).error().code, ErrorCode::kOutOfRange);
  auto base = view.release_base();
  EXPECT_EQ(base->size(), 10U);
  EXPECT_EQ(view.size(), 0U);
}

}  // namespace
}  // namespace capriole
