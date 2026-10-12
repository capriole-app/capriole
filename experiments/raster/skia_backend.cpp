#include <algorithm>
#include <cmath>
#include <variant>

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkDashPathEffect.h"
#include "raster_backend.hpp"

namespace capriole::experiments {

namespace {

using namespace capriole::graphics;

SkMatrix to_sk(const Matrix& m) { return SkMatrix::MakeAll(float(m.a), float(m.c), float(m.e), float(m.b), float(m.d), float(m.f), 0, 0, 1); }

SkPath to_sk(const Path& path, FillRule rule) {
  SkPathBuilder b;
  b.setFillType(rule == FillRule::kEvenOdd ? SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
  path.for_each([&](SegmentKind kind, const Point* p) {
    switch (kind) {
      case SegmentKind::kMove:
        b.moveTo(float(p[0].x), float(p[0].y));
        break;
      case SegmentKind::kLine:
        b.lineTo(float(p[0].x), float(p[0].y));
        break;
      case SegmentKind::kCubic:
        b.cubicTo(float(p[0].x), float(p[0].y), float(p[1].x), float(p[1].y), float(p[2].x), float(p[2].y));
        break;
      case SegmentKind::kClose:
        b.close();
        break;
    }
  });
  return b.detach();
}

SkColor4f color_of(const DisplayList& list, ColorIndex index, double alpha) {
  const Rgb rgb = index < list.colors.size() ? list.colors[index].rgb : Rgb{};
  return {float(rgb.r), float(rgb.g), float(rgb.b), float(std::clamp(alpha, 0.0, 1.0))};
}

class SkiaBackend final : public RasterBackend {
 public:
  [[nodiscard]] std::string name() const override { return "skia"; }

  [[nodiscard]] Bitmap render(const DisplayList& list, double scale) override {
    const OutputGeometry geometry = output_geometry(list, scale);
    const int w = std::max(1, int(std::ceil(geometry.width)));
    const int h = std::max(1, int(std::ceil(geometry.height)));
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h));
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
    const Matrix page = geometry.page_to_output;

    for (const Command& command : list.commands) {
      if (std::holds_alternative<Save>(command)) {
        canvas->save();
      } else if (std::holds_alternative<Restore>(command)) {
        canvas->restore();
      } else if (const auto* clip = std::get_if<ClipPath>(&command)) {
        SkPath path = to_sk(clip->path, clip->rule);
        canvas->clipPath(path.makeTransform(to_sk(clip->ctm.then(page))), SkClipOp::kIntersect, true);
      } else if (const auto* fill = std::get_if<FillPath>(&command)) {
        SkPaint paint(color_of(list, fill->color, fill->alpha));
        paint.setAntiAlias(true);
        canvas->drawPath(to_sk(fill->path, fill->rule).makeTransform(to_sk(fill->ctm.then(page))), paint);
      } else if (const auto* stroke = std::get_if<StrokePath>(&command)) {
        const StrokeStyle style = stroke->style < list.styles.size() ? list.styles[stroke->style] : StrokeStyle{};
        const Matrix device = stroke->ctm.then(page);
        SkPaint paint(color_of(list, stroke->color, stroke->alpha));
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(float(stroke_width(style.width, device)));
        paint.setStrokeCap(style.cap == LineCap::kRound ? SkPaint::kRound_Cap : style.cap == LineCap::kSquare ? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
        paint.setStrokeJoin(style.join == LineJoin::kRound   ? SkPaint::kRound_Join
                            : style.join == LineJoin::kBevel ? SkPaint::kBevel_Join
                                                             : SkPaint::kMiter_Join);
        paint.setStrokeMiter(float(std::max(style.miter_limit, 1.0)));
        if (const auto dash = dash_pattern(style.dash); !dash.empty()) {
          std::vector<float> intervals(dash.begin(), dash.end());
          paint.setPathEffect(SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals.data(), intervals.size()), float(style.dash_phase)));
        }
        canvas->save();
        canvas->concat(to_sk(device));
        canvas->drawPath(to_sk(stroke->path, FillRule::kNonZero), paint);
        canvas->restore();
      } else if (const auto* image = command_as<DrawImage>(command)) {
        SkPaint paint(image->mask ? color_of(list, image->color, image->alpha * 0.35) : SkColor4f{0.875f, 0.898f, 0.953f, 1});
        canvas->save();
        canvas->concat(to_sk(image->ctm.then(page)));
        canvas->drawRect(SkRect::MakeWH(1, 1), paint);
        canvas->restore();
      } else if (const auto* text = command_as<ShowText>(command); text != nullptr && visible_text(*text)) {
        SkPaint paint(color_of(list, text->fill, text->alpha * 0.35));
        paint.setAntiAlias(true);
        canvas->save();
        canvas->concat(to_sk(text_space(*text).then(page)));
        for (const GlyphBox& box : glyph_boxes(*text)) {
          canvas->drawRect(SkRect::MakeXYWH(float(box.x), 0, float(box.width), 0.68f), paint);
        }
        canvas->restore();
      }
    }

    Bitmap out;
    out.width = w;
    out.height = h;
    out.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    surface->readPixels(SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType), out.rgba.data(), std::size_t(w) * 4, 0, 0);
    return out;
  }
};

}  // namespace

std::unique_ptr<RasterBackend> make_skia_backend() { return std::make_unique<SkiaBackend>(); }

}  // namespace capriole::experiments
