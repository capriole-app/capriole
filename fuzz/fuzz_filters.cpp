// Fuzz target for the stream filters: FlateDecode, LZWDecode (both followed by the PNG/TIFF
// predictors), ASCIIHexDecode, ASCII85Decode and RunLengthDecode. The first four bytes choose the
// filter and the predictor parameters; the rest is the encoded data. The seed corpus is
// fuzz/corpus/filters/.
//
// Invariant beyond "no crash, no UB": no decoder ever returns more than its output limit.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/filters/basic.hpp"
#include "capriole/filters/flate.hpp"

namespace {

constexpr std::size_t kMaxOutput = std::size_t{1} << 20U;

void check_size(const capriole::Result<std::vector<std::byte>>& result) {
  if (result && result->size() > kMaxOutput) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) {
    return 0;
  }
  constexpr std::array kPredictors = {1, 2, 10, 11, 12, 13, 14, 15};
  constexpr std::array kBits = {1, 2, 4, 8, 16};
  const capriole::filters::PredictorParams params{
      .predictor = kPredictors[data[0] % 8],
      .colors = 1 + (data[1] % 4),
      .bits_per_component = kBits[data[2] % 5],
      .columns = 1 + data[3],
  };
  const auto payload = std::as_bytes(std::span(data + 4, size - 4));

  switch ((data[0] >> 3U) % 6) {
    case 0: {
      // Predictors on raw bytes: random data is rarely valid zlib, so reach them directly too.
      check_size(capriole::filters::apply_predictor(std::vector<std::byte>(payload.begin(), payload.end()), params));
      break;
    }
    case 1: {
      auto inflated = capriole::filters::flate_decode(payload, kMaxOutput);
      check_size(inflated);
      if (inflated) {
        check_size(capriole::filters::apply_predictor(std::move(*inflated), params));
      }
      break;
    }
    case 2: {
      auto expanded = capriole::filters::lzw_decode(payload, (data[1] & 0x80U) == 0, kMaxOutput);
      check_size(expanded);
      if (expanded) {
        check_size(capriole::filters::apply_predictor(std::move(*expanded), params));
      }
      break;
    }
    case 3:
      check_size(capriole::filters::ascii_hex_decode(payload, kMaxOutput));
      break;
    case 4:
      check_size(capriole::filters::ascii85_decode(payload, kMaxOutput));
      break;
    default:
      check_size(capriole::filters::run_length_decode(payload, kMaxOutput));
      break;
  }
  return 0;
}
