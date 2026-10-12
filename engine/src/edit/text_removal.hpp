#pragma once

// Internal to edit/: taking glyphs out of a page's content without moving the rest (redact.cpp's
// machinery, text only), for edit/text_edit, which writes the new text itself.

#include <cstdint>
#include <span>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit::detail {

// The glyphs whose centre (or a fifth of whose box) lies in an area (the page as shown) leave their
// strings — in the page's content and the forms it draws (copied when changed) —, the glyphs after them
// keeping their places; paths, images and annotations stay. How many went.
[[nodiscard]] Result<std::uint32_t> remove_glyphs(Document& document, std::uint32_t page_index, std::span<const graphics::Rect> areas);

}  // namespace capriole::edit::detail
