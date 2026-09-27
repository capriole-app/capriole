// Fuzz target for the content stream parser. The seed corpus is fuzz/corpus/content/.
//
// The input is parsed as a content stream in both modes. Invariants beyond "no crash, no UB":
//   - parsing terminates, with at most one operation per input byte;
//   - offsets increase and lie inside the input; inline image data is no larger than the input;
//   - an operation never carries more than kMaxOperands operands.

#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/content/content_parser.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::ParseLimits limits;
  limits.max_nesting_depth = 32;  // small enough for the fuzzer to reach it
  for (const auto mode : {capriole::parser::ParseMode::kStrict, capriole::parser::ParseMode::kTolerant}) {
    capriole::content::ContentParser parser(bytes, limits, mode);
    std::uint64_t previous = 0;
    bool first = true;
    for (std::size_t count = 0;; ++count) {
      check(count <= size);
      auto operation = parser.next();
      if (!operation || !*operation) {
        break;
      }
      const auto& op = **operation;
      check(op.offset < size && (first || op.offset > previous));
      check(op.operands.size() <= capriole::content::kMaxOperands);
      check(op.image == nullptr || op.image->data.size() <= size);
      previous = op.offset;
      first = false;
    }
  }
  return 0;
}
