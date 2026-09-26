#include "capriole/document/document.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "support/pdf_builder.hpp"

namespace capriole {
namespace {

using model::Array;
using model::Dict;
using model::Name;
using model::Object;
using model::ObjectRef;
using test::PdfBuilder;

std::unique_ptr<ByteSource> source_of(const std::string& text) {
  std::vector<std::byte> data;
  data.reserve(text.size());
  for (const char ch : text) {
    data.push_back(static_cast<std::byte>(ch));
  }
  return std::make_unique<MemoryByteSource>(std::move(data));
}

Document open_ok(const std::string& pdf) {
  auto document = Document::open(source_of(pdf));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

// Catalog 1, page tree root 2 with two leaves (3, 4) and an intermediate node 5 holding page 6.
std::string three_page_pdf() {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 5 0 R 4 0 R]/Count 3>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R>>");
  b.add_object(4, "<</Type/Page/Parent 2 0 R>>");
  b.add_object(5, "<</Type/Pages/Parent 2 0 R/Kids[6 0 R]/Count 1>>");
  b.add_object(6, "<</Type/Page/Parent 5 0 R>>");
  b.finish_table("/Size 7/Root 1 0 R");
  return b.str();
}

TEST(Document, OpenReadsHeaderXrefAndTrailer) {
  Document document = open_ok(three_page_pdf());
  EXPECT_EQ(document.header().major, 1);
  EXPECT_EQ(document.header().minor, 7);
  EXPECT_EQ(document.xref().sections.size(), 1U);
  EXPECT_EQ(document.trailer().find("Root")->as_ref(), (ObjectRef{.number = 1, .generation = 0}));
  EXPECT_EQ(document.next_object_number(), 7U);
  EXPECT_FALSE(document.has_changes());
}

TEST(Document, OpenFailsWithoutHeaderOrXref) {
  EXPECT_FALSE(Document::open(source_of("not a pdf")).has_value());
  EXPECT_FALSE(Document::open(source_of("%PDF-1.7\n1 0 obj null endobj\n")).has_value());
  EXPECT_FALSE(Document::open(nullptr).has_value());
}

TEST(Document, CrossReferenceStreamKeysAreNotPartOfTheTrailer) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.finish_xref_stream(3, "/Size 4/Root 1 0 R");
  Document document = open_ok(b.str());
  EXPECT_TRUE(document.trailer().contains("Root"));
  EXPECT_TRUE(document.trailer().contains("Size"));
  for (const char* key : {"Type", "W", "Index", "Length", "Filter"}) {
    EXPECT_FALSE(document.trailer().contains(key)) << key;
  }
}

TEST(Document, GetAndResolve) {
  Document document = open_ok(three_page_pdf());
  const auto page = document.get(3);
  ASSERT_TRUE(page.has_value());
  EXPECT_TRUE((*page)->as_dict()->find("Type")->is_name("Page"));
  EXPECT_TRUE((*document.get(99))->is_null());
  EXPECT_TRUE((*document.get(ObjectRef{.number = 3, .generation = 1}))->is_null());
  const auto resolved = document.resolve(Object(ObjectRef{.number = 2, .generation = 0}));
  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(resolved->as_dict()->find("Type")->is_name("Pages"));
  EXPECT_EQ(*document.resolve(Object(7)), Object(7));
  // Reading does not count as a change.
  EXPECT_EQ(document.state(3), ObjectState::kUnmodified);
  EXPECT_TRUE(document.changed_objects().empty());
}

TEST(Document, PageTreeInDocumentOrder) {
  Document document = open_ok(three_page_pdf());
  const auto pages = document.page_refs();
  ASSERT_TRUE(pages.has_value()) << pages.error().message;
  ASSERT_EQ(pages->size(), 3U);
  EXPECT_EQ((*pages)[0].number, 3U);
  EXPECT_EQ((*pages)[1].number, 6U);  // inside intermediate node 5, which comes second
  EXPECT_EQ((*pages)[2].number, 4U);
}

TEST(Document, PageTreeErrors) {
  const auto with_kids = [](const std::string& kids, const std::string& extra_objects_body = "") {
    PdfBuilder b;
    b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
    b.add_object(2, "<</Type/Pages/Kids[" + kids + "]/Count 1>>");
    b.add_object(3, extra_objects_body.empty() ? "<</Type/Page>>" : extra_objects_body);
    b.finish_table("/Size 4/Root 1 0 R");
    return b.str();
  };
  EXPECT_TRUE(open_ok(with_kids("3 0 R")).page_refs().has_value());
  EXPECT_EQ(open_ok(with_kids("2 0 R")).page_refs().error().code, ErrorCode::kSyntax);  // cycle
  EXPECT_EQ(open_ok(with_kids("3")).page_refs().error().code, ErrorCode::kSyntax);      // not a reference
  // A node with a wrong /Type is an error for strict reading (the repair layer takes it as a page).
  auto strict = Document::open(source_of(with_kids("3 0 R", "<</Type/Font>>")), OpenOptions{.limits = kDefaultParseLimits, .password = "", .repair = false});
  EXPECT_EQ(strict->page_refs().error().code, ErrorCode::kSyntax);
  EXPECT_EQ(open_ok(with_kids("3 0 R", "<</Type/Font>>")).page_refs()->size(), 1U);
  EXPECT_EQ(open_ok(with_kids("3 0 R 3 0 R")).page_refs().error().code, ErrorCode::kSyntax);  // same page twice
}

TEST(Document, SetMarksModifiedAndKeepsTheNewValue) {
  Document document = open_ok(three_page_pdf());
  Dict page = *(*document.get(3))->as_dict();
  page.set("Rotate", 90);
  ASSERT_TRUE(document.set(3, page).has_value());
  EXPECT_EQ(document.state(3), ObjectState::kModified);
  EXPECT_EQ((*document.get(3))->as_dict()->find("Rotate")->as_integer(), 90);
  EXPECT_EQ(document.changed_objects(), (std::vector<std::uint32_t>{3}));
  EXPECT_TRUE(document.has_changes());
  // Setting a missing object is an error.
  EXPECT_EQ(document.set(99, Object(1)).error().code, ErrorCode::kOutOfRange);
}

TEST(Document, AddUsesTheNextFreeNumber) {
  Document document = open_ok(three_page_pdf());
  const auto ref = document.add(Object(Name{"Fresh"}));
  ASSERT_TRUE(ref.has_value());
  EXPECT_EQ(ref->number, 7U);
  EXPECT_EQ(ref->generation, 0);
  EXPECT_EQ(document.state(7), ObjectState::kNew);
  EXPECT_TRUE((*document.get(*ref))->is_name("Fresh"));
  EXPECT_EQ(document.add(Object(1))->number, 8U);
  // A new object stays New when changed again.
  ASSERT_TRUE(document.set(7, Object(2)).has_value());
  EXPECT_EQ(document.state(7), ObjectState::kNew);
}

TEST(Document, RemoveDeletesOrForgets) {
  Document document = open_ok(three_page_pdf());
  ASSERT_TRUE(document.remove(4).has_value());
  EXPECT_EQ(document.state(4), ObjectState::kDeleted);
  EXPECT_TRUE((*document.get(4))->is_null());
  EXPECT_EQ(document.remove(4).error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(document.set(4, Object(1)).error().code, ErrorCode::kOutOfRange);

  const auto added = document.add(Object(1));
  ASSERT_TRUE(document.remove(added->number).has_value());
  EXPECT_EQ(document.state(added->number), ObjectState::kUnmodified);  // never existed as far as a save is concerned
  EXPECT_EQ(document.changed_objects(), (std::vector<std::uint32_t>{4}));
  EXPECT_EQ(document.next_object_number(), added->number + 1);  // numbers are not reused
}

TEST(Document, TrailerEdits) {
  Document document = open_ok(three_page_pdf());
  document.set_trailer_entry("Info", ObjectRef{.number = 3, .generation = 0});
  EXPECT_TRUE(document.trailer_modified());
  EXPECT_TRUE(document.trailer().contains("Info"));
  document.set_trailer_entry("Info", model::Null{});
  EXPECT_FALSE(document.trailer().contains("Info"));
  EXPECT_TRUE(document.has_changes());
}

TEST(Document, InMemoryStreamsDecodeThroughTheirFilters) {
  Document document = open_ok(three_page_pdf());
  const std::string compressed = test::zlib_compress("q 1 0 0 1 0 0 cm Q");
  model::Stream stream;
  stream.dict.set("Filter", Name{"FlateDecode"});
  std::vector<std::byte> data_bytes;
  for (const char ch : compressed) {
    data_bytes.push_back(static_cast<std::byte>(ch));
  }
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(data_bytes));
  const auto ref = document.add(Object(stream));
  ASSERT_TRUE(ref.has_value());
  const auto data = document.decode_stream(*(*document.get(*ref))->as_stream());
  ASSERT_TRUE(data.has_value()) << data.error().message;
  EXPECT_EQ(data->size(), 18U);
}

TEST(Document, HostileSizeCannotWrapObjectNumbers) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.finish_table("/Size 99999999999/Root 1 0 R");
  Document document = open_ok(b.str());
  EXPECT_EQ(document.add(Object(1)).error().code, ErrorCode::kLimitExceeded);
}

}  // namespace
}  // namespace capriole
