#pragma once

// Converting a document to PDF/A (ISO 19005-2, -3: PDF/A-2b, PDF/A-3b; PLAN.md phase 8i): what the
// archival profile forbids or requires is put right, and what cannot be is reported instead of
// written — the document is then refused, not declared PDF/A when it is not.
//
// Put right:
// - colour: an output intent (GTS_PDFA1) with an sRGB profile (made by Little CMS), so that DeviceRGB
//   and DeviceGray are defined; a document's own output intent is kept when it is the only profile;
// - fonts: a font that is not embedded and is one of the standard 14 — or Arial, Times New Roman,
//   Courier New, metrically the same — gets its substitute embedded (the Foxit fonts, CFF), its /Widths
//   those of the program; an incomplete /CIDSet goes; an embedded TrueType CIDFont gets /CIDToGIDMap;
// - metadata: an XMP packet written from /Info (which it must agree with), with the PDF/A identification
//   (and the Factur-X properties of an invoice), uncompressed;
// - annotations: printable, never hidden; their appearances only /N; those not shown (hidden, of no
//   size), of types PDF/A forbids, or without an appearance the engine can make, go;
// - actions: JavaScript, Launch and the others PDF/A forbids, /AA, a document's JavaScript; XFA and
//   /NeedsRendering; transfer functions; /Interpolate, /Alternates and /OPI of images; LZW streams are
//   compressed anew with Flate; optional content configurations get a /Name and lose /AS;
// - embedded files (PDF/A-3 only): MIME type, modification date, /AFRelationship, the document's /AF;
// - the file: written anew, in full (no earlier revision kept), with /ID, version 1.7 at most; a
//   document that needed repairs is converted as repaired.
//
// Refused (kUnsupported, every reason in the message): an encrypted or signed document (converting would
// remove the protection, or break the signatures); a font used for visible text that is not embedded and
// has no metrically identical substitute; an embedded font whose program does not match its dictionary
// (widths, missing glyphs, .notdef shown); DeviceCMYK colour (with an RGB output intent: a CMYK profile
// would be needed); embedded files when PDF/A-2 is asked for.

#include <cstdint>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"

namespace capriole::edit {

struct PdfaOptions {
  // 2 or 3; 0: 2, or 3 when the document has embedded files.
  int part = 0;
  // The modification date written (/ModDate, xmp:ModifyDate, embedded files without one), a PDF date:
  // the engine reads no clock.
  std::string date;
};

struct PdfaResult {
  std::vector<std::byte> file;  // the document as PDF/A, written in full
  int part = 2;
  // What was put right, one line each ("fonts embedded: Helvetica, Arial,Bold", "annotations removed: 2").
  std::vector<std::string> changes;
};

// The conversion works on the document written in full and read back (a damaged file's repairs made,
// every stream as the output will have it): the document itself is not changed; the file is what to go
// on with.
[[nodiscard]] Result<PdfaResult> convert_to_pdfa(Document& document, const PdfaOptions& options);

}  // namespace capriole::edit
