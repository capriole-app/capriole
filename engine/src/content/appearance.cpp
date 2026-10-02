#include "capriole/content/appearance.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace capriole::content {

namespace {

using graphics::Point;
using model::Object;

std::optional<double> number(const Object& object) { return object.as_number(); }

}  // namespace

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

std::string markup_appearance(Document& document, std::string_view subtype, const model::Dict& a, const graphics::Rect& rect) {
  const auto numbers_of = [&document](const model::Dict& dict, std::string_view key) { return numbers(document, dict, key); };
  const auto colour = [&](std::string_view key, bool stroke) -> std::string {
    const std::vector<double> c = numbers_of(a, key);
    const auto n = [](double v) { return std::format("{:.4f} ", v); };
    if (c.size() == 1) {
      return n(c[0]) + (stroke ? "G " : "g ");
    }
    if (c.size() == 3) {
      return n(c[0]) + n(c[1]) + n(c[2]) + (stroke ? "RG " : "rg ");
    }
    if (c.size() == 4) {
      return n(c[0]) + n(c[1]) + n(c[2]) + n(c[3]) + (stroke ? "K " : "k ");
    }
    return {};  // none: transparent
  };
  double width = 1;
  std::string dash;
  char style = 'S';
  bool border_given = false;  // /BS or /Border (links draw a border only when one is given, as readers do)
  if (const Object* bs_entry = a.find("BS")) {
    if (auto bs = document.resolve(*bs_entry); bs && bs->as_dict() != nullptr) {
      border_given = true;
      if (const Object* w = bs->as_dict()->find("W")) {
        width = number(*w).value_or(1);
      }
      const Object* style_entry = bs->as_dict()->find("S");
      if (style_entry != nullptr && style_entry->as_name() != nullptr && !style_entry->as_name()->bytes.empty()) {
        style = style_entry->as_name()->bytes[0];
      }
      if (style == 'D') {
        const std::vector<double> d = numbers_of(*bs->as_dict(), "D");
        dash = "[";
        for (const double v : d.empty() ? std::vector<double>{3} : d) {
          dash += std::format("{:.4f} ", v);
        }
        dash += "] 0 d ";
      }
    }
  } else if (const std::vector<double> border = numbers_of(a, "Border"); border.size() >= 3) {
    border_given = true;
    width = border[2];
  }
  width = std::max(width, 0.0);
  const std::string stroke = colour("C", true);
  const std::string fill = colour("IC", false);
  const auto pt = [](double x, double y) { return std::format("{:.4f} {:.4f} ", x, y); };
  const std::string line_style = std::format("{:.4f} w ", width) + dash;

  if (subtype == "Square" || subtype == "Circle") {
    const bool stroked = !stroke.empty() && width > 0;
    if (!stroked && fill.empty()) {
      return {};
    }
    const double inset = stroked ? width / 2 : 0;
    const double x0 = rect.x0 + inset;
    const double y0 = rect.y0 + inset;
    const double x1 = rect.x1 - inset;
    const double y1 = rect.y1 - inset;
    std::string path;
    if (subtype == "Square") {
      path = std::format("{:.4f} {:.4f} {:.4f} {:.4f} re ", x0, y0, x1 - x0, y1 - y0);
    } else {
      // An ellipse from four Bézier arcs (κ = 0.5523).
      constexpr double kKappa = 0.5523;
      const double cx = (x0 + x1) / 2;
      const double cy = (y0 + y1) / 2;
      const double rx = (x1 - x0) / 2;
      const double ry = (y1 - y0) / 2;
      path = pt(cx + rx, cy) + "m " + pt(cx + rx, cy + (ry * kKappa)) + pt(cx + (rx * kKappa), cy + ry) + pt(cx, cy + ry) + "c " +
             pt(cx - (rx * kKappa), cy + ry) + pt(cx - rx, cy + (ry * kKappa)) + pt(cx - rx, cy) + "c " + pt(cx - rx, cy - (ry * kKappa)) +
             pt(cx - (rx * kKappa), cy - ry) + pt(cx, cy - ry) + "c " + pt(cx + (rx * kKappa), cy - ry) + pt(cx + rx, cy - (ry * kKappa)) + pt(cx + rx, cy) +
             "c h ";
    }
    std::string_view paint = "f";
    if (stroked) {
      paint = fill.empty() ? "S" : "B";
    }
    return stroke + fill + line_style + path + std::string(paint);
  }
  if (subtype == "Link") {
    // A link's border (§12.5.6.5): black when /C does not say, as readers draw it; /U underlines.
    if (!border_given || width <= 0) {
      return {};
    }
    const std::string pen = (stroke.empty() ? std::string("0 G ") : stroke) + line_style;
    if (style == 'U') {
      return pen + pt(rect.x0, rect.y0 + (width / 2)) + "m " + pt(rect.x1, rect.y0 + (width / 2)) + "l S";
    }
    return pen + std::format("{:.4f} {:.4f} {:.4f} {:.4f} re S", rect.x0 + (width / 2), rect.y0 + (width / 2), rect.width() - width, rect.height() - width);
  }
  if (stroke.empty() && subtype != "Highlight") {
    return {};
  }
  if (subtype == "Line") {
    const std::vector<double> l = numbers_of(a, "L");
    return l.size() == 4 && width > 0 ? stroke + line_style + pt(l[0], l[1]) + "m " + pt(l[2], l[3]) + "l S" : std::string();
  }
  if (subtype == "Ink") {
    std::string content = stroke + line_style + "1 J 1 j ";
    const Object* ink_entry = a.find("InkList");
    auto ink = ink_entry != nullptr ? document.resolve(*ink_entry) : Result<Object>(Object());
    if (!ink || ink->as_array() == nullptr || width <= 0) {
      return {};
    }
    for (const Object& stroke_entry : *ink->as_array()) {
      auto points = document.resolve(stroke_entry);
      if (!points || points->as_array() == nullptr) {
        continue;
      }
      const model::Array& p = *points->as_array();
      for (std::size_t i = 0; i + 1 < p.size(); i += 2) {
        content += pt(number(p[i]).value_or(0), number(p[i + 1]).value_or(0)) + (i == 0 ? "m " : "l ");
      }
      content += "S ";
    }
    return content;
  }
  if (subtype == "Polygon" || subtype == "PolyLine") {
    const std::vector<double> v = numbers_of(a, "Vertices");
    if (v.size() < 4 || width <= 0) {
      return {};
    }
    std::string content = stroke + fill + line_style;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
      content += pt(v[i], v[i + 1]) + (i == 0 ? "m " : "l ");
    }
    if (subtype == "PolyLine") {
      return content + "S";
    }
    return content + (fill.empty() ? "s" : "b");
  }
  if (subtype == "Highlight" || subtype == "Underline" || subtype == "StrikeOut" || subtype == "Squiggly") {
    // /QuadPoints: per quadrilateral, the upper left, upper right, lower left and lower right corners
    // (as Acrobat writes them).
    const std::vector<double> q = numbers_of(a, "QuadPoints");
    const std::string colour_fill = colour("C", false);
    if (q.size() < 8 || (subtype == "Highlight" ? colour_fill.empty() : stroke.empty())) {
      return {};
    }
    std::string content = subtype == "Highlight" ? colour_fill : stroke;
    for (std::size_t i = 0; i + 7 < q.size(); i += 8) {
      const Point ul{q[i], q[i + 1]};
      const Point ur{q[i + 2], q[i + 3]};
      const Point ll{q[i + 4], q[i + 5]};
      const Point lr{q[i + 6], q[i + 7]};
      const double height = std::hypot(ul.x - ll.x, ul.y - ll.y);
      if (subtype == "Highlight") {
        content += pt(ll.x, ll.y) + "m " + pt(lr.x, lr.y) + "l " + pt(ur.x, ur.y) + "l " + pt(ul.x, ul.y) + "l h f ";
        continue;
      }
      const double w = std::max(height / 14, 0.5);
      content += std::format("{:.4f} w ", w);
      if (subtype == "Underline") {
        content += pt(ll.x, ll.y + w) + "m " + pt(lr.x, lr.y + w) + "l S ";
      } else if (subtype == "StrikeOut") {
        content += pt((ll.x + ul.x) / 2, (ll.y + ul.y) / 2) + "m " + pt((lr.x + ur.x) / 2, (lr.y + ur.y) / 2) + "l S ";
      } else {
        // Squiggly: a zigzag along the bottom, a period of a quarter of the height.
        const double length = std::hypot(lr.x - ll.x, lr.y - ll.y);
        const double step = std::max(height / 4, 1.0);
        const double ux = length > 0 ? (lr.x - ll.x) / length : 1;
        const double uy = length > 0 ? (lr.y - ll.y) / length : 0;
        constexpr int kMaxSteps = 10'000;
        const int steps = static_cast<int>(std::min(std::floor(length / step), static_cast<double>(kMaxSteps)));
        for (int n = 0; n <= steps; ++n) {
          const double d = n * step;
          const double lift = (n % 2 == 0 ? 0 : step);
          content += pt(ll.x + (ux * d) - (uy * lift), ll.y + (uy * d) + (ux * lift)) + (n == 0 ? "m " : "l ");
        }
        content += "S ";
      }
    }
    return content;
  }
  return {};
}

}  // namespace capriole::content
