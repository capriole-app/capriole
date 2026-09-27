#include "capriole/writer/save.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::writer {
namespace {

using model::Object;
using test::PdfBuilder;

std::unique_ptr<ByteSource> source_of(const std::string& text) {
  std::vector<std::byte> data;
  for (const char ch : text) {
    data.push_back(static_cast<std::byte>(ch));
  }
  return std::make_unique<MemoryByteSource>(std::move(data));
}

Document open(const std::string& pdf, bool repair = true) {
  auto document = Document::open(source_of(pdf), OpenOptions{.limits = kDefaultParseLimits, .password = "", .repair = repair});
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

std::string save(Document& document, const SaveOptions& options = {}, bool incremental = false) {
  MemorySink sink;
  const auto report = incremental ? save_incremental(document, sink, options) : save_full(document, sink, options);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  std::string out;
  for (const std::byte b : sink.data()) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

// Every object of `original` reads the same from `saved`: equal values, and for streams equal
// dictionaries apart from /Length and equal decoded data.
void expect_same_objects(Document& original, Document& saved) {
  for (const model::ObjectRef ref : original.live_objects()) {
    SCOPED_TRACE("object " + std::to_string(ref.number));
    const Object before = **original.get(ref.number);
    if (const auto* stream = before.as_stream()) {
      const auto* dict = stream->dict.find("Type");
      if (dict != nullptr && (dict->is_name("ObjStm") || dict->is_name("XRef"))) {
        continue;  // containers are not written; their content is
      }
    }
    const auto after = saved.get(ref.number);
    ASSERT_TRUE(after.has_value()) << after.error().message;
    if (const auto* stream = before.as_stream()) {
      const auto* saved_stream = (*after)->as_stream();
      ASSERT_NE(saved_stream, nullptr);
      model::Dict a = stream->dict;
      model::Dict b = saved_stream->dict;
      a.erase("Length");
      b.erase("Length");
      EXPECT_EQ(Object(a), Object(b));
      EXPECT_EQ(*original.decode_stream(*stream), *saved.decode_stream(*saved_stream));
    } else {
      EXPECT_EQ(before, **after);
    }
  }
}

std::string sample_pdf() {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Contents 4 0 R>>");
  b.add_stream(4, test::zlib_compress("BT /F1 12 Tf (Hello) Tj ET"), "/Filter/FlateDecode");
  b.add_object(5, "<</Title(Sample)/Creator(\\(test\\))>>");
  b.finish_table("/Size 6/Root 1 0 R/Info 5 0 R/ID[<0102030405060708><0102030405060708>]");
  return b.str();
}

// ── Round trips ─────────────────────────────────────────────────────────────────────────────

TEST(FullSave, RoundTripKeepsEveryObject) {
  Document original = open(sample_pdf());
  const std::string out = save(original);
  Document saved = open(out, false);  // the output must read strictly
  EXPECT_EQ(saved.page_refs()->size(), 1U);
  expect_same_objects(original, saved);
  EXPECT_EQ(saved.xref().sections.size(), 1U);
  EXPECT_EQ(saved.xref().sections[0].form, parser::XrefForm::kTable);
}

TEST(FullSave, ObjectStreamsAndCrossReferenceStreamsBecomeOrdinaryObjects) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  const std::string header = "2 0 3 40 ";
  const std::string objects = "<</Type/Pages/Kids[3 0 R]/Count 1>>     <</Type/Page/Parent 2 0 R>>";
  const std::string data = test::zlib_compress(header + objects);
  b.add_object(5, "<</Type/ObjStm/N 2/First " + std::to_string(header.size()) + "/Length " + std::to_string(data.size()) + "/Filter/FlateDecode>>\nstream\n" +
                      data + "\nendstream");
  b.finish_xref_stream(6, "/Size 7/Root 1 0 R", {{2, 2, 5, 0}, {3, 2, 5, 1}});

  Document original = open(b.str());
  const std::string out = save(original);
  EXPECT_EQ(out.find("/ObjStm"), std::string::npos);
  EXPECT_EQ(out.find("/XRef"), std::string::npos);
  Document saved = open(out, false);
  EXPECT_EQ(saved.xref().find(3)->type, parser::XrefEntryType::kInUse);
  EXPECT_EQ(saved.page_refs()->size(), 1U);
  expect_same_objects(original, saved);
}

TEST(FullSave, IncrementalUpdatesAreFlattened) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.add_object(3, "(old)");
  const std::uint64_t first = b.finish_table("/Size 4/Root 1 0 R");
  b.add_object(3, "(new)");
  b.finish_table("/Size 4/Root 1 0 R", first);
  Document original = open(b.str());
  Document saved = open(save(original), false);
  EXPECT_EQ(saved.xref().sections.size(), 1U);
  EXPECT_EQ(**saved.get(3), Object(model::String{.bytes = "new"}));
}

TEST(FullSave, RepairedFilesComeOutConforming) {
  std::string pdf = sample_pdf();
  pdf.resize(pdf.find("xref"));  // no cross-reference table: needs the reconstruction repair
  Document original = open(pdf);
  ASSERT_FALSE(original.repairs().empty());
  Document saved = open(save(original), false);
  EXPECT_TRUE(saved.repairs().empty());
  EXPECT_EQ(saved.page_refs()->size(), 1U);
}

TEST(FullSave, OutputIsDeterministic) {
  Document first = open(sample_pdf());
  Document second = open(sample_pdf());
  EXPECT_EQ(save(first), save(second));
}

TEST(FullSave, FileIdentifierKeepsTheFirstStringAndHashesTheSecond) {
  Document original = open(sample_pdf());
  Document saved = open(save(original), false);
  const auto* id = saved.trailer().find("ID")->as_array();
  ASSERT_NE(id, nullptr);
  ASSERT_EQ(id->size(), 2U);
  EXPECT_EQ((*id)[0].as_string()->bytes, std::string("\x01\x02\x03\x04\x05\x06\x07\x08", 8));
  EXPECT_EQ((*id)[1].as_string()->bytes.size(), 16U);
  EXPECT_NE((*id)[1].as_string()->bytes, (*id)[0].as_string()->bytes);
}

// ── Page tree repairs are written out ─────────────────────────────────────────────────────────

// The pages of `saved`, read strictly, are the pages `original` found with repairs.
void expect_same_pages(Document& original, Document& saved) {
  const auto before = original.page_refs();
  const auto after = saved.page_refs();
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(after.has_value()) << after.error().message;
  ASSERT_EQ(after->size(), before->size());
  const auto root = saved.resolve(*(*saved.catalog())->find("Pages"));
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(root->as_dict()->find("Count")->as_integer(), static_cast<std::int64_t>(after->size()));
  EXPECT_TRUE(saved.page_tree_conforms(*after));
}

TEST(FullSave, ConformingPageTreeIsKeptAsItIs) {
  Document original = open(sample_pdf());
  MemorySink sink;
  EXPECT_FALSE(save_full(original, sink)->page_tree_rewritten);
}

TEST(FullSave, PageTreeNodesWithoutTypeGetOne) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Parent 2 0 R/MediaBox[0 0 10 10]>>");
  b.finish_table("/Size 4/Root 1 0 R");
  Document original = open(b.str());
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  EXPECT_TRUE((*saved.get(2))->as_dict()->find("Type")->is_name("Pages"));
  EXPECT_TRUE((*saved.get(3))->as_dict()->find("Type")->is_name("Page"));
}

TEST(FullSave, RebuiltPageTreeIsWrittenFlatWithInheritedAttributes) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 6 0 R]/Count 3/MediaBox[0 0 612 792]>>");
  b.add_object(3, "<</Type/Pages/Parent 2 0 R/Kids[]/Count 2/Rotate 90/Resources<</ProcSet[/PDF]>>>>");
  b.add_object(4, "<</Type/Page/Parent 3 0 R>>");
  b.add_object(5, "<</Type/Page/Parent 3 0 R/Rotate 180>>");
  b.add_object(6, "<</Type/Page/Parent 2 0 R>>");
  b.finish_table("/Size 7/Root 1 0 R");
  Document original = open(b.str());
  MemorySink sink;
  ASSERT_TRUE(save_full(original, sink)->page_tree_rewritten);
  std::string out;
  for (const std::byte byte : sink.data()) {
    out.push_back(static_cast<char>(byte));
  }
  Document saved = open(out, false);
  expect_same_pages(original, saved);
  const auto pages = *saved.page_refs();
  EXPECT_EQ(pages[0].number, 4U);
  EXPECT_EQ(pages[2].number, 6U);
  const auto* page4 = (*saved.get(4))->as_dict();
  EXPECT_EQ(page4->find("Rotate")->as_integer(), 90);                        // inherited from the node left out
  EXPECT_TRUE(page4->contains("Resources"));                                 // likewise
  EXPECT_FALSE(page4->contains("MediaBox"));                                 // still inherited from the root
  EXPECT_EQ((*saved.get(5))->as_dict()->find("Rotate")->as_integer(), 180);  // its own wins
  EXPECT_FALSE((*saved.get(6))->as_dict()->contains("Rotate"));
}

TEST(FullSave, UnreadablePageBecomesABlankPage) {
  // Kid 9 does not exist: the repair keeps its place so that /Count 2 holds; the save writes a blank
  // page at a new number (9 is not an object of the file).
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 9 0 R]/Count 2>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 10 10]>>");
  b.finish_table("/Size 4/Root 1 0 R");
  Document original = open(b.str());
  ASSERT_EQ(original.page_refs()->size(), 2U);
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  const auto blank = (*saved.page_refs())[1];
  EXPECT_EQ(blank.number, 4U);
  EXPECT_TRUE((*saved.get(blank))->as_dict()->contains("MediaBox"));
}

TEST(FullSave, RootTakenAsTheOnePageGetsANewRoot) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Count 1/MediaBox[0 0 10 10]>>");  // no /Type, no /Kids: read as the one page
  b.finish_table("/Size 3/Root 1 0 R");
  Document original = open(b.str());
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  EXPECT_EQ((*saved.page_refs())[0].number, 2U);
  EXPECT_EQ((*saved.catalog())->find("Pages")->as_ref()->number, 3U);
  EXPECT_FALSE((*saved.get(2))->as_dict()->contains("Count"));
}

TEST(FullSave, PageWithoutAnyMediaBoxGetsTheDefault) {
  // Page 4 is found through its /Parent link (node 3 lost it from /Kids); no node has a /MediaBox.
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Pages/Parent 2 0 R/Kids[]/Count 1>>");
  b.add_object(4, "<</Type/Page/Parent 3 0 R>>");
  b.finish_table("/Size 5/Root 1 0 R");
  Document original = open(b.str());
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  const auto* box = (*saved.get(4))->as_dict()->find("MediaBox");
  ASSERT_NE(box, nullptr);
  EXPECT_EQ(box->as_array()->size(), 4U);
}

TEST(FullSave, AnotherObjectInAPagesPlaceIsKeptAndTheSlotBecomesABlankPage) {
  // Kid 4 is a font descriptor, but /Count 2 keeps its slot: the font stays a font.
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 10 10]>>");
  b.add_object(4, "<</Type/FontDescriptor/FontName/Arial>>");
  b.finish_table("/Size 5/Root 1 0 R");
  Document original = open(b.str());
  ASSERT_EQ(original.page_refs()->size(), 2U);
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  EXPECT_TRUE((*saved.get(4))->as_dict()->find("Type")->is_name("FontDescriptor"));
  EXPECT_NE((*saved.page_refs())[1].number, 4U);
}

TEST(FullSave, WrongRootCountAndRootParentAreCorrected) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Parent 3 0 R/Kids[3 0 R 4 0 R]/Count 50>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R>>");
  b.add_object(4, "<</Type/Page/Parent 2 0 R>>");
  b.finish_table("/Size 5/Root 1 0 R");
  Document original = open(b.str());
  Document saved = open(save(original), false);
  expect_same_pages(original, saved);
  EXPECT_FALSE((*saved.get(2))->as_dict()->contains("Parent"));
}

// ── Edits ───────────────────────────────────────────────────────────────────────────────────

TEST(FullSave, WritesEdits) {
  Document document = open(sample_pdf());
  model::Dict page = *(*document.get(3))->as_dict();
  page.set("Rotate", 90);
  ASSERT_TRUE(document.set(3, page).has_value());
  const auto added = document.add(Object(model::String{.bytes = "added"}));
  ASSERT_TRUE(added.has_value());
  ASSERT_TRUE(document.remove(5).has_value());
  document.set_trailer_entry("Info", model::Null{});

  Document saved = open(save(document), false);
  EXPECT_EQ((*saved.get(3))->as_dict()->find("Rotate")->as_integer(), 90);
  EXPECT_EQ(**saved.get(added->number), Object(model::String{.bytes = "added"}));
  EXPECT_TRUE((*saved.get(5))->is_null());
  const auto* entry = saved.xref().find(5);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->type, parser::XrefEntryType::kFree);
  EXPECT_EQ(entry->generation, 1);  // deleted: the generation goes up by one
  EXPECT_FALSE(saved.trailer().contains("Info"));
}

TEST(FullSave, NewStreamsWithDataInMemory) {
  Document document = open(sample_pdf());
  model::Stream stream;
  std::vector<std::byte> bytes;
  for (const char ch : std::string("q Q")) {
    bytes.push_back(static_cast<std::byte>(ch));
  }
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
  const auto ref = document.add(Object(stream));
  Document saved = open(save(document), false);
  const auto data = saved.decode_stream(*(*saved.get(ref->number))->as_stream());
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(data->size(), 3U);
}

TEST(FullSave, ModificationDateOnlyWhenGiven) {
  Document with_info = open(sample_pdf());
  Document saved = open(save(with_info, SaveOptions{.modification_date = "D:20260926213000Z"}), false);
  const auto info = saved.resolve(*saved.trailer().find("Info"));
  EXPECT_EQ(info->as_dict()->find("ModDate")->as_string()->bytes, "D:20260926213000Z");
  EXPECT_EQ(info->as_dict()->find("Title")->as_string()->bytes, "Sample");

  // Without /Info, one is created for the date.
  Document without_info = open(sample_pdf());
  without_info.set_trailer_entry("Info", model::Null{});
  Document saved_new = open(save(without_info, SaveOptions{.modification_date = "D:20260926Z"}), false);
  EXPECT_EQ(saved_new.resolve(*saved_new.trailer().find("Info"))->as_dict()->find("ModDate")->as_string()->bytes, "D:20260926Z");

  // And without a date, /Info is untouched.
  Document untouched = open(sample_pdf());
  Document saved_plain = open(save(untouched), false);
  EXPECT_FALSE(saved_plain.resolve(*saved_plain.trailer().find("Info"))->as_dict()->contains("ModDate"));
}

// ── What is not written ─────────────────────────────────────────────────────────────────────

TEST(FullSave, LinearizationDictionaryIsDropped) {
  PdfBuilder b;
  b.add_object(7, "<</Linearized 1/L 1000/O 1/E 500/N 1/T 900/H[100 50]>>");
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  b.finish_table("/Size 8/Root 1 0 R");
  Document original = open(b.str());
  const std::string out = save(original);
  EXPECT_EQ(out.find("/Linearized"), std::string::npos);
}

TEST(FullSave, EncryptedDocumentsStayEncrypted) {
  // The details are in encryption_test.cpp, for every revision.
  auto source = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/r4_aes_128_empty_user.pdf");
  ASSERT_TRUE(source.has_value());
  auto document = Document::open(std::move(*source));
  ASSERT_TRUE(document.has_value());
  Document saved = open(save(*document), false);
  ASSERT_NE(saved.security(), nullptr);
  EXPECT_EQ(saved.page_refs()->size(), 1U);
}

// ── Incremental save ────────────────────────────────────────────────────────────────────────

std::string save_incremental_text(Document& document, const SaveOptions& options = {}) { return save(document, options, true); }

// The original file with a table section, a stream, and objects 1..5.
std::string incremental_sample() { return sample_pdf(); }

TEST(IncrementalSave, WithoutChangesTheOutputIsTheOriginal) {
  const std::string pdf = incremental_sample();
  Document document = open(pdf, false);
  EXPECT_EQ(save_incremental_text(document), pdf);
}

TEST(IncrementalSave, AppendsOnlyTheChangesAfterTheOriginalBytes) {
  const std::string pdf = incremental_sample();
  Document document = open(pdf, false);
  model::Dict page = *(*document.get(3))->as_dict();
  page.set("Rotate", 90);
  ASSERT_TRUE(document.set(3, page).has_value());
  const std::string out = save_incremental_text(document);
  ASSERT_GT(out.size(), pdf.size());
  EXPECT_EQ(out.substr(0, pdf.size()), pdf);  // the original, byte for byte
  const std::string update = out.substr(pdf.size());
  EXPECT_NE(update.find("3 0 obj"), std::string::npos);
  EXPECT_EQ(update.find("4 0 obj"), std::string::npos);  // unchanged objects are not rewritten
  EXPECT_NE(update.find("/Prev "), std::string::npos);

  Document saved = open(out, false);
  ASSERT_EQ(saved.xref().sections.size(), 2U);
  EXPECT_EQ(saved.xref().sections[0].form, parser::XrefForm::kTable);
  EXPECT_EQ((*saved.get(3))->as_dict()->find("Rotate")->as_integer(), 90);
  EXPECT_EQ(saved.page_refs()->size(), 1U);
  EXPECT_EQ(*saved.decode_stream(*(*saved.get(4))->as_stream()), *document.decode_stream(*(*document.get(4))->as_stream()));
}

TEST(IncrementalSave, NewAndDeletedObjects) {
  Document document = open(incremental_sample(), false);
  const auto added = document.add(Object(model::String{.bytes = "added"}));
  ASSERT_TRUE(added.has_value());
  ASSERT_TRUE(document.remove(5).has_value());
  document.set_trailer_entry("Info", model::Null{});
  Document saved = open(save_incremental_text(document), false);
  EXPECT_EQ(**saved.get(added->number), Object(model::String{.bytes = "added"}));
  EXPECT_TRUE((*saved.get(5))->is_null());
  EXPECT_EQ(saved.xref().find(5)->type, parser::XrefEntryType::kFree);
  EXPECT_EQ(saved.xref().find(5)->generation, 1);
  EXPECT_FALSE(saved.trailer().contains("Info"));
  EXPECT_EQ(saved.trailer().find("Size")->as_integer(), static_cast<std::int64_t>(added->number) + 1);
}

TEST(IncrementalSave, CrossReferenceStreamFilesGetAStream) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  const std::string header = "2 0 3 40 ";
  const std::string objects = "<</Type/Pages/Kids[3 0 R]/Count 1>>     <</Type/Page/Parent 2 0 R>>";
  const std::string data = test::zlib_compress(header + objects);
  b.add_object(5, "<</Type/ObjStm/N 2/First " + std::to_string(header.size()) + "/Length " + std::to_string(data.size()) + "/Filter/FlateDecode>>\nstream\n" +
                      data + "\nendstream");
  b.finish_xref_stream(6, "/Size 7/Root 1 0 R", {{2, 2, 5, 0}, {3, 2, 5, 1}});
  const std::string pdf = b.str();

  Document document = open(pdf, false);
  model::Dict page = *(*document.get(3))->as_dict();  // lives in the object stream
  page.set("Rotate", 180);
  ASSERT_TRUE(document.set(3, page).has_value());
  const std::string out = save_incremental_text(document);
  EXPECT_EQ(out.substr(0, pdf.size()), pdf);
  Document saved = open(out, false);
  ASSERT_EQ(saved.xref().sections.size(), 2U);
  EXPECT_EQ(saved.xref().sections[0].form, parser::XrefForm::kStream);
  EXPECT_EQ(saved.xref().find(3)->type, parser::XrefEntryType::kInUse);       // the new definition
  EXPECT_EQ(saved.xref().find(2)->type, parser::XrefEntryType::kCompressed);  // still in the stream
  EXPECT_EQ((*saved.get(3))->as_dict()->find("Rotate")->as_integer(), 180);
  EXPECT_EQ(saved.page_refs()->size(), 1U);
}

TEST(IncrementalSave, HybridFilesGetATable) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
  const std::uint64_t stream_at = b.write_xref_stream(20, "/Size 21", {}, true, std::nullopt, false);
  b.finish_table("/Size 21/Root 1 0 R/XRefStm " + std::to_string(stream_at));
  Document document = open(b.str(), false);
  ASSERT_EQ(document.xref().sections[0].form, parser::XrefForm::kHybrid);
  ASSERT_TRUE(document.add(Object(1)).has_value());
  const std::string out = save_incremental_text(document);
  Document saved = open(out, false);
  EXPECT_EQ(saved.xref().sections[0].form, parser::XrefForm::kTable);
  EXPECT_EQ(out.rfind("/XRefStm"), out.find("/XRefStm"));  // not repeated in the new trailer
}

TEST(IncrementalSave, SuccessiveSavesChainTheSections) {
  const std::string pdf = incremental_sample();
  Document first = open(pdf, false);
  ASSERT_TRUE(first.add(Object(1)).has_value());
  const std::string once = save_incremental_text(first);
  Document second = open(once, false);
  ASSERT_TRUE(second.add(Object(2)).has_value());
  const std::string twice = save_incremental_text(second);
  EXPECT_EQ(twice.substr(0, once.size()), once);
  Document saved = open(twice, false);
  EXPECT_EQ(saved.xref().sections.size(), 3U);
  EXPECT_EQ(**saved.get(6), Object(1));
  EXPECT_EQ(**saved.get(7), Object(2));
}

TEST(IncrementalSave, ModificationDateIsTheChange) {
  Document document = open(incremental_sample(), false);
  Document saved = open(save_incremental_text(document, SaveOptions{.modification_date = "D:20260927Z"}), false);
  const auto info = saved.resolve(*saved.trailer().find("Info"));
  EXPECT_EQ(info->as_dict()->find("ModDate")->as_string()->bytes, "D:20260927Z");
  EXPECT_EQ(info->as_dict()->find("Title")->as_string()->bytes, "Sample");
  EXPECT_EQ(saved.xref().sections.size(), 2U);
}

TEST(IncrementalSave, FileIdentifierKeepsTheFirstStringAndChangesTheSecond) {
  Document document = open(incremental_sample(), false);
  ASSERT_TRUE(document.add(Object(1)).has_value());
  Document saved = open(save_incremental_text(document), false);
  const auto* id = saved.trailer().find("ID")->as_array();
  ASSERT_NE(id, nullptr);
  EXPECT_EQ((*id)[0].as_string()->bytes, std::string("\x01\x02\x03\x04\x05\x06\x07\x08", 8));
  EXPECT_EQ((*id)[1].as_string()->bytes.size(), 16U);
}

TEST(IncrementalSave, FileWithoutFinalEndOfLine) {
  std::string pdf = incremental_sample();
  pdf.pop_back();  // "%%EOF" with nothing after it
  Document document = open(pdf, false);
  ASSERT_TRUE(document.add(Object(1)).has_value());
  const std::string out = save_incremental_text(document);
  EXPECT_EQ(out.substr(0, pdf.size() + 1), pdf + "\n");
  Document saved = open(out, false);
  EXPECT_EQ(**saved.get(6), Object(1));
}

TEST(IncrementalSave, RepairedDocumentsAreRefused) {
  std::string pdf = sample_pdf();
  pdf.resize(pdf.find("xref"));  // needs the reconstruction repair
  Document document = open(pdf);
  ASSERT_FALSE(document.repairs().empty());
  MemorySink sink;
  EXPECT_EQ(save_incremental(document, sink).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(sink.position(), 0U);
}

TEST(IncrementalSave, PageTreeRepairsAreFoundBeforeDeciding) {
  PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Kids[3 0 R]/Count 1>>");  // no /Type: read only with a repair
  b.add_object(3, "<</Type/Page/Parent 2 0 R>>");
  b.finish_table("/Size 4/Root 1 0 R");
  Document document = open(b.str());
  ASSERT_TRUE(document.repairs().empty());  // not read yet
  MemorySink sink;
  EXPECT_EQ(save_incremental(document, sink).error().code, ErrorCode::kUnsupported);
}

}  // namespace
}  // namespace capriole::writer
