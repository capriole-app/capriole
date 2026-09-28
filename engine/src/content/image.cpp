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

// Reads bit fields of a row of samples, most significant bit first (§8.9.5.2).
class SampleReader {
 public:
  SampleReader(std::span<const std::byte> row, int bits) : row_(row), bits_(bits) {}

  std::uint32_t next() {
    if (bits_ == 8) {
      return at_ < row_.size() ? std::to_integer<std::uint32_t>(row_[at_++]) : 0;
    }
    if (bits_ == 16) {
      const std::uint32_t hi = at_ < row_.size() ? std::to_integer<std::uint32_t>(row_[at_]) : 0;
      const std::uint32_t lo = at_ + 1 < row_.size() ? std::to_integer<std::uint32_t>(row_[at_ + 1]) : 0;
      at_ += 2;
      return (hi << 8U) | lo;
    }
    std::uint32_t v = 0;
    for (int b = 0; b < bits_; ++b, ++bit_) {
      const std::size_t byte = bit_ / 8;
      const std::uint32_t set = byte < row_.size() ? (std::to_integer<std::uint32_t>(row_[byte]) >> (7 - (bit_ % 8))) & 1U : 0U;
      v = (v << 1U) | set;
    }
    return v;
  }

 private:
  std::span<const std::byte> row_;
  int bits_;
  std::size_t at_ = 0;   // 8 and 16 bits
  std::size_t bit_ = 0;  // 1, 2 and 4 bits
};

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

// The samples of a one-component image (a soft mask or an explicit mask) as 0..255, resampled to
// width × height by the nearest sample. invert: stencil masks mark with 1 what is masked out.
Result<std::vector<std::uint8_t>> single_channel(Document& document, const Object& reference, int width, int height, bool stencil, Warnings& warnings,
                                                 const ImageLimits& limits) {
  auto resolved = document.resolve(reference);
  const model::Stream* stream = resolved ? resolved->as_stream() : nullptr;
  if (stream == nullptr) {
    return make_error(ErrorCode::kSyntax, "image mask is not a stream");
  }
  const model::Dict& dict = stream->dict;
  const std::int64_t w = integer(document, dict, "Width", 0);
  const std::int64_t h = integer(document, dict, "Height", 0);
  if (w < 1 || h < 1 || w > limits.max_pixels / h) {
    return make_error(ErrorCode::kLimitExceeded, "image mask size invalid or too large");
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
  if (data->size() < row_bytes * static_cast<std::size_t>(h)) {
    warnings.add("image mask data shorter than its size: the rest reads as 0");
  }
  // The mask at its own size, then resampled.
  std::vector<std::uint8_t> own(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (std::int64_t y = 0; y < h; ++y) {
    const std::size_t start = std::min(data->size(), static_cast<std::size_t>(y) * row_bytes);
    SampleReader reader{std::span(*data).subspan(start, std::min(row_bytes, data->size() - start)), bits};
    for (std::int64_t x = 0; x < w; ++x) {
      const double v = decode[0] + ((reader.next() / top) * (decode[1] - decode[0]));
      // A stencil's 1 masks out; a soft mask's value is the alpha itself.
      double alpha = v;
      if (stencil) {
        alpha = v >= 0.5 ? 0.0 : 1.0;
      }
      own[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255));
    }
  }
  if (w == width && h == height) {
    return own;
  }
  std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const std::int64_t sy = std::min<std::int64_t>(h - 1, (static_cast<std::int64_t>(y) * h) / height);
    for (int x = 0; x < width; ++x) {
      const std::int64_t sx = std::min<std::int64_t>(w - 1, (static_cast<std::int64_t>(x) * w) / width);
      out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)] =
          own[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(sx)];
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
  if (width < 1 || height < 1 || width > limits.max_pixels / height) {
    return make_error(ErrorCode::kLimitExceeded, "image size invalid or too large: " + std::to_string(width) + "x" + std::to_string(height));
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
  if (data.size() < row_bytes * h) {
    warnings.add("image data shorter than its size: the rest reads as 0");
  }
  auto pixels = std::make_shared<graphics::ImagePixels>();
  pixels->width = static_cast<std::int32_t>(width);
  pixels->height = static_cast<std::int32_t>(height);
  pixels->rgba.resize(w * h * 4);

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
  for (std::size_t y = 0; y < h; ++y) {
    const std::size_t start = std::min(data.size(), y * row_bytes);
    SampleReader reader{std::span(data).subspan(start, std::min(row_bytes, data.size() - start)), bits};
    for (std::size_t x = 0; x < w; ++x) {
      std::uint8_t* p = &pixels->rgba[((y * w) + x) * 4];
      for (std::size_t i = 0; i < n; ++i) {
        raw[i] = reader.next();
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
      p[3] = 255;
    }
  }
  if (stencil) {
    return pixels;
  }

  // Transparency: a soft mask image (§11.6.5.3) wins over /Mask (§8.9.6).
  if (const Object* smask = dict.find("SMask")) {
    auto alpha = single_channel(document, *smask, pixels->width, pixels->height, false, warnings, limits);
    if (alpha) {
      for (std::size_t i = 0; i < w * h; ++i) {
        pixels->rgba[(i * 4) + 3] = (*alpha)[i];
      }
      // /Matte: the colours were blended with this colour by the alpha (§11.6.5.3); undo it:
      // c = m + (c' − m) / α.
      if (auto mask = document.resolve(*smask); mask && mask->as_stream() != nullptr) {
        if (const std::vector<double> matte = numbers(document, mask->as_stream()->dict.find("Matte")); matte.size() == n) {
          const Rgb m = to_rgb(space, matte);
          const std::array<double, 3> base = {m.r, m.g, m.b};
          for (std::size_t i = 0; i < w * h; ++i) {
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
    if (resolved && resolved->as_array() != nullptr) {
      // Colour key masking: samples whose every component lies in its range are not painted.
      const std::vector<double> ranges = numbers(document, &*resolved);
      if (ranges.size() == 2 * n) {
        for (std::size_t y = 0; y < h; ++y) {
          const std::size_t start = std::min(data.size(), y * row_bytes);
          SampleReader reader{std::span(data).subspan(start, std::min(row_bytes, data.size() - start)), bits};
          for (std::size_t x = 0; x < w; ++x) {
            bool keyed = true;
            for (std::size_t i = 0; i < n; ++i) {
              const auto v = static_cast<double>(reader.next());
              keyed = keyed && v >= ranges[2 * i] && v <= ranges[(2 * i) + 1];
            }
            if (keyed) {
              pixels->rgba[(((y * w) + x) * 4) + 3] = 0;
            }
          }
        }
      }
    } else if (resolved && resolved->as_stream() != nullptr) {
      auto alpha = single_channel(document, *mask, pixels->width, pixels->height, true, warnings, limits);
      if (alpha) {
        for (std::size_t i = 0; i < w * h; ++i) {
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
