#pragma once

// CMS SignedData (RFC 5652) for PDF signatures (ISO 32000-2 §12.8.3.3, ETSI EN 319 142-1 PAdES), and
// RFC 3161 time stamps.
//
// Signing: a detached signature in the PAdES baseline form (B-B): SHA-256; signed attributes
// content-type, message-digest and signing-certificate-v2 (ESS, RFC 5035), no signing-time (the time is
// the signature dictionary's /M); RSA PKCS#1 v1.5 or ECDSA (RFC 6979, deterministic); the signer's
// certificate and the others of its credentials. A signature time stamp (B-T) is an unsigned attribute
// added afterwards: the request is over the signature value, the response comes from a time-stamping
// authority the host asks (the engine makes no network calls).
//
// Verifying: hostile input. The signer's certificate is found by its identifier, else by trying each
// certificate's key; RSA (PKCS#1 v1.5, PSS) and ECDSA, the hashes MD5 to SHA-512. Whether a certificate
// chains to a trusted root is a separate question (check_chain), asked with roots the host gives: the
// engine trusts no one by itself. Revocation is crypto/revocation.hpp's.

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/crypto/pkcs12.hpp"

namespace capriole::crypto {

// What a certificate says, read.
struct CertificateInfo {
  std::string subject;      // the distinguished name, "C=IT, O=…, CN=…"
  std::string common_name;  // its CN, else the subject
  std::string issuer;
  std::string serial;      // hexadecimal
  std::string not_before;  // PDF dates, UTC
  std::string not_after;
  std::string key;  // "RSA 2048", "EC 256"
  bool ca = false;
};
[[nodiscard]] Result<CertificateInfo> certificate_info(std::span<const std::byte> der);
// Whether a certificate is valid at a time (a PDF date): within its notBefore–notAfter.
[[nodiscard]] bool valid_at(const CertificateInfo& certificate, std::string_view pdf_date);

// A detached signature made, before it is encoded.
struct CmsSignature {
  DigestAlgorithm digest = DigestAlgorithm::kSha256;
  der::Bytes signed_attributes;    // the SET that was signed
  der::Bytes signature_algorithm;  // AlgorithmIdentifier
  der::Bytes signature;
  der::Bytes signer_identifier;  // IssuerAndSerialNumber
  std::vector<der::Bytes> certificates;
};

// content_digest: the SHA-256 digest of what is signed (a PDF signature's byte ranges).
[[nodiscard]] Result<CmsSignature> sign_detached(const Credentials& credentials, std::span<const std::byte> content_digest);

// The same signature in two steps, for a key that is elsewhere (a smart card, PKCS#11): everything but
// the value, from the signer's certificate and the others to include; what the key signs is to_be_signed
// — the SHA-256 digest of signed_attributes (or the attributes themselves, for a mechanism that hashes:
// CKM_SHA256_RSA_PKCS, CKM_ECDSA_SHA256). kUnsupported for a key neither RSA nor EC.
[[nodiscard]] Result<CmsSignature> unsigned_detached(std::span<const std::byte> certificate, const std::vector<der::Bytes>& others,
                                                     std::span<const std::byte> content_digest);
[[nodiscard]] std::vector<std::byte> to_be_signed(const CmsSignature& signature);
// The DigestInfo (RFC 8017 §9.2) of to_be_signed: what a raw RSA PKCS#1 v1.5 mechanism (CKM_RSA_PKCS) signs.
[[nodiscard]] der::Bytes digest_info(const CmsSignature& signature);
// The longest value the signer's key can give (the room a PDF keeps for the signature); 0 when unknown.
[[nodiscard]] std::size_t longest_value(std::span<const std::byte> certificate);
// The value made by the key: RSA PKCS#1 v1.5; ECDSA as DER, or as r‖s (what PKCS#11 gives), made DER.
// Checked against the certificate's key first: kCorruptData when it does not verify (another key, another
// digest).
[[nodiscard]] Result<void> set_value(CmsSignature& signature, std::span<const std::byte> certificate, std::span<const std::byte> value);
// The value made here, with the credentials' key (what sign_detached does after unsigned_detached).
[[nodiscard]] Result<void> sign_with(CmsSignature& signature, const Credentials& credentials);
// The ContentInfo, DER; with a time stamp token when given (an unsigned attribute).
[[nodiscard]] der::Bytes encode(const CmsSignature& signature, std::span<const std::byte> timestamp_token = {});
// The RFC 3161 request for a time stamp of a signature value (SHA-256, certificates asked for).
[[nodiscard]] der::Bytes timestamp_request(std::span<const std::byte> signature_value);
// The token of a granted RFC 3161 response, checked: its imprint is that of the signature value.
[[nodiscard]] Result<der::Bytes> timestamp_token(std::span<const std::byte> response, std::span<const std::byte> signature_value);
// The same with the SHA-256 imprint given (a document time stamp's: the digest of the byte ranges).
[[nodiscard]] der::Bytes timestamp_request_of_digest(std::span<const std::byte> imprint);
[[nodiscard]] Result<der::Bytes> timestamp_token_of_digest(std::span<const std::byte> response, std::span<const std::byte> imprint);

struct CmsVerification {
  DigestAlgorithm digest = DigestAlgorithm::kSha256;
  bool digest_matches = false;   // the content's digest is the one signed
  bool signature_valid = false;  // the signer's key verifies the signature
  std::string problem;           // why not, when one of them is false
  std::vector<der::Bytes> certificates;
  std::optional<std::size_t> signer;  // in certificates
  bool signed_attributes = false;
  bool signing_certificate = false;           // an ESS signing-certificate(-v2) attribute naming the signer's
  std::optional<std::string> signing_time;    // the signing-time attribute, a PDF date
  std::optional<der::Bytes> timestamp_token;  // the signature time stamp, an unsigned attribute
  std::string content_type;                   // the encapsulated content's type
  std::optional<der::Bytes> content;          // and the content itself, when encapsulated
  der::Bytes signature;                       // the signature value
  // Revocation data the signer kept in a signed attribute (adbe-revocationInfoArchival).
  std::vector<der::Bytes> archived_crls;
  std::vector<der::Bytes> archived_ocsps;
};
// The certificates of a PEM bundle, or of DER certificates one after the other, as DER.
[[nodiscard]] std::vector<der::Bytes> certificates_of(std::span<const std::byte> bundle);

// content_digest: the digest of the detached content with the signer's algorithm; none: the
// encapsulated content is what is signed.
using ContentDigest = std::function<std::vector<std::byte>(DigestAlgorithm)>;
// An error when it is not a SignedData that can be read; else what verifying found.
[[nodiscard]] Result<CmsVerification> verify_cms(std::span<const std::byte> cms, const ContentDigest& content_digest = nullptr);

// A time stamp token's TSTInfo (RFC 3161 §2.4.2).
struct TimestampInfo {
  std::string time;  // genTime, a PDF date
  DigestAlgorithm imprint_algorithm = DigestAlgorithm::kSha256;
  der::Bytes imprint;
};
[[nodiscard]] Result<TimestampInfo> timestamp_info(const CmsVerification& token);

// A raw PKCS#1 v1.5 RSA signature over a digest, by a certificate's key (adbe.x509.rsa_sha1, §12.8.3.2).
[[nodiscard]] bool verify_pkcs1(std::span<const std::byte> certificate, DigestAlgorithm digest, std::span<const std::byte> hash,
                                std::span<const std::byte> signature);

struct ChainCheck {
  bool trusted = false;
  std::string problem;
};
// Whether certificates[signer] chains to one of the anchors (a PEM bundle, or DER certificates one after
// the other), through the other certificates. Validity periods are not checked here (valid_at: against
// the signing time, not the current one).
[[nodiscard]] ChainCheck check_chain(const std::vector<der::Bytes>& certificates, std::size_t signer, std::span<const std::byte> anchors);

}  // namespace capriole::crypto
