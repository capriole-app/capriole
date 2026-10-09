/*
 * Capriole C API — the stable boundary of the engine.
 *
 * Everything outside the engine (WASM bindings, the desktop shell via FFI, third-party embedders)
 * talks to it through this header only. Rules:
 *   - plain C, no C++ types, no exceptions crossing the boundary;
 *   - opaque handles, created and destroyed by paired functions;
 *   - every fallible call returns capriole_status; details via capriole_last_error_message();
 *   - additions only: removing or changing a function bumps CAPRIOLE_ABI_VERSION.
 */
#ifndef CAPRIOLE_CAPRIOLE_H
#define CAPRIOLE_CAPRIOLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPRIOLE_ABI_VERSION 0

typedef enum capriole_status {
  CAPRIOLE_OK = 0,
  CAPRIOLE_ERROR_IO = 1,
  CAPRIOLE_ERROR_OUT_OF_RANGE = 2,
  CAPRIOLE_ERROR_UNEXPECTED_EOF = 3,
  CAPRIOLE_ERROR_SYNTAX = 4,
  CAPRIOLE_ERROR_UNSUPPORTED = 5,
  CAPRIOLE_ERROR_LIMIT_EXCEEDED = 6,
  CAPRIOLE_ERROR_CORRUPT_DATA = 7,
  CAPRIOLE_ERROR_PASSWORD_REQUIRED = 8,
  CAPRIOLE_ERROR_INVALID_ARGUMENT = 100,
  CAPRIOLE_ERROR_INTERNAL = 101
} capriole_status;

/* Engine version as "major.minor.patch". The string is static; do not free it. */
const char* capriole_version(void);

/* ABI version this library was built with; compare with CAPRIOLE_ABI_VERSION. */
int capriole_abi_version(void);

/*
 * Message describing the last failed call on this thread, or "" if none. Valid until the next
 * API call on the same thread; do not free it.
 */
const char* capriole_last_error_message(void);

/*
 * Reads the PDF header of an in-memory file. On success writes the version to *major / *minor.
 * data may be NULL only when size is 0.
 */
capriole_status capriole_probe_header(const uint8_t* data, size_t size, int* major, int* minor);

/* ── Documents ──────────────────────────────────────────────────────────────────────────────── */

/* An open document. Not thread-safe: one thread at a time per handle. */
typedef struct capriole_document capriole_document;

/*
 * Opens a PDF held in memory; the bytes are copied, so the caller may release them afterwards.
 * password may be NULL (the empty password). repair: nonzero to let the engine repair damaged files
 * (what a viewer wants), zero for strict reading only. On success *out receives a handle to release
 * with capriole_document_close. CAPRIOLE_ERROR_PASSWORD_REQUIRED: encrypted, and the password does
 * not open it.
 */
capriole_status capriole_document_open(const uint8_t* data, size_t size, const char* password, int repair, capriole_document** out);

/* Releases a document (NULL is allowed). */
void capriole_document_close(capriole_document* document);

/* Releases a buffer returned by this API (JSON text, stream data, saved files). NULL is allowed. */
void capriole_free(void* buffer);

/*
 * The document at a glance, as JSON (UTF-8, NUL-terminated, release with capriole_free): header
 * version, size, encryption, cross-reference sections and entries, trailer, pages and the repairs
 * applied. The format is described in capi/README.md.
 */
capriole_status capriole_document_summary_json(capriole_document* document, char** json);

/* One object as JSON (see capi/README.md); a free or absent number gives null. */
capriole_status capriole_document_object_json(capriole_document* document, uint32_t number, char** json);

/*
 * The operations of page page_index (0-based, in page order) as JSON: its content stream parsed as a
 * viewer does (syntax errors skipped and counted). At most max_operations are listed; "total" says
 * how many there are. The format is described in capi/README.md.
 */
capriole_status capriole_document_page_content_json(capriole_document* document, uint32_t page_index, uint32_t max_operations, char** json);

/*
 * A vector preview of page page_index (0-based) as JSON: the page interpreted (content streams, forms,
 * graphics state) and written as SVG — paths, colours, clips, glyph outlines and images — with the size
 * of the drawing, the commands by kind, the interpreter's warnings and the missing resources.
 * The SVG is plain ASCII (the document's text is escaped). Format in capi/README.md.
 */
capriole_status capriole_document_page_preview_json(capriole_document* document, uint32_t page_index, char** json);

/*
 * Page page_index (0-based) drawn at dpi dots per inch (72 = one pixel per point): *rgba receives
 * *width x *height pixels, rows top to bottom, 4 bytes each (R, G, B, A, not premultiplied), on a white
 * page. Release *rgba with capriole_free. Paths, colours, clips, dashes, transparency, text and images;
 * resources the page wanted and did not find: capriole_last_missing_resources_json.
 * CAPRIOLE_ERROR_LIMIT_EXCEEDED when the bitmap would be larger than 50 million pixels.
 */
capriole_status capriole_document_page_render(capriole_document* document, uint32_t page_index, double dpi, uint8_t** rgba, uint32_t* width, uint32_t* height);

/* Reading orders of capriole_document_page_text_json. */
#define CAPRIOLE_TEXT_ORDER_CONTENT 0 /* the content's order, joined where the geometry continues (default) */
#define CAPRIOLE_TEXT_ORDER_LAYOUT 1  /* by columns */

/*
 * The text of page page_index (0-based) as JSON: the plain text in reading order, its blocks, lines and
 * words with their boxes, and every character with its box and where it comes from (content stream,
 * operator, byte of its code, font). Coordinates are points on the page as shown (origin at the top
 * left of the crop box, /Rotate applied) — a rendering at 72 dpi. order: CAPRIOLE_TEXT_ORDER_*. The
 * JSON is ASCII (text as \u escapes). Resources the text wanted and did not find:
 * "missing_resources", as for capriole_document_page_preview_json. Format in capi/README.md.
 */
capriole_status capriole_document_page_text_json(capriole_document* document, uint32_t page_index, int order, char** json);

/*
 * The model of page page_index (0-based) as JSON (phase 10a): its paragraphs (lines, margins, alignment,
 * line pitch, styles; why one cannot be laid out anew), the elements it shows — paragraphs, images,
 * drawings (rules, boxes, tables, backgrounds), shadings, annotations —, each with its box, header or
 * footer, column, and where it comes from (the display list's commands and the content streams'
 * operators), and its columns. Coordinates as for capriole_document_page_text_json. Format in
 * capi/README.md.
 */
capriole_status capriole_document_page_model_json(capriole_document* document, uint32_t page_index, char** json);

/*
 * Registers a resource pages may need (fonts and CMaps too large to build into the engine: the resource
 * pack of scripts/fetch_resources.sh), by its name in the pack — "cmap/UniJIS-UCS2-H",
 * "font/NotoSansJP-Regular.otf" — with its bytes (copied). For every document of the process, from the
 * next page interpreted on; registering a name again replaces its data. Thread-safe.
 */
capriole_status capriole_resource_add(const char* name, const uint8_t* data, size_t size);

/*
 * Fonts the host gives (PLAN.md phase 9c; edit/font_library.hpp): full TrueType fonts (.ttf, .otf with
 * TrueType outlines, every face of a .ttc), shared by every document. When an edit of text (replace_text,
 * replace_paragraph) needs a letter an embedded TrueType subset lacks, the glyph comes from the face of
 * the same font — the same PostScript name and units, its outlines among the face's — if its embedding
 * permissions allow edited documents; else the letter goes to a similar standard font. *json: the faces
 * added, [{"postscriptName", "family", "style", "embeddable"}]. A face replaces one of the same name.
 */
capriole_status capriole_font_add(const uint8_t* data, size_t size, char** json);
capriole_status capriole_fonts_json(char** json);
capriole_status capriole_fonts_clear(void);

/*
 * The resources the last page interpreted on this thread (preview or render) wanted and did not find,
 * as a JSON array of names (sorted; release with capriole_free): what to fetch and register before
 * drawing the page again. [] when nothing was missing or no page was interpreted.
 */
capriole_status capriole_last_missing_resources_json(char** json);

/*
 * The data of stream object number: decoded (all filters undone) when decoded is nonzero, otherwise
 * as stored (decrypted, still encoded). Release *data with capriole_free (it may be NULL when *size is
 * 0). CAPRIOLE_ERROR_UNSUPPORTED for filters the engine does not decode yet (e.g. images).
 */
capriole_status capriole_document_stream_data(capriole_document* document, uint32_t number, int decoded, uint8_t** data, size_t* size);

/*
 * Saves the document: a full save (the whole file written anew, repairs fixed in it) or, with
 * incremental nonzero, the original bytes followed by an update. modification_date (may be NULL) is a
 * PDF date ("D:20260927120000Z") written as /Info /ModDate. Release *data with capriole_free.
 */
capriole_status capriole_document_save(capriole_document* document, int incremental, const char* modification_date, uint8_t** data, size_t* size);

/* ── Page operations (edit/pages.hpp) ───────────────────────────────────────────────────── */

/*
 * Rotates pages (count indices from 0) by degrees clockwise, a multiple of 90 (negative allowed), on top
 * of their current rotation. CAPRIOLE_ERROR_UNSUPPORTED for another angle, CAPRIOLE_ERROR_OUT_OF_RANGE
 * for an index past the last page. The document changes in memory; save it to keep the result.
 */
capriole_status capriole_document_pages_rotate(capriole_document* document, const uint32_t* pages, size_t count, int degrees);

/*
 * Makes the document's pages, in this order, its current pages with these indices (from 0): pages left
 * out are deleted, a page listed again is duplicated. count must be at least 1.
 */
capriole_status capriole_document_pages_arrange(capriole_document* document, const uint32_t* order, size_t count);

/*
 * Copies pages of source (count indices from 0, each at most once) into document, before its page at
 * (= its page count: appended). Contents, resources and annotations come along; form fields join the
 * document's form. source stays as it is and may be closed afterwards.
 */
capriole_status capriole_document_pages_import(capriole_document* document, capriole_document* source, const uint32_t* pages, size_t count, uint32_t at);

/* ── Annotations (edit/annotations.hpp) ─────────────────────────────────────────────────── */

/*
 * The annotations of page page_index as JSON (an array, popups left out): for each its reference
 * ([number, generation], null for a direct object), subtype, rectangle as shown (as for text), contents
 * and author (Unicode), colour ([r, g, b] or null), hidden, editable. Format in capi/README.md.
 */
capriole_status capriole_document_page_annotations_json(capriole_document* document, uint32_t page_index, char** json);

/*
 * Adds an annotation to page page_index from a JSON specification (UTF-8; format in capi/README.md):
 * kind, geometry as shown, colour, text. Its appearance is generated. On success *number and
 * *generation receive its reference. CAPRIOLE_ERROR_SYNTAX for malformed JSON or a specification that
 * does not fit the kind.
 */
capriole_status capriole_document_annotation_add(capriole_document* document, uint32_t page_index, const char* spec_json, uint32_t* number,
                                                 uint32_t* generation);

/* Changes an annotation of page page_index (JSON: moveBy, contents, color, opacity, date). */
capriole_status capriole_document_annotation_update(capriole_document* document, uint32_t page_index, uint32_t number, uint32_t generation,
                                                    const char* change_json);

/* Removes an annotation (and its popup) from page page_index. */
capriole_status capriole_document_annotation_remove(capriole_document* document, uint32_t page_index, uint32_t number, uint32_t generation);

/* ── Forms (edit/forms.hpp) ──────────────────────────────────────────────────────────────── */

/*
 * The terminal fields of the document's form as JSON (an array; empty without a form): for each its
 * reference, fully qualified name and label (Unicode), type ("text", "checkbox", "radio", "pushbutton",
 * "combobox", "listbox", "signature"), value (an array of strings), options, flags, and widgets (page,
 * rectangle as shown, on state). Format in capi/README.md.
 */
capriole_status capriole_document_form_fields_json(capriole_document* document, char** json);

/*
 * Fills field number/generation with value_json (UTF-8): a string, an array of strings (list boxes take
 * several), or null to clear it. Its widgets get the appearance of the value. CAPRIOLE_ERROR_OUT_OF_RANGE
 * for a value the field does not take (not an option, a state it lacks, too long) or a reference that is
 * not a field; CAPRIOLE_ERROR_UNSUPPORTED for a read-only field, a signature, a push button.
 */
capriole_status capriole_document_form_field_fill(capriole_document* document, uint32_t number, uint32_t generation, const char* value_json);

/* ── Redaction (edit/redact.hpp) ───────────────────────────────────────────────────────────── */

/*
 * Redacts areas of page page_index — what lies there is taken out of the file —: spec_json
 * {"areas":[[x0,y0,x1,y1], …], "fill":[r,g,b] | null} (the page as shown; fill: the colour the areas are
 * filled with afterwards, null for none; black when left out). *json receives what was taken out:
 * {"glyphs", "paths", "imagesRemoved", "imagesCleared", "annotations"}. The document is then saved
 * fully only (an incremental save is refused: it would keep the original bytes).
 */
capriole_status capriole_document_page_redact(capriole_document* document, uint32_t page_index, const char* spec_json, char** json);

/* Applies the page's redaction marks (/Redact annotations: annotation_add with kind "redact") and removes them. */
capriole_status capriole_document_page_apply_redactions(capriole_document* document, uint32_t page_index, char** json);

/* ── Attachments and Factur-X (edit/attachments.hpp, edit/facturx.hpp) ────────────────────── */

/*
 * Replaces the text of a page's characters whose centres lie in an area, on one line (PLAN.md phase 8j;
 * edit/text_edit.hpp): spec_json {"area": [x0,y0,x1,y1] (the page as shown), "text": "…" (empty:
 * deleted)}. *json: {"method": "sameFont" | "similarFont", "font", "replaced", "scale"} — the same font
 * when it can write the new text in place — with glyphs a font given by capriole_font_add adds to it —,
 * else a similar standard font where the old text began; "addedGlyphs", and "missingFont": the font
 * that lacked letters, when they went to a similar one.
 */
capriole_status capriole_document_page_replace_text(capriole_document* document, uint32_t page_index, const char* spec_json, char** json);

/*
 * Replaces the text of a paragraph — the lines whose characters lie in an area, consecutive lines of one
 * block — and lays it out anew inside the paragraph (PLAN.md phase 9a; edit/paragraph_edit.hpp):
 * spec_json {"area": [x0,y0,x1,y1] (the page as shown), "text": "…" (a line break starts a new line;
 * empty: deleted), "room": "none" | "shrink" | "move" (when it does not fit: refused; smaller text; what is
 * below it moved down the page — PLAN.md phase 9d; "shrink": true is "room": "shrink")}. *json: {"method":
 * "sameFont" | "similarFont", "font", "replaced", "oldLines", "lines", "size", "scale", "alignment":
 * "left" | "justified" | "centre" | "right", "styles", "sameFontStyles"} — the styles (fonts, sizes, colours)
 * come from the old text word by word; "sameFontStyles" of the "styles" are in their own font;
 * "addedGlyphs" (capriole_font_add) and "missingFonts" (the fonts that lacked letters); "moved" (points
 * what follows went down; 0: not), "movedItems", "movedAnnotations". New text that does not fit is refused
 * (CAPRIOLE_ERROR_UNSUPPORTED, "no room: the new text needs N lines, M fit"); a move that cannot be made
 * too ("cannot move what follows: …").
 */
capriole_status capriole_document_page_replace_paragraph(capriole_document* document, uint32_t page_index, const char* spec_json, char** json);

/*
 * Moves, resizes or deletes an image, a drawing or a shading of the page — an element of
 * capriole_document_page_model_json, by its index there (PLAN.md phase 10b; edit/object_edit.hpp): spec_json
 * {"element": n, "move": [dx, dy]} | {"element": n, "box": [x0,y0,x1,y1]} | {"element": n, "delete": true}
 * (the page as shown). Only its operators change. *json: {"operators": n (changed or taken out), "box":
 * [x0,y0,x1,y1] (where it is now)}. Refused (CAPRIOLE_ERROR_UNSUPPORTED, "cannot edit the object: …"): text,
 * annotations, off the page, out of a clip that holds it, a path that clips others too, a soft mask;
 * CAPRIOLE_ERROR_OUT_OF_RANGE: no such element.
 */
capriole_status capriole_document_page_object_edit(capriole_document* document, uint32_t page_index, const char* spec_json, char** json);

/*
 * Converts the document to PDF/A (PLAN.md phase 8i; edit/pdfa.hpp): spec_json {"part": 2 | 3 | 0 (2, or 3
 * with embedded files), "date": PDF date (required)}. *data receives the file, written in full; *json what
 * was put right, {"part", "changes": [text…]}. The document is reopened from the file. A document that
 * cannot be made PDF/A is refused (CAPRIOLE_ERROR_UNSUPPORTED) with every reason in the message.
 */
capriole_status capriole_document_convert_pdfa(capriole_document* document, const char* spec_json, uint8_t** data, size_t* size, char** json);

/*
 * Digital signatures (PLAN.md phase 8h; format in capi/README.md).
 *
 * The certificate of a PKCS#12 file (.p12, .pfx) as JSON: {"name", "subject", "issuer", "serial",
 * "notBefore", "notAfter", "key", "ca", "chain"} (chain: how many other certificates it has).
 * CAPRIOLE_ERROR_PASSWORD_REQUIRED for a wrong password.
 */
capriole_status capriole_pkcs12_info(const uint8_t* p12, size_t size, const char* password, char** json);

typedef struct capriole_signing capriole_signing;

/*
 * Signs the document with the key of a PKCS#12 file, in two steps. spec_json: {"date": PDF date
 * (required), "page": index (0), "rect": [x0,y0,x1,y1] as shown | null (invisible), "lines": [text…]
 * ("{signer}" becomes the certificate's name), "fieldName", "name", "reason", "location",
 * "contactInfo", "timestamp": room for a time stamp (false)}. *signing holds the signed revision until
 * capriole_document_sign_finish; release it with capriole_signing_free. An encrypted document, or one
 * whose structure needed repairs, is refused (CAPRIOLE_ERROR_UNSUPPORTED).
 */
capriole_status capriole_document_sign_begin(capriole_document* document, const char* spec_json, const uint8_t* p12, size_t p12_size, const char* password,
                                             capriole_signing** signing);
/* The RFC 3161 request to send to a time-stamping authority (none — size 0 — without "timestamp"). */
capriole_status capriole_signing_timestamp_request(capriole_signing* signing, uint8_t** data, size_t* size);
/*
 * The signed file (with the time stamp of an RFC 3161 response, when given: response may be NULL). The
 * document is reopened from it: what comes next is appended after the signature.
 */
capriole_status capriole_document_sign_finish(capriole_document* document, capriole_signing* signing, const uint8_t* response, size_t response_size,
                                              uint8_t** data, size_t* size);
void capriole_signing_free(capriole_signing* signing);

/*
 * The document's signatures, verified, as JSON (an array; format in capi/README.md). anchors: the
 * roots the host trusts — a PEM bundle, or DER certificates one after the other —, NULL not to check
 * the signers' identity.
 */
capriole_status capriole_document_signatures_json(capriole_document* document, const uint8_t* anchors, size_t anchors_size, char** json);

/*
 * The document's attachments as JSON (an array): for each its file specification's reference, its name
 * in /EmbeddedFiles, file name, description, MIME type, size, dates, relationship, page (for a file
 * attachment annotation; null for the document's), and whether it is associated (/AF). Format in
 * capi/README.md.
 */
capriole_status capriole_document_attachments_json(capriole_document* document, char** json);

/*
 * The bytes of an attachment (*data freed with capriole_free; may be null for an empty file). checksum
 * (may be null) receives 1 when they match /CheckSum, 0 when they do not, -1 when there is none.
 */
capriole_status capriole_document_attachment_data(capriole_document* document, uint32_t number, uint32_t generation, uint8_t** data, size_t* size,
                                                  int* checksum);

/*
 * Embeds a file (data, size bytes) described by spec_json ({"fileName", "description", "mimeType",
 * "relationship", "creationDate", "modificationDate"}; fileName required); *number and *generation
 * receive its file specification.
 */
capriole_status capriole_document_attachment_add(capriole_document* document, const char* spec_json, const uint8_t* data, size_t size, uint32_t* number,
                                                 uint32_t* generation);

/* Removes one of the document's attachments (not an annotation's: CAPRIOLE_ERROR_UNSUPPORTED). */
capriole_status capriole_document_attachment_remove(capriole_document* document, uint32_t number, uint32_t generation);

/*
 * The document's Factur-X invoice as JSON: null when it has none, else {"ref", "fileName",
 * "conformanceLevel", "version", "pdfa3"}.
 */
capriole_status capriole_document_facturx_json(capriole_document* document, char** json);

/*
 * Embeds an invoice's XML (a Cross Industry Invoice) as the document's Factur-X invoice, with its XMP
 * metadata; date (required: PDF/A-3 asks for it) for the file's dates. *json receives {"ref", "conformanceLevel",
 * "pdfa3"}: pdfa3 false means the document is not PDF/A-3, so not yet a conforming Factur-X.
 */
capriole_status capriole_document_facturx_embed(capriole_document* document, const uint8_t* xml, size_t size, const char* date, char** json);

/*
 * A full save that leaves out the objects nothing references any more (after deleting or extracting
 * pages); otherwise as capriole_document_save with incremental = 0.
 */
capriole_status capriole_document_save_compact(capriole_document* document, const char* modification_date, uint8_t** data, size_t* size);

#ifdef __cplusplus
}
#endif

#endif /* CAPRIOLE_CAPRIOLE_H */
