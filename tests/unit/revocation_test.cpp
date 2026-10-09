// Revocation: OCSP responses and CRLs checked (PLAN.md phase 11b), with the fixtures of
// tests/data/signing/make_ltv_fixtures.sh.

#include "capriole/crypto/revocation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/pkcs12.hpp"

namespace capriole::crypto {
namespace {

const std::string kDir = std::string(CAPRIOLE_TEST_DATA_DIR) + "/signing/";

std::vector<std::byte> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::memcpy(out.data(), chars.data(), chars.size());
  return out;
}

// A signer's certificate and its issuer's (the CA, the other certificate of the file).
struct Pair {
  std::vector<std::byte> certificate;
  std::vector<std::byte> issuer;
};

Pair pair_of(const char* p12) {
  auto c = read_pkcs12(read_file(kDir + p12), "capriole");
  EXPECT_TRUE(c.has_value());
  if (!c || c->chain().empty()) {
    std::abort();
  }
  return {.certificate = {c->certificate().begin(), c->certificate().end()}, .issuer = c->chain().front()};
}

TEST(Revocation, ACertificateSaysWhereToAsk) {
  const Pair ltv = pair_of("ltv.p12");
  auto urls = certificate_urls(ltv.certificate);
  ASSERT_TRUE(urls.has_value()) << urls.error().message;
  EXPECT_EQ(urls->ocsp, std::vector<std::string>{"http://127.0.0.1:9/ocsp"});
  EXPECT_EQ(urls->ca_issuers, std::vector<std::string>{"http://127.0.0.1:9/ca.cer"});
  EXPECT_EQ(urls->crl, std::vector<std::string>{"http://127.0.0.1:9/ca.crl"});
  // One without: nothing.
  auto none = certificate_urls(pair_of("rsa.p12").certificate);
  ASSERT_TRUE(none.has_value());
  EXPECT_TRUE(none->ocsp.empty() && none->ca_issuers.empty() && none->crl.empty());
  EXPECT_FALSE(certificate_urls(read_file(kDir + "ocsp-ltv.der")).has_value());

  EXPECT_TRUE(issued_by(ltv.certificate, ltv.issuer));
  EXPECT_FALSE(issued_by(ltv.issuer, ltv.certificate));
  EXPECT_TRUE(ocsp_request(ltv.certificate, ltv.issuer).has_value());
}

TEST(Revocation, OcspAnswersAreCheckedAndMatched) {
  const Pair ltv = pair_of("ltv.p12");
  const Pair revoked = pair_of("revoked.p12");
  const auto good_answer = read_file(kDir + "ocsp-ltv.der");
  const auto revoked_answer = read_file(kDir + "ocsp-revoked.der");

  // By a responder the CA authorised.
  auto good = check_ocsp(good_answer, ltv.certificate, ltv.issuer);
  ASSERT_TRUE(good.has_value()) << good.error().message;
  EXPECT_EQ(good->status, RevocationStatus::kGood);
  EXPECT_TRUE(good->produced_at.starts_with("D:20"));
  EXPECT_FALSE(good->next_update.empty());
  // By the CA itself.
  auto bad = check_ocsp(revoked_answer, revoked.certificate, revoked.issuer);
  ASSERT_TRUE(bad.has_value()) << bad.error().message;
  EXPECT_EQ(bad->status, RevocationStatus::kRevoked);
  EXPECT_EQ(bad->revoked_at, "D:20260101000000Z");

  // About another certificate; another issuer; changed.
  EXPECT_EQ(check_ocsp(good_answer, revoked.certificate, revoked.issuer).error().code, ErrorCode::kOutOfRange);
  EXPECT_FALSE(check_ocsp(good_answer, ltv.certificate, ltv.certificate).has_value());
  auto changed = good_answer;
  changed[changed.size() / 3] ^= std::byte{1};
  EXPECT_FALSE(check_ocsp(changed, ltv.certificate, ltv.issuer).has_value());
  EXPECT_FALSE(check_ocsp(ltv.certificate, ltv.certificate, ltv.issuer).has_value());
}

TEST(Revocation, CrlsAreCheckedAndRead) {
  const Pair ltv = pair_of("ltv.p12");
  const Pair revoked = pair_of("revoked.p12");
  const auto crl = read_file(kDir + "ca.crl");
  auto good = check_crl(crl, ltv.certificate, ltv.issuer);
  ASSERT_TRUE(good.has_value()) << good.error().message;
  EXPECT_EQ(good->status, RevocationStatus::kGood);
  EXPECT_TRUE(good->this_update.starts_with("D:20"));
  auto bad = check_crl(crl, revoked.certificate, revoked.issuer);
  ASSERT_TRUE(bad.has_value()) << bad.error().message;
  EXPECT_EQ(bad->status, RevocationStatus::kRevoked);
  EXPECT_EQ(bad->revoked_at, "D:20260101000000Z");

  auto changed = crl;
  changed[changed.size() - 10] ^= std::byte{1};  // in the signature
  EXPECT_EQ(check_crl(changed, ltv.certificate, ltv.issuer).error().code, ErrorCode::kCorruptData);
  // Given another issuer: not its CRL.
  EXPECT_EQ(check_crl(crl, ltv.certificate, ltv.certificate).error().code, ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace capriole::crypto
