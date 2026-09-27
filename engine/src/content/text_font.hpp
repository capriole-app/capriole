#pragma once

// Internal to the interpreter: what text positioning needs from a font (ISO 32000-2 §9.2.4, §9.6–9.7)
// — how a string splits into character codes, and each code's advance width. No glyph outlines.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"
#include "color_space.hpp"

namespace capriole::content::detail {

struct TextFont {
  bool two_byte = false;      // Type0 with an Identity CMap: 2-byte codes, CID = code
  bool widths_known = false;  // false: advances are an estimate (no /Widths, e.g. a standard 14 font)
  // True when codes read as Latin text: a simple font with a standard encoding (WinAnsi, MacRoman,
  // Standard, no /Differences) or a non-symbolic standard 14 font without its own. Fonts with custom
  // encodings (embedded subsets, /Differences, CMaps) need ToUnicode, the text phase.
  bool latin_codes = false;
  double scale = 0.001;      // glyph space → text space (1/1000; Type3: its FontMatrix)
  double default_width = 0;  // glyph space: /MissingWidth, /DW, or the estimate
  std::uint32_t first_char = 0;
  std::vector<double> widths;  // simple fonts: /Widths from /FirstChar

  struct Range {
    std::uint32_t first;
    std::uint32_t last;
    double width;
  };
  std::vector<Range> cid_widths;  // Type0: /W as ranges (a list [w1 w2 ...] becomes one-code ranges)

  // The codes of a string (§9.4.3): one byte each, or two for 2-byte fonts (a stray last byte is dropped).
  [[nodiscard]] std::vector<std::uint32_t> codes(std::string_view bytes) const;
  // The horizontal advance of a code in glyph space (multiply by scale for text space).
  [[nodiscard]] double width(std::uint32_t code) const;
};

// The font of a Tf operator (a dictionary, or a reference to one). Never fails: what cannot be read
// becomes estimated widths, with a warning.
TextFont load_text_font(Document& document, const model::Object& font, Warnings& warnings);

}  // namespace capriole::content::detail
