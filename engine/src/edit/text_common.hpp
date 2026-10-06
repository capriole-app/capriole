#pragma once

// Internal to edit/: what editing a page's text (text_edit.cpp) and its paragraphs (paragraph_edit.cpp)
// share — writing text in a font of the document or in a similar standard one, telling the page's own
// text from an annotation's, and drawing new text over the page.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "capriole/text/text.hpp"
#include "font/font.hpp"

namespace capriole::edit::detail {

// The standard font nearest a font (its name, its descriptor's flags, italic angle and weight): one of
// the twelve Latin ones.
[[nodiscard]] std::string similar_font(Document& document, const std::optional<model::ObjectRef>& ref);

// The codes that write a text in a font: each character's code, the one whose glyph the font draws and
// whose meaning (ToUnicode, encoding) is that character. nullopt when a character has none. widths: the
// codes whose width the dictionary leaves at 0 (a code the document never used), with the program's.
[[nodiscard]] std::optional<std::string> encode_in(font::detail::Font& font, font::detail::FontContext& context, std::u32string_view text, bool two_byte,
                                                   std::map<std::uint32_t, double>& widths);

// Whether the widths of codes a font's /Widths leaves at 0 can be given: its FirstChar and Widths are
// there and hold the codes, and the page shows none of them in that font (a damaged font draws its text
// so, overlapped: giving them widths would move it).
[[nodiscard]] bool widths_writable(Document& document, model::ObjectRef font_ref, const std::map<std::uint32_t, double>& widths,
                                   const graphics::DisplayList& list);

// A simple font's /Widths given the widths of codes it left at 0.
[[nodiscard]] Result<void> set_widths(Document& document, model::ObjectRef font_ref, const std::map<std::uint32_t, double>& widths);

// The text-showing operators of the page's own content (its /Contents and the forms it draws), as
// (stream: none for /Contents, operation): text of the others — an annotation's appearance — is not
// the page's to edit.
[[nodiscard]] std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> content_operators(const graphics::DisplayList& list);
[[nodiscard]] bool of_content(const std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>>& operators, const text::Provenance& p);

// The fill colour of the run a character was shown by (black when not found).
[[nodiscard]] graphics::Rgb fill_of(const graphics::DisplayList& list, const text::Provenance& p);

// A font made a resource of the page under a name of its own (CprTextN), inherited resources copied in:
// the name.
[[nodiscard]] Result<std::string> add_font_resource(Document& document, std::uint32_t page_index, model::ObjectRef font);

// Operators appended to the page's content as a stream of their own, after the graphics states the
// content leaves open are closed (so that they draw in the page's own space).
[[nodiscard]] Result<void> append_content(Document& document, std::uint32_t page_index, std::string_view operators);

}  // namespace capriole::edit::detail
