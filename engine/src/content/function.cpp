#include "capriole/content/function.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace capriole::content {

namespace {

using model::Object;

double interpolate(double x, double x0, double x1, double y0, double y1) { return x1 == x0 ? y0 : y0 + ((x - x0) * (y1 - y0) / (x1 - x0)); }

double clip(double v, double a, double b) { return std::clamp(v, std::min(a, b), std::max(a, b)); }

// ── Types 0, 2, 3 ────────────────────────────────────────────────────────────────────────────

struct Sampled {
  std::vector<std::size_t> size;
  std::vector<double> encode;
  std::vector<double> decode;
  std::vector<float> samples;  // 0..1, the first input varying fastest, the outputs of a point together
};

struct Exponential {
  std::vector<double> c0;
  std::vector<double> c1;
  double exponent = 1;
};

struct Stitching {
  std::vector<Function> functions;
  std::vector<double> bounds;
  std::vector<double> encode;
};

// ── Type 4: the PostScript calculator (§7.10.5) ─────────────────────────────────────────────

enum class Op : std::uint8_t {
  kPush,
  kJumpUnless,  // pops a boolean; to target when it is false
  kJump,
  kAbs,
  kAdd,
  kAnd,
  kAtan,
  kBitshift,
  kCeiling,
  kCopy,
  kCos,
  kCvi,
  kCvr,
  kDiv,
  kDup,
  kEq,
  kExch,
  kExp,
  kFalse,
  kFloor,
  kGe,
  kGt,
  kIdiv,
  kIndex,
  kLe,
  kLn,
  kLog,
  kLt,
  kMod,
  kMul,
  kNe,
  kNeg,
  kNot,
  kOr,
  kPop,
  kRoll,
  kRound,
  kSin,
  kSqrt,
  kSub,
  kTrue,
  kTruncate,
  kXor,
};

struct Instruction {
  Op op = Op::kPush;
  double value = 0;  // kPush
  bool integer = false;
  std::size_t target = 0;  // jumps
};

struct Calculator {
  std::vector<Instruction> program;
};

std::optional<Op> operator_named(std::string_view name) {
  struct Entry {
    std::string_view name;
    Op op;
  };
  static constexpr std::array<Entry, 40> kOperators = {{
      {.name = "abs", .op = Op::kAbs},
      {.name = "add", .op = Op::kAdd},
      {.name = "and", .op = Op::kAnd},
      {.name = "atan", .op = Op::kAtan},
      {.name = "bitshift", .op = Op::kBitshift},
      {.name = "ceiling", .op = Op::kCeiling},
      {.name = "copy", .op = Op::kCopy},
      {.name = "cos", .op = Op::kCos},
      {.name = "cvi", .op = Op::kCvi},
      {.name = "cvr", .op = Op::kCvr},
      {.name = "div", .op = Op::kDiv},
      {.name = "dup", .op = Op::kDup},
      {.name = "eq", .op = Op::kEq},
      {.name = "exch", .op = Op::kExch},
      {.name = "exp", .op = Op::kExp},
      {.name = "false", .op = Op::kFalse},
      {.name = "floor", .op = Op::kFloor},
      {.name = "ge", .op = Op::kGe},
      {.name = "gt", .op = Op::kGt},
      {.name = "idiv", .op = Op::kIdiv},
      {.name = "index", .op = Op::kIndex},
      {.name = "le", .op = Op::kLe},
      {.name = "ln", .op = Op::kLn},
      {.name = "log", .op = Op::kLog},
      {.name = "lt", .op = Op::kLt},
      {.name = "mod", .op = Op::kMod},
      {.name = "mul", .op = Op::kMul},
      {.name = "ne", .op = Op::kNe},
      {.name = "neg", .op = Op::kNeg},
      {.name = "not", .op = Op::kNot},
      {.name = "or", .op = Op::kOr},
      {.name = "pop", .op = Op::kPop},
      {.name = "roll", .op = Op::kRoll},
      {.name = "round", .op = Op::kRound},
      {.name = "sin", .op = Op::kSin},
      {.name = "sqrt", .op = Op::kSqrt},
      {.name = "sub", .op = Op::kSub},
      {.name = "true", .op = Op::kTrue},
      {.name = "truncate", .op = Op::kTruncate},
      {.name = "xor", .op = Op::kXor},
  }};
  // A plain loop: std::array iterators are pointers in libstdc++ but not in libc++ (WebAssembly).
  for (const Entry& e : kOperators) {
    if (e.name == name) {
      return e.op;
    }
  }
  return std::nullopt;
}

// The program as a tree first: a procedure is only meaningful with the if/ifelse that follows it.
struct Node {
  enum class Kind : std::uint8_t { kNumber, kOperator, kBlock };
  Kind kind = Kind::kNumber;
  double number = 0;
  bool integer = false;
  std::string name;
  std::vector<Node> block;
};

class CalculatorParser {
 public:
  CalculatorParser(std::string_view text, const FunctionLimits& limits) : text_(text), limits_(&limits) {}

  Result<std::vector<Instruction>> parse() {
    skip_space();
    if (at_ >= text_.size() || text_[at_] != '{') {
      return make_error(ErrorCode::kSyntax, "calculator function does not start with '{'");
    }
    ++at_;
    auto nodes = block(0);
    if (!nodes) {
      return std::unexpected(std::move(nodes.error()));
    }
    std::vector<Instruction> program;
    if (auto compiled = compile(*nodes, program); !compiled) {
      return std::unexpected(std::move(compiled.error()));
    }
    return program;
  }

 private:
  static constexpr std::size_t kMaxNesting = 100;

  void skip_space() {
    while (at_ < text_.size()) {
      const char c = text_[at_];
      if (c == '%') {
        while (at_ < text_.size() && text_[at_] != '\n' && text_[at_] != '\r') {
          ++at_;
        }
      } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\0') {
        ++at_;
      } else {
        return;
      }
    }
  }

  // The inside of a procedure, after its '{', up to and including its '}'.
  Result<std::vector<Node>> block(std::size_t depth) {
    if (depth > kMaxNesting) {
      return make_error(ErrorCode::kLimitExceeded, "calculator procedures nested too deeply");
    }
    std::vector<Node> nodes;
    for (;;) {
      skip_space();
      if (at_ >= text_.size()) {
        return make_error(ErrorCode::kSyntax, "calculator function without its closing '}'");
      }
      if (++tokens_ > limits_->max_program_size) {
        return make_error(ErrorCode::kLimitExceeded, "calculator program too long");
      }
      const char c = text_[at_];
      if (c == '}') {
        ++at_;
        return nodes;
      }
      if (c == '{') {
        ++at_;
        auto inner = block(depth + 1);
        if (!inner) {
          return inner;
        }
        nodes.push_back(Node{.kind = Node::Kind::kBlock, .number = 0, .integer = false, .name = {}, .block = std::move(*inner)});
        continue;
      }
      const std::size_t start = at_;
      while (at_ < text_.size() && std::string_view(" \t\n\r\f{}%").find(text_[at_]) == std::string_view::npos && text_[at_] != '\0') {
        ++at_;
      }
      const std::string_view token = text_.substr(start, at_ - start);
      if (auto number = parse_number(token)) {
        nodes.push_back(Node{.kind = Node::Kind::kNumber, .number = number->first, .integer = number->second, .name = {}, .block = {}});
      } else {
        nodes.push_back(Node{.kind = Node::Kind::kOperator, .number = 0, .integer = false, .name = std::string(token), .block = {}});
      }
    }
  }

  // An integer or a real in PostScript syntax (radix numbers are not used in PDF functions).
  static std::optional<std::pair<double, bool>> parse_number(std::string_view token) {
    std::size_t i = 0;
    if (i < token.size() && (token[i] == '+' || token[i] == '-')) {
      ++i;
    }
    bool digits = false;
    bool point = false;
    bool exponent = false;
    for (; i < token.size(); ++i) {
      const char c = token[i];
      if (c >= '0' && c <= '9') {
        digits = true;
      } else if (c == '.' && !point && !exponent) {
        point = true;
      } else if ((c == 'e' || c == 'E') && digits && !exponent) {
        exponent = true;
        if (i + 1 < token.size() && (token[i + 1] == '+' || token[i + 1] == '-')) {
          ++i;
        }
      } else {
        return std::nullopt;
      }
    }
    if (!digits) {
      return std::nullopt;
    }
    const double value = std::strtod(std::string(token).c_str(), nullptr);
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
    return std::pair{value, !point && !exponent && std::abs(value) <= 2147483647.0};
  }

  Result<void> compile(const std::vector<Node>& nodes, std::vector<Instruction>& program) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const Node& node = nodes[i];
      if (program.size() > limits_->max_program_size) {
        return make_error(ErrorCode::kLimitExceeded, "calculator program too long");
      }
      switch (node.kind) {
        case Node::Kind::kNumber:
          program.push_back({.op = Op::kPush, .value = node.number, .integer = node.integer, .target = 0});
          break;
        case Node::Kind::kOperator: {
          const auto op = operator_named(node.name);
          if (!op) {
            return make_error(ErrorCode::kSyntax, "unknown or misplaced calculator operator '" + node.name.substr(0, 32) + "'");
          }
          program.push_back({.op = *op, .value = 0, .integer = false, .target = 0});
          break;
        }
        case Node::Kind::kBlock: {
          const auto is_operator = [&](std::size_t j, std::string_view name) {
            return j < nodes.size() && nodes[j].kind == Node::Kind::kOperator && nodes[j].name == name;
          };
          if (i + 2 < nodes.size() && nodes[i + 1].kind == Node::Kind::kBlock && is_operator(i + 2, "ifelse")) {
            const std::size_t test = program.size();
            program.push_back({.op = Op::kJumpUnless, .value = 0, .integer = false, .target = 0});
            if (auto r = compile(node.block, program); !r) {
              return r;
            }
            const std::size_t skip = program.size();
            program.push_back({.op = Op::kJump, .value = 0, .integer = false, .target = 0});
            program[test].target = program.size();
            if (auto r = compile(nodes[i + 1].block, program); !r) {
              return r;
            }
            program[skip].target = program.size();
            i += 2;
          } else if (is_operator(i + 1, "if")) {
            const std::size_t test = program.size();
            program.push_back({.op = Op::kJumpUnless, .value = 0, .integer = false, .target = 0});
            if (auto r = compile(node.block, program); !r) {
              return r;
            }
            program[test].target = program.size();
            i += 1;
          } else {
            return make_error(ErrorCode::kSyntax, "calculator procedure not followed by if or ifelse");
          }
          break;
        }
      }
    }
    return {};
  }

  std::string_view text_;
  const FunctionLimits* limits_;
  std::size_t at_ = 0;
  std::size_t tokens_ = 0;
};

// A value on the calculator's stack.
struct Value {
  enum class Type : std::uint8_t { kInteger, kReal, kBoolean };
  double v = 0;
  Type type = Type::kReal;
};

constexpr double kIntMax = 2147483647.0;
constexpr double kIntMin = -2147483648.0;

Value number(double v, bool integer) {
  // Integer results that overflow become reals, as in PostScript.
  const bool fits = integer && v >= kIntMin && v <= kIntMax;
  return {.v = v, .type = fits ? Value::Type::kInteger : Value::Type::kReal};
}

class Machine {
 public:
  Machine(const std::vector<Instruction>& program, std::size_t max_stack) : program_(&program), max_stack_(max_stack) {}

  // The outputs, or nullopt when the program fails.
  std::optional<std::vector<double>> run(std::span<const double> inputs, std::size_t outputs) {
    for (const double x : inputs) {
      if (!push({.v = x, .type = Value::Type::kReal})) {
        return std::nullopt;
      }
    }
    const auto& program = *program_;
    for (std::size_t pc = 0; pc < program.size();) {
      const Instruction& in = program[pc];
      ++pc;
      if (in.op == Op::kJump) {
        pc = in.target;
        continue;
      }
      if (in.op == Op::kJumpUnless) {
        const auto test = pop();
        if (!test || test->type != Value::Type::kBoolean) {
          return std::nullopt;
        }
        if (test->v == 0) {
          pc = in.target;
        }
        continue;
      }
      if (!step(in)) {
        return std::nullopt;
      }
    }
    if (stack_.size() < outputs) {
      return std::nullopt;
    }
    std::vector<double> out;
    for (std::size_t i = stack_.size() - outputs; i < stack_.size(); ++i) {
      out.push_back(stack_[i].v);
    }
    return out;
  }

 private:
  bool push(Value v) {
    if (stack_.size() >= max_stack_ || !std::isfinite(v.v)) {
      return false;
    }
    stack_.push_back(v);
    return true;
  }

  std::optional<Value> pop() {
    if (stack_.empty()) {
      return std::nullopt;
    }
    const Value v = stack_.back();
    stack_.pop_back();
    return v;
  }

  static bool is_number(const Value& v) { return v.type != Value::Type::kBoolean; }
  static bool is_integer(const Value& v) { return v.type == Value::Type::kInteger; }

  static double degrees(double radians) { return radians * 180 / std::numbers::pi; }

  bool step(const Instruction& in) {  // NOLINT(readability-function-cognitive-complexity): one case per operator
    using T = Value::Type;
    const auto real = [](double v) { return Value{.v = v, .type = T::kReal}; };
    const auto boolean = [](bool v) { return Value{.v = v ? 1.0 : 0.0, .type = T::kBoolean}; };
    switch (in.op) {
      case Op::kPush:
        return push(number(in.value, in.integer));
      case Op::kTrue:
        return push(boolean(true));
      case Op::kFalse:
        return push(boolean(false));
      case Op::kAbs:
      case Op::kNeg:
      case Op::kCeiling:
      case Op::kFloor:
      case Op::kRound:
      case Op::kTruncate:
      case Op::kSqrt:
      case Op::kSin:
      case Op::kCos:
      case Op::kLn:
      case Op::kLog:
      case Op::kCvi:
      case Op::kCvr: {
        const auto a = pop();
        if (!a || !is_number(*a)) {
          return false;
        }
        const bool integer = is_integer(*a);
        switch (in.op) {
          case Op::kAbs:
            return push(number(std::abs(a->v), integer));
          case Op::kNeg:
            return push(number(-a->v, integer));
          case Op::kCeiling:
            return push(number(std::ceil(a->v), integer));
          case Op::kFloor:
            return push(number(std::floor(a->v), integer));
          case Op::kRound:
            return push(number(std::floor(a->v + 0.5), integer));  // PostScript rounds halves up
          case Op::kTruncate:
            return push(number(std::trunc(a->v), integer));
          case Op::kSqrt:
            return a->v >= 0 && push(real(std::sqrt(a->v)));
          case Op::kSin:
            return push(real(std::sin(a->v * std::numbers::pi / 180)));
          case Op::kCos:
            return push(real(std::cos(a->v * std::numbers::pi / 180)));
          case Op::kLn:
            return a->v > 0 && push(real(std::log(a->v)));
          case Op::kLog:
            return a->v > 0 && push(real(std::log10(a->v)));
          case Op::kCvi:
            return std::trunc(a->v) >= kIntMin && std::trunc(a->v) <= kIntMax && push(number(std::trunc(a->v), true));
          default:  // kCvr
            return push(real(a->v));
        }
      }
      case Op::kAdd:
      case Op::kSub:
      case Op::kMul:
      case Op::kDiv:
      case Op::kIdiv:
      case Op::kMod:
      case Op::kAtan:
      case Op::kExp:
      case Op::kGe:
      case Op::kGt:
      case Op::kLe:
      case Op::kLt: {
        const auto b = pop();
        const auto a = pop();
        if (!a || !b || !is_number(*a) || !is_number(*b)) {
          return false;
        }
        const bool integers = is_integer(*a) && is_integer(*b);
        switch (in.op) {
          case Op::kAdd:
            return push(number(a->v + b->v, integers));
          case Op::kSub:
            return push(number(a->v - b->v, integers));
          case Op::kMul:
            return push(number(a->v * b->v, integers));
          case Op::kDiv:
            return b->v != 0 && push(real(a->v / b->v));
          case Op::kIdiv:
            return integers && b->v != 0 && push(number(std::trunc(a->v / b->v), true));
          case Op::kMod:
            return integers && b->v != 0 && push(number(std::fmod(a->v, b->v), true));
          case Op::kAtan: {
            if (a->v == 0 && b->v == 0) {
              return false;
            }
            double angle = degrees(std::atan2(a->v, b->v));
            if (angle < 0) {
              angle += 360;
            }
            return push(real(angle));
          }
          case Op::kExp:
            return push(real(std::pow(a->v, b->v)));
          case Op::kGe:
            return push(boolean(a->v >= b->v));
          case Op::kGt:
            return push(boolean(a->v > b->v));
          case Op::kLe:
            return push(boolean(a->v <= b->v));
          default:  // kLt
            return push(boolean(a->v < b->v));
        }
      }
      case Op::kEq:
      case Op::kNe: {
        const auto b = pop();
        const auto a = pop();
        if (!a || !b || (is_number(*a) != is_number(*b))) {
          return false;
        }
        return push(boolean((a->v == b->v) == (in.op == Op::kEq)));
      }
      case Op::kAnd:
      case Op::kOr:
      case Op::kXor:
      case Op::kBitshift: {
        const auto b = pop();
        const auto a = pop();
        if (!a || !b) {
          return false;
        }
        if (in.op != Op::kBitshift && a->type == T::kBoolean && b->type == T::kBoolean) {
          const bool x = a->v != 0;
          const bool y = b->v != 0;
          bool result = x != y;  // kXor
          if (in.op == Op::kAnd) {
            result = x && y;
          } else if (in.op == Op::kOr) {
            result = x || y;
          }
          return push(boolean(result));
        }
        if (!is_integer(*a) || !is_integer(*b)) {
          return false;
        }
        const auto x = static_cast<std::int32_t>(a->v);
        const auto y = static_cast<std::int32_t>(b->v);
        const auto ux = static_cast<std::uint32_t>(x);
        std::uint32_t result = 0;
        if (in.op == Op::kBitshift) {
          if (y >= 0 && y < 32) {
            result = ux << static_cast<unsigned>(y);
          } else if (y < 0 && y > -32) {
            result = ux >> static_cast<unsigned>(-y);
          }
        } else {
          const auto uy = static_cast<std::uint32_t>(y);
          result = ux ^ uy;  // kXor
          if (in.op == Op::kAnd) {
            result = ux & uy;
          } else if (in.op == Op::kOr) {
            result = ux | uy;
          }
        }
        return push(number(static_cast<double>(static_cast<std::int32_t>(result)), true));
      }
      case Op::kNot: {
        const auto a = pop();
        if (!a) {
          return false;
        }
        if (a->type == T::kBoolean) {
          return push(boolean(a->v == 0));
        }
        return is_integer(*a) && push(number(static_cast<double>(~static_cast<std::int32_t>(a->v)), true));
      }
      case Op::kDup:
        return !stack_.empty() && push(stack_.back());
      case Op::kPop:
        return pop().has_value();
      case Op::kExch: {
        if (stack_.size() < 2) {
          return false;
        }
        std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
        return true;
      }
      case Op::kCopy: {
        const auto n = pop();
        if (!n || !is_integer(*n) || n->v < 0 || n->v > static_cast<double>(stack_.size())) {
          return false;
        }
        const auto count = static_cast<std::size_t>(n->v);
        const std::size_t from = stack_.size() - count;
        for (std::size_t i = 0; i < count; ++i) {
          if (!push(stack_[from + i])) {
            return false;
          }
        }
        return true;
      }
      case Op::kIndex: {
        const auto n = pop();
        if (!n || !is_integer(*n) || n->v < 0 || n->v >= static_cast<double>(stack_.size())) {
          return false;
        }
        return push(stack_[stack_.size() - 1 - static_cast<std::size_t>(n->v)]);
      }
      case Op::kRoll: {
        const auto j = pop();
        const auto n = pop();
        if (!n || !j || !is_integer(*n) || !is_integer(*j) || n->v < 0 || n->v > static_cast<double>(stack_.size())) {
          return false;
        }
        const auto count = static_cast<std::ptrdiff_t>(n->v);
        if (count == 0) {
          return true;
        }
        // Positive j rolls towards the top: the top j elements move to the bottom of the n.
        const auto shift =
            static_cast<std::ptrdiff_t>(std::fmod(std::fmod(j->v, static_cast<double>(count)) + static_cast<double>(count), static_cast<double>(count)));
        const auto first = stack_.end() - count;
        std::rotate(first, stack_.end() - shift, stack_.end());
        return true;
      }
      case Op::kJump:
      case Op::kJumpUnless:
        return true;  // handled by run
    }
    return false;
  }

  const std::vector<Instruction>* program_;
  std::size_t max_stack_;
  std::vector<Value> stack_;
};

}  // namespace

struct Function::Impl {
  std::vector<double> domain;  // 2m
  std::vector<double> range;   // 2n, or empty (types 2 and 3 may leave it out)
  std::size_t m = 1;
  std::size_t n = 1;
  std::size_t max_stack = 100;
  std::variant<Sampled, Exponential, Stitching, Calculator> body;
};

std::size_t Function::inputs() const noexcept { return impl_ ? impl_->m : 0; }
std::size_t Function::outputs() const noexcept { return impl_ ? impl_->n : 0; }

std::vector<double> Function::evaluate(std::span<const double> in) const {
  if (!impl_) {
    return {};
  }
  const Impl& f = *impl_;
  std::vector<double> x(f.m);
  for (std::size_t i = 0; i < f.m; ++i) {
    const double lo = f.domain[2 * i];
    const double hi = f.domain[(2 * i) + 1];
    const double v = i < in.size() && std::isfinite(in[i]) ? in[i] : lo;
    x[i] = clip(v, lo, hi);
  }
  std::vector<double> y = std::visit(
      [&](const auto& body) -> std::vector<double> {
        using B = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<B, Sampled>) {
          const std::size_t m = f.m;
          std::vector<std::size_t> base(m);
          std::vector<double> t(m);
          std::vector<std::size_t> stride(m);
          std::size_t s = 1;
          for (std::size_t i = 0; i < m; ++i) {
            stride[i] = s;
            s *= body.size[i];
            const auto top = static_cast<double>(body.size[i] - 1);
            const double e = std::clamp(interpolate(x[i], f.domain[2 * i], f.domain[(2 * i) + 1], body.encode[2 * i], body.encode[(2 * i) + 1]), 0.0, top);
            const double floor = std::min(std::floor(e), std::max(top - 1, 0.0));
            base[i] = static_cast<std::size_t>(floor);
            t[i] = body.size[i] > 1 ? e - floor : 0;
          }
          std::vector<double> out(f.n, 0.0);
          for (std::size_t corner = 0; corner < (std::size_t{1} << m); ++corner) {
            double weight = 1;
            std::size_t index = 0;
            for (std::size_t i = 0; i < m; ++i) {
              const bool up = ((corner >> i) & 1U) != 0;
              weight *= up ? t[i] : 1 - t[i];
              index += std::min(base[i] + (up ? 1 : 0), body.size[i] - 1) * stride[i];
            }
            if (weight == 0) {
              continue;
            }
            for (std::size_t j = 0; j < f.n; ++j) {
              out[j] += weight * static_cast<double>(body.samples[(index * f.n) + j]);
            }
          }
          for (std::size_t j = 0; j < f.n; ++j) {
            out[j] = interpolate(out[j], 0, 1, body.decode[2 * j], body.decode[(2 * j) + 1]);
          }
          return out;
        } else if constexpr (std::is_same_v<B, Exponential>) {
          const double p = std::pow(x[0], body.exponent);
          std::vector<double> out(f.n);
          for (std::size_t j = 0; j < f.n; ++j) {
            out[j] = std::isfinite(p) ? body.c0[j] + (p * (body.c1[j] - body.c0[j])) : body.c0[j];
          }
          return out;
        } else if constexpr (std::is_same_v<B, Stitching>) {
          const std::size_t k = body.functions.size();
          std::size_t i = 0;
          while (i + 1 < k && x[0] >= body.bounds[i]) {
            ++i;
          }
          const double lo = i == 0 ? f.domain[0] : body.bounds[i - 1];
          const double hi = i + 1 == k ? f.domain[1] : body.bounds[i];
          const double e = interpolate(x[0], lo, hi, body.encode[2 * i], body.encode[(2 * i) + 1]);
          std::vector<double> out = body.functions[i].evaluate(std::span<const double>(&e, 1));
          out.resize(f.n, 0.0);
          return out;
        } else {
          Machine machine(body.program, f.max_stack);
          return machine.run(x, f.n).value_or(std::vector<double>(f.n, 0.0));
        }
      },
      f.body);
  y.resize(f.n, 0.0);
  for (std::size_t j = 0; j < f.n; ++j) {
    if (!std::isfinite(y[j])) {
      y[j] = 0;
    }
    if (2 * j + 1 < f.range.size()) {
      y[j] = clip(y[j], f.range[2 * j], f.range[(2 * j) + 1]);
    }
  }
  return y;
}

namespace {

std::vector<double> numbers_of(Document& document, const model::Dict& dict, std::string_view key) {
  std::vector<double> out;
  const Object* value = dict.find(key);
  if (value == nullptr) {
    return out;
  }
  auto resolved = document.resolve(*value);
  if (!resolved || resolved->as_array() == nullptr) {
    return out;
  }
  constexpr std::size_t kMaxNumbers = 4096;
  for (const Object& element : *resolved->as_array()) {
    if (out.size() >= kMaxNumbers) {
      break;
    }
    const auto n = element.as_number();
    out.push_back(n && std::isfinite(*n) ? *n : 0);
  }
  return out;
}

Result<Function> load(Document& document, const Object& object, const FunctionLimits& limits, std::size_t depth) {
  if (depth > limits.max_depth) {
    return make_error(ErrorCode::kLimitExceeded, "functions nested too deeply");
  }
  auto resolved = document.resolve(object);
  if (!resolved) {
    return std::unexpected(std::move(resolved.error()));
  }
  const model::Stream* stream = resolved->as_stream();
  const model::Dict* dict = stream != nullptr ? &stream->dict : resolved->as_dict();
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "a function is not a dictionary or a stream");
  }
  const Object* type = dict->find("FunctionType");
  const std::int64_t kind = type != nullptr ? type->as_integer().value_or(-1) : -1;
  auto impl = std::make_shared<Function::Impl>();
  impl->max_stack = limits.max_stack;
  impl->domain = numbers_of(document, *dict, "Domain");
  impl->range = numbers_of(document, *dict, "Range");
  if (impl->domain.size() < 2 || impl->domain.size() % 2 != 0 || impl->range.size() % 2 != 0) {
    return make_error(ErrorCode::kSyntax, "function without a valid /Domain or /Range");
  }
  impl->m = impl->domain.size() / 2;
  impl->n = impl->range.size() / 2;
  if (impl->m > limits.max_inputs || impl->n > limits.max_outputs) {
    return make_error(ErrorCode::kLimitExceeded, "function with too many inputs or outputs");
  }

  switch (kind) {
    case 0: {
      if (stream == nullptr || impl->n == 0) {
        return make_error(ErrorCode::kSyntax, "sampled function without its stream or /Range");
      }
      Sampled sampled;
      for (const double s : numbers_of(document, *dict, "Size")) {
        if (s < 1 || s != std::floor(s) || s > static_cast<double>(limits.max_samples)) {
          return make_error(ErrorCode::kSyntax, "sampled function with an invalid /Size");
        }
        sampled.size.push_back(static_cast<std::size_t>(s));
      }
      if (sampled.size.size() != impl->m) {
        return make_error(ErrorCode::kSyntax, "sampled function whose /Size does not match its /Domain");
      }
      std::size_t points = 1;
      for (const std::size_t s : sampled.size) {
        if (points > limits.max_samples / s) {
          return make_error(ErrorCode::kLimitExceeded, "sampled function table too large");
        }
        points *= s;
      }
      if (points > limits.max_samples / impl->n) {
        return make_error(ErrorCode::kLimitExceeded, "sampled function table too large");
      }
      const Object* bits_entry = dict->find("BitsPerSample");
      const std::int64_t bits = bits_entry != nullptr ? bits_entry->as_integer().value_or(0) : 0;
      if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 12 && bits != 16 && bits != 24 && bits != 32) {
        return make_error(ErrorCode::kSyntax, "sampled function with an invalid /BitsPerSample");
      }
      sampled.encode = numbers_of(document, *dict, "Encode");
      if (sampled.encode.size() != 2 * impl->m) {
        sampled.encode.clear();
        for (const std::size_t s : sampled.size) {
          sampled.encode.push_back(0);
          sampled.encode.push_back(static_cast<double>(s - 1));
        }
      }
      sampled.decode = numbers_of(document, *dict, "Decode");
      if (sampled.decode.size() != 2 * impl->n) {
        sampled.decode = impl->range;
      }
      auto data = document.decode_stream(*stream);
      if (!data) {
        return std::unexpected(std::move(data.error()));
      }
      // Big-endian bit fields; a short table reads the missing samples as 0, as viewers do.
      const std::size_t count = points * impl->n;
      const double scale = 1.0 / (std::ldexp(1.0, static_cast<int>(bits)) - 1);
      sampled.samples.resize(count);
      std::size_t bit = 0;
      for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t v = 0;
        for (std::int64_t b = 0; b < bits; ++b, ++bit) {
          const std::size_t byte = bit / 8;
          const unsigned set = byte < data->size() ? (std::to_integer<unsigned>((*data)[byte]) >> (7 - (bit % 8))) & 1U : 0U;
          v = (v << 1U) | set;
        }
        sampled.samples[i] = static_cast<float>(static_cast<double>(v) * scale);
      }
      impl->body = std::move(sampled);
      break;
    }
    case 2: {
      Exponential e;
      e.c0 = numbers_of(document, *dict, "C0");
      e.c1 = numbers_of(document, *dict, "C1");
      if (e.c0.empty()) {
        e.c0 = {0};
      }
      if (e.c1.empty()) {
        e.c1 = {1};
      }
      const Object* exponent = dict->find("N");
      const auto n = exponent != nullptr ? exponent->as_number() : std::nullopt;
      if (!n || !std::isfinite(*n) || e.c0.size() != e.c1.size() || e.c0.size() > limits.max_outputs) {
        return make_error(ErrorCode::kSyntax, "exponential function with an invalid /N, /C0 or /C1");
      }
      e.exponent = *n;
      impl->m = 1;
      impl->n = e.c0.size();
      impl->body = std::move(e);
      break;
    }
    case 3: {
      Stitching s;
      const Object* functions = dict->find("Functions");
      auto list = functions != nullptr ? document.resolve(*functions) : Result<Object>(Object());
      if (!list || list->as_array() == nullptr || list->as_array()->empty()) {
        return make_error(ErrorCode::kSyntax, "stitching function without /Functions");
      }
      constexpr std::size_t kMaxParts = 256;
      if (list->as_array()->size() > kMaxParts) {
        return make_error(ErrorCode::kLimitExceeded, "stitching function with too many parts");
      }
      for (const Object& part : *list->as_array()) {
        auto f = load(document, part, limits, depth + 1);
        if (!f) {
          return f;
        }
        s.functions.push_back(std::move(*f));
      }
      const std::size_t k = s.functions.size();
      s.bounds = numbers_of(document, *dict, "Bounds");
      s.encode = numbers_of(document, *dict, "Encode");
      if (s.bounds.size() + 1 != k || s.encode.size() != 2 * k) {
        return make_error(ErrorCode::kSyntax, "stitching function with a /Bounds or /Encode of the wrong size");
      }
      impl->m = 1;
      impl->n = impl->range.empty() ? s.functions.front().outputs() : impl->n;
      impl->body = std::move(s);
      break;
    }
    case 4: {
      if (stream == nullptr || impl->n == 0) {
        return make_error(ErrorCode::kSyntax, "calculator function without its stream or /Range");
      }
      auto data = document.decode_stream(*stream);
      if (!data) {
        return std::unexpected(std::move(data.error()));
      }
      const std::string text(reinterpret_cast<const char*>(data->data()), data->size());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
      auto program = CalculatorParser(text, limits).parse();
      if (!program) {
        return std::unexpected(std::move(program.error()));
      }
      impl->body = Calculator{.program = std::move(*program)};
      break;
    }
    default:
      return make_error(ErrorCode::kUnsupported, "unknown /FunctionType");
  }
  if (impl->domain.size() < 2 * impl->m) {
    return make_error(ErrorCode::kSyntax, "function /Domain too short");
  }
  return Function(std::move(impl));
}

}  // namespace

Result<Function> load_function(Document& document, const model::Object& object, const FunctionLimits& limits) { return load(document, object, limits, 0); }

Result<Function> calculator_function(std::string_view program, std::vector<double> domain, std::vector<double> range, const FunctionLimits& limits) {
  if (domain.size() < 2 || domain.size() % 2 != 0 || range.size() < 2 || range.size() % 2 != 0 || domain.size() / 2 > limits.max_inputs ||
      range.size() / 2 > limits.max_outputs) {
    return make_error(ErrorCode::kSyntax, "calculator function without a valid domain or range");
  }
  auto code = CalculatorParser(program, limits).parse();
  if (!code) {
    return std::unexpected(std::move(code.error()));
  }
  auto impl = std::make_shared<Function::Impl>();
  impl->m = domain.size() / 2;
  impl->n = range.size() / 2;
  impl->domain = std::move(domain);
  impl->range = std::move(range);
  impl->max_stack = limits.max_stack;
  impl->body = Calculator{.program = std::move(*code)};
  return Function(std::move(impl));
}

}  // namespace capriole::content
