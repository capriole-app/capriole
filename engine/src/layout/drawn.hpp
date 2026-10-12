#pragma once

// Internal to layout/ and edit/: what a page's content streams draw, operator by operator — each thing
// drawn with its box as shown, the clips and soft masks it is drawn in —, and its annotations. The page
// model groups it into elements (layout/page_model); edit/content_move moves it.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"
#include "capriole/text/text.hpp"
#include "edit/common.hpp"

namespace capriole::layout::detail {

// The text-showing operators of the page's content streams (not of annotations' appearances), as (stream
// number, none for the page's /Contents; operator ordinal).
[[nodiscard]] std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> content_operators(const graphics::DisplayList& list);
[[nodiscard]] bool of_content(const std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>>& operators, const text::Provenance& p);

[[nodiscard]] bool finite(const graphics::Rect& r);

// A run's glyphs as shown (their boxes, from the baseline's descent to its ascent); nullopt: none.
[[nodiscard]] std::optional<graphics::Rect> run_box(const graphics::ShowText& run, const graphics::Matrix& to_view);

// Whether an invocation is the page's content or a form it draws (not an annotation's appearance, nor a
// Type 3 glyph's procedure): what the page's edits rewrite.
[[nodiscard]] bool rewritable(const graphics::DisplayList& list, std::uint32_t invocation);

// What is drawn: by one operator of a content stream the page's edits rewrite, or an annotation.
struct Item {
  enum class Kind : std::uint8_t { kText, kFill, kStroke, kImage, kShading, kAnnotation };
  Kind kind = Kind::kFill;
  graphics::Rect box;
  std::uint32_t invocation = 0;
  std::uint32_t operation = 0;
  std::size_t command = 0;
  std::vector<std::size_t> clips;  // the clips in force (Clip indices)
  bool masked = false;             // under a soft mask
  bool text_clip = false;          // text that clips (Tr 4–7)
  bool plain = false;              // a fill of one simple shape, an image, a shading: what can be a background
  // kAnnotation: its object (none for a direct dictionary in /Annots), its /Subtype.
  std::optional<model::ObjectRef> annotation;
  std::string subtype;
};

struct Clip {
  graphics::Rect box;
  std::uint32_t invocation = 0;
  std::uint32_t operation = 0;
  std::size_t command = 0;
  bool rewritable = false;
  std::vector<std::size_t> under;  // the items it clips
};

struct Drawn {
  std::vector<Item> items;    // drawn by operators (in the display list's order), then the annotations
  std::size_t operators = 0;  // how many of the items are drawn by operators
  std::vector<Clip> clips;
};

// What the page's display list draws (any purpose), and its annotations — form widgets too, not popups
// (they go with their parent).
[[nodiscard]] Drawn drawn_items(Document& document, const edit::detail::Page& page, const graphics::DisplayList& list);

}  // namespace capriole::layout::detail
