#include "color_space.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace capriole::content::detail {

namespace {

using graphics::ColorSpaceFamily;
using graphics::Rgb;
using model::Object;

double clamp01(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; }

ColorSpace device(ColorSpaceFamily family) {
  ColorSpace space;
  space.family = family;
  switch (family) {
    case ColorSpaceFamily::kRGB:
    case ColorSpaceFamily::kLab:
      space.components = 3;
      break;
    case ColorSpaceFamily::kCMYK:
      space.components = 4;
      break;
    default:
      space.components = 1;
      break;
  }
  return space;
}

// The Device family an ICC profile with n components stands for (§8.6.5.5: 1, 3 or 4).
ColorSpaceFamily icc_family(std::int64_t n) {
  switch (n) {
    case 1:
      return ColorSpaceFamily::kGray;
    case 4:
      return ColorSpaceFamily::kCMYK;
    default:
      return ColorSpaceFamily::kRGB;
  }
}

// CIE L*a*b* (D50, the PDF default white point for /Lab without better data) to sRGB.
Rgb lab_to_rgb(double l, double a, double b) {
  const double fy = (l + 16) / 116;
  const double fx = fy + (a / 500);
  const double fz = fy - (b / 200);
  const auto inverse = [](double t) { return t > 6.0 / 29 ? t * t * t : 3 * (6.0 / 29) * (6.0 / 29) * (t - (4.0 / 29)); };
  const double x = 0.9642 * inverse(fx);
  const double y = 1.0 * inverse(fy);
  const double z = 0.8249 * inverse(fz);
  const auto gamma = [](double v) { return clamp01(v <= 0.0031308 ? 12.92 * v : (1.055 * std::pow(v, 1 / 2.4)) - 0.055); };
  return {gamma((3.1338561 * x) - (1.6168667 * y) - (0.4906146 * z)), gamma((-0.9787684 * x) + (1.9161415 * y) + (0.0334540 * z)),
          gamma((0.0719453 * x) - (0.2289914 * y) + (1.4052427 * z))};
}

Rgb to_rgb(const ColorSpace& space, std::span<const double> c) {
  const auto at = [&](std::size_t i) { return i < c.size() ? c[i] : 0.0; };
  switch (space.family) {
    case ColorSpaceFamily::kGray:
      return {clamp01(at(0)), clamp01(at(0)), clamp01(at(0))};
    case ColorSpaceFamily::kRGB:
      return {clamp01(at(0)), clamp01(at(1)), clamp01(at(2))};
    case ColorSpaceFamily::kCMYK: {
      const double k = clamp01(at(3));
      return {(1 - clamp01(at(0))) * (1 - k), (1 - clamp01(at(1))) * (1 - k), (1 - clamp01(at(2))) * (1 - k)};
    }
    case ColorSpaceFamily::kLab:
      return lab_to_rgb(at(0), at(1), at(2));
    case ColorSpaceFamily::kIndexed: {
      if (!space.base || space.base->components == 0) {
        return {};
      }
      const std::size_t n = space.base->components;
      const std::size_t entries = space.lookup.size() / n;
      if (entries == 0 || !std::isfinite(at(0))) {
        return {};
      }
      const auto index = static_cast<std::size_t>(std::clamp(std::round(at(0)), 0.0, static_cast<double>(entries - 1)));
      std::vector<double> base(n);
      for (std::size_t i = 0; i < n; ++i) {
        base[i] = std::to_integer<unsigned char>(space.lookup[(index * n) + i]) / 255.0;
      }
      if (space.base->family == ColorSpaceFamily::kLab) {
        base = {base[0] * 100, ((base.size() > 1 ? base[1] : 0.5) * 255) - 128, ((base.size() > 2 ? base[2] : 0.5) * 255) - 128};
      }
      return to_rgb(*space.base, base);
    }
    case ColorSpaceFamily::kSeparation:
    case ColorSpaceFamily::kDeviceN: {
      // Without the tint transform (a PDF function): the more ink, the darker.
      if (space.colorant == "None") {
        return {1, 1, 1};
      }
      double ink = 0;
      for (std::size_t i = 0; i < space.components; ++i) {
        ink = std::max(ink, clamp01(at(i)));
      }
      return {1 - ink, 1 - ink, 1 - ink};
    }
    case ColorSpaceFamily::kICCBased:
    case ColorSpaceFamily::kPattern:
    case ColorSpaceFamily::kUnknown:
      return {0.5, 0.5, 0.5};
  }
  return {};
}

std::vector<std::byte> bytes_of(Document& document, const Object& object) {
  auto resolved = document.resolve(object);
  if (!resolved) {
    return {};
  }
  if (const model::String* string = resolved->as_string()) {
    const auto* first = reinterpret_cast<const std::byte*>(string->bytes.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return {first, first + string->bytes.size()};
  }
  if (const model::Stream* stream = resolved->as_stream()) {
    auto data = document.decode_stream(*stream);
    return data ? std::move(*data) : std::vector<std::byte>{};
  }
  return {};
}

}  // namespace

void Warnings::add(std::string message) {
  constexpr std::size_t kMaxWarnings = 100;
  if (list_.size() < kMaxWarnings && std::ranges::find(list_, message) == list_.end()) {
    list_.push_back(std::move(message));
  }
}

ColorSpace resolve_color_space(Document& document, const Object& spec, const model::Dict* resources, Warnings& warnings, int depth) {
  constexpr int kMaxDepth = 4;
  auto resolved = document.resolve(spec);
  if (!resolved || depth > kMaxDepth) {
    warnings.add("unreadable colour space: DeviceGray used");
    return device(ColorSpaceFamily::kGray);
  }
  if (const model::Name* name = resolved->as_name()) {
    const std::string& n = name->bytes;
    if (n == "DeviceGray" || n == "G" || n == "CalGray") {
      return device(ColorSpaceFamily::kGray);
    }
    if (n == "DeviceRGB" || n == "RGB" || n == "CalRGB") {
      return device(ColorSpaceFamily::kRGB);
    }
    if (n == "DeviceCMYK" || n == "CMYK") {
      return device(ColorSpaceFamily::kCMYK);
    }
    if (n == "Pattern") {
      ColorSpace pattern;
      pattern.family = ColorSpaceFamily::kPattern;
      pattern.components = 0;
      return pattern;
    }
    // A resource name: look it up in /ColorSpace.
    const Object* entry = nullptr;
    if (resources != nullptr) {
      if (auto spaces = document.resolve(resources->find("ColorSpace") != nullptr ? *resources->find("ColorSpace") : Object());
          spaces && spaces->as_dict() != nullptr) {
        entry = spaces->as_dict()->find(n);
        if (entry != nullptr) {
          return resolve_color_space(document, *entry, nullptr, warnings, depth + 1);
        }
      }
    }
    warnings.add("colour space /" + n + " not found: DeviceGray used");
    return device(ColorSpaceFamily::kGray);
  }

  const model::Array* array = resolved->as_array();
  const model::Name* family = array != nullptr && !array->empty() ? (*array)[0].as_name() : nullptr;
  if (family == nullptr) {
    warnings.add("malformed colour space: DeviceGray used");
    return device(ColorSpaceFamily::kGray);
  }
  const std::string& f = family->bytes;
  const auto operand = [&](std::size_t i) -> const Object* { return i < array->size() ? &(*array)[i] : nullptr; };
  if (f == "CalGray" || f == "DeviceGray") {
    return device(ColorSpaceFamily::kGray);
  }
  if (f == "CalRGB" || f == "DeviceRGB") {
    return device(ColorSpaceFamily::kRGB);
  }
  if (f == "CalCMYK" || f == "DeviceCMYK") {
    return device(ColorSpaceFamily::kCMYK);
  }
  if (f == "Lab") {
    return device(ColorSpaceFamily::kLab);
  }
  if (f == "ICCBased") {
    // The profile is not applied yet: /N says whether it is a gray, RGB or CMYK space.
    std::int64_t n = 3;
    if (const Object* stream = operand(1)) {
      if (auto s = document.resolve(*stream); s && s->as_stream() != nullptr) {
        if (const Object* count = s->as_stream()->dict.find("N"); count != nullptr) {
          n = count->as_integer().value_or(n);
        }
      }
    }
    return device(icc_family(n));
  }
  if (f == "Indexed" || f == "I") {
    ColorSpace indexed;
    indexed.family = ColorSpaceFamily::kIndexed;
    indexed.components = 1;
    indexed.base = std::make_shared<ColorSpace>(operand(1) != nullptr ? resolve_color_space(document, *operand(1), resources, warnings, depth + 1)
                                                                      : device(ColorSpaceFamily::kGray));
    if (const Object* table = operand(3)) {
      indexed.lookup = bytes_of(document, *table);
    }
    return indexed;
  }
  if (f == "Separation" || f == "DeviceN") {
    ColorSpace spot;
    spot.family = f == "Separation" ? ColorSpaceFamily::kSeparation : ColorSpaceFamily::kDeviceN;
    spot.components = 1;
    if (const Object* names = operand(1)) {
      if (const model::Name* colorant = names->as_name()) {
        spot.colorant = colorant->bytes;
      } else if (auto list = document.resolve(*names); list && list->as_array() != nullptr) {
        spot.components = std::max<std::size_t>(1, list->as_array()->size());
      }
    }
    warnings.add("spot colours (Separation/DeviceN) approximated: tint transforms are not evaluated yet");
    return spot;
  }
  if (f == "Pattern") {
    ColorSpace pattern;
    pattern.family = ColorSpaceFamily::kPattern;
    pattern.components = 0;
    if (const Object* under = operand(1)) {  // uncoloured patterns: the colour comes in this space
      pattern.base = std::make_shared<ColorSpace>(resolve_color_space(document, *under, resources, warnings, depth + 1));
      pattern.components = pattern.base->components;
    }
    return pattern;
  }
  warnings.add("unknown colour space /" + f + ": DeviceGray used");
  return device(ColorSpaceFamily::kGray);
}

graphics::Color initial_color(const ColorSpace& space) {
  std::vector<double> components(space.components, 0.0);
  if (space.family == ColorSpaceFamily::kCMYK) {
    components = {0, 0, 0, 1};
  } else if (space.family == ColorSpaceFamily::kSeparation || space.family == ColorSpaceFamily::kDeviceN) {
    components.assign(space.components, 1.0);
  }
  Warnings ignored;
  return make_color(space, components, {}, ignored);
}

graphics::Color make_color(const ColorSpace& space, std::span<const double> components, std::string pattern, Warnings& warnings) {
  graphics::Color color;
  color.family = space.family;
  color.components.assign(components.begin(), components.begin() + static_cast<std::ptrdiff_t>(std::min(components.size(), space.components)));
  color.components.resize(space.components, 0.0);
  color.pattern = std::move(pattern);
  if (space.family == ColorSpaceFamily::kPattern) {
    warnings.add("patterns are not drawn yet: a gray placeholder is used");
    color.rgb = space.base ? to_rgb(*space.base, color.components) : Rgb{0.5, 0.5, 0.5};
  } else {
    color.rgb = to_rgb(space, color.components);
  }
  return color;
}

}  // namespace capriole::content::detail
