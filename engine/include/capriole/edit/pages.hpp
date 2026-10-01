#pragma once

// Page operations (PLAN.md phase 8a): rotate, rearrange (reorder, delete, duplicate) and import pages
// from another document — merging, extracting and splitting are made of these. They edit the low-level
// model (DOCS/CAPRIOLE.md §5): only the objects they change are marked, so an incremental save appends
// just those.
//
// The page tree: rotate_pages touches the pages alone. arrange_pages and import_pages write the tree
// anew, flat — the root /Pages node lists every page — keeping the root object: intermediate nodes are
// deleted, and a page whose parent changes takes the attributes it inherited from them (§7.7.3.4:
// Resources, MediaBox, CropBox, Rotate). Pages already directly under the root are left untouched.
//
// What points at a page that is deleted (outline items, link destinations, form widgets) is left as
// it is: a reference to a deleted object reads as null (§7.3.10), which readers take as "no target".
//
// The document edited must not have needed repairs (Document::repairs() empty, after its page tree was
// read): what repairs make of a damaged file exists in memory only — placeholder pages, a page tree
// found through /Parent links, numbers of missing objects that a new object would take. Those are
// refused with kUnsupported, before any change; a full save writes the repaired document out soundly
// (writer/save.hpp), and the document read back from it can be edited. The source of an import may be
// damaged: what it reads as is what is copied.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit {

// Rotates pages (indices from 0, in page order) by degrees clockwise, a multiple of 90 (negative
// allowed), on top of their current rotation — their own /Rotate or the one they inherit. kOutOfRange
// for an index past the last page, kUnsupported for an angle that is not a multiple of 90.
[[nodiscard]] Result<void> rotate_pages(Document& document, std::span<const std::uint32_t> pages, std::int32_t degrees);

// Makes the document's pages, in this order, the current pages with these indices (from 0). Pages left
// out are deleted; a page listed again is duplicated (a new page object sharing the contents and
// resources; its annotations are copied, except form widgets, which belong to their field once).
// kOutOfRange for an index past the last page or an empty list (a document has at least one page).
[[nodiscard]] Result<void> arrange_pages(Document& document, std::span<const std::uint32_t> order);

struct ImportLimits {
  // Objects copied in one import, and stream bytes held in memory for them (the copied streams leave
  // the source's file behind: they are read into memory, decrypted, still encoded).
  std::size_t max_objects = 4'000'000;
  std::uint64_t max_stream_bytes = 1'500'000'000;
};

// Copies pages of source (indices from 0, in the order given; a page at most once) into document,
// inserted before its page at (== page count: appended). Everything the pages use is copied once,
// renumbered — contents, resources, annotations —; references to source pages that are not imported
// become null; the structure tree (tags), threads and the outline are not carried over. Form fields
// reached through the pages' widgets join the document's /AcroForm (renamed "name_2", ... when the
// name is taken), with the source form's default resources it lacks. Returns the new pages.
[[nodiscard]] Result<std::vector<model::ObjectRef>> import_pages(Document& document, Document& source, std::span<const std::uint32_t> pages, std::uint32_t at,
                                                                 const ImportLimits& limits = {});

}  // namespace capriole::edit
