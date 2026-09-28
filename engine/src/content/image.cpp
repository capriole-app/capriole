#include "image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace capriole::content::detail {

namespace {

using graphics::ColorSpaceFamily;
using graphics::Rgb;
using model::Object;

// The sample that starts at bit `bit` of data, most significant bit first (§8.9.5.2); past the end, 0.
// Samples of 1, 2 and 4 bits never straddle a byte; 16-bit ones are big-endian.
std::uint32_t sample_at(std::span<const std::byte> data, std::size_t bit, int bits) {
  const std::size_t byte = bit / 8;
  const auto at = [&](std::size_t i) { return i < data.size() ? std::to_integer<std::uint32_t>(data[i]) : 0U; };
  if (bits == 8) {
    return at(byte);
  }
  if (bits == 16) {
    return (at(byte) << 8U) | at(byte + 1);
  }
  const auto shift = static_cast<unsigned>(8 - bits - static_cast<int>(bit % 8));
  return (at(byte) >> shift) & ((1U << static_cast<unsigned>(bits)) - 1U);
}

// Absurd sizes are refused outright; large ones are read at a reduced size (below).
constexpr std::int64_t kMaxSide = std::int64_t{1} << 24;

// The step s that brings width × height within max_pixels when only every s-th sample of every s-th
// row is read.
std::int64_t reduction(std::int64_t width, std::int64_t height, std::int64_t max_pixels) {
  std::int64_t step = 1;
  const auto ceil_div = [](std::int64_t a, std::int64_t b) { return (a + b - 1) / b; };
  while (ceil_div(width, step) * ceil_div(height, step) > std::max<std::int64_t>(max_pixels, 1)) {
    ++step;
  }
  return step;
}

const Object* entry(const model::Dict& dict, std::string_view key) { return dict.find(key); }

std::int64_t integer(Document& document, const model::Dict& dict, std::string_view key, std::int64_t fallback) {
  const Object* value = entry(dict, key);
  if (value == nullptr) {
    return fallback;
  }
  auto resolved = document.resolve(*value);
  return resolved ? resolved->as_integer().value_or(fallback) : fallback;
}

bool flag(Document& document, const model::Dict& dict, std::string_view key) {
  const Object* value = entry(dict, key);
  if (value == nullptr) {
    return false;
  }
  auto resolved = document.resolve(*value);
  return resolved && resolved->as_bool().value_or(false);
}

std::vector<double> numbers(Document& document, const Object* value) {
  std::vector<double> out;
  if (value == nullptr) {
    return out;
  }
  auto resolved = document.resolve(*value);
  if (!resolved || resolved->as_array() == nullptr) {
    return out;
  }
  constexpr std::size_t kMax = 64;
  for (const Object& element : *resolved->as_array()) {
    if (out.size() >= kMax) {
      break;
    }
    const auto n = element.as_number();
    out.push_back(n && std::isfinite(*n) ? *n : 0);
  }
  return out;
}

// The samples of a one-component image (a soft mask or an explicit mask) as 0..255, read at width ×
// height by the nearest sample (the mask may have any size). stencil: an explicit mask, whose 1 masks
// out.
Result<std::vector<std::uint8_t>> single_channel(Document& document, const Object& reference, int width, int height, bool stencil, Warnings& warnings) {
  auto resolved = document.resolve(reference);
  const model::Stream* stream = resolved ? resolved->as_stream() : nullptr;
  if (stream == nullptr) {
    return make_error(ErrorCode::kSyntax, "image mask is not a stream");
  }
  const model::Dict& dict = stream->dict;
  const std::int64_t w = integer(document, dict, "Width", 0);
  const std::int64_t h = integer(document, dict, "Height", 0);
  if (w < 1 || h < 1 || w > kMaxSide || h > kMaxSide) {
    return make_error(ErrorCode::kLimitExceeded, "image mask size invalid");
  }
  const int bits = stencil ? 1 : static_cast<int>(integer(document, dict, "BitsPerComponent", 8));
  if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
    return make_error(ErrorCode::kSyntax, "image mask with invalid /BitsPerComponent");
  }
  auto data = document.decode_stream(*stream);
  if (!data) {
    return std::unexpected(std::move(data.error()));
  }
  std::vector<double> decode = numbers(document, dict.find("Decode"));
  if (decode.size() != 2) {
    decode = {0, 1};
  }
  const double top = std::ldexp(1.0, bits) - 1;
  const std::size_t row_bytes = ((static_cast<std::size_t>(w) * static_cast<std::size_t>(bits)) + 7) / 8;
  if (data->size() / row_bytes < static_cast<std::size_t>(h)) {
    warnings.add("image mask data shorter than its size: the rest reads as 0");
  }
  std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const std::int64_t sy = std::min<std::int64_t>(h - 1, (static_cast<std::int64_t>(y) * h) / height);
    const std::size_t row = static_cast<std::size_t>(sy) * row_bytes * 8;
    for (int x = 0; x < width; ++x) {
      const std::int64_t sx = std::min<std::int64_t>(w - 1, (static_cast<std::int64_t>(x) * w) / width);
      const std::uint32_t raw = sample_at(*data, row + (static_cast<std::size_t>(sx) * static_cast<std::size_t>(bits)), bits);
      const double v = decode[0] + ((raw / top) * (decode[1] - decode[0]));
      // A stencil's 1 masks out; a soft mask's value is the alpha itself.
      double alpha = v;
      if (stencil) {
        alpha = v >= 0.5 ? 0.0 : 1.0;
      }
      out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255));
    }
  }
  return out;
}

std::uint8_t byte_of(double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); }

}  // namespace

model::Dict expand_inline_dict(const model::Dict& dict) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kKeys = {{{"BPC", "BitsPerComponent"},
                                                                                           {"CS", "ColorSpace"},
                                                                                           {"D", "Decode"},
                                                                                           {"DP", "DecodeParms"},
                                                                                           {"F", "Filter"},
                                                                                           {"H", "Height"},
                                                                                           {"IM", "ImageMask"},
                                                                                           {"I", "Interpolate"},
                                                                                           {"W", "Width"},
                                                                                           {"L", "Length"},
                                                                                           {"Filter", "Filter"},
                                                                                           {"DecodeParms", "DecodeParms"}}};
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kFilters = {{{"AHx", "ASCIIHexDecode"},
                                                                                             {"A85", "ASCII85Decode"},
                                                                                             {"LZW", "LZWDecode"},
                                                                                             {"Fl", "FlateDecode"},
                                                                                             {"RL", "RunLengthDecode"},
                                                                                             {"CCF", "CCITTFaxDecode"},
                                                                                             {"DCT", "DCTDecode"}}};
  const auto filter_name = [&](const Object& value) -> Object {
    if (const model::Name* name = value.as_name()) {
      for (const auto& [shortened, full] : kFilters) {
        if (name->bytes == shortened) {
          return model::Name{std::string(full)};
        }
      }
    }
    return value;
  };
  model::Dict out;
  for (const model::DictEntry& e : dict.entries()) {
    std::string key = e.key;
    for (const auto& [shortened, full] : kKeys) {
      if (key == shortened) {
        key = std::string(full);
      }
    }
    Object value = e.value;
    if (key == "Filter") {
      if (const model::Array* list = value.as_array()) {
        model::Array names;
        for (const Object& f : *list) {
          names.push_back(filter_name(f));
        }
        value = std::move(names);
      } else {
        value = filter_name(value);
      }
    }
    out.set(std::move(key), std::move(value));
  }
  return out;
}

Result<std::shared_ptr<const graphics::ImagePixels>> decode_image(Document& document, const model::Dict& dict, const std::vector<std::byte>& data,
                                                                  const model::Dict* resources, Rgb fill, Warnings& warnings, const ImageLimits& limits) {
  const std::int64_t width = integer(document, dict, "Width", 0);
  const std::int64_t height = integer(document, dict, "Height", 0);
  if (width < 1 || height < 1 || width > kMaxSide || height > kMaxSide) {
    return make_error(ErrorCode::kLimitExceeded, "image size invalid: " + std::to_string(width) + "x" + std::to_string(height));
  }
  const bool stencil = flag(document, dict, "ImageMask");
  const int bits = stencil ? 1 : static_cast<int>(integer(document, dict, "BitsPerComponent", 8));
  if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
    return make_error(ErrorCode::kSyntax, "image with invalid /BitsPerComponent");
  }
  ColorSpace space;
  if (!stencil) {
    const Object* spec = dict.find("ColorSpace");
    if (spec == nullptr) {
      return make_error(ErrorCode::kSyntax, "image without /ColorSpace");
    }
    space = resolve_color_space(document, *spec, resources, warnings);
    if (space.family == ColorSpaceFamily::kPattern || space.components == 0) {
      return make_error(ErrorCode::kSyntax, "image in a Pattern colour space");
    }
  }
  const std::size_t n = stencil ? 1 : space.components;
  const double top = std::ldexp(1.0, bits) - 1;
  // /Decode: [Dmin Dmax] per component; the defaults of Table 90.
  std::vector<double> decode = numbers(document, dict.find("Decode"));
  if (decode.size() != 2 * n) {
    decode.clear();
    for (std::size_t i = 0; i < n; ++i) {
      if (space.family == ColorSpaceFamily::kIndexed) {
        decode.insert(decode.end(), {0, top});
      } else if (space.family == ColorSpaceFamily::kLab) {
        decode.insert(decode.end(), i == 0 ? std::initializer_list<double>{0, 100} : std::initializer_list<double>{-100, 100});
      } else {
        decode.insert(decode.end(), {0, 1});
      }
    }
  }

  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  const std::size_t row_bytes = ((w * n * static_cast<std::size_t>(bits)) + 7) / 8;
  if (data.size() / row_bytes < h) {
    warnings.add("image data shorter than its size: the rest reads as 0");
  }
  // Too many pixels to keep: every step-th sample of every step-th row (viewers never refuse an image;
  // a 600 dpi A4 scan is 35 million pixels).
  const auto step = static_cast<std::size_t>(reduction(width, height, limits.max_pixels));
  const std::size_t ow = (w + step - 1) / step;
  const std::size_t oh = (h + step - 1) / step;
  if (step > 1) {
    warnings.add("image of " + std::to_string(width) + "x" + std::to_string(height) + " read at " + std::to_string(ow) + "x" + std::to_string(oh) +
                 " to stay within the memory limit");
  }
  auto pixels = std::make_shared<graphics::ImagePixels>();
  pixels->width = static_cast<std::int32_t>(ow);
  pixels->height = static_cast<std::int32_t>(oh);
  pixels->rgba.resize(ow * oh * 4);

  // Colour key masking (§8.9.6.4): samples whose every component lies in its range are not painted.
  std::vector<double> key;
  if (const Object* mask = dict.find("Mask"); mask != nullptr && !stencil && !dict.contains("SMask")) {
    if (auto resolved = document.resolve(*mask); resolved && resolved->as_array() != nullptr) {
      key = numbers(document, &*resolved);
      if (key.size() != 2 * n) {
        key.clear();
      }
    }
  }

  // One-component spaces (and Indexed): a table of every sample value, computed once.
  std::vector<Rgb> table;
  const bool tabled = !stencil && n == 1 && bits <= 8;
  if (tabled) {
    table.resize(static_cast<std::size_t>(top) + 1);
    for (std::size_t v = 0; v < table.size(); ++v) {
      const double value = decode[0] + ((static_cast<double>(v) / top) * (decode[1] - decode[0]));
      table[v] = to_rgb(space, std::span<const double>(&value, 1));
    }
  }
  std::vector<double> components(n);
  std::vector<std::uint32_t> raw(n);
  const auto sample_bits = n * static_cast<std::size_t>(bits);
  for (std::size_t oy = 0; oy < oh; ++oy) {
    const std::size_t row = oy * step * row_bytes * 8;
    for (std::size_t ox = 0; ox < ow; ++ox) {
      std::uint8_t* p = &pixels->rgba[((oy * ow) + ox) * 4];
      const std::size_t first = row + (ox * step * sample_bits);
      bool keyed = !key.empty();
      for (std::size_t i = 0; i < n; ++i) {
        raw[i] = sample_at(data, first + (i * static_cast<std::size_t>(bits)), bits);
        keyed = keyed && raw[i] >= key[2 * i] && raw[i] <= key[(2 * i) + 1];
      }
      if (stencil) {
        // §8.9.6.2: with /Decode [0 1], 0 paints the fill colour and 1 leaves the backdrop.
        const bool paints = (decode[0] + ((raw[0] / top) * (decode[1] - decode[0]))) < 0.5;
        p[0] = byte_of(fill.r);
        p[1] = byte_of(fill.g);
        p[2] = byte_of(fill.b);
        p[3] = paints ? 255 : 0;
        continue;
      }
      Rgb rgb;
      if (tabled) {
        rgb = table[raw[0]];
      } else {
        for (std::size_t i = 0; i < n; ++i) {
          components[i] = decode[2 * i] + ((raw[i] / top) * (decode[(2 * i) + 1] - decode[2 * i]));
        }
        rgb = to_rgb(space, components);
      }
      p[0] = byte_of(rgb.r);
      p[1] = byte_of(rgb.g);
      p[2] = byte_of(rgb.b);
      p[3] = keyed ? 0 : 255;
    }
  }
  if (stencil) {
    return pixels;
  }
  const std::size_t count = ow * oh;

  // Transparency: a soft mask image (§11.6.5.3) wins over /Mask (§8.9.6).
  if (const Object* smask = dict.find("SMask")) {
    auto alpha = single_channel(document, *smask, pixels->width, pixels->height, false, warnings);
    if (alpha) {
      for (std::size_t i = 0; i < count; ++i) {
        pixels->rgba[(i * 4) + 3] = (*alpha)[i];
      }
      // /Matte: the colours were blended with this colour by the alpha (§11.6.5.3); undo it:
      // c = m + (c' − m) / α.
      if (auto mask = document.resolve(*smask); mask && mask->as_stream() != nullptr) {
        if (const std::vector<double> matte = numbers(document, mask->as_stream()->dict.find("Matte")); matte.size() == n) {
          const Rgb m = to_rgb(space, matte);
          const std::array<double, 3> base = {m.r, m.g, m.b};
          for (std::size_t i = 0; i < count; ++i) {
            const double a = pixels->rgba[(i * 4) + 3] / 255.0;
            for (std::size_t c = 0; c < 3 && a > 0; ++c) {
              std::uint8_t& v = pixels->rgba[(i * 4) + c];
              v = byte_of(base.at(c) + (((v / 255.0) - base.at(c)) / a));
            }
          }
        }
      }
    } else {
      warnings.add("image soft mask unreadable: " + alpha.error().message);
    }
  } else if (const Object* mask = dict.find("Mask")) {
    auto resolved = document.resolve(*mask);
    if (resolved && resolved->as_stream() != nullptr) {
      auto alpha = single_channel(document, *mask, pixels->width, pixels->height, true, warnings);
      if (alpha) {
        for (std::size_t i = 0; i < count; ++i) {
          pixels->rgba[(i * 4) + 3] = (*alpha)[i];
        }
      } else {
        warnings.add("image mask unreadable: " + alpha.error().message);
      }
    }
  }
  return pixels;
}

}  // namespace capriole::content::detail
