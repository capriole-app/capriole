#pragma once

// PDF functions (ISO 32000-2 §7.10): sampled (type 0), exponential (2), stitching (3) and PostScript
// calculator (4). They map m inputs to n outputs and appear in soft mask transfer functions, the tint
// transforms of Separation and DeviceN colour spaces, and shadings.
//
// Everything comes from the file, so everything is bounded: the sample table, the nesting of
// stitching functions, the calculator program and its stack. A calculator program has no loops, so
// evaluating it takes at most one step per instruction. An evaluation that fails at run time (stack
// underflow, a wrong operand type) gives zeros clipped to the range, as viewers do.

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::content {

struct FunctionLimits {
  std::size_t max_inputs = 8;              // sampled functions interpolate over 2^m corners
  std::size_t max_outputs = 32;            // DeviceN has at most 32 colorants
  std::size_t max_samples = 4'000'000;     // sample table entries (all outputs)
  std::size_t max_depth = 8;               // stitching functions inside stitching functions
  std::size_t max_program_size = 100'000;  // calculator instructions
  std::size_t max_stack = 100;             // calculator operand stack (§7.10.5.2)
};

class Function {
 public:
  struct Impl;

  Function() = default;
  explicit Function(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

  [[nodiscard]] std::size_t inputs() const noexcept;
  [[nodiscard]] std::size_t outputs() const noexcept;

  // Inputs are clipped to the domain (missing ones read as the domain's start), outputs to the range.
  [[nodiscard]] std::vector<double> evaluate(std::span<const double> in) const;

 private:
  std::shared_ptr<const Impl> impl_;
};

// A function from its dictionary or stream (types 0 and 4 are streams). kSyntax for a malformed one,
// kLimitExceeded past the limits, kUnsupported for an unknown type.
[[nodiscard]] Result<Function> load_function(Document& document, const model::Object& object, const FunctionLimits& limits = {});

// A type 4 function from its program text ("{ ... }"), for tests and fuzzing: domain and range as
// [min0 max0 min1 max1 ...].
[[nodiscard]] Result<Function> calculator_function(std::string_view program, std::vector<double> domain, std::vector<double> range,
                                                   const FunctionLimits& limits = {});

}  // namespace capriole::content
