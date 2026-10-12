#include "capriole/filters/dct.hpp"

#include <array>
#include <string>

#include "jpeg_shim.h"

namespace capriole::filters {

Result<std::vector<std::byte>> dct_decode(std::span<const std::byte> input, const model::Dict* parms, std::size_t max_output) {
  // /ColorTransform (Table 13): absent, the file's markers decide (as libjpeg reads them).
  int transform = -1;
  if (const model::Object* value = parms != nullptr ? parms->find("ColorTransform") : nullptr) {
    if (const auto integer = value->as_integer(); integer && (*integer == 0 || *integer == 1)) {
      transform = static_cast<int>(*integer);
    }
  }
  capriole_jpeg_image image{};
  std::array<char, 256> message{};
  // unsigned char and std::byte have the same representation.
  const auto* data = reinterpret_cast<const unsigned char*>(input.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  if (capriole_jpeg_decode(data, input.size(), transform, max_output, &image, message.data(), message.size()) != 0) {
    const std::string reason(message.data());
    return make_error(reason.find("too large") != std::string::npos ? ErrorCode::kLimitExceeded : ErrorCode::kCorruptData, "DCTDecode: " + reason);
  }
  const auto* first = reinterpret_cast<const std::byte*>(image.samples);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  std::vector<std::byte> out(first, first + image.size);
  capriole_jpeg_free(&image);
  return out;
}

}  // namespace capriole::filters
