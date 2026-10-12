#pragma once

// Long-term validation data (ETSI EN 319 142-1 §5.4, ISO 32000-2 §12.8.4.3; PLAN.md phase 11b): the
// certificates, OCSP responses and CRLs that show a document's signatures valid when they were made,
// kept in the document security store (/DSS in the catalog) — a PAdES B-LT signature; with a document
// time stamp after it (edit/sign.hpp, prepare_document_timestamp), B-LTA.
//
// In two steps, because the engine makes no network calls: validation_needs says, for every certificate
// of every signature's chain (document time stamps' too) but the roots, where to ask and what (the OCSP
// request, the URLs the certificate gives); the host fetches, and add_validation_data writes what it got —
// with what the file already keeps — as a new revision (an incremental save, so the signatures stay
// valid), and a /VRI entry per signature listing what is about its chain.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/revocation.hpp"
#include "capriole/document/document.hpp"
#include "capriole/signature/signatures.hpp"

namespace capriole::edit {

struct NeededCertificate {
  std::vector<std::byte> certificate;
  std::string subject;  // its common name
  // Its issuer's certificate, when known (in the signature, the file, or given); none: fetch it first
  // (urls.ca_issuers), then ask again.
  std::optional<std::vector<std::byte>> issuer;
  crypto::CertificateUrls urls;
  std::vector<std::byte> ocsp_request;  // when the issuer is known
  bool answered = false;                // an OCSP response or CRL in the file or given says whether it is revoked
};

// The certificates every signature's chain needs answers for (each once, in the order of the signatures).
// known: what the host has already fetched (looked at with what the file keeps).
[[nodiscard]] Result<std::vector<NeededCertificate>> validation_needs(Document& document, const signature::ValidationData& known = {});

struct ValidationResult {
  std::vector<std::byte> file;   // the document with the new revision
  std::size_t certificates = 0;  // in the /DSS now
  std::size_t ocsp_responses = 0;
  std::size_t crls = 0;
};

// Writes data (DER: certificates, OCSP responses, CRLs) into the /DSS, after what it holds (each item
// once), with a /VRI entry per signature (the SHA-1 of its /Contents, upper-case hexadecimal). date: the
// revision's /ModDate (a PDF date; the engine reads no clock). Refused: a document with no signature;
// an item that is not a certificate, OCSP response or CRL (kSyntax).
[[nodiscard]] Result<ValidationResult> add_validation_data(Document& document, const signature::ValidationData& data, const std::string& date);

}  // namespace capriole::edit
