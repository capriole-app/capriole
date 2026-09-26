// Fuzz target for the lexer. The seed corpus is fuzz/corpus/lexer/.
//
// Beyond "no crash, no UB", it checks invariants the object parser will rely on:
//   - every token lies inside the input and tokens never overlap or go backwards;
//   - the result does not depend on the reader's window size (differential check: a tiny window
//     forces refills at every possible boundary).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/parser/lexer.hpp"

namespace {

using capriole::parser::Token;
using capriole::parser::TokenKind;

struct LexResult {
  std::vector<Token> tokens;
  bool failed = false;
  capriole::ErrorCode error{};
  std::uint64_t error_offset = 0;
};

LexResult lex_all(std::span<const std::byte> bytes, std::size_t window) {
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  capriole::SourceReader reader(source, window);
  capriole::parser::Lexer lexer(reader);
  LexResult result;
  std::uint64_t previous_end = 0;
  for (;;) {
    auto token = lexer.next();
    if (!token) {
      result.failed = true;
      result.error = token.error().code;
      result.error_offset = token.error().offset;
      return result;
    }
    if (token->kind == TokenKind::kEnd) {
      return result;
    }
    const std::uint64_t end = token->offset + token->length;
    if (token->length == 0 || token->offset < previous_end || end > bytes.size()) {
      __builtin_trap();
    }
    previous_end = end;
    result.tokens.push_back(std::move(*token));
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  const LexResult big = lex_all(bytes, capriole::SourceReader::kDefaultWindow);
  const LexResult small = lex_all(bytes, capriole::SourceReader::kMinWindow);

  if (big.failed != small.failed || big.error != small.error || big.error_offset != small.error_offset || big.tokens.size() != small.tokens.size()) {
    __builtin_trap();
  }
  for (std::size_t i = 0; i < big.tokens.size(); ++i) {
    const Token& a = big.tokens[i];
    const Token& b = small.tokens[i];
    // Exact comparison of reals is intended: the same text must convert to the same double.
    if (a.kind != b.kind || a.offset != b.offset || a.length != b.length || a.integer != b.integer || a.real != b.real || a.bytes != b.bytes ||
        a.string_form != b.string_form) {
      __builtin_trap();
    }
  }
  return 0;
}
