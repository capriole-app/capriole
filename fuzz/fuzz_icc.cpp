// Fuzz target for ICC profiles (content/icc: Little CMS and our use of it): the input is a profile, tried
// as a gray, RGB or CMYK one (the first byte's value chooses), then used on a few colours and a row of
// pixels. The seed corpus is fuzz/corpus/icc/.
//
// Invariants beyond "no crash, no UB, no hang, no leak": colours come out in [0, 1].

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "content/icc.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 1) {
    return 0;
  }
  constexpr std::array<std::size_t, 3> kComponents = {1, 3, 4};
  const std::size_t n = kComponents.at(data[0] % kComponents.size());
  auto transform = capriole::content::detail::IccTransform::open(std::as_bytes(std::span(data + 1, size - 1)), n,
                                                                 {.max_memory = std::size_t{64} << 20U, .max_profile_bytes = std::size_t{4} << 20U});
  if (!transform) {
    return 0;
  }
  for (const double v : {0.0, 0.25, 1.0}) {
    const std::array<double, 4> in = {v, 1 - v, v, 0.5};
    const auto rgb = (*transform)->convert(std::span<const double>(in.data(), n));
    check(rgb.r >= 0 && rgb.r <= 1 && rgb.g >= 0 && rgb.g <= 1 && rgb.b >= 0 && rgb.b <= 1);
  }
  std::array<std::uint8_t, 16> row = {0, 64, 128, 255, 255, 128, 64, 0, 10, 20, 30, 40, 200, 150, 100, 50};
  std::array<std::uint8_t, 12> out{};
  (*transform)->convert8(row.data(), out.data(), 16 / n > 4 ? 4 : 16 / n);
  return 0;
}
