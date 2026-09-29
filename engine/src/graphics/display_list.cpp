#include "capriole/graphics/display_list.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace capriole::graphics {

namespace {

// In BlendMode order.
constexpr std::array<std::string_view, 16> kBlendNames = {"Normal",     "Multiply",   "Screen",    "Overlay",   "Darken",     "Lighten",
                                                          "ColorDodge", "ColorBurn",  "HardLight", "SoftLight", "Difference", "Exclusion",
                                                          "Hue",        "Saturation", "Color",     "Luminosity"};

template <typename... F>
struct Overloaded : F... {
  using F::operator()...;
};

}  // namespace

std::optional<BlendMode> blend_mode_named(std::string_view name) noexcept {
  if (name == "Compatible") {
    return BlendMode::kNormal;
  }
  for (std::size_t i = 0; i < kBlendNames.size(); ++i) {
    if (kBlendNames.at(i) == name) {
      return static_cast<BlendMode>(i);
    }
  }
  return std::nullopt;
}

std::string_view blend_mode_name(BlendMode mode) noexcept {
  const auto i = static_cast<std::size_t>(std::to_underlying(mode));
  return i < kBlendNames.size() ? kBlendNames.at(i) : "Normal";
}

std::string_view command_kind(const Command& command) {
  return std::visit(Overloaded{
                        [](const Save&) -> std::string_view { return "save"; },
                        [](const Restore&) -> std::string_view { return "restore"; },
                        [](const ClipPath&) -> std::string_view { return "clip"; },
                        [](const FillPath&) -> std::string_view { return "fill"; },
                        [](const StrokePath&) -> std::string_view { return "stroke"; },
                        [](const Boxed<DrawImage>&) -> std::string_view { return "image"; },
                        [](const Boxed<PaintShading>&) -> std::string_view { return "shading"; },
                        [](const Boxed<ShowText>&) -> std::string_view { return "text"; },
                        [](const BeginGroup&) -> std::string_view { return "group"; },
                        [](const EndGroup&) -> std::string_view { return "group_end"; },
                        [](const BeginSoftMask&) -> std::string_view { return "soft_mask"; },
                        [](const EndSoftMask&) -> std::string_view { return "soft_mask_end"; },
                        [](const ClearSoftMask&) -> std::string_view { return "soft_mask_clear"; },
                    },
                    command);
}

Path glyph_run_path(const ShowText& run) {
  Path out;
  const double h = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
  for (const Glyph& glyph : run.glyphs) {
    if (!glyph.outline) {
      continue;
    }
    const Matrix m = Matrix{run.font_size * h, 0, 0, run.font_size, glyph.origin.x, glyph.origin.y}.then(run.tm);
    glyph.outline->for_each([&](SegmentKind kind, const Point* p) {
      switch (kind) {
        case SegmentKind::kMove:
          out.move_to(m.apply(p[0]));
          break;
        case SegmentKind::kLine:
          out.line_to(m.apply(p[0]));
          break;
        case SegmentKind::kCubic:
          out.cubic_to(m.apply(p[0]), m.apply(p[1]), m.apply(p[2]));
          break;
        case SegmentKind::kClose:
          out.close();
          break;
      }
    });
  }
  return out;
}

}  // namespace capriole::graphics
