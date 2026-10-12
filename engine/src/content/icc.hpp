#pragma once

// Internal to the interpreter: ICC colour management with Little CMS (ADR 0006). A transform from a
// profile — embedded in the PDF (ICCBased, an output intent), made from CalRGB/CalGray parameters, or
// the press profile of the resource pack — to sRGB, relative colorimetric with black point
// compensation, as Acrobat displays. Every profile is untrusted: each transform has its own Little CMS
// context with a memory budget.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "capriole/core/error.hpp"
#include "capriole/graphics/display_list.hpp"

namespace capriole::content::detail {

struct IccLimits {
  std::size_t max_memory = std::size_t{64} << 20U;  // per transform: the profile's tables and the pipeline
  std::size_t max_profile_bytes = std::size_t{32} << 20U;
};

class IccTransform {
 public:
  // components: 1 (gray), 3 (RGB, or Lab), 4 (CMYK), as the PDF says it uses the profile; the profile's
  // own colour space must agree.
  [[nodiscard]] static Result<std::shared_ptr<const IccTransform>> open(std::span<const std::byte> profile, std::size_t components,
                                                                        const IccLimits& limits = {});
  // A calibrated space (§8.6.5.2-3): white point, gamma per component (1 or 3), and for RGB the matrix
  // from linear components to XYZ.
  [[nodiscard]] static Result<std::shared_ptr<const IccTransform>> calibrated(std::array<double, 3> white, std::array<double, 3> gamma,
                                                                              const std::array<double, 9>* matrix);

  IccTransform(const IccTransform&) = delete;
  IccTransform& operator=(const IccTransform&) = delete;
  IccTransform(IccTransform&&) = delete;
  IccTransform& operator=(IccTransform&&) = delete;
  ~IccTransform();

  [[nodiscard]] std::size_t components() const noexcept { return components_; }
  // One colour, components in [0, 1].
  [[nodiscard]] graphics::Rgb convert(std::span<const double> in) const;
  // count pixels of 8-bit interleaved components to 8-bit RGB (3 bytes each): whole rows at once.
  void convert8(const std::uint8_t* in, std::uint8_t* out, std::size_t count) const;

 private:
  struct Impl;
  IccTransform(std::unique_ptr<Impl> impl, std::size_t components);
  std::unique_ptr<Impl> impl_;
  std::size_t components_;
};

}  // namespace capriole::content::detail
