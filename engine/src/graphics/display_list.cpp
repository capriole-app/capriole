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

}  // namespace capriole::graphics
