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
// needed repairs is written anew, flat, with the pages the repairs found (see page_tree_conforms()).
//
// Output is deterministic: the same document with the same edits gives the same bytes. The second
// /ID string is a hash of the bytes written (§14.4); the first is kept from the file when present.
// Nothing time-dependent is written unless the caller passes it (SaveOptions::modification_date).
//
// Encrypted documents cannot be saved yet (ErrorCode::kUnsupported): re-encryption is PLAN.md 3d.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/writer/byte_sink.hpp"

namespace capriole::writer {

struct SaveOptions {
  // When set, written as /Info /ModDate (a PDF date string, e.g. "D:20260926213000Z", §7.9.4).
  std::optional<std::string> modification_date;
};

struct SaveReport {
  std::size_t objects_written = 0;
  // Objects that could not be read even with repairs, written as null so references stay valid.
  std::vector<std::uint32_t> unreadable_objects;
  // The page tree was written anew (flat), because the document's pages were found by repairs.
  bool page_tree_rewritten = false;
};

[[nodiscard]] Result<SaveReport> save_full(Document& document, ByteSink& output, const SaveOptions& options = {});

}  // namespace capriole::writer
