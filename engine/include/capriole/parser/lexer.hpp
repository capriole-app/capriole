#pragma once

// Lexical analysis of PDF syntax (ISO 32000-2 §7.2, token forms of §7.3).
//
// Strict by design: input that violates the grammar is an error with the offset of the offending
// byte. Tolerating broken files is the job of the repair layer (repair/), never of this lexer.
//
// The same lexer serves file structure and content streams: operators such as BT, Tj, T*, ' and "
// are returned as keywords, and { } (PostScript calculator functions) as brace tokens.
//
// Every token records where it came from (offset, length), so unmodified objects can later be
// written back byte for byte.

#include <cstdint>
#include <string>
#include <string_view>

#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/core/source_reader.hpp"

namespace capriole::parser {

// ── Character classes (§7.2.3) ──────────────────────────────────────────────────────────────

constexpr bool is_whitespace(int c) noexcept { return c == 0x00 || c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20; }

constexpr bool is_delimiter(int c) noexcept {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

// Any byte that is neither whitespace nor a delimiter. kEof is not regular.
constexpr bool is_regular(int c) noexcept { return c >= 0 && c <= 0xFF && !is_whitespace(c) && !is_delimiter(c); }

// ── Tokens ──────────────────────────────────────────────────────────────────────────────────

enum class TokenKind : std::uint8_t {
  kEnd,         // End of input.
  kInteger,     // integer
  kReal,        // real
  kString,      // bytes = decoded string; string_form tells literal (...) from hex <...>
  kName,        // bytes = decoded name without the leading '/'
  kKeyword,     // bytes = the keyword or operator as written (true, obj, R, BT, T*, ...)
  kArrayOpen,   // [
  kArrayClose,  // ]
  kDictOpen,    // <<
  kDictClose,   // >>
  kBraceOpen,   // {
  kBraceClose,  // }
};

std::string_view to_string(TokenKind kind) noexcept;

enum class StringForm : std::uint8_t { kLiteral, kHex };

struct Token {
  TokenKind kind = TokenKind::kEnd;
  // Span of the token in the source (delimiters included).
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::int64_t integer = 0;  // kInteger
  double real = 0.0;         // kReal
  // Raw bytes for kString, kName, kKeyword. PDF strings are binary: std::string is a byte container here.
  std::string bytes;
  StringForm string_form = StringForm::kLiteral;  // kString
};

// ── Lexer ───────────────────────────────────────────────────────────────────────────────────

class Lexer {
 public:
  // The reader must outlive the lexer. The lexer starts at the reader's current position.
  explicit Lexer(SourceReader& reader, const ParseLimits& limits = kDefaultParseLimits) noexcept;

  // Skips whitespace and comments and returns the next token, or a kEnd token at end of input.
  // Errors: kSyntax (grammar violation), kUnexpectedEof (construct cut by end of input),
  // kLimitExceeded (see ParseLimits), kIo (the source failed).
  [[nodiscard]] Result<Token> next();

  // The underlying reader: after a `stream` keyword the caller reads the raw stream data from it.
  [[nodiscard]] SourceReader& reader() noexcept { return *reader_; }

 private:
  void skip_whitespace_and_comments();
  Result<Token> lex_literal_string(std::uint64_t start);
  Result<Token> lex_hex_string(std::uint64_t start);
  Result<Token> lex_name(std::uint64_t start);
  Result<Token> lex_number_or_keyword(std::uint64_t start);
  Token make_token(TokenKind kind, std::uint64_t start) const;
  // Error for input that ended inside a construct: the reader's I/O error if that is the cause.
  std::unexpected<Error> end_of_input_error(std::string what, std::uint64_t offset) const;

  SourceReader* reader_;
  ParseLimits limits_;
};

}  // namespace capriole::parser
