#pragma once

// Saving documents (DOCS/CAPRIOLE.md §5.3, PLAN.md phase 3).
//
// Full save: the whole document is written anew, with a classic cross-reference table. Object
// numbers and generations are kept, so every reference stays valid. Objects that lived in object
// streams are written as ordinary objects; object streams and cross-reference streams themselves are
// not written (their content is). Stream data is copied as it is (never re-compressed) and /Length is
// set to its real size — which also fixes the wrong lengths the repair layer read around.
//
// Repairs exist only in memory, so what the output must carry is written explicitly: a page tree that
// needed repairs is written anew, flat, with the pages the repairs found (see page_tree_conforms()) —
// under a new root object when the catalog's is missing —, and references to objects the output does
// not have are written as null, which is what they read as (§7.3.10): no object added to the output
// later can take their number.
//
// Output is deterministic: the same document with the same edits gives the same bytes. The second
// /ID string is a hash of the bytes written (§14.4); the first is kept from the file when present.
// Nothing time-dependent is written unless the caller passes it (SaveOptions::modification_date).
//
// Encrypted documents are saved encrypted, with their own /Encrypt dictionary, key and methods (the
// first /ID string, which enters the key, is kept): strings and stream data of every object written
// are encrypted again for that object (PLAN.md 3d). Changing or removing the protection is not a save
// option yet. Permissions (/P) are the application's to enforce: the writer does not check them.

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/writer/byte_sink.hpp"

namespace capriole::writer {

struct SaveOptions {
  // When set, written as /Info /ModDate (a PDF date string, e.g. "D:20260926213000Z", §7.9.4).
  std::optional<std::string> modification_date;
  // Full save only: write just the objects reachable from the trailer (/Root, /Info, /Encrypt), as
  // after deleting or extracting pages, whose objects would otherwise stay in the file unused. Always
  // so for a redacted document (Document::redacted()): what was removed must not stay in the file.
  bool drop_unreachable = false;
  // Full save only: the version written in the header instead of the document's (PDF/A-2 and -3 are
  // based on PDF 1.7: edit/pdfa.hpp).
  std::optional<std::pair<int, int>> version = std::nullopt;
};

struct SaveReport {
  std::size_t objects_written = 0;
  // Objects that could not be read even with repairs, written as null so references stay valid.
  std::vector<std::uint32_t> unreadable_objects;
  // The page tree was written anew (flat), because the document's pages were found by repairs.
  bool page_tree_rewritten = false;
  // Objects left out by SaveOptions::drop_unreachable.
  std::size_t objects_dropped = 0;
};

[[nodiscard]] Result<SaveReport> save_full(Document& document, ByteSink& output, const SaveOptions& options = {});

// Incremental save (§7.5.6): the original file byte for byte, then — only if something changed — the
// Modified and New objects, free entries for the Deleted ones (generation + 1), and a new
// cross-reference section in the same form as the file's last one (table, or cross-reference stream
// written without filters) whose /Prev points to it. Nothing that was in the file is rewritten, so
// signatures over the original bytes stay valid (the "preferred" save of DOCS/CAPRIOLE.md §5).
// Without changes (and without SaveOptions::modification_date) the output is the original file.
//
// Encrypted documents: what is appended is encrypted like the rest of the file (see above).
// Refused with ErrorCode::kUnsupported: redacted documents (Document::redacted(): the original bytes,
// kept by an incremental save, hold what was removed); documents that needed repairs (Document::repairs() not empty, after reading the page tree) — appending
// to a structure that only reads with repairs would carry them over unrepaired; save_full writes the repaired document instead.
[[nodiscard]] Result<SaveReport> save_incremental(Document& document, ByteSink& output, const SaveOptions& options = {});

}  // namespace capriole::writer
