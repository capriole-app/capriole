#include "capriole/edit/pages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/writer/save.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using model::Dict;
using model::Object;
using model::ObjectRef;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::string text_of(const std::vector<std::byte>& data) {
  std::string out;
  for (const std::byte b : data) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

Document open(const std::string& pdf) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(pdf)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

std::string save(Document& document, const writer::SaveOptions& options = {}, bool incremental = false) {
  writer::MemorySink sink;
  const auto report = incremental ? writer::save_incremental(document, sink, options) : writer::save_full(document, sink, options);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  return text_of(sink.data());
}

// Three pages: 4 and 5 under an intermediate node (3, /Rotate 90), 6 directly under the root (2,
// /MediaBox and /Resources for all). Each page's content names it.
std::string three_pages() {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 6 0 R]/Count 3/MediaBox[0 0 100 100]/Resources<</Font<</F1 9 0 R>>>>>>");
  b.add_object(3, "<</Type/Pages/Parent 2 0 R/Kids[4 0 R 5 0 R]/Count 2/Rotate 90>>");
  b.add_object(4, "<</Type/Page/Parent 3 0 R/Contents 7 0 R/Annots[<</Subtype/Text/Rect[0 0 10 10]/P 4 0 R>> 10 0 R]>>");
  b.add_object(5, "<</Type/Page/Parent 3 0 R/Contents 8 0 R>>");
  b.add_object(6, "<</Type/Page/Parent 2 0 R/Contents 11 0 R>>");
  b.add_stream(7, "(one) Tj");
  b.add_stream(8, "(two) Tj");
  b.add_object(9, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  b.add_object(10, "<</Subtype/Widget/Rect[0 0 10 10]/P 4 0 R/FT/Tx/T(field)>>");
  b.add_stream(11, "(three) Tj");
  b.finish_table("/Size 12/Root 1 0 R");
  return b.str();
}

std::vector<std::uint32_t> page_numbers(Document& document) {
  auto pages = document.page_refs();
  EXPECT_TRUE(pages.has_value());
  std::vector<std::uint32_t> out;
  for (const ObjectRef page : pages.value_or(std::vector<ObjectRef>{})) {
    out.push_back(page.number);
  }
  return out;
}

const Dict& dict_of(Document& document, std::uint32_t number) {
  static const Dict kEmpty;
  auto object = document.get(number);
  return object && (*object)->as_dict() != nullptr ? *(*object)->as_dict() : kEmpty;
}

// The decoded content of a page.
std::string content_of(Document& document, std::uint32_t page) {
  const Object* contents = dict_of(document, page).find("Contents");
  if (contents == nullptr) {
    return {};
  }
  auto stream = document.resolve(*contents);
  if (!stream || stream->as_stream() == nullptr) {
    return {};
  }
  auto data = document.decode_stream(*stream->as_stream());
  return data ? text_of(*data) : std::string();
}

TEST(Pages, RotateAddsToTheInheritedRotationAndTouchesThePageAlone) {
  Document document = open(three_pages());
  const std::vector<std::uint32_t> first = {0};
  ASSERT_TRUE(rotate_pages(document, first, 90).has_value());
  EXPECT_EQ(dict_of(document, 4).find("Rotate")->as_integer(), 180);  // 90 inherited + 90
  EXPECT_EQ(document.changed_objects(), (std::vector<std::uint32_t>{4}));
  const std::vector<std::uint32_t> last = {2};
  ASSERT_TRUE(rotate_pages(document, last, -90).has_value());
  EXPECT_EQ(dict_of(document, 6).find("Rotate")->as_integer(), 270);
  EXPECT_EQ(rotate_pages(document, first, 45).error().code, ErrorCode::kUnsupported);
  const std::vector<std::uint32_t> past = {3};
  EXPECT_EQ(rotate_pages(document, past, 90).error().code, ErrorCode::kOutOfRange);
}

TEST(Pages, ArrangeWritesAFlatTreeKeepingWhatPagesInherited) {
  Document document = open(three_pages());
  const std::vector<std::uint32_t> reversed = {2, 1, 0};
  ASSERT_TRUE(arrange_pages(document, reversed).has_value());
  EXPECT_EQ(page_numbers(document), (std::vector<std::uint32_t>{6, 5, 4}));
  // The intermediate node is gone; the pages under it carry its /Rotate, and still inherit the root's
  // /MediaBox (the root stays).
  EXPECT_EQ(document.state(3), ObjectState::kDeleted);
  EXPECT_EQ(dict_of(document, 4).find("Rotate")->as_integer(), 90);
  EXPECT_EQ(dict_of(document, 4).find("Parent")->as_ref(), (ObjectRef{.number = 2, .generation = 0}));
  EXPECT_FALSE(dict_of(document, 4).contains("MediaBox"));
  EXPECT_EQ(dict_of(document, 2).find("Count")->as_integer(), 3);
  // Page 6 was already under the root: untouched.
  EXPECT_EQ(document.state(6), ObjectState::kUnmodified);
}

TEST(Pages, ArrangeDeletesAndDuplicates) {
  Document document = open(three_pages());
  const std::vector<std::uint32_t> without_second = {0, 2};
  ASSERT_TRUE(arrange_pages(document, without_second).has_value());
  EXPECT_EQ(page_numbers(document), (std::vector<std::uint32_t>{4, 6}));
  EXPECT_EQ(document.state(5), ObjectState::kDeleted);

  // The first page twice: the second is a new page object with the same contents, its note copied
  // (pointing at the new page), its form widget not.
  const std::vector<std::uint32_t> twice = {0, 0, 1};
  ASSERT_TRUE(arrange_pages(document, twice).has_value());
  const std::vector<std::uint32_t> pages = page_numbers(document);
  ASSERT_EQ(pages.size(), 3U);
  EXPECT_EQ(pages[0], 4U);
  EXPECT_NE(pages[1], 4U);
  EXPECT_EQ(content_of(document, pages[1]), "(one) Tj");
  const Dict& copy = dict_of(document, pages[1]);
  EXPECT_EQ(copy.find("Rotate")->as_integer(), 90);
  const auto annots = document.resolve(*copy.find("Annots"));
  ASSERT_TRUE(annots && annots->as_array() != nullptr);
  ASSERT_EQ(annots->as_array()->size(), 1U);
  const auto note = document.resolve(annots->as_array()->front());
  EXPECT_EQ(note->as_dict()->find("P")->as_ref()->number, pages[1]);

  const std::vector<std::uint32_t> none;
  EXPECT_EQ(arrange_pages(document, none).error().code, ErrorCode::kOutOfRange);
  const std::vector<std::uint32_t> past = {7};
  EXPECT_EQ(arrange_pages(document, past).error().code, ErrorCode::kOutOfRange);
}

TEST(Pages, ArrangedDocumentsSaveFullyAndIncrementally) {
  for (const bool incremental : {false, true}) {
    SCOPED_TRACE(incremental ? "incremental" : "full");
    Document document = open(three_pages());
    const std::vector<std::uint32_t> order = {2, 0};
    ASSERT_TRUE(arrange_pages(document, order).has_value());
    const std::string saved = save(document, {}, incremental);
    Document reopened = open(saved);
    const auto pages = reopened.page_refs();
    ASSERT_TRUE(pages.has_value());
    ASSERT_EQ(pages->size(), 2U);
    EXPECT_EQ(content_of(reopened, (*pages)[0].number), "(three) Tj");
    EXPECT_EQ(content_of(reopened, (*pages)[1].number), "(one) Tj");
    EXPECT_TRUE(reopened.repairs().empty());
  }
}

TEST(Pages, FullSavesCanDropWhatNothingReaches) {
  Document document = open(three_pages());
  const std::vector<std::uint32_t> order = {0, 2};  // page 5 and its content (8) are no longer used
  ASSERT_TRUE(arrange_pages(document, order).has_value());
  writer::MemorySink sink;
  const auto report = writer::save_full(document, sink, {.modification_date = std::nullopt, .drop_unreachable = true});
  ASSERT_TRUE(report.has_value());
  EXPECT_GE(report->objects_dropped, 1U);
  Document compact = open(text_of(sink.data()));
  EXPECT_TRUE((*compact.get(8))->is_null());
  EXPECT_EQ(compact.page_refs()->size(), 2U);
  // /Size is one past the highest number the table defines (§7.5.5), not the document's next number.
  EXPECT_EQ(compact.trailer().find("Size")->as_integer(), static_cast<std::int64_t>(compact.live_objects().back().number) + 1);
  // Without the option the content stays.
  Document full = open(save(document));
  EXPECT_FALSE((*full.get(8))->is_null());
}

// Two pages sharing a font; page 1 links to page 2 and carries a form field named "name".
std::string source_document() {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[9 0 R]/DR<</Font<</Helv 7 0 R>>>>/DA(/Helv 0 Tf 0 g)>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2/MediaBox[0 0 200 200]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 5 0 R/Resources<</Font<</F1 7 0 R>>>>/Annots[8 0 R 9 0 R]/StructParents 0>>");
  b.add_object(4, "<</Type/Page/Parent 2 0 R/Contents 6 0 R/Resources<</Font<</F1 7 0 R>>>>>>");
  b.add_stream(5, "BT /F1 12 Tf (source one) Tj ET");
  b.add_stream(6, "BT /F1 12 Tf (source two) Tj ET");
  b.add_object(7, "<</Type/Font/Subtype/Type1/BaseFont/Courier>>");
  b.add_object(8, "<</Subtype/Link/Rect[0 0 10 10]/Dest[4 0 R/Fit]/StructParent 1>>");
  b.add_object(9, "<</Subtype/Widget/Rect[0 0 50 20]/P 3 0 R/FT/Tx/T(name)>>");
  b.finish_table("/Size 10/Root 1 0 R");
  return b.str();
}

std::string target_document() {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm 5 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 100 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Annots[6 0 R]>>");
  b.add_stream(4, "(target) Tj");
  b.add_object(5, "<</Fields[6 0 R]>>");
  b.add_object(6, "<</Subtype/Widget/Rect[0 0 50 20]/P 3 0 R/FT/Tx/T(name)>>");
  b.finish_table("/Size 7/Root 1 0 R");
  return b.str();
}

TEST(Pages, ImportCopiesWhatThePagesUseOnce) {
  Document target = open(target_document());
  Document source = open(source_document());
  const std::vector<std::uint32_t> both = {1, 0};
  const auto added = import_pages(target, source, both, 0);
  ASSERT_TRUE(added.has_value()) << added.error().message;
  ASSERT_EQ(added->size(), 2U);
  const std::vector<std::uint32_t> pages = page_numbers(target);
  ASSERT_EQ(pages.size(), 3U);
  EXPECT_EQ(content_of(target, pages[0]), "BT /F1 12 Tf (source two) Tj ET");
  EXPECT_EQ(content_of(target, pages[1]), "BT /F1 12 Tf (source one) Tj ET");
  EXPECT_EQ(content_of(target, pages[2]), "(target) Tj");
  // The pages take the source root's /MediaBox; the shared font is one object; no structure links.
  const Dict& two = dict_of(target, pages[0]);
  const Dict& one = dict_of(target, pages[1]);
  EXPECT_EQ(*two.find("MediaBox"), Object(model::Array{0, 0, 200, 200}));
  const auto font = [&](const Dict& page) { return target.resolve(*page.find("Resources"))->as_dict()->find("Font")->as_dict()->find("F1")->as_ref(); };
  EXPECT_EQ(font(one), font(two));
  EXPECT_FALSE(one.contains("StructParents"));
  // The link to the other imported page follows it; the annotations sit on their new page.
  const auto annots = target.resolve(*one.find("Annots"));
  const auto link = target.resolve(annots->as_array()->at(0));
  EXPECT_EQ(link->as_dict()->find("Dest")->as_array()->front().as_ref()->number, pages[0]);
  EXPECT_FALSE(link->as_dict()->contains("StructParent"));

  // The imported field joins the form under a free name, with the source's default resources.
  const auto form = target.resolve(*(*target.catalog())->find("AcroForm"));
  const auto fields = form->as_dict()->find("Fields")->as_array();
  ASSERT_EQ(fields->size(), 2U);
  EXPECT_EQ(target.resolve(fields->at(1))->as_dict()->find("T")->as_string()->bytes, "name_2");
  EXPECT_TRUE(form->as_dict()->find("DR")->as_dict()->find("Font")->as_dict()->contains("Helv"));
  EXPECT_TRUE(form->as_dict()->contains("DA"));

  // Saved and read again, the imported streams are there.
  Document reopened = open(save(target));
  const std::vector<std::uint32_t> after = page_numbers(reopened);
  ASSERT_EQ(after.size(), 3U);
  EXPECT_EQ(content_of(reopened, after[1]), "BT /F1 12 Tf (source one) Tj ET");
}

TEST(Pages, ImportLeavesLinksToOtherPagesEmpty) {
  Document target = open(target_document());
  Document source = open(source_document());
  const std::vector<std::uint32_t> first = {0};
  const auto added = import_pages(target, source, first, 1);
  ASSERT_TRUE(added.has_value()) << added.error().message;
  EXPECT_EQ(page_numbers(target).back(), added->front().number);
  const auto annots = target.resolve(*dict_of(target, added->front().number).find("Annots"));
  const auto link = target.resolve(annots->as_array()->at(0));
  EXPECT_TRUE(link->as_dict()->find("Dest")->as_array()->front().is_null());  // page 2 was not imported

  const std::vector<std::uint32_t> past = {5};
  EXPECT_EQ(import_pages(target, source, past, 0).error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(import_pages(target, source, first, 9).error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(import_pages(target, target, first, 0).error().code, ErrorCode::kUnsupported);
}

TEST(Pages, ImportFromAnEncryptedDocument) {
  // The source's streams are decrypted on the way: the target is not encrypted.
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/r6_aes_256_empty_user.pdf");
  ASSERT_TRUE(file.has_value());
  auto source = Document::open(std::move(*file));
  ASSERT_TRUE(source.has_value()) << source.error().message;
  Document target = open(target_document());
  const std::vector<std::uint32_t> first = {0};
  ASSERT_TRUE(import_pages(target, *source, first, 0).has_value());
  Document reopened = open(save(target));
  EXPECT_EQ(content_of(reopened, page_numbers(reopened).front()), "BT /F1 12 Tf 72 720 Td (Hello encrypted) Tj ET");
}

TEST(Pages, ATreeFoundOnlyByRepairsIsNotEditedInPlace) {
  // tests/regression/pages (fuzz_pages): the catalog says 2 0 R, the file has 2 2 obj with a mangled
  // /Kids — the page is found by the repair layer only. Writing a tree under that root would not be
  // found again: refused before anything changes (a full save writes the tree anew first).
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/../regression/pages/root-read-only-through-repairs.pdf");
  ASSERT_TRUE(file.has_value());
  auto document = Document::open(std::move(*file));
  ASSERT_TRUE(document.has_value());
  ASSERT_EQ(document->page_refs()->size(), 1U);
  const std::vector<std::uint32_t> twice = {0, 0};
  EXPECT_EQ(arrange_pages(*document, twice).error().code, ErrorCode::kUnsupported);
  Document source = open(three_pages());
  const std::vector<std::uint32_t> first = {0};
  EXPECT_EQ(import_pages(*document, source, first, 0).error().code, ErrorCode::kUnsupported);
  EXPECT_FALSE(document->has_changes());
  // After a full save the tree is sound, and editable.
  Document saved = open(save(*document));
  EXPECT_TRUE(arrange_pages(saved, twice).has_value());
  EXPECT_EQ(saved.page_refs()->size(), 2U);
}

TEST(Pages, AFullSaveGivesAMissingTreeRootANewObject) {
  // tests/regression/pages (fuzz_pages): the catalog's /Pages object does not exist at all (its header
  // is damaged; even the scan does not find it) and the page is found through its /Parent. A full save
  // writes a new root at a new number and points the catalog to it: the output reads without repairs
  // — before, it had no page tree, and a compact save left the page out.
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/../regression/pages/missing-tree-root.pdf");
  ASSERT_TRUE(file.has_value());
  auto document = Document::open(std::move(*file));
  ASSERT_TRUE(document.has_value());
  ASSERT_EQ(document->page_refs()->size(), 1U);
  for (const bool compact : {false, true}) {
    SCOPED_TRACE(compact ? "compact" : "full");
    Document saved = open(save(*document, {.modification_date = std::nullopt, .drop_unreachable = compact}));
    EXPECT_EQ(saved.page_refs()->size(), 1U);
    EXPECT_TRUE(saved.repairs().empty());
  }
}

TEST(Pages, PlaceholderPagesAreMadeRealByAFullSaveFirst) {
  // tests/regression/pages (fuzz_pages): the tree lists 3 0 R, which does not exist; the repair keeps it
  // as a page so the numbering holds. An import would have given its new page that free number — the
  // tree then listed 3 0 R twice. Refused before any change; after a full save it is a blank page.
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/../regression/pages/placeholder-page.pdf");
  ASSERT_TRUE(file.has_value());
  auto document = Document::open(std::move(*file));
  ASSERT_TRUE(document.has_value());
  ASSERT_EQ(document->page_refs()->size(), 1U);
  Document source = open(three_pages());
  const std::vector<std::uint32_t> first = {0};
  EXPECT_EQ(import_pages(*document, source, first, 1).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(arrange_pages(*document, first).error().code, ErrorCode::kUnsupported);
  EXPECT_FALSE(document->has_changes());
  // Each repair is listed once, however often the tree is walked.
  const std::size_t notes = document->repairs().size();
  (void)document->page_refs();
  EXPECT_EQ(document->repairs().size(), notes);

  Document saved = open(save(*document));
  ASSERT_TRUE(import_pages(saved, source, first, 1).has_value());
  const std::vector<std::uint32_t> pages = page_numbers(saved);
  ASSERT_EQ(pages.size(), 2U);
  EXPECT_NE(pages[0], pages[1]);
}

TEST(Pages, AFullSaveListsFarApartNumbersCheaply) {
  // tests/regression/pages (fuzz_pages): the catalog is object 1666666666 of a 341-byte file. The cross-
  // reference table listed every number up to it — gigabytes; now one subsection per run of numbers.
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/../regression/pages/hostile-size.pdf");
  ASSERT_TRUE(file.has_value());
  auto document = Document::open(std::move(*file));
  ASSERT_TRUE(document.has_value());
  const std::string saved = save(*document);
  EXPECT_LT(saved.size(), 4096U);
  Document reopened = open(saved);
  EXPECT_TRUE(reopened.repairs().empty());
  EXPECT_EQ(reopened.page_refs()->size(), 1U);
}

TEST(Pages, EveryRegressionInputSavesSoundly) {
  // Every input of tests/regression/pages: a full save, plain and compact, reads back without repairs and
  // with the pages the damaged file was read with — what fuzz_pages checks before editing. (One of them,
  // catalog-is-the-page.pdf: the catalog object is also the one page; the page now gets a new number.)
  const std::filesystem::path dir = std::filesystem::path(CAPRIOLE_TEST_DATA_DIR).parent_path() / "regression" / "pages";
  std::size_t inputs = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    SCOPED_TRACE(entry.path().filename().string());
    ++inputs;
    auto file = FileByteSource::open(entry.path());
    ASSERT_TRUE(file.has_value());
    auto document = Document::open(std::move(*file));
    ASSERT_TRUE(document.has_value());
    const std::size_t pages = document->page_refs()->size();
    for (const bool compact : {false, true}) {
      Document saved = open(save(*document, {.modification_date = std::nullopt, .drop_unreachable = compact}));
      EXPECT_TRUE(saved.repairs().empty());
      EXPECT_EQ(saved.page_refs()->size(), pages);
    }
  }
  EXPECT_GE(inputs, 5U);
}

}  // namespace
}  // namespace capriole::edit
