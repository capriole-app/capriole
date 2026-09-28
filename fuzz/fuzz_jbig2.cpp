// Fuzz target for JBIG2Decode (filters/jbig2): the input is an embedded JBIG2 stream; when its first
// byte is odd, the stream is split at the byte its second byte names, and the first part is passed as
// /JBIG2Globals. The seed corpus is fuzz/corpus/jbig2/ (the fixtures of tests/data/jbig2).
//
// Invariant beyond "no crash, no UB, no hang": the output never exceeds its limit.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/filters/jbig2.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

constexpr std::size_t kMaxOutput = std::size_t{1} << 18U;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  std::span<const std::byte> globals;
  std::span<const std::byte> stream = bytes;
  if (size > 2 && (data[0] & 1U) != 0) {
    const std::size_t split = std::min<std::size_t>(size - 2, data[1]);
    globals = bytes.subspan(2, split);
    stream = bytes.subspan(2 + split);
  }
  auto decoded = capriole::filters::jbig2_decode(stream, globals, kMaxOutput);
  if (decoded) {
    check(decoded->size() <= kMaxOutput);
  }
  return 0;
}
