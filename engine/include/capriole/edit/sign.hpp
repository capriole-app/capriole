#pragma once

// Signing a document (ISO 32000-2 §12.8, ETSI EN 319 142-1 PAdES baseline B-B, B-T; PLAN.md phase 8h).
//
// The signature is a new revision of the file: a signature field (a widget on a page: visible, with an
// appearance — lines of Helvetica in a rectangle —, or invisible), its signature dictionary
// (/Filter /Adobe.PPKLite, /SubFilter /ETSI.CAdES.detached, /M) and the form's /SigFlags are written by an
// incremental save (writer/save.hpp), with room in /Contents for the CMS signature (crypto/cms.hpp) and a
// /ByteRange that leaves out only that room. The file is then hashed and signed: nothing before the
// signature's revision changes, so earlier signatures stay valid, and nothing written afterwards may
// change its bytes (a later edit is a further revision).
//
// In two steps, for a signature time stamp (B-T): prepare_signature signs, and gives the RFC 3161 request
// the host sends to a time-stamping authority (the engine makes no network calls); finish_signature puts
// the response's token in and gives the signed file. Without a time stamp, finish_signature right away.
// The same document, credentials and request give the same bytes (the date is the caller's).
//
// For a key that is elsewhere (a smart card: PKCS#11, in the desktop app) a third step between the two:
// prepare_signature with the signer's certificates gives what the key signs (crypto::to_be_signed of
// PreparedSignature::cms, or its crypto::digest_info for CKM_RSA_PKCS), the host has it signed, and
// set_signature_value puts the value in (checked against the certificate's key); the time stamp request
// is made then. With credentials (a .p12) the engine makes the value itself, the same way: a key that
// signs the same bytes gives the same file either way.
//
// The document is changed (the field, the widget, the form): the signed file is what to go on with —
// reopened, so that what comes next is appended after the signature.
//
// Refused with kUnsupported: an encrypted document (its /Contents would have to stay unencrypted: not
// written yet); a document whose structure needed repairs (save it fully and reopen it first).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit {

struct SignRequest {
  // /M, a PDF date ("D:20261004143000+02'00'"): the engine reads no clock.
  std::string date;
  // The page of the widget (from 0), and its rectangle as shown (edit/annotations.hpp); no rectangle:
  // an invisible signature (on this page, of no area).
  std::uint32_t page = 0;
  std::optional<graphics::Rect> rect;
  // A visible signature's text, UTF-8, one line each ("{signer}" becomes the certificate's name); none:
  // "Digitally signed by {signer}" and the date.
  std::vector<std::string> lines;
  // The field's name; empty: "Signature1", "Signature2"… (the first that is free).
  std::string field_name;
  // In the signature dictionary, UTF-8; /Name defaults to the certificate's name.
  std::string name;
  std::string reason;
  std::string location;
  std::string contact_info;
  // Room for a signature time stamp (finish_signature with a response).
  bool timestamp = false;
};

struct PreparedSignature {
  std::vector<std::byte> file;        // the signed revision, /Contents still empty
  std::uint64_t contents_offset = 0;  // of the first hexadecimal digit of /Contents
  std::size_t contents_capacity = 0;  // bytes it holds
  crypto::CmsSignature cms;
  std::vector<std::byte> timestamp_request;  // RFC 3161, when SignRequest::timestamp (once the value is in)
  std::string field_name;
  bool timestamp = false;  // SignRequest::timestamp
};

// The signer's certificate (DER) and the others to include in the signature (its chain), for a key that
// is not given to the engine.
struct SignerCertificates {
  std::vector<std::byte> certificate;
  std::vector<std::vector<std::byte>> others;
};

[[nodiscard]] Result<PreparedSignature> prepare_signature(Document& document, const crypto::Credentials& credentials, const SignRequest& request);
// Without the value: PreparedSignature::cms has everything but it.
[[nodiscard]] Result<PreparedSignature> prepare_signature(Document& document, const SignerCertificates& signer, const SignRequest& request);
// The value made outside (crypto::set_value: RSA PKCS#1 v1.5, ECDSA as DER or r‖s), checked.
[[nodiscard]] Result<void> set_signature_value(PreparedSignature& prepared, std::span<const std::byte> value);

// The signed file (kUnsupported while the value is missing); with the time stamp of an RFC 3161 response when given (checked: granted, of this
// signature, verified).
[[nodiscard]] Result<std::vector<std::byte>> finish_signature(PreparedSignature prepared, std::span<const std::byte> timestamp_response = {});

}  // namespace capriole::edit
