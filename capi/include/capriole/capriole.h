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
 * Registers a resource pages may need (fonts and CMaps too large to build into the engine: the resource
 * pack of scripts/fetch_resources.sh), by its name in the pack — "cmap/UniJIS-UCS2-H",
 * "font/NotoSansJP-Regular.otf" — with its bytes (copied). For every document of the process, from the
 * next page interpreted on; registering a name again replaces its data. Thread-safe.
 */
capriole_status capriole_resource_add(const char* name, const uint8_t* data, size_t size);

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

#ifdef __cplusplus
}
#endif

#endif /* CAPRIOLE_CAPRIOLE_H */
