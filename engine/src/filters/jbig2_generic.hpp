#pragma once

// Internal to the JBIG2 decoder (filters/jbig2.cpp): its bitmaps and generic region decoding (ITU-T T.88
// 6.2), the coding of every scan, of every collective symbol bitmap, of every halftone gray-scale plane.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "jbig2_arith.hpp"

namespace capriole::filters::jbig2 {

enum class Combine : std::uint8_t { kOr, kAnd, kXor, kXnor, kReplace };

// One byte per pixel, 1 for black (JBIG2's convention); outside the bitmap, 0.
struct Bitmap {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<std::uint8_t> px;

  [[nodiscard]] std::uint8_t get(std::int64_t x, std::int64_t y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return 0;
    }
    return px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
  }
  void set(std::int64_t x, std::int64_t y, std::uint8_t v) {
    if (x >= 0 && y >= 0 && x < width && y < height) {
      px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)] = v;
    }
  }

  // Places src with its top-left corner at (x, y), combining (6.4.5, 7.4.1.5), clipped to this bitmap.
  void compose(const Bitmap& src, std::int64_t x, std::int64_t y, Combine op) {
    const std::int64_t x0 = std::max<std::int64_t>(0, x);
    const std::int64_t y0 = std::max<std::int64_t>(0, y);
    const std::int64_t x1 = std::min<std::int64_t>(width, x + src.width);
    const std::int64_t y1 = std::min<std::int64_t>(height, y + src.height);
    for (std::int64_t yy = y0; yy < y1; ++yy) {
      for (std::int64_t xx = x0; xx < x1; ++xx) {
        const std::uint8_t s = src.get(xx - x, yy - y);
        auto& d = px[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(xx)];
        switch (op) {
          case Combine::kOr:
            d = d | s;
            break;
          case Combine::kAnd:
            d = d & s;
            break;
          case Combine::kXor:
            d = d ^ s;
            break;
          case Combine::kXnor:
            d = (d ^ s) ^ 1U;
            break;
          case Combine::kReplace:
            d = s;
            break;
        }
      }
    }
  }
};

struct GenericParams {
  unsigned tmpl = 0;
  bool tpgdon = false;
  std::array<std::int32_t, 8> at{};  // (x, y) of A1..A4
  const Bitmap* skip = nullptr;      // USESKIP: pixels set here are 0, not decoded
};

// The number of contexts of a template (6.2.5.3: 16, 13, 10 or 10 pixels).
std::size_t generic_contexts(unsigned tmpl);

// Decodes b (allocated: width × height pixels) with arithmetic coding (6.2.5.7); the pixels USESKIP skips
// keep the value they have.
void decode_generic(ArithmeticDecoder& decoder, Contexts& contexts, Bitmap& b, const GenericParams& p);

}  // namespace capriole::filters::jbig2
