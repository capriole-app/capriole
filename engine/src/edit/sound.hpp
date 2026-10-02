#pragma once

// Internal to edit/: whether a document can be edited in place. Repairs that make a structure existing
// only in memory — a cross-reference table rebuilt by scanning (numbers of missing objects that a new
// object could take), a page tree rebuilt or whose node types were guessed, a catalog found by scanning
// (edits would go to an object the trailer does not name) — need a full save first: the document read
// back from it has none of this. Repairs of single objects (tolerant parsing, an object relocated or
// missing, data partly decoded, an offset header) appear as objects are read and do not stand in the way.

#include <algorithm>
#include <string>
#include <string_view>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"

namespace capriole::edit::detail {

[[nodiscard]] inline Result<void> require_sound_structure(Document& document, std::string_view what) {
  const bool structural = std::ranges::any_of(document.repairs(), [](const repair::RepairNote& note) {
    using enum repair::RepairKind;
    return note.kind == kXrefReconstructed || note.kind == kPageTreeRebuilt || note.kind == kPageTreeType || note.kind == kCatalogFound;
  });
  if (structural) {
    return make_error(ErrorCode::kUnsupported,
                      "the document's structure needed repairs: save it fully and open the result, then edit its " + std::string(what));
  }
  return {};
}

}  // namespace capriole::edit::detail
