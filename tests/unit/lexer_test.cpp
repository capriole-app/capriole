#include "capriole/parser/lexer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace capriole::parser {
namespace {

// Lexes the whole input. On error returns the error; otherwise every token up to (excluding) kEnd.
Result<std::vector<Token>> lex_all(const std::string& text, std::size_t window = SourceReader::kDefaultWindow,
                                   const ParseLimits& limits = kDefaultParseLimits) {
  auto source = MemoryByteSource::from_string(text);
  SourceReader reader(source, window);
  Lexer lexer(reader, limits);
  std::vector<Token> tokens;
  for (;;) {
    auto token = lexer.next();
    if (!token) {
      return std::unexpected(token.error());
    }
    if (token->kind == TokenKind::kEnd) {
      return tokens;
    }
    tokens.push_back(std::move(*token));
  }
}

// Lexes input expected to hold exactly one token.
Token lex_one(const std::string& text) {
  auto tokens = lex_all(text);
  EXPECT_TRUE(tokens.has_value()) << text << ": " << (tokens ? "" : tokens.error().message);
  EXPECT_EQ(tokens.has_value() ? tokens->size() : 0U, 1U) << text;
  return tokens.has_value() && !tokens->empty() ? tokens->front() : Token{};
}

ErrorCode lex_error(const std::string& text, const ParseLimits& limits = kDefaultParseLimits) {
  auto tokens = lex_all(text, SourceReader::kDefaultWindow, limits);
  EXPECT_FALSE(tokens.has_value()) << "expected an error for: " << text;
  return tokens.has_value() ? ErrorCode::kIo : tokens.error().code;
}

// ── Whitespace, comments, delimiters ────────────────────────────────────────────────────────

TEST(Lexer, EmptyAndBlankInputs) {
  EXPECT_TRUE(lex_all("")->empty());
  EXPECT_TRUE(lex_all(std::string(" \t\r\n\f\0", 6))->empty());
  EXPECT_TRUE(lex_all("% only a comment")->empty());
}

TEST(Lexer, CommentsRunToEndOfLine) {
  const auto tokens = lex_all("% header\n42 % trailing\r7%x\r\n8");
  ASSERT_TRUE(tokens.has_value());
  ASSERT_EQ(tokens->size(), 3U);
  EXPECT_EQ((*tokens)[0].integer, 42);
  EXPECT_EQ((*tokens)[1].integer, 7);
  EXPECT_EQ((*tokens)[2].integer, 8);
}

TEST(Lexer, Punctuation) {
  const auto tokens = lex_all("[]<<>>{}");
  ASSERT_TRUE(tokens.has_value());
  const std::vector<TokenKind> expected = {TokenKind::kArrayOpen, TokenKind::kArrayClose, TokenKind::kDictOpen,
                                           TokenKind::kDictClose, TokenKind::kBraceOpen,  TokenKind::kBraceClose};
  ASSERT_EQ(tokens->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ((*tokens)[i].kind, expected[i]) << i;
  }
}

TEST(Lexer, TokenSpans) {
  const auto tokens = lex_all("  /Type 12 (ab) <<");
  ASSERT_TRUE(tokens.has_value());
  ASSERT_EQ(tokens->size(), 4U);
  EXPECT_EQ((*tokens)[0].offset, 2U);
  EXPECT_EQ((*tokens)[0].length, 5U);
  EXPECT_EQ((*tokens)[1].offset, 8U);
  EXPECT_EQ((*tokens)[1].length, 2U);
  EXPECT_EQ((*tokens)[2].offset, 11U);
  EXPECT_EQ((*tokens)[2].length, 4U);
  EXPECT_EQ((*tokens)[3].offset, 16U);
  EXPECT_EQ((*tokens)[3].length, 2U);
}

TEST(Lexer, StrayClosingDelimitersAreErrors) {
  EXPECT_EQ(lex_error(">"), ErrorCode::kSyntax);
  EXPECT_EQ(lex_error("1 > 2"), ErrorCode::kSyntax);
  EXPECT_EQ(lex_error(")"), ErrorCode::kSyntax);
}

// ── Numbers (§7.3.3) ────────────────────────────────────────────────────────────────────────

TEST(Lexer, Integers) {
  const auto tokens = lex_all("123 43445 +17 -98 0 -0");
  ASSERT_TRUE(tokens.has_value());
  const std::vector<std::int64_t> expected = {123, 43445, 17, -98, 0, 0};
  ASSERT_EQ(tokens->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ((*tokens)[i].kind, TokenKind::kInteger);
    EXPECT_EQ((*tokens)[i].integer, expected[i]);
  }
}

TEST(Lexer, IntegerRange) {
  EXPECT_EQ(lex_one("9223372036854775807").integer, std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(lex_one("-9223372036854775808").integer, std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(lex_error("9223372036854775808"), ErrorCode::kLimitExceeded);
}

TEST(Lexer, Reals) {
  const auto tokens = lex_all("34.5 -3.62 +123.6 4. -.002 0.0 .5");
  ASSERT_TRUE(tokens.has_value());
  const std::vector<double> expected = {34.5, -3.62, 123.6, 4.0, -0.002, 0.0, 0.5};
  ASSERT_EQ(tokens->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ((*tokens)[i].kind, TokenKind::kReal) << i;
    EXPECT_DOUBLE_EQ((*tokens)[i].real, expected[i]) << i;
  }
}

TEST(Lexer, MalformedNumbersAreSyntaxErrors) {
  for (const char* text : {"1.2.3", "--5", "+-1", "-", "+", ".", "-.", "5-", "1e5", "12abc", "0x1F"}) {
    EXPECT_EQ(lex_error(text), ErrorCode::kSyntax) << text;
  }
}

TEST(Lexer, NumberEndsAtDelimiter) {
  const auto tokens = lex_all("[1 2.5]/X(3)");
  ASSERT_TRUE(tokens.has_value());
  ASSERT_EQ(tokens->size(), 6U);
  EXPECT_EQ((*tokens)[1].integer, 1);
  EXPECT_DOUBLE_EQ((*tokens)[2].real, 2.5);
}

// ── Keywords and operators ──────────────────────────────────────────────────────────────────

TEST(Lexer, Keywords) {
  const auto tokens = lex_all("true false null obj endobj R stream BT T* ' \" d0");
  ASSERT_TRUE(tokens.has_value());
  const std::vector<std::string> expected = {"true", "false", "null", "obj", "endobj", "R", "stream", "BT", "T*", "'", "\"", "d0"};
  ASSERT_EQ(tokens->size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ((*tokens)[i].kind, TokenKind::kKeyword);
    EXPECT_EQ((*tokens)[i].bytes, expected[i]);
  }
}

TEST(Lexer, IndirectReferenceIsThreeTokens) {
  const auto tokens = lex_all("12 0 R");
  ASSERT_TRUE(tokens.has_value());
  ASSERT_EQ(tokens->size(), 3U);
  EXPECT_EQ((*tokens)[2].bytes, "R");
}

TEST(Lexer, StreamKeywordLeavesReaderAfterKeyword) {
  auto source = MemoryByteSource::from_string("<</Length 3>>stream\r\nabc");
  SourceReader reader(source);
  Lexer lexer(reader);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(lexer.next().has_value());
  }
  const auto keyword = lexer.next();
  ASSERT_TRUE(keyword.has_value());
  EXPECT_EQ(keyword->bytes, "stream");
  EXPECT_EQ(lexer.reader().position(), 19U);  // on the CR: the caller reads the EOL and the data
}

// ── Literal strings (§7.3.4.2) ──────────────────────────────────────────────────────────────

TEST(Lexer, LiteralStrings) {
  EXPECT_EQ(lex_one("(This is a string)").bytes, "This is a string");
  EXPECT_EQ(lex_one("(Strings may contain balanced parentheses ( ) and special characters (*!&}^% and so on).)").bytes,
            "Strings may contain balanced parentheses ( ) and special characters (*!&}^% and so on).");
  EXPECT_EQ(lex_one("()").bytes, "");
  EXPECT_EQ(lex_one("(a(b(c)d)e)").bytes, "a(b(c)d)e");
  const Token token = lex_one("(x)");
  EXPECT_EQ(token.kind, TokenKind::kString);
  EXPECT_EQ(token.string_form, StringForm::kLiteral);
}

TEST(Lexer, LiteralStringEscapes) {
  EXPECT_EQ(lex_one(R"((\n\r\t\b\f\(\)\\))").bytes, "\n\r\t\b\f()\\");
  EXPECT_EQ(lex_one(R"((unbalanced \( is fine))").bytes, "unbalanced ( is fine");
  EXPECT_EQ(lex_one(R"((\q))").bytes, "q");  // unknown escape: backslash dropped
}

TEST(Lexer, LiteralStringOctalEscapes) {
  EXPECT_EQ(lex_one(R"((\053))").bytes, "+");
  EXPECT_EQ(lex_one(R"((\53))").bytes, "+");
  EXPECT_EQ(lex_one(R"((\7))").bytes, "\x07");
  EXPECT_EQ(lex_one(R"((\0053))").bytes, std::string("\x05"
                                                     "3"));  // at most three digits
  EXPECT_EQ(lex_one(R"((\777))").bytes, "\xFF");             // high-order overflow ignored
  EXPECT_EQ(lex_one(R"((\000))").bytes, std::string(1, '\0'));
  EXPECT_EQ(lex_one(R"((\8))").bytes, "8");  // 8 is not octal: unknown escape
}

TEST(Lexer, LiteralStringLineHandling) {
  EXPECT_EQ(lex_one("(a\r\nb\rc\nd)").bytes, "a\nb\nc\nd");  // EOLs normalised to LF
  EXPECT_EQ(lex_one("(abc\\\ndef)").bytes, "abcdef");        // continuation with LF
  EXPECT_EQ(lex_one("(abc\\\r\ndef)").bytes, "abcdef");      // continuation with CR LF
  EXPECT_EQ(lex_one("(abc\\\rdef)").bytes, "abcdef");        // continuation with CR
}

TEST(Lexer, LiteralStringKeepsBinaryBytes) { EXPECT_EQ(lex_one(std::string("(\xFE\xFF\0\x41)", 6)).bytes, std::string("\xFE\xFF\0\x41", 4)); }

TEST(Lexer, UnterminatedLiteralString) {
  EXPECT_EQ(lex_error("(abc"), ErrorCode::kUnexpectedEof);
  EXPECT_EQ(lex_error("(a(b)"), ErrorCode::kUnexpectedEof);
  EXPECT_EQ(lex_error("(abc\\"), ErrorCode::kUnexpectedEof);
}

// ── Hexadecimal strings (§7.3.4.3) ──────────────────────────────────────────────────────────

TEST(Lexer, HexStrings) {
  EXPECT_EQ(lex_one("<4E6F762073686D6F7A206B6120706F702E>").bytes, "Nov shmoz ka pop.");
  EXPECT_EQ(lex_one("<901FA3>").bytes, "\x90\x1F\xA3");
  EXPECT_EQ(lex_one("<901FA>").bytes, "\x90\x1F\xA0");  // odd count: final 0 assumed
  EXPECT_EQ(lex_one("<90 1f\r\na3>").bytes, "\x90\x1F\xA3");
  EXPECT_EQ(lex_one("<>").bytes, "");
  EXPECT_EQ(lex_one("<41>").string_form, StringForm::kHex);
}

TEST(Lexer, BadHexStrings) {
  EXPECT_EQ(lex_error("<4G>"), ErrorCode::kSyntax);
  EXPECT_EQ(lex_error("<41"), ErrorCode::kUnexpectedEof);
}

// ── Names (§7.3.5, examples from Table 4) ───────────────────────────────────────────────────

TEST(Lexer, Names) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"/Name1", "Name1"},
      {"/ASomewhatLongerName", "ASomewhatLongerName"},
      {"/A;Name_With-Various***Characters?", "A;Name_With-Various***Characters?"},
      {"/1.2", "1.2"},
      {"/$$", "$$"},
      {"/@pattern", "@pattern"},
      {"/.notdef", ".notdef"},
      {"/Lime#20Green", "Lime Green"},
      {"/paired#28#29parentheses", "paired()parentheses"},
      {"/The_Key_of_F#23_Minor", "The_Key_of_F#_Minor"},
      {"/A#42", "AB"},
      {"/", ""},
  };
  for (const auto& [text, expected] : cases) {
    const Token token = lex_one(text);
    EXPECT_EQ(token.kind, TokenKind::kName) << text;
    EXPECT_EQ(token.bytes, expected) << text;
  }
}

TEST(Lexer, NamesEndAtDelimiters) {
  const auto tokens = lex_all("/Type/Page/Kids[/A]");
  ASSERT_TRUE(tokens.has_value());
  ASSERT_EQ(tokens->size(), 6U);
  EXPECT_EQ((*tokens)[0].bytes, "Type");
  EXPECT_EQ((*tokens)[1].bytes, "Page");
  EXPECT_EQ((*tokens)[2].bytes, "Kids");
  EXPECT_EQ((*tokens)[4].bytes, "A");
}

TEST(Lexer, BadNameEscapes) {
  for (const char* text : {"/A#4", "/A#4 ", "/A#GG", "/A#", "/A#00"}) {
    EXPECT_EQ(lex_error(text), ErrorCode::kSyntax) << text;
  }
}

// ── Composite input ─────────────────────────────────────────────────────────────────────────

TEST(Lexer, DictionaryObject) {
  const auto tokens = lex_all("1 0 obj\n<</Type/Page/MediaBox[0 0 612 792]/Contents 4 0 R>>\nendobj");
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->size(), 19U);
  EXPECT_EQ(tokens->back().bytes, "endobj");
}

TEST(Lexer, WindowSizeDoesNotChangeTheResult) {
  std::string text;
  for (int i = 0; i < 200; ++i) {
    text += "<</N" + std::to_string(i) + " (s\\(tr\\051ing) /Lime#20Green <48656C6C6F> [" + std::to_string(i) + " -." + std::to_string(i) + "]>>\n";
  }
  const auto big = lex_all(text);
  const auto small = lex_all(text, SourceReader::kMinWindow);
  ASSERT_TRUE(big.has_value());
  ASSERT_TRUE(small.has_value());
  ASSERT_EQ(big->size(), small->size());
  for (std::size_t i = 0; i < big->size(); ++i) {
    EXPECT_EQ((*big)[i].kind, (*small)[i].kind) << i;
    EXPECT_EQ((*big)[i].offset, (*small)[i].offset) << i;
    EXPECT_EQ((*big)[i].bytes, (*small)[i].bytes) << i;
  }
}

// ── Limits ──────────────────────────────────────────────────────────────────────────────────

TEST(Lexer, LimitsAreEnforced) {
  ParseLimits limits;
  limits.max_token_bytes = 8;
  limits.max_string_bytes = 4;
  EXPECT_EQ(lex_error("/ABCDEFGHI", limits), ErrorCode::kLimitExceeded);
  EXPECT_EQ(lex_error("123456789", limits), ErrorCode::kLimitExceeded);
  EXPECT_EQ(lex_error("keywordxx", limits), ErrorCode::kLimitExceeded);
  EXPECT_EQ(lex_error("(abcde)", limits), ErrorCode::kLimitExceeded);
  EXPECT_EQ(lex_error("<4142434445>", limits), ErrorCode::kLimitExceeded);
  EXPECT_TRUE(lex_all("/ABCDEFGH (abcd) <41424344>", SourceReader::kDefaultWindow, limits).has_value());
}

TEST(Lexer, TokenKindNames) {
  EXPECT_EQ(to_string(TokenKind::kDictOpen), "dict_open");
  EXPECT_EQ(to_string(TokenKind::kEnd), "end");
}

}  // namespace
}  // namespace capriole::parser
