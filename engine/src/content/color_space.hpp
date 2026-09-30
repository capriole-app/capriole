#pragma once

// Internal to the interpreter: colour spaces (ISO 32000-2 §8.6) as far as placing colours needs — the
// family, the number of components, the initial colour, and the colour in sRGB: through ICC profiles
// (icc.hpp: ICCBased, CalRGB/CalGray, output intents, the press profile for DeviceCMYK) where there are
// some, else by formula, DeviceCMYK by a table sampled from the press profile (ADR 0006).

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "capriole/content/function.hpp"
#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "icc.hpp"

namespace capriole::content::detail {

struct ColorSpace {
  graphics::ColorSpaceFamily family = graphics::ColorSpaceFamily::kGray;
  std::size_t components = 1;
  // Indexed: the base space and the table (hival + 1 entries of base.components bytes).
  std::shared_ptr<const ColorSpace> base;
  std::vector<std::byte> lookup;
  // Gray, RGB and CMYK: the transform to sRGB of their profile (ICCBased, CalRGB, CalGray; the default
  // CMYK one for DeviceCMYK), null when there is none — the colour is then computed by formula, or for
  // CMYK by the built-in table.
  std::shared_ptr<const IccTransform> icc;
  // Separation and DeviceN: the colorant name (for "All" and "None"), and the tint transform into the
  // alternate space (held in base); without a usable one, tints are approximated as ink coverage.
  std::string colorant;
  std::shared_ptr<const Function> tint;
};

// Collects warnings without repeating them (a page can hit the same approximation thousands of times).
class Warnings {
 public:
  void add(std::string message);
  [[nodiscard]] std::vector<std::string> take() { return std::move(list_); }

 private:
  std::vector<std::string> list_;
};

// What the colour spaces of a page share: the transforms made from its profiles (once per profile
// stream), the default CMYK profile — the document's output intent (§14.11.5) if it has one, else the
// press profile of the resource pack (font/resources.hpp) — and what the host did not supply.
class ColorContext {
 public:
  ColorContext(Document& document, const font::Resources* resources) : document_(&document), resources_(resources) {}

  // An ICCBased profile stream's transform; null (with a warning) when it cannot be used.
  [[nodiscard]] std::shared_ptr<const IccTransform> icc_based(const model::Object& stream, std::size_t components, Warnings& warnings);
  [[nodiscard]] std::shared_ptr<const IccTransform> calibrated(std::array<double, 3> white, std::array<double, 3> gamma, const std::array<double, 9>* matrix);
  // For DeviceCMYK: null when neither is available (the built-in table is used).
  [[nodiscard]] std::shared_ptr<const IccTransform> default_cmyk();
  [[nodiscard]] std::vector<std::string> missing() const { return {missing_.begin(), missing_.end()}; }

 private:
  Document* document_;
  const font::Resources* resources_;
  std::map<std::uint32_t, std::shared_ptr<const IccTransform>> profiles_;
  std::map<std::string, std::shared_ptr<const IccTransform>, std::less<>> calibrated_;
  std::optional<std::shared_ptr<const IccTransform>> default_cmyk_;
  std::set<std::string> missing_;
};

// The resource pack's press profile (ADR 0006).
inline constexpr std::string_view kPressProfile = "icc/SWOP2006_Coated3v2.icc";

// A colour space from its specification: a name (/DeviceRGB, or a key of the /ColorSpace resources) or
// an array ([/ICCBased 5 0 R], [/Indexed base hival lookup], ...). Unknown or malformed: DeviceGray,
// with a warning. depth bounds nested specifications (Indexed of Indexed is not allowed anyway).
// colors: the page's colour context (profiles applied); without one, colours are computed by formula.
ColorSpace resolve_color_space(Document& document, const model::Object& spec, const model::Dict* resources, Warnings& warnings, ColorContext* colors = nullptr,
                               int depth = 0);

// DeviceCMYK, with the context's default profile when there is one.
ColorSpace device_cmyk(ColorContext* colors);

// The initial colour of a space (§8.6.8: black, or 1.0 tint for Separation/DeviceN, index 0).
graphics::Color initial_color(const ColorSpace& space);

// The sRGB approximation of a colour given by its components in the space.
graphics::Rgb to_rgb(const ColorSpace& space, std::span<const double> components);

// A colour from operands (sc/scn/...): missing components are 0, extra ones ignored.
graphics::Color make_color(const ColorSpace& space, std::span<const double> components, std::string pattern, Warnings& warnings);

}  // namespace capriole::content::detail
