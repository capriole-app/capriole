#include "capriole/crypto/revocation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mbedtls/asn1.h>
#include <mbedtls/md.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "mbed.hpp"

namespace capriole::crypto {

namespace {

using der::Bytes;
using der::Element;
using der::Reader;

// RFC 5280 §4.2.2.1, §4.2.1.13; RFC 6960 §4.2.1, §4.2.2.2.
constexpr std::string_view kAuthorityInfoAccess = "1.3.6.1.5.5.7.1.1";
constexpr std::string_view kCrlDistributionPoints = "2.5.29.31";
constexpr std::string_view kAccessOcsp = "1.3.6.1.5.5.7.48.1";
constexpr std::string_view kAccessCaIssuers = "1.3.6.1.5.5.7.48.2";
constexpr std::string_view kOcspBasic = "1.3.6.1.5.5.7.48.1.1";
constexpr std::string_view kSha1 = "1.3.14.3.2.26";
// GeneralName's uniformResourceIdentifier: [6] IMPLICIT IA5String.
constexpr std::uint8_t kUri = der::context(6);

std::unexpected<Error> unreadable(std::string_view what) { return make_error(ErrorCode::kSyntax, std::string(what)); }

std::string oid_of(const Element& e) { return e.tag == der::kOid ? der::oid_text(e.content) : std::string(); }

// The parts of a certificate the checks need, as its DER has them.
struct Parts {
  Element serial;                       // the INTEGER
  Element issuer;                       // Name
  Element subject;                      // Name
  std::span<const std::byte> key_bits;  // the subjectPublicKey BIT STRING's bits
  std::optional<Element> extensions;    // [3]'s SEQUENCE
};

Result<Parts> parts_of(std::span<const std::byte> certificate) {
  Reader outer(certificate);
  auto cert = outer.expect(der::kSequence);
  auto cr = cert ? Reader::into(*cert) : Result<Reader>(std::unexpected(cert.error()));
  auto tbs = cr ? cr->expect(der::kSequence) : Result<Element>(std::unexpected(cr.error()));
  auto r = tbs ? Reader::into(*tbs, cr->depth()) : Result<Reader>(std::unexpected(tbs.error()));
  if (!r) {
    return unreadable("not a certificate");
  }
  if (auto version = r->next_if(der::context_constructed(0)); !version) {
    return unreadable("not a certificate");
  }
  auto serial = r->expect(der::kInteger);
  auto algorithm = serial ? r->expect(der::kSequence) : Result<Element>(std::unexpected(serial.error()));
  auto issuer = algorithm ? r->expect(der::kSequence) : Result<Element>(std::unexpected(algorithm.error()));
  auto validity = issuer ? r->expect(der::kSequence) : Result<Element>(std::unexpected(issuer.error()));
  auto subject = validity ? r->expect(der::kSequence) : Result<Element>(std::unexpected(validity.error()));
  auto spki = subject ? r->expect(der::kSequence) : Result<Element>(std::unexpected(subject.error()));
  auto sr = spki ? Reader::into(*spki, r->depth()) : Result<Reader>(std::unexpected(spki.error()));
  auto key_algorithm = sr ? sr->expect(der::kSequence) : Result<Element>(std::unexpected(sr.error()));
  auto key = key_algorithm ? sr->expect(der::kBitString) : Result<Element>(std::unexpected(key_algorithm.error()));
  if (!key || key->content.empty()) {
    return unreadable("not a certificate");
  }
  Parts p{.serial = *serial, .issuer = *issuer, .subject = *subject, .key_bits = key->content.subspan(1), .extensions = std::nullopt};
  while (!r->empty()) {
    auto field = r->next();
    if (!field) {
      return unreadable("not a certificate");
    }
    if (field->tag == der::context_constructed(3)) {
      auto er = Reader::into(*field, r->depth());
      auto sequence = er ? er->expect(der::kSequence) : Result<Element>(std::unexpected(er.error()));
      if (!sequence) {
        return unreadable("a certificate's extensions cannot be read");
      }
      p.extensions = *sequence;
    }
  }
  return p;
}

// The URIs among GeneralNames, http and https only.
void uris_of(const Element& names, int depth, std::vector<std::string>& out) {
  auto r = Reader::into(names, depth);
  while (r && !r->empty()) {
    auto name = r->next();
    if (!name) {
      return;
    }
    if (name->tag == kUri) {
      const std::string uri(der::chars_of(name->content));
      if ((uri.starts_with("http://") || uri.starts_with("https://")) && out.size() < 16) {
        out.push_back(uri);
      }
    }
  }
}

// Verifies a signed structure (a CRL, an OCSP response): the signature algorithm's digest of tbs (the
// whole element), the BIT STRING's value, with key.
Result<void> verify_signed(const Element& tbs, const Element& algorithm, const Element& bits, mbedtls_pk_context& key) {
  auto ar = Reader::into(algorithm);
  auto id = ar ? ar->expect(der::kOid) : Result<Element>(std::unexpected(ar.error()));
  if (!id || bits.content.empty() || bits.content[0] != std::byte{0}) {
    return unreadable("a signature that cannot be read");
  }
  mbedtls_asn1_buf oid{};
  oid.tag = MBEDTLS_ASN1_OID;
  oid.len = id->content.size();
  oid.p = const_cast<unsigned char*>(mbed::uchars(id->content));  // NOLINT(cppcoreguidelines-pro-type-const-cast): Mbed TLS only reads it
  mbedtls_md_type_t md = MBEDTLS_MD_NONE;
  mbedtls_pk_type_t pk = MBEDTLS_PK_NONE;
  if (mbedtls_oid_get_sig_alg(&oid, &md, &pk) != 0 || pk == MBEDTLS_PK_RSASSA_PSS) {
    return make_error(ErrorCode::kUnsupported, "a signature algorithm not read here");
  }
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(md);
  if (info == nullptr || mbedtls_pk_can_do(&key, pk) == 0) {
    return make_error(ErrorCode::kCorruptData, "signed with another kind of key");
  }
  std::vector<unsigned char> hash(mbedtls_md_get_size(info));
  if (mbedtls_md(info, mbed::uchars(tbs.whole), tbs.whole.size(), hash.data()) != 0) {
    return make_error(ErrorCode::kUnsupported, "a digest not read here");
  }
  const auto value = bits.content.subspan(1);
  if (mbedtls_pk_verify(&key, md, hash.data(), hash.size(), mbed::uchars(value), value.size()) != 0) {
    return make_error(ErrorCode::kCorruptData, "the signature does not verify with the issuer's key");
  }
  return {};
}

// An OCSP CertID (RFC 6960 §4.1.1) names certificate (issued by the holder of issuer's parts).
bool names(const Element& cert_id, int depth, const Parts& certificate, const Parts& issuer) {
  auto r = Reader::into(cert_id, depth);
  auto algorithm = r ? r->expect(der::kSequence) : Result<Element>(std::unexpected(r.error()));
  auto ar = algorithm ? Reader::into(*algorithm, r->depth()) : Result<Reader>(std::unexpected(algorithm.error()));
  auto id = ar ? ar->expect(der::kOid) : Result<Element>(std::unexpected(ar.error()));
  auto name_hash = id ? r->expect(der::kOctetString) : Result<Element>(std::unexpected(id.error()));
  auto key_hash = name_hash ? r->expect(der::kOctetString) : Result<Element>(std::unexpected(name_hash.error()));
  auto serial = key_hash ? r->expect(der::kInteger) : Result<Element>(std::unexpected(key_hash.error()));
  const auto digest = id ? digest_of_oid(oid_of(*id)) : std::nullopt;
  return serial && digest && std::ranges::equal(serial->content, certificate.serial.content) &&
         std::ranges::equal(name_hash->content, Digest::of(*digest, certificate.issuer.whole)) &&
         std::ranges::equal(key_hash->content, Digest::of(*digest, issuer.key_bits));
}

// The time of a Time element, else empty.
std::string time_of(const Result<Element>& e) {
  if (!e) {
    return {};
  }
  auto t = der::time_text(*e);
  return t ? std::move(*t) : std::string();
}

}  // namespace

Result<CertificateUrls> certificate_urls(std::span<const std::byte> certificate) {
  auto p = parts_of(certificate);
  if (!p) {
    return std::unexpected(std::move(p.error()));
  }
  CertificateUrls out;
  if (!p->extensions) {
    return out;
  }
  auto r = Reader::into(*p->extensions);
  while (r && !r->empty()) {
    auto extension = r->expect(der::kSequence);
    auto er = extension ? Reader::into(*extension, r->depth()) : Result<Reader>(std::unexpected(extension.error()));
    auto id = er ? er->expect(der::kOid) : Result<Element>(std::unexpected(er.error()));
    if (!id || !er->next_if(der::kBoolean)) {
      return unreadable("a certificate's extensions cannot be read");
    }
    auto value = er->expect(der::kOctetString);
    if (!value) {
      return unreadable("a certificate's extensions cannot be read");
    }
    const std::string oid = oid_of(*id);
    Reader vr(value->content, er->depth());
    auto list = vr.expect(der::kSequence);
    auto lr = list ? Reader::into(*list, vr.depth()) : Result<Reader>(std::unexpected(list.error()));
    if (!lr || (oid != kAuthorityInfoAccess && oid != kCrlDistributionPoints)) {
      continue;
    }
    while (!lr->empty()) {
      auto item = lr->expect(der::kSequence);
      auto ir = item ? Reader::into(*item, lr->depth()) : Result<Reader>(std::unexpected(item.error()));
      if (!ir) {
        break;
      }
      if (oid == kAuthorityInfoAccess) {
        // AccessDescription: the method, one GeneralName.
        auto method = ir->expect(der::kOid);
        auto location = method ? ir->next() : Result<Element>(std::unexpected(method.error()));
        if (location && location->tag == kUri) {
          const std::string uri(der::chars_of(location->content));
          auto& to = oid_of(*method) == kAccessOcsp ? out.ocsp : out.ca_issuers;
          if ((oid_of(*method) == kAccessOcsp || oid_of(*method) == kAccessCaIssuers) && (uri.starts_with("http://") || uri.starts_with("https://")) &&
              to.size() < 16) {
            to.push_back(uri);
          }
        }
      } else {
        // DistributionPoint: [0] DistributionPointName, whose [0] is the full name (GeneralNames).
        auto point = ir->next_if(der::context_constructed(0));
        if (point && *point) {
          auto pr = Reader::into(**point, ir->depth());
          auto full = pr ? pr->next_if(der::context_constructed(0)) : Result<std::optional<Element>>(std::unexpected(pr.error()));
          if (full && *full) {
            uris_of(**full, pr->depth(), out.crl);
          }
        }
      }
    }
  }
  return out;
}

bool issued_by(std::span<const std::byte> certificate, std::span<const std::byte> issuer) {
  mbed::Certificates child;
  mbed::Certificates parent;
  if (!child.add(certificate) || !parent.add(issuer)) {
    return false;
  }
  std::uint32_t flags = 0;
  return mbedtls_x509_crt_verify(child.get(), parent.get(), nullptr, nullptr, &flags, nullptr, nullptr) == 0;
}

Result<Bytes> ocsp_request(std::span<const std::byte> certificate, std::span<const std::byte> issuer) {
  auto c = parts_of(certificate);
  auto i = c ? parts_of(issuer) : Result<Parts>(std::unexpected(c.error()));
  if (!i) {
    return std::unexpected(std::move(i.error()));
  }
  const Bytes algorithm = der::sequence({der::oid(kSha1), der::null()});
  const Bytes cert_id = der::sequence({algorithm, der::octet_string(Digest::of(DigestAlgorithm::kSha1, c->issuer.whole)),
                                       der::octet_string(Digest::of(DigestAlgorithm::kSha1, i->key_bits)), c->serial.whole});
  // OCSPRequest { TBSRequest { requestList { Request { CertID } } } }
  return der::sequence({der::sequence({der::sequence({der::sequence({cert_id})})})});
}

Result<RevocationInfo> check_ocsp(std::span<const std::byte> response, std::span<const std::byte> certificate, std::span<const std::byte> issuer) {
  auto c = parts_of(certificate);
  auto i = c ? parts_of(issuer) : Result<Parts>(std::unexpected(c.error()));
  if (!i) {
    return std::unexpected(std::move(i.error()));
  }
  // OCSPResponse { responseStatus, [0] { ResponseBytes { type, OCTET STRING } } }
  Reader outer(response);
  auto top = outer.expect(der::kSequence);
  auto tr = top ? Reader::into(*top) : Result<Reader>(std::unexpected(top.error()));
  auto status = tr ? tr->expect(0x0A) : Result<Element>(std::unexpected(tr.error()));  // ENUMERATED
  if (!status) {
    return unreadable("not an OCSP response");
  }
  if (status->content.size() != 1 || status->content[0] != std::byte{0}) {
    return unreadable("the OCSP responder did not answer (its status is not successful)");
  }
  auto wrapped = tr->expect(der::context_constructed(0));
  auto wr = wrapped ? Reader::into(*wrapped, tr->depth()) : Result<Reader>(std::unexpected(wrapped.error()));
  auto bytes = wr ? wr->expect(der::kSequence) : Result<Element>(std::unexpected(wr.error()));
  auto br = bytes ? Reader::into(*bytes, wr->depth()) : Result<Reader>(std::unexpected(bytes.error()));
  auto type = br ? br->expect(der::kOid) : Result<Element>(std::unexpected(br.error()));
  auto octets = type ? br->expect(der::kOctetString) : Result<Element>(std::unexpected(type.error()));
  if (!octets) {
    return unreadable("not an OCSP response");
  }
  if (oid_of(*type) != kOcspBasic) {
    return make_error(ErrorCode::kUnsupported, "an OCSP response type not read here");
  }
  // BasicOCSPResponse { tbsResponseData, signatureAlgorithm, signature, [0] certs }
  Reader basic_reader(octets->content, br->depth());
  auto basic = basic_reader.expect(der::kSequence);
  auto r = basic ? Reader::into(*basic, basic_reader.depth()) : Result<Reader>(std::unexpected(basic.error()));
  auto tbs = r ? r->expect(der::kSequence) : Result<Element>(std::unexpected(r.error()));
  auto algorithm = tbs ? r->expect(der::kSequence) : Result<Element>(std::unexpected(tbs.error()));
  auto signature = algorithm ? r->expect(der::kBitString) : Result<Element>(std::unexpected(algorithm.error()));
  auto certs = signature ? r->next_if(der::context_constructed(0)) : Result<std::optional<Element>>(std::unexpected(signature.error()));
  if (!certs) {
    return unreadable("not an OCSP response");
  }

  // ResponseData { [0] version, responderID, producedAt, responses, [1] extensions }
  auto dr = Reader::into(*tbs, r->depth());
  if (!dr || !dr->next_if(der::context_constructed(0))) {
    return unreadable("not an OCSP response");
  }
  auto responder = dr->next();
  auto produced = responder ? dr->expect(der::kGeneralizedTime) : Result<Element>(std::unexpected(responder.error()));
  auto responses = produced ? dr->expect(der::kSequence) : Result<Element>(std::unexpected(produced.error()));
  if (!responses) {
    return unreadable("not an OCSP response");
  }
  std::optional<RevocationInfo> found;
  auto rr = Reader::into(*responses, dr->depth());
  while (rr && !rr->empty() && !found) {
    auto single = rr->expect(der::kSequence);
    auto sr = single ? Reader::into(*single, rr->depth()) : Result<Reader>(std::unexpected(single.error()));
    auto cert_id = sr ? sr->expect(der::kSequence) : Result<Element>(std::unexpected(sr.error()));
    auto cert_status = cert_id ? sr->next() : Result<Element>(std::unexpected(cert_id.error()));
    auto this_update = cert_status ? sr->expect(der::kGeneralizedTime) : Result<Element>(std::unexpected(cert_status.error()));
    if (!this_update) {
      return unreadable("an OCSP response's answer cannot be read");
    }
    if (!names(*cert_id, sr->depth(), *c, *i)) {
      continue;
    }
    RevocationInfo info;
    info.this_update = time_of(this_update);
    info.produced_at = time_of(produced);
    if (auto next = sr->next_if(der::context_constructed(0)); next && *next) {
      auto nr = Reader::into(**next, sr->depth());
      info.next_update = nr ? time_of(nr->expect(der::kGeneralizedTime)) : std::string();
    }
    if (cert_status->tag == der::context(0)) {
      info.status = RevocationStatus::kGood;
    } else if (cert_status->tag == der::context_constructed(1)) {
      info.status = RevocationStatus::kRevoked;
      auto vr = Reader::into(*cert_status, sr->depth());
      info.revoked_at = vr ? time_of(vr->expect(der::kGeneralizedTime)) : std::string();
    } else if (cert_status->tag == der::context(2)) {
      info.status = RevocationStatus::kUnknown;
    } else {
      return unreadable("an OCSP answer's status cannot be read");
    }
    found = std::move(info);
  }
  if (!rr) {
    return unreadable("not an OCSP response");
  }
  if (!found) {
    return make_error(ErrorCode::kOutOfRange, "the OCSP response is about other certificates");
  }

  // Signed by the issuer, or by a responder it authorised (§4.2.2.2): one of the response's certificates
  // that the issuer issued, for OCSP signing.
  mbed::Certificates issuer_cert;
  if (!issuer_cert.add(issuer)) {
    return unreadable("the issuer's certificate cannot be read");
  }
  auto verified = verify_signed(*tbs, *algorithm, *signature, issuer_cert.get()->pk);
  if (!verified && verified.error().code == ErrorCode::kCorruptData && *certs) {
    auto cr = Reader::into(**certs, r->depth());
    auto list = cr ? cr->expect(der::kSequence) : Result<Element>(std::unexpected(cr.error()));
    auto lr = list ? Reader::into(*list, cr->depth()) : Result<Reader>(std::unexpected(list.error()));
    for (std::size_t n = 0; lr && !lr->empty() && n < 16 && !verified; ++n) {
      auto candidate = lr->expect(der::kSequence);
      if (!candidate) {
        break;
      }
      mbed::Certificates delegate;
      if (!delegate.add(candidate->whole) || !issued_by(candidate->whole, issuer) ||
          mbedtls_x509_crt_has_ext_type(delegate.get(), MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE) == 0 ||
          mbedtls_x509_crt_check_extended_key_usage(delegate.get(), MBEDTLS_OID_OCSP_SIGNING, MBEDTLS_OID_SIZE(MBEDTLS_OID_OCSP_SIGNING)) != 0) {
        continue;
      }
      verified = verify_signed(*tbs, *algorithm, *signature, delegate.get()->pk);
    }
  }
  if (!verified) {
    return std::unexpected(std::move(verified.error()));
  }
  return *found;
}

Result<RevocationInfo> check_crl(std::span<const std::byte> crl, std::span<const std::byte> certificate, std::span<const std::byte> issuer) {
  auto c = parts_of(certificate);
  auto i = c ? parts_of(issuer) : Result<Parts>(std::unexpected(c.error()));
  if (!i) {
    return std::unexpected(std::move(i.error()));
  }
  // CertificateList { tbsCertList, signatureAlgorithm, signatureValue }
  Reader outer(crl);
  auto list = outer.expect(der::kSequence);
  auto lr = list ? Reader::into(*list) : Result<Reader>(std::unexpected(list.error()));
  auto tbs = lr ? lr->expect(der::kSequence) : Result<Element>(std::unexpected(lr.error()));
  auto algorithm = tbs ? lr->expect(der::kSequence) : Result<Element>(std::unexpected(tbs.error()));
  auto signature = algorithm ? lr->expect(der::kBitString) : Result<Element>(std::unexpected(algorithm.error()));
  // TBSCertList { version?, signature, issuer, thisUpdate, nextUpdate?, revokedCertificates?, [0] extensions? }
  auto r = signature ? Reader::into(*tbs, lr->depth()) : Result<Reader>(std::unexpected(signature.error()));
  if (!r || !r->next_if(der::kInteger)) {
    return unreadable("not a CRL");
  }
  auto inner_algorithm = r->expect(der::kSequence);
  auto crl_issuer = inner_algorithm ? r->expect(der::kSequence) : Result<Element>(std::unexpected(inner_algorithm.error()));
  auto this_update = crl_issuer ? r->next() : Result<Element>(std::unexpected(crl_issuer.error()));
  if (!this_update) {
    return unreadable("not a CRL");
  }
  if (!std::ranges::equal(crl_issuer->whole, c->issuer.whole) || !std::ranges::equal(crl_issuer->whole, i->subject.whole)) {
    return make_error(ErrorCode::kOutOfRange, "the CRL is another issuer's");
  }
  RevocationInfo info;
  info.status = RevocationStatus::kGood;
  info.this_update = time_of(this_update);
  if (const auto tag = r->peek(); tag && (*tag == der::kUtcTime || *tag == der::kGeneralizedTime)) {
    info.next_update = time_of(r->next());
  }
  if (r->peek() == der::kSequence) {
    auto revoked = r->expect(der::kSequence);
    auto rr = revoked ? Reader::into(*revoked, r->depth()) : Result<Reader>(std::unexpected(revoked.error()));
    while (rr && !rr->empty()) {
      auto entry = rr->expect(der::kSequence);
      auto er = entry ? Reader::into(*entry, rr->depth()) : Result<Reader>(std::unexpected(entry.error()));
      auto serial = er ? er->expect(der::kInteger) : Result<Element>(std::unexpected(er.error()));
      auto date = serial ? er->next() : Result<Element>(std::unexpected(serial.error()));
      if (!date) {
        return unreadable("a CRL entry cannot be read");
      }
      if (std::ranges::equal(serial->content, c->serial.content)) {
        info.status = RevocationStatus::kRevoked;
        info.revoked_at = time_of(date);
        break;
      }
    }
    if (!rr) {
      return unreadable("a CRL's entries cannot be read");
    }
  }
  mbed::Certificates issuer_cert;
  if (!issuer_cert.add(issuer)) {
    return unreadable("the issuer's certificate cannot be read");
  }
  if (auto verified = verify_signed(*tbs, *algorithm, *signature, issuer_cert.get()->pk); !verified) {
    return std::unexpected(std::move(verified.error()));
  }
  return info;
}

Result<std::string> ocsp_produced_at(std::span<const std::byte> response) {
  Reader outer(response);
  auto top = outer.expect(der::kSequence);
  auto tr = top ? Reader::into(*top) : Result<Reader>(std::unexpected(top.error()));
  auto status = tr ? tr->expect(0x0A) : Result<Element>(std::unexpected(tr.error()));
  auto wrapped = status ? tr->expect(der::context_constructed(0)) : Result<Element>(std::unexpected(status.error()));
  auto wr = wrapped ? Reader::into(*wrapped, tr->depth()) : Result<Reader>(std::unexpected(wrapped.error()));
  auto bytes = wr ? wr->expect(der::kSequence) : Result<Element>(std::unexpected(wr.error()));
  auto br = bytes ? Reader::into(*bytes, wr->depth()) : Result<Reader>(std::unexpected(bytes.error()));
  auto type = br ? br->expect(der::kOid) : Result<Element>(std::unexpected(br.error()));
  auto octets = type ? br->expect(der::kOctetString) : Result<Element>(std::unexpected(type.error()));
  if (!octets || oid_of(*type) != kOcspBasic) {
    return unreadable("not a successful OCSP response");
  }
  Reader basic_reader(octets->content, br->depth());
  auto basic = basic_reader.expect(der::kSequence);
  auto r = basic ? Reader::into(*basic, basic_reader.depth()) : Result<Reader>(std::unexpected(basic.error()));
  auto tbs = r ? r->expect(der::kSequence) : Result<Element>(std::unexpected(r.error()));
  auto dr = tbs ? Reader::into(*tbs, r->depth()) : Result<Reader>(std::unexpected(tbs.error()));
  if (!dr || !dr->next_if(der::context_constructed(0))) {
    return unreadable("not a successful OCSP response");
  }
  auto responder = dr->next();
  auto produced = responder ? dr->expect(der::kGeneralizedTime) : Result<Element>(std::unexpected(responder.error()));
  std::string time = time_of(produced);
  if (time.empty()) {
    return unreadable("not a successful OCSP response");
  }
  return time;
}

Result<std::string> crl_this_update(std::span<const std::byte> crl) {
  Reader outer(crl);
  auto list = outer.expect(der::kSequence);
  auto lr = list ? Reader::into(*list) : Result<Reader>(std::unexpected(list.error()));
  auto tbs = lr ? lr->expect(der::kSequence) : Result<Element>(std::unexpected(lr.error()));
  auto algorithm = tbs ? lr->expect(der::kSequence) : Result<Element>(std::unexpected(tbs.error()));
  auto signature = algorithm ? lr->expect(der::kBitString) : Result<Element>(std::unexpected(algorithm.error()));
  auto r = signature ? Reader::into(*tbs, lr->depth()) : Result<Reader>(std::unexpected(signature.error()));
  if (!r || !r->next_if(der::kInteger)) {
    return unreadable("not a CRL");
  }
  auto inner_algorithm = r->expect(der::kSequence);
  auto issuer = inner_algorithm ? r->expect(der::kSequence) : Result<Element>(std::unexpected(inner_algorithm.error()));
  std::string time = issuer ? time_of(r->next()) : std::string();
  if (time.empty()) {
    return unreadable("not a CRL");
  }
  return time;
}

}  // namespace capriole::crypto
