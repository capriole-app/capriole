#pragma once

// Object parser (ISO 32000-2 §7.3): builds model::Object values from lexer tokens.
//
// Strict, like the lexer: anything the grammar does not allow is an error at the offending offset.
// References "n g R" are returned as ObjectRef, never resolved here: resolving needs the xref, which
// is the document's job. Streams are located (offset + length of the encoded data), not read.

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// Strict follows the standard to the letter. Tolerant is used ONLY by the repair layer (repair/),
// after strict parsing failed, and accepts a closed list of common deviations, nothing else:
//   - duplicate dictionary keys: the last one wins;
//   - "stream" followed by spaces before the EOL, by a lone CR, or directly by the data;
//   - a missing, invalid or wrong /Length: the data ends at the next "endstream" (or at the end of
//     a file cut inside the stream);
//   - a missing or misspelt "endobj".
enum class ParseMode : std::uint8_t { kStrict, kTolerant };

class ObjectParser {
 public:
  // The reader must outlive the parser. Parsing starts at the reader's current position.
  explicit ObjectParser(SourceReader& reader, const ParseLimits& limits = kDefaultParseLimits, ParseMode mode = ParseMode::kStrict);

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
  // Tolerant mode: tokens where a dictionary key belongs, starting with `first` (see the .cpp).
  // Tolerant mode: after a token that cannot be lexed at all (a lone ')' or '>'), moves past its
  // first byte. Returns false (nothing done) for any other error, or in strict mode.
  bool skip_unlexable(const Error& error);
  Result<void> recover_stray_tokens(model::Dict& dict, const std::optional<std::string>& last_key, Token first, std::size_t depth);
  Result<model::Stream> parse_stream_data(model::Dict dict, std::uint64_t after_keyword, const LengthResolver& resolver);
  // The declared /Length of a stream whose data starts at data_offset.
  Result<std::uint64_t> declared_length(const model::Dict& dict, std::uint64_t data_offset, const LengthResolver& resolver);
  // Tolerant mode: the stream data ends right before the next "endstream" (minus one EOL).
  Result<std::uint64_t> length_by_scanning(std::uint64_t data_offset);

  SourceReader* reader_;
  Lexer lexer_;
  ParseLimits limits_;
  ParseMode mode_;
  std::deque<Result<Token>> lookahead_;
  // Tolerant mode: key/value pairs recovered from the tail of an array that lost its ']', for the
  // dictionary that holds the array (see parse_array).
  std::vector<std::pair<std::string, model::Object>> recovered_pairs_;
};

}  // namespace capriole::parser
