#include "capriole/repair/repair.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "capriole/core/source_reader.hpp"
#include "capriole/document/document.hpp"
#include "capriole/parser/object_parser.hpp"
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

TEST(RepairHeaderOffset, BrokenXrefAsWellFallsThroughToReconstruction) {
  // Junk before the header and a broken xref: shifting the view does not help, rebuilding does.
  std::string pdf = std::string(kJunk) + two_page_pdf();
  pdf.replace(pdf.find("xref"), 4, "xreg");
  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  ASSERT_FALSE(document->repairs().empty());
  EXPECT_EQ(document->repairs()[0].kind, repair::RepairKind::kXrefReconstructed);
  EXPECT_EQ(document->page_refs()->size(), 2U);
}

TEST(RepairPolicy, UnrecoverableFilesReportTheStrictError) {
  // A header and nothing that looks like an object: no repair applies.
  const auto document = Document::open(source_of("%PDF-1.7\nthis is not a document\n"));
  ASSERT_FALSE(document.has_value());
  EXPECT_EQ(document.error().code, ErrorCode::kSyntax);
  EXPECT_NE(document.error().message.find("startxref"), std::string::npos);
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

namespace capriole {
namespace {

Result<Document> open_strict(const std::string& pdf) {
  return Document::open(source_of(pdf), OpenOptions{.limits = kDefaultParseLimits, .password = "", .repair = false});
}

bool has_repair(const Document& document, repair::RepairKind kind) {
  for (const auto& note : document.repairs()) {
    if (note.kind == kind) {
      return true;
    }
  }
  return false;
}

std::string as_text(const std::vector<std::byte>& data) {
  std::string out;
  for (const std::byte b : data) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

// ── Tolerant object parsing ─────────────────────────────────────────────────────────────────

Result<parser::IndirectObject> parse_tolerant(const std::string& text) {
  auto source = source_of(text);
  SourceReader reader(*source);
  parser::ObjectParser parser(reader, kDefaultParseLimits, parser::ParseMode::kTolerant);
  return parser.parse_indirect_object();
}

TEST(TolerantParsing, DuplicateKeysLastWins) {
  const auto object = parse_tolerant("1 0 obj <</A 1 /B 2 /A 3>> endobj");
  ASSERT_TRUE(object.has_value()) << object.error().message;
  const auto* dict = object->object.as_dict();
  ASSERT_EQ(dict->size(), 2U);
  EXPECT_EQ(dict->entries()[0].key, "A");  // position of the first occurrence, value of the last
  EXPECT_EQ(dict->find("A")->as_integer(), 3);
}

TEST(TolerantParsing, WrongOrMissingLengthFindsEndstream) {
  for (const std::string dict : {"<</Length 99>>", "<</Length 2>>", "<<>>", "<</Length (x)>>"}) {
    const std::string text = "1 0 obj " + dict + " stream\r\nHELLO\r\nendstream endobj";
    const auto object = parse_tolerant(text);
    ASSERT_TRUE(object.has_value()) << dict << ": " << object.error().message;
    const auto* stream = object->object.as_stream();
    ASSERT_NE(stream, nullptr) << dict;
    EXPECT_EQ(text.substr(static_cast<std::size_t>(stream->data_offset), static_cast<std::size_t>(stream->data_length)), "HELLO") << dict;
  }
}

TEST(TolerantParsing, StreamKeywordEolVariants) {
  // (Data glued to the keyword, "streamDATA", lexes as one keyword and is not recognised.)
  for (const std::string after : {"stream\rDATA", "stream  \nDATA", "stream \r\nDATA"}) {
    const std::string text = "1 0 obj <</Length 4>> " + after + "\nendstream endobj";
    const auto object = parse_tolerant(text);
    ASSERT_TRUE(object.has_value()) << after << ": " << object.error().message;
    ASSERT_NE(object->object.as_stream(), nullptr) << after;
    EXPECT_EQ(object->object.as_stream()->data_length, 4U) << after;
  }
}

TEST(TolerantParsing, MissingOrMisspeltEndobj) {
  EXPECT_TRUE(parse_tolerant("1 0 obj (text) objend").has_value());
  EXPECT_TRUE(parse_tolerant("1 0 obj (text) 2 0 obj").has_value());
  EXPECT_TRUE(parse_tolerant("1 0 obj (text)").has_value());
}

TEST(TolerantParsing, StillRejectsWhatIsNotAnObject) {
  EXPECT_FALSE(parse_tolerant("1 0 obj <</A 1").has_value());
  EXPECT_FALSE(parse_tolerant("1 0 (x) endobj").has_value());
  // A stream cut by the end of the file keeps its data up to the end.
  const auto cut = parse_tolerant("1 0 obj <<>> stream\nno end marker");
  ASSERT_TRUE(cut.has_value());
  EXPECT_EQ(cut->object.as_stream()->data_length, 13U);
}

// ── Cross-reference reconstruction ──────────────────────────────────────────────────────────

TEST(RepairReconstruction, MissingStartxref) {
  std::string pdf = two_page_pdf();
  pdf.resize(pdf.find("xref"));  // truncated before the cross-reference table
  EXPECT_FALSE(open_strict(pdf).has_value());
  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kXrefReconstructed));
  EXPECT_EQ(document->xref().sections.front().form, parser::XrefForm::kReconstructed);
  EXPECT_EQ(document->page_refs()->size(), 2U);  // /Root came from the catalog found by the scan
}

TEST(RepairReconstruction, LastDefinitionWins) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Rotate 0>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Rotate 90>>");  // an update appended later
  b.raw("trailer <</Root 1 0 R>>\n%%EOF\n");                 // no xref, no startxref
  auto document = Document::open(source_of(b.str()));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_EQ((*document->get(3))->as_dict()->find("Rotate")->as_integer(), 90);
}

TEST(RepairReconstruction, ObjectsInObjectStreams) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  const std::string header = "2 0 3 40 ";
  const std::string objects = "<</Type/Pages/Kids[3 0 R]/Count 1>>     <</Type/Page/Parent 2 0 R>>";
  const std::string data = test::zlib_compress(header + objects);
  b.add_object(5, "<</Type/ObjStm/N 2/First " + std::to_string(header.size()) + "/Length " + std::to_string(data.size()) + "/Filter/FlateDecode>>\nstream\n" +
                      data + "\nendstream");
  b.finish_xref_stream(6, "/Size 7/Root 1 0 R", {{2, 2, 5, 0}, {3, 2, 5, 1}});
  std::string pdf = b.str();
  pdf.replace(pdf.rfind("startxref"), 9, "startxrex");  // break the pointer to the xref stream

  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kXrefReconstructed));
  EXPECT_EQ(document->xref().find(3)->type, parser::XrefEntryType::kCompressed);
  const auto pages = document->page_refs();
  ASSERT_TRUE(pages.has_value()) << pages.error().message;
  EXPECT_EQ(pages->size(), 1U);
}

TEST(RepairReconstruction, LaterObjectStreamOverridesEarlierDirectDefinition) {
  // An incremental update that rewrote object 3 inside an object stream, then got truncated.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Rotate 0>>");  // original
  const std::string header = "3 0 ";
  const std::string objects = "<</Type/Page/Parent 2 0 R/Rotate 180>>";  // the update
  const std::string data = test::zlib_compress(header + objects);
  b.add_object(5, "<</Type/ObjStm/N 1/First " + std::to_string(header.size()) + "/Length " + std::to_string(data.size()) + "/Filter/FlateDecode>>\nstream\n" +
                      data + "\nendstream");
  b.raw("trailer <</Root 1 0 R>>\n");  // truncated: no xref, no startxref
  auto document = Document::open(source_of(b.str()));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_EQ(document->xref().find(3)->type, parser::XrefEntryType::kCompressed);
  EXPECT_EQ((*document->get(3))->as_dict()->find("Rotate")->as_integer(), 180);
}

// ── Per-object repairs ──────────────────────────────────────────────────────────────────────

TEST(RepairObjects, EntryPointingElsewhereIsRelocated) {
  std::string pdf = two_page_pdf();
  // Point object 4's entry at object 3's offset: strict loading finds the wrong object.
  const auto entry_of = [&](int number) {
    const std::size_t xref = pdf.find("xref");
    return pdf.find(" 00000 n", xref) + static_cast<std::size_t>(number - 1) * 20 - 10;
  };
  pdf.replace(entry_of(4), 10, pdf.substr(entry_of(3), 10));
  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  const auto page = document->get(4);
  ASSERT_TRUE(page.has_value()) << page.error().message;
  EXPECT_TRUE((*page)->as_dict()->find("Type")->is_name("Page"));
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kObjectRelocated));
  EXPECT_FALSE(open_strict(pdf)->get(4).has_value());
}

TEST(RepairObjects, EntryForAnObjectThatDoesNotExistReadsAsNull) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.finish_table("/Size 4/Root 1 0 R");
  std::string pdf = b.str();
  // Add an in-use entry for object 3 at offset 0, as some writers do for objects they never wrote.
  pdf.replace(pdf.find("trailer"), 0, "3 1\n0000000000 00000 n \n");
  // The xref grew by 23 bytes after the "xref" keyword: startxref is unaffected.
  auto document = Document::open(source_of(pdf));
  ASSERT_TRUE(document.has_value()) << document.error().message;
  const auto missing = document->get(3);
  ASSERT_TRUE(missing.has_value()) << missing.error().message;
  EXPECT_TRUE((*missing)->is_null());
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kMissingObject));
}

TEST(RepairObjects, PageTreeNodesWithoutType) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Kids[3 0 R]/Count 1>>");  // no /Type
  b.add_object(3, "<</Parent 2 0 R>>");         // no /Type
  b.finish_table("/Size 4/Root 1 0 R");
  EXPECT_FALSE(open_strict(b.str())->page_refs().has_value());
  auto document = Document::open(source_of(b.str()));
  const auto pages = document->page_refs();
  ASSERT_TRUE(pages.has_value()) << pages.error().message;
  EXPECT_EQ(pages->size(), 1U);
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kPageTreeType));
}

TEST(RepairObjects, TruncatedStreamDecodesPartially) {
  std::string original;
  for (int i = 0; i < 4000; ++i) {
    original += std::to_string((i * 7919) % 10007) + " ";  // compresses poorly: a long deflate stream
  }
  const std::string full = test::zlib_compress(original);
  const std::string cut = full.substr(0, full.size() / 2);
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.add_stream(3, cut, "/Filter/FlateDecode");
  b.finish_table("/Size 4/Root 1 0 R");

  auto strict = open_strict(b.str());
  EXPECT_EQ(strict->decode_stream(*(*strict->get(3))->as_stream()).error().code, ErrorCode::kCorruptData);

  auto document = Document::open(source_of(b.str()));
  const auto data = document->decode_stream(*(*document->get(3))->as_stream());
  ASSERT_TRUE(data.has_value()) << data.error().message;
  EXPECT_GT(data->size(), 0U);
  EXPECT_LT(data->size(), original.size());
  EXPECT_EQ(as_text(*data), original.substr(0, data->size()));  // an exact prefix of the original
  EXPECT_TRUE(has_repair(*document, repair::RepairKind::kPartialDecode));
}

}  // namespace
}  // namespace capriole
