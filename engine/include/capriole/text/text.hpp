#pragma once

// Text extraction (phase 7): the characters a page shows, with where they are and where they come from,
// grouped into words, lines and blocks in reading order.
//
// Coordinates are the page's as it is shown: points, origin at the top left of the crop box, y down,
// /Rotate applied — the same as a rendering at 72 dpi (graphics::output_geometry with scale 1).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"

namespace capriole::text {

// Where a character comes from: the text-showing operator (Tj, TJ, ' or ") and the glyph's code in it.
struct Provenance {
  std::optional<model::ObjectRef> stream;  // the form XObject or glyph procedure; none: the page's /Contents
  std::uint64_t offset = 0;                // of the operator in that stream (decoded; /Contents joined)
  std::uint32_t operation = 0;             // the operator's ordinal among the stream's operators
  std::uint32_t byte = 0;                  // where the glyph's code starts in the operator's strings, joined
  std::optional<model::ObjectRef> font;    // the font dictionary, when it is an indirect object
};

struct Char {
  // What the glyph stands for: usually one character, a few for a ligature (Latin ligatures are
  // expanded), U+FFFD when the font does not say; for a marked-content sequence with /ActualText, that
  // whole text on one Char spanning its glyphs.
  std::u32string text;
  graphics::Rect box;      // x0 < x1, y0 < y1 (y down); the glyph's width, without character spacing
  graphics::Point origin;  // on the baseline, where the glyph starts
  double size = 0;         // the font size as shown (the em, in points)
  std::int32_t angle = 0;  // of the baseline, degrees clockwise from rightwards (y down): 0, 90, 180, 270 or other
  bool invisible = false;  // render mode 3 or 7 (for example, the text layer of a scan)
  Provenance provenance;
};

// Ranges: a word is chars [first, first + count), a line words, a block lines.
struct Word {
  std::u32string text;
  graphics::Rect box;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};
struct Line {
  graphics::Rect box;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};
struct Block {
  graphics::Rect box;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};

struct PageText {
  double width = 0;  // of the page as shown
  double height = 0;
  std::vector<Char> chars;  // in reading order
  std::vector<Word> words;
  std::vector<Line> lines;
  std::vector<Block> blocks;

  // UTF-8: the words of a line separated by spaces, lines by a newline, blocks by an empty line.
  [[nodiscard]] std::string plain() const;
};

// Bounds against hostile pages: beyond max_chars the rest of the page's text is dropped; beyond
// max_ordered_blocks the blocks are ordered top to bottom, left to right, without the column analysis
// (which is cubic in the number of blocks).
struct TextLimits {
  std::size_t max_chars = 2'000'000;
  std::size_t max_ordered_blocks = 400;
};

// Reading order. kContent: lines and blocks follow the content stream, joined where the geometry
// continues (a line to the right on the same baseline, the next line below) — writers mostly draw in
// reading order, tables row by row. kLayout: lines are gathered by baseline over the whole page, blocks
// by vertical adjacency, and ordered by columns (Breuel's rules) — for content drawn out of order.
// kContent is the default: on the phase 7 sample it agrees with a reference engine on 95% of the pages,
// kLayout on 70% (PLAN.md 7d); choosing kLayout where the content goes back up within a column fixed one
// page for every 34 it broke.
enum class Order : std::uint8_t { kContent, kLayout };

// The text of a page from its display list (content::interpret_page).
[[nodiscard]] PageText extract(const graphics::DisplayList& list, Order order = Order::kContent, const TextLimits& limits = {});

}  // namespace capriole::text
