#pragma once

// A font program (ISO 32000-2 §9.9) read with FreeType (ADR 0005): an embedded font — TrueType or
// OpenType, Type 1 (FontFile), CFF bare or CID-keyed (FontFile3) — or a substitute for one that is not
// embedded. What text drawing needs from it: which glyph a code, a name or a CID selects, and the
// glyph's outline, unhinted, in glyph space of 1000 units per em.
//
// Every program has its own FreeType library with a memory budget: a hostile font cannot take more
// than its share, and nothing is shared between documents.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/graphics/display_list.hpp"

namespace capriole::font::detail {

struct ProgramLimits {
  // FreeType's memory for one program (its tables, charstrings, the glyph being loaded).
  std::size_t max_memory = std::size_t{256} << 20U;
};

// The character maps a program can have (FreeType synthesizes kUnicode for Type 1 and CFF fonts from
// their glyph names, and kBuiltin from their own encoding).
enum class Charmap : std::uint8_t {
  kUnicode,          // TrueType (3,1), (3,10), (0,x); Type 1/CFF: from the glyph names
  kMicrosoftSymbol,  // TrueType (3,0)
  kMacRoman,         // TrueType (1,0)
  kBuiltin,          // Type 1/CFF: the font's own encoding
};

class FontProgram {
 public:
  [[nodiscard]] static Result<std::unique_ptr<FontProgram>> open(std::shared_ptr<const std::vector<std::byte>> data, const ProgramLimits& limits = {});

  FontProgram(const FontProgram&) = delete;
  FontProgram& operator=(const FontProgram&) = delete;
  FontProgram(FontProgram&&) = delete;
  FontProgram& operator=(FontProgram&&) = delete;
  ~FontProgram();

  // TrueType outlines (an sfnt with glyf), as opposed to PostScript ones (Type 1, CFF, OpenType CFF).
  [[nodiscard]] bool is_truetype() const;
  // A CID-keyed CFF or Type 1 font: glyphs are selected by CID (glyph() takes CIDs).
  [[nodiscard]] bool is_cid_keyed() const;
  [[nodiscard]] std::uint32_t glyph_count() const;

  [[nodiscard]] bool has_charmap(Charmap charmap) const;
  // The glyph a code selects in a character map; 0 (.notdef) when none.
  [[nodiscard]] std::uint32_t glyph_for(Charmap charmap, std::uint32_t code) const;
  // The glyph of a PostScript glyph name (Type 1, CFF, TrueType with a post table); 0 when none.
  [[nodiscard]] std::uint32_t glyph_named(std::string_view name) const;

  // The outline of a glyph (a glyph index; a CID for CID-keyed fonts), in glyph space of 1000 units per
  // em; nullopt when the glyph cannot be loaded.
  [[nodiscard]] std::optional<graphics::Path> outline(std::uint32_t glyph) const;
  // Its horizontal advance in the same units.
  [[nodiscard]] std::optional<double> advance(std::uint32_t glyph) const;

  // For text extraction: a glyph's PostScript name, when the font has names; the Unicode character its
  // cmap gives each glyph (the first one, when several share a glyph); the font's ascent and descent in
  // glyph space (1000 units per em).
  [[nodiscard]] std::optional<std::string> glyph_name(std::uint32_t glyph) const;
  [[nodiscard]] std::unordered_map<std::uint32_t, char32_t> unicode_of_glyphs() const;
  [[nodiscard]] std::pair<double, double> ascent_descent() const;

 private:
  struct Impl;
  explicit FontProgram(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace capriole::font::detail
