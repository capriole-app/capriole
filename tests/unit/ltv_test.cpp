// Long-term validation: revocation checked when verifying, the /DSS written, document time stamps
// (PLAN.md phase 11b), with the fixtures of tests/data/signing/make_ltv_fixtures.sh.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/dss.hpp"
#include "capriole/edit/sign.hpp"
#include "capriole/model/object.hpp"
#include "capriole/signature/signatures.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using signature::Revocation;
using signature::SignatureInfo;

const std::string kDir = std::string(CAPRIOLE_TEST_DATA_DIR) + "/signing/";
constexpr const char* kDate = "D:20261004150000Z";

std::vector<std::byte> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::memcpy(out.data(), chars.data(), chars.size());
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

std::vector<std::byte> plain_pdf() {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]>>");
  b.finish_table("/Root 1 0 R");
  const std::string text = b.str();
  std::vector<std::byte> out(text.size());
  std::memcpy(out.data(), text.data(), text.size());
  return out;
}

std::vector<std::byte> signed_by(const char* p12) {
  Document document = open(plain_pdf());
  auto credentials = crypto::read_pkcs12(read_file(kDir + p12), "capriole");
  EXPECT_TRUE(credentials.has_value());
  SignRequest request;
  request.date = kDate;
  auto prepared = prepare_signature(document, *credentials, request);
  EXPECT_TRUE(prepared.has_value()) << (prepared ? "" : prepared.error().message);
  auto file = finish_signature(std::move(*prepared));
  EXPECT_TRUE(file.has_value());
  return file ? std::move(*file) : std::vector<std::byte>();
}

std::vector<SignatureInfo> verified(const std::vector<std::byte>& pdf, const signature::ValidationData& fetched = {}) {
  Document document = open(pdf);
  const auto anchors = read_file(kDir + "ca.pem");
  signature::VerifyOptions options;
  options.trust_anchors = anchors;
  options.certificates = fetched.certificates;
  options.ocsp_responses = fetched.ocsp_responses;
  options.crls = fetched.crls;
  auto list = signature::list_signatures(document, options);
  EXPECT_TRUE(list.has_value());
  return list ? std::move(*list) : std::vector<SignatureInfo>();
}

TEST(Ltv, WhatASignatureNeedsThenItsDssMakesItBLt) {
  const auto signed_file = signed_by("ltv.p12");
  ASSERT_EQ(verified(signed_file).at(0).revocation, Revocation::kNotChecked);

  Document document = open(signed_file);
  auto needs = validation_needs(document);
  ASSERT_TRUE(needs.has_value()) << needs.error().message;
  ASSERT_EQ(needs->size(), 1U);  // the signer's certificate; the CA is the root
  const NeededCertificate& need = needs->front();
  EXPECT_EQ(need.subject, "Luca Verdi (test)");
  ASSERT_TRUE(need.issuer.has_value());
  EXPECT_FALSE(need.ocsp_request.empty());
  EXPECT_EQ(need.urls.ocsp, std::vector<std::string>{"http://127.0.0.1:9/ocsp"});
  EXPECT_FALSE(need.answered);

  signature::ValidationData data;
  data.ocsp_responses.push_back(read_file(kDir + "ocsp-ltv.der"));
  auto result = add_validation_data(document, data, "D:20261009120000Z");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->ocsp_responses, 1U);
  EXPECT_EQ(result->certificates, 2U);                                                    // the signer's and the CA's
  EXPECT_TRUE(std::equal(signed_file.begin(), signed_file.end(), result->file.begin()));  // appended

  const auto list = verified(result->file);
  ASSERT_EQ(list.size(), 1U);
  EXPECT_EQ(list[0].integrity, signature::Integrity::kValid) << list[0].problem;
  EXPECT_EQ(list[0].revocation, Revocation::kGood) << list[0].revocation_problem;
  EXPECT_TRUE(list[0].revocation_in_file);
  EXPECT_EQ(list[0].level, "B-LT");

  // Asked again: answered. Added again: each item once.
  Document again = open(result->file);
  auto answered = validation_needs(again);
  ASSERT_TRUE(answered.has_value());
  EXPECT_TRUE(answered->front().answered);
  auto twice = add_validation_data(again, data, "D:20261009120000Z");
  ASSERT_TRUE(twice.has_value()) << twice.error().message;
  EXPECT_EQ(twice->ocsp_responses, 1U);
}

// The /VRI dictionary of a document's /DSS.
model::Dict vri_of(Document& document) {
  auto catalog = document.catalog();
  const model::Object* dss_entry = catalog ? (*catalog)->find("DSS") : nullptr;
  auto dss = dss_entry != nullptr ? document.resolve(*dss_entry) : Result<model::Object>(model::Object());
  const model::Object* vri = dss && dss->as_dict() != nullptr ? dss->as_dict()->find("VRI") : nullptr;
  auto resolved = vri != nullptr ? document.resolve(*vri) : Result<model::Object>(model::Object());
  return resolved && resolved->as_dict() != nullptr ? *resolved->as_dict() : model::Dict();
}

TEST(Ltv, ASignaturesVriEntryIsNotChangedByLaterData) {
  Document document = open(signed_by("ltv.p12"));
  signature::ValidationData ocsp;
  ocsp.ocsp_responses.push_back(read_file(kDir + "ocsp-ltv.der"));
  auto first = add_validation_data(document, ocsp, kDate);
  ASSERT_TRUE(first.has_value()) << first.error().message;
  Document once = open(first->file);
  const model::Dict before = vri_of(once);
  ASSERT_EQ(before.size(), 1U);

  // More data later (a CRL that is about the chain too): the entry stays as it was — a verifier takes a
  // changed one for tampering (pyHanko's difference analysis does).
  signature::ValidationData crl;
  crl.crls.push_back(read_file(kDir + "ca.crl"));
  auto second = add_validation_data(once, crl, kDate);
  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_EQ(second->crls, 1U);
  Document twice = open(second->file);
  EXPECT_EQ(vri_of(twice), before);
}

TEST(Ltv, ARevokedSignerIsSaidRevoked) {
  const auto signed_file = signed_by("revoked.p12");
  for (const char* answer : {"ocsp-revoked.der", "ca.crl"}) {
    signature::ValidationData fetched;
    (std::string(answer).ends_with(".crl") ? fetched.crls : fetched.ocsp_responses).push_back(read_file(kDir + answer));
    const auto list = verified(signed_file, fetched);
    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list[0].revocation, Revocation::kRevoked) << answer;
    EXPECT_EQ(list[0].revoked_at, "D:20260101000000Z") << answer;
    EXPECT_FALSE(list[0].revocation_in_file);
  }
  // The CRL says the other signer is good; an answer about someone else says nothing.
  signature::ValidationData crl;
  crl.crls.push_back(read_file(kDir + "ca.crl"));
  EXPECT_EQ(verified(signed_by("ltv.p12"), crl).at(0).revocation, Revocation::kGood);
  signature::ValidationData other;
  other.ocsp_responses.push_back(read_file(kDir + "ocsp-ltv.der"));
  EXPECT_EQ(verified(signed_file, other).at(0).revocation, Revocation::kUnknown);
}

TEST(Ltv, WhatIsNotValidationDataIsRefused) {
  Document unsigned_document = open(plain_pdf());
  EXPECT_EQ(add_validation_data(unsigned_document, {}, kDate).error().code, ErrorCode::kUnsupported);
  Document document = open(signed_by("ltv.p12"));
  signature::ValidationData bad;
  bad.ocsp_responses.push_back(read_file(kDir + "ca.crl"));
  EXPECT_EQ(add_validation_data(document, bad, kDate).error().code, ErrorCode::kSyntax);
  bad = {};
  bad.crls.push_back(read_file(kDir + "ocsp-ltv.der"));
  EXPECT_EQ(add_validation_data(document, bad, kDate).error().code, ErrorCode::kSyntax);
  bad = {};
  bad.certificates.push_back(read_file(kDir + "ocsp-ltv.der"));
  EXPECT_EQ(add_validation_data(document, bad, kDate).error().code, ErrorCode::kSyntax);
}

TEST(Ltv, ADocumentTimeStampTakesOnlyItsOwnToken) {
  Document document = open(signed_by("ltv.p12"));
  auto prepared = prepare_document_timestamp(document, {.date = kDate, .field_name = "Marca"});
  ASSERT_TRUE(prepared.has_value()) << prepared.error().message;
  EXPECT_TRUE(prepared->document_timestamp);
  EXPECT_FALSE(prepared->timestamp_request.empty());
  EXPECT_EQ(finish_signature(*prepared).error().code, ErrorCode::kUnsupported);  // no response
  // A response for other bytes ("capriole").
  EXPECT_FALSE(finish_signature(*prepared, read_file(kDir + "timestamp.tsr")).has_value());
}

}  // namespace
}  // namespace capriole::edit
