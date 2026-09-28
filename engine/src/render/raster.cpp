#include "capriole/render/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "blend2d/blend2d.h"
#include "capriole/graphics/dash.hpp"

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

// A surface drawn on: the page, or a clip layer (content composited through its mask on restore).
struct Surface {
  BLImage image;
  BLContext context;
  BLImage mask;  // A8, empty for the page
  int x = 0;     // position on the page, in device pixels
  int y = 0;
  int width = 0;  // 0: nothing can be drawn through this clip
  int height = 0;
  int level = 0;  // the save level that owns the clip
};

class Rasterizer {
 public:
  Rasterizer(const DisplayList& list, const RenderLimits& limits, const Matrix& page) : list_(&list), limits_(&limits), page_(page) {}

  Result<Bitmap> run(int width, int height) {
    Surface root;
    if (root.image.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS || root.context.begin(root.image) != BL_SUCCESS) {
      return make_error(ErrorCode::kLimitExceeded, "cannot allocate a " + std::to_string(width) + "x" + std::to_string(height) + " bitmap");
    }
    root.context.fill_all(BLRgba32(0xFFFFFFFFU));
    root.width = width;
    root.height = height;
    stack_.push_back(std::move(root));

    for (const Command& command : list_->commands) {
      std::visit([this](const auto& c) { this->draw(c); }, command);
    }
    while (stack_.size() > 1) {
      pop();
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
      const auto* source = static_cast<const std::uint8_t*>(data.pixel_data) + (static_cast<std::ptrdiff_t>(row) * data.stride);
      std::uint8_t* target = &out.rgba[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4];
      for (int col = 0; col < width; ++col) {
        std::uint32_t argb = 0;  // PRGB32: 0xAARRGGBB, opaque here (the page is white underneath)
        std::memcpy(&argb, source + (static_cast<std::ptrdiff_t>(col) * 4), 4);
        target[(col * 4) + 0] = static_cast<std::uint8_t>(argb >> 16U);
        target[(col * 4) + 1] = static_cast<std::uint8_t>(argb >> 8U);
        target[(col * 4) + 2] = static_cast<std::uint8_t>(argb);
        target[(col * 4) + 3] = static_cast<std::uint8_t>(argb >> 24U);
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

  void draw(const Save& /*unused*/) {
    ++level_;
    stack_.back().context.save();  // rectangular clips live in the context's state
  }

  void draw(const Restore& /*unused*/) {
    while (stack_.size() > 1 && stack_.back().level == level_) {
      pop();
    }
    stack_.back().context.restore();
    level_ = std::max(0, level_ - 1);
  }

  void draw(const ClipPath& clip) {
    if (!clip_to_rectangle(clip)) {
      push_layer(clip);
    }
  }

  void draw(const FillPath& fill) {
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
    s.context.set_transform(to_blend2d(device(fill.ctm, s)));
    s.context.set_fill_rule(fill.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
    s.context.fill_path(to_blend2d(fill.path), color(fill.color, fill.alpha));
  }

  void draw(const StrokePath& stroke) {
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
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
    s.context.set_transform(to_blend2d(device(stroke.ctm, s)));
    s.context.set_stroke_width(width);
    s.context.set_stroke_caps(cap_of(style.cap));
    s.context.set_stroke_join(join_of(style.join));
    s.context.set_stroke_miter_limit(std::max(style.miter_limit, 1.0));
    s.context.stroke_path(to_blend2d(*path), color(stroke.color, stroke.alpha));
  }

  void draw(const Boxed<DrawImage>& boxed) {
    // A placeholder until the images step: the unit square of the image's matrix.
    const DrawImage& image = *boxed;
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
    s.context.set_transform(to_blend2d(device(image.ctm, s)));
    s.context.fill_rect(BLRect(0, 0, 1, 1), image.mask ? color(image.color, image.alpha * 0.35) : BLRgba(0.875F, 0.898F, 0.953F, 1.0F));
  }

  void draw(const Boxed<PaintShading>& /*unused*/) {}

  void draw(const Boxed<ShowText>& boxed) {
    // The glyph boxes until the fonts step, as in the SVG preview.
    const ShowText& run = *boxed;
    Surface& s = stack_.back();
    const double h = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
    const double unit = run.font_size * h;
    if (s.width == 0 || run.render_mode == 3 || run.render_mode == 7 || unit == 0 || run.glyphs.empty()) {
      return;
    }
    const Matrix element = Matrix{unit, 0, 0, run.font_size, 0, run.glyphs.front().origin.y}.then(run.text_matrix);
    s.context.set_transform(to_blend2d(device(element, s)));
    const BLRgba paint = color(run.fill, run.alpha * 0.35);
    for (const Glyph& glyph : run.glyphs) {
      s.context.fill_rect(BLRect(glyph.origin.x / unit, 0, std::max(glyph.advance / unit * 0.85, 0.05), 0.68), paint);
    }
  }

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
    Surface& s = stack_.back();
    if (s.width == 0) {
      return;
    }
    s.context.reset_transform();
    s.context.clip_to_rect(BLRect(x0 - s.x, y0 - s.y, x1 - x0, y1 - y0));
  }

  // Any other clip: a layer the size of the clip's box, with the clip as its mask.
  void push_layer(const ClipPath& clip) {
    const Matrix m = clip.ctm.then(page_);
    double x0 = std::numeric_limits<double>::max();
    double y0 = x0;
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = x1;
    clip.path.for_each([&](SegmentKind kind, const Point* p) {
      for (std::size_t i = 0; i < Path::points_of(kind); ++i) {
        const Point q = m.apply(p[i]);
        x0 = std::min(x0, q.x);
        y0 = std::min(y0, q.y);
        x1 = std::max(x1, q.x);
        y1 = std::max(y1, q.y);
      }
    });
    if (stack_.size() > limits_->max_layers) {
      clip_box(x0, y0, x1, y1);  // too many clips alive: their box is the best that can be done
      return;
    }
    const Surface& parent = stack_.back();
    Surface layer;
    layer.level = level_;
    const bool bounded = x0 <= x1 && std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1);
    layer.x = bounded ? std::max(parent.x, static_cast<int>(std::floor(std::max(x0, -1e9)))) : parent.x;
    layer.y = bounded ? std::max(parent.y, static_cast<int>(std::floor(std::max(y0, -1e9)))) : parent.y;
    const int right = bounded ? std::min(parent.x + parent.width, static_cast<int>(std::ceil(std::min(x1, 1e9)))) : parent.x;
    const int bottom = bounded ? std::min(parent.y + parent.height, static_cast<int>(std::ceil(std::min(y1, 1e9)))) : parent.y;
    if (parent.width == 0 || right <= layer.x || bottom <= layer.y) {
      stack_.push_back(std::move(layer));  // an empty clip: nothing inside is drawn
      return;
    }
    layer.width = right - layer.x;
    layer.height = bottom - layer.y;
    BLContext mask;
    if (layer.image.create(layer.width, layer.height, BL_FORMAT_PRGB32) != BL_SUCCESS || layer.context.begin(layer.image) != BL_SUCCESS ||
        layer.mask.create(layer.width, layer.height, BL_FORMAT_A8) != BL_SUCCESS || mask.begin(layer.mask) != BL_SUCCESS) {
      clip_box(x0, y0, x1, y1);  // no memory for the layer: clip to its box instead
      return;
    }
    layer.context.clear_all();
    mask.clear_all();
    mask.set_transform(to_blend2d(m.then(Matrix::translate(-layer.x, -layer.y))));
    mask.set_fill_rule(clip.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
    mask.fill_path(to_blend2d(clip.path), BLRgba32(0xFFFFFFFFU));
    mask.end();
    stack_.push_back(std::move(layer));
  }

  // The top layer, through its mask, onto the surface below.
  void pop() {
    Surface layer = std::move(stack_.back());
    stack_.pop_back();
    if (layer.width == 0) {
      return;
    }
    layer.context.end();
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
    Surface& below = stack_.back();
    if (below.width == 0) {
      return;
    }
    below.context.reset_transform();
    below.context.blit_image(BLPointI(layer.x - below.x, layer.y - below.y), layer.image);
  }

  const DisplayList* list_;
  const RenderLimits* limits_;
  Matrix page_;
  int level_ = 0;
  std::vector<Surface> stack_;
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
