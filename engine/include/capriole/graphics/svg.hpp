#pragma once

// A display list as SVG: a vector preview of a page, for the Inspector and for checking the
// interpreter by eye. Paths, colours, alpha, line styles, clipping, blend modes, groups, soft masks and
// decoded images (embedded as PNG) are as in the file; what the renderer phases will draw properly is
// approximated: text in a generic font at the right positions (glyph outlines need the font
// programs), images not decoded as framed placeholders, shadings and patterns not drawn.
//
// The output is plain ASCII: every string that comes from the document is written as XML character
// references, so the SVG cannot carry markup or scripts from the file.

#include <cstddef>
#include <cstdint>
#include <string>

#include "capriole/graphics/display_list.hpp"

namespace capriole::graphics {

struct SvgOptions {
  // Commands written at most (a page can hold millions): past this, a comment says the rest is cut.
  std::size_t max_commands = 200'000;
  // Pixels of the images embedded (as PNG data), all together: past this, placeholders.
  std::int64_t max_embedded_pixels = 8'000'000;
};

// The page as an SVG document, sized in points, the crop box turned by /Rotate.
[[nodiscard]] std::string to_svg(const DisplayList& list, const SvgOptions& options = {});

}  // namespace capriole::graphics
