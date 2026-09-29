// Fuzz target for JPXDecode (filters/jpx: OpenJPEG and our handling of what it returns): the input is a
// JP2 file or a JPEG 2000 codestream. The seed corpus is fuzz/corpus/jpx/ (tests/data/jpx).
//
// Invariants beyond "no crash, no UB, no hang": the image stays within the pixel limit and its samples
// are exactly width × height × components.

#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/filters/jpx.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

constexpr std::int64_t kMaxPixels = 1'000'000;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  auto image = capriole::filters::jpx_decode(std::as_bytes(std::span(data, size)), kMaxPixels);
  if (image) {
    check(static_cast<std::int64_t>(image->width) * image->height <= kMaxPixels);
    check(image->samples.size() ==
          static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height) * static_cast<std::size_t>(image->components));
  }
  return 0;
}
