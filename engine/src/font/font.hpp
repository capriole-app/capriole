#pragma once

// The fonts of a page (ISO 32000-2 §9.5–9.10): what text needs from a font dictionary and its program.
// How the bytes of a string split into codes (one byte for simple fonts, a CMap for Type 0 fonts), each
// code's widths (horizontal, and vertical for vertical writing), and each glyph's outline — from the
// embedded program, or from a substitute when the font is not embedded (standard_fonts, and the CJK and
// Liberation fonts of the resource pack). Type 3 fonts have no outlines: their glyphs are content
// streams, which the interpreter runs.
//
// Loading never fails: what cannot be read is replaced (a substitute, estimated widths) and reported.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "cmap.hpp"
#include "program.hpp"
#include "standard_fonts.hpp"

namespace capriole::font::detail {

struct FontLimits {
  ProgramLimits program;
  CMapLimits cmap;
  // /W and /W2 entries of a composite font (ranges count once).
  std::size_t max_width_entries = std::size_t{1} << 20U;
  // Glyph outlines kept per font; past it they are converted again at each use.
  std::size_t max_cached_outlines = 100'000;
};

// What the fonts of a page share: the resources the host supplied, the predefined CMaps and the
// substitute programs once opened, and the resources that were wanted but missing.
class FontContext {
 public:
  explicit FontContext(const Resources* resources, FontLimits limits = {});

  [[nodiscard]] const FontLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] const Resources* resources() const noexcept { return resources_; }
  // A predefined CMap (Identity-H/V built in, the others from the resources) — nullptr when missing.
  [[nodiscard]] std::shared_ptr<const CMap> predefined_cmap(std::string_view name, std::size_t depth = 0);
  // A substitute built into the engine.
  [[nodiscard]] const FontProgram* standard(StandardFont font);
  // A font of the resource pack ("font/<file>") — nullptr when missing.
  [[nodiscard]] const FontProgram* pack_font(std::string_view file);
  // CID → Unicode for a character collection (Adobe-Japan1, -GB1, -CNS1, -Korea1, -KR), from the
  // reverse of its UTF-16 CMap — nullptr when missing.
  [[nodiscard]] const std::unordered_map<std::uint32_t, char32_t>* cid_unicode(std::string_view ordering);

  // The resources that were missing, sorted.
  [[nodiscard]] std::vector<std::string> missing() const { return {missing_.begin(), missing_.end()}; }

 private:
  Resources::Data resource(const std::string& name);

  const Resources* resources_;
  FontLimits limits_;
  std::map<std::string, std::shared_ptr<const CMap>, std::less<>> cmaps_;
  std::map<StandardFont, std::unique_ptr<FontProgram>> standard_;
  std::map<std::string, std::unique_ptr<FontProgram>, std::less<>> pack_;
  std::map<std::string, std::unordered_map<std::uint32_t, char32_t>, std::less<>> cid_unicode_;
  std::set<std::string> missing_;
};

class Font {
 public:
  enum class Kind : std::uint8_t { kSimple, kComposite, kType3 };

  struct Code {
    std::uint32_t code = 0;
    std::uint8_t length = 1;
    std::uint32_t cid = 0;  // composite fonts; the code itself for the others
  };

  // Vertical writing (§9.7.4.3), in glyph space: the vertical advance and the position vector from the
  // horizontal origin to the vertical one.
  struct VerticalMetrics {
    double advance = -1000;
    double vx = 0;
    double vy = 880;
  };

  // A font resource (a dictionary, or a reference to one). Problems go to warnings.
  [[nodiscard]] static std::shared_ptr<Font> load(Document& document, const model::Object& object, FontContext& context, std::vector<std::string>& warnings);

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool vertical() const noexcept { return vertical_; }
  // True when the widths come from the font (its dictionary, or a standard font's metrics); false when
  // they are an estimate.
  [[nodiscard]] bool widths_known() const noexcept { return widths_known_; }
  // True when codes read as Latin text (a standard encoding), for previews that write text as text.
  [[nodiscard]] bool latin_codes() const noexcept { return latin_codes_; }
  // Glyph space → text space: 1/1000, or a Type 3 font's matrix (its first element).
  [[nodiscard]] double scale() const noexcept { return scale_; }

  // The codes of a string, appended to out.
  void codes(std::string_view bytes, std::vector<Code>& out) const;
  // The horizontal advance of a code, in glyph space.
  [[nodiscard]] double width(const Code& code) const;
  [[nodiscard]] VerticalMetrics vertical_metrics(const Code& code) const;
  // Word spacing applies to the single-byte code 32 only (§9.3.3).
  [[nodiscard]] static bool is_word_space(const Code& code) noexcept { return code.length == 1 && code.code == 32; }

  // The outline of a code's glyph in text space for a font size of 1 — glyph space scaled, a vertical
  // font's glyph moved to its vertical origin, a substitute's glyph fitted to the width the PDF gives —
  // or null when there is none (Type 3 fonts, missing glyphs, missing programs).
  [[nodiscard]] std::shared_ptr<const graphics::Path> outline(const Code& code);

  // What a code stands for (§9.10.2): /ToUnicode first; for simple fonts the glyph's name (from the
  // encoding, else from the program) through the Adobe Glyph List; for composite fonts the character
  // collection's Unicode mapping (CJK) or the embedded program's cmap; else the code itself when it is a
  // printable character. Empty when nothing says.
  [[nodiscard]] std::u32string text(const Code& code);
  // What /ToUnicode alone says a code stands for (empty: it says nothing, or there is none) — the one
  // mapping every reader of a composite font agrees on.
  [[nodiscard]] bool has_to_unicode() const noexcept { return to_unicode_ != nullptr; }
  [[nodiscard]] std::u32string to_unicode_text(const Code& code) const;
  // Ascent and descent in text space for a font size of 1 (for the boxes of glyphs).
  [[nodiscard]] double ascent() const noexcept { return ascent_; }
  [[nodiscard]] double descent() const noexcept { return descent_; }

  // For conversions (edit/pdfa.hpp): the program and glyph a code is drawn with (no program: none), and
  // whether that program is the font's embedded one.
  struct GlyphSource {
    const FontProgram* program = nullptr;
    std::uint32_t glyph = 0;
    bool embedded = false;
  };
  [[nodiscard]] GlyphSource glyph_source(const Code& code, FontContext& context);
  // Simple fonts: the glyph name a code's encoding gives (/Differences, the base encoding; a
  // non-symbolic font without one: the standard encoding); empty when none says.
  [[nodiscard]] std::string glyph_name(std::uint32_t code) const;
  [[nodiscard]] bool embedded() const noexcept { return embedded_ != nullptr; }

  // Type 3 fonts (§9.6.4).
  [[nodiscard]] const graphics::Matrix& font_matrix() const noexcept { return font_matrix_; }
  // The glyph procedure of a code (possibly a reference); nullptr when the font has none for it.
  [[nodiscard]] const model::Object* char_proc(std::uint32_t code) const;
  [[nodiscard]] const model::Dict* type3_resources() const noexcept { return type3_resources_ ? type3_resources_->as_dict() : nullptr; }

 private:
  // Where a code's glyph comes from.
  struct Slot {
    const FontProgram* program = nullptr;
    std::uint32_t glyph = 0;
    double fit = 1;  // horizontal scale fitting a substitute's glyph to the PDF's width
  };

  struct Range {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    double width = 0;
  };
  struct VerticalRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    VerticalMetrics metrics;
  };

  void load_simple(Document& document, const model::Dict& dict, FontContext& context, std::vector<std::string>& warnings);
  void load_composite(Document& document, const model::Dict& dict, FontContext& context, std::vector<std::string>& warnings);
  void load_type3(Document& document, const model::Dict& dict, std::vector<std::string>& warnings);
  void load_to_unicode(Document& document, const model::Dict& dict, FontContext& context);
  [[nodiscard]] std::u32string lookup_text(const Code& code);
  [[nodiscard]] char32_t unicode_of_code(const Code& code) const;  // unicode_codes_: the character, or 0
  void set_extent(Document& document, const model::Dict* descriptor);
  [[nodiscard]] Slot slot(const Code& code, FontContext& context);
  [[nodiscard]] std::uint32_t simple_glyph(const FontProgram& program, std::uint32_t code) const;
  [[nodiscard]] std::uint32_t cid_glyph(std::uint32_t cid) const;

  double scale_ = 0.001;
  double default_width_ = 500;
  double ascent_ = 0.8;
  double descent_ = -0.2;
  // A predefined Unicode CMap (UniJIS-UCS2-H, UniGB-UTF16-V, ...): the codes are the characters.
  enum class UnicodeCodes : std::uint8_t { kNone, kUcs2, kUtf16, kUtf32, kUtf8 };
  UnicodeCodes unicode_codes_ = UnicodeCodes::kNone;

  // The programs: the embedded one, or substitutes (the standard one, and a fallback for glyphs it lacks).
  std::unique_ptr<FontProgram> embedded_;
  const FontProgram* primary_ = nullptr;
  const FontProgram* secondary_ = nullptr;
  FontContext* context_ = nullptr;  // valid while the page that loaded the font is interpreted

  // Simple fonts: /Widths from /FirstChar, and each code's glyph (worked out once, when first shown).
  std::vector<double> widths_;
  std::array<std::string, 256> names_;  // the glyph name of each code, from the encoding and /Differences
  std::array<Slot, 256> slots_{};

  // Composite fonts.
  std::shared_ptr<const CMap> cmap_;
  std::shared_ptr<const CMap> to_unicode_;
  std::vector<Range> cid_widths_;  // sorted by first
  std::vector<VerticalRange> cid_vertical_;
  VerticalMetrics default_vertical_;
  std::vector<std::uint16_t> cid_to_gid_;  // empty: identity
  std::string ordering_;

  // Type 3.
  graphics::Matrix font_matrix_{0.001, 0, 0, 0.001, 0, 0};
  std::optional<model::Object> char_procs_;
  std::optional<model::Object> type3_resources_;

  std::unordered_map<std::uint32_t, std::shared_ptr<const graphics::Path>> outlines_;
  std::optional<std::unordered_map<std::uint32_t, char32_t>> glyph_unicode_;  // the program's cmap, reversed (made when first needed)
  std::unordered_map<std::uint64_t, std::u32string> texts_;                   // text() by code and length

  // The small fields together (no padding between them).
  std::uint32_t first_char_ = 0;
  Kind kind_ = Kind::kSimple;
  StandardFont substitute_ = StandardFont::kSans;
  bool vertical_ = false;
  bool widths_known_ = false;
  bool latin_codes_ = false;
  bool symbolic_ = false;  // the descriptor's Symbolic flag (§9.8.2)
  bool has_encoding_ = false;
  bool has_differences_ = false;
  bool cmap_missing_ = false;
  bool cid_truetype_ = false;
  bool substituted_ = false;
  std::array<bool, 256> slot_known_{};
};

}  // namespace capriole::font::detail
