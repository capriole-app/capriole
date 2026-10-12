#pragma once

// Factur-X / ZUGFeRD (PLAN.md phase 8f): an electronic invoice in a PDF — the invoice's XML (UN/CEFACT
// Cross Industry Invoice) embedded as an associated file of the document (edit/attachments.hpp) and
// named in its XMP metadata by the Factur-X extension schema (fx: DocumentType, DocumentFileName,
// Version, ConformanceLevel), declared for PDF/A (pdfaExtension).
//
// A Factur-X document is a PDF/A-3 one. Embedding keeps a PDF/A-3 document so (it adds an associated
// file with what PDF/A-3 asks of it); a document that is not PDF/A-3 gets the invoice and the metadata,
// but is NOT declared PDF/A-3 — it is not one — and the result says so: converting it is phase 8i.
//
// The engine reads of the XML only what it needs (its root, and the profile in
// GuidelineSpecifiedDocumentContextParameter), with bounded text searches; it does not validate it.

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit {

struct FacturxInfo {
  model::ObjectRef spec;          // the invoice's attachment
  std::string file_name;          // factur-x.xml, zugferd-invoice.xml, xrechnung.xml
  std::string conformance_level;  // the XMP's fx:ConformanceLevel ("EN 16931"); empty when not said
  std::string version;            // fx:Version
  bool pdfa3 = false;             // the XMP declares PDF/A-3
};

// The document's embedded invoice, if it has one (an attachment of the document with one of the names
// above, the associated one first).
[[nodiscard]] Result<std::optional<FacturxInfo>> find_facturx(Document& document);

// The Factur-X profile an invoice's XML declares: "MINIMUM", "BASIC WL", "BASIC", "EN 16931",
// "EXTENDED", "XRECHNUNG". kUnsupported for an XML that is not a Cross Industry Invoice, or whose
// guideline is not one of these.
[[nodiscard]] Result<std::string> facturx_profile(std::span<const std::byte> xml);

struct FacturxResult {
  model::ObjectRef spec;
  std::string conformance_level;
  bool pdfa3 = false;  // the document was, and stays, PDF/A-3
};

// Embeds xml as the document's Factur-X invoice (factur-x.xml; an invoice already there is replaced),
// and writes the Factur-X XMP metadata. date: a PDF date for the file's dates — required (PDF/A-3 asks
// associated files for /ModDate): kUnsupported when empty.
[[nodiscard]] Result<FacturxResult> embed_facturx(Document& document, std::span<const std::byte> xml, const std::string& date);

}  // namespace capriole::edit
