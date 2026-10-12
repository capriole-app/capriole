#include "capriole/graphics/dash.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace capriole::graphics {

namespace {

// Flattens a cubic into points (the start excluded) within tolerance, by subdivision until the control
// points lie close enough to the chord.
void flatten(Point p0, Point p1, Point p2, Point p3, double tolerance, int depth, std::vector<Point>& out) {
  const auto distance = [&](Point p) {
    const double dx = p3.x - p0.x;
    const double dy = p3.y - p0.y;
    const double length = std::hypot(dx, dy);
    if (length == 0) {
      return std::hypot(p.x - p0.x, p.y - p0.y);
    }
    return std::abs(((p.x - p0.x) * dy) - ((p.y - p0.y) * dx)) / length;
  };
  constexpr int kMaxDepth = 16;
  if (depth >= kMaxDepth || std::max(distance(p1), distance(p2)) <= tolerance) {
    out.push_back(p3);
    return;
  }
  const auto mid = [](Point a, Point b) { return Point{(a.x + b.x) / 2, (a.y + b.y) / 2}; };
  const Point a = mid(p0, p1);
  const Point b = mid(p1, p2);
  const Point c = mid(p2, p3);
  const Point ab = mid(a, b);
  const Point bc = mid(b, c);
  const Point m = mid(ab, bc);
  flatten(p0, a, ab, m, tolerance, depth + 1, out);
  flatten(m, bc, c, p3, tolerance, depth + 1, out);
}

// One subpath as a polyline (closed: back to its start).
struct Polyline {
  std::vector<Point> points;
};

std::vector<Polyline> polylines(const Path& path, double tolerance) {
  std::vector<Polyline> out;
  Point current;
  Point start;
  path.for_each([&](SegmentKind kind, const Point* p) {
    switch (kind) {
      case SegmentKind::kMove:
        out.push_back({{p[0]}});
        current = start = p[0];
        break;
      case SegmentKind::kLine:
        if (out.empty()) {
          out.push_back({{current}});
        }
        out.back().points.push_back(p[0]);
        current = p[0];
        break;
      case SegmentKind::kCubic:
        if (out.empty()) {
          out.push_back({{current}});
        }
        flatten(current, p[0], p[1], p[2], tolerance, 0, out.back().points);
        current = p[2];
        break;
      case SegmentKind::kClose:
        if (!out.empty()) {
          out.back().points.push_back(start);
          current = start;
        }
        break;
    }
  });
  return out;
}

}  // namespace

DashedPath dash_path(const Path& path, std::span<const double> pattern, double phase, double tolerance, std::size_t max_segments) {
  // The pattern as PDF reads it: an odd count repeats; all zeros or a negative entry mean solid.
  std::vector<double> dashes(pattern.begin(), pattern.end());
  double total = 0;
  for (const double d : dashes) {
    if (d < 0 || !std::isfinite(d)) {
      return {.path = path, .too_many = false};
    }
    total += d;
  }
  if (dashes.empty() || total <= 0 || !std::isfinite(phase)) {
    return {.path = path, .too_many = false};
  }
  if (dashes.size() % 2 == 1) {
    dashes.insert(dashes.end(), pattern.begin(), pattern.end());
    total *= 2;
  }
  tolerance = std::max(tolerance, 1e-6);

  // Where the pattern starts: phase into it (a negative phase counts back from the end).
  double offset = std::fmod(phase, total);
  if (offset < 0) {
    offset += total;
  }
  std::size_t first = 0;
  while (offset >= dashes[first]) {
    offset -= dashes[first];
    first = (first + 1) % dashes.size();
  }

  DashedPath out;
  std::size_t segments = 0;
  for (const Polyline& line : polylines(path, tolerance)) {
    std::size_t index = first;
    double left = dashes[index] - offset;  // of the current dash element
    bool drawing = false;
    for (std::size_t i = 1; i < line.points.size(); ++i) {
      const Point a = line.points[i - 1];
      const Point b = line.points[i];
      const double length = std::hypot(b.x - a.x, b.y - a.y);
      double done = 0;
      while (done < length || (length == 0 && done == 0)) {
        const double step = std::min(left, length - done);
        const bool on = index % 2 == 0;
        const auto at = [&](double t) { return length == 0 ? a : Point{a.x + ((b.x - a.x) * t / length), a.y + ((b.y - a.y) * t / length)}; };
        if (on) {
          if (!drawing) {
            out.path.move_to(at(done));
            drawing = true;
          }
          out.path.line_to(at(done + step));
          if (++segments > max_segments) {
            return {.path = path, .too_many = true};
          }
        }
        done += step;
        left -= step;
        if (left <= 0) {
          index = (index + 1) % dashes.size();
          left = dashes[index];
          drawing = false;
        }
        if (length == 0) {
          break;
        }
      }
    }
  }
  return out;
}

}  // namespace capriole::graphics
