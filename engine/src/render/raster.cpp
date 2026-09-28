#include "capriole/render/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <variant>

#include "blend2d/blend2d.h"
#include "capriole/graphics/dash.hpp"
#include "capriole/render/blend.hpp"

namespace capriole::render {

namespace {

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

// A soft mask: coverage 0..255 over a box of the page; outside the box nothing is painted while it is
// in force (the clip at the time it was set contains everything painted under it).
struct SoftMask {
  BLImage a8;
  PixelBox box;
};

const BLRgba kTransparent(0, 0, 0, 0);

enum class SurfaceKind : std::uint8_t { kPage, kClip, kGroup, kSoftMask };

// A surface drawn on: the page, a clip layer (composited through its A8 mask when its state is
// restored), a transparency group (composited as one object at EndGroup), or the group of a soft mask
// (turned into a mask at EndSoftMask).
struct Surface {
  SurfaceKind kind = SurfaceKind::kPage;
  BLImage image;
  BLContext context;
  BLImage mask;  // kClip: A8
  int x = 0;     // position on the page, in device pixels
  int y = 0;
  int width = 0;  // 0: nothing can be drawn on it (an empty clip, or no memory)
  int height = 0;
  int level = 0;  // the save level it was made at
  // kGroup: how it is composited; kGroup and kSoftMask: the soft mask in force before it.
  double alpha = 1;
  BlendMode blend = BlendMode::kNormal;
  std::shared_ptr<const SoftMask> soft_mask;
  SoftMaskKind mask_kind = SoftMaskKind::kAlpha;

  [[nodiscard]] PixelBox box() const noexcept { return {.x0 = x, .y0 = y, .x1 = x + width, .y1 = y + height}; }
};

std::uint32_t pixel_at(const BLImageData& data, int col, int row) {
  std::uint32_t v = 0;
  std::memcpy(&v, static_cast<const std::uint8_t*>(data.pixel_data) + (static_cast<std::ptrdiff_t>(row) * data.stride) + (static_cast<std::ptrdiff_t>(col) * 4),
              4);
  return v;
}

PremultipliedRgba unpack(std::uint32_t argb) {
  constexpr float kUnit = 1.0F / 255;
  return {.r = static_cast<float>((argb >> 16U) & 0xFFU) * kUnit,
          .g = static_cast<float>((argb >> 8U) & 0xFFU) * kUnit,
          .b = static_cast<float>(argb & 0xFFU) * kUnit,
          .a = static_cast<float>(argb >> 24U) * kUnit};
}

std::uint32_t pack(PremultipliedRgba c) {
  const auto channel = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255)); };
  const std::uint32_t a = channel(c.a);
  // Premultiplied: no colour component above alpha.
  const auto colour = [&](float v) { return std::min(channel(v), a); };
  return (a << 24U) | (colour(c.r) << 16U) | (colour(c.g) << 8U) | colour(c.b);
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
  [[nodiscard]] Matrix device(const Matrix& ctm, const Surface& s) const { return ctm.then(page_).then(Matrix::translate(-s.x, -s.y)); }

  [[nodiscard]] BLRgba color(ColorIndex index, double alpha) const {
    const Rgb rgb = index < list_->colors.size() ? list_->colors[index].rgb : Rgb{};
    return {static_cast<float>(rgb.r), static_cast<float>(rgb.g), static_cast<float>(rgb.b), static_cast<float>(std::clamp(alpha, 0.0, 1.0))};
  }

  // ── The graphics state ────────────────────────────────────────────────────────────────────

  void draw(const Save& /*unused*/) {
    ++level_;
    saved_.push_back({.clip = clip_, .soft_mask = soft_mask_});
    stack_.back().context.save();  // rectangular clips live in the context's state
  }

  void draw(const Restore& /*unused*/) {
    while (stack_.size() > 1 && stack_.back().level == level_) {
      finish_top();
    }
    stack_.back().context.restore();
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

  // Paints through the blend mode and the soft mask in force. `draw(surface)` sets its transform for
  // the surface and paints; `box` bounds what it paints on the page. Straight onto the surface when
  // Blend2D can do it (a separable mode, no soft mask); otherwise onto a transparent layer of the box,
  // composited here.
  template <typename Draw>
  void paint(BlendMode blend, const PixelBox& box, const Draw& draw) {
    Surface& target = stack_.back();
    if (target.width == 0) {
      return;
    }
    if (!soft_mask_ && (blend == BlendMode::kNormal || (is_separable(blend) && native_blend_modes()))) {
      target.context.set_comp_op(comp_op_of(blend));
      draw(target);
      target.context.set_comp_op(BL_COMP_OP_SRC_OVER);
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
    composite(layer, target, blend, 1, soft_mask_.get());
    release(layer);
  }

  void draw(const FillPath& fill) {
    const Matrix m = fill.ctm.then(page_);
    paint(fill.blend, bounds_of(fill.path, m, 1), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(fill.ctm, s)));
      s.context.set_fill_rule(fill.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
      s.context.fill_path(to_blend2d(fill.path), color(fill.color, fill.alpha));
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
    paint(stroke.blend, bounds_of(*path, on_page, std::min(reach, 1e7)), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(ctm, s)));
      s.context.set_stroke_width(width);
      s.context.set_stroke_caps(cap_of(style.cap));
      s.context.set_stroke_join(join_of(style.join));
      s.context.set_stroke_miter_limit(std::max(style.miter_limit, 1.0));
      s.context.stroke_path(to_blend2d(*path), color(stroke.color, stroke.alpha));
    });
  }

  void draw(const Boxed<DrawImage>& boxed) {
    // A placeholder until the images step: the unit square of the image's matrix.
    const DrawImage& image = *boxed;
    paint(image.blend, bounds_of_rect(0, 0, 1, 1, image.ctm.then(page_)), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(image.ctm, s)));
      s.context.fill_rect(BLRect(0, 0, 1, 1), image.mask ? color(image.color, image.alpha * 0.35) : BLRgba(0.875F, 0.898F, 0.953F, 1.0F));
    });
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
    paint(run.blend, bounds_of_rect(x0, 0, x1, 0.68, element.then(page_)), [&](Surface& s) {
      s.context.set_transform(to_blend2d(device(element, s)));
      const BLRgba fill = color(run.fill, run.alpha * 0.35);
      for (const Glyph& glyph : run.glyphs) {
        s.context.fill_rect(BLRect(glyph.origin.x / unit, 0, std::max(glyph.advance / unit * 0.85, 0.05), 0.68), fill);
      }
    });
  }

  // ── Transparency groups and soft masks ─────────────────────────────────────────────────────

  void draw(const BeginGroup& group) {
    // A group composited plainly (alpha 1, Normal, no soft mask) and not isolated gives what drawing
    // its content straight onto the backdrop gives (§11.4.7): no layer needed.
    const bool plain = group.alpha >= 1 && group.blend == BlendMode::kNormal && !soft_mask_ && !group.isolated;
    Surface layer;
    layer.kind = SurfaceKind::kGroup;
    const PixelBox area = clip_.intersect(stack_.back().box());
    if (plain || stack_.back().width == 0 || area.empty() || stack_.size() > limits_->max_layers || !make_surface(layer, area, kTransparent)) {
      groups_.push_back(false);  // drawn straight onto what is below
      return;
    }
    groups_.push_back(true);
    layer.level = level_;
    layer.alpha = group.alpha;
    layer.blend = group.blend;
    layer.soft_mask = std::exchange(soft_mask_, nullptr);  // inside, no soft mask until one is set (§11.6.6)
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
    clip_ = clip_.intersect(pixels_of(x0, y0, x1, y1));
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
    s.context.reset_transform();
    s.context.clip_to_rect(BLRect(x0 - s.x, y0 - s.y, x1 - x0, y1 - y0));
  }

  // Any other clip: a layer the size of the clip's box, with the clip as its mask.
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
    const PixelBox area = box.intersect(stack_.back().box()).intersect(clip_);
    clip_ = clip_.intersect(box);
    if (stack_.back().width == 0 || area.empty()) {
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
    mask.set_transform(to_blend2d(m.then(Matrix::translate(-layer.x, -layer.y))));
    mask.set_fill_rule(clip.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
    mask.fill_path(to_blend2d(clip.path), BLRgba32(0xFFFFFFFFU));
    mask.end();
    stack_.push_back(std::move(layer));
  }

  // ── Layers ────────────────────────────────────────────────────────────────────────────────

  // A layer over `area` of the page, filled with `ground`; false (and nothing allocated) past the
  // layer budget or without memory.
  bool make_surface(Surface& layer, const PixelBox& area, const BLRgba& ground) {
    const std::int64_t pixels = static_cast<std::int64_t>(area.x1 - area.x0) * (area.y1 - area.y0);
    if (layer_pixels_ + pixels > layer_budget_) {
      return false;
    }
    layer.x = area.x0;
    layer.y = area.y0;
    layer.width = area.x1 - area.x0;
    layer.height = area.y1 - area.y0;
    if (layer.image.create(layer.width, layer.height, BL_FORMAT_PRGB32) != BL_SUCCESS || layer.context.begin(layer.image) != BL_SUCCESS) {
      layer.width = 0;
      return false;
    }
    layer.context.clear_all();  // a new image holds whatever the memory held
    if (ground.a > 0) {
      layer.context.fill_all(ground);
    }
    layer_pixels_ += pixels;
    return true;
  }

  void release(Surface& layer) {
    layer_pixels_ -= static_cast<std::int64_t>(layer.width) * layer.height;
    layer.width = 0;
  }

  // Composites `layer` onto `target` pixel by pixel (§11.3.6): the layer's alpha scaled by `alpha` and
  // by the soft mask, blended with `blend`. The result goes back through the target's context as a
  // copy, so that its rectangular clip still applies.
  static void composite(const Surface& layer, Surface& target, BlendMode blend, double alpha, const SoftMask* mask) {
    const PixelBox area = layer.box().intersect(target.box());
    if (area.empty() || target.width == 0) {
      return;
    }
    target.context.flush(BL_CONTEXT_FLUSH_SYNC);
    BLImageData below{};
    BLImageData above{};
    BLImageData coverage{};
    target.image.get_data(&below);
    layer.image.get_data(&above);
    if (mask != nullptr) {
      mask->a8.get_data(&coverage);
    }
    BLImage result;
    BLImageData out{};
    if (result.create(area.x1 - area.x0, area.y1 - area.y0, BL_FORMAT_PRGB32) != BL_SUCCESS || result.make_mutable(&out) != BL_SUCCESS) {
      return;
    }
    const auto global = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    for (int y = area.y0; y < area.y1; ++y) {
      auto* row = static_cast<std::uint8_t*>(out.pixel_data) + (static_cast<std::ptrdiff_t>(y - area.y0) * out.stride);
      for (int x = area.x0; x < area.x1; ++x) {
        const std::uint32_t backdrop = pixel_at(below, x - target.x, y - target.y);
        std::uint32_t source = pixel_at(above, x - layer.x, y - layer.y);
        float factor = global;
        if (mask != nullptr) {
          const bool inside = x >= mask->box.x0 && x < mask->box.x1 && y >= mask->box.y0 && y < mask->box.y1;
          factor *= inside ? static_cast<float>(static_cast<const std::uint8_t*>(
                                 coverage.pixel_data)[(static_cast<std::ptrdiff_t>(y - mask->box.y0) * coverage.stride) + (x - mask->box.x0)]) /
                                 255
                           : 0;
        }
        std::uint32_t mixed = backdrop;
        if ((source >> 24U) != 0 && factor > 0) {
          PremultipliedRgba s = unpack(source);
          s = {.r = s.r * factor, .g = s.g * factor, .b = s.b * factor, .a = s.a * factor};
          mixed = pack(render::composite(blend, unpack(backdrop), s));
        }
        source = mixed;
        std::memcpy(row + (static_cast<std::ptrdiff_t>(x - area.x0) * 4), &source, 4);
      }
    }
    target.context.save();
    target.context.reset_transform();
    target.context.set_comp_op(BL_COMP_OP_SRC_COPY);
    target.context.blit_image(BLPointI(area.x0 - target.x, area.y0 - target.y), result);
    target.context.restore();
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
    Surface& below = stack_.back();
    if (layer.kind == SurfaceKind::kGroup) {
      composite(layer, below, layer.blend, layer.alpha, layer.soft_mask.get());
      release(layer);
      return;
    }
    BLImageData pixels{};
    BLImageData mask{};
    layer.image.make_mutable(&pixels);
    layer.mask.get_data(&mask);
    for (int row = 0; row < layer.height; ++row) {
      auto* p = static_cast<std::uint8_t*>(pixels.pixel_data) + (static_cast<std::ptrdiff_t>(row) * pixels.stride);
      const auto* a = static_cast<const std::uint8_t*>(mask.pixel_data) + (static_cast<std::ptrdiff_t>(row) * mask.stride);
      for (int col = 0; col < layer.width * 4; ++col) {
        const unsigned m = a[col / 4];
        if (m != 255) {
          p[col] = static_cast<std::uint8_t>(((p[col] * m) + 127) / 255);  // premultiplied: every channel scales
        }
      }
    }
    release(layer);
    if (below.width == 0) {
      return;
    }
    below.context.save();
    below.context.reset_transform();
    below.context.set_comp_op(BL_COMP_OP_SRC_OVER);
    below.context.blit_image(BLPointI(layer.x - below.x, layer.y - below.y), layer.image);
    below.context.restore();
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
        target[col] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255));
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
  std::vector<Saved> saved_;  // per Save
  std::vector<bool> groups_;  // per open BeginGroup: whether it has its own layer
  PixelBox clip_;             // the device box everything is clipped to (rectangles and the boxes of other clips)
  std::shared_ptr<const SoftMask> soft_mask_;
  std::int64_t layer_pixels_ = 0;
  std::int64_t layer_budget_ = 0;
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
