#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>
#include <vector>

#include "blend2d/blend2d.h"
#include "raster_backend.hpp"

namespace capriole::experiments {

namespace {

using namespace capriole::graphics;

// Blend2D initializes its runtime from a static object in the library; a static link can leave that
// object out (it did in the WebAssembly build: the first image allocation crashed). This one is here.
const BLRuntimeInitializer runtime;

BLPath to_bl(const Path& path) {
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

BLMatrix2D to_bl(const Matrix& m) { return BLMatrix2D(m.a, m.b, m.c, m.d, m.e, m.f); }

BLRgba color_of(const DisplayList& list, ColorIndex index, double alpha) {
  const Rgb rgb = index < list.colors.size() ? list.colors[index].rgb : Rgb{};
  return BLRgba(float(rgb.r), float(rgb.g), float(rgb.b), float(std::clamp(alpha, 0.0, 1.0)));
}

// Blend2D clips to rectangles only. A clip path becomes a layer: its content is drawn on a transparent
// image the size of the path's bounding box, and composited through a mask of the path when the
// saved state that holds the clip is restored.
struct Surface {
  BLImage image;
  BLContext context;
  BLImage mask;  // empty for the page itself
  int x = 0;     // position on the page, in device pixels
  int y = 0;
  int width = 0;
  int height = 0;
  int level = 0;  // the save level that owns the clip
};

class Blend2DBackend final : public RasterBackend {
 public:
  [[nodiscard]] std::string name() const override { return "blend2d"; }

  [[nodiscard]] Bitmap render(const DisplayList& list, double scale) override {
    const OutputGeometry geometry = output_geometry(list, scale);
    const int w = std::max(1, int(std::ceil(geometry.width)));
    const int h = std::max(1, int(std::ceil(geometry.height)));
    page_ = geometry.page_to_output;
    list_ = &list;
    level_ = 0;
    stack_.clear();
    stack_.emplace_back();
    Surface& root = stack_.back();
    root.image.create(w, h, BL_FORMAT_PRGB32);
    root.context.begin(root.image);
    root.context.fill_all(BLRgba32(0xFFFFFFFFu));
    root.width = w;
    root.height = h;

    for (const Command& command : list.commands) {
      if (std::holds_alternative<Save>(command)) {
        ++level_;
        stack_.back().context.save();  // rectangular clips live in the context state
      } else if (std::holds_alternative<Restore>(command)) {
        while (stack_.size() > 1 && stack_.back().level == level_) {
          pop();
        }
        stack_.back().context.restore();
        level_ = std::max(0, level_ - 1);
      } else if (const auto* clip = std::get_if<ClipPath>(&command)) {
        if (!clip_to_rectangle(*clip)) {
          push_clip(*clip);
        }
      } else if (const auto* fill = std::get_if<FillPath>(&command)) {
        Surface& s = stack_.back();
        if (s.width == 0) {
          continue;
        }
        s.context.set_transform(to_bl(device(fill->ctm, s)));
        s.context.set_fill_rule(fill->rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
        s.context.fill_path(to_bl(fill->path), color_of(list, fill->color, fill->alpha));
      } else if (const auto* stroke = std::get_if<StrokePath>(&command)) {
        Surface& s = stack_.back();
        if (s.width == 0) {
          continue;
        }
        const StrokeStyle style = stroke->style < list.styles.size() ? list.styles[stroke->style] : StrokeStyle{};
        const Matrix d = device(stroke->ctm, s);
        s.context.set_transform(to_bl(d));
        s.context.set_stroke_width(stroke_width(style.width, stroke->ctm.then(page_)));
        s.context.set_stroke_caps(style.cap == LineCap::kRound    ? BL_STROKE_CAP_ROUND
                                  : style.cap == LineCap::kSquare ? BL_STROKE_CAP_SQUARE
                                                                  : BL_STROKE_CAP_BUTT);
        s.context.set_stroke_join(style.join == LineJoin::kRound   ? BL_STROKE_JOIN_ROUND
                                  : style.join == LineJoin::kBevel ? BL_STROKE_JOIN_BEVEL
                                                                   : BL_STROKE_JOIN_MITER_BEVEL);
        s.context.set_stroke_miter_limit(std::max(style.miter_limit, 1.0));
        BLArray<double> dash;
        for (const double v : dash_pattern(style.dash)) {
          dash.append(v);
        }
        s.context.set_stroke_dash_array(dash);
        s.context.set_stroke_dash_offset(style.dash_phase);
        s.context.stroke_path(to_bl(stroke->path), color_of(list, stroke->color, stroke->alpha));
      } else if (const auto* image = command_as<DrawImage>(command)) {
        Surface& s = stack_.back();
        if (s.width == 0) {
          continue;
        }
        s.context.set_transform(to_bl(device(image->ctm, s)));
        s.context.fill_rect(BLRect(0, 0, 1, 1), image->mask ? color_of(list, image->color, image->alpha * 0.35) : BLRgba(0.875f, 0.898f, 0.953f, 1.0f));
      } else if (const auto* text = command_as<ShowText>(command); text != nullptr && visible_text(*text)) {
        Surface& s = stack_.back();
        if (s.width == 0) {
          continue;
        }
        s.context.set_transform(to_bl(device(text_space(*text), s)));
        const BLRgba color = color_of(list, text->fill, text->alpha * 0.35);
        for (const GlyphBox& box : glyph_boxes(*text)) {
          s.context.fill_rect(BLRect(box.x, 0, box.width, 0.68), color);
        }
      }
    }
    while (stack_.size() > 1) {
      pop();
    }
    stack_.back().context.end();

    Bitmap out;
    out.width = w;
    out.height = h;
    out.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    BLImageData data{};
    stack_.back().image.get_data(&data);
    for (int row = 0; row < h; ++row) {
      const auto* src = static_cast<const std::uint32_t*>(data.pixel_data) + row * (data.stride / 4);
      for (int col = 0; col < w; ++col) {
        const std::uint32_t argb = src[col];  // premultiplied; the page is opaque white underneath
        std::uint8_t* dst = &out.rgba[(std::size_t(row) * std::size_t(w) + std::size_t(col)) * 4];
        dst[0] = std::uint8_t(argb >> 16);
        dst[1] = std::uint8_t(argb >> 8);
        dst[2] = std::uint8_t(argb);
        dst[3] = std::uint8_t(argb >> 24);
      }
    }
    return out;
  }

 private:
  // The matrix for drawing on surface s: user space → page → device, moved to the surface's origin.
  [[nodiscard]] Matrix device(const Matrix& ctm, const Surface& s) const { return ctm.then(page_).then(Matrix::translate(-s.x, -s.y)); }

  // The common case: a clip that is an axis-aligned rectangle on the device, which Blend2D clips to
  // natively (no layer). Returns false for any other shape.
  bool clip_to_rectangle(const ClipPath& clip) {
    std::vector<Point> corners;
    bool shape = true;
    clip.path.for_each([&](SegmentKind kind, const Point* p) {
      if (kind == SegmentKind::kCubic || (kind == SegmentKind::kMove && !corners.empty())) {
        shape = false;
      } else if (kind != SegmentKind::kClose) {
        corners.push_back(clip.ctm.then(page_).apply(p[0]));
      }
    });
    if (!shape || corners.size() < 4 || corners.size() > 5) {
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
    for (const Point& c : corners) {  // every corner on the bounding box's edges: axis-aligned
      const bool on_x = std::abs(c.x - x0) < kEpsilon || std::abs(c.x - x1) < kEpsilon;
      const bool on_y = std::abs(c.y - y0) < kEpsilon || std::abs(c.y - y1) < kEpsilon;
      if (!on_x || !on_y) {
        return false;
      }
    }
    Surface& s = stack_.back();
    if (s.width == 0) {
      return true;
    }
    s.context.reset_transform();
    s.context.clip_to_rect(BLRect(x0 - s.x, y0 - s.y, x1 - x0, y1 - y0));
    ++rect_clips_;
    return true;
  }

  void push_clip(const ClipPath& clip) {
    ++mask_clips_;
    const Surface& parent = stack_.back();
    // The clip's bounding box in device pixels, within the parent surface.
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
    Surface layer;
    layer.level = level_;
    layer.x = std::max(parent.x, int(std::floor(x0)));
    layer.y = std::max(parent.y, int(std::floor(y0)));
    const int right = std::min(parent.x + parent.width, int(std::ceil(x1)));
    const int bottom = std::min(parent.y + parent.height, int(std::ceil(y1)));
    if (parent.width == 0 || right <= layer.x || bottom <= layer.y) {
      layer.width = 0;  // nothing can be drawn through this clip
      stack_.push_back(std::move(layer));
      return;
    }
    layer.width = right - layer.x;
    layer.height = bottom - layer.y;
    layer.image.create(layer.width, layer.height, BL_FORMAT_PRGB32);
    layer.context.begin(layer.image);
    layer.context.clear_all();
    layer.mask.create(layer.width, layer.height, BL_FORMAT_A8);
    BLContext mask(layer.mask);
    mask.clear_all();
    mask.set_transform(to_bl(m.then(Matrix::translate(-layer.x, -layer.y))));
    mask.set_fill_rule(clip.rule == FillRule::kEvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
    mask.fill_path(to_bl(clip.path), BLRgba32(0xFFFFFFFFu));
    mask.end();
    stack_.push_back(std::move(layer));
  }

  // Composites the top layer, through its mask, onto the surface below.
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
      auto* p = static_cast<std::uint32_t*>(pixels.pixel_data) + row * (pixels.stride / 4);
      const auto* a = static_cast<const std::uint8_t*>(mask.pixel_data) + row * mask.stride;
      for (int col = 0; col < layer.width; ++col) {
        const std::uint32_t m = a[col];
        if (m == 255) {
          continue;
        }
        const std::uint32_t v = p[col];
        const auto scale = [m](std::uint32_t c) { return (c * m + 127) / 255; };
        p[col] = (scale(v >> 24) << 24) | (scale((v >> 16) & 0xFF) << 16) | (scale((v >> 8) & 0xFF) << 8) | scale(v & 0xFF);
      }
    }
    Surface& below = stack_.back();
    if (below.width == 0) {
      return;
    }
    below.context.reset_transform();  // the transform of the last command drawn there does not apply
    below.context.blit_image(BLPointI(layer.x - below.x, layer.y - below.y), layer.image);
  }

  Matrix page_;
  const DisplayList* list_ = nullptr;
  int level_ = 0;
  std::vector<Surface> stack_;

 public:
  std::size_t rect_clips_ = 0;
  std::size_t mask_clips_ = 0;
};

}  // namespace

std::unique_ptr<RasterBackend> make_blend2d_backend() { return std::make_unique<Blend2DBackend>(); }

}  // namespace capriole::experiments
