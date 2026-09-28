// Fuzz target for PDF functions: the input is a PostScript calculator program (type 4, the one
// function type with a parser of its own), compiled with two inputs and three outputs and evaluated
// at a few points. The seed corpus is fuzz/corpus/function/.
//
// Invariants beyond "no crash, no UB, no hang": the outputs are as many as the range says, finite and
// inside it.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "capriole/content/function.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view program(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  capriole::content::FunctionLimits limits;
  limits.max_program_size = 10'000;
  auto f = capriole::content::calculator_function(program, {0, 1, -10, 10}, {0, 1, -5, 5, 0, 100}, limits);
  if (!f) {
    return 0;
  }
  constexpr std::array<std::array<double, 2>, 4> kPoints = {{{0, 0}, {0.5, -3}, {1, 10}, {0.25, 7.5}}};
  for (const auto& point : kPoints) {
    const std::vector<double> y = f->evaluate(point);
    check(y.size() == 3);
    check(std::isfinite(y[0]) && y[0] >= 0 && y[0] <= 1);
    check(std::isfinite(y[1]) && y[1] >= -5 && y[1] <= 5);
    check(std::isfinite(y[2]) && y[2] >= 0 && y[2] <= 100);
  }
  return 0;
}
