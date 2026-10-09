// Signing documents and verifying their signatures (PLAN.md phase 8h).

#include "capriole/edit/sign.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/annotations.hpp"
#include "capriole/signature/signatures.hpp"
#include "capriole/writer/save.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using signature::Integrity;
using signature::SignatureInfo;
using signature::Trust;

const std::string kDir = std::string(CAPRIOLE_TEST_DATA_DIR) + "/signing/";
constexpr const char* kDate = "D:20261004150000+02'00'";

std::vector<std::byte> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::memcpy(out.data(), chars.data(), chars.size());
  return out;
}

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  std::memcpy(out.data(), text.data(), text.size());
  return out;
}

Document open(std::vector<std::byte> data) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(std::move(data)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

crypto::Credentials credentials(const char* name) {
  auto c = crypto::read_pkcs12(read_file(kDir + name), "capriole");
  EXPECT_TRUE(c.has_value());
  if (!c) {
    std::abort();
  }
  return std::move(*c);
}

// Two pages 300 × 200 with a line of text.
std::vector<std::byte> plain_pdf() {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Contents 4 0 R>>");
  b.add_stream(4, "0 0 1 rg 20 20 100 50 re f");
  b.add_object(5, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Rotate 90>>");
  b.finish_table("/Root 1 0 R");
  return bytes(b.str());
}

// A request: the date and, when given, a rectangle (visible), a page, a field name.
SignRequest request(std::optional<graphics::Rect> rect = std::nullopt, std::uint32_t page = 0, const std::string& field = {}, const std::string& date = kDate) {
  SignRequest r;
  r.date = date;
  r.page = page;
  r.rect = rect;
  r.field_name = field;
  return r;
}

std::vector<std::byte> sign(std::vector<std::byte> pdf, const char* key, const SignRequest& request) {
  Document document = open(std::move(pdf));
  auto prepared = prepare_signature(document, credentials(key), request);
  EXPECT_TRUE(prepared.has_value()) << (prepared ? "" : prepared.error().message);
  if (!prepared) {
    std::abort();
  }
  auto signed_file = finish_signature(std::move(*prepared));
  EXPECT_TRUE(signed_file.has_value()) << (signed_file ? "" : signed_file.error().message);
  return signed_file ? std::move(*signed_file) : std::vector<std::byte>();
}

std::vector<SignatureInfo> signatures(const std::vector<std::byte>& pdf, bool trust = true) {
  Document document = open(pdf);
  const auto anchors = read_file(kDir + "ca.pem");
  signature::VerifyOptions options;
  if (trust) {
    options.trust_anchors = anchors;
  }
  auto list = signature::list_signatures(document, options);
  EXPECT_TRUE(list.has_value()) << (list ? "" : list.error().message);
  return list ? std::move(*list) : std::vector<SignatureInfo>();
}

TEST(Sign, SignsVisiblyAndTheSignatureVerifies) {
  const auto original = plain_pdf();
  SignRequest visible = request(graphics::Rect{150, 120, 290, 180});
  visible.reason = "Approvazione";
  visible.location = "Milano";
  for (const char* key : {"rsa.p12", "ec.p12"}) {
    const auto signed_file = sign(original, key, visible);
    // An incremental save: the original bytes first, unchanged.
    ASSERT_GT(signed_file.size(), original.size());
    EXPECT_TRUE(std::equal(original.begin(), original.end(), signed_file.begin())) << key;
    // Deterministic.
    EXPECT_EQ(sign(original, key, visible), signed_file) << key;

    const auto list = signatures(signed_file);
    ASSERT_EQ(list.size(), 1U) << key;
    const SignatureInfo& s = list[0];
    EXPECT_EQ(s.integrity, Integrity::kValid) << key << ": " << s.problem;
    EXPECT_EQ(s.field_name, "Signature1");
    EXPECT_EQ(s.sub_filter, "ETSI.CAdES.detached");
    EXPECT_EQ(s.level, "B-B");
    EXPECT_TRUE(s.covers_whole_file);
    EXPECT_EQ(s.reason, "Approvazione");
    EXPECT_EQ(s.location, "Milano");
    EXPECT_EQ(s.date, kDate);
    EXPECT_EQ(s.signing_time, kDate);
    EXPECT_EQ(s.name, s.signer.value_or(crypto::CertificateInfo{}).common_name);
    EXPECT_EQ(s.trust, Trust::kTrusted);
    EXPECT_EQ(s.page, 0U);
    const graphics::Rect shown = s.rect.value_or(graphics::Rect{});
    EXPECT_NEAR(shown.x0, 150, 0.01);
    EXPECT_NEAR(shown.y1, 180, 0.01);
  }
}

TEST(Sign, AnInvisibleSignatureOnARotatedPage) {
  const auto signed_file = sign(plain_pdf(), "ec.p12", request(std::nullopt, 1, "Firma"));
  const auto list = signatures(signed_file, false);
  ASSERT_EQ(list.size(), 1U);
  EXPECT_EQ(list[0].integrity, Integrity::kValid) << list[0].problem;
  EXPECT_EQ(list[0].field_name, "Firma");
  EXPECT_EQ(list[0].trust, Trust::kNotChecked);
  EXPECT_FALSE(list[0].rect.has_value());
  EXPECT_FALSE(list[0].page.has_value());
}

TEST(Sign, AChangedByteBreaksTheSignature) {
  auto signed_file = sign(plain_pdf(), "rsa.p12", request(graphics::Rect{10, 10, 100, 50}));
  // A byte of the page's content ("re f" → "re F"), inside the signed range.
  const std::string text(crypto::der::chars_of(signed_file));
  const auto at = text.find("re f");
  ASSERT_NE(at, std::string::npos);
  signed_file[at + 3] = std::byte{'F'};
  const auto list = signatures(signed_file);
  ASSERT_EQ(list.size(), 1U);
  EXPECT_EQ(list[0].integrity, Integrity::kInvalid);
  EXPECT_NE(list[0].problem.find("changed"), std::string::npos) << list[0].problem;
}

TEST(Sign, ASecondSignatureAndALaterEditKeepTheFirstValid) {
  const auto once = sign(plain_pdf(), "rsa.p12", request(graphics::Rect{10, 10, 100, 50}));
  const auto twice = sign(once, "ec.p12", request(graphics::Rect{150, 10, 290, 50}, 0, {}, "D:20261004160000Z"));
  // A note added afterwards, saved incrementally.
  Document document = open(twice);
  AnnotationSpec note;
  note.kind = AnnotationKind::kNote;
  note.rect = graphics::Rect{200, 100, 220, 120};
  ASSERT_TRUE(add_annotation(document, 0, note).has_value());
  writer::MemorySink sink;
  ASSERT_TRUE(writer::save_incremental(document, sink).has_value());

  const auto list = signatures(sink.data());
  ASSERT_EQ(list.size(), 2U);
  EXPECT_EQ(list[0].field_name, "Signature1");
  EXPECT_EQ(list[1].field_name, "Signature2");
  for (const SignatureInfo& s : list) {
    EXPECT_EQ(s.integrity, Integrity::kValid) << s.field_name << ": " << s.problem;
    EXPECT_FALSE(s.covers_whole_file) << s.field_name;
  }
  EXPECT_EQ(list[0].signed_bytes, once.size());
  EXPECT_EQ(list[1].signed_bytes, twice.size());
}

TEST(Sign, RefusesWhatItCannotSign) {
  Document document = open(plain_pdf());
  // No date; a field name in use; a page that is not there; a rectangle of no area.
  EXPECT_FALSE(prepare_signature(document, credentials("rsa.p12"), request(std::nullopt, 0, {}, "")).has_value());
  EXPECT_FALSE(prepare_signature(document, credentials("rsa.p12"), request(std::nullopt, 7)).has_value());
  EXPECT_FALSE(prepare_signature(document, credentials("rsa.p12"), request(graphics::Rect{5, 5, 5, 5})).has_value());
  const auto signed_file = sign(plain_pdf(), "rsa.p12", request());
  Document again = open(signed_file);
  EXPECT_FALSE(prepare_signature(again, credentials("ec.p12"), request(std::nullopt, 0, "Signature1")).has_value());

  // An encrypted document.
  Document encrypted = open(read_file(std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/r4_aes_128_empty_user.pdf"));
  auto refused = prepare_signature(encrypted, credentials("rsa.p12"), request());
  ASSERT_FALSE(refused.has_value());
  EXPECT_EQ(refused.error().code, ErrorCode::kUnsupported);
}

TEST(Sign, TakesOnlyItsOwnTimeStamp) {
  Document document = open(plain_pdf());
  SignRequest stamped = request();
  stamped.timestamp = true;
  auto prepared = prepare_signature(document, credentials("rsa.p12"), stamped);
  ASSERT_TRUE(prepared.has_value()) << prepared.error().message;
  EXPECT_FALSE(prepared->timestamp_request.empty());
  // A response for other bytes ("capriole") is refused; without one, the signature is B-B.
  EXPECT_FALSE(finish_signature(*prepared, read_file(kDir + "timestamp.tsr")).has_value());
  auto signed_file = finish_signature(std::move(*prepared));
  ASSERT_TRUE(signed_file.has_value());
  const auto list = signatures(*signed_file);
  ASSERT_EQ(list.size(), 1U);
  EXPECT_EQ(list[0].integrity, Integrity::kValid) << list[0].problem;
  EXPECT_EQ(list[0].level, "B-B");
}

// An ECDSA value as PKCS#11 gives it: r and s, each as wide as the curve's order.
std::vector<std::byte> raw_of(const std::vector<std::byte>& der, std::size_t width) {
  crypto::der::Reader outer(der);
  auto sequence = outer.next();
  auto inner = crypto::der::Reader::into(*sequence);
  std::vector<std::byte> out;
  for (int i = 0; i < 2; ++i) {
    auto n = inner->expect(crypto::der::kInteger);
    std::span<const std::byte> m = n->content;
    while (m.size() > width && m[0] == std::byte{0}) {
      m = m.subspan(1);
    }
    out.insert(out.end(), width - m.size(), std::byte{0});
    out.insert(out.end(), m.begin(), m.end());
  }
  return out;
}

TEST(Sign, AValueMadeOutsideGivesTheSameFile) {
  SignRequest stamped = request(graphics::Rect{150, 120, 290, 180});
  stamped.timestamp = true;
  for (const char* key : {"rsa.p12", "ec.p12"}) {
    const auto c = credentials(key);
    Document inside = open(plain_pdf());
    auto expected = prepare_signature(inside, c, stamped);
    ASSERT_TRUE(expected.has_value()) << expected.error().message;

    // The key elsewhere: only its certificates go to the engine; the value is made here, by the key.
    Document outside = open(plain_pdf());
    const SignerCertificates certificates{.certificate = {c.certificate().begin(), c.certificate().end()}, .others = c.chain()};
    auto prepared = prepare_signature(outside, certificates, stamped);
    ASSERT_TRUE(prepared.has_value()) << prepared.error().message;
    EXPECT_TRUE(prepared->cms.signature.empty());
    EXPECT_TRUE(prepared->timestamp_request.empty());
    EXPECT_EQ(finish_signature(*prepared).error().code, ErrorCode::kUnsupported) << key;  // no value yet
    EXPECT_EQ(crypto::to_be_signed(prepared->cms), crypto::to_be_signed(expected->cms)) << key;
    auto made = prepared->cms;
    ASSERT_TRUE(crypto::sign_with(made, c).has_value());
    // ECDSA as r‖s, as a smart card gives it.
    const auto value = std::string_view(key) == "ec.p12" ? raw_of(made.signature, 32) : made.signature;
    ASSERT_TRUE(set_signature_value(*prepared, value).has_value()) << key;
    EXPECT_EQ(prepared->timestamp_request, expected->timestamp_request) << key;
    EXPECT_EQ(finish_signature(*prepared).value(), finish_signature(*expected).value()) << key;
  }
}

TEST(Sign, AValueOfAnotherKeyIsRefused) {
  const auto rsa = credentials("rsa.p12");
  const auto ec = credentials("ec.p12");
  Document document = open(plain_pdf());
  const SignerCertificates certificates{.certificate = {rsa.certificate().begin(), rsa.certificate().end()}, .others = {}};
  auto prepared = prepare_signature(document, certificates, request());
  ASSERT_TRUE(prepared.has_value()) << prepared.error().message;
  auto other = prepared->cms;
  ASSERT_TRUE(crypto::sign_with(other, ec).has_value());
  for (const auto& value : {other.signature, std::vector<std::byte>(256, std::byte{1}), std::vector<std::byte>()}) {
    auto refused = set_signature_value(*prepared, value);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::kCorruptData);
  }
  // A certificate that is not one.
  const SignerCertificates broken{.certificate = bytes("not a certificate"), .others = {}};
  EXPECT_FALSE(prepare_signature(document, broken, request()).has_value());
}

TEST(Signatures, ReportsWhatCannotBeRead) {
  // A signature whose /ByteRange runs past the file, and one whose /V is not a dictionary.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[5 0 R 6 0 R]>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]/Annots[5 0 R 6 0 R]>>");
  b.add_object(4, "<</Type/Sig/Filter/Adobe.PPKLite/SubFilter/adbe.pkcs7.detached/ByteRange[0 10 20 999999]/Contents<3000>>>");
  b.add_object(5, "<</Type/Annot/Subtype/Widget/FT/Sig/T(A)/V 4 0 R/Rect[0 0 0 0]/P 3 0 R>>");
  b.add_object(6, "<</Type/Annot/Subtype/Widget/FT/Sig/T(B)/V 42/Rect[0 0 0 0]/P 3 0 R>>");
  b.finish_table("/Root 1 0 R");
  const auto list = signatures(bytes(b.str()));
  ASSERT_EQ(list.size(), 2U);
  for (const SignatureInfo& s : list) {
    EXPECT_EQ(s.integrity, Integrity::kInvalid) << s.field_name;
    EXPECT_FALSE(s.problem.empty());
  }
}

}  // namespace
}  // namespace capriole::edit
