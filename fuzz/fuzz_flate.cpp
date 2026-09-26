// Fuzz target for FlateDecode and the PNG/TIFF predictors. The first four bytes choose the
// predictor parameters; the rest is used both as zlib input and directly as predictor input (random
// bytes are rarely valid zlib, so the predictors would otherwise be reached too seldom).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/filters/flate.hpp"

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

  (void)capriole::filters::apply_predictor(std::vector<std::byte>(payload.begin(), payload.end()), params);

  auto inflated = capriole::filters::flate_decode(payload, 1U << 20U);
  if (inflated) {
    auto predicted = capriole::filters::apply_predictor(std::move(*inflated), params);
    if (predicted && predicted->size() > (1U << 20U)) {
      __builtin_trap();  // predictors never grow the data
    }
  }
  return 0;
}
