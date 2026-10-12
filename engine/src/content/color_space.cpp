#include "color_space.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "cmyk_table.hpp"

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

// DeviceCMYK without a profile: quadrilinear interpolation in the table sampled from the press profile.
Rgb cmyk_to_rgb(double c, double m, double y, double k) {
  std::array<int, 4> low{};
  std::array<double, 4> frac{};
  const std::array<double, 4> ink = {clamp01(c), clamp01(m), clamp01(y), clamp01(k)};
  for (std::size_t i = 0; i < 4; ++i) {
    const double at = ink.at(i) * (kCmykGrid - 1);
    low.at(i) = std::min(static_cast<int>(at), kCmykGrid - 2);
    frac.at(i) = at - low.at(i);
  }
  std::array<double, 3> out{};
  for (unsigned corner = 0; corner < 16; ++corner) {
    double weight = 1;
    std::size_t index = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const bool high = ((corner >> (3 - i)) & 1U) != 0;
      weight *= high ? frac.at(i) : 1 - frac.at(i);
      index = (index * kCmykGrid) + static_cast<std::size_t>(low.at(i) + (high ? 1 : 0));
    }
    if (weight == 0) {
      continue;
    }
    for (std::size_t j = 0; j < 3; ++j) {
      out.at(j) += weight * kCmykTable.at((index * 3) + j);
    }
  }
  return {out[0] / 255, out[1] / 255, out[2] / 255};
}

}  // namespace

Rgb to_rgb(const ColorSpace& space, std::span<const double> c) {
  const auto at = [&](std::size_t i) { return i < c.size() ? c[i] : 0.0; };
  const bool profiled =
      space.icc && (space.family == ColorSpaceFamily::kGray || space.family == ColorSpaceFamily::kRGB || space.family == ColorSpaceFamily::kCMYK);
  if (profiled) {
    return space.icc->convert(c);
  }
  switch (space.family) {
    case ColorSpaceFamily::kGray:
      return {clamp01(at(0)), clamp01(at(0)), clamp01(at(0))};
    case ColorSpaceFamily::kRGB:
      return {clamp01(at(0)), clamp01(at(1)), clamp01(at(2))};
    case ColorSpaceFamily::kCMYK:
      return cmyk_to_rgb(at(0), at(1), at(2), at(3));
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
      if (space.colorant == "None") {
        return {1, 1, 1};
      }
      if (space.tint && space.base) {
        const std::vector<double> alternate = space.tint->evaluate(c.subspan(0, std::min(c.size(), space.components)));
        return to_rgb(*space.base, alternate);
      }
      // Without a tint transform: the more ink, the darker.
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

namespace {

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

ColorSpace device_cmyk(ColorContext* colors) {
  ColorSpace space = device(ColorSpaceFamily::kCMYK);
  if (colors != nullptr) {
    space.icc = colors->default_cmyk();
  }
  return space;
}

ColorSpace resolve_color_space(Document& document, const Object& spec, const model::Dict* resources, Warnings& warnings, ColorContext* colors, int depth) {
  constexpr int kMaxDepth = 4;
  auto resolved = document.resolve(spec);
  if (!resolved || depth > kMaxDepth) {
    warnings.add("unreadable colour space: DeviceGray used");
    return device(ColorSpaceFamily::kGray);
  }
  if (const model::Name* name = resolved->as_name()) {
    const std::string& n = name->bytes;
    if (n == "DeviceGray" || n == "CalGray") {
      return device(ColorSpaceFamily::kGray);
    }
    if (n == "DeviceRGB" || n == "CalRGB") {
      return device(ColorSpaceFamily::kRGB);
    }
    if (n == "DeviceCMYK") {
      return device_cmyk(colors);
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
          return resolve_color_space(document, *entry, nullptr, warnings, colors, depth + 1);
        }
      }
    }
    // The abbreviations of inline images (§8.9.7), when no resource has the name.
    if (n == "G") {
      return device(ColorSpaceFamily::kGray);
    }
    if (n == "RGB") {
      return device(ColorSpaceFamily::kRGB);
    }
    if (n == "CMYK") {
      return device_cmyk(colors);
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
  if (f == "CalGray" || f == "CalRGB") {
    // §8.6.5.2-3: a white point, gammas and (CalRGB) a matrix — as an ICC profile made from them.
    ColorSpace space = device(f == "CalGray" ? ColorSpaceFamily::kGray : ColorSpaceFamily::kRGB);
    auto parameters = operand(1) != nullptr ? document.resolve(*operand(1)) : Result<Object>(Object());
    const model::Dict* p = parameters ? parameters->as_dict() : nullptr;
    if (p != nullptr && colors != nullptr) {
      const auto list = [&](std::string_view key) {
        std::vector<double> out;
        auto value = p->find(key) != nullptr ? document.resolve(*p->find(key)) : Result<Object>(Object());
        if (value && value->as_array() != nullptr) {
          for (const Object& item : *value->as_array()) {
            out.push_back(item.as_number().value_or(0));
          }
        } else if (value && value->as_number()) {
          out.push_back(*value->as_number());
        }
        return out;
      };
      const std::vector<double> white = list("WhitePoint");
      std::vector<double> gamma = list("Gamma");
      const std::vector<double> matrix = list("Matrix");
      const std::size_t n = space.components;
      if (gamma.empty()) {
        gamma.assign(n, 1);
      }
      if (white.size() == 3 && gamma.size() == n && (n == 1 || matrix.empty() || matrix.size() == 9)) {
        const std::array<double, 9> m =
            matrix.size() == 9 ? std::array<double, 9>{matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5], matrix[6], matrix[7], matrix[8]}
                               : std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1};
        space.icc = colors->calibrated({white[0], white[1], white[2]},
                                       n == 1 ? std::array<double, 3>{gamma[0], gamma[0], gamma[0]} : std::array<double, 3>{gamma[0], gamma[1], gamma[2]},
                                       n == 1 ? nullptr : &m);
      }
    }
    return space;
  }
  if (f == "DeviceGray") {
    return device(ColorSpaceFamily::kGray);
  }
  if (f == "DeviceRGB") {
    return device(ColorSpaceFamily::kRGB);
  }
  if (f == "CalCMYK" || f == "DeviceCMYK") {
    return device_cmyk(colors);
  }
  if (f == "Lab") {
    return device(ColorSpaceFamily::kLab);
  }
  if (f == "ICCBased") {
    // §8.6.5.5: /N components; the profile's transform when it is usable, else /Alternate, else the
    // device space of /N components.
    std::int64_t n = 3;
    std::optional<Object> alternate;
    const Object* stream = operand(1);
    if (stream != nullptr) {
      if (auto s = document.resolve(*stream); s && s->as_stream() != nullptr) {
        if (const Object* count = s->as_stream()->dict.find("N"); count != nullptr) {
          n = count->as_integer().value_or(n);
        }
        if (const Object* a = s->as_stream()->dict.find("Alternate")) {
          alternate = *a;
        }
      }
    }
    ColorSpace space = device(icc_family(n));
    if (colors != nullptr && stream != nullptr) {
      space.icc = colors->icc_based(*stream, space.components, warnings);
    }
    if (!space.icc && alternate) {
      ColorSpace fallback = resolve_color_space(document, *alternate, resources, warnings, colors, depth + 1);
      if (fallback.components == space.components) {
        return fallback;
      }
    }
    if (!space.icc && space.family == ColorSpaceFamily::kCMYK) {
      return device_cmyk(colors);
    }
    return space;
  }
  if (f == "Indexed" || f == "I") {
    ColorSpace indexed;
    indexed.family = ColorSpaceFamily::kIndexed;
    indexed.components = 1;
    indexed.base = std::make_shared<ColorSpace>(operand(1) != nullptr ? resolve_color_space(document, *operand(1), resources, warnings, colors, depth + 1)
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
    // The alternate space and the tint transform (§8.6.6.4): the colour as a device shows it.
    if (const Object* alternate = operand(2)) {
      spot.base = std::make_shared<ColorSpace>(resolve_color_space(document, *alternate, resources, warnings, colors, depth + 1));
    }
    if (const Object* transform = operand(3); transform != nullptr && spot.base) {
      if (auto tint = load_function(document, *transform); tint && tint->inputs() == spot.components && tint->outputs() >= spot.base->components) {
        spot.tint = std::make_shared<const Function>(std::move(*tint));
      } else {
        warnings.add("spot colour tint transform unusable: tints approximated as ink coverage");
      }
    }
    return spot;
  }
  if (f == "Pattern") {
    ColorSpace pattern;
    pattern.family = ColorSpaceFamily::kPattern;
    pattern.components = 0;
    if (const Object* under = operand(1)) {  // uncoloured patterns: the colour comes in this space
      pattern.base = std::make_shared<ColorSpace>(resolve_color_space(document, *under, resources, warnings, colors, depth + 1));
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

std::shared_ptr<const IccTransform> ColorContext::icc_based(const Object& stream, std::size_t components, Warnings& warnings) {
  const auto ref = stream.as_ref();
  if (ref) {
    if (const auto found = profiles_.find(ref->number); found != profiles_.end()) {
      return found->second;
    }
  }
  std::shared_ptr<const IccTransform> transform;
  auto resolved = document_->resolve(stream);
  if (resolved && resolved->as_stream() != nullptr) {
    if (auto data = document_->decode_stream(*resolved->as_stream())) {
      if (auto opened = IccTransform::open(*data, components)) {
        transform = std::move(*opened);
      } else {
        warnings.add("ICC profile unusable (" + opened.error().message + "): its alternate space used");
      }
    }
  }
  if (ref) {
    profiles_.emplace(ref->number, transform);
  }
  return transform;
}

std::shared_ptr<const IccTransform> ColorContext::calibrated(std::array<double, 3> white, std::array<double, 3> gamma, const std::array<double, 9>* matrix) {
  std::string key;
  for (const double v : white) {
    key += std::to_string(v) + ' ';
  }
  for (const double v : gamma) {
    key += std::to_string(v) + ' ';
  }
  if (matrix != nullptr) {
    for (const double v : *matrix) {
      key += std::to_string(v) + ' ';
    }
  }
  if (const auto found = calibrated_.find(key); found != calibrated_.end()) {
    return found->second;
  }
  auto transform = IccTransform::calibrated(white, gamma, matrix);
  return calibrated_.emplace(key, transform ? std::move(*transform) : nullptr).first->second;
}

std::shared_ptr<const IccTransform> ColorContext::default_cmyk() {
  if (default_cmyk_) {
    return *default_cmyk_;
  }
  default_cmyk_ = std::shared_ptr<const IccTransform>();
  // The document's output intent (§14.11.5): the printing condition it was made for.
  if (auto catalog = document_->catalog(); catalog && *catalog != nullptr) {
    if (const Object* intents_entry = (*catalog)->find("OutputIntents")) {
      if (auto intents = document_->resolve(*intents_entry); intents && intents->as_array() != nullptr) {
        for (const Object& item : *intents->as_array()) {
          auto intent = document_->resolve(item);
          const Object* profile = intent && intent->as_dict() != nullptr ? intent->as_dict()->find("DestOutputProfile") : nullptr;
          auto stream = profile != nullptr ? document_->resolve(*profile) : Result<Object>(Object());
          if (!stream || stream->as_stream() == nullptr) {
            continue;
          }
          if (auto data = document_->decode_stream(*stream->as_stream())) {
            if (auto opened = IccTransform::open(*data, 4)) {
              default_cmyk_ = std::move(*opened);
              return *default_cmyk_;
            }
          }
        }
      }
    }
  }
  // The press profile, when the host has it.
  const font::Resources::Data data = resources_ != nullptr ? resources_->find(kPressProfile) : nullptr;
  if (!data) {
    missing_.insert(std::string(kPressProfile));
    return nullptr;
  }
  if (auto opened = IccTransform::open(*data, 4)) {
    default_cmyk_ = std::move(*opened);
  }
  return *default_cmyk_;
}

graphics::Color make_color(const ColorSpace& space, std::span<const double> components, std::string pattern, Warnings& /*warnings*/) {
  graphics::Color color;
  color.family = space.family;
  color.components.assign(components.begin(), components.begin() + static_cast<std::ptrdiff_t>(std::min(components.size(), space.components)));
  color.components.resize(space.components, 0.0);
  color.pattern = std::move(pattern);
  if (space.family == ColorSpaceFamily::kPattern) {
    // Painted by the pattern itself; this stands in where a pattern cannot be used (strokes).
    color.rgb = space.base ? to_rgb(*space.base, color.components) : Rgb{0.5, 0.5, 0.5};
  } else {
    color.rgb = to_rgb(space, color.components);
  }
  return color;
}

}  // namespace capriole::content::detail
