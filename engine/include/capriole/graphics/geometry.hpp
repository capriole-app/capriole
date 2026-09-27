#pragma once

// Geometry of the imaging model (ISO 32000-2 §8.3): points, rectangles and the 3×3 transformation
// matrices written [a b c d e f], which map (x, y) to (a·x + c·y + e, b·x + d·y + f).

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace capriole::graphics {

struct Point {
  double x = 0;
  double y = 0;
  constexpr Point() = default;
  constexpr Point(double px, double py) noexcept : x(px), y(py) {}
  friend bool operator==(const Point&, const Point&) = default;
};

struct Rect {
  double x0 = 0;
  double y0 = 0;
  double x1 = 0;
  double y1 = 0;
  constexpr Rect() = default;
  constexpr Rect(double left, double bottom, double right, double top) noexcept : x0(left), y0(bottom), x1(right), y1(top) {}
  friend bool operator==(const Rect&, const Rect&) = default;

  // With x0 ≤ x1 and y0 ≤ y1 (PDF rectangles may list their corners in any order, §7.9.5).
  [[nodiscard]] Rect normalized() const noexcept { return {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)}; }
  [[nodiscard]] double width() const noexcept { return x1 - x0; }
  [[nodiscard]] double height() const noexcept { return y1 - y0; }
};

struct Matrix {
  double a = 1;
  double b = 0;
  double c = 0;
  double d = 1;
  double e = 0;
  double f = 0;
  constexpr Matrix() = default;
  constexpr Matrix(double ma, double mb, double mc, double md, double me, double mf) noexcept : a(ma), b(mb), c(mc), d(md), e(me), f(mf) {}
  friend bool operator==(const Matrix&, const Matrix&) = default;

  [[nodiscard]] Point apply(Point p) const noexcept { return {(a * p.x) + (c * p.y) + e, (b * p.x) + (d * p.y) + f}; }

  // This transformation followed by then: in PDF terms, `this × then` (cm concatenates as
  // CTM' = M × CTM, so M comes first).
  [[nodiscard]] Matrix then(const Matrix& m) const noexcept {
    return {(a * m.a) + (b * m.c), (a * m.b) + (b * m.d),       (c * m.a) + (d * m.c),
            (c * m.b) + (d * m.d), (e * m.a) + (f * m.c) + m.e, (e * m.b) + (f * m.d) + m.f};
  }

  [[nodiscard]] double determinant() const noexcept { return (a * d) - (b * c); }

  // The inverse, or nullopt when the matrix is singular (or not finite).
  [[nodiscard]] std::optional<Matrix> inverse() const noexcept {
    const double det = determinant();
    if (det == 0 || !std::isfinite(det)) {
      return std::nullopt;
    }
    return Matrix{d / det, -b / det, -c / det, a / det, ((c * f) - (d * e)) / det, ((b * e) - (a * f)) / det};
  }

  // The bounding box of a rectangle's image.
  [[nodiscard]] Rect apply(const Rect& r) const noexcept {
    const std::array<Point, 4> p = {apply(Point{r.x0, r.y0}), apply(Point{r.x1, r.y0}), apply(Point{r.x0, r.y1}), apply(Point{r.x1, r.y1})};
    Rect out{p[0].x, p[0].y, p[0].x, p[0].y};
    for (const Point& q : p) {
      out = {std::min(out.x0, q.x), std::min(out.y0, q.y), std::max(out.x1, q.x), std::max(out.y1, q.y)};
    }
    return out;
  }

  static Matrix translate(double tx, double ty) noexcept { return {1, 0, 0, 1, tx, ty}; }
  static Matrix scale(double sx, double sy) noexcept { return {sx, 0, 0, sy, 0, 0}; }
};

}  // namespace capriole::graphics
