#pragma once

// The content of a page (ISO 32000-2 §7.7.3.3 /Contents): a stream, or an array of streams that
// together make one content stream — decoded and joined with a newline between them (the boundary
// between two streams is a token boundary, §7.8.2). No /Contents: an empty page.

#include <cstddef>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::content {

// Entries of a /Contents array that are not streams (e.g. null, where the repair layer found a missing
// object) are skipped; a stream that cannot be decoded is an error.
[[nodiscard]] Result<std::vector<std::byte>> page_content(Document& document, model::ObjectRef page);

}  // namespace capriole::content
