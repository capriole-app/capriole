#pragma once

// JPXDecode (ISO 32000-2 §7.4.9): JPEG 2000 images — a JP2 file or a bare codestream —, decoded with
// OpenJPEG (ADR 0004). Unlike the other filters, the data describes itself: size, components, colour
// space and alpha come from it, and the image dictionary may leave /ColorSpace and /BitsPerComponent
// out. So the result is an image, not only bytes.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::filters {

struct JpxImage {
  enum class Space : std::uint8_t { kUnknown, kGray, kRgb, kCmyk };
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t components = 0;        // colour components and the alpha channel, if any, last
  bool has_alpha = false;             // the last component is an opacity channel (JP2 channel definitions)
  Space space = Space::kUnknown;      // from the file; YCC data is converted to RGB
  std::uint32_t reduction = 0;        // resolution levels dropped to stay within max_pixels (0: full size)
  std::vector<std::uint8_t> samples;  // 8 bits per component, interleaved, rows top to bottom
};

// kLimitExceeded when even the smallest resolution the codestream offers exceeds max_pixels;
// kCorruptData when OpenJPEG cannot decode it.
[[nodiscard]] Result<JpxImage> jpx_decode(std::span<const std::byte> data, std::int64_t max_pixels);

}  // namespace capriole::filters
