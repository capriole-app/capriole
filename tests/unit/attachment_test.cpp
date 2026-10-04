#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/attachments.hpp"
#include "capriole/edit/facturx.hpp"
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

std::string text(const std::vector<std::byte>& data) {
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

std::string save(Document& document) {
  writer::MemorySink sink;
  const auto report = writer::save_incremental(document, sink);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  return text(sink.data());
}

// One page; catalog_extra: more catalog entries; objects: more objects.
std::string pdf(const std::string& catalog_extra = "", const std::vector<std::pair<std::uint32_t, std::string>>& objects = {},
                const std::string& page_extra = "") {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R" + catalog_extra + ">>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]" + page_extra + ">>");
  for (const auto& [number, body] : objects) {
    if (body.starts_with("stream:")) {
      b.add_stream(number, body.substr(7), "/Type/EmbeddedFile");
    } else {
      b.add_object(number, body);
    }
  }
  b.finish_table("/Size 30/Root 1 0 R");
  return b.str();
}

AttachmentSpec spec(const std::string& file_name, const std::string& relationship = "Unspecified") {
  AttachmentSpec s;
  s.file_name = file_name;
  s.relationship = relationship;
  return s;
}

const Dict& dict_of(Document& document, std::uint32_t number) {
  static const Dict kEmpty;
  auto object = document.get(number);
  return object && (*object)->as_dict() != nullptr ? *(*object)->as_dict() : kEmpty;
}

TEST(Attachments, AddListReadRemove) {
  Document document = open(pdf());
  EXPECT_TRUE(list_attachments(document)->empty());
  AttachmentSpec first = spec("note.txt", "Source");
  first.description = "Le note";
  first.mime_type = "text/plain";
  first.creation_date = "D:20261003120000Z";
  first.modification_date = "D:20261003120000Z";
  const auto a = add_attachment(document, first, bytes("ciao, mondo"));
  ASSERT_TRUE(a.has_value()) << a.error().message;
  const auto b = add_attachment(document, spec("note.txt"), bytes(std::string(10000, 'x')));  // same name: "note.txt (2)"
  ASSERT_TRUE(b.has_value());
  const auto c = add_attachment(document, spec("a.bin"), bytes(""));
  ASSERT_TRUE(c.has_value());

  auto list = list_attachments(document);
  ASSERT_TRUE(list.has_value());
  ASSERT_EQ(list->size(), 3U);
  // In key order.
  EXPECT_EQ((*list)[0].name, "a.bin");
  EXPECT_EQ((*list)[1].name, "note.txt");
  EXPECT_EQ((*list)[2].name, "note.txt (2)");
  const AttachmentInfo& note = (*list)[1];
  EXPECT_EQ(note.spec, *a);
  EXPECT_EQ(note.file_name, "note.txt");
  EXPECT_EQ(note.description, "Le note");
  EXPECT_EQ(note.mime_type, "text/plain");
  EXPECT_EQ(note.size, 11U);
  EXPECT_EQ(note.modification_date, "D:20261003120000Z");
  EXPECT_EQ(note.relationship, "Source");
  EXPECT_TRUE(note.associated);
  EXPECT_FALSE(note.page.has_value());

  auto data = attachment_data(document, *b);
  ASSERT_TRUE(data.has_value());
  EXPECT_EQ(text(data->bytes), std::string(10000, 'x'));
  EXPECT_EQ(data->checksum_matches, true);
  EXPECT_EQ(attachment_data(document, *c)->bytes.size(), 0U);

  // Read back after an incremental save.
  Document reread = open(save(document));
  ASSERT_EQ(list_attachments(reread)->size(), 3U);
  EXPECT_EQ(text(attachment_data(reread, *a)->bytes), "ciao, mondo");
  EXPECT_EQ(dict_of(reread, 1).find("AF")->as_array()->size(), 3U);

  ASSERT_TRUE(remove_attachment(reread, *a).has_value());
  ASSERT_TRUE(remove_attachment(reread, *b).has_value());
  ASSERT_TRUE(remove_attachment(reread, *c).has_value());
  EXPECT_TRUE(list_attachments(reread)->empty());
  EXPECT_EQ(dict_of(reread, 1).find("AF"), nullptr);
  EXPECT_EQ(dict_of(reread, 1).find("Names"), nullptr);  // nothing left in it
  EXPECT_EQ(remove_attachment(reread, *a).error().code, ErrorCode::kOutOfRange);
}

TEST(Attachments, RefusalsChangeNothing) {
  Document document = open(pdf());
  EXPECT_EQ(add_attachment(document, spec(""), bytes("x")).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(add_attachment(document, spec("x", "Invoice"), bytes("x")).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(attachment_data(document, {1, 0}).error().code, ErrorCode::kOutOfRange);
  EXPECT_FALSE(document.has_changes());
}

TEST(Attachments, TreesWithKidsAreReadAndWrittenFlat) {
  // An existing tree with kids, /Names direct in the catalog, and a cycle back to the root.
  Document document = open(pdf("/Names<</EmbeddedFiles 10 0 R>>", {{10, "<</Kids[11 0 R 12 0 R]>>"},
                                                                   {11, "<</Limits[(a)(b)]/Names[(a)20 0 R(b)21 0 R]>>"},
                                                                   {12, "<</Limits[(z)(z)]/Names[(z)22 0 R]/Kids[10 0 R]>>"},
                                                                   {20, "<</Type/Filespec/F(a.txt)/EF<</F 23 0 R>>>>"},
                                                                   {21, "<</Type/Filespec/F(b.txt)>>"},  // no embedded file: a reference elsewhere, left out
                                                                   {22, "<</Type/Filespec/UF(z.txt)/EF<</F 24 0 R>>>>"},
                                                                   {23, "stream:AAA"},
                                                                   {24, "stream:ZZZ"}}));
  auto list = list_attachments(document);
  ASSERT_TRUE(list.has_value());
  ASSERT_EQ(list->size(), 2U);
  EXPECT_EQ((*list)[0].file_name, "a.txt");
  EXPECT_EQ((*list)[1].file_name, "z.txt");
  EXPECT_FALSE((*list)[0].associated);
  EXPECT_FALSE(attachment_data(document, {20, 0})->checksum_matches.has_value());

  const auto added = add_attachment(document, spec("m.txt"), bytes("M"));
  ASSERT_TRUE(added.has_value());
  // The root, its own object, now holds every entry, sorted.
  const Object* names = dict_of(document, 10).find("Names");
  ASSERT_NE(names, nullptr);
  ASSERT_EQ(names->as_array()->size(), 8U);  // a, b (kept: it is an entry), m, z
  EXPECT_EQ((*names->as_array())[4].as_string()->bytes, "m.txt");
  EXPECT_EQ(dict_of(document, 10).find("Kids"), nullptr);
  EXPECT_EQ(list_attachments(document)->size(), 3U);
}

TEST(Attachments, ReferencesThatReachNothingAreReplaced) {
  // Regression (fuzz_attachments): the catalog's /Names is 10 0 R, but object 10 has generation 1 — the
  // reference reads as null (§7.3.10). The new tree used to be written into object 10, out of reach:
  // added, never listed. Same for /AF.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/Names 10 0 R/AF 11 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>");
  b.add_object(10, "<</Dests<<>>>>", 1);
  b.add_object(11, "[]", 1);
  b.finish_table("/Size 12/Root 1 0 R");
  Document document = open(b.str());
  const auto added = add_attachment(document, spec("x.txt"), bytes("x"));
  ASSERT_TRUE(added.has_value());
  Document reread = open(save(document));
  const auto list = list_attachments(reread);
  ASSERT_EQ(list->size(), 1U);
  EXPECT_TRUE(list->front().associated);
  EXPECT_NE(dict_of(reread, 1).find("Names")->as_dict(), nullptr);  // replaced by the tree, in the catalog
}

TEST(Attachments, FileAttachmentAnnotationsAreListedWithTheirPage) {
  Document document = open(pdf("",
                               {{10, "<</Type/Annot/Subtype/FileAttachment/Rect[0 0 10 10]/FS 11 0 R>>"},
                                {11, "<</Type/Filespec/F(allegato.pdf)/EF<</F 12 0 R>>/Params<</CheckSum<00>>>>>"},
                                {12, "stream:%PDF-"}},
                               "/Annots[10 0 R]"));
  auto list = list_attachments(document);
  ASSERT_EQ(list->size(), 1U);
  EXPECT_EQ(list->front().page, 0U);
  EXPECT_EQ(list->front().spec, (ObjectRef{11, 0}));
  EXPECT_EQ(text(attachment_data(document, {11, 0})->bytes), "%PDF-");
  EXPECT_EQ(remove_attachment(document, {11, 0}).error().code, ErrorCode::kUnsupported);
}

TEST(Attachments, ChecksumsThatDoNotMatchAreReported) {
  Document document = open(
      pdf("/Names<</EmbeddedFiles<</Names[(x)10 0 R]>>>>",
          {{10, "<</Type/Filespec/F(x)/EF<</F 11 0 R>>>>"}, {11, "<</Type/EmbeddedFile/Length 3/Params<</CheckSum<00112233445566778899AABBCCDDEEFF>>>>>"}}));
  // Object 11 is a dictionary, not a stream: no embedded file, not listed.
  EXPECT_TRUE(list_attachments(document)->empty());
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/Names<</EmbeddedFiles<</Names[(x)10 0 R]>>>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>");
  b.add_object(10, "<</Type/Filespec/F(x)/EF<</F 11 0 R>>>>");
  b.add_stream(11, "abc", "/Type/EmbeddedFile/Params<</CheckSum<00112233445566778899AABBCCDDEEFF>>>");
  b.finish_table("/Size 12/Root 1 0 R");
  Document other = open(b.str());
  EXPECT_EQ(attachment_data(other, {10, 0})->checksum_matches, false);
}

std::string invoice(const std::string& guideline) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<rsm:CrossIndustryInvoice xmlns:rsm=\"urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100\" "
         "xmlns:ram=\"urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100\">\n"
         "<rsm:ExchangedDocumentContext><ram:GuidelineSpecifiedDocumentContextParameter>\n<ram:ID>" +
         guideline + "</ram:ID>\n</ram:GuidelineSpecifiedDocumentContextParameter></rsm:ExchangedDocumentContext>\n</rsm:CrossIndustryInvoice>\n";
}

TEST(Facturx, ProfilesFromTheGuideline) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"urn:factur-x.eu:1p0:minimum", "MINIMUM"},
      {"urn:factur-x.eu:1p0:basicwl", "BASIC WL"},
      {"urn:cen.eu:en16931:2017#compliant#urn:factur-x.eu:1p0:basic", "BASIC"},
      {"urn:cen.eu:en16931:2017", "EN 16931"},
      {"urn:cen.eu:en16931:2017#conformant#urn:factur-x.eu:1p0:extended", "EXTENDED"},
      {"urn:cen.eu:en16931:2017#compliant#urn:xeinkauf.de:kosit:xrechnung_3.0", "XRECHNUNG"},
  };
  for (const auto& [guideline, level] : cases) {
    EXPECT_EQ(facturx_profile(bytes(invoice(guideline))).value_or("?"), level) << guideline;
  }
  EXPECT_EQ(facturx_profile(bytes(invoice("urn:something:else"))).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(facturx_profile(bytes("<Invoice>UBL</Invoice>")).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(facturx_profile(bytes("")).error().code, ErrorCode::kUnsupported);
}

TEST(Facturx, EmbeddedWithItsMetadataAndReplacedOnTheSecondTime) {
  Document document = open(pdf());
  EXPECT_FALSE(find_facturx(document)->has_value());
  const auto result = embed_facturx(document, bytes(invoice("urn:cen.eu:en16931:2017")), "D:20261003120000Z");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->conformance_level, "EN 16931");
  EXPECT_FALSE(result->pdfa3);  // not PDF/A-3, and not declared so

  Document reread = open(save(document));
  const auto found = find_facturx(reread);
  ASSERT_TRUE(found && found->has_value());
  EXPECT_EQ((*found)->file_name, "factur-x.xml");
  EXPECT_EQ((*found)->conformance_level, "EN 16931");
  EXPECT_EQ((*found)->version, "1.0");
  EXPECT_FALSE((*found)->pdfa3);
  const auto list = list_attachments(reread);
  ASSERT_EQ(list->size(), 1U);
  EXPECT_EQ(list->front().relationship, "Alternative");
  EXPECT_EQ(list->front().mime_type, "text/xml");
  EXPECT_TRUE(list->front().associated);

  // Again, as MINIMUM: one invoice, its relationship Data, one fx description, one extension schema.
  EXPECT_EQ(embed_facturx(reread, bytes(invoice("urn:factur-x.eu:1p0:minimum")), "").error().code, ErrorCode::kUnsupported);  // no date
  ASSERT_TRUE(embed_facturx(reread, bytes(invoice("urn:factur-x.eu:1p0:minimum")), "D:20261003130000Z").has_value());
  const auto again = list_attachments(reread);
  ASSERT_EQ(again->size(), 1U);
  EXPECT_EQ(again->front().relationship, "Data");
  auto catalog = reread.catalog();
  auto metadata = reread.resolve(*(*catalog)->find("Metadata"));
  const std::string xmp = text(*reread.decode_stream(*metadata->as_stream()));
  const auto count = [&](const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = xmp.find(what); at != std::string::npos; at = xmp.find(what, at + 1)) {
      ++n;
    }
    return n;
  };
  EXPECT_EQ(count("<fx:ConformanceLevel>MINIMUM</fx:ConformanceLevel>"), 1U);
  EXPECT_EQ(count("<fx:ConformanceLevel>"), 1U);
  EXPECT_EQ(count("<pdfaSchema:prefix>fx</pdfaSchema:prefix>"), 1U);
  EXPECT_EQ(count("pdfaid:"), 0U);  // never declared PDF/A-3 here
}

TEST(Facturx, PdfA3StaysPdfA3AndItsMetadataIsKept) {
  const std::string packet =
      "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" pdfaid:part=\"3\" pdfaid:conformance=\"B\"/>"
      "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Fattura</dc:title></rdf:Description>"
      "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/Metadata 4 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>");
  b.add_stream(4, packet, "/Type/Metadata/Subtype/XML");
  b.finish_table("/Size 5/Root 1 0 R");
  Document document = open(b.str());
  const auto result = embed_facturx(document, bytes(invoice("urn:factur-x.eu:1p0:basicwl")), "D:20261003120000Z");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->pdfa3);
  auto metadata = document.get(4);  // written over the same object
  const std::string xmp = text(*document.decode_stream(*(*metadata)->as_stream()));
  EXPECT_NE(xmp.find("pdfaid:part=\"3\""), std::string::npos);
  EXPECT_NE(xmp.find("<dc:title>Fattura</dc:title>"), std::string::npos);
  EXPECT_NE(xmp.find("<fx:ConformanceLevel>BASIC WL</fx:ConformanceLevel>"), std::string::npos);
  EXPECT_LT(xmp.find("<fx:ConformanceLevel>"), xmp.find("</rdf:RDF>"));
  EXPECT_TRUE((*find_facturx(document))->pdfa3);
}

TEST(Facturx, AMetadataReferenceThatReachesNothingIsReplaced) {
  // /Metadata 4 0 R, object 4 of generation 1: null (§7.3.10). The packet goes to a new object.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/Metadata 4 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>");
  b.add_object(4, "<<>>", 1);
  b.finish_table("/Size 5/Root 1 0 R");
  Document document = open(b.str());
  ASSERT_TRUE(embed_facturx(document, bytes(invoice("urn:cen.eu:en16931:2017")), "D:20261003120000Z").has_value());
  Document reread = open(save(document));
  EXPECT_EQ((*find_facturx(reread))->conformance_level, "EN 16931");
}

TEST(Facturx, NotAnInvoiceChangesNothing) {
  Document document = open(pdf());
  EXPECT_EQ(embed_facturx(document, bytes("<html/>"), "D:20261003120000Z").error().code, ErrorCode::kUnsupported);
  EXPECT_FALSE(document.has_changes());
}

}  // namespace
}  // namespace capriole::edit
