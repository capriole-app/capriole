#include "capriole/crypto/cms.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "mbed.hpp"

namespace capriole::crypto {

namespace {

using der::Bytes;
using der::Element;
using der::Reader;

// Object identifiers (RFC 5652, RFC 5035, RFC 3161, RFC 8017, RFC 5758).
constexpr std::string_view kSignedData = "1.2.840.113549.1.7.2";
constexpr std::string_view kData = "1.2.840.113549.1.7.1";
constexpr std::string_view kContentType = "1.2.840.113549.1.9.3";
constexpr std::string_view kMessageDigest = "1.2.840.113549.1.9.4";
constexpr std::string_view kSigningTime = "1.2.840.113549.1.9.5";
constexpr std::string_view kSigningCertificate = "1.2.840.113549.1.9.16.2.12";
constexpr std::string_view kSigningCertificateV2 = "1.2.840.113549.1.9.16.2.47";
constexpr std::string_view kTimestampToken = "1.2.840.113549.1.9.16.2.14";
constexpr std::string_view kTstInfo = "1.2.840.113549.1.9.16.1.4";
constexpr std::string_view kRsaPss = "1.2.840.113549.1.1.10";
constexpr std::string_view kSha256WithRsa = "1.2.840.113549.1.1.11";
constexpr std::string_view kEcdsaWithSha256 = "1.2.840.10045.4.3.2";

// A SignedData holds at most this many certificates (hostile input).
constexpr std::size_t kMaxCertificates = 64;

std::unexpected<Error> unreadable(std::string_view what) { return make_error(ErrorCode::kSyntax, std::format("CMS: {}", what)); }

std::string oid_of(const Element& e) { return e.tag == der::kOid ? der::oid_text(e.content) : std::string(); }

std::span<const std::byte> span_of(const mbedtls_x509_buf& buf) { return std::as_bytes(std::span(buf.p, buf.len)); }

std::string pdf_date(const mbedtls_x509_time& t) { return std::format("D:{:04}{:02}{:02}{:02}{:02}{:02}Z", t.year, t.mon, t.day, t.hour, t.min, t.sec); }

// A PDF date as UTC "YYYYMMDDHHmmSS", to compare (the offset applied).
std::optional<std::string> utc_of(std::string_view pdf_date) {
  auto t = der::generalized_time(pdf_date);
  if (!t || t->size() < 2 + 14) {
    return std::nullopt;
  }
  return std::string(der::chars_of(*t).substr(2, 14));
}

// An AlgorithmIdentifier: its OID and its parameters (none, or the element).
struct Algorithm {
  std::string oid;
  std::optional<Element> parameters;
};

Result<Algorithm> algorithm_of(const Element& e, int depth) {
  auto r = Reader::into(e, depth);
  auto id = r ? r->expect(der::kOid) : Result<Element>(std::unexpected(r.error()));
  if (!id) {
    return std::unexpected(std::move(id.error()));
  }
  Algorithm a{.oid = oid_of(*id), .parameters = std::nullopt};
  if (!r->empty()) {
    auto params = r->next();
    if (!params) {
      return std::unexpected(std::move(params.error()));
    }
    if (params->tag != der::kNull) {
      a.parameters = *params;
    }
  }
  return a;
}

Bytes algorithm_identifier(std::string_view oid, bool null_parameters) {
  const Bytes id = der::oid(oid);
  return null_parameters ? der::sequence({id, der::null()}) : der::sequence({id});
}

Bytes attribute(std::string_view oid, const Bytes& value) { return der::sequence({der::oid(oid), der::set_of({value})}); }

// The values of attributes: (oid, first value) in order.
Result<std::vector<std::pair<std::string, Element>>> attributes_of(const Element& set, int depth) {
  std::vector<std::pair<std::string, Element>> out;
  auto r = Reader::into(set, depth);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  while (!r->empty()) {
    auto attribute = r->expect(der::kSequence);
    auto ar = attribute ? Reader::into(*attribute, r->depth()) : Result<Reader>(std::unexpected(attribute.error()));
    auto id = ar ? ar->expect(der::kOid) : Result<Element>(std::unexpected(ar.error()));
    auto values = id ? ar->expect(der::kSet) : Result<Element>(std::unexpected(id.error()));
    auto vr = values ? Reader::into(*values, ar->depth()) : Result<Reader>(std::unexpected(values.error()));
    auto value = vr ? vr->next() : Result<Element>(std::unexpected(vr.error()));
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    out.emplace_back(oid_of(*id), *value);
    if (out.size() > 64) {
      return make_error(ErrorCode::kLimitExceeded, "CMS: too many attributes");
    }
  }
  return out;
}

// Verifies a signature over a digest with a certificate's key.
bool verifies(mbedtls_pk_context& pk, const Algorithm& signature_algorithm, DigestAlgorithm digest, std::span<const std::byte> hash,
              std::span<const std::byte> signature) {
  if (signature_algorithm.oid == kRsaPss && mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA) != 0) {
    // RSASSA-PSS-params (RFC 8017 A.2.3): hash [0], mask generation [1], salt length [2]; SHA-1 and 20
    // by default.
    mbedtls_md_type_t hash_md = MBEDTLS_MD_SHA1;
    mbedtls_md_type_t mgf_md = MBEDTLS_MD_SHA1;
    int salt = 20;
    if (signature_algorithm.parameters && signature_algorithm.parameters->tag == der::kSequence) {
      auto r = Reader::into(*signature_algorithm.parameters);
      while (r && !r->empty()) {
        auto field = r->next();
        if (!field) {
          return false;
        }
        auto fr = Reader::into(*field);
        auto inner = fr ? fr->next() : Result<Element>(std::unexpected(Error{}));
        if (!inner) {
          return false;
        }
        if (field->tag == der::context_constructed(0)) {
          auto a = algorithm_of(*inner, fr->depth());
          const auto d = a ? digest_of_oid(a->oid) : std::nullopt;
          if (!d) {
            return false;
          }
          hash_md = mbed::md_of(*d);
        } else if (field->tag == der::context_constructed(1)) {
          auto a = algorithm_of(*inner, fr->depth());
          auto mr = a && a->parameters ? Reader::into(*a->parameters) : Result<Reader>(std::unexpected(Error{}));
          auto mgf_hash = mr ? mr->expect(der::kSequence) : Result<Element>(std::unexpected(Error{}));
          auto ma = mgf_hash ? algorithm_of(*mgf_hash, mr->depth()) : Result<Algorithm>(std::unexpected(Error{}));
          const auto d = ma ? digest_of_oid(ma->oid) : std::nullopt;
          if (!d) {
            return false;
          }
          mgf_md = mbed::md_of(*d);
        } else if (field->tag == der::context_constructed(2)) {
          auto n = der::small_integer(*inner);
          if (!n || *n > 1024) {
            return false;
          }
          salt = static_cast<int>(*n);
        }
      }
    }
    if (hash_md != mbed::md_of(digest)) {
      return false;
    }
    mbedtls_pk_rsassa_pss_options options{.mgf1_hash_id = mgf_md, .expected_salt_len = salt};
    return mbedtls_pk_verify_ext(MBEDTLS_PK_RSASSA_PSS, &options, &pk, hash_md, mbed::uchars(hash), hash.size(), mbed::uchars(signature), signature.size()) ==
           0;
  }
  return mbedtls_pk_verify(&pk, mbed::md_of(digest), mbed::uchars(hash), hash.size(), mbed::uchars(signature), signature.size()) == 0;
}

// The ESS certificate identifiers of a signing-certificate(-v2) attribute name this certificate.
bool names(const Element& value, bool v2, int depth, std::span<const std::byte> certificate) {
  // SigningCertificate(V2) ::= SEQUENCE { certs SEQUENCE OF ESSCertID(v2), policies OPTIONAL }
  auto r = Reader::into(value, depth);
  auto certs = r ? r->expect(der::kSequence) : Result<Element>(std::unexpected(Error{}));
  auto cr = certs ? Reader::into(*certs, r->depth()) : Result<Reader>(std::unexpected(Error{}));
  auto first = cr ? cr->expect(der::kSequence) : Result<Element>(std::unexpected(Error{}));
  auto ir = first ? Reader::into(*first, cr->depth()) : Result<Reader>(std::unexpected(Error{}));
  if (!ir) {
    return false;
  }
  DigestAlgorithm hash = v2 ? DigestAlgorithm::kSha256 : DigestAlgorithm::kSha1;
  if (v2) {
    if (auto algorithm = ir->next_if(der::kSequence); algorithm && *algorithm) {
      auto a = algorithm_of(**algorithm, ir->depth());
      const auto d = a ? digest_of_oid(a->oid) : std::nullopt;
      if (!d) {
        return false;
      }
      hash = *d;
    }
  }
  auto cert_hash = ir->expect(der::kOctetString);
  return cert_hash && std::ranges::equal(cert_hash->content, Digest::of(hash, certificate));
}

std::string text_of(const mbedtls_x509_name& name) {
  std::array<char, 1024> buffer{};
  const int n = mbedtls_x509_dn_gets(buffer.data(), buffer.size(), &name);
  return n > 0 ? std::string(buffer.data(), static_cast<std::size_t>(n)) : std::string();
}

}  // namespace

Result<CertificateInfo> certificate_info(std::span<const std::byte> der) {
  mbed::Certificates certificate;
  if (!certificate.add(der)) {
    return make_error(ErrorCode::kSyntax, "not a certificate that can be read");
  }
  const mbedtls_x509_crt& c = *certificate.get();
  CertificateInfo info;
  info.subject = text_of(c.subject);
  info.issuer = text_of(c.issuer);
  for (const mbedtls_x509_name* n = &c.subject; n != nullptr; n = n->next) {
    if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
      info.common_name = der::chars_of(span_of(n->val));
    }
  }
  if (info.common_name.empty()) {
    info.common_name = info.subject;
  }
  for (std::size_t i = 0; i < c.serial.len; ++i) {
    info.serial += std::format("{:02X}", c.serial.p[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): within its length
  }
  info.not_before = pdf_date(c.valid_from);
  info.not_after = pdf_date(c.valid_to);
  info.key = std::format("{} {}", mbedtls_pk_can_do(&c.pk, MBEDTLS_PK_RSA) != 0 ? "RSA" : "EC", mbedtls_pk_get_bitlen(&c.pk));
  info.ca = mbedtls_x509_crt_get_ca_istrue(&c) == 1;
  return info;
}

bool valid_at(const CertificateInfo& certificate, std::string_view pdf_date) {
  const auto at = utc_of(pdf_date);
  const auto from = utc_of(certificate.not_before);
  const auto to = utc_of(certificate.not_after);
  return at && from && to && *from <= *at && *at <= *to;
}

Result<CmsSignature> unsigned_detached(std::span<const std::byte> certificate, const std::vector<Bytes>& others, std::span<const std::byte> content_digest) {
  mbed::Certificates parsed;
  if (!parsed.add(certificate)) {
    return make_error(ErrorCode::kSyntax, "the signer's certificate cannot be read");
  }
  const mbedtls_x509_crt& c = *parsed.get();
  const bool rsa = mbedtls_pk_can_do(&c.pk, MBEDTLS_PK_RSA) != 0;
  if (!rsa && mbedtls_pk_can_do(&c.pk, MBEDTLS_PK_ECDSA) == 0) {
    return make_error(ErrorCode::kUnsupported, "the key is neither RSA nor ECDSA");
  }

  CmsSignature out;
  out.digest = DigestAlgorithm::kSha256;
  // IssuerAndSerialNumber: the issuer's Name and the serial as the certificate has them.
  out.signer_identifier = der::sequence({span_of(c.issuer_raw), der::tlv(der::kInteger, span_of(c.serial))});
  // ESSCertIDv2 (RFC 5035): SHA-256 (the default, left out), the certificate's hash, its issuer and serial.
  const Bytes issuer_serial =
      der::sequence({der::sequence({der::tlv(der::context_constructed(4), span_of(c.issuer_raw))}), der::tlv(der::kInteger, span_of(c.serial))});
  const Bytes cert_id = der::sequence({der::octet_string(Digest::of(DigestAlgorithm::kSha256, certificate)), issuer_serial});
  out.signed_attributes = der::set_of({
      attribute(kContentType, der::oid(kData)),
      attribute(kMessageDigest, der::octet_string(content_digest)),
      attribute(kSigningCertificateV2, der::sequence({der::sequence({cert_id})})),
  });
  out.signature_algorithm = rsa ? algorithm_identifier(kSha256WithRsa, true) : algorithm_identifier(kEcdsaWithSha256, false);
  out.certificates.emplace_back(certificate.begin(), certificate.end());
  for (const auto& other : others) {
    out.certificates.push_back(other);
  }
  return out;
}

std::vector<std::byte> to_be_signed(const CmsSignature& signature) { return Digest::of(signature.digest, signature.signed_attributes); }

Bytes digest_info(const CmsSignature& signature) {
  return der::sequence({algorithm_identifier(oid_of(signature.digest), true), der::octet_string(to_be_signed(signature))});
}

std::size_t longest_value(std::span<const std::byte> certificate) {
  mbed::Certificates parsed;
  if (!parsed.add(certificate)) {
    return 0;
  }
  mbedtls_pk_context& pk = parsed.get()->pk;
  const std::size_t bytes = (mbedtls_pk_get_bitlen(&pk) + 7) / 8;
  if (mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA) != 0) {
    return bytes;
  }
  // Ecdsa-Sig-Value: a SEQUENCE of two INTEGERs, each with a leading zero at most.
  return (2 * (bytes + 1 + 3)) + 3;
}

Result<void> set_value(CmsSignature& signature, std::span<const std::byte> certificate, std::span<const std::byte> value) {
  mbed::Certificates parsed;
  if (!parsed.add(certificate)) {
    return make_error(ErrorCode::kSyntax, "the signer's certificate cannot be read");
  }
  mbedtls_pk_context& pk = parsed.get()->pk;
  Bytes v(value.begin(), value.end());
  if (mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA) != 0) {
    // DER when it reads as a whole SEQUENCE of two INTEGERs; else r‖s, two halves of the same length.
    Reader r(value);
    const auto sequence = r.next();
    auto inner = sequence && sequence->tag == der::kSequence && r.empty() ? Reader::into(*sequence) : Result<Reader>(std::unexpected(Error{}));
    auto first = inner ? inner->expect(der::kInteger) : Result<Element>(std::unexpected(Error{}));
    auto second = first ? inner->expect(der::kInteger) : Result<Element>(std::unexpected(Error{}));
    if (!second || !inner->empty()) {
      if (value.empty() || value.size() % 2 != 0) {
        return make_error(ErrorCode::kCorruptData, "the signature value is neither DER nor r and s");
      }
      const std::size_t half = value.size() / 2;
      v = der::sequence({der::integer(value.first(half)), der::integer(value.subspan(half))});
    }
  }
  const auto hash = to_be_signed(signature);
  if (mbedtls_pk_verify(&pk, mbed::md_of(signature.digest), mbed::uchars(hash), hash.size(), mbed::uchars(std::span<const std::byte>(v)), v.size()) != 0) {
    return make_error(ErrorCode::kCorruptData, "the signature value does not verify with the certificate's key (another key signed, or something else)");
  }
  signature.signature = std::move(v);
  return {};
}

Result<void> sign_with(CmsSignature& signature, const Credentials& credentials) {
  mbed::Pk key;
  if (auto parsed = mbed::parse_private_key(key, credentials.key()); !parsed) {
    return parsed;
  }
  const auto hash = to_be_signed(signature);
  // Blinding randomness from the key and what is signed: the same input, the same signature.
  std::vector<std::byte> seed(credentials.key().begin(), credentials.key().end());
  seed.insert(seed.end(), hash.begin(), hash.end());
  mbed::Drbg drbg(seed);
  mbedtls_platform_zeroize(seed.data(), seed.size());
  std::vector<unsigned char> value(MBEDTLS_PK_SIGNATURE_MAX_SIZE);
  std::size_t length = 0;
  if (const int rc =
          mbedtls_pk_sign(key.get(), MBEDTLS_MD_SHA256, mbed::uchars(hash), hash.size(), value.data(), value.size(), &length, mbed::Drbg::random, &drbg);
      rc != 0) {
    return make_error(ErrorCode::kUnsupported, std::format("signing failed (Mbed TLS {:#x})", -rc));
  }
  signature.signature.resize(length);
  std::ranges::copy(std::as_bytes(std::span(value.data(), length)), signature.signature.begin());
  return {};
}

Result<CmsSignature> sign_detached(const Credentials& credentials, std::span<const std::byte> content_digest) {
  auto out = unsigned_detached(credentials.certificate(), credentials.chain(), content_digest);
  if (!out) {
    return out;
  }
  if (auto signed_ = sign_with(*out, credentials); !signed_) {
    return std::unexpected(std::move(signed_.error()));
  }
  return out;
}

Bytes encode(const CmsSignature& signature, std::span<const std::byte> timestamp_token) {
  const Bytes digest_algorithm = algorithm_identifier(oid_of(signature.digest), false);
  // [0] IMPLICIT: the SET's content under the context tag.
  Reader set_reader(signature.signed_attributes);
  const auto set = set_reader.next();
  const std::span<const std::byte> attributes = set ? set->content : std::span<const std::byte>();
  Bytes unsigned_attributes;
  if (!timestamp_token.empty()) {
    const Bytes token_attribute = der::sequence({der::oid(kTimestampToken), der::tlv(der::kSet, timestamp_token)});
    unsigned_attributes = der::tlv(der::context_constructed(1), token_attribute);
  }
  const Bytes signer_info =
      der::sequence({der::integer(std::uint64_t{1}), signature.signer_identifier, digest_algorithm, der::tlv(der::context_constructed(0), attributes),
                     signature.signature_algorithm, der::octet_string(signature.signature), unsigned_attributes});
  Bytes certificates;
  for (const Bytes& c : signature.certificates) {
    certificates.insert(certificates.end(), c.begin(), c.end());
  }
  const Bytes signed_data = der::sequence({der::integer(std::uint64_t{1}), der::set_of({digest_algorithm}), der::sequence({der::oid(kData)}),
                                           der::tlv(der::context_constructed(0), certificates), der::set_of({signer_info})});
  return der::sequence({der::oid(kSignedData), der::tlv(der::context_constructed(0), signed_data)});
}

Bytes timestamp_request(std::span<const std::byte> signature_value) {
  const auto imprint = Digest::of(DigestAlgorithm::kSha256, signature_value);
  // A nonce from the imprint: the same signature, the same request (the TSA's answer differs anyway).
  std::array<std::byte, 8> nonce{};
  std::copy_n(imprint.begin(), nonce.size(), nonce.begin());
  nonce[0] &= std::byte{0x7F};
  return der::sequence({der::integer(std::uint64_t{1}),
                        der::sequence({algorithm_identifier(oid_of(DigestAlgorithm::kSha256), false), der::octet_string(imprint)}), der::integer(nonce),
                        der::boolean(true)});
}

Result<Bytes> timestamp_token(std::span<const std::byte> response, std::span<const std::byte> signature_value) {
  // TimeStampResp ::= SEQUENCE { status PKIStatusInfo, timeStampToken ContentInfo OPTIONAL }
  Reader top(response);
  auto resp = top.expect(der::kSequence);
  auto r = resp ? Reader::into(*resp) : Result<Reader>(std::unexpected(resp.error()));
  auto status_info = r ? r->expect(der::kSequence) : Result<Element>(std::unexpected(r.error()));
  auto sr = status_info ? Reader::into(*status_info, r->depth()) : Result<Reader>(std::unexpected(status_info.error()));
  auto status = sr ? sr->expect(der::kInteger) : Result<Element>(std::unexpected(sr.error()));
  if (!status) {
    return make_error(ErrorCode::kSyntax, "not a time stamp response");
  }
  if (const auto s = der::small_integer(*status); !s || *s > 1) {
    return make_error(ErrorCode::kUnsupported, std::format("the time-stamping authority refused the request (status {})", s.value_or(99)));
  }
  auto token = r->expect(der::kSequence);
  if (!token) {
    return make_error(ErrorCode::kSyntax, "the time stamp response has no token");
  }
  Bytes bytes(token->whole.begin(), token->whole.end());
  auto verified = verify_cms(bytes);
  if (!verified) {
    return std::unexpected(std::move(verified.error()));
  }
  if (!verified->digest_matches || !verified->signature_valid) {
    return make_error(ErrorCode::kCorruptData, std::format("the time stamp token does not verify: {}", verified->problem));
  }
  auto info = timestamp_info(*verified);
  if (!info) {
    return std::unexpected(std::move(info.error()));
  }
  if (info->imprint != Digest::of(info->imprint_algorithm, signature_value)) {
    return make_error(ErrorCode::kCorruptData, "the time stamp is not of this signature");
  }
  return bytes;
}

Result<CmsVerification> verify_cms(std::span<const std::byte> cms, const ContentDigest& content_digest) {
  // ContentInfo ::= SEQUENCE { contentType, [0] EXPLICIT content }
  Reader top(cms);
  auto info = top.expect(der::kSequence);
  auto ir = info ? Reader::into(*info) : Result<Reader>(std::unexpected(info.error()));
  auto type = ir ? ir->expect(der::kOid) : Result<Element>(std::unexpected(ir.error()));
  if (!type) {
    return unreadable("not a ContentInfo");
  }
  if (oid_of(*type) != kSignedData) {
    return make_error(ErrorCode::kUnsupported, std::format("CMS: content {} is not SignedData", oid_of(*type)));
  }
  auto wrapped = ir->expect(der::context_constructed(0));
  auto wr = wrapped ? Reader::into(*wrapped, ir->depth()) : Result<Reader>(std::unexpected(wrapped.error()));
  auto signed_data = wr ? wr->expect(der::kSequence) : Result<Element>(std::unexpected(wr.error()));
  auto sd = signed_data ? Reader::into(*signed_data, wr->depth()) : Result<Reader>(std::unexpected(signed_data.error()));
  // SignedData ::= SEQUENCE { version, digestAlgorithms SET, encapContentInfo, [0] certificates,
  //                           [1] crls, signerInfos SET }
  auto version = sd ? sd->expect(der::kInteger) : Result<Element>(std::unexpected(sd.error()));
  auto digests = version ? sd->expect(der::kSet) : Result<Element>(std::unexpected(version.error()));
  auto encap = digests ? sd->expect(der::kSequence) : Result<Element>(std::unexpected(digests.error()));
  if (!encap) {
    return unreadable("bad SignedData");
  }
  CmsVerification out;
  {
    auto er = Reader::into(*encap, sd->depth());
    auto content_type = er ? er->expect(der::kOid) : Result<Element>(std::unexpected(er.error()));
    if (!content_type) {
      return unreadable("bad encapsulated content");
    }
    out.content_type = oid_of(*content_type);
    if (auto content = er->next_if(der::context_constructed(0)); content && *content) {
      auto cr = Reader::into(**content, er->depth());
      auto octets = cr ? cr->next() : Result<Element>(std::unexpected(cr.error()));
      if (!octets || (octets->tag & ~der::kConstructed) != der::kOctetString) {
        return unreadable("bad encapsulated content");
      }
      auto bytes = der::string_bytes(*octets, cr->depth());
      if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
      }
      out.content = std::move(*bytes);
    }
  }
  if (auto certificates = sd->next_if(der::context_constructed(0)); certificates && *certificates) {
    auto cr = Reader::into(**certificates, sd->depth());
    while (cr && !cr->empty()) {
      auto c = cr->next();
      if (!c) {
        return unreadable("bad certificates");
      }
      if (c->tag == der::kSequence) {  // other certificate formats are left out
        out.certificates.emplace_back(c->whole.begin(), c->whole.end());
      }
      if (out.certificates.size() > kMaxCertificates) {
        return make_error(ErrorCode::kLimitExceeded, "CMS: too many certificates");
      }
    }
  }
  if (auto crls = sd->next_if(der::context_constructed(1)); !crls) {
    return unreadable("bad CRLs");
  }
  auto signer_infos = sd->expect(der::kSet);
  auto sir = signer_infos ? Reader::into(*signer_infos, sd->depth()) : Result<Reader>(std::unexpected(signer_infos.error()));
  auto signer_info = sir ? sir->expect(der::kSequence) : Result<Element>(std::unexpected(sir.error()));
  if (!signer_info) {
    return unreadable("no signer");
  }
  // SignerInfo ::= SEQUENCE { version, sid, digestAlgorithm, [0] signedAttrs, signatureAlgorithm,
  //                           signature, [1] unsignedAttrs }
  auto si = Reader::into(*signer_info, sir->depth());
  auto si_version = si ? si->expect(der::kInteger) : Result<Element>(std::unexpected(si.error()));
  auto sid = si_version ? si->next() : Result<Element>(std::unexpected(si_version.error()));
  auto digest_algorithm = sid ? si->expect(der::kSequence) : Result<Element>(std::unexpected(sid.error()));
  if (!digest_algorithm) {
    return unreadable("bad signer");
  }
  auto signed_attributes = si->next_if(der::context_constructed(0));
  auto signature_algorithm = signed_attributes ? si->expect(der::kSequence) : Result<Element>(std::unexpected(signed_attributes.error()));
  auto signature = signature_algorithm ? si->expect(der::kOctetString) : Result<Element>(std::unexpected(signature_algorithm.error()));
  if (!signature) {
    return unreadable("bad signer");
  }
  out.signature.assign(signature->content.begin(), signature->content.end());
  auto digest_alg = algorithm_of(*digest_algorithm, si->depth());
  auto sig_alg = algorithm_of(*signature_algorithm, si->depth());
  if (!digest_alg || !sig_alg) {
    return unreadable("bad algorithm");
  }
  const auto digest = digest_of_oid(digest_alg->oid);
  if (!digest) {
    return make_error(ErrorCode::kUnsupported, std::format("CMS: hash {} not supported", digest_alg->oid));
  }
  out.digest = *digest;
  if (auto unsigned_attributes = si->next_if(der::context_constructed(1)); unsigned_attributes && *unsigned_attributes) {
    auto list = attributes_of(**unsigned_attributes, si->depth());
    if (!list) {
      return std::unexpected(std::move(list.error()));
    }
    for (const auto& [id, value] : *list) {
      if (id == kTimestampToken) {
        out.timestamp_token = Bytes(value.whole.begin(), value.whole.end());
      }
    }
  }

  // What the content's digest is, and what the signature is over.
  std::vector<std::byte> content_hash;
  if (content_digest) {
    content_hash = content_digest(out.digest);
  } else if (out.content) {
    content_hash = Digest::of(out.digest, *out.content);
  } else {
    return make_error(ErrorCode::kUnsupported, "CMS: detached content not given");
  }
  std::vector<std::byte> signed_hash;
  std::optional<Element> signing_certificate;
  bool signing_certificate_v2 = false;
  if (*signed_attributes) {
    out.signed_attributes = true;
    auto list = attributes_of(**signed_attributes, si->depth());
    if (!list) {
      return std::unexpected(std::move(list.error()));
    }
    std::optional<Element> message_digest;
    for (const auto& [id, value] : *list) {
      if (id == kMessageDigest) {
        message_digest = value;
      } else if (id == kSigningTime) {
        if (auto t = der::time_text(value)) {
          out.signing_time = *t;
        }
      } else if (id == kSigningCertificateV2 || id == kSigningCertificate) {
        signing_certificate = value;
        signing_certificate_v2 = id == kSigningCertificateV2;
      }
    }
    out.digest_matches = message_digest && message_digest->tag == der::kOctetString && std::ranges::equal(message_digest->content, content_hash);
    if (!out.digest_matches) {
      out.problem = "the document was changed after it was signed (its digest is not the one signed)";
    }
    // The signature is over the DER of the attributes as a SET (RFC 5652 §5.4).
    Bytes set((*signed_attributes)->whole.begin(), (*signed_attributes)->whole.end());
    set[0] = std::byte{der::kSet};
    signed_hash = Digest::of(out.digest, set);
  } else {
    out.digest_matches = true;  // checked by the signature itself
    signed_hash = content_hash;
  }

  // The signer: the certificate the identifier names, else any whose key verifies.
  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < out.certificates.size(); ++i) {
    mbed::Certificates c;
    if (!c.add(out.certificates[i])) {
      continue;
    }
    bool named = false;
    if (sid->tag == der::kSequence) {
      auto ir2 = Reader::into(*sid, si->depth());
      auto issuer = ir2 ? ir2->expect(der::kSequence) : Result<Element>(std::unexpected(Error{}));
      auto serial = issuer ? ir2->expect(der::kInteger) : Result<Element>(std::unexpected(Error{}));
      named = serial && std::ranges::equal(issuer->whole, span_of(c.get()->issuer_raw)) && std::ranges::equal(serial->content, span_of(c.get()->serial));
    } else if (sid->tag == der::context(0)) {
      named = std::ranges::equal(sid->content, span_of(c.get()->subject_key_id));
    }
    if (named) {
      order.insert(order.begin(), i);
    } else {
      order.push_back(i);
    }
  }
  for (const std::size_t i : order) {
    mbed::Certificates c;
    if (c.add(out.certificates[i]) && verifies(c.get()->pk, *sig_alg, out.digest, signed_hash, out.signature)) {
      out.signer = i;
      out.signature_valid = true;
      break;
    }
  }
  if (!out.signature_valid) {
    out.problem = out.certificates.empty() ? "the signer's certificate is not in the signature" : "the signature does not verify with the signer's key";
  } else if (signing_certificate) {
    out.signing_certificate = names(*signing_certificate, signing_certificate_v2, si->depth(), out.certificates[*out.signer]);
  }
  if (!out.digest_matches) {
    out.problem = "the document was changed after it was signed (its digest is not the one signed)";
  }
  return out;
}

Result<TimestampInfo> timestamp_info(const CmsVerification& token) {
  if (token.content_type != kTstInfo || !token.content) {
    return make_error(ErrorCode::kSyntax, "not a time stamp token");
  }
  // TSTInfo ::= SEQUENCE { version, policy, messageImprint, serialNumber, genTime, … }
  Reader top(*token.content);
  auto tst = top.expect(der::kSequence);
  auto r = tst ? Reader::into(*tst) : Result<Reader>(std::unexpected(tst.error()));
  auto version = r ? r->expect(der::kInteger) : Result<Element>(std::unexpected(r.error()));
  auto policy = version ? r->expect(der::kOid) : Result<Element>(std::unexpected(version.error()));
  auto imprint = policy ? r->expect(der::kSequence) : Result<Element>(std::unexpected(policy.error()));
  auto serial = imprint ? r->expect(der::kInteger) : Result<Element>(std::unexpected(imprint.error()));
  auto time = serial ? r->expect(der::kGeneralizedTime) : Result<Element>(std::unexpected(serial.error()));
  auto mr = time ? Reader::into(*imprint, r->depth()) : Result<Reader>(std::unexpected(time.error()));
  auto algorithm = mr ? mr->expect(der::kSequence) : Result<Element>(std::unexpected(mr.error()));
  auto hash = algorithm ? mr->expect(der::kOctetString) : Result<Element>(std::unexpected(algorithm.error()));
  if (!hash) {
    return make_error(ErrorCode::kSyntax, "bad time stamp token");
  }
  auto a = algorithm_of(*algorithm, mr->depth());
  const auto d = a ? digest_of_oid(a->oid) : std::nullopt;
  auto when = der::time_text(*time);
  if (!d || !when) {
    return make_error(ErrorCode::kSyntax, "bad time stamp token");
  }
  return TimestampInfo{.time = *when, .imprint_algorithm = *d, .imprint = Bytes(hash->content.begin(), hash->content.end())};
}

bool verify_pkcs1(std::span<const std::byte> certificate, DigestAlgorithm digest, std::span<const std::byte> hash, std::span<const std::byte> signature) {
  mbed::Certificates c;
  return c.add(certificate) && mbedtls_pk_can_do(&c.get()->pk, MBEDTLS_PK_RSA) != 0 &&
         mbedtls_pk_verify(&c.get()->pk, mbed::md_of(digest), mbed::uchars(hash), hash.size(), mbed::uchars(signature), signature.size()) == 0;
}

ChainCheck check_chain(const std::vector<der::Bytes>& certificates, std::size_t signer, std::span<const std::byte> anchors) {
  if (signer >= certificates.size()) {
    return {.trusted = false, .problem = "no signer's certificate"};
  }
  mbed::Certificates chain;
  if (!chain.add(certificates[signer])) {
    return {.trusted = false, .problem = "the signer's certificate cannot be read"};
  }
  for (std::size_t i = 0; i < certificates.size(); ++i) {
    if (i != signer) {
      (void)chain.add(certificates[i]);
    }
  }
  mbed::Certificates trusted;
  if (trusted.add_all(anchors) == 0) {
    return {.trusted = false, .problem = "no trusted certificates given"};
  }
  std::uint32_t flags = 0;
  if (mbedtls_x509_crt_verify(chain.get(), trusted.get(), nullptr, nullptr, &flags, nullptr, nullptr) == 0) {
    return {.trusted = true, .problem = {}};
  }
  std::array<char, 512> text{};
  const int n = mbedtls_x509_crt_verify_info(text.data(), text.size(), "", flags);
  std::string problem = n > 0 ? std::string(text.data(), static_cast<std::size_t>(n)) : std::string("the chain does not verify");
  while (!problem.empty() && (problem.back() == '\n' || problem.back() == ' ')) {
    problem.pop_back();
  }
  return {.trusted = false, .problem = problem};
}

}  // namespace capriole::crypto
