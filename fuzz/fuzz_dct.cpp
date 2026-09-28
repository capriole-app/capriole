// Fuzz target for DCTDecode (JPEG through libjpeg-turbo and our bridge to it, filters/jpeg_shim.c):
// the input is the JPEG data; its last byte chooses /ColorTransform (absent, 0 or 1). The seed corpus is
// fuzz/corpus/dct/ (the fixtures of tests/data/jpeg).
//
// Invariant beyond "no crash, no UB, no hang": the output never exceeds its limit.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/filters/dct.hpp"
#include "capriole/model/object.hpp"

namespace {

constexpr std::size_t kMaxOutput = std::size_t{1} << 22U;

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  capriole::model::Dict parms;
  const int choice = data[size - 1] % 3;
  if (choice > 0) {
    parms.set("ColorTransform", capriole::model::Object(std::int64_t{choice - 1}));
  }
  auto decoded = capriole::filters::dct_decode(std::as_bytes(std::span(data, size)), choice > 0 ? &parms : nullptr, kMaxOutput);
  if (decoded) {
    check(decoded->size() <= kMaxOutput);
  }
  return 0;
}
