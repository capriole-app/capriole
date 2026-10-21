#include "jbig2_generic.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace capriole::filters::jbig2 {

namespace {

// The context of a pixel is read from bit windows that slide along the row being decoded and the two
// above it: a 600 dpi scan is 30 million pixels a page, and gathering each pixel's 10 to 16 neighbours
// from the bitmap one by one made decoding it take half a second. Above, bit j of a window is the pixel
// at x + kAhead - j; on the row being decoded, the pixel at x - 1 - j.
constexpr std::int64_t kAhead = 8;
constexpr std::int64_t kWindow = 32;

enum class Source : std::uint8_t { kRow0, kRow1, kRow2, kBitmap };

// An adaptive template pixel (6.2.5.4): from a window when it falls inside one (the nominal positions
// do), else from the bitmap (wherever a hostile stream puts it).
struct AtPixel {
  Source source = Source::kBitmap;
  unsigned shift = 0;
  std::int32_t dx = 0;
  std::int32_t dy = 0;
  unsigned bit = 0;  // its place in the context
};

AtPixel at_pixel(std::int32_t dx, std::int32_t dy, unsigned bit) {
  if (dy == 0 && dx <= -1 && dx >= -kWindow) {
    return {.source = Source::kRow0, .shift = static_cast<unsigned>(-1 - dx), .dx = dx, .dy = dy, .bit = bit};
  }
  if ((dy == -1 || dy == -2) && dx <= kAhead && dx > kAhead - kWindow) {
    return {.source = dy == -1 ? Source::kRow1 : Source::kRow2, .shift = static_cast<unsigned>(kAhead - dx), .dx = dx, .dy = dy, .bit = bit};
  }
  return {.source = Source::kBitmap, .shift = 0, .dx = dx, .dy = dy, .bit = bit};
}

// The pixels of the template that are not adaptive, from the windows (the bit order of 6.2.5.3, which
// TPGDON's pseudo-pixel context relies on).
std::uint32_t fixed_context(unsigned tmpl, std::uint32_t row0, std::uint32_t row1, std::uint32_t row2) {
  switch (tmpl) {
    case 0:  // x-1..x-4; x+2..x-2 above; x+1..x-1 two above
      return (row0 & 0xFU) | (((row1 >> (kAhead - 2)) & 0x1FU) << 5U) | (((row2 >> (kAhead - 1)) & 0x7U) << 12U);
    case 1:  // x-1..x-3; x+2..x-2 above; x+2..x-1 two above
      return (row0 & 0x7U) | (((row1 >> (kAhead - 2)) & 0x1FU) << 4U) | (((row2 >> (kAhead - 2)) & 0xFU) << 9U);
    case 2:  // x-1..x-2; x+1..x-2 above; x+1..x-1 two above
      return (row0 & 0x3U) | (((row1 >> (kAhead - 1)) & 0xFU) << 3U) | (((row2 >> (kAhead - 1)) & 0x7U) << 7U);
    default:  // x-1..x-4; x+1..x-3 above
      return (row0 & 0xFU) | (((row1 >> (kAhead - 1)) & 0x1FU) << 5U);
  }
}

}  // namespace

std::size_t generic_contexts(unsigned tmpl) {
  switch (tmpl) {
    case 0:
      return std::size_t{1} << 16U;
    case 1:
      return std::size_t{1} << 13U;
    default:
      return std::size_t{1} << 10U;
  }
}

void decode_generic(ArithmeticDecoder& decoder, Contexts& contexts, Bitmap& b, const GenericParams& p) {
  const auto width = static_cast<std::size_t>(std::max(b.width, 0));
  const auto height = static_cast<std::size_t>(std::max(b.height, 0));
  if (width == 0 || height == 0 || b.px.size() != width * height) {
    return;
  }
  // The pseudo-pixel of TPGDON: "this row is the same as the one above" (6.2.5.7).
  constexpr std::array<std::uint32_t, 4> kSltp = {0x9B25, 0x0795, 0x00E5, 0x0195};
  // Where each template puts its adaptive pixels in the context: A1..A4 for template 0, A1 for the others.
  const auto& a = p.at;
  std::array<AtPixel, 4> at{};
  std::size_t at_count = 1;
  switch (p.tmpl) {
    case 0:
      at = {at_pixel(a[0], a[1], 4), at_pixel(a[2], a[3], 10), at_pixel(a[4], a[5], 11), at_pixel(a[6], a[7], 15)};
      at_count = 4;
      break;
    case 1:
      at[0] = at_pixel(a[0], a[1], 3);
      break;
    case 2:
      at[0] = at_pixel(a[0], a[1], 2);
      break;
    default:
      at[0] = at_pixel(a[0], a[1], 4);
      break;
  }
  const std::span<const AtPixel> adaptive(at.data(), at_count);
  const std::span<std::uint8_t> px(b.px);
  bool ltp = false;
  for (std::size_t y = 0; y < height && !decoder.exhausted(); ++y) {
    const std::span<std::uint8_t> row = px.subspan(y * width, width);
    if (p.tpgdon) {
      ltp = ltp != (decoder.decode(contexts, kSltp.at(p.tmpl)) != 0);
      if (ltp) {
        if (y == 0) {
          std::ranges::fill(row, std::uint8_t{0});
        } else {
          std::ranges::copy(px.subspan((y - 1) * width, width), row.begin());
        }
        continue;
      }
    }
    const std::span<const std::uint8_t> above = y >= 1 ? px.subspan((y - 1) * width, width) : std::span<const std::uint8_t>{};
    const std::span<const std::uint8_t> above2 = y >= 2 ? px.subspan((y - 2) * width, width) : std::span<const std::uint8_t>{};
    const auto pixel = [width](std::span<const std::uint8_t> r, std::int64_t x) -> std::uint32_t {
      return !r.empty() && x >= 0 && std::cmp_less(x, width) ? r[static_cast<std::size_t>(x)] : 0U;
    };
    std::uint32_t row0 = 0;  // left of x on this row: none yet
    std::uint32_t row1 = 0;
    std::uint32_t row2 = 0;
    for (std::int64_t j = 0; j < kWindow; ++j) {
      row1 |= pixel(above, kAhead - j) << static_cast<unsigned>(j);
      row2 |= pixel(above2, kAhead - j) << static_cast<unsigned>(j);
    }
    for (std::size_t x = 0; x < width; ++x) {
      if (p.skip == nullptr || p.skip->get(static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)) == 0) {
        std::uint32_t cx = fixed_context(p.tmpl, row0, row1, row2);
        for (const AtPixel& t : adaptive) {
          std::uint32_t v = 0;
          switch (t.source) {
            case Source::kRow0:
              v = (row0 >> t.shift) & 1U;
              break;
            case Source::kRow1:
              v = (row1 >> t.shift) & 1U;
              break;
            case Source::kRow2:
              v = (row2 >> t.shift) & 1U;
              break;
            case Source::kBitmap:
              v = b.get(static_cast<std::int64_t>(x) + t.dx, static_cast<std::int64_t>(y) + t.dy);
              break;
          }
          cx |= v << t.bit;
        }
        row[x] = static_cast<std::uint8_t>(decoder.decode(contexts, cx));
      }
      const auto next = static_cast<std::int64_t>(x) + 1 + kAhead;
      row0 = (row0 << 1U) | row[x];
      row1 = (row1 << 1U) | pixel(above, next);
      row2 = (row2 << 1U) | pixel(above2, next);
    }
  }
}

}  // namespace capriole::filters::jbig2
