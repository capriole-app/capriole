#include "standard_fonts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>

namespace capriole::font::detail {

// Generated (cmake/EmbedFiles.cmake): the Foxit programs, in StandardFont order.
std::span<const unsigned char> embedded_standard_font(std::size_t index);

namespace {

// Lower case, without spaces, hyphens, commas and underscores: "Arial,Bold" and "Arial-Bold" and
// "Arial Bold" read alike.
std::string normalized(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    if (c != ' ' && c != '-' && c != ',' && c != '_') {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

bool names_bold(std::string_view n) { return contains(n, "bold") || contains(n, "black") || contains(n, "heavy") || contains(n, "demi"); }
bool names_italic(std::string_view n) { return contains(n, "italic") || contains(n, "oblique"); }

StandardFont styled(StandardFont regular, bool bold, bool italic) {
  const auto base = static_cast<std::uint8_t>(regular);
  return static_cast<StandardFont>(base + (bold ? 1U : 0U) + (italic ? 2U : 0U));
}

}  // namespace

std::span<const unsigned char> standard_font_data(StandardFont font) { return embedded_standard_font(static_cast<std::size_t>(font)); }

std::string_view without_subset_tag(std::string_view base_font) {
  // Six upper-case letters and a plus sign.
  constexpr std::size_t kTag = 7;
  if (base_font.size() > kTag && base_font[kTag - 1] == '+' &&
      std::all_of(base_font.begin(), base_font.begin() + kTag - 1, [](char c) { return c >= 'A' && c <= 'Z'; })) {
    return base_font.substr(kTag);
  }
  return base_font;
}

std::optional<StandardFont> standard_font_named(std::string_view base_font) {
  const std::string n = normalized(without_subset_tag(base_font));
  if (n.starts_with("symbol")) {
    return StandardFont::kSymbol;
  }
  if (n.starts_with("zapfdingbats") || n == "dingbats") {
    return StandardFont::kDingbats;
  }
  const bool bold = names_bold(n);
  const bool italic = names_italic(n);
  if (n.starts_with("helvetica") || n.starts_with("arial")) {
    return styled(StandardFont::kSans, bold, italic);
  }
  if (n.starts_with("times")) {
    return styled(StandardFont::kSerif, bold, italic);
  }
  if (n.starts_with("courier")) {
    return styled(StandardFont::kFixed, bold, italic);
  }
  return std::nullopt;
}

StandardFont substitute_font(std::string_view base_font, std::uint32_t flags, double italic_angle, double weight) {
  if (const auto standard = standard_font_named(base_font); standard && !is_symbolic(*standard)) {
    return *standard;
  }
  const std::string n = normalized(without_subset_tag(base_font));
  // Flags (§9.8.2, Table 121): bit 1 FixedPitch, 2 Serif, 7 Italic, 19 ForceBold.
  constexpr std::uint32_t kFixedPitch = 1U << 0U;
  constexpr std::uint32_t kSerif = 1U << 1U;
  constexpr std::uint32_t kItalic = 1U << 6U;
  constexpr std::uint32_t kForceBold = 1U << 18U;
  constexpr std::array<std::string_view, 5> kMonospaced = {"mono", "courier", "consolas", "typewriter", "fixed"};
  constexpr std::array<std::string_view, 12> kSerifs = {"times",    "roman",   "georgia", "garamond", "cambria", "minion",
                                                        "palatino", "bookman", "book",    "century",  "caslon",  "baskerville"};
  const bool sans_named = contains(n, "sans") || contains(n, "gothic");
  StandardFont family = StandardFont::kSans;
  if (std::ranges::any_of(kMonospaced, [&](std::string_view part) { return contains(n, part); }) || (flags & kFixedPitch) != 0) {
    family = StandardFont::kFixed;
  } else if (!sans_named &&
             (std::ranges::any_of(kSerifs, [&](std::string_view part) { return contains(n, part); }) || contains(n, "serif") || (flags & kSerif) != 0)) {
    family = StandardFont::kSerif;
  }
  constexpr double kBoldWeight = 600;
  const bool bold = names_bold(n) || (flags & kForceBold) != 0 || weight >= kBoldWeight;
  const bool italic = names_italic(n) || (flags & kItalic) != 0 || italic_angle != 0;
  return styled(family, bold, italic);
}

}  // namespace capriole::font::detail
