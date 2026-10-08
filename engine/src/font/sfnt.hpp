#pragma once

// TrueType programs as tables (an sfnt: OpenType §5, Apple TrueType Reference), read and written by the
// engine itself — FreeType (font/program.hpp) reads glyphs, it does not write fonts. What extending an
// embedded subset with glyphs of the full font (PLAN.md phase 9c) needs: the tables of a font or of one
// face of a collection, its PostScript name, units per em, embedding permissions and Unicode cmap, its
// glyphs' data and outlines, and the font written anew with glyphs added.
//
// Every byte is hostile: offsets and lengths are checked against the data, counts bounded.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::font::detail {

struct Sfnt {
  std::uint32_t version = 0x00010000;
  std::map<std::string, std::vector<std::byte>> tables;  // by tag
};

// How many faces a font file has (a TrueType collection: several; else 1; 0 when it is not an sfnt).
[[nodiscard]] std::uint32_t face_count(std::span<const std::byte> data);
// The tables of a font, or of face `face` of a collection.
[[nodiscard]] Result<Sfnt> read_sfnt(std::span<const std::byte> data, std::uint32_t face = 0);
// The font as a file: tables in tag order, 4-byte aligned, their checksums and head's adjustment.
[[nodiscard]] std::vector<std::byte> write_sfnt(const Sfnt& font);

[[nodiscard]] std::optional<std::string> postscript_name(const Sfnt& font);  // name ID 6
// A name of the font (1: family, 2: subfamily, 4: full name, 6: PostScript name), as ASCII.
[[nodiscard]] std::optional<std::string> name_string(const Sfnt& font, std::uint16_t wanted);
[[nodiscard]] std::optional<std::uint16_t> units_per_em(const Sfnt& font);
[[nodiscard]] std::optional<std::uint16_t> glyph_count(const Sfnt& font);
// A glyph's advance, in font units.
[[nodiscard]] std::optional<std::uint16_t> advance(const Sfnt& font, std::uint16_t glyph);
// OS/2 fsType: the embedding permissions (none: no OS/2, installable).
[[nodiscard]] std::uint16_t fs_type(const Sfnt& font);
// The Unicode cmap: (3,10) format 12, else (3,1) or (0,x) format 4. Empty when it has neither.
[[nodiscard]] std::map<char32_t, std::uint16_t> unicode_cmap(const Sfnt& font);

// A glyph's data in glyf (empty: a glyph with no outline); nullopt when the font has no such glyph.
[[nodiscard]] std::optional<std::vector<std::byte>> glyph_data(const Sfnt& font, std::uint16_t glyph);
// Whether two glyphs have the same outline: a simple glyph's contours and points (its instructions
// aside; composite glyphs: compared by nothing, false).
[[nodiscard]] bool same_outline(std::span<const std::byte> a, std::span<const std::byte> b);
// A simple glyph's outline as a key (equal outlines, equal keys); nullopt for an empty or composite one.
[[nodiscard]] std::optional<std::string> outline_key(std::span<const std::byte> glyph);

struct Extended {
  Sfnt font;
  std::map<std::uint16_t, std::uint16_t> glyphs;  // a glyph of the full font → its index in the font now
};

// The subset with glyphs of the full font added after its own (and the components of composite ones),
// unhinted (their instructions need the full font's programs); its metrics, maxp, head and hhea updated,
// the tables that list every glyph and cannot be extended (hdmx, LTSH, vmtx, vhea) and DSIG left out.
[[nodiscard]] Result<Extended> extend(const Sfnt& subset, const Sfnt& full, std::span<const std::uint16_t> glyphs);

// The codes a symbolic font's cmap maps — (1,0), else (3,0) (its 0xF0xx codes taken as their low byte) —
// to glyphs (0: none).
[[nodiscard]] std::map<std::uint8_t, std::uint16_t> symbol_cmap(const Sfnt& font);
// A symbolic font's cmap — (1,0) format 0, and (3,0) format 4 at 0xF000 + code — with entries added;
// nullopt when the font has neither to extend.
[[nodiscard]] std::optional<std::vector<std::byte>> symbol_cmap_with(const Sfnt& font, const std::map<std::uint8_t, std::uint16_t>& added);

// A Unicode cmap (one (3,1) format 4 subtable) with entries added to the font's own; nullopt when the
// font has none to extend or a character is beyond the BMP.
[[nodiscard]] std::optional<std::vector<std::byte>> cmap_with(const Sfnt& font, const std::map<char32_t, std::uint16_t>& added);

}  // namespace capriole::font::detail
