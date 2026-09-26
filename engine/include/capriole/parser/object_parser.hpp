#pragma once

// Object parser (ISO 32000-2 §7.3): builds model::Object values from lexer tokens.
//
// Strict, like the lexer: anything the grammar does not allow is an error at the offending offset.
// References "n g R" are returned as ObjectRef, never resolved here: resolving needs the xref, which
// is the document's job. Streams are located (offset + length of the encoded data), not read.

#include <cstdint>
#include <deque>
#include <functional>

#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/model/object.hpp"
#include "capriole/parser/lexer.hpp"

namespace capriole::parser {

// Resolves an indirect /Length (e.g. "/Length 12 0 R") to its integer value. It may freely use the
// same SourceReader: the parser saves and restores its own position around the call.
using LengthResolver = std::function<Result<std::int64_t>(model::ObjectRef)>;

// An indirect object definition "n g obj ... endobj" (§7.3.10).
struct IndirectObject {
  model::ObjectRef ref;
  model::Object object;
  std::uint64_t offset = 0;      // first byte of "n g obj"
  std::uint64_t end_offset = 0;  // one past "endobj"
};

class ObjectParser {
 public:
  // The reader must outlive the parser. Parsing starts at the reader's current position.
  explicit ObjectParser(SourceReader& reader, const ParseLimits& limits = kDefaultParseLimits);

  // Parses one direct object. Streams only exist as indirect objects: after a dictionary, a following
  // `stream` keyword is left unconsumed (use parse_indirect_object for object definitions).
  [[nodiscard]] Result<model::Object> parse_object();

  // Parses an indirect object definition at the current position, stream included. An indirect
  // /Length is resolved through resolver; without one it is ErrorCode::kUnsupported.
  [[nodiscard]] Result<IndirectObject> parse_indirect_object(const LengthResolver& resolver = {});

  // Repositions the parser (and its reader) at offset, discarding any lookahead.
  [[nodiscard]] Result<void> seek(std::uint64_t offset);

 private:
  Result<Token> next_token();
  // Token at lookahead index (0 = next). Lexing errors are stored and only surface when consumed,
  // so peeking past the end of an object never fails a parse that did not need that token.
  const Result<Token>& peek_token(std::size_t index);

  Result<model::Object> parse_value(Token token, std::size_t depth);
  Result<model::Object> parse_integer_or_reference(const Token& first);
  Result<model::Object> parse_array(std::size_t depth);
  Result<model::Dict> parse_dict(std::size_t depth);
  Result<model::Stream> parse_stream_data(model::Dict dict, std::uint64_t after_keyword, const LengthResolver& resolver);

  SourceReader* reader_;
  Lexer lexer_;
  ParseLimits limits_;
  std::deque<Result<Token>> lookahead_;
};

}  // namespace capriole::parser
