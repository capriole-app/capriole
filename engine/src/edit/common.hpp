#pragma once

// Internal to edit/: what annotations and form fields share — numbers and arrays as the editor writes
// them, a page and how its space maps onto the page as shown, Helvetica (the font the editor writes text
// with when nothing better is at hand) with its metrics and line wrapping, default appearance strings
// (/DA, §12.7.4.3), and appearance streams.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit::detail {

using Rgb = std::array<double, 3>;  // each 0..1

[[nodiscard]] model::Name name(std::string text);
// Short and exact enough for page coordinates; no "-0".
[[nodiscard]] std::string num(double v);
[[nodiscard]] model::Object number_object(double v);
[[nodiscard]] model::Array rect_array(const graphics::Rect& r);
[[nodiscard]] model::Array rgb_array(const Rgb& c);
[[nodiscard]] Rgb clamp(Rgb c);
[[nodiscard]] std::string rgb_operands(const Rgb& c);
[[nodiscard]] graphics::Rect bounds(std::span<const graphics::Point> points);
// A rectangle of some space, through a matrix, as its bounds.
[[nodiscard]] graphics::Rect transformed(const graphics::Rect& r, const graphics::Matrix& m);
// The bounds of a path's points (control points included); none for an empty path.
[[nodiscard]] std::optional<graphics::Rect> path_bounds(const graphics::Path& path);
[[nodiscard]] graphics::Rect grow(const graphics::Rect& r, double by);
// <hex> for bytes, as a string operand.
[[nodiscard]] std::string hex(std::string_view bytes);

// A page, and how its space maps onto the page as shown (as a rendering at 72 dpi: the crop box, y down,
// /Rotate applied).
struct Page {
  model::ObjectRef ref;
  model::Dict dict;
  graphics::Matrix to_view;  // page space → as shown
  graphics::Matrix to_user;  // as shown → page space

  [[nodiscard]] graphics::Point user(graphics::Point p) const { return to_user.apply(p); }
  [[nodiscard]] graphics::Rect user(const graphics::Rect& r) const;
  [[nodiscard]] graphics::Rect view(const graphics::Rect& r) const;
};

// An attribute the page has or inherits (§7.7.3.4).
[[nodiscard]] std::optional<model::Object> inherited(Document& document, const model::Dict& page, std::string_view key);
// Four numbers, normalised.
[[nodiscard]] std::optional<graphics::Rect> rect_of(const std::optional<model::Object>& value);

// A page to read, or to edit (the structure must be sound, sound.hpp; what names the edits refused).
[[nodiscard]] Result<Page> page_at(Document& document, std::uint32_t index, std::optional<std::string_view> edit);

// Moves the coordinates of an annotation's geometry (/Rect, /QuadPoints, /L, /Vertices, /CL, /InkList) by
// (dx, dy) in page space: its appearance follows /Rect.
void shift_annotation(Document& document, model::Dict& annotation, double dx, double dy);

// /Subtype of an annotation; empty when it has none.
[[nodiscard]] std::string subtype_of(const model::Dict& annotation);
// A text string entry as UTF-8; empty when absent.
[[nodiscard]] std::string utf8_of(const model::Dict& dict, std::string_view key);

// ── Simple fonts for appearances ──────────────────────────────────────────────────────────────────

// Advances per em of the 256 codes of a simple font, and its extent above and below the baseline.
struct Metrics {
  std::array<double, 256> widths{};
  double ascent = 0.718;
  double descent = -0.207;
};

// Helvetica in WinAnsiEncoding (the built-in substitute's metrics, Adobe's).
[[nodiscard]] const Metrics& helvetica();
// Text as WinAnsi codes for Helvetica: characters it lacks become '?'.
[[nodiscard]] std::string win_ansi_bytes(std::u32string_view text);
// A lowercase letter (Latin, Greek, Cyrillic): what continues a word broken at a line's end.
[[nodiscard]] bool lowercase(char32_t c);
// /Font << /Helv Helvetica >>, for an appearance's resources.
[[nodiscard]] model::Dict helvetica_font();

[[nodiscard]] double text_width(std::string_view codes, double size, const Metrics& metrics);
// Lines of codes wrapped to width at spaces ('\n' breaks a line; a word longer than the width is cut).
[[nodiscard]] std::vector<std::string> wrap(std::string_view codes, double size, double width, const Metrics& metrics);

// A default appearance string (§12.7.4.3): "/Helv 12 Tf 0 0 1 rg". The font's resource name (empty when
// there is no Tf) and size (0: auto, also without Tf), the colour operator (g, rg or k with its operands;
// black when none), and the colour as RGB (for the editor's own annotations).
struct DefaultAppearance {
  std::string font;
  double size = 0;
  std::string color = "0 g";
  Rgb rgb{0, 0, 0};
};
[[nodiscard]] DefaultAppearance parse_da(std::string_view da);

// Content drawn in the page's coordinates as shown (y down), placed into page space.
[[nodiscard]] std::string in_view(const Page& page, const std::string& content);

// The page's /Annots as an array, and where it lives (the page, or its own object).
struct Annots {
  model::Array list;
  std::optional<model::ObjectRef> ref;
};
[[nodiscard]] Annots annots_of(Document& document, const Page& page);
[[nodiscard]] Result<void> write_annots(Document& document, const Page& page, Annots annots);

// A form XObject: content in the space of /BBox (through /Matrix when given).
[[nodiscard]] model::Stream form_xobject(const graphics::Rect& bbox, model::Dict resources, std::string_view content,
                                         const std::optional<graphics::Matrix>& matrix = std::nullopt);

}  // namespace capriole::edit::detail
