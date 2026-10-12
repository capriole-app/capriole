#include "capriole/content/content_parser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/operators.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/document/document.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::content {
namespace {

using model::Object;
using parser::ParseMode;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::vector<Operation> parse(const std::string& text, ParseMode mode = ParseMode::kStrict) {
  const auto data = bytes(text);
  auto operations = parse_content(data, kDefaultParseLimits, mode);
  EXPECT_TRUE(operations.has_value()) << (operations ? "" : operations.error().message);
  return operations ? std::move(*operations) : std::vector<Operation>{};
}

std::string names(const std::vector<Operation>& operations) {
  std::string out;
  for (const Operation& op : operations) {
    out += (out.empty() ? "" : " ") + op.name;
  }
  return out;
}

TEST(Operators, TableIsCompleteAndConsistent) {
  // Every operator of Annex A, Table A.1, except the inline image parts ID and EI.
  const std::vector<std::string> all = {"b",   "B",  "b*", "B*", "BDC", "BI", "BMC", "BT", "BX", "c",  "cm", "CS", "cs",  "d",   "d0", "d1", "Do", "DP",
                                        "EMC", "ET", "EX", "f",  "F",   "f*", "G",   "g",  "gs", "h",  "i",  "j",  "J",   "K",   "k",  "l",  "m",  "M",
                                        "MP",  "n",  "q",  "Q",  "re",  "RG", "rg",  "ri", "s",  "S",  "SC", "sc", "SCN", "scn", "sh", "T*", "Tc", "Td",
                                        "TD",  "Tf", "Tj", "TJ", "TL",  "Tm", "Tr",  "Ts", "Tw", "Tz", "v",  "w",  "W",   "W*",  "y",  "'",  "\""};
  EXPECT_EQ(all.size(), 71U);
  for (const std::string& name : all) {
    const OperatorInfo* found = find_operator(name);
    ASSERT_NE(found, nullptr) << name;
    EXPECT_EQ(found->name, name);
    EXPECT_EQ(info(found->op).name, name);
    EXPECT_LE(found->min_operands, found->max_operands);
  }
  EXPECT_EQ(find_operator("ID"), nullptr);
  EXPECT_EQ(find_operator("xyz"), nullptr);
  EXPECT_EQ(find_operator("scn")->max_operands, 33);
  EXPECT_EQ(info(Operator::kUnknown).category, OperatorCategory::kUnknown);
}

TEST(ContentParser, OperatorsAndOperands) {
  const auto ops = parse("q 1 0 0 1 72 720 cm BT /F1 12 Tf (Hello) Tj [(A) -120 (B)] TJ ET Q");
  EXPECT_EQ(names(ops), "q cm BT Tf Tj TJ ET Q");
  EXPECT_EQ(ops[1].op, Operator::kConcatMatrix);
  ASSERT_EQ(ops[1].operands.size(), 6U);
  EXPECT_EQ(ops[1].operands[5], Object(720));
  EXPECT_EQ(ops[3].operands[0], Object(model::Name{.bytes = "F1"}));
  EXPECT_EQ(ops[4].operands[0].as_string()->bytes, "Hello");
  ASSERT_NE(ops[5].operands[0].as_array(), nullptr);
  EXPECT_EQ(ops[5].operands[0].as_array()->size(), 3U);
  EXPECT_EQ(ops[0].offset, 0U);
  EXPECT_EQ(ops[2].offset, std::string("q 1 0 0 1 72 720 cm ").size());
}

TEST(ContentParser, QuotesDictionariesAndUnknownOperators) {
  const auto ops = parse("/Span <</MCID 0>> BDC 1 2 (x) \" (y) ' EMC 5 foo BX 7 bar EX");
  EXPECT_EQ(names(ops), "BDC \" ' EMC foo BX bar EX");
  EXPECT_NE(ops[0].operands[1].as_dict(), nullptr);
  EXPECT_EQ(ops[1].op, Operator::kNextLineSpacingShowText);
  EXPECT_EQ(ops[1].operands.size(), 3U);
  EXPECT_EQ(ops[4].op, Operator::kUnknown);
  EXPECT_EQ(ops[4].operands.size(), 1U);
  EXPECT_EQ(ops[5].op, Operator::kBeginCompatibility);
}

TEST(ContentParser, OperandsWithoutAnOperatorAtTheEndAreDropped) { EXPECT_EQ(names(parse("q 1 2 3")), "q"); }

TEST(ContentParser, InlineImageIsOneOperation) {
  // The data holds "EI" twice: after a non-space byte, and between spaces but followed by binary.
  const std::string content = std::string("q BI /W 4 /H 2 /BPC 8 /CS /G ID \x01") + "EI" + "\x02 EI \xFF\x80\x01 " + "\x03 EI Q";
  const auto ops = parse(content);
  ASSERT_EQ(names(ops), "q BI Q");
  const Operation& image = ops[1];
  EXPECT_EQ(image.op, Operator::kInlineImage);
  ASSERT_NE(image.image, nullptr);
  EXPECT_EQ(image.image->dict.find("W")->as_integer(), 4);
  EXPECT_TRUE(image.image->dict.find("CS")->is_name("G"));
  EXPECT_EQ(image.image->data, bytes(std::string("\x01") + "EI" + "\x02 EI \xFF\x80\x01 \x03"));
  EXPECT_EQ(image.offset, 2U);
}

TEST(ContentParser, InlineImageWithDeclaredLength) {
  // PDF 2.0 /L: the data may end in white-space and contain " EI " followed by text.
  const auto ops = parse("BI /W 1 /H 1 /L 6 ID\nab EI \nEI Tj");
  ASSERT_EQ(names(ops), "BI Tj");
  EXPECT_EQ(ops[0].image->data, bytes("ab EI "));
}

TEST(ContentParser, InlineImageErrors) {
  const auto missing_ei = bytes("BI /W 1 ID \x01\x02");
  EXPECT_EQ(parse_content(missing_ei).error().code, ErrorCode::kSyntax);
  const auto odd = bytes("BI /W ID x EI");
  EXPECT_EQ(parse_content(odd).error().code, ErrorCode::kSyntax);
  const auto operator_inside = bytes("BI /W 1 q ID x EI");
  EXPECT_EQ(parse_content(operator_inside).error().code, ErrorCode::kSyntax);
  // Tolerant: without EI, the data runs to the end.
  const auto tolerant = parse("BI /W 1 ID \x01\x02", ParseMode::kTolerant);
  ASSERT_EQ(tolerant.size(), 1U);
  EXPECT_EQ(tolerant[0].image->data, bytes("\x01\x02"));
}

TEST(ContentParser, StrictStopsTolerantSkips) {
  const std::string broken = "q 1 0 0 1 0 0 cm ) 2 w S Q";
  const auto data = bytes(broken);
  EXPECT_EQ(parse_content(data).error().code, ErrorCode::kSyntax);

  ContentParser parser(data, kDefaultParseLimits, ParseMode::kTolerant);
  std::vector<Operation> ops;
  while (auto op = parser.next()) {
    ASSERT_TRUE(op.has_value());
    if (!*op) {
      break;
    }
    ops.push_back(std::move(**op));
  }
  EXPECT_EQ(names(ops), "q cm w S Q");
  EXPECT_EQ(parser.recovered_errors(), 1U);
}

TEST(ContentParser, TolerantKeepsWhatCameBeforeAnUnterminatedOperand) {
  EXPECT_EQ(names(parse("q 1 w (never closed", ParseMode::kTolerant)), "q w");
  EXPECT_EQ(names(parse("q [1 2", ParseMode::kTolerant)), "q");
}

TEST(ContentParser, OperandLimit) {
  std::string many;
  for (std::size_t i = 0; i <= kMaxOperands; ++i) {
    many += "1 ";
  }
  const auto data = bytes(many + "w");
  EXPECT_EQ(parse_content(data).error().code, ErrorCode::kLimitExceeded);
  const auto tolerant = parse(many + "w", ParseMode::kTolerant);
  ASSERT_EQ(tolerant.size(), 1U);
  EXPECT_LE(tolerant[0].operands.size(), kMaxOperands);
}

TEST(ContentParser, HostileInputStaysLinear) {
  // Unclosed strings are each an error that reaches the end of the input: skipping one byte and trying
  // again would be quadratic. The tolerant parser stops at the first one instead.
  const std::string opens(200000, '(');
  const auto start = std::chrono::steady_clock::now();
  const auto ops = parse("q " + opens, ParseMode::kTolerant);
  EXPECT_EQ(names(ops), "q");
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
}

TEST(PageContent, JoinsTheStreamsOfAPage) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R 6 0 R]/Count 2>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents[4 0 R 5 0 R]>>");
  b.add_stream(4, "q 1 0 0 1 0 0");
  b.add_stream(5, test::zlib_compress("cm Q"), "/Filter/FlateDecode");
  b.add_object(6, "<</Type/Page/Parent 2 0 R>>");
  b.finish_table("/Size 7/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  ASSERT_TRUE(document.has_value());
  const auto content = page_content(*document, model::ObjectRef{.number = 3, .generation = 0});
  ASSERT_TRUE(content.has_value()) << content.error().message;
  EXPECT_EQ(*content, bytes("q 1 0 0 1 0 0\ncm Q"));  // a token split across streams stays split
  const auto ops = parse_content(*content);
  EXPECT_EQ(names(*ops), "q cm Q");
  EXPECT_TRUE(page_content(*document, model::ObjectRef{.number = 6, .generation = 0})->empty());
}

}  // namespace
}  // namespace capriole::content
