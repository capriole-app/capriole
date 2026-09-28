#include "capriole/render/raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "blend2d/blend2d.h"
#include "capriole/graphics/dash.hpp"
#include "capriole/render/blend.hpp"

namespace capriole::render {

namespace {

const graphics::Matrix kIdentity{};

using namespace capriole::graphics;

// Blend2D initializes its runtime from a static object in the library, which a static link can leave
// out (it did in WebAssembly: the first image allocation crashed). This one is always linked.
const BLRuntimeInitializer kRuntime;

BLPath to_blend2d(const Path& path) {
  BLPath out;
  path.for_each([&](SegmentKind kind, const Point* p) {
    switch (kind) {
      case SegmentKind::kMove:
        out.move_to(p[0].x, p[0].y);
        break;
      case SegmentKind::kLine:
        out.line_to(p[0].x, p[0].y);
        break;
      case SegmentKind::kCubic:
        out.cubic_to(p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y);
        break;
      case SegmentKind::kClose:
        out.close();
        break;
    }
  });
  return out;
}

BLMatrix2D to_blend2d(const Matrix& m) { return {m.a, m.b, m.c, m.d, m.e, m.f}; }

BLStrokeCap cap_of(LineCap cap) {
  switch (cap) {
    case LineCap::kRound:
      return BL_STROKE_CAP_ROUND;
    case LineCap::kSquare:
      return BL_STROKE_CAP_SQUARE;
    case LineCap::kButt:
      return BL_STROKE_CAP_BUTT;
  }
  return BL_STROKE_CAP_BUTT;
}

BLStrokeJoin join_of(LineJoin join) {
  switch (join) {
    case LineJoin::kRound:
      return BL_STROKE_JOIN_ROUND;
    case LineJoin::kBevel:
      return BL_STROKE_JOIN_BEVEL;
    case LineJoin::kMiter:
      return BL_STROKE_JOIN_MITER_BEVEL;  // past the miter limit a miter becomes a bevel (§8.4.3.5)
  }
  return BL_STROKE_JOIN_MITER_BEVEL;
}

double length_scale(const Matrix& m) { return std::sqrt(std::abs(m.determinant())); }

BLCompOp comp_op_of(BlendMode mode) {
  switch (mode) {
    case BlendMode::kMultiply:
      return BL_COMP_OP_MULTIPLY;
    case BlendMode::kScreen:
      return BL_COMP_OP_SCREEN;
    case BlendMode::kOverlay:
      return BL_COMP_OP_OVERLAY;
    case BlendMode::kDarken:
      return BL_COMP_OP_DARKEN;
    case BlendMode::kLighten:
      return BL_COMP_OP_LIGHTEN;
    case BlendMode::kColorDodge:
      return BL_COMP_OP_COLOR_DODGE;
    case BlendMode::kColorBurn:
      return BL_COMP_OP_COLOR_BURN;
    case BlendMode::kHardLight:
      return BL_COMP_OP_HARD_LIGHT;
    case BlendMode::kSoftLight:
      return BL_COMP_OP_SOFT_LIGHT;
    case BlendMode::kDifference:
      return BL_COMP_OP_DIFFERENCE;
    case BlendMode::kExclusion:
      return BL_COMP_OP_EXCLUSION;
    default:
      return BL_COMP_OP_SRC_OVER;
  }
}

// Whether Blend2D composites the separable blend modes itself. Only its JIT pipelines do: without the
// JIT (WebAssembly, sanitizer and fuzzing builds) painting with those operators fails with "not
// implemented" and draws nothing. Asked once, by painting a pixel.
bool native_blend_modes() {
  static const bool kNative = [] {
    BLImage image;
    BLContext context;
    if (image.create(1, 1, BL_FORMAT_PRGB32) != BL_SUCCESS || context.begin(image) != BL_SUCCESS) {
      return false;
    }
    context.set_comp_op(BL_COMP_OP_MULTIPLY);
    const bool ok = context.fill_all(BLRgba32(0xFF808080U)) == BL_SUCCESS;
    context.end();
    return ok;
  }();
  return kNative;
}

// A rectangle of device pixels on the page, [x0, x1) × [y0, y1).
struct PixelBox {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  [[nodiscard]] bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
  [[nodiscard]] PixelBox intersect(const PixelBox& o) const noexcept {
    return {.x0 = std::max(x0, o.x0), .y0 = std::max(y0, o.y0), .x1 = std::min(x1, o.x1), .y1 = std::min(y1, o.y1)};
  }
};

// The pixels a box of real device coordinates touches, `margin` pixels wider on each side; clamped so
// that hostile coordinates stay integers.
PixelBox pixels_of(double x0, double y0, double x1, double y1, double margin = 0) {
  if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || x0 > x1 || y0 > y1) {
    return {};
  }
  constexpr double kFar = 1e9;
  const auto low = [&](double v) { return static_cast<int>(std::floor(std::clamp(v - margin, -kFar, kFar))); };
  const auto high = [&](double v) { return static_cast<int>(std::ceil(std::clamp(v + margin, -kFar, kFar))); };
  return {.x0 = low(x0), .y0 = low(y0), .x1 = high(x1), .y1 = high(y1)};
}

// The device box of a path's points (a cubic lies within its control points) under m.
PixelBox bounds_of(const Path& path, const Matrix& m, double margin) {
  double x0 = std::numeric_limits<double>::max();
  double y0 = x0;
  double x1 = std::numeric_limits<double>::lowest();
  double y1 = x1;
  path.for_each([&](SegmentKind kind, const Point* p) {
    for (std::size_t i = 0; i < Path::points_of(kind); ++i) {
      const Point q = m.apply(p[i]);
      x0 = std::min(x0, q.x);
      y0 = std::min(y0, q.y);
      x1 = std::max(x1, q.x);
      y1 = std::max(y1, q.y);
    }
  });
  return pixels_of(x0, y0, x1, y1, margin);
}

PixelBox bounds_of_rect(double x0, double y0, double x1, double y1, const Matrix& m) {
  Path box;
  box.move_to({x0, y0});
  box.line_to({x1, y0});
  box.line_to({x1, y1});
  box.line_to({x0, y1});
  return bounds_of(box, m, 1);
}

// ── Geometry out of range ─────────────────────────────────────────────────────────────────
//
// Blend2D computes in fixed point; with coordinates or pen widths of astronomic size (a file can scale
// its CTM without bound) its stroker and edge builder overflow and it reads outside the image (found by
// fuzz_render). Nothing that large is ever handed to it: geometry beyond kSafeCoordinate device pixels
// is brought to device space here and clipped, in double precision, to the page's box widened by a
// margin, where it looks the same.
constexpr double kSafeCoordinate = 1e6;

bool within_safe_range(const Path& path, const Matrix& m) {
  bool safe = true;
  path.for_each([&](SegmentKind kind, const Point* p) {
    for (std::size_t i = 0; i < Path::points_of(kind); ++i) {
      const Point q = m.apply(p[i]);
      safe = safe && std::abs(q.x) <= kSafeCoordinate && std::abs(q.y) <= kSafeCoordinate;  // false for NaN too
    }
  });
  return safe;
}

bool rect_within_safe_range(double x0, double y0, double x1, double y1, const Matrix& m) {
  Path box;
  box.move_to({x0, y0});
  box.line_to({x1, y0});
  box.line_to({x1, y1});
  box.line_to({x0, y1});
  return within_safe_range(box, m);
}

struct Rect2 {
  double x0, y0, x1, y1;
};

// The path in device space as polylines (curves as chords: this is for geometry far out of range, where
// only its crossing of the page matters), without non-finite points.
std::vector<std::pair<std::vector<Point>, bool>> device_polylines(const Path& path, const Matrix& m) {
  std::vector<std::pair<std::vector<Point>, bool>> out;
  Point current;
  const auto add = [&](Point q) {
    if (std::isfinite(q.x) && std::isfinite(q.y) && !out.empty()) {
      out.back().first.push_back(q);
    }
  };
  path.for_each([&](SegmentKind kind, const Point* p) {
    switch (kind) {
      case SegmentKind::kMove:
        out.emplace_back();
        current = p[0];
        add(m.apply(p[0]));
        break;
      case SegmentKind::kLine:
        if (out.empty()) {
          out.emplace_back();
        }
        add(m.apply(p[0]));
        current = p[0];
        break;
      case SegmentKind::kCubic: {
        if (out.empty()) {
          out.emplace_back();
        }
        constexpr int kChords = 32;
        for (int i = 1; i <= kChords; ++i) {
          const double t = static_cast<double>(i) / kChords;
          const double u = 1 - t;
          const Point q{(u * u * u * current.x) + (3 * u * u * t * p[0].x) + (3 * u * t * t * p[1].x) + (t * t * t * p[2].x),
                        (u * u * u * current.y) + (3 * u * u * t * p[0].y) + (3 * u * t * t * p[1].y) + (t * t * t * p[2].y)};
          add(m.apply(q));
        }
        current = p[2];
        break;
      }
      case SegmentKind::kClose:
        if (!out.empty()) {
          out.back().second = true;
        }
        break;
    }
  });
  return out;
}

// Sutherland–Hodgman: a closed polygon clipped to a rectangle. Clipping every contour of a path to the
// same convex box keeps the winding number of every point inside it, so fill rules still hold.
std::vector<Point> clip_polygon(std::vector<Point> polygon, const Rect2& r) {
  const auto clip_edge = [](const std::vector<Point>& in, auto inside, auto cross) {
    std::vector<Point> result;
    for (std::size_t i = 0; i < in.size(); ++i) {
      const Point& a = in[i];
      const Point& b = in[(i + 1) % in.size()];
      if (inside(b)) {
        if (!inside(a)) {
          result.push_back(cross(a, b));
        }
        result.push_back(b);
      } else if (inside(a)) {
        result.push_back(cross(a, b));
      }
    }
    return result;
  };
  const auto at_x = [](double x) { return [x](const Point& a, const Point& b) { return Point{x, a.y + ((b.y - a.y) * (x - a.x) / (b.x - a.x))}; }; };
  const auto at_y = [](double y) { return [y](const Point& a, const Point& b) { return Point{a.x + ((b.x - a.x) * (y - a.y) / (b.y - a.y)), y}; }; };
  polygon = clip_edge(polygon, [&](const Point& q) { return q.x >= r.x0; }, at_x(r.x0));
  polygon = clip_edge(polygon, [&](const Point& q) { return q.x <= r.x1; }, at_x(r.x1));
  polygon = clip_edge(polygon, [&](const Point& q) { return q.y >= r.y0; }, at_y(r.y0));
  polygon = clip_edge(polygon, [&](const Point& q) { return q.y <= r.y1; }, at_y(r.y1));
  return polygon;
}

// Liang–Barsky: the part of segment a–b inside the rectangle; false when none.
bool clip_segment(Point& a, Point& b, const Rect2& r) {
  double t0 = 0;
  double t1 = 1;
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const std::array<double, 4> p = {-dx, dx, -dy, dy};
  const std::array<double, 4> q = {a.x - r.x0, r.x1 - a.x, a.y - r.y0, r.y1 - a.y};
  for (std::size_t i = 0; i < 4; ++i) {
    if (p.at(i) == 0) {
      if (q.at(i) < 0) {
        return false;
      }
      continue;
    }
    const double t = q.at(i) / p.at(i);
    if (p.at(i) < 0) {
      t0 = std::max(t0, t);
    } else {
      t1 = std::min(t1, t);
    }
  }
  if (t0 > t1) {
    return false;
  }
  const Point start{a.x + (t0 * dx), a.y + (t0 * dy)};
  b = Point{a.x + (t1 * dx), a.y + (t1 * dy)};
  a = start;
  return true;
}

// A filled path out of range, in device space, clipped to r.
BLPath clipped_fill(const Path& path, const Matrix& m, const Rect2& r) {
  BLPath out;
  for (auto& [points, closed] : device_polylines(path, m)) {
    (void)closed;
    const std::vector<Point> polygon = clip_polygon(std::move(points), r);
    for (std::size_t i = 0; i < polygon.size(); ++i) {
      if (i == 0) {
        out.move_to(polygon[i].x, polygon[i].y);
      } else {
        out.line_to(polygon[i].x, polygon[i].y);
      }
    }
    if (!polygon.empty()) {
      out.close();
    }
  }
  return out;
}

// A stroked path out of range, in device space: its segments clipped to r (widened by the pen by the
// caller), the pieces that stay connected kept as one subpath.
BLPath clipped_stroke(const Path& path, const Matrix& m, const Rect2& r) {
  BLPath out;
  for (auto& [points, closed] : device_polylines(path, m)) {
    if (closed && points.size() > 1) {
      points.push_back(points.front());
    }
    bool open = false;
    Point last;
    for (std::size_t i = 1; i < points.size(); ++i) {
      Point a = points[i - 1];
      Point b = points[i];
      if (!clip_segment(a, b, r)) {
        open = false;
        continue;
      }
      if (!open || a.x != last.x || a.y != last.y) {
        out.move_to(a.x, a.y);
      }
      out.line_to(b.x, b.y);
      last = b;
      open = true;
    }
  }
  return out;
}

// A soft mask: coverage 0..255 over a box of the page; outside the box nothing is painted while it is
// in force (the clip at the time it was set contains everything painted under it).
struct SoftMask {
  BLImage a8;
  PixelBox box;
};

// A copy of pixels at a box of the page: the initial backdrop of a knockout group.
struct Backdrop {
  BLImage image;
  PixelBox box;
};

const BLRgba kTransparent(0, 0, 0, 0);

enum class SurfaceKind : std::uint8_t { kPage, kClip, kGroup, kSoftMask, kShape };

// A surface drawn on: the page, a clip layer, a transparency group (composited as one object at
// EndGroup), the group of a soft mask (turned into a mask at EndSoftMask), or the A8 "shape" twin that
// follows a surface inside a non-isolated group: the alpha of the group's own objects, without the
// backdrop, which removing the backdrop at the end needs (§11.4.8).
//
// A clip layer starts as a copy of what is below it and ends by keeping its pixels where the clip
// covers and the ones below elsewhere: so that painting inside it blends with the real backdrop.
struct Surface {
  double alpha = 1;                // kGroup: the alpha it is composited with
  std::unique_ptr<Surface> shape;  // the kShape twin, inside a non-isolated group that needs one
  BLImage image;                   // PRGB32; A8 for kShape
  BLContext context;
  BLImage mask;  // kClip: the clip, A8
  // kGroup and kSoftMask: the soft mask in force before it (a group is composited through it).
  std::shared_ptr<const SoftMask> soft_mask;
  std::shared_ptr<const std::vector<std::uint8_t>> transfer;  // kSoftMask: /TR as a table
  // Inside a knockout group (the group and the clip layers in it): each object composites with the
  // group's initial backdrop (null: transparent), replacing what the group held where it has shape.
  std::shared_ptr<const Backdrop> initial;
  int x = 0;  // position on the page, in device pixels
  int y = 0;
  int width = 0;  // 0: nothing can be drawn on it (an empty clip, or no memory)
  int height = 0;
  int level = 0;  // the save level it was made at
  SurfaceKind kind = SurfaceKind::kPage;
  BlendMode blend = BlendMode::kNormal;  // kGroup
  SoftMaskKind mask_kind = SoftMaskKind::kAlpha;
  bool knockout = false;
  bool remove_backdrop = false;  // kGroup: non-isolated, composed on a copy of its backdrop

  [[nodiscard]] PixelBox box() const noexcept { return {.x0 = x, .y0 = y, .x1 = x + width, .y1 = y + height}; }
};

std::uint32_t pixel_at(const BLImageData& data, int col, int row) {
  std::uint32_t v = 0;
  std::memcpy(&v, static_cast<const std::uint8_t*>(data.pixel_data) + (static_cast<std::ptrdiff_t>(row) * data.stride) + (static_cast<std::ptrdiff_t>(col) * 4),
              4);
  return v;
}

std::uint8_t byte_at(const BLImageData& data, int col, int row) {
  return static_cast<const std::uint8_t*>(data.pixel_data)[(static_cast<std::ptrdiff_t>(row) * data.stride) + col];
}

PremultipliedRgba unpack(std::uint32_t argb) {
  constexpr float kUnit = 1.0F / 255;
  return {.r = static_cast<float>((argb >> 16U) & 0xFFU) * kUnit,
          .g = static_cast<float>((argb >> 8U) & 0xFFU) * kUnit,
          .b = static_cast<float>(argb & 0xFFU) * kUnit,
          .a = static_cast<float>(argb >> 24U) * kUnit};
}

std::uint8_t to_byte(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); }

std::uint32_t pack(PremultipliedRgba c) {
  const std::uint32_t a = to_byte(c.a);
  // Premultiplied: no colour component above alpha.
  const auto colour = [&](float v) { return std::min<std::uint32_t>(to_byte(v), a); };
  return (a << 24U) | (colour(c.r) << 16U) | (colour(c.g) << 8U) | colour(c.b);
}

PremultipliedRgba mix(const PremultipliedRgba& a, const PremultipliedRgba& b, float t) {
  return {.r = a.r + ((b.r - a.r) * t), .g = a.g + ((b.g - a.g) * t), .b = a.b + ((b.b - a.b) * t), .a = a.a + ((b.a - a.a) * t)};
}

class Rasterizer {
 public:
  Rasterizer(const DisplayList& list, const RenderLimits& limits, const Matrix& page) : list_(&list), limits_(&limits), page_(page) {}

  Result<Bitmap> run(int width, int height) {
    Surface root;
    if (root.image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS || root.context.begin(root.image) != BL_SUCCESS) {
      return make_error(ErrorCode::kLimitExceeded, "cannot allocate a " + std::to_string(width) + "x" + std::to_string(height) + " bitmap");
    }
    // The page's content is composed on a transparent backdrop and then onto the white of the paper:
    // blend modes act on what was painted, never on the paper (as the reference readers do).
    root.context.clear_all();
    root.width = width;
    root.height = height;
    clip_ = root.box();
    layer_budget_ = static_cast<std::int64_t>(static_cast<double>(width) * height * limits_->max_layer_area);
    stack_.push_back(std::move(root));
    scan_groups();

    for (const Command& command : list_->commands) {
      std::visit([this](const auto& c) { this->draw(c); }, command);
    }
    while (stack_.size() > 1) {
      finish_top();
    }
    Surface& page = stack_.back();
    page.context.end();

    Bitmap out;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    BLImageData data{};
    page.image.get_data(&data);
    for (int row = 0; row < height; ++row) {
      std::uint8_t* target = &out.rgba[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4];
      for (int col = 0; col < width; ++col) {
        const std::uint32_t argb = pixel_at(data, col, row);  // PRGB32: 0xAARRGGBB
        // Over white: premultiplied c + (1 − α)·255.
        const std::uint32_t paper = 255U - (argb >> 24U);
        target[(col * 4) + 0] = static_cast<std::uint8_t>(((argb >> 16U) & 0xFFU) + paper);
        target[(col * 4) + 1] = static_cast<std::uint8_t>(((argb >> 8U) & 0xFFU) + paper);
        target[(col * 4) + 2] = static_cast<std::uint8_t>((argb & 0xFFU) + paper);
        target[(col * 4) + 3] = 255;
      }
    }
    return out;
  }

 private:
  // For every group of the list, in order: whether something inside it (at any depth) is painted or
  // composited with a blend mode other than Normal. Without that, a non-isolated group gives exactly
  // what an isolated one gives, and needs no copy of its backdrop.
  void scan_groups() {
    std::vector<std::size_t> open;
    for (const Command& command : list_->commands) {
      if (const auto* begin = std::get_if<BeginGroup>(&command)) {
        for (const std::size_t g : open) {
          blends_inside_[g] = blends_inside_[g] || begin->blend != BlendMode::kNormal;
        }
        open.push_back(blends_inside_.size());
        blends_inside_.push_back(false);
      } else if (std::holds_alternative<EndGroup>(command)) {
        if (!open.empty()) {
          open.pop_back();
        }
      } else {
        const BlendMode blend = std::visit(
            [](const auto& c) {
              using C = std::decay_t<decltype(c)>;
              if constexpr (std::is_same_v<C, FillPath> || std::is_same_v<C, StrokePath>) {
                return c.blend;
              } else if constexpr (std::is_same_v<C, Boxed<DrawImage>> || std::is_same_v<C, Boxed<ShowText>>) {
                return c->blend;
              } else {
                return BlendMode::kNormal;
              }
            },
            command);
        if (blend != BlendMode::kNormal) {
          for (const std::size_t g : open) {
            blends_inside_[g] = true;
          }
        }
      }
    }
  }

  [[nodiscard]] Matrix device(const Matrix& ctm, const Surface& s) const { return ctm.then(page_).then(Matrix::translate(-s.x, -s.y)); }

  [[nodiscard]] BLRgba color(ColorIndex index, double alpha) const {
    const Rgb rgb = index < list_->colors.size() ? list_->colors[index].rgb : Rgb{};
    return {static_cast<float>(rgb.r), static_cast<float>(rgb.g), static_cast<float>(rgb.b), static_cast<float>(std::clamp(alpha, 0.0, 1.0))};
  }

  // ── The graphics state ────────────────────────────────────────────────────────────────────

  void draw(const Save& /*unused*/) {
    ++level_;
    saved_.push_back({.clip = clip_, .soft_mask = soft_mask_});
    Surface& top = stack_.back();
    top.context.save();  // rectangular clips live in the context's state
    if (top.shape) {
      top.shape->context.save();
    }
  }

  void draw(const Restore& /*unused*/) {
    while (stack_.size() > 1 && stack_.back().level == level_) {
      finish_top();
    }
    Surface& top = stack_.back();
    top.context.restore();
    if (top.shape) {
      top.shape->context.restore();
    }
    if (!saved_.empty()) {
      clip_ = saved_.back().clip;
      soft_mask_ = saved_.back().soft_mask;
      saved_.pop_back();
    }
    level_ = std::max(0, level_ - 1);
  }

  void draw(const ClipPath& clip) {
    if (!clip_to_rectangle(clip)) {
      push_clip_layer(clip);
    }
  }

  // ── Painting ──────────────────────────────────────────────────────────────────────────────

  // Paints through the blend mode, the soft mask and the knockout in force. `draw(surface)` sets its
  // transform for the surface and paints; `box` bounds what it paints on the page; `opacity` is the
  // object's constant alpha (what separates its shape from its opacity in a knockout group). Straight
  // onto the surface when Blend2D can do it; otherwise onto a transparent layer of the box, composited
  // here.
  template <typename Draw>
  void paint(BlendMode blend, double opacity, const PixelBox& box, const Draw& draw) {
    Surface& target = stack_.back();
    if (target.width == 0) {
      return;
    }
    if (!soft_mask_ && !target.knockout && (blend == BlendMode::kNormal || (is_separable(blend) && native_blend_modes()))) {
      target.context.set_comp_op(comp_op_of(blend));
      draw(target);
      target.context.set_comp_op(BL_COMP_OP_SRC_OVER);
      if (target.shape) {
        draw(*target.shape);  // the shape only adds up alphas: always "over"
      }
      return;
    }
    const PixelBox area = box.intersect(clip_).intersect(target.box()).intersect(soft_mask_ ? soft_mask_->box : target.box());
    if (area.empty()) {
      return;
    }
    Surface layer;
    if (!make_surface(layer, area, kTransparent)) {
      draw(target);  // no memory for the layer: plainly, the nearest that can be done
      return;
    }
    draw(layer);
    layer.context.end();
    composite(layer, target, blend, 1, soft_mask_.get(), opacity);
    release(layer);
  }

  // The page's box, widened by margin device pixels: where geometry out of range is clipped to.
  [[nodiscard]] Rect2 page_box(double margin) const {
    const PixelBox b = stack_.front().box();
    return {.x0 = b.x0 - margin, .y0 = b.y0 - margin, .x1 = b.x1 + margin, .y1 = b.y1 + margin};
  }

  void draw(const FillPath& fill) {
    const Matrix m = fill.ctm.then(page_);
    const bool safe = within_safe_range(fill.path, m);
    // Out of range: clipped here, drawn in device space (to_device is the identity on the page).
    const BLPath geometry = safe ? to_blend2d(fill.path) : clipped_fill(fill.path, m, page_box(64));
    const Matrix& to_device = safe ? fill.ctm : kIdentity;
    const PixelBox box = safe ? bounds_of(fill.path, m, 1) : stack_.front().box();
    paint(fill.blend, fill.alpha, box, [&](Surface& s) {
      s.context.set_transform(to_blend2d(safe ? device(to_device, s) : Matrix::translate(-s.x, -s.y)));
      s.context.set_fill_rule(fill.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
      s.context.fill_path(geometry, color(fill.color, fill.alpha));
    });
  }

  void draw(const StrokePath& stroke) {
    const StrokeStyle style = stroke.style < list_->styles.size() ? list_->styles[stroke.style] : StrokeStyle{};
    const Matrix on_page = stroke.ctm.then(page_);
    const double scale = length_scale(on_page);
    // Never thinner than a device pixel, like the reference readers (width 0: the thinnest line, §8.4.3.2).
    double width = style.width;
    if (scale > 0 && width * scale < 1) {
      width = 1 / scale;
    }
    const Path* path = &stroke.path;
    DashedPath dashed;
    if (!style.dash.empty() && scale > 0) {
      dashed = dash_path(stroke.path, style.dash, style.dash_phase, 0.2 / scale, limits_->max_dash_segments);
      path = &dashed.path;
    }
    // A stroke reaches half its width past the path, more at miters and square caps.
    const double reach = (std::abs(width) * scale / 2 * std::max({style.miter_limit, std::numbers::sqrt2, 1.0})) + 2;
    const Matrix& ctm = stroke.ctm;
    // Out of range (the path or the pen): the segments clipped here in device space, the pen in device
    // pixels, no wider than what covers the page anyway.
    const PixelBox page = stack_.front().box();
    const double pen_limit = 4.0 * ((page.x1 - page.x0) + (page.y1 - page.y0));
    const bool safe = within_safe_range(*path, on_page) && std::isfinite(reach) && reach <= kSafeCoordinate;
    const double pen = safe ? width : std::min(std::abs(width) * scale, pen_limit);
    const BLPath geometry = safe ? to_blend2d(*path) : clipped_stroke(*path, on_page, page_box(std::min(reach, pen_limit) + 64));
    if (!std::isfinite(pen)) {
      return;
    }
    paint(stroke.blend, stroke.alpha, safe ? bounds_of(*path, on_page, std::min(reach, 1e7)) : page, [&](Surface& s) {
      s.context.set_transform(to_blend2d(safe ? device(ctm, s) : Matrix::translate(-s.x, -s.y)));
      s.context.set_stroke_width(pen);
      s.context.set_stroke_caps(cap_of(style.cap));
      s.context.set_stroke_join(join_of(style.join));
      s.context.set_stroke_miter_limit(std::max(style.miter_limit, 1.0));
      s.context.stroke_path(geometry, color(stroke.color, stroke.alpha));
    });
  }

  void draw(const Boxed<DrawImage>& boxed) {
    const DrawImage& image = *boxed;
    if (image.pixels && image.pixels->width > 0 && image.pixels->height > 0) {
      draw_pixels(image);
      return;
    }
    // Not decoded (a codec not implemented yet, or broken data): the unit square as a placeholder —
    // except inside a soft mask's group, where it would turn into a solid mask (a shadow drawn as a
    // black box): there, nothing.
    if (std::ranges::any_of(stack_, [](const Surface& s) { return s.kind == SurfaceKind::kSoftMask; })) {
      return;
    }
    const double opacity = image.mask ? image.alpha * 0.35 : 1;
    if (!rect_within_safe_range(0, 0, 1, 1, image.ctm.then(page_))) {
      return;  // at an astronomic scale nothing meaningful is left to draw (see kSafeCoordinate)
    }
    paint(image.blend, opacity, bounds_of_rect(0, 0, 1, 1, image.ctm.then(page_)), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(image.ctm, s)));
      s.context.fill_rect(BLRect(0, 0, 1, 1), image.mask ? color(image.color, opacity) : BLRgba(0.875F, 0.898F, 0.953F, 1.0F));
    });
  }

  // An image: its pixels as a pattern over the unit square (the first row at the top, §8.9.4),
  // smoothed when drawn smaller, or larger when it asks for it (/Interpolate); drawn much smaller, it
  // is first reduced by averaging blocks of pixels, which bilinear sampling alone would alias.
  void draw_pixels(const DrawImage& image) {
    const ImagePixels& pixels = *image.pixels;
    const Matrix on_page = image.ctm.then(page_);
    const double across = std::hypot(on_page.a, on_page.b);  // device pixels along the image's width
    const double down = std::hypot(on_page.c, on_page.d);
    const double reduce = std::min(pixels.width / std::max(across, 1e-9), pixels.height / std::max(down, 1e-9));
    const int factor = reduce >= 2 ? static_cast<int>(std::min(std::floor(reduce), 64.0)) : 1;
    const BLImage* picture = picture_of(pixels, factor);
    if (picture == nullptr) {
      return;
    }
    const BLSizeI size = picture->size();
    const bool smooth = image.interpolate || reduce > 1;
    const Matrix to_unit{1.0 / size.w, 0, 0, -1.0 / size.h, 0, 1};
    if (!rect_within_safe_range(0, 0, 1, 1, on_page)) {
      return;
    }
    paint(image.blend, image.alpha, bounds_of_rect(0, 0, 1, 1, on_page), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(image.ctm, s)));
      s.context.set_pattern_quality(smooth ? BL_PATTERN_QUALITY_BILINEAR : BL_PATTERN_QUALITY_NEAREST);
      s.context.set_global_alpha(std::clamp(image.alpha, 0.0, 1.0));
      s.context.fill_rect(BLRect(0, 0, 1, 1), BLPattern(*picture, BL_EXTEND_MODE_PAD, to_blend2d(to_unit)));
      s.context.set_global_alpha(1);
    });
  }

  // The pixels as a premultiplied Blend2D image, reduced by `factor` (block averages), made once per
  // rendering.
  const BLImage* picture_of(const ImagePixels& pixels, int factor) {
    const auto key = std::pair{&pixels, factor};
    if (const auto found = pictures_.find(key); found != pictures_.end()) {
      return &found->second;
    }
    const int w = std::max(1, pixels.width / factor);
    const int h = std::max(1, pixels.height / factor);
    BLImage picture;
    BLImageData out{};
    if (picture.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS || picture.make_mutable(&out) != BL_SUCCESS) {
      return nullptr;
    }
    const auto stride = static_cast<std::size_t>(pixels.width) * 4;
    for (int y = 0; y < h; ++y) {
      auto* row = static_cast<std::uint8_t*>(out.pixel_data) + (static_cast<std::ptrdiff_t>(y) * out.stride);
      for (int x = 0; x < w; ++x) {
        // The block's premultiplied average (the last block takes the leftover rows and columns).
        const int x1 = x + 1 == w ? pixels.width : (x + 1) * factor;
        const int y1 = y + 1 == h ? pixels.height : (y + 1) * factor;
        std::array<std::uint64_t, 4> sum{};
        std::uint64_t count = 0;
        for (int sy = y * factor; sy < y1; ++sy) {
          for (int sx = x * factor; sx < x1; ++sx) {
            const std::uint8_t* p = &pixels.rgba[(static_cast<std::size_t>(sy) * stride) + (static_cast<std::size_t>(sx) * 4)];
            const std::uint64_t a = p[3];
            sum[0] += p[0] * a;
            sum[1] += p[1] * a;
            sum[2] += p[2] * a;
            sum[3] += a;
            ++count;
          }
        }
        const auto channel = [](std::uint64_t v, std::uint64_t scale) {
          return static_cast<std::uint32_t>(std::min<std::uint64_t>(v / std::max<std::uint64_t>(scale, 1), 255));
        };
        const std::uint32_t a = channel(sum[3], count);
        const std::uint32_t argb = (a << 24U) | (channel(sum[0], count * 255) << 16U) | (channel(sum[1], count * 255) << 8U) | channel(sum[2], count * 255);
        std::memcpy(row + (static_cast<std::ptrdiff_t>(x) * 4), &argb, 4);
      }
    }
    return &pictures_.emplace(key, std::move(picture)).first->second;
  }

  void draw(const Boxed<PaintShading>& /*unused*/) {}

  void draw(const Boxed<ShowText>& boxed) {
    // The glyph boxes until the fonts step, as in the SVG preview.
    const ShowText& run = *boxed;
    const double h = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
    const double unit = run.font_size * h;
    if (run.render_mode == 3 || run.render_mode == 7 || unit == 0 || run.glyphs.empty()) {
      return;
    }
    const Matrix element = Matrix{unit, 0, 0, run.font_size, 0, run.glyphs.front().origin.y}.then(run.text_matrix);
    double x0 = 0;
    double x1 = 0;
    for (const Glyph& glyph : run.glyphs) {
      x0 = std::min(x0, glyph.origin.x / unit);
      x1 = std::max(x1, (glyph.origin.x / unit) + std::max(glyph.advance / unit * 0.85, 0.05));
    }
    const double opacity = run.alpha * 0.35;
    if (!rect_within_safe_range(x0, 0, x1, 0.68, element.then(page_))) {
      return;
    }
    paint(run.blend, opacity, bounds_of_rect(x0, 0, x1, 0.68, element.then(page_)), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(element, s)));
      const BLRgba fill = color(run.fill, opacity);
      for (const Glyph& glyph : run.glyphs) {
        s.context.fill_rect(BLRect(glyph.origin.x / unit, 0, std::max(glyph.advance / unit * 0.85, 0.05), 0.68), fill);
      }
    });
  }

  // ── Transparency groups and soft masks ─────────────────────────────────────────────────────

  void draw(const BeginGroup& group) {
    const bool blends = group_ordinal_ < blends_inside_.size() && blends_inside_[group_ordinal_];
    ++group_ordinal_;
    // Without blend modes inside, isolated or not gives the same result; a non-isolated group with
    // them, or any knockout group, is composed on a copy of its backdrop.
    const bool on_backdrop = !group.isolated && (blends || group.knockout);
    // Composited plainly (alpha 1, Normal, no soft mask, no knockout), a group that is not isolated
    // — or has nothing inside that the isolation would change — gives what drawing its content straight
    // onto the backdrop gives (§11.4.7): no layer needed.
    const bool plain = group.alpha >= 1 && group.blend == BlendMode::kNormal && !soft_mask_ && !group.knockout && (!group.isolated || !blends);
    Surface& below = stack_.back();
    const PixelBox area = clip_.intersect(below.box());
    Surface layer;
    layer.kind = SurfaceKind::kGroup;
    if (plain || below.width == 0 || area.empty() || stack_.size() > limits_->max_layers || !make_surface(layer, area, kTransparent)) {
      groups_.push_back(false);  // drawn straight onto what is below
      return;
    }
    groups_.push_back(true);
    layer.level = level_;
    layer.alpha = group.alpha;
    layer.blend = group.blend;
    layer.soft_mask = std::exchange(soft_mask_, nullptr);  // inside, no soft mask until one is set (§11.6.6)
    if (on_backdrop) {
      copy_below(layer, below);
      layer.shape = make_shape(layer, nullptr);
      layer.remove_backdrop = layer.shape != nullptr;
    }
    if (group.knockout) {
      layer.knockout = true;
      if (on_backdrop) {
        layer.initial = snapshot(below, layer.box());
      }
    }
    stack_.push_back(std::move(layer));
  }

  void draw(const EndGroup& /*unused*/) {
    if (groups_.empty()) {
      return;
    }
    const bool real = groups_.back();
    groups_.pop_back();
    if (!real) {
      return;
    }
    while (stack_.size() > 1) {
      const bool group = stack_.back().kind == SurfaceKind::kGroup;
      finish_top();
      if (group) {
        break;
      }
    }
  }

  void draw(const BeginSoftMask& mask) {
    Surface layer;
    layer.kind = SurfaceKind::kSoftMask;
    layer.level = level_;
    layer.mask_kind = mask.kind;
    layer.transfer = mask.transfer != nullptr && mask.transfer->size() == 256 ? mask.transfer : nullptr;
    layer.soft_mask = soft_mask_;  // back in force if this mask cannot be made
    const PixelBox area = clip_.intersect(stack_.front().box());
    const BLRgba ground = mask.kind == SoftMaskKind::kLuminosity
                              ? BLRgba(static_cast<float>(mask.backdrop.r), static_cast<float>(mask.backdrop.g), static_cast<float>(mask.backdrop.b), 1.0F)
                              : kTransparent;
    if (area.empty() || stack_.size() > limits_->max_layers || !make_surface(layer, area, ground)) {
      layer = Surface{};  // nothing of the mask's group is drawn, and no mask results
      layer.kind = SurfaceKind::kSoftMask;
      layer.level = level_;
      layer.soft_mask = soft_mask_;
    }
    soft_mask_ = nullptr;  // the mask's own group is drawn without one
    stack_.push_back(std::move(layer));
  }

  void draw(const EndSoftMask& /*unused*/) {
    const auto open = std::ranges::find(stack_, SurfaceKind::kSoftMask, &Surface::kind);
    if (open == stack_.end()) {
      return;
    }
    while (stack_.size() > 1) {
      const bool mask = stack_.back().kind == SurfaceKind::kSoftMask;
      finish_top();
      if (mask) {
        break;
      }
    }
  }

  void draw(const ClearSoftMask& /*unused*/) { soft_mask_ = nullptr; }

  // ── Clipping ──────────────────────────────────────────────────────────────────────────────

  // An axis-aligned rectangle on the device: Blend2D clips to it natively. False for any other shape.
  bool clip_to_rectangle(const ClipPath& clip) {
    std::vector<Point> corners;
    bool polygon = true;
    const Matrix m = clip.ctm.then(page_);
    clip.path.for_each([&](SegmentKind kind, const Point* p) {
      if (kind == SegmentKind::kCubic || (kind == SegmentKind::kMove && !corners.empty())) {
        polygon = false;
      } else if (kind != SegmentKind::kClose) {
        corners.push_back(m.apply(p[0]));
      }
    });
    if (!polygon || corners.size() < 4 || corners.size() > 5) {
      return false;
    }
    double x0 = corners[0].x;
    double y0 = corners[0].y;
    double x1 = x0;
    double y1 = y0;
    for (const Point& c : corners) {
      x0 = std::min(x0, c.x);
      y0 = std::min(y0, c.y);
      x1 = std::max(x1, c.x);
      y1 = std::max(y1, c.y);
    }
    constexpr double kEpsilon = 1e-6;
    for (const Point& c : corners) {
      const bool on_x = std::abs(c.x - x0) < kEpsilon || std::abs(c.x - x1) < kEpsilon;
      const bool on_y = std::abs(c.y - y0) < kEpsilon || std::abs(c.y - y1) < kEpsilon;
      if (!on_x || !on_y) {
        return false;
      }
    }
    clip_box(x0, y0, x1, y1);
    return true;
  }

  void clip_box(double x0, double y0, double x1, double y1) {
    // Out of range and not finite: the page's box is all that can matter (see kSafeCoordinate).
    const Rect2 page = page_box(64);
    const auto bound = [](double v, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };
    x0 = bound(x0, page.x0, page.x1);
    x1 = bound(x1, page.x0, page.x1);
    y0 = bound(y0, page.y0, page.y1);
    y1 = bound(y1, page.y0, page.y1);
    clip_ = clip_.intersect(pixels_of(x0, y0, x1, y1));
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
    for (Surface* on = &s; on != nullptr; on = on->shape.get()) {
      on->context.reset_transform();
      on->context.clip_to_rect(BLRect(x0 - s.x, y0 - s.y, x1 - x0, y1 - y0));
    }
  }

  // Any other clip: a layer the size of the clip's box, starting as a copy of what is below, with the
  // clip as its mask.
  void push_clip_layer(const ClipPath& clip) {
    const Matrix m = clip.ctm.then(page_);
    const PixelBox box = bounds_of(clip.path, m, 0);
    if (stack_.size() > limits_->max_layers) {
      clip_box(box.x0, box.y0, box.x1, box.y1);  // too many layers alive: the clip's box is the best that can be done
      return;
    }
    Surface layer;
    layer.kind = SurfaceKind::kClip;
    layer.level = level_;
    Surface& below = stack_.back();
    const PixelBox area = box.intersect(below.box()).intersect(clip_);
    clip_ = clip_.intersect(box);
    if (below.width == 0 || area.empty()) {
      stack_.push_back(std::move(layer));  // an empty clip: nothing inside is drawn
      return;
    }
    BLContext mask;
    if (!make_surface(layer, area, kTransparent) || layer.mask.create(layer.width, layer.height, BL_FORMAT_A8) != BL_SUCCESS ||
        mask.begin(layer.mask) != BL_SUCCESS) {
      release(layer);
      clip_box(box.x0, box.y0, box.x1, box.y1);  // no memory for the layer: clip to its box instead
      return;
    }
    mask.clear_all();
    mask.set_fill_rule(clip.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
    if (within_safe_range(clip.path, m)) {
      mask.set_transform(to_blend2d(m.then(Matrix::translate(-layer.x, -layer.y))));
      mask.fill_path(to_blend2d(clip.path), BLRgba32(0xFFFFFFFFU));
    } else {
      mask.set_transform(to_blend2d(Matrix::translate(-layer.x, -layer.y)));
      mask.fill_path(clipped_fill(clip.path, m, page_box(64)), BLRgba32(0xFFFFFFFFU));
    }
    mask.end();
    copy_below(layer, below);
    layer.knockout = below.knockout;
    layer.initial = below.initial;
    if (below.shape) {
      layer.shape = make_shape(layer, below.shape.get());
    }
    stack_.push_back(std::move(layer));
  }

  // ── Layers ────────────────────────────────────────────────────────────────────────────────

  // A layer over `area` of the page, filled with `ground`; false (and nothing allocated) past the
  // layer budget or without memory.
  bool make_surface(Surface& layer, const PixelBox& area, const BLRgba& ground, BLFormat format = BL_FORMAT_PRGB32) {
    const std::int64_t pixels = static_cast<std::int64_t>(area.x1 - area.x0) * (area.y1 - area.y0);
    const std::int64_t cost = format == BL_FORMAT_A8 ? (pixels + 3) / 4 : pixels;
    if (layer_pixels_ + cost > layer_budget_) {
      return false;
    }
    layer.x = area.x0;
    layer.y = area.y0;
    layer.width = area.x1 - area.x0;
    layer.height = area.y1 - area.y0;
    if (layer.image.create(layer.width, layer.height, format) != BL_SUCCESS || layer.context.begin(layer.image) != BL_SUCCESS) {
      layer.width = 0;
      return false;
    }
    layer.context.clear_all();  // a new image holds whatever the memory held
    if (ground.a > 0) {
      layer.context.fill_all(ground);
    }
    layer_pixels_ += cost;
    return true;
  }

  void release(Surface& layer) {
    if (layer.width > 0) {
      const std::int64_t pixels = static_cast<std::int64_t>(layer.width) * layer.height;
      layer_pixels_ -= layer.kind == SurfaceKind::kShape ? (pixels + 3) / 4 : pixels;
    }
    layer.width = 0;
    if (layer.shape) {
      release(*layer.shape);
    }
  }

  // The shape twin of `layer`: transparent, or a copy of `from` (the shape twin of what is below).
  std::unique_ptr<Surface> make_shape(const Surface& layer, Surface* from) {
    auto shape = std::make_unique<Surface>();
    shape->kind = SurfaceKind::kShape;
    shape->level = layer.level;
    if (!make_surface(*shape, layer.box(), kTransparent, BL_FORMAT_A8)) {
      return nullptr;
    }
    if (from != nullptr) {
      copy_below(*shape, *from);
    }
    return shape;
  }

  // Copies what `below` holds under `layer` into it.
  static void copy_below(Surface& layer, Surface& below) {
    below.context.flush(BL_CONTEXT_FLUSH_SYNC);
    layer.context.save();
    layer.context.reset_transform();
    layer.context.set_comp_op(BL_COMP_OP_SRC_COPY);
    layer.context.blit_image(BLPointI(below.x - layer.x, below.y - layer.y), below.image);
    layer.context.restore();
  }

  // What `from` holds at `box`, kept aside.
  static std::shared_ptr<const Backdrop> snapshot(Surface& from, const PixelBox& box) {
    auto copy = std::make_shared<Backdrop>();
    copy->box = box;
    BLContext context;
    if (copy->image.create(box.x1 - box.x0, box.y1 - box.y0, BL_FORMAT_PRGB32) != BL_SUCCESS || context.begin(copy->image) != BL_SUCCESS) {
      return nullptr;  // read as transparent
    }
    from.context.flush(BL_CONTEXT_FLUSH_SYNC);
    context.set_comp_op(BL_COMP_OP_SRC_COPY);
    context.blit_image(BLPointI(from.x - box.x0, from.y - box.y0), from.image);
    context.end();
    return copy;
  }

  // Writes `result` (covering `area`) into `target` through its context, as a copy: its rectangular
  // clip still applies.
  static void put(Surface& target, const PixelBox& area, const BLImage& result) {
    target.context.save();
    target.context.reset_transform();
    target.context.set_comp_op(BL_COMP_OP_SRC_COPY);
    target.context.blit_image(BLPointI(area.x0 - target.x, area.y0 - target.y), result);
    target.context.restore();
  }

  // Composites `layer` onto `target` pixel by pixel (§11.3.6): the layer's alpha scaled by `alpha` and
  // by the soft mask, blended with `blend`. In a knockout group the object composites with the group's
  // initial backdrop instead, and replaces what is there in proportion to its shape — its alpha over
  // `opacity`, the constant alpha it was painted with (§11.4.9). The shape twin, if any, follows.
  static void composite(const Surface& layer, Surface& target, BlendMode blend, double alpha, const SoftMask* mask, double opacity) {
    const PixelBox area = layer.box().intersect(target.box());
    if (area.empty() || target.width == 0) {
      return;
    }
    target.context.flush(BL_CONTEXT_FLUSH_SYNC);
    Surface* shape = target.shape.get();
    if (shape != nullptr) {
      shape->context.flush(BL_CONTEXT_FLUSH_SYNC);
    }
    BLImageData below{};
    BLImageData above{};
    BLImageData coverage{};
    BLImageData initial{};
    BLImageData shape_below{};
    target.image.get_data(&below);
    layer.image.get_data(&above);
    if (mask != nullptr) {
      mask->a8.get_data(&coverage);
    }
    const Backdrop* start = target.knockout ? target.initial.get() : nullptr;
    if (start != nullptr) {
      start->image.get_data(&initial);
    }
    if (shape != nullptr) {
      shape->image.get_data(&shape_below);
    }
    BLImage result;
    BLImageData out{};
    BLImage shape_result;
    BLImageData shape_out{};
    const int w = area.x1 - area.x0;
    const int h = area.y1 - area.y0;
    if (result.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS || result.make_mutable(&out) != BL_SUCCESS ||
        (shape != nullptr && (shape_result.create(w, h, BL_FORMAT_A8) != BL_SUCCESS || shape_result.make_mutable(&shape_out) != BL_SUCCESS))) {
      return;
    }
    const auto global = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    const auto constant = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    const auto inside = [](const PixelBox& b, int x, int y) { return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1; };
    for (int y = area.y0; y < area.y1; ++y) {
      auto* row = static_cast<std::uint8_t*>(out.pixel_data) + (static_cast<std::ptrdiff_t>(y - area.y0) * out.stride);
      auto* shape_row =
          shape != nullptr ? static_cast<std::uint8_t*>(shape_out.pixel_data) + (static_cast<std::ptrdiff_t>(y - area.y0) * shape_out.stride) : nullptr;
      for (int x = area.x0; x < area.x1; ++x) {
        const std::uint32_t backdrop = pixel_at(below, x - target.x, y - target.y);
        const std::uint32_t source = pixel_at(above, x - layer.x, y - layer.y);
        const float group_alpha = shape != nullptr ? static_cast<float>(byte_at(shape_below, x - shape->x, y - shape->y)) / 255 : 0;
        float factor = global;
        if (mask != nullptr) {
          factor *= inside(mask->box, x, y) ? static_cast<float>(byte_at(coverage, x - mask->box.x0, y - mask->box.y0)) / 255 : 0;
        }
        std::uint32_t mixed = backdrop;
        float shape_mixed = group_alpha;
        if ((source >> 24U) != 0 && factor > 0) {
          const PremultipliedRgba raw = unpack(source);
          const PremultipliedRgba s{.r = raw.r * factor, .g = raw.g * factor, .b = raw.b * factor, .a = raw.a * factor};
          if (target.knockout) {
            const PremultipliedRgba ground =
                start != nullptr && inside(start->box, x, y) ? unpack(pixel_at(initial, x - start->box.x0, y - start->box.y0)) : PremultipliedRgba{};
            const float f = constant > 0 ? std::min(1.0F, raw.a / constant) : 0;
            mixed = pack(mix(unpack(backdrop), render::composite(blend, ground, s), f));
            shape_mixed = group_alpha + ((s.a - group_alpha) * f);
          } else {
            mixed = pack(render::composite(blend, unpack(backdrop), s));
            shape_mixed = s.a + (group_alpha * (1 - s.a));
          }
        }
        std::memcpy(row + (static_cast<std::ptrdiff_t>(x - area.x0) * 4), &mixed, 4);
        if (shape_row != nullptr) {
          shape_row[x - area.x0] = to_byte(shape_mixed);
        }
      }
    }
    put(target, area, result);
    if (shape != nullptr) {
      put(*shape, area, shape_result);
    }
  }

  // A non-isolated group, composed on a copy of its backdrop, becomes what it would be on its own
  // (§11.4.8): C = Cn + (Cn − C0)·(α0/αg − α0), with alpha αg, the group's own.
  static void remove_backdrop(Surface& group, Surface& below) {
    below.context.flush(BL_CONTEXT_FLUSH_SYNC);
    BLImageData pixels{};
    BLImageData base{};
    BLImageData shape{};
    if (group.image.make_mutable(&pixels) != BL_SUCCESS) {
      return;
    }
    below.image.get_data(&base);
    group.shape->image.get_data(&shape);
    for (int y = 0; y < group.height; ++y) {
      auto* row = static_cast<std::uint8_t*>(pixels.pixel_data) + (static_cast<std::ptrdiff_t>(y) * pixels.stride);
      for (int x = 0; x < group.width; ++x) {
        const float ag = static_cast<float>(byte_at(shape, x, y)) / 255;
        std::uint32_t value = 0;
        if (ag > 0) {
          const PremultipliedRgba n = unpack(pixel_at(pixels, x, y));
          const PremultipliedRgba b = unpack(pixel_at(base, x + group.x - below.x, y + group.y - below.y));
          const auto straight = [](float c, float a) { return a > 0 ? c / a : 0.0F; };
          const float k = (b.a / ag) - b.a;
          const auto channel = [&](float cn, float c0) {
            return std::clamp(straight(cn, n.a) + ((straight(cn, n.a) - straight(c0, b.a)) * k), 0.0F, 1.0F) * ag;
          };
          value = pack({.r = channel(n.r, b.r), .g = channel(n.g, b.g), .b = channel(n.b, b.b), .a = ag});
        }
        std::memcpy(row + (static_cast<std::ptrdiff_t>(x) * 4), &value, 4);
      }
    }
  }

  // Ends the top surface: a clip layer through its mask, a group composited, a soft mask made.
  void finish_top() {
    Surface layer = std::move(stack_.back());
    stack_.pop_back();
    if (layer.kind == SurfaceKind::kSoftMask) {
      soft_mask_ = layer.width > 0 ? make_soft_mask(layer) : layer.soft_mask;
      release(layer);
      return;
    }
    if (layer.kind == SurfaceKind::kGroup) {
      soft_mask_ = layer.soft_mask;
    }
    if (layer.width == 0) {
      return;
    }
    layer.context.end();
    if (layer.shape) {
      layer.shape->context.end();
    }
    Surface& below = stack_.back();
    if (layer.kind == SurfaceKind::kGroup) {
      if (layer.remove_backdrop) {
        remove_backdrop(layer, below);
      }
      composite(layer, below, layer.blend, layer.alpha, layer.soft_mask.get(), 1);
      release(layer);
      return;
    }
    // A clip layer: its pixels where the clip covers, what is below elsewhere.
    if (below.width > 0) {
      through_clip(layer, below, layer.image, below.image, 4);
      if (layer.shape && below.shape) {
        through_clip(layer, *below.shape, layer.shape->image, below.shape->image, 1);
      }
    }
    release(layer);
  }

  // target = target·(1 − m) + source·m over the clip layer's box, for 4-byte or 1-byte pixels.
  static void through_clip(const Surface& layer, Surface& target, const BLImage& source, const BLImage& current, int bytes) {
    target.context.flush(BL_CONTEXT_FLUSH_SYNC);
    BLImageData from{};
    BLImageData under{};
    BLImageData mask{};
    source.get_data(&from);
    current.get_data(&under);
    layer.mask.get_data(&mask);
    const PixelBox area = layer.box().intersect(target.box());
    if (area.empty()) {
      return;
    }
    BLImage result;
    BLImageData out{};
    if (result.create(area.x1 - area.x0, area.y1 - area.y0, bytes == 4 ? BL_FORMAT_PRGB32 : BL_FORMAT_A8) != BL_SUCCESS ||
        result.make_mutable(&out) != BL_SUCCESS) {
      return;
    }
    for (int y = area.y0; y < area.y1; ++y) {
      const auto* s = static_cast<const std::uint8_t*>(from.pixel_data) + (static_cast<std::ptrdiff_t>(y - layer.y) * from.stride);
      const auto* u = static_cast<const std::uint8_t*>(under.pixel_data) + (static_cast<std::ptrdiff_t>(y - target.y) * under.stride);
      auto* o = static_cast<std::uint8_t*>(out.pixel_data) + (static_cast<std::ptrdiff_t>(y - area.y0) * out.stride);
      for (int x = area.x0; x < area.x1; ++x) {
        const unsigned m = byte_at(mask, x - layer.x, y - layer.y);
        for (int c = 0; c < bytes; ++c) {
          const unsigned a = s[((x - layer.x) * bytes) + c];
          const unsigned b = u[((x - target.x) * bytes) + c];
          o[((x - area.x0) * bytes) + c] = static_cast<std::uint8_t>(((a * m) + (b * (255 - m)) + 127) / 255);
        }
      }
    }
    put(target, area, result);
  }

  // The mask a soft mask group gives: its alpha, or the luminosity of the group over its backdrop.
  static std::shared_ptr<const SoftMask> make_soft_mask(Surface& layer) {
    layer.context.end();
    auto mask = std::make_shared<SoftMask>();
    mask->box = layer.box();
    BLImageData pixels{};
    BLImageData out{};
    if (mask->a8.create(layer.width, layer.height, BL_FORMAT_A8) != BL_SUCCESS || mask->a8.make_mutable(&out) != BL_SUCCESS) {
      return layer.soft_mask;
    }
    layer.image.get_data(&pixels);
    for (int row = 0; row < layer.height; ++row) {
      auto* target = static_cast<std::uint8_t*>(out.pixel_data) + (static_cast<std::ptrdiff_t>(row) * out.stride);
      for (int col = 0; col < layer.width; ++col) {
        const PremultipliedRgba c = unpack(pixel_at(pixels, col, row));
        // Luminosity: the group was composited over an opaque backdrop, so its colours are final.
        const float value = layer.mask_kind == SoftMaskKind::kLuminosity ? luminosity({c.r, c.g, c.b}) : c.a;
        const std::uint8_t level = to_byte(value);
        target[col] = layer.transfer ? (*layer.transfer)[level] : level;
      }
    }
    return mask;
  }

  struct Saved {
    PixelBox clip;
    std::shared_ptr<const SoftMask> soft_mask;
  };

  const DisplayList* list_;
  const RenderLimits* limits_;
  Matrix page_;
  int level_ = 0;
  std::vector<Surface> stack_;
  std::vector<Saved> saved_;         // per Save
  std::vector<bool> groups_;         // per open BeginGroup: whether it has its own layer
  std::vector<bool> blends_inside_;  // per BeginGroup of the list, in order (scan_groups)
  std::size_t group_ordinal_ = 0;    // the next BeginGroup's position in blends_inside_
  PixelBox clip_;                    // the device box everything is clipped to (rectangles and the boxes of other clips)
  std::shared_ptr<const SoftMask> soft_mask_;
  std::int64_t layer_pixels_ = 0;
  std::int64_t layer_budget_ = 0;
  std::map<std::pair<const ImagePixels*, int>, BLImage> pictures_;  // images ready to draw, per reduction
};

}  // namespace

Result<Bitmap> rasterize(const DisplayList& list, double scale, const RenderLimits& limits) {
  if (!(scale > 0) || !std::isfinite(scale)) {
    return make_error(ErrorCode::kOutOfRange, "the scale must be a positive number");
  }
  const OutputGeometry geometry = output_geometry(list, scale);
  const double w = std::ceil(geometry.width);
  const double h = std::ceil(geometry.height);
  if (std::isnan(w) || std::isnan(h) || w < 1 || h < 1 || w > limits.max_side || h > limits.max_side || w * h > static_cast<double>(limits.max_pixels)) {
    return make_error(ErrorCode::kLimitExceeded,
                      "the bitmap would be " + std::to_string(static_cast<std::int64_t>(w)) + "x" + std::to_string(static_cast<std::int64_t>(h)) + " pixels");
  }
  return Rasterizer(list, limits, geometry.page_to_output).run(static_cast<int>(w), static_cast<int>(h));
}

}  // namespace capriole::render
