#include "capriole/parser/lexer.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

namespace capriole::parser {

namespace {

constexpr int kEof = SourceReader::kEof;

constexpr bool is_digit(int c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_octal_digit(int c) noexcept { return c >= '0' && c <= '7'; }

constexpr int hex_value(int c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

constexpr char to_char(int byte) noexcept { return static_cast<char>(static_cast<unsigned char>(byte)); }

}  // namespace

std::string_view to_string(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::kEnd:
      return "end";
    case TokenKind::kInteger:
      return "integer";
    case TokenKind::kReal:
      return "real";
    case TokenKind::kString:
      return "string";
    case TokenKind::kName:
      return "name";
    case TokenKind::kKeyword:
      return "keyword";
    case TokenKind::kArrayOpen:
      return "array_open";
    case TokenKind::kArrayClose:
      return "array_close";
    case TokenKind::kDictOpen:
      return "dict_open";
    case TokenKind::kDictClose:
      return "dict_close";
    case TokenKind::kBraceOpen:
      return "brace_open";
    case TokenKind::kBraceClose:
      return "brace_close";
  }
  return "unknown";
}

Lexer::Lexer(SourceReader& reader, const ParseLimits& limits) noexcept : reader_(&reader), limits_(limits) {}

Token Lexer::make_token(TokenKind kind, std::uint64_t start) const {
  Token token;
  token.kind = kind;
  token.offset = start;
  token.length = reader_->position() - start;
  return token;
}

std::unexpected<Error> Lexer::end_of_input_error(std::string what, std::uint64_t offset) const {
  if (reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  }
  return make_error(ErrorCode::kUnexpectedEof, std::move(what), offset);
}

void Lexer::skip_whitespace_and_comments() {
  for (;;) {
    const int c = reader_->peek();
    if (is_whitespace(c)) {
      reader_->advance();
    } else if (c == '%') {
      // A comment runs to the end of the line (§7.2.4); the EOL itself is whitespace.
      int d = reader_->next();
      while (d != kEof && d != '\r' && d != '\n') {
        d = reader_->next();
      }
    } else {
      return;
    }
  }
}

Result<Token> Lexer::next() {
  skip_whitespace_and_comments();
  const std::uint64_t start = reader_->position();
  const int c = reader_->peek();

  switch (c) {
    case kEof:
      if (reader_->io_error()) {
        return std::unexpected(*reader_->io_error());
      }
      return make_token(TokenKind::kEnd, start);
    case '(':
      return lex_literal_string(start);
    case '<':
      if (reader_->peek(1) == '<') {
        reader_->advance(2);
        return make_token(TokenKind::kDictOpen, start);
      }
      return lex_hex_string(start);
    case '>':
      if (reader_->peek(1) == '>') {
        reader_->advance(2);
        return make_token(TokenKind::kDictClose, start);
      }
      return make_error(ErrorCode::kSyntax, "unexpected '>'", start);
    case '[':
      reader_->advance();
      return make_token(TokenKind::kArrayOpen, start);
    case ']':
      reader_->advance();
      return make_token(TokenKind::kArrayClose, start);
    case '{':
      reader_->advance();
      return make_token(TokenKind::kBraceOpen, start);
    case '}':
      reader_->advance();
      return make_token(TokenKind::kBraceClose, start);
    case ')':
      return make_error(ErrorCode::kSyntax, "unbalanced ')'", start);
    case '/':
      return lex_name(start);
    default:
      return lex_number_or_keyword(start);
  }
}

// §7.3.4.2. Balanced parentheses need no escape; an unescaped EOL (CR, LF or CR LF) reads as LF;
// a backslash before an EOL continues the line; a backslash before any other unknown byte is dropped.
Result<Token> Lexer::lex_literal_string(std::uint64_t start) {
  reader_->advance();  // '('
  std::string out;
  std::size_t depth = 1;

  for (;;) {
    const int c = reader_->next();
    switch (c) {
      case kEof:
        return end_of_input_error("unterminated literal string", start);
      case '(':
        ++depth;
        out.push_back('(');
        break;
      case ')':
        if (--depth == 0) {
          Token token = make_token(TokenKind::kString, start);
          token.bytes = std::move(out);
          token.string_form = StringForm::kLiteral;
          return token;
        }
        out.push_back(')');
        break;
      case '\r':
        if (reader_->peek() == '\n') {
          reader_->advance();
        }
        out.push_back('\n');
        break;
      case '\\': {
        const int e = reader_->next();
        switch (e) {
          case kEof:
            return end_of_input_error("unterminated literal string", start);
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case '\r':  // line continuation: backslash + EOL produce nothing
            if (reader_->peek() == '\n') {
              reader_->advance();
            }
            break;
          case '\n':
            break;
          default:
            if (is_octal_digit(e)) {
              // \d, \dd or \ddd; high-order overflow is ignored (\777 is 0xFF).
              int value = e - '0';
              for (int i = 0; i < 2 && is_octal_digit(reader_->peek()); ++i) {
                value = (value * 8) + (reader_->next() - '0');
              }
              out.push_back(to_char(value & 0xFF));
            } else {
              out.push_back(to_char(e));  // covers \( \) \\ and unknown escapes alike
            }
            break;
        }
        break;
      }
      default:
        out.push_back(to_char(c));
        break;
    }
    if (out.size() > limits_.max_string_bytes) {
      return make_error(ErrorCode::kLimitExceeded, "literal string exceeds max_string_bytes", start);
    }
  }
}

// §7.3.4.3. Whitespace is ignored; an odd number of digits behaves as if a final 0 followed.
Result<Token> Lexer::lex_hex_string(std::uint64_t start) {
  reader_->advance();  // '<'
  std::string out;
  int high = -1;

  for (;;) {
    const int c = reader_->next();
    if (c == kEof) {
      return end_of_input_error("unterminated hexadecimal string", start);
    }
    if (c == '>') {
      break;
    }
    if (is_whitespace(c)) {
      continue;
    }
    const int value = hex_value(c);
    if (value < 0) {
      return make_error(ErrorCode::kSyntax, "invalid character in hexadecimal string", reader_->position() - 1);
    }
    if (high < 0) {
      high = value;
    } else {
      out.push_back(to_char((high * 16) + value));
      high = -1;
      if (out.size() > limits_.max_string_bytes) {
        return make_error(ErrorCode::kLimitExceeded, "hexadecimal string exceeds max_string_bytes", start);
      }
    }
  }
  if (high >= 0) {
    out.push_back(to_char(high * 16));
  }

  Token token = make_token(TokenKind::kString, start);
  token.bytes = std::move(out);
  token.string_form = StringForm::kHex;
  return token;
}

// §7.3.5. A name is '/' followed by regular characters; #xx is a byte in hexadecimal. The null
// byte is not allowed. "/" alone is a valid, empty name.
Result<Token> Lexer::lex_name(std::uint64_t start) {
  reader_->advance();  // '/'
  std::string out;

  while (is_regular(reader_->peek())) {
    const int c = reader_->next();
    if (c == '#') {
      const int high = hex_value(reader_->peek());
      const int low = hex_value(reader_->peek(1));
      if (high < 0 || low < 0) {
        return make_error(ErrorCode::kSyntax, "invalid #xx escape in name", reader_->position() - 1);
      }
      const int value = (high * 16) + low;
      if (value == 0) {
        return make_error(ErrorCode::kSyntax, "null byte (#00) in name", reader_->position() - 1);
      }
      reader_->advance(2);
      out.push_back(to_char(value));
    } else {
      out.push_back(to_char(c));
    }
    if (out.size() > limits_.max_token_bytes) {
      return make_error(ErrorCode::kLimitExceeded, "name exceeds max_token_bytes", start);
    }
  }
  if (reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  }

  Token token = make_token(TokenKind::kName, start);
  token.bytes = std::move(out);
  return token;
}

// A run of regular characters is a number (§7.3.3) when it starts with a digit, sign or '.',
// otherwise a keyword. Numbers are [+-]digits[.digits]; PDF has no exponent notation, so "1e5" or
// "1.2.3" are syntax errors rather than keywords.
Result<Token> Lexer::lex_number_or_keyword(std::uint64_t start) {
  std::string text;
  while (is_regular(reader_->peek())) {
    text.push_back(to_char(reader_->next()));
    if (text.size() > limits_.max_token_bytes) {
      return make_error(ErrorCode::kLimitExceeded, "token exceeds max_token_bytes", start);
    }
  }
  if (reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  }

  const int first = static_cast<unsigned char>(text.front());
  if (!is_digit(first) && first != '+' && first != '-' && first != '.') {
    Token token = make_token(TokenKind::kKeyword, start);
    token.bytes = std::move(text);
    return token;
  }

  // Validate the grammar before converting: from_chars would also accept exponents.
  std::string_view body = text;
  if (body.front() == '+' || body.front() == '-') {
    body.remove_prefix(1);
  }
  const auto digits = std::ranges::count_if(body, [](char ch) { return is_digit(static_cast<unsigned char>(ch)); });
  const auto dots = std::ranges::count(body, '.');
  if (digits == 0 || dots > 1 || static_cast<std::size_t>(digits + dots) != body.size()) {
    return make_error(ErrorCode::kSyntax, "malformed number '" + text + "'", start);
  }

  // from_chars rejects a leading '+'; it handles '-' itself (so INT64_MIN parses).
  std::string_view number = text;
  if (number.front() == '+') {
    number.remove_prefix(1);
  }
  const char* const first_char = number.data();
  const char* const last_char = number.data() + number.size();

  Token token = make_token(dots == 0 ? TokenKind::kInteger : TokenKind::kReal, start);
  const auto [end, ec] = dots == 0 ? std::from_chars(first_char, last_char, token.integer) : std::from_chars(first_char, last_char, token.real);
  if (ec == std::errc::result_out_of_range) {
    return make_error(ErrorCode::kLimitExceeded, "number out of range '" + text + "'", start);
  }
  if (ec != std::errc{} || end != last_char) {
    return make_error(ErrorCode::kSyntax, "malformed number '" + text + "'", start);
  }
  return token;
}

}  // namespace capriole::parser
