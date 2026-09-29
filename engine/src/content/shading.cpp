#include "shading.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capriole/content/function.hpp"

namespace capriole::content::detail {

namespace {

using graphics::Point;
using graphics::Rgb;
using graphics::Shading;
using model::Object;

std::optional<double> number(const Object& object) {
  const auto value = object.as_number();
  return value && std::isfinite(*value) ? value : std::nullopt;
}

std::vector<double> numbers(Document& document, const model::Dict& dict, std::string_view key) {
  std::vector<double> out;
  const Object* entry = dict.find(key);
  auto value = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (value && value->as_array() != nullptr) {
    for (const Object& item : *value->as_array()) {
      auto resolved = document.resolve(item);
      out.push_back(resolved ? number(*resolved).value_or(0) : 0);
    }
  }
  return out;
}

// The colour of a shading: its /Function (one function of n outputs, or n functions of one output) of
// the inputs, else the inputs themselves as components, in its colour space.
class Colours {
 public:
  Colours(ColorSpace space, std::vector<Function> functions) : space_(std::move(space)), functions_(std::move(functions)) {}

  [[nodiscard]] bool has_function() const noexcept { return !functions_.empty(); }
  [[nodiscard]] std::size_t components() const noexcept { return space_.components; }

  [[nodiscard]] Rgb operator()(std::span<const double> in) const {
    if (functions_.empty()) {
      return to_rgb(space_, in);
    }
    std::vector<double> out;
    for (const Function& f : functions_) {
      const std::vector<double> part = f.evaluate(in);
      out.insert(out.end(), part.begin(), part.end());
    }
    return to_rgb(space_, out);
  }

 private:
  ColorSpace space_;
  std::vector<Function> functions_;
};

// Most significant bit first, as mesh data is packed (§8.7.4.5.5).
class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) : data_(data) {}

  std::optional<std::uint32_t> read(int bits) {
    if (bits <= 0 || bits > 32 || static_cast<std::size_t>(bits) > (data_.size() * 8) - position_) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < bits; ++i, ++position_) {
      const auto byte = std::to_integer<unsigned>(data_[position_ / 8]);
      value = (value << 1U) | ((byte >> (7U - (position_ % 8))) & 1U);
    }
    return static_cast<std::uint32_t>(value);
  }
  // A read ending mid-byte: the next item starts at the next byte.
  void align() { position_ = (position_ + 7) / 8 * 8; }
  [[nodiscard]] bool done() const { return position_ + 8 > data_.size() * 8; }

 private:
  std::span<const std::byte> data_;
  std::size_t position_ = 0;
};

struct MeshFormat {
  int coordinate_bits = 0;
  int component_bits = 0;
  int flag_bits = 0;
  std::vector<double> decode;  // x, y, then each colour value
  std::size_t values = 0;      // colour values per vertex: 1 (t) with a function, else the space's components
};

struct MeshVertex {
  Point point;
  std::vector<double> values;
};

std::optional<MeshVertex> read_vertex(BitReader& bits, const MeshFormat& f) {
  const auto scale = [](std::uint32_t raw, int n, double lo, double hi) {
    const double max = n >= 32 ? 4294967295.0 : static_cast<double>((std::uint64_t{1} << static_cast<unsigned>(n)) - 1);
    return lo + (static_cast<double>(raw) * (hi - lo) / max);
  };
  const auto x = bits.read(f.coordinate_bits);
  const auto y = bits.read(f.coordinate_bits);
  if (!x || !y) {
    return std::nullopt;
  }
  MeshVertex v{.point = {scale(*x, f.coordinate_bits, f.decode[0], f.decode[1]), scale(*y, f.coordinate_bits, f.decode[2], f.decode[3])}, .values = {}};
  for (std::size_t i = 0; i < f.values; ++i) {
    const auto c = bits.read(f.component_bits);
    if (!c) {
      return std::nullopt;
    }
    v.values.push_back(scale(*c, f.component_bits, f.decode[4 + (2 * i)], f.decode[5 + (2 * i)]));
  }
  return v;
}

Shading::Vertex prepared(const MeshVertex& v, const Colours& colours) {
  Shading::Vertex out{.point = v.point, .color = {}, .t = 0};
  if (colours.has_function()) {
    out.t = v.values.empty() ? 0 : v.values[0];
  } else {
    out.color = colours(v.values);
  }
  return out;
}

double bernstein(int i, double t) {
  const double s = 1 - t;
  switch (i) {
    case 0:
      return s * s * s;
    case 1:
      return 3 * t * s * s;
    case 2:
      return 3 * t * t * s;
    default:
      return t * t * t;
  }
}

// A patch (§8.7.4.5.7-8): 4 × 4 control points p[i][j] and the corner values c00, c03, c33, c30.
struct Patch {
  std::array<std::array<Point, 4>, 4> p{};
  std::array<std::vector<double>, 4> corner;  // at (0,0), (0,3), (3,3), (3,0)
};

// A Coons patch's four interior points, from its boundary (the tensor-product form of §8.7.4.5.8).
void coons_interior(Patch& patch) {
  auto& p = patch.p;
  const auto combine = [](std::initializer_list<std::pair<double, Point>> terms) {
    Point out;
    for (const auto& [w, q] : terms) {
      out.x += w * q.x / 9;
      out.y += w * q.y / 9;
    }
    return out;
  };
  p[1][1] = combine({{-4, p[0][0]}, {6, p[0][1]}, {6, p[1][0]}, {-2, p[0][3]}, {-2, p[3][0]}, {3, p[3][1]}, {3, p[1][3]}, {-1, p[3][3]}});
  p[1][2] = combine({{-4, p[0][3]}, {6, p[0][2]}, {6, p[1][3]}, {-2, p[0][0]}, {-2, p[3][3]}, {3, p[3][2]}, {3, p[1][0]}, {-1, p[3][0]}});
  p[2][1] = combine({{-4, p[3][0]}, {6, p[3][1]}, {6, p[2][0]}, {-2, p[3][3]}, {-2, p[0][0]}, {3, p[0][1]}, {3, p[2][3]}, {-1, p[0][3]}});
  p[2][2] = combine({{-4, p[3][3]}, {6, p[3][2]}, {6, p[2][3]}, {-2, p[3][0]}, {-2, p[0][3]}, {3, p[0][2]}, {3, p[2][0]}, {-1, p[0][0]}});
}

// A patch as an n × n grid of quads, two triangles each; corner values interpolated bilinearly.
void subdivide(const Patch& patch, double scale, const Colours& colours, std::size_t max_triangles, std::vector<std::array<Shading::Vertex, 3>>& out) {
  double x0 = patch.p[0][0].x;
  double x1 = x0;
  double y0 = patch.p[0][0].y;
  double y1 = y0;
  for (const auto& row : patch.p) {
    for (const Point& q : row) {
      x0 = std::min(x0, q.x);
      x1 = std::max(x1, q.x);
      y0 = std::min(y0, q.y);
      y1 = std::max(y1, q.y);
    }
  }
  // About one quad per 4 points of the patch's size on the page, from 2 to 24 per side.
  const double extent = std::max(x1 - x0, y1 - y0) * scale;
  const int n = std::isfinite(extent) ? std::clamp(static_cast<int>(std::ceil(extent / 4)), 2, 24) : 2;
  const std::size_t values = patch.corner[0].size();
  const auto vertex = [&](int iu, int iv) {
    const double u = static_cast<double>(iu) / n;
    const double v = static_cast<double>(iv) / n;
    Point q;
    for (std::size_t i = 0; i < 4; ++i) {
      for (std::size_t j = 0; j < 4; ++j) {
        const double w = bernstein(static_cast<int>(i), u) * bernstein(static_cast<int>(j), v);
        q.x += w * patch.p[i][j].x;
        q.y += w * patch.p[i][j].y;
      }
    }
    MeshVertex m{.point = q, .values = std::vector<double>(values)};
    for (std::size_t k = 0; k < values; ++k) {
      m.values[k] =
          ((1 - u) * (1 - v) * patch.corner[0][k]) + ((1 - u) * v * patch.corner[1][k]) + (u * v * patch.corner[2][k]) + (u * (1 - v) * patch.corner[3][k]);
    }
    return prepared(m, colours);
  };
  std::vector<Shading::Vertex> grid;
  const auto side = static_cast<std::size_t>(n) + 1;
  grid.reserve(side * side);
  for (int iu = 0; iu <= n; ++iu) {
    for (int iv = 0; iv <= n; ++iv) {
      grid.push_back(vertex(iu, iv));
    }
  }
  const auto at = [&](int iu, int iv) { return grid[(static_cast<std::size_t>(iu) * side) + static_cast<std::size_t>(iv)]; };
  for (int iu = 0; iu < n && out.size() + 2 <= max_triangles; ++iu) {
    for (int iv = 0; iv < n; ++iv) {
      out.push_back({at(iu, iv), at(iu + 1, iv), at(iu + 1, iv + 1)});
      out.push_back({at(iu, iv), at(iu + 1, iv + 1), at(iu, iv + 1)});
    }
  }
}

}  // namespace

std::shared_ptr<const graphics::Shading> load_shading(Document& document, const Object& object, const model::Dict* resources, double scale, Warnings& warnings,
                                                      const ShadingLimits& limits) {
  auto resolved = document.resolve(object);
  if (!resolved) {
    warnings.add("shading unreadable: " + resolved.error().message);
    return nullptr;
  }
  const model::Stream* stream = resolved->as_stream();
  const model::Dict* dict = stream != nullptr ? &stream->dict : resolved->as_dict();
  if (dict == nullptr) {
    warnings.add("shading is not a dictionary or stream: not drawn");
    return nullptr;
  }
  const Object* type_entry = dict->find("ShadingType");
  auto type_object = type_entry != nullptr ? document.resolve(*type_entry) : Result<Object>(Object());
  const std::int64_t type = type_object ? type_object->as_integer().value_or(0) : 0;
  const Object* space_entry = dict->find("ColorSpace");
  if (type < 1 || type > 7 || space_entry == nullptr) {
    warnings.add("shading without a valid /ShadingType and /ColorSpace: not drawn");
    return nullptr;
  }
  const ColorSpace space = resolve_color_space(document, *space_entry, resources, warnings);

  // /Function: one, or an array of one per colour component.
  std::vector<Function> functions;
  if (const Object* entry = dict->find("Function")) {
    auto value = document.resolve(*entry);
    const model::Array* list = value ? value->as_array() : nullptr;
    const std::vector<Object> items = list != nullptr ? *list : std::vector<Object>{value ? *value : Object()};
    for (const Object& item : items) {
      auto f = load_function(document, item);
      if (!f) {
        warnings.add("shading function unusable (" + f.error().message + "): not drawn");
        return nullptr;
      }
      functions.push_back(std::move(*f));
    }
  }
  const Colours colours(space, std::move(functions));
  if ((type == 1 || type == 2 || type == 3) && !colours.has_function()) {
    warnings.add("shading of type " + std::to_string(type) + " without /Function: not drawn");
    return nullptr;
  }

  auto shading = std::make_shared<Shading>();
  if (const std::vector<double> background = numbers(document, *dict, "Background"); !background.empty()) {
    shading->background = to_rgb(space, background);
  }
  if (const std::vector<double> bbox = numbers(document, *dict, "BBox"); bbox.size() == 4) {
    shading->bbox = graphics::Rect{bbox[0], bbox[1], bbox[2], bbox[3]}.normalized();
  }

  // A ramp of the function along [t0, t1].
  const auto ramp = [&](double t0, double t1) {
    shading->t0 = t0;
    shading->t1 = t1;
    shading->ramp.resize(limits.ramp_samples);
    for (std::size_t i = 0; i < limits.ramp_samples; ++i) {
      const double t = t0 + ((t1 - t0) * static_cast<double>(i) / static_cast<double>(limits.ramp_samples - 1));
      shading->ramp[i] = colours(std::span<const double>(&t, 1));
    }
  };

  if (type == 1) {
    shading->kind = Shading::Kind::kFunction;
    std::vector<double> domain = numbers(document, *dict, "Domain");
    if (domain.size() != 4) {
      domain = {0, 1, 0, 1};
    }
    shading->domain = graphics::Rect{domain[0], domain[2], domain[1], domain[3]};
    if (const std::vector<double> m = numbers(document, *dict, "Matrix"); m.size() == 6) {
      shading->matrix = {m[0], m[1], m[2], m[3], m[4], m[5]};
    }
    const int n = limits.grid_samples;
    shading->grid_width = n;
    shading->grid_height = n;
    shading->grid.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    for (int j = 0; j < n; ++j) {
      for (int i = 0; i < n; ++i) {
        const std::array<double, 2> in = {domain[0] + ((domain[1] - domain[0]) * i / (n - 1)), domain[2] + ((domain[3] - domain[2]) * j / (n - 1))};
        shading->grid[(static_cast<std::size_t>(j) * static_cast<std::size_t>(n)) + static_cast<std::size_t>(i)] = colours(in);
      }
    }
    return shading;
  }

  if (type == 2 || type == 3) {
    shading->kind = type == 2 ? Shading::Kind::kAxial : Shading::Kind::kRadial;
    const std::vector<double> coords = numbers(document, *dict, "Coords");
    const std::size_t needed = type == 2 ? 4 : 6;
    if (coords.size() < needed) {
      warnings.add("shading without valid /Coords: not drawn");
      return nullptr;
    }
    std::copy_n(coords.begin(), needed, shading->coords.begin());
    const std::vector<double> domain = numbers(document, *dict, "Domain");
    ramp(domain.size() == 2 ? domain[0] : 0, domain.size() == 2 ? domain[1] : 1);
    if (const Object* extend = dict->find("Extend")) {
      if (auto e = document.resolve(*extend); e && e->as_array() != nullptr && e->as_array()->size() == 2) {
        shading->extend_start = (*e->as_array())[0].as_bool().value_or(false);
        shading->extend_end = (*e->as_array())[1].as_bool().value_or(false);
      }
    }
    return shading;
  }

  // Meshes (types 4–7): a stream of packed vertices or patches.
  shading->kind = Shading::Kind::kMesh;
  if (stream == nullptr) {
    warnings.add("mesh shading is not a stream: not drawn");
    return nullptr;
  }
  const auto integer = [&](std::string_view key) {
    const Object* entry = dict->find(key);
    auto value = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
    return value ? static_cast<int>(std::clamp<std::int64_t>(value->as_integer().value_or(0), 0, 64)) : 0;
  };
  MeshFormat format{.coordinate_bits = integer("BitsPerCoordinate"),
                    .component_bits = integer("BitsPerComponent"),
                    .flag_bits = integer("BitsPerFlag"),
                    .decode = numbers(document, *dict, "Decode"),
                    .values = colours.has_function() ? 1 : colours.components()};
  const bool flags = type != 5;
  if (format.coordinate_bits < 1 || format.coordinate_bits > 32 || format.component_bits < 1 || format.component_bits > 16 ||
      (flags && (format.flag_bits < 1 || format.flag_bits > 8)) || format.decode.size() < 4 + (2 * format.values)) {
    warnings.add("mesh shading with invalid /BitsPerCoordinate, /BitsPerComponent, /BitsPerFlag or /Decode: not drawn");
    return nullptr;
  }
  if (colours.has_function()) {
    ramp(format.decode[4], format.decode[5]);
  }
  auto data = document.decode_stream(*stream);
  if (!data) {
    warnings.add("mesh shading data unreadable: " + data.error().message);
    return nullptr;
  }
  BitReader bits(*data);
  auto& triangles = shading->triangles;

  if (type == 4) {
    // Free-form (§8.7.4.5.5): flag 0 starts a triangle from three new vertices; 1 and 2 continue from
    // the last one's edge (vb vc, or va vc). Each vertex starts on a byte boundary.
    std::vector<Shading::Vertex> last;
    std::size_t read = 0;
    while (!bits.done() && read++ < limits.max_vertices && triangles.size() < limits.max_triangles) {
      const auto flag = bits.read(format.flag_bits);
      const auto v = flag ? read_vertex(bits, format) : std::nullopt;
      bits.align();
      if (!v) {
        break;
      }
      const Shading::Vertex vertex = prepared(*v, colours);
      if (*flag == 0 || last.size() < 3) {
        if (last.size() >= 3) {
          last.clear();
        }
        last.push_back(vertex);
        if (last.size() == 3) {
          triangles.push_back({last[0], last[1], last[2]});
        }
        continue;
      }
      const std::array<Shading::Vertex, 3> next = *flag == 1 ? std::array{last[1], last[2], vertex} : std::array{last[0], last[2], vertex};
      triangles.push_back(next);
      last.assign(next.begin(), next.end());
    }
  } else if (type == 5) {
    // Lattice (§8.7.4.5.6): rows of /VerticesPerRow vertices, two triangles per cell.
    const Object* per_row_entry = dict->find("VerticesPerRow");
    auto per_row_value = per_row_entry != nullptr ? document.resolve(*per_row_entry) : Result<Object>(Object());
    constexpr std::int64_t kMaxPerRow = 1'000'000;
    const auto per_row = static_cast<int>(std::clamp<std::int64_t>(per_row_value ? per_row_value->as_integer().value_or(0) : 0, 0, kMaxPerRow));
    if (per_row < 2) {
      warnings.add("lattice shading with /VerticesPerRow below 2: not drawn");
      return nullptr;
    }
    std::vector<Shading::Vertex> previous;
    std::vector<Shading::Vertex> row;
    std::size_t read = 0;
    while (read++ < limits.max_vertices && triangles.size() < limits.max_triangles) {
      const auto v = read_vertex(bits, format);
      if (!v) {
        break;
      }
      row.push_back(prepared(*v, colours));
      if (std::cmp_less(row.size(), per_row)) {
        continue;
      }
      if (!previous.empty()) {
        for (std::size_t c = 0; c + 1 < row.size(); ++c) {
          triangles.push_back({previous[c], previous[c + 1], row[c]});
          triangles.push_back({previous[c + 1], row[c + 1], row[c]});
        }
      }
      previous = std::move(row);
      row.clear();
    }
  } else {
    // Coons (6) and tensor-product (7) patches (§8.7.4.5.7-8). Flag 0: all points and colours; 1–3: the
    // edge shared with the previous patch comes from it, the rest from the stream.
    const bool tensor = type == 7;
    std::optional<Patch> previous;
    std::size_t read = 0;
    // The boundary, in stream order, then the interior (type 7).
    constexpr std::array<std::pair<std::size_t, std::size_t>, 16> kOrder = {
        {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3}, {3, 2}, {3, 1}, {3, 0}, {2, 0}, {1, 0}, {1, 1}, {1, 2}, {2, 2}, {2, 1}}};
    while (!bits.done() && read++ < limits.max_vertices && triangles.size() < limits.max_triangles) {
      const auto flag = bits.read(format.flag_bits);
      if (!flag || *flag > 3 || (*flag != 0 && !previous)) {
        break;
      }
      Patch patch;
      std::size_t first = 0;
      std::size_t first_colour = 0;
      if (*flag != 0) {
        // The shared edge (Table 85): the previous patch's side after p03–p33, p33–p30 or p30–p00.
        const auto& q = previous->p;
        const auto& c = previous->corner;
        const std::array<std::array<Point, 4>, 3> edges = {
            {{q[0][3], q[1][3], q[2][3], q[3][3]}, {q[3][3], q[3][2], q[3][1], q[3][0]}, {q[3][0], q[2][0], q[1][0], q[0][0]}}};
        const std::array<std::array<std::vector<double>, 2>, 3> colours_of = {{{c[1], c[2]}, {c[2], c[3]}, {c[3], c[0]}}};
        for (std::size_t j = 0; j < 4; ++j) {
          patch.p[0][j] = edges[*flag - 1][j];
        }
        patch.corner[0] = colours_of[*flag - 1][0];
        patch.corner[1] = colours_of[*flag - 1][1];
        first = 4;
        first_colour = 2;
      }
      bool complete = true;
      const std::size_t points = tensor ? 16 : 12;
      for (std::size_t k = first; k < points && complete; ++k) {
        const auto x = bits.read(format.coordinate_bits);
        const auto y = bits.read(format.coordinate_bits);
        if (!x || !y) {
          complete = false;
          break;
        }
        const auto scale_bits = [&](std::uint32_t raw, double lo, double hi) {
          const double max =
              format.coordinate_bits >= 32 ? 4294967295.0 : static_cast<double>((std::uint64_t{1} << static_cast<unsigned>(format.coordinate_bits)) - 1);
          return lo + (static_cast<double>(raw) * (hi - lo) / max);
        };
        patch.p[kOrder[k].first][kOrder[k].second] = {scale_bits(*x, format.decode[0], format.decode[1]), scale_bits(*y, format.decode[2], format.decode[3])};
      }
      for (std::size_t k = first_colour; k < 4 && complete; ++k) {
        for (std::size_t i = 0; i < format.values; ++i) {
          const auto c = bits.read(format.component_bits);
          if (!c) {
            complete = false;
            break;
          }
          const auto max = static_cast<double>((std::uint64_t{1} << static_cast<unsigned>(format.component_bits)) - 1);
          patch.corner[k].push_back(format.decode[4 + (2 * i)] + (static_cast<double>(*c) * (format.decode[5 + (2 * i)] - format.decode[4 + (2 * i)]) / max));
        }
      }
      bits.align();
      if (!complete) {
        break;
      }
      if (!tensor) {
        coons_interior(patch);
      }
      subdivide(patch, scale, colours, limits.max_triangles, triangles);
      previous = std::move(patch);
    }
  }
  if (triangles.size() >= limits.max_triangles) {
    warnings.add("mesh shading too large: drawn in part");
  }
  if (triangles.empty()) {
    warnings.add("mesh shading without complete triangles: not drawn");
    return nullptr;
  }
  return shading;
}

}  // namespace capriole::content::detail
