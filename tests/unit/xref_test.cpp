#include "capriole/parser/xref.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "capriole/parser/object_loader.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::parser {
namespace {

using model::Name;
using model::Object;
using model::ObjectRef;
using test::PdfBuilder;

// Owns the source for a loader built from a byte string.
class Opened {
 public:
  explicit Opened(const std::string& pdf) : source_(MemoryByteSource::from_string(pdf)) {}
  Result<XrefTable> xref() { return read_xref(source_); }
  ObjectLoader loader() {
    auto table = read_xref(source_);
    EXPECT_TRUE(table.has_value()) << (table ? "" : table.error().message);
    return ObjectLoader(source_, table.has_value() ? std::move(*table) : XrefTable{});
  }

 private:
  MemoryByteSource source_;
};

std::string simple_pdf() {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.add_object(3, "(third)");
  b.finish_table("/Size 4/Root 1 0 R");
  return b.str();
}

// ── startxref ───────────────────────────────────────────────────────────────────────────────

TEST(FindStartxref, Basic) {
  const std::string pdf = simple_pdf();
  auto source = MemoryByteSource::from_string(pdf);
  const auto offset = find_startxref(source);
  ASSERT_TRUE(offset.has_value()) << offset.error().message;
  EXPECT_EQ(pdf.substr(static_cast<std::size_t>(*offset), 4), "xref");
}

TEST(FindStartxref, TrailingBytesAfterEofMarker) {
  auto source = MemoryByteSource::from_string(simple_pdf() + std::string("\r\n\0\0", 4));
  EXPECT_TRUE(find_startxref(source).has_value());
}

TEST(FindStartxref, Errors) {
  auto no_keyword = MemoryByteSource::from_string("%PDF-1.7\n1 0 obj null endobj\n%%EOF\n");
  EXPECT_EQ(find_startxref(no_keyword).error().code, ErrorCode::kSyntax);
  auto no_eof = MemoryByteSource::from_string("%PDF-1.7\nxref\nstartxref\n9\n");
  EXPECT_EQ(find_startxref(no_eof).error().code, ErrorCode::kSyntax);
  auto no_offset = MemoryByteSource::from_string("%PDF-1.7\nstartxref\n%%EOF\n");
  EXPECT_EQ(find_startxref(no_offset).error().code, ErrorCode::kSyntax);
  auto beyond = MemoryByteSource::from_string("%PDF-1.7\nstartxref\n99999\n%%EOF\n");
  EXPECT_EQ(find_startxref(beyond).error().code, ErrorCode::kOutOfRange);
}

// ── Classic tables ──────────────────────────────────────────────────────────────────────────

TEST(ReadXref, ClassicTable) {
  Opened file(simple_pdf());
  const auto xref = file.xref();
  ASSERT_TRUE(xref.has_value()) << xref.error().message;
  ASSERT_EQ(xref->sections.size(), 1U);
  EXPECT_EQ(xref->sections[0].form, XrefForm::kTable);
  EXPECT_EQ(xref->entries.size(), 4U);
  EXPECT_EQ(xref->find(0)->type, XrefEntryType::kFree);
  EXPECT_EQ(xref->find(0)->generation, 65535);
  EXPECT_EQ(xref->find(2)->type, XrefEntryType::kInUse);
  EXPECT_EQ(xref->trailer.find("Root")->as_ref(), (ObjectRef{.number = 1, .generation = 0}));
}

TEST(ReadXref, EntriesWithCrLfAndSpLf) {
  // "SP LF" (from the builder) and "CR LF" are both valid two-byte EOLs.
  std::string pdf = simple_pdf();
  const std::size_t first = pdf.find(" n \n");
  pdf.replace(first, 4, " n\r\n");
  Opened file(pdf);
  EXPECT_TRUE(file.xref().has_value());
}

TEST(ReadXref, NineteenByteEntriesAreRejected) {
  std::string pdf = simple_pdf();
  const std::size_t xref_at = pdf.find("xref\n");
  std::string fixed = pdf.substr(0, xref_at);
  std::string tail = pdf.substr(xref_at);
  // Drop the space before each LF: the classic, non-conforming 19-byte entry.
  for (std::size_t pos = tail.find(" n \n"); pos != std::string::npos; pos = tail.find(" n \n")) {
    tail.replace(pos, 4, " n\n");
  }
  for (std::size_t pos = tail.find(" f \n"); pos != std::string::npos; pos = tail.find(" f \n")) {
    tail.replace(pos, 4, " f\n");
  }
  const std::string startxref = "startxref\n" + std::to_string(xref_at) + "\n%%EOF\n";
  tail = tail.substr(0, tail.find("startxref")) + startxref;
  Opened file(fixed + tail);
  EXPECT_EQ(file.xref().error().code, ErrorCode::kSyntax);
}

TEST(ReadXref, IncrementalUpdateOverridesObjects) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog>>");
  b.add_object(2, "(old)");
  const std::uint64_t first = b.finish_table("/Size 3/Root 1 0 R");
  b.add_object(2, "(new)");
  b.add_object(3, "(added)");
  b.finish_table("/Size 4/Root 1 0 R", first);

  Opened file(b.str());
  const auto xref = file.xref();
  ASSERT_TRUE(xref.has_value()) << xref.error().message;
  ASSERT_EQ(xref->sections.size(), 2U);
  EXPECT_EQ(xref->sections[1].offset, first);
  EXPECT_EQ(xref->trailer.find("Size")->as_integer(), 4);

  auto loader = file.loader();
  EXPECT_EQ(*loader.load(2), Object(model::String{.bytes = "new"}));
  EXPECT_EQ(*loader.load(3), Object(model::String{.bytes = "added"}));
  EXPECT_TRUE(loader.load(1)->as_dict() != nullptr);
}

TEST(ReadXref, PrevCycleIsDetected) {
  PdfBuilder b;
  b.add_object(1, "<<>>");
  const std::uint64_t xref_at = b.size();
  b.finish_table("/Size 2", xref_at);  // /Prev points to itself
  Opened file(b.str());
  EXPECT_EQ(file.xref().error().code, ErrorCode::kSyntax);
}

TEST(ReadXref, PrevBeyondFile) {
  PdfBuilder b;
  b.add_object(1, "<<>>");
  b.finish_table("/Size 2", 1'000'000);
  Opened file(b.str());
  EXPECT_EQ(file.xref().error().code, ErrorCode::kOutOfRange);
}

TEST(ReadXref, MalformedSubsectionHeader) {
  Opened file("%PDF-1.7\nxref\n0 x\ntrailer<<>>\nstartxref\n9\n%%EOF\n");
  EXPECT_EQ(file.xref().error().code, ErrorCode::kSyntax);
}

// ── Cross-reference streams and object streams ──────────────────────────────────────────────

// Object stream 5 holding objects 10 and 11.
std::string object_stream_body(bool compress) {
  const std::string objects = "<</Kind/Ten>> [11 (eleven)]";
  const std::string header = "10 0 11 14 ";
  const std::string data = header + objects;
  const std::string encoded = compress ? test::zlib_compress(data) : data;
  return "<</Type/ObjStm/N 2/First " + std::to_string(header.size()) + "/Length " + std::to_string(encoded.size()) + (compress ? "/Filter/FlateDecode" : "") +
         ">>\nstream\n" + encoded + "\nendstream";
}

TEST(ReadXref, CompressedCrossReferenceStreamWithObjectStream) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog>>");
  b.add_object(5, object_stream_body(true));
  const std::uint64_t at = b.finish_xref_stream(20, "/Size 21/Root 1 0 R", {{10, 2, 5, 0}, {11, 2, 5, 1}});

  Opened file(b.str());
  const auto xref = file.xref();
  ASSERT_TRUE(xref.has_value()) << xref.error().message;
  ASSERT_EQ(xref->sections.size(), 1U);
  EXPECT_EQ(xref->sections[0].form, XrefForm::kStream);
  EXPECT_EQ(xref->sections[0].offset, at);
  EXPECT_EQ(xref->find(10)->type, XrefEntryType::kCompressed);
  EXPECT_EQ(xref->find(11)->stream_index, 1U);
  EXPECT_TRUE(xref->trailer.find("Type")->is_name("XRef"));

  auto loader = file.loader();
  const auto ten = loader.load(10);
  ASSERT_TRUE(ten.has_value()) << ten.error().message;
  EXPECT_TRUE(ten->as_dict()->find("Kind")->is_name("Ten"));
  const auto eleven = loader.resolve(ObjectRef{.number = 11, .generation = 0});
  ASSERT_TRUE(eleven.has_value()) << eleven.error().message;
  EXPECT_EQ(*eleven, Object(model::Array{11, model::String{.bytes = "eleven"}}));
}

TEST(ReadXref, UncompressedCrossReferenceStream) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog>>");
  b.add_object(5, object_stream_body(false));
  b.finish_xref_stream(20, "/Size 21/Root 1 0 R", {{10, 2, 5, 0}, {11, 2, 5, 1}}, false);
  Opened file(b.str());
  auto loader = file.loader();
  EXPECT_TRUE(loader.load(10)->as_dict() != nullptr);
}

TEST(ReadXref, HybridFile) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog>>");
  b.add_object(5, object_stream_body(true));
  const std::uint64_t stream_at = b.write_xref_stream(20, "/Size 21", {{10, 2, 5, 0}, {11, 2, 5, 1}}, true, std::nullopt, false);
  b.finish_table("/Size 21/Root 1 0 R/XRefStm " + std::to_string(stream_at));

  Opened file(b.str());
  const auto xref = file.xref();
  ASSERT_TRUE(xref.has_value()) << xref.error().message;
  EXPECT_EQ(xref->sections[0].form, XrefForm::kHybrid);
  EXPECT_EQ(xref->find(10)->type, XrefEntryType::kCompressed);
  EXPECT_EQ(xref->find(1)->type, XrefEntryType::kInUse);
  auto loader = file.loader();
  EXPECT_TRUE(loader.load(11)->as_array() != nullptr);
}

TEST(ReadXref, CrossReferenceStreamErrors) {
  const auto with_dict = [](const std::string& dict, const std::string& rows) {
    PdfBuilder b;
    const std::uint64_t at = b.size();
    b.raw("1 0 obj\n<</Type/XRef/Size 1" + dict + "/Length " + std::to_string(rows.size()) + ">>\nstream\n" + rows + "\nendstream\nendobj\n");
    b.raw("startxref\n" + std::to_string(at) + "\n%%EOF\n");
    return b.str();
  };
  const std::string one_row = test::be(1, 1) + test::be(15, 4) + test::be(0, 2);
  EXPECT_TRUE(Opened(with_dict("/W[1 4 2]", one_row)).xref().has_value());
  EXPECT_EQ(Opened(with_dict("/W[1 4]", one_row)).xref().error().code, ErrorCode::kSyntax);
  EXPECT_EQ(Opened(with_dict("/W[0 0 0]", one_row)).xref().error().code, ErrorCode::kSyntax);
  EXPECT_EQ(Opened(with_dict("/W[1 9 2]", one_row)).xref().error().code, ErrorCode::kSyntax);
  EXPECT_EQ(Opened(with_dict("/W[1 4 2]", one_row + "x")).xref().error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(Opened(with_dict("/W[1 4 2]/Index[0 999999999999]", one_row)).xref().error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(Opened(with_dict("/W[1 4 2]/Index[0]", one_row)).xref().error().code, ErrorCode::kSyntax);
}

// ── Loader semantics ────────────────────────────────────────────────────────────────────────

TEST(ObjectLoader, FreeMissingAndWrongGenerationLoadAsNull) {
  Opened file(simple_pdf());
  auto loader = file.loader();
  EXPECT_TRUE(loader.load(0)->is_null());   // free
  EXPECT_TRUE(loader.load(99)->is_null());  // absent
  EXPECT_TRUE(loader.resolve(ObjectRef{.number = 3, .generation = 1})->is_null());
  EXPECT_EQ(*loader.resolve(ObjectRef{.number = 3, .generation = 0}), Object(model::String{.bytes = "third"}));
  EXPECT_EQ(*loader.resolve(Object(42)), Object(42));  // non-references pass through
}

TEST(ObjectLoader, IndirectLengthIsResolvedThroughTheXref) {
  PdfBuilder b;
  b.add_object(1, "<</Length 2 0 R>>\nstream\nABCD\nendstream");
  b.add_object(2, "4");
  b.finish_table("/Size 3");
  Opened file(b.str());
  auto loader = file.loader();
  const auto object = loader.load(1);
  ASSERT_TRUE(object.has_value()) << object.error().message;
  const auto data = loader.decode_stream(*object->as_stream());
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), 4U);
}

TEST(ObjectLoader, DecodesStreamsWithIndirectFilter) {
  PdfBuilder b;
  const std::string compressed = test::zlib_compress("payload");
  b.add_object(1, "<</Length " + std::to_string(compressed.size()) + "/Filter 2 0 R>>\nstream\n" + compressed + "\nendstream");
  b.add_object(2, "/FlateDecode");
  b.finish_table("/Size 3");
  Opened file(b.str());
  auto loader = file.loader();
  const auto data = loader.decode_stream(*loader.load(1)->as_stream());
  ASSERT_TRUE(data.has_value()) << data.error().message;
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(data->data()), data->size()), "payload");  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

TEST(ObjectLoader, XrefPointingAtTheWrongObjectIsAnError) {
  PdfBuilder b;
  b.add_object(1, "(one)");
  b.finish_table("/Size 2");
  std::string pdf = b.str();
  pdf.replace(pdf.find("1 0 obj"), 7, "7 0 obj");
  Opened file(pdf);
  auto loader = file.loader();
  EXPECT_EQ(loader.load(1).error().code, ErrorCode::kSyntax);
}

TEST(ObjectLoader, ReferenceCyclesHitTheLoadDepthLimit) {
  // Each stream's /Length is the other stream: resolving either never terminates without a limit.
  PdfBuilder b;
  b.add_object(1, "<</Length 2 0 R>>\nstream\nx\nendstream");
  b.add_object(2, "<</Length 1 0 R>>\nstream\nx\nendstream");
  b.finish_table("/Size 3");
  Opened file(b.str());
  auto loader = file.loader();
  EXPECT_EQ(loader.load(1).error().code, ErrorCode::kLimitExceeded);
}

TEST(ObjectLoader, ObjectStreamMismatchIsAnError) {
  PdfBuilder b;
  b.add_object(5, object_stream_body(true));
  b.finish_xref_stream(20, "/Size 21", {{12, 2, 5, 0}});  // the stream holds 10 at index 0, not 12
  Opened file(b.str());
  auto loader = file.loader();
  EXPECT_EQ(loader.load(12).error().code, ErrorCode::kSyntax);
}

TEST(ObjectLoader, EncryptedFilesCannotDecodeYet) {
  PdfBuilder b;
  b.add_object(1, "<</Length 1>>\nstream\nx\nendstream");
  b.finish_table("/Size 2/Encrypt<</Filter/Standard>>");
  Opened file(b.str());
  auto loader = file.loader();
  EXPECT_TRUE(loader.encrypted());
  const auto object = loader.load(1);
  ASSERT_TRUE(object.has_value());
  EXPECT_EQ(loader.decode_stream(*object->as_stream()).error().code, ErrorCode::kUnsupported);
}

}  // namespace
}  // namespace capriole::parser
