#pragma once

// Fonts the host provides (PLAN.md phase 9c): full TrueType programs — a .ttf, a .otf with TrueType
// outlines, the faces of a .ttc — from which an embedded subset that lacks a letter gets its glyph
// (edit/text_edit, edit/paragraph_edit). A face serves only the subsets of the same font: the same
// PostScript name, units per em, and the outlines the subset has found among its own; and only when its
// embedding permissions allow documents that can be edited (OS/2 fsType: installable or editable; not
// "no subsetting", not bitmaps only).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::edit {

struct FontFace {
  std::string postscript_name;
  std::string family;
  std::string style;
  bool embeddable = false;  // its permissions allow adding its glyphs to a document
};

class FontLibrary {
 public:
  FontLibrary();
  // The faces of a font file, added (kUnsupported: not a TrueType font, PostScript outlines).
  [[nodiscard]] Result<std::vector<FontFace>> add(std::vector<std::byte> data);
  [[nodiscard]] std::vector<FontFace> faces() const;
  void clear();

  struct Face;  // internal (edit/font_extend)
  // The face with this PostScript name (a subset's /BaseFont, its tag aside); null when none.
  [[nodiscard]] const Face* find(std::string_view postscript_name) const;

 private:
  std::vector<std::shared_ptr<const Face>> faces_;
};

}  // namespace capriole::edit
