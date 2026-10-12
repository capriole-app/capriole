#include "capriole/graphics/shading.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace capriole::graphics {

namespace {

std::uint8_t byte_of(double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); }

// The ramp at s in [0, 1], linear between its samples.
Rgb ramp_at(const std::vector<Rgb>& ramp, double s) {
  if (ramp.empty()) {
    return {};
  }
  const double at = std::clamp(s, 0.0, 1.0) * static_cast<double>(ramp.size() - 1);
  const auto i = static_cast<std::size_t>(at);
  if (i + 1 >= ramp.size()) {
    return ramp.back();
  }
  const double f = at - static_cast<double>(i);
  const Rgb& a = ramp[i];
  const Rgb& b = ramp[i + 1];
  return {a.r + ((b.r - a.r) * f), a.g + ((b.g - a.g) * f), a.b + ((b.b - a.b) * f)};
}

class Canvas {
 public:
  Canvas(int width, int height) {
    out_.width = std::max(width, 0);
    out_.height = std::max(height, 0);
    out_.rgba.assign(static_cast<std::size_t>(out_.width) * static_cast<std::size_t>(out_.height) * 4, 0);
  }
  void put(int x, int y, const Rgb& c) {
    std::uint8_t* p = &out_.rgba[((static_cast<std::size_t>(y) * static_cast<std::size_t>(out_.width)) + static_cast<std::size_t>(x)) * 4];
    p[0] = byte_of(c.r);
    p[1] = byte_of(c.g);
    p[2] = byte_of(c.b);
    p[3] = 255;
  }
  [[nodiscard]] int width() const noexcept { return out_.width; }
  [[nodiscard]] int height() const noexcept { return out_.height; }
  ImagePixels take() { return std::move(out_); }

 private:
  ImagePixels out_;
};

bool in_bbox(const Shading& shading, const Point& p) {
  if (!shading.bbox) {
    return true;
  }
  const Rect b = shading.bbox->normalized();
  return p.x >= b.x0 && p.x <= b.x1 && p.y >= b.y0 && p.y <= b.y1;
}

// Axial (§8.7.4.5.3): the point's projection on the axis.
std::optional<double> axial(const Shading& s, const Point& p) {
  const double dx = s.coords[2] - s.coords[0];
  const double dy = s.coords[3] - s.coords[1];
  const double length2 = (dx * dx) + (dy * dy);
  if (length2 == 0) {
    return std::nullopt;
  }
  double t = (((p.x - s.coords[0]) * dx) + ((p.y - s.coords[1]) * dy)) / length2;
  if (t < 0) {
    if (!s.extend_start) {
      return std::nullopt;
    }
    t = 0;
  } else if (t > 1) {
    if (!s.extend_end) {
      return std::nullopt;
    }
    t = 1;
  }
  return t;
}

// Radial (§8.7.4.5.4): the largest s whose circle — centre and radius interpolated between the two —
// passes through the point, with a radius not negative, and s in [0, 1] or past an extended end.
std::optional<double> radial(const Shading& s, const Point& p) {
  const double x0 = s.coords[0];
  const double y0 = s.coords[1];
  const double r0 = s.coords[2];
  const double cdx = s.coords[3] - x0;
  const double cdy = s.coords[4] - y0;
  const double dr = s.coords[5] - r0;
  const double pdx = p.x - x0;
  const double pdy = p.y - y0;
  const double a = (cdx * cdx) + (cdy * cdy) - (dr * dr);
  const double b = -2 * ((pdx * cdx) + (pdy * cdy) + (r0 * dr));
  const double c = (pdx * pdx) + (pdy * pdy) - (r0 * r0);
  const auto usable = [&](double t) -> std::optional<double> {
    if (!std::isfinite(t) || r0 + (t * dr) < 0) {
      return std::nullopt;
    }
    if (t < 0) {
      return s.extend_start ? std::optional<double>(0) : std::nullopt;
    }
    if (t > 1) {
      return s.extend_end ? std::optional<double>(1) : std::nullopt;
    }
    return t;
  };
  constexpr double kFlat = 1e-12;
  if (std::abs(a) < kFlat * std::max(1.0, std::abs(b) + std::abs(c))) {
    return b != 0 ? usable(-c / b) : std::nullopt;
  }
  const double discriminant = (b * b) - (4 * a * c);
  if (discriminant < 0) {
    return std::nullopt;
  }
  const double root = std::sqrt(discriminant);
  const double t1 = (-b + root) / (2 * a);
  const double t2 = (-b - root) / (2 * a);
  if (const auto t = usable(std::max(t1, t2))) {
    return t;
  }
  return usable(std::min(t1, t2));
}

// A function shading's colour at a point of its domain: bilinear over the sampled grid.
std::optional<Rgb> function_colour(const Shading& s, const Matrix& to_domain, const Point& p) {
  const Point q = to_domain.apply(p);
  const Rect d = s.domain.normalized();
  if (q.x < d.x0 || q.x > d.x1 || q.y < d.y0 || q.y > d.y1 || s.grid_width < 1 || s.grid_height < 1) {
    return std::nullopt;
  }
  const double gx = d.width() > 0 ? (q.x - d.x0) / d.width() * (s.grid_width - 1) : 0;
  const double gy = d.height() > 0 ? (q.y - d.y0) / d.height() * (s.grid_height - 1) : 0;
  const int x0 = std::clamp(static_cast<int>(gx), 0, s.grid_width - 1);
  const int y0 = std::clamp(static_cast<int>(gy), 0, s.grid_height - 1);
  const int x1 = std::min(x0 + 1, s.grid_width - 1);
  const int y1 = std::min(y0 + 1, s.grid_height - 1);
  const double fx = gx - x0;
  const double fy = gy - y0;
  const auto at = [&](int x, int y) { return s.grid[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.grid_width)) + static_cast<std::size_t>(x)]; };
  const auto mix = [](const Rgb& a, const Rgb& b, double f) { return Rgb{a.r + ((b.r - a.r) * f), a.g + ((b.g - a.g) * f), a.b + ((b.b - a.b) * f)}; };
  return mix(mix(at(x0, y0), at(x1, y0), fx), mix(at(x0, y1), at(x1, y1), fx), fy);
}

// Triangles with a colour (or a t) at each corner, interpolated linearly (Gouraud) at pixel centres.
void draw_mesh(const Shading& s, const Matrix& to_pixels, const std::optional<Matrix>& to_shading, Canvas& canvas) {
  const bool by_t = !s.ramp.empty();
  const double t_span = s.t1 - s.t0;
  // Hostile meshes can hold millions of triangles as large as the page: past this many pixels examined,
  // the rest is not drawn.
  constexpr std::int64_t kMaxWork = 64'000'000;
  std::int64_t work = 0;
  for (const auto& triangle : s.triangles) {
    const Point a = to_pixels.apply(triangle[0].point);
    const Point b = to_pixels.apply(triangle[1].point);
    const Point c = to_pixels.apply(triangle[2].point);
    const double area = ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
    if (area == 0 || !std::isfinite(area)) {
      continue;
    }
    const double left = std::min({a.x, b.x, c.x});
    const double right = std::max({a.x, b.x, c.x});
    const double top = std::min({a.y, b.y, c.y});
    const double bottom = std::max({a.y, b.y, c.y});
    const int x0 = static_cast<int>(std::clamp(std::floor(left), 0.0, static_cast<double>(canvas.width())));
    const int x1 = static_cast<int>(std::clamp(std::ceil(right), 0.0, static_cast<double>(canvas.width())));
    const int y0 = static_cast<int>(std::clamp(std::floor(top), 0.0, static_cast<double>(canvas.height())));
    const int y1 = static_cast<int>(std::clamp(std::ceil(bottom), 0.0, static_cast<double>(canvas.height())));
    work += static_cast<std::int64_t>(x1 - x0) * (y1 - y0);
    if (work > kMaxWork) {
      return;
    }
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        const Point p{x + 0.5, y + 0.5};
        // Barycentric weights; a pixel centre exactly on a shared edge belongs to both triangles.
        const double wa = (((b.x - p.x) * (c.y - p.y)) - ((b.y - p.y) * (c.x - p.x))) / area;
        const double wb = (((c.x - p.x) * (a.y - p.y)) - ((c.y - p.y) * (a.x - p.x))) / area;
        const double wc = 1 - wa - wb;
        if (wa < 0 || wb < 0 || wc < 0) {
          continue;
        }
        if (s.bbox && to_shading && !in_bbox(s, to_shading->apply(p))) {
          continue;
        }
        if (by_t) {
          const double t = (wa * triangle[0].t) + (wb * triangle[1].t) + (wc * triangle[2].t);
          canvas.put(x, y, ramp_at(s.ramp, t_span != 0 ? (t - s.t0) / t_span : 0));
        } else {
          const Rgb& ca = triangle[0].color;
          const Rgb& cb = triangle[1].color;
          const Rgb& cc = triangle[2].color;
          canvas.put(x, y, Rgb{(wa * ca.r) + (wb * cb.r) + (wc * cc.r), (wa * ca.g) + (wb * cb.g) + (wc * cc.g), (wa * ca.b) + (wb * cb.b) + (wc * cc.b)});
        }
      }
    }
  }
}

}  // namespace

ImagePixels shade(const Shading& shading, const Matrix& to_pixels, int width, int height) {
  Canvas canvas(width, height);
  const std::optional<Matrix> to_shading = to_pixels.inverse();
  if (shading.kind == Shading::Kind::kMesh) {
    draw_mesh(shading, to_pixels, to_shading, canvas);
    return canvas.take();
  }
  if (!to_shading) {
    return canvas.take();
  }
  const std::optional<Matrix> to_domain = shading.matrix.inverse();
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      const Point p = to_shading->apply(Point{x + 0.5, y + 0.5});
      if (!in_bbox(shading, p)) {
        continue;
      }
      switch (shading.kind) {
        case Shading::Kind::kAxial:
          if (const auto t = axial(shading, p)) {
            canvas.put(x, y, ramp_at(shading.ramp, *t));
          }
          break;
        case Shading::Kind::kRadial:
          if (const auto t = radial(shading, p)) {
            canvas.put(x, y, ramp_at(shading.ramp, *t));
          }
          break;
        case Shading::Kind::kFunction:
          if (to_domain) {
            if (const auto c = function_colour(shading, *to_domain, p)) {
              canvas.put(x, y, *c);
            }
          }
          break;
        case Shading::Kind::kMesh:
          break;
      }
    }
  }
  return canvas.take();
}

}  // namespace capriole::graphics
