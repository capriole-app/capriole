#pragma once

// Internal to edit/: what is drawn below a paragraph moved down the page, the rest staying where it is
// (edit/paragraph_edit: a longer paragraph given room by the person's choice, CAPRIOLE.md §5.4). Every
// operator below is moved as it is — text by a text matrix set before it, paths by their coordinates,
// images, forms and shadings by a translation around them —, the annotations below by their geometry.

#include <cstdint>
#include <map>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit::detail {

// What to move: what lies below cut (the paragraph's bottom, the page as shown: y downwards) across
// [x0, x1] (its width), by `by` points down — the paragraph's new bottom at `bottom`. Not what is beside
// it, nor a footer (what starts in the page's last eighth, two lines of the paragraph's pitch below the
// rest at least), nor a frame that holds the paragraph (what moves stays inside it). What starts a little
// above the paragraph's bottom (a fifth of a line, 3 points at most) and goes on below it follows it.
struct MoveRequest {
  double top = 0;  // the paragraph's top: what holds it whole is a frame (it stays; what moves stays inside)
  double cut = 0;
  double x0 = 0;
  double x1 = 0;
  double by = 0;
  double bottom = 0;
  double pitch = 0;
  // The paragraph's glyphs (their boxes): what starts beside its last line without touching them, and the
  // next line's text however tight the lines, follow it; its own links (their centres above its bottom) stay.
  std::vector<graphics::Rect> glyphs;
};

// How an operator is moved (or taken out).
struct OpMove {
  enum class Kind : std::uint8_t {
    kPath,    // a path painted or clipping: its construction operators' coordinates through matrix
    kWrap,    // an image, a form, a shading (Do, inline image, sh): drawn through matrix (q … cm … Q)
    kText,    // text shown: at tm, set before it once text of its BT … ET moved
    kDelete,  // taken out: a path with its construction (one that also clips keeps clipping: n), Do, BI, sh
  };
  Kind kind = Kind::kPath;
  graphics::Matrix matrix;  // in the operator's user space
  graphics::Matrix tm;
  bool moved = false;
};

struct MovePlan {
  double by = 0;
  std::uint32_t items = 0;                                         // drawing operators moved
  std::map<std::uint32_t, std::map<std::uint32_t, OpMove>> edits;  // by invocation, by operator ordinal
  std::vector<graphics::Invocation> invocations;
  std::vector<model::ObjectRef> annotations;  // moved, by (dx, dy) in page space
  double dx = 0;
  double dy = 0;
};

// What moving would do, or why it cannot be done (kUnsupported, "cannot move what follows: …"): it would
// go past the page's bottom (half its bottom margin may be taken) or into a footer, run into what is
// beside it, leave a clip or a soft mask that does not move with it; or something is drawn across the
// paragraph's bottom (a box, a table, a rule).
[[nodiscard]] Result<MovePlan> plan_move(Document& document, std::uint32_t page_index, const MoveRequest& request);

// Moves it (the page's content written anew; forms changed are copied). Any plan of OpMove edits is applied
// so (edit/object_edit too).
[[nodiscard]] Result<void> apply_move(Document& document, std::uint32_t page_index, const MovePlan& plan);

}  // namespace capriole::edit::detail
