#pragma once

// Editing a paragraph of a page (PLAN.md phase 9a): the lines whose characters lie in an area (the page as
// shown; consecutive lines of one block of the page's text, as laid out) are replaced by other text, laid
// out anew inside the paragraph — word by word to its margins, its first line's indent kept, on the same
// baselines (the same line pitch), aligned as it was (left, justified, centred, right). The rest of the
// page does not move (CAPRIOLE.md §5.4): new text longer than the paragraph takes lines below it only
// where the page is empty down to the next thing drawn (text, a path, an image) — else it is refused,
// unless asked to shrink (smaller text and pitch, down to kMinShrink).
//
// The old glyphs leave their strings without moving the rest (edit/redact's way, text only: in forms too)
// and the new text is drawn over the page: in the paragraph's own font when it draws every new character
// (its encoding names it, its program has the glyph; embedded fonts are often subsets), else in the
// standard font nearest it (only what WinAnsi encodes). Same size, colour and horizontal scaling.
//
// Not edited (kUnsupported): lines of several blocks or with a line between them left out, text that is
// not horizontal, invisible text, an annotation's text, a paragraph of more than one font, size or colour
// (a word in bold: edit its lines one by one, edit/text_edit), and — with "no room" in the message, and
// the lines needed and those that fit — new text that does not fit. kOutOfRange: no text in the area.

#include <cstdint>
#include <string>
#include <string_view>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/text_edit.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit {

// How far shrinking may make the text smaller.
inline constexpr double kMinShrink = 0.7;

enum class ParagraphAlignment : std::uint8_t { kLeft, kJustified, kCentre, kRight };

struct ParagraphOptions {
  // When the new text does not fit, make it smaller (with its line pitch) until it does, down to kMinShrink.
  bool shrink = false;
};

struct ParagraphEditResult {
  TextEditMethod method = TextEditMethod::kSameFont;
  std::string font;      // the font the new text is written in (its /BaseFont, or the standard one)
  std::string replaced;  // the text that was there, UTF-8 (its lines joined by spaces)
  std::uint32_t old_lines = 0;
  std::uint32_t lines = 0;  // the new text's
  double size = 0;          // the font size it is written in (points as shown)
  double scale = 1;         // that size over the paragraph's (below 1 when shrunk)
  ParagraphAlignment alignment = ParagraphAlignment::kLeft;
};

// text: UTF-8; a line break starts a new line; empty: the paragraph is deleted.
[[nodiscard]] Result<ParagraphEditResult> replace_paragraph(Document& document, std::uint32_t page_index, const graphics::Rect& area, std::string_view text,
                                                            const ParagraphOptions& options = {});

}  // namespace capriole::edit
