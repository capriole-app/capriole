// The cryptography of digital signatures (PLAN.md phase 8h): ASN.1, RC2, PKCS#12, CMS.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "crypto/rc2.hpp"

namespace capriole::crypto {
namespace {

// Fixtures made by tests/data/signing/make_fixtures.sh (OpenSSL): a throwaway test PKI.
const std::string kDir = std::string(CAPRIOLE_TEST_DATA_DIR) + "/signing/";

std::vector<std::byte> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::memcpy(out.data(), chars.data(), chars.size());
  return out;
}

std::vector<std::byte> hex(std::string_view text) {
  std::vector<std::byte> out;
  for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
    out.push_back(static_cast<std::byte>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

TEST(Rc2, DecryptsTheVectorsOfRfc2268) {
  struct Vector {
    std::string_view key;
    unsigned bits;
    std::string_view plain;
    std::string_view cipher;
  };
  for (const Vector& v : {Vector{.key = "0000000000000000", .bits = 63, .plain = "0000000000000000", .cipher = "ebb773f993278eff"},
                          Vector{.key = "ffffffffffffffff", .bits = 64, .plain = "ffffffffffffffff", .cipher = "278b27e42e2f0d49"},
                          Vector{.key = "3000000000000000", .bits = 64, .plain = "1000000000000001", .cipher = "30649edf9be7d2c2"},
                          Vector{.key = "88", .bits = 64, .plain = "0000000000000000", .cipher = "61a8a244adacccf0"},
                          Vector{.key = "88bca90e90875a", .bits = 64, .plain = "0000000000000000", .cipher = "6ccf4308974c267f"},
                          Vector{.key = "88bca90e90875a7f0f79c384627bafb2", .bits = 64, .plain = "0000000000000000", .cipher = "1a807d272bbe5db1"},
                          Vector{.key = "88bca90e90875a7f0f79c384627bafb2", .bits = 128, .plain = "0000000000000000", .cipher = "2269552ab0f85ca6"},
                          Vector{.key = "88bca90e90875a7f0f79c384627bafb216f80a6f85920584c42fceb0be255daf1e",
                                 .bits = 129,
                                 .plain = "0000000000000000",
                                 .cipher = "5b78d3a43dfff1f1"}}) {
    auto rc2 = Rc2::create(hex(v.key), v.bits);
    ASSERT_TRUE(rc2.has_value());
    Rc2::Block block{};
    const auto c = hex(v.cipher);
    std::ranges::copy(c, block.begin());
    const auto plain = rc2->decrypt(block);
    EXPECT_EQ(std::vector<std::byte>(plain.begin(), plain.end()), hex(v.plain)) << v.key;
  }
}

TEST(Der, WritesAndReadsBack) {
  using namespace der;
  const Bytes encoded = sequence({oid("1.2.840.113549.1.7.2"), integer(std::uint64_t{0x80}), octet_string(bytes_of("abc")),
                                  set_of({integer(std::uint64_t{2}), integer(std::uint64_t{1})})});
  Reader top(encoded);
  auto seq = top.expect(kSequence);
  ASSERT_TRUE(seq.has_value());
  EXPECT_TRUE(top.empty());
  auto r = Reader::into(*seq);
  ASSERT_TRUE(r.has_value());
  auto id = r->expect(kOid);
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(oid_text(id->content), "1.2.840.113549.1.7.2");
  auto n = r->expect(kInteger);
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(n->content.size(), 2U);  // 0x80 needs a sign byte
  EXPECT_EQ(small_integer(*n), std::uint64_t{0x80});
  auto s = r->expect(kOctetString);
  ASSERT_TRUE(s.has_value());
  auto set = r->expect(kSet);
  ASSERT_TRUE(set.has_value());
  EXPECT_EQ(std::to_integer<int>(set->content[2]), 1);  // sorted
  EXPECT_TRUE(r->empty());
}

TEST(Der, ReadsIndefiniteLengthsAndRefusesWhatOverruns) {
  using namespace der;
  // SEQUENCE (indefinite) { OCTET STRING (constructed, indefinite) { "ab", "c" } } 00 00
  const auto ber =
      hex("3080"
          "2480"
          "04026162"
          "040163"
          "0000"
          "0000");
  Reader top(ber);
  auto seq = top.next();
  ASSERT_TRUE(seq.has_value());
  EXPECT_EQ(seq->whole.size(), ber.size());
  auto r = Reader::into(*seq);
  ASSERT_TRUE(r.has_value());
  auto octets = r->next();
  ASSERT_TRUE(octets.has_value());
  auto bytes = string_bytes(*octets);
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(*bytes, hex("616263"));

  for (const std::string_view bad : {"3005020101", "30", "3084ffffffff00", "0480", "3080020101"}) {
    const auto data = hex(bad);
    Reader reader(data);
    EXPECT_FALSE(reader.next().has_value()) << bad;
  }
  // Nested deeper than the limit.
  std::string deep;
  for (int i = 0; i < kMaxDepth + 2; ++i) {
    deep += "3080";
  }
  for (int i = 0; i < kMaxDepth + 2; ++i) {
    deep += "0000";
  }
  const auto data = hex(deep);
  Reader reader(data);
  EXPECT_FALSE(reader.next().has_value());
}

TEST(Der, ConvertsPdfDatesToUtc) {
  auto t = der::generalized_time("D:20260704123000+02'00'");
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(der::chars_of(*t).substr(2), "20260704103000Z");
  auto z = der::generalized_time("D:2026");
  ASSERT_TRUE(z.has_value());
  EXPECT_EQ(der::chars_of(*z).substr(2), "20260101000000Z");
  EXPECT_FALSE(der::generalized_time("yesterday").has_value());
  EXPECT_FALSE(der::generalized_time("D:20261340").has_value());
}

TEST(Pkcs12, ReadsWhatToolsWrite) {
  for (const auto* name : {"rsa.p12", "ec.p12", "legacy.p12"}) {
    auto credentials = read_pkcs12(read_file(kDir + name), "capriole");
    ASSERT_TRUE(credentials.has_value()) << name << ": " << credentials.error().message;
    EXPECT_FALSE(credentials->key().empty()) << name;
    EXPECT_FALSE(credentials->certificate().empty()) << name;
    EXPECT_EQ(credentials->chain().size(), 1U) << name;  // the CA
  }
  auto nopass = read_pkcs12(read_file(kDir + "nopass.p12"), "");
  ASSERT_TRUE(nopass.has_value()) << nopass.error().message;
  EXPECT_TRUE(nopass->chain().empty());
}

TEST(Pkcs12, TellsAWrongPasswordFromADamagedFile) {
  for (const auto* name : {"rsa.p12", "legacy.p12", "nopass.p12"}) {
    auto wrong = read_pkcs12(read_file(kDir + name), "wrong");
    ASSERT_FALSE(wrong.has_value()) << name;
    EXPECT_EQ(wrong.error().code, ErrorCode::kPasswordRequired) << name;
  }
  auto file = read_file(kDir + "rsa.p12");
  file.resize(file.size() / 2);
  auto cut = read_pkcs12(file, "capriole");
  ASSERT_FALSE(cut.has_value());
  EXPECT_EQ(cut.error().code, ErrorCode::kSyntax);
  EXPECT_FALSE(read_pkcs12(read_file(kDir + "ca.pem"), "capriole").has_value());
}

TEST(Digest, HashesInPieces) {
  Digest d(DigestAlgorithm::kSha256);
  d.update(der::bytes_of("ab"));
  d.update(der::bytes_of("c"));
  EXPECT_EQ(d.finish(), hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  EXPECT_EQ(digest_of_oid("1.2.840.10045.4.3.2"), DigestAlgorithm::kSha256);
  EXPECT_EQ(digest_of_oid("1.2.3"), std::nullopt);
}

Credentials credentials(const char* name) {
  auto c = read_pkcs12(read_file(kDir + name), "capriole");
  EXPECT_TRUE(c.has_value());
  return std::move(*c);
}

TEST(Cms, SignsAndVerifiesDetached) {
  const auto content = der::bytes_of("the bytes a PDF signature covers");
  const auto digest = Digest::of(DigestAlgorithm::kSha256, content);
  const auto ca = read_file(kDir + "ca.pem");
  for (const auto* name : {"rsa.p12", "ec.p12"}) {
    const Credentials signer = credentials(name);
    auto signature = sign_detached(signer, digest);
    ASSERT_TRUE(signature.has_value()) << name << ": " << signature.error().message;
    // Deterministic: the same input, the same signature.
    auto again = sign_detached(signer, digest);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->signature, signature->signature) << name;

    const auto cms = encode(*signature);
    auto verified = verify_cms(cms, [&](DigestAlgorithm a) { return Digest::of(a, content); });
    ASSERT_TRUE(verified.has_value()) << name << ": " << verified.error().message;
    EXPECT_TRUE(verified->digest_matches) << name;
    EXPECT_TRUE(verified->signature_valid) << name << ": " << verified->problem;
    EXPECT_TRUE(verified->signing_certificate) << name;
    EXPECT_FALSE(verified->signing_time.has_value());
    ASSERT_EQ(verified->signer, std::size_t{0});
    EXPECT_EQ(verified->certificates.size(), 2U);

    EXPECT_TRUE(check_chain(verified->certificates, 0, ca).trusted) << name;
    const auto untrusted = check_chain(verified->certificates, 0, read_file(kDir + "other.pem"));
    EXPECT_FALSE(untrusted.trusted);
    EXPECT_FALSE(untrusted.problem.empty());

    // Other content: the digest does not match.
    auto other = verify_cms(cms, [&](DigestAlgorithm a) { return Digest::of(a, der::bytes_of("other bytes")); });
    ASSERT_TRUE(other.has_value());
    EXPECT_FALSE(other->digest_matches);
    // A signature byte changed: it does not verify.
    CmsSignature broken = *signature;
    broken.signature[broken.signature.size() / 2] ^= std::byte{1};
    auto bad = verify_cms(encode(broken), [&](DigestAlgorithm a) { return Digest::of(a, content); });
    ASSERT_TRUE(bad.has_value());
    EXPECT_TRUE(bad->digest_matches);
    EXPECT_FALSE(bad->signature_valid);
  }
}

TEST(Cms, ReadsCertificates) {
  const Credentials signer = credentials("rsa.p12");
  auto info = certificate_info(signer.certificate());
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->common_name, "Mario Rossi (test)");
  EXPECT_EQ(info->serial, "02");
  EXPECT_EQ(info->key, "RSA 2048");
  EXPECT_FALSE(info->ca);
  EXPECT_TRUE(valid_at(*info, info->not_before));
  EXPECT_FALSE(valid_at(*info, "D:19990101000000Z"));
  EXPECT_FALSE(certificate_info(der::bytes_of("not a certificate")).has_value());
}

TEST(Cms, TakesATimeStampResponse) {
  const auto value = der::bytes_of("capriole");
  // The request: version 1, a SHA-256 imprint of the value, certificates asked for.
  const auto request = timestamp_request(value);
  der::Reader reader(request);
  ASSERT_TRUE(reader.expect(der::kSequence).has_value());

  const auto response = read_file(kDir + "timestamp.tsr");
  auto token = timestamp_token(response, value);
  ASSERT_TRUE(token.has_value()) << token.error().message;
  auto verified = verify_cms(*token);
  ASSERT_TRUE(verified.has_value());
  auto info = timestamp_info(*verified);
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(info->time.starts_with("D:20"));
  EXPECT_TRUE(check_chain(verified->certificates, verified->signer.value_or(99), read_file(kDir + "ca.pem")).trusted);
  // Not for this value; not a response.
  EXPECT_FALSE(timestamp_token(response, der::bytes_of("other")).has_value());
  EXPECT_FALSE(timestamp_token(value, value).has_value());

  // Encoded with the token: an unsigned attribute the verification finds.
  auto signature = sign_detached(credentials("ec.p12"), Digest::of(DigestAlgorithm::kSha256, value));
  ASSERT_TRUE(signature.has_value());
  auto with = verify_cms(encode(*signature, *token), [&](DigestAlgorithm a) { return Digest::of(a, value); });
  ASSERT_TRUE(with.has_value());
  EXPECT_TRUE(with->signature_valid);
  EXPECT_EQ(with->timestamp_token, *token);
}

TEST(Cms, RefusesWhatIsNotSignedData) {
  for (const std::string_view bad : {"", "3000", "300b06092a864886f70d010701", "3080"}) {
    EXPECT_FALSE(verify_cms(hex(bad), [](DigestAlgorithm) { return std::vector<std::byte>(); }).has_value()) << bad;
  }
}

}  // namespace
}  // namespace capriole::crypto
