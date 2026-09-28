// Fuzz target for CCITTFaxDecode (filters/ccitt): the first 4 bytes choose the parameters — K (Group 4,
// Group 3 1-D or mixed), /EndOfLine, /EncodedByteAlign, /BlackIs1, /Columns (1..1024) and /Rows (0 or
// up to 255) —, the rest is the fax data. The seed corpus is fuzz/corpus/ccitt/.
//
// Invariants beyond "no crash, no UB, no hang": the output stays within its limit and is a whole number
// of rows; with /Rows, exactly that many.

#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/filters/ccitt.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

constexpr std::size_t kMaxOutput = std::size_t{1} << 22U;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) {
    return 0;
  }
  capriole::filters::CcittParams p;
  p.k = static_cast<std::int64_t>(data[0] % 3) - 1;  // -1, 0, 1
  if (p.k > 0) {
    p.k = 1 + (data[0] >> 4U);
  }
  p.end_of_line = (data[1] & 1U) != 0;
  p.encoded_byte_align = (data[1] & 2U) != 0;
  p.black_is_1 = (data[1] & 4U) != 0;
  p.columns = 1 + ((static_cast<std::int64_t>(data[2]) << 2U) | (data[1] >> 6U));
  p.rows = data[3];
  auto decoded = capriole::filters::ccitt_decode(std::as_bytes(std::span(data + 4, size - 4)), p, kMaxOutput);
  if (decoded) {
    const auto row = static_cast<std::size_t>((p.columns + 7) / 8);
    check(decoded->size() <= kMaxOutput);
    check(decoded->size() % row == 0);
    check(p.rows == 0 || decoded->size() == row * static_cast<std::size_t>(p.rows));
  }
  return 0;
}
