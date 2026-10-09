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
//   roots it trusts (the engine trusts no one by itself); its validity at the signing time.
// - Revocation (PLAN.md phase 11b): of each certificate of the signer's chain but the root, from the
//   OCSP responses and CRLs the file keeps (the /DSS, ETSI EN 319 142-1 §5.4; the signature's
//   adbe-revocationInfoArchival attribute) and those the host fetched (VerifyOptions). Not checked when
//   there are none at all.

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

enum class Revocation : std::uint8_t {
  kNotChecked,  // no revocation data, in the file or given
  kGood,        // every certificate of the chain but the root has an answer, and none is revoked
  kRevoked,     // one is revoked (and no verified time stamp says the signature came first)
  kUnknown,     // one has no answer that can be used (none, or not for it, or not verified)
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
  std::string revocation_problem;        // why not kGood, or a note (revoked after a time stamp)
  std::string revoked_at;                // kRevoked: since when, a PDF date
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
  Revocation revocation = Revocation::kNotChecked;
  bool revocation_in_file = false;  // what kGood rests on is all in the file (long-term validation)
  std::optional<crypto::DigestAlgorithm> digest;
};

struct VerifyOptions {
  // The roots the host trusts: a PEM bundle, or DER certificates one after the other; none: not checked.
  std::span<const std::byte> trust_anchors;
  // Validation data the host fetched (DER): issuers' certificates, OCSP responses, CRLs.
  std::vector<std::vector<std::byte>> certificates;
  std::vector<std::vector<std::byte>> ocsp_responses;
  std::vector<std::vector<std::byte>> crls;
};

// The validation data a file keeps in its /DSS (DER, in its order), at most a few hundred of each.
struct ValidationData {
  std::vector<std::vector<std::byte>> certificates;
  std::vector<std::vector<std::byte>> ocsp_responses;
  std::vector<std::vector<std::byte>> crls;
};
[[nodiscard]] Result<ValidationData> document_validation_data(Document& document);

// The chain of a signer's certificate up to a root (self-signed) or as far as the certificates known go:
// certificates[signer] first; complete is false when an issuer is missing.
struct Chain {
  std::vector<std::vector<std::byte>> certificates;
  bool complete = false;
};
[[nodiscard]] Chain chain_of(std::span<const std::byte> certificate, const std::vector<std::vector<std::byte>>& known);

// What a signature is made of, for those who add to it (edit/dss.hpp): its /Contents as the file has it
// (padding included), its certificates, the signer's among them.
struct SignatureParts {
  std::string field_name;
  std::vector<std::byte> contents;
  std::vector<std::vector<std::byte>> certificates;
  std::optional<std::size_t> signer;
  // Its signature time stamp's (the time-stamping authority's certificates), when it has one.
  std::vector<std::vector<std::byte>> timestamp_certificates;
  std::optional<std::size_t> timestamp_signer;
  bool document_timestamp = false;
};
// The parts of the signed signature fields whose dictionary reads, in the order of list_signatures.
[[nodiscard]] Result<std::vector<SignatureParts>> signature_parts(Document& document);

// The signature fields that are signed, in the order of the form's field tree. Empty for a document with
// no form; a signature that cannot be read is listed as kUnsupported with what is wrong.
[[nodiscard]] Result<std::vector<SignatureInfo>> list_signatures(Document& document, const VerifyOptions& options = {});

}  // namespace capriole::signature
