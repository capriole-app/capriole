#pragma once

// Internal to the interpreter: colour spaces (ISO 32000-2 §8.6) as far as placing colours needs —
// the family, the number of components, the initial colour, and an sRGB approximation for previews.

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"

namespace capriole::content::detail {

struct ColorSpace {
  graphics::ColorSpaceFamily family = graphics::ColorSpaceFamily::kGray;
  std::size_t components = 1;
  // Indexed: the base space and the table (hival + 1 entries of base.components bytes).
  std::shared_ptr<const ColorSpace> base;
  std::vector<std::byte> lookup;
  // ICCBased: the family is Gray, RGB or CMYK from /N (the profile itself is not applied yet).
  // Separation: the colorant name (for "All" and "None").
  std::string colorant;
};

// Collects warnings without repeating them (a page can hit the same approximation thousands of times).
class Warnings {
 public:
  void add(std::string message);
  [[nodiscard]] std::vector<std::string> take() { return std::move(list_); }

 private:
  std::vector<std::string> list_;
};

// A colour space from its specification: a name (/DeviceRGB, or a key of the /ColorSpace resources) or
// an array ([/ICCBased 5 0 R], [/Indexed base hival lookup], ...). Unknown or malformed: DeviceGray,
// with a warning. depth bounds nested specifications (Indexed of Indexed is not allowed anyway).
ColorSpace resolve_color_space(Document& document, const model::Object& spec, const model::Dict* resources, Warnings& warnings, int depth = 0);

// The initial colour of a space (§8.6.8: black, or 1.0 tint for Separation/DeviceN, index 0).
graphics::Color initial_color(const ColorSpace& space);

// A colour from operands (sc/scn/...): missing components are 0, extra ones ignored.
graphics::Color make_color(const ColorSpace& space, std::span<const double> components, std::string pattern, Warnings& warnings);

}  // namespace capriole::content::detail
