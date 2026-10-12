#pragma once

// Editing what a page shows as an object (PLAN.md phase 10b): an image, a drawing or a shading of the page's
// model (layout/page_model.hpp) moved, resized or deleted. Only its operators change (CAPRIOLE.md §5.3): a
// path's coordinates, an image, form or shading drawn through a matrix around it (q … cm … Q), a form all of
// whose drawing is the object's moved whole by its Do, a form changed copied (the others drawing it keep
// theirs); the rest of the page is written as it was.
//
// Refused (kUnsupported, "cannot edit the object: …"): what is not an image, a drawing or a shading (a
// paragraph is edited as text, edit/paragraph_edit; an annotation as one, edit/annotations); moved or resized
// off the page, out of a clip that holds it and does not move with it (a frame, a form's bounding box), when
// it clips what is drawn after it too, or is drawn through a soft mask; a resize to an empty box.
// kOutOfRange: no such element.

#include <cstddef>
#include <cstdint>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit {

enum class ObjectAction : std::uint8_t { kMove, kResize, kDelete };

struct ObjectEdit {
  std::size_t element = 0;  // in the page's model (layout::page_model), as it is now
  ObjectAction action = ObjectAction::kMove;
  graphics::Point by;  // kMove: how far, as shown (points, y down)
  graphics::Rect box;  // kResize: where its box goes, as shown (scaled to it each way)
};

struct ObjectEditResult {
  std::uint32_t operators = 0;  // the operators changed (or taken out)
  graphics::Rect box;           // where it is now, as shown (kDelete: where it was)
};

[[nodiscard]] Result<ObjectEditResult> edit_object(Document& document, std::uint32_t page_index, const ObjectEdit& edit);

}  // namespace capriole::edit
