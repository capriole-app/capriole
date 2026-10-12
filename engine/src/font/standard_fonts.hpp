#pragma once

// The fonts a PDF may use without embedding them (ISO 32000-2 §9.6.2.2, the standard 14), and the
// substitutes for every other font that is not embedded: the Foxit fonts built into the engine
// (ADR 0005) — Sans, Serif and Fixed in four styles, Symbol, Dingbats — with the metrics of Adobe's
// core fonts, so that a standard font without /Widths still gets its widths.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace capriole::font::detail {

enum class StandardFont : std::uint8_t {
  kSans,
  kSansBold,
  kSansItalic,
  kSansBoldItalic,
  kSerif,
  kSerifBold,
  kSerifItalic,
  kSerifBoldItalic,
  kFixed,
  kFixedBold,
  kFixedItalic,
  kFixedBoldItalic,
  kSymbol,
  kDingbats,
};

// The program built into the engine (a CFF font).
[[nodiscard]] std::span<const unsigned char> standard_font_data(StandardFont font);

// The standard font a /BaseFont names, under its own name or the ones Windows and others give it
// (Arial, TimesNewRoman, CourierNew, ArialMT, "Arial,Bold", ...); nullopt for any other font.
[[nodiscard]] std::optional<StandardFont> standard_font_named(std::string_view base_font);

// What stands in for a font that is not embedded and is not a standard one: by its name when it tells
// (serif families, monospaced ones, bold, italic), else by the font descriptor's flags (§9.8.2) and
// italic angle.
[[nodiscard]] StandardFont substitute_font(std::string_view base_font, std::uint32_t flags, double italic_angle, double weight);

// Symbol and Dingbats: fonts with their own encoding, not the standard Latin one.
[[nodiscard]] constexpr bool is_symbolic(StandardFont font) noexcept { return font == StandardFont::kSymbol || font == StandardFont::kDingbats; }

// The /BaseFont without a subset tag ("ABCDEF+Name", §9.9.2).
[[nodiscard]] std::string_view without_subset_tag(std::string_view base_font);

}  // namespace capriole::font::detail
