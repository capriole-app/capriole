#pragma once

// Internal to edit/: an embedded TrueType subset extended with glyphs of a font the host provided
// (edit/font_library.hpp; PLAN.md phase 9c), for edit/text_edit and edit/paragraph_edit. The font is
// prepared apart — an object to load, measure and encode with, its streams in memory — and written into
// the document only when the edit is (commit): an edit refused leaves the document as it was.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/font_library.hpp"
#include "capriole/model/object.hpp"
#include "font/sfnt.hpp"

namespace capriole::edit {

struct FontLibrary::Face {
  font::detail::Sfnt sfnt;
  FontFace info;
  std::uint16_t units = 0;
  std::map<char32_t, std::uint16_t> cmap;
  std::unordered_set<std::string> outlines;  // of its simple glyphs (font/sfnt.hpp outline_key)
};

namespace detail {

struct FontExtension {
  model::Object font;                                           // the font dictionary, what changed in it inline
  std::vector<std::pair<std::uint32_t, model::Object>> writes;  // object number → its new value
  std::size_t added = 0;                                        // glyphs added
};

// The embedded font at `font` with the glyphs of `letters` it lacks, from the library's face of the same
// font. nullopt: none to add — no such face, or it is not the same (its name, units, outlines), its
// permissions do not allow it, the font is not a kind extended (extendable), or the face has none of
// the letters.
[[nodiscard]] Result<std::optional<FontExtension>> extend_font(Document& document, model::ObjectRef font, std::u32string_view letters,
                                                               const FontLibrary& library);
[[nodiscard]] Result<void> commit(Document& document, const FontExtension& extension);

// True for an embedded font of a kind extend_font extends — a CIDFontType2 with Identity-H and a
// /ToUnicode, a non-symbolic TrueType with WinAnsiEncoding, or a symbolic TrueType without /Encoding and
// with a /ToUnicode, its program in /FontFile2 — whatever fonts the host has given.
[[nodiscard]] bool extendable(Document& document, model::ObjectRef font);

// The font's name for a person: its /BaseFont without the subset tag.
[[nodiscard]] std::string font_name(Document& document, model::ObjectRef font);

}  // namespace detail
}  // namespace capriole::edit
