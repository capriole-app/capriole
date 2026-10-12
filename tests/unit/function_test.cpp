#include "capriole/content/function.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::content {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

// A document whose object 10 is the function under test (and 11.. the parts it refers to).
struct Fixture {
  explicit Fixture(const std::vector<std::pair<std::uint32_t, std::string>>& objects) {
    test::PdfBuilder b;
    b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
    b.add_object(2, "<</Type/Pages/Kids[]/Count 0>>");
    for (const auto& [number, body] : objects) {
      b.add_object(number, body);
    }
    b.finish_table("/Size 20/Root 1 0 R");
    auto opened = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
    EXPECT_TRUE(opened.has_value());
    document = std::make_unique<Document>(std::move(*opened));
  }

  Result<Function> function(std::uint32_t number = 10) { return load_function(*document, model::Object(model::ObjectRef{.number = number, .generation = 0})); }

  std::unique_ptr<Document> document;
};

std::string stream(const std::string& dict, const std::string& data) {
  return "<<" + dict + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream";
}

std::vector<double> at(const Function& f, std::vector<double> x) { return f.evaluate(x); }

TEST(Function, Exponential) {
  Fixture fx({{10, "<</FunctionType 2/Domain[0 1]/C0[0 1]/C1[1 0]/N 2>>"}});
  auto f = fx.function();
  ASSERT_TRUE(f.has_value()) << f.error().message;
  EXPECT_EQ(f->inputs(), 1U);
  EXPECT_EQ(f->outputs(), 2U);
  EXPECT_EQ(at(*f, {0.5}), (std::vector<double>{0.25, 0.75}));
  EXPECT_EQ(at(*f, {2}), (std::vector<double>{1, 0}));  // the input is clipped to the domain
}

TEST(Function, SampledInterpolatesAndDecodes) {
  // 1 input, 3 samples of 8 bits: 0, 255, 0 — a tent; decoded to 0..2.
  Fixture fx({{10, stream("/FunctionType 0/Domain[0 1]/Range[0 2]/Size[3]/BitsPerSample 8", std::string("\x00\xFF\x00", 3))}});
  auto f = fx.function();
  ASSERT_TRUE(f.has_value()) << f.error().message;
  EXPECT_NEAR(at(*f, {0})[0], 0, 1e-6);
  EXPECT_NEAR(at(*f, {0.25})[0], 1, 1e-6);
  EXPECT_NEAR(at(*f, {0.5})[0], 2, 1e-6);
  EXPECT_NEAR(at(*f, {1})[0], 0, 1e-6);
  // 2 inputs (2 x 2), 4 bits: bilinear.
  Fixture two({{10, stream("/FunctionType 0/Domain[0 1 0 1]/Range[0 1]/Size[2 2]/BitsPerSample 4", std::string("\x0F\xF0", 2))}});
  auto g = two.function();
  ASSERT_TRUE(g.has_value()) << g.error().message;
  EXPECT_NEAR(at(*g, {0, 0})[0], 0, 1e-6);  // samples (x, y): (0,0)=0 (1,0)=1 (0,1)=1 (1,1)=0
  EXPECT_NEAR(at(*g, {1, 0})[0], 1, 1e-6);
  EXPECT_NEAR(at(*g, {0.5, 0.5})[0], 0.5, 1e-6);
}

TEST(Function, Stitching) {
  Fixture fx(
      {{10, "<</FunctionType 3/Domain[0 1]/Functions[11 0 R 11 0 R]/Bounds[0.5]/Encode[0 1 1 0]>>"}, {11, "<</FunctionType 2/Domain[0 1]/C0[0]/C1[1]/N 1>>"}});
  auto f = fx.function();
  ASSERT_TRUE(f.has_value()) << f.error().message;
  EXPECT_NEAR(at(*f, {0.25})[0], 0.5, 1e-9);
  EXPECT_NEAR(at(*f, {0.5})[0], 1, 1e-9);
  EXPECT_NEAR(at(*f, {0.75})[0], 0.5, 1e-9);
}

TEST(Function, StitchingThatContainsItselfIsBounded) {
  Fixture fx({{10, "<</FunctionType 3/Domain[0 1]/Functions[10 0 R]/Bounds[]/Encode[0 1]>>"}});
  auto f = fx.function();
  ASSERT_FALSE(f.has_value());
  EXPECT_EQ(f.error().code, ErrorCode::kLimitExceeded);
}

TEST(Function, CalculatorFromTheStream) {
  Fixture fx({{10, stream("/FunctionType 4/Domain[0 1 0 1]/Range[0 1 0 1]", "{ 2 copy mul 3 1 roll add 2 div }")}});
  auto f = fx.function();
  ASSERT_TRUE(f.has_value()) << f.error().message;
  EXPECT_EQ(f->outputs(), 2U);
  const auto y = at(*f, {0.5, 0.25});
  ASSERT_EQ(y.size(), 2U);  // x·y, then (x + y) / 2
  EXPECT_NEAR(y[0], 0.5 * 0.25, 1e-12);
  EXPECT_NEAR(y[1], 0.375, 1e-12);
}

TEST(Function, CalculatorOperators) {
  const auto run = [](std::string_view program, std::vector<double> x = {0.5}) {
    auto f = calculator_function(program, {-1000, 1000}, {-1000, 1000});
    EXPECT_TRUE(f.has_value()) << program << ": " << (f ? "" : f.error().message);
    return f ? f->evaluate(x)[0] : -9999;
  };
  EXPECT_EQ(run("{ pop 7 2 idiv }"), 3);
  EXPECT_EQ(run("{ pop -7 2 mod }"), -1);
  EXPECT_EQ(run("{ pop 1 3 bitshift }"), 8);
  EXPECT_EQ(run("{ pop 12 10 and 1 or }"), 9);
  EXPECT_EQ(run("{ pop 2.5 round 2.5 truncate add 3.7 floor add -1.2 ceiling add }"), 3 + 2 + 3 - 1);
  EXPECT_NEAR(run("{ pop 30 sin 60 cos add }"), 1, 1e-12);
  EXPECT_NEAR(run("{ pop 1 1 atan }"), 45, 1e-12);
  EXPECT_NEAR(run("{ pop 2 5 exp 100 log add 1 ln add }"), 32 + 2, 1e-9);
  EXPECT_EQ(run("{ 0.25 gt { 1 } { 2 } ifelse }"), 1);
  EXPECT_EQ(run("{ 0.25 gt { 1 } { 2 } ifelse }", {0.1}), 2);
  EXPECT_EQ(run("{ dup 0.25 lt { pop 5 } if }", {0.1}), 5);
  EXPECT_EQ(run("{ pop 1 2 3 2 index }"), 1);
  EXPECT_EQ(run("{ pop 1 2 exch sub }"), 1);
  EXPECT_EQ(run("{ pop true false xor { 4 } { 0 } ifelse }"), 4);
  EXPECT_EQ(run("{ pop 3 dup eq 1 2 ne and { 6 } if }"), 6);
  EXPECT_EQ(run("{ pop 2 sqrt 1.5 mul cvi }"), 2);  // 2.12 truncated
  EXPECT_EQ(run("{ pop 1 2 3 3 -1 roll pop pop }"), 2);
}

TEST(Function, CalculatorErrors) {
  // Parse errors: unknown operators, procedures without if, unbalanced braces, runaway nesting.
  EXPECT_FALSE(calculator_function("{ 1 frobnicate }", {0, 1}, {0, 1}).has_value());
  EXPECT_FALSE(calculator_function("{ { 1 } }", {0, 1}, {0, 1}).has_value());
  EXPECT_FALSE(calculator_function("{ 1 2 add", {0, 1}, {0, 1}).has_value());
  EXPECT_FALSE(calculator_function(std::string(10'000, '{'), {0, 1}, {0, 1}).has_value());
  // Run-time errors give zeros (clipped to the range): underflow, division by zero, stack overflow.
  for (const std::string_view program : {"{ pop pop pop }", "{ 0 div }", "{ 1 1 index 1 index 1 index 99 copy }", "{ 1 true add }"}) {
    auto f = calculator_function(program, {0, 1}, {0.5, 1});
    ASSERT_TRUE(f.has_value()) << program;
    EXPECT_EQ(f->evaluate(std::vector<double>{0.3})[0], 0.5) << program;
  }
}

}  // namespace
}  // namespace capriole::content
