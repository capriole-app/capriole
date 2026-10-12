#pragma once

// Editing the text of a page (PLAN.md phase 8j): the characters whose centres lie in an area (the page as
// shown; on one line, as the text is laid out — the text an earlier edit drew is on its line) are
// replaced with other text — a word, part of a line, a whole line.
//
// Two ways, the first that can:
// - the same font: when the characters come from one text-showing operator of the page's content (Tj,
//   TJ, ', ") and its font — an indirect object, as nearly always — draws every new character (its
//   encoding names it, its program has the glyph: embedded fonts are often subsets; codes the font's
//   /Widths leaves at 0 and the page does not show are given the program's width), the codes in that operator's strings are written anew — nothing
//   else of the content changes, byte for byte. The rest of what the operator shows moves along, as in an
//   editor; text further on the line, shown by other operators, keeps its place, so new text wider than
//   the room before it is not written this way.
// - a similar font: the old glyphs leave their strings without moving the rest (edit/redact's way, text
//   only: in forms too), and the new text is drawn where they began — same size, colour and direction —
//   in the standard font nearest the old one (sans, serif or monospaced; bold, italic), which the result
//   names. It is narrowed (horizontal scaling, down to half) to keep to the room before the next text.
//   Only what WinAnsi encodes can be written so.
//
// Not edited (kUnsupported): characters on more than one line; invisible text (a scan's text layer); an
// annotation's text (a form field's value, a signature's appearance).
// kOutOfRange: no character in the area.

#include <cstdint>
#include <string>
#include <string_view>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/font_library.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit {

enum class TextEditMethod : std::uint8_t { kSameFont, kSimilarFont };

struct TextEditOptions {
  // Fonts the host gave: an embedded subset that lacks a letter gets its glyph from the same font in it
  // (edit/font_library.hpp), and the text stays in its font.
  const FontLibrary* fonts = nullptr;
};

struct TextEditResult {
  TextEditMethod method = TextEditMethod::kSameFont;
  std::string font;                // the font the new text is written in (its /BaseFont, or the standard one)
  std::string replaced;            // the text that was there, UTF-8
  double scale = 1;                // kSimilarFont: the horizontal scaling it is written with
  std::uint32_t added_glyphs = 0;  // to the embedded font, from the library
  std::string missing_font;        // kSimilarFont because the text's font lacked letters: its name
};

// text: UTF-8; empty: the characters are deleted.
[[nodiscard]] Result<TextEditResult> replace_text(Document& document, std::uint32_t page_index, const graphics::Rect& area, std::string_view text,
                                                  const TextEditOptions& options = {});

}  // namespace capriole::edit
