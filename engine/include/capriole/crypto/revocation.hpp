#pragma once

// Whether a certificate was revoked (PLAN.md phase 11b): OCSP (RFC 6960) and CRLs (RFC 5280 §5), what a
// long-term PDF signature keeps in its /DSS (ETSI EN 319 142-1 §5.4). The engine makes no network calls:
// it says where to ask (certificate_urls) and what (ocsp_request), the host fetches, and the answers are
// checked here — hostile input like every file: a response counts only when its issuer's key (or a
// responder the issuer authorised, id-kp-OCSPSigning) verifies it, and only for the certificate it names.
//
// Times are PDF dates in UTC ("D:20261009150000Z"); what they mean against a signing time is the
// caller's to judge (signature/signatures.hpp).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"

namespace capriole::crypto {

// Where a certificate says to look (http and https only): OCSP responders and the issuer's certificate
// (authority information access), CRLs (CRL distribution points).
struct CertificateUrls {
  std::vector<std::string> ocsp;
  std::vector<std::string> ca_issuers;
  std::vector<std::string> crl;
};
[[nodiscard]] Result<CertificateUrls> certificate_urls(std::span<const std::byte> certificate);

// Whether issuer issued certificate: its subject is the certificate's issuer, and its key verifies the
// certificate's signature.
[[nodiscard]] bool issued_by(std::span<const std::byte> certificate, std::span<const std::byte> issuer);

// The OCSP request for a certificate (SHA-1 CertID, as responders expect; no nonce: the same request
// for the same certificate).
[[nodiscard]] Result<der::Bytes> ocsp_request(std::span<const std::byte> certificate, std::span<const std::byte> issuer);

enum class RevocationStatus : std::uint8_t {
  kGood,
  kRevoked,
  kUnknown,  // the responder does not know the certificate
};

struct RevocationInfo {
  RevocationStatus status = RevocationStatus::kUnknown;
  std::string this_update;  // when the answer was true
  std::string next_update;  // until when (empty: not said)
  std::string revoked_at;   // kRevoked: since when
  std::string produced_at;  // an OCSP response's (empty for a CRL)
};

// What an OCSP response (OCSPResponse, DER) says of certificate. kOutOfRange: it is about other
// certificates; kCorruptData: not signed by the issuer or a responder it authorised; kSyntax: not a
// response, or not a successful one; kUnsupported: a response type or signature not read here.
[[nodiscard]] Result<RevocationInfo> check_ocsp(std::span<const std::byte> response, std::span<const std::byte> certificate, std::span<const std::byte> issuer);

// What a CRL (DER) says of certificate: kRevoked when it lists its serial. kOutOfRange: another issuer's
// CRL; kCorruptData: the issuer's key does not verify it.
[[nodiscard]] Result<RevocationInfo> check_crl(std::span<const std::byte> crl, std::span<const std::byte> certificate, std::span<const std::byte> issuer);

// Whether bytes read as an OCSP response (its producedAt, when successful) or a CRL (its thisUpdate),
// whoever they are about: kSyntax when not.
[[nodiscard]] Result<std::string> ocsp_produced_at(std::span<const std::byte> response);
[[nodiscard]] Result<std::string> crl_this_update(std::span<const std::byte> crl);

}  // namespace capriole::crypto
