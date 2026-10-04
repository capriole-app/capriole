#pragma once

// The digital signatures of a document (ISO 32000-2 §12.8, ETSI EN 319 142-1 PAdES; PLAN.md phase 8h):
// each signature field's signature dictionary, verified.
//
// - Integrity: the bytes the signature covers (/ByteRange: all of the file up to the end of its
//   revision but /Contents) are hashed and checked against what was signed, and the signature against
//   the signer's key. Formats: adbe.pkcs7.detached and ETSI.CAdES.detached (CMS, detached),
//   adbe.pkcs7.sha1 (CMS over a SHA-1 digest), adbe.x509.rsa_sha1 (raw PKCS#1, the certificates in
//   /Cert), ETSI.RFC3161 (document time stamps).
// - Coverage: whether the signature covers the whole file, or later revisions were added (changes made
//   after it was signed, or other signatures: telling them apart, as DocMDP does, is not done yet).
// - Time: the signature time stamp's, verified, else /M — what the signer claims.
// - Identity: whether the signer's certificate chains to a trusted root, only when the host gives the
//   roots it trusts (the engine trusts no one by itself); its validity at the signing time. Revocation
//   (OCSP, CRL, the /DSS) is not checked.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"

namespace capriole::signature {

enum class Integrity : std::uint8_t {
  kValid,        // what was signed is unchanged, and the signer's key verifies it
  kInvalid,      // changed, or the signature does not verify
  kUnsupported,  // a format or algorithm not read here, or a signature that cannot be read
};

enum class Trust : std::uint8_t {
  kNotChecked,  // no roots given
  kTrusted,     // the signer's certificate chains to one of them
  kUntrusted,
};

// (Its members in the order that packs them: sizes first, flags last.)
struct SignatureInfo {
  std::uint64_t signed_bytes = 0;  // where its revision ends
  std::string field_name;          // fully qualified, UTF-8
  std::string sub_filter;          // /SubFilter: "ETSI.CAdES.detached", "adbe.pkcs7.detached", …
  // From the signature dictionary, UTF-8: what the signer wrote.
  std::string name;
  std::string reason;
  std::string location;
  std::string contact_info;
  std::string date;          // /M, a PDF date, as written
  std::string problem;       // what is wrong, or not read, when not kValid
  std::string level;         // PAdES baseline level ("B-B", "B-T", "B-LT", "B-LTA"); empty when not PAdES
  std::string signing_time;  // the time stamp's, else /M
  std::string trust_problem;
  std::optional<std::string> timestamp;  // the time of a verified time stamp, a PDF date
  // Where it is shown: none for an invisible signature (no page, or a rectangle of no area).
  std::optional<graphics::Rect> rect;             // as shown (edit/annotations.hpp)
  std::optional<crypto::CertificateInfo> signer;  // the signer's certificate (the TSA's for a document time stamp)
  model::ObjectRef field;                         // the signature field
  std::optional<std::uint32_t> page;
  bool document_timestamp = false;  // a document time stamp (/Type /DocTimeStamp), not a person's signature
  Integrity integrity = Integrity::kUnsupported;
  bool covers_whole_file = false;
  bool timestamp_invalid = false;  // a time stamp that does not verify
  bool certificate_valid_at_signing = false;
  Trust trust = Trust::kNotChecked;
  std::optional<crypto::DigestAlgorithm> digest;
};

struct VerifyOptions {
  // The roots the host trusts: a PEM bundle, or DER certificates one after the other; none: not checked.
  std::span<const std::byte> trust_anchors;
};

// The signature fields that are signed, in the order of the form's field tree. Empty for a document with
// no form; a signature that cannot be read is listed as kUnsupported with what is wrong.
[[nodiscard]] Result<std::vector<SignatureInfo>> list_signatures(Document& document, const VerifyOptions& options = {});

}  // namespace capriole::signature
