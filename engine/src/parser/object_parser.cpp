#include "capriole/parser/object_parser.hpp"

#include <string>
#include <utility>

namespace capriole::parser {

namespace {

using model::Object;

bool is_keyword(const Token& token, std::string_view text) { return token.kind == TokenKind::kKeyword && token.bytes == text; }

// Printable description of a token for error messages.
std::string describe(const Token& token) {
  switch (token.kind) {
    case TokenKind::kKeyword:
      return "keyword '" + token.bytes + "'";
    case TokenKind::kArrayClose:
      return "']'";
    case TokenKind::kDictClose:
      return "'>>'";
    case TokenKind::kBraceOpen:
      return "'{'";
    case TokenKind::kBraceClose:
      return "'}'";
    default:
      return std::string(to_string(token.kind));
  }
}

}  // namespace

ObjectParser::ObjectParser(SourceReader& reader, const ParseLimits& limits, ParseMode mode)
    : reader_(&reader), lexer_(reader, limits), limits_(limits), mode_(mode) {}

Result<void> ObjectParser::seek(std::uint64_t offset) {
  lookahead_.clear();
  recovered_pairs_.clear();
  return reader_->seek(offset);
}

Result<Token> ObjectParser::next_token() {
  if (lookahead_.empty()) {
    return lexer_.next();
  }
  Result<Token> token = std::move(lookahead_.front());
  lookahead_.pop_front();
  return token;
}

const Result<Token>& ObjectParser::peek_token(std::size_t index) {
  while (lookahead_.size() <= index) {
    lookahead_.push_back(lexer_.next());
  }
  return lookahead_[index];
}

Result<ObjectParser::ContentItem> ObjectParser::parse_content_item() {
  auto token = next_token();
  if (!token) {
    return std::unexpected(std::move(token.error()));
  }
  if (token->kind == TokenKind::kEnd) {
    return ContentItem{};
  }
  if (token->kind == TokenKind::kKeyword && token->bytes != "true" && token->bytes != "false" && token->bytes != "null") {
    return ContentItem{std::move(*token)};
  }
  auto value = parse_value(std::move(*token), 0);
  if (!value) {
    return std::unexpected(std::move(value.error()));
  }
  return ContentItem{std::move(*value)};
}

Result<Object> ObjectParser::parse_object() {
  recovered_pairs_.clear();  // pairs recovered inside an earlier object never reach this one
  auto token = next_token();
  if (!token) {
    return std::unexpected(token.error());
  }
  return parse_value(std::move(*token), 0);
}

Result<Object> ObjectParser::parse_value(Token token, std::size_t depth) {
  switch (token.kind) {
    case TokenKind::kInteger:
      return parse_integer_or_reference(token);
    case TokenKind::kReal:
      return Object(token.real);
    case TokenKind::kString:
      return Object(model::String{.bytes = std::move(token.bytes),
                                  .form = token.string_form == StringForm::kHex ? model::StringForm::kHex : model::StringForm::kLiteral});
    case TokenKind::kName:
      return Object(model::Name{.bytes = std::move(token.bytes)});
    case TokenKind::kArrayOpen:
      return parse_array(depth + 1);
    case TokenKind::kDictOpen: {
      auto dict = parse_dict(depth + 1);
      if (!dict) {
        return std::unexpected(std::move(dict.error()));
      }
      return Object(std::move(*dict));
    }
    case TokenKind::kKeyword:
      if (token.bytes == "true") {
        return Object(true);
      }
      if (token.bytes == "false") {
        return Object(false);
      }
      if (token.bytes == "null") {
        return Object(model::Null{});
      }
      return make_error(ErrorCode::kSyntax, "unexpected " + describe(token) + ", expected an object", token.offset);
    case TokenKind::kEnd:
      return make_error(ErrorCode::kUnexpectedEof, "unexpected end of input, expected an object", token.offset);
    default:
      return make_error(ErrorCode::kSyntax, "unexpected " + describe(token) + ", expected an object", token.offset);
  }
}

// "n g R" is three tokens; an integer is a reference only if the next two are an integer and R.
Result<Object> ObjectParser::parse_integer_or_reference(const Token& first) {
  const auto& second = peek_token(0);
  if (!second || second->kind != TokenKind::kInteger) {
    return Object(first.integer);
  }
  const auto& third = peek_token(1);
  if (!third || !is_keyword(*third, "R")) {
    return Object(first.integer);
  }

  const std::int64_t number = first.integer;
  const std::int64_t generation = second->integer;
  // Object 0 is the head of the free list and never a valid target (§7.5.4).
  if (number < 1 || !std::in_range<std::uint32_t>(number) || !std::in_range<std::uint16_t>(generation)) {
    return make_error(ErrorCode::kSyntax, "invalid indirect reference " + std::to_string(number) + " " + std::to_string(generation) + " R", first.offset);
  }
  lookahead_.pop_front();
  lookahead_.pop_front();
  return Object(model::ObjectRef{.number = static_cast<std::uint32_t>(number), .generation = static_cast<std::uint16_t>(generation)});
}

Result<Object> ObjectParser::parse_array(std::size_t depth) {
  if (depth > limits_.max_nesting_depth) {
    return make_error(ErrorCode::kLimitExceeded, "nesting deeper than max_nesting_depth", reader_->position());
  }
  model::Array array;
  for (;;) {
    auto token = next_token();
    if (!token) {
      return std::unexpected(std::move(token.error()));
    }
    if (token->kind == TokenKind::kArrayClose) {
      return Object(std::move(array));
    }
    if (token->kind == TokenKind::kEnd) {
      return make_error(ErrorCode::kUnexpectedEof, "unterminated array", token->offset);
    }
    if (token->kind == TokenKind::kDictClose && mode_ == ParseMode::kTolerant) {
      // Repair: the '>>' of the enclosing dictionary inside an array — the array lost its ']' and ran
      // on into the dictionary's next keys. It ends at the first name followed by something that is
      // not a name, when what follows is all key/value pairs ("/MediaBox [0 0 612 792 /Resources <<…>>
      // /Type /Page >>"; "/ProcSet [/PDF /Text /ExtGState <<…>> >>" keeps its names). The pairs go
      // back to the dictionary, and so does the '>>'.
      lookahead_.emplace_front(std::move(*token));
      for (std::size_t i = 0; i + 1 < array.size(); ++i) {
        const bool pairs_follow = (array.size() - i) % 2 == 0 && [&] {
          for (std::size_t k = i; k < array.size(); k += 2) {
            if (array[k].as_name() == nullptr) {
              return false;
            }
          }
          return true;
        }();
        if (array[i].as_name() != nullptr && array[i + 1].as_name() == nullptr && pairs_follow) {
          for (std::size_t k = i; k < array.size(); k += 2) {
            recovered_pairs_.emplace_back(array[k].as_name()->bytes, std::move(array[k + 1]));
          }
          array.resize(i);
          break;
        }
      }
      return Object(std::move(array));
    }
    auto value = parse_value(std::move(*token), depth);
    if (!value) {
      return value;
    }
    array.push_back(std::move(*value));
  }
}

Result<model::Dict> ObjectParser::parse_dict(std::size_t depth) {
  if (depth > limits_.max_nesting_depth) {
    return make_error(ErrorCode::kLimitExceeded, "nesting deeper than max_nesting_depth", reader_->position());
  }
  model::Dict dict;
  std::optional<std::string> last_key;
  for (;;) {
    auto key = next_token();
    if (!key && skip_unlexable(key.error())) {
      continue;  // e.g. the ')' of a string that lost its '(' — the next key follows
    }
    if (!key) {
      return std::unexpected(std::move(key.error()));
    }
    if (key->kind == TokenKind::kDictClose) {
      return dict;
    }
    if (key->kind == TokenKind::kEnd) {
      return make_error(ErrorCode::kUnexpectedEof, "unterminated dictionary", key->offset);
    }
    if (key->kind != TokenKind::kName) {
      if (mode_ != ParseMode::kTolerant) {
        return make_error(ErrorCode::kSyntax, "dictionary key must be a name, found " + describe(*key), key->offset);
      }
      if (auto recovered = recover_stray_tokens(dict, last_key, std::move(*key), depth); !recovered) {
        return std::unexpected(std::move(recovered.error()));
      }
      continue;
    }
    if (dict.contains(key->bytes) && mode_ == ParseMode::kStrict) {
      // §7.3.7: keys are unique. Which duplicate wins is a repair decision (tolerant mode: the last).
      return make_error(ErrorCode::kSyntax, "duplicate dictionary key /" + key->bytes, key->offset);
    }

    auto value_token = next_token();
    if (!value_token) {
      return std::unexpected(std::move(value_token.error()));
    }
    if (value_token->kind == TokenKind::kDictClose) {
      if (mode_ == ParseMode::kTolerant) {
        // Repair: a last key with no value ("<</Markedtrue>>": a value run into its key): left out, as a
        // null value would be (§7.3.7).
        return dict;
      }
      return make_error(ErrorCode::kSyntax, "missing value for key /" + key->bytes, value_token->offset);
    }
    if (value_token->kind == TokenKind::kArrayClose && mode_ == ParseMode::kTolerant) {
      // Repair: "/ProcSet /PDF /Text ]" — an array of names that lost its '[': the "key" is the last
      // element of the previous value's array.
      Object* previous = last_key ? dict.find(*last_key) : nullptr;
      if (previous != nullptr && previous->as_name() != nullptr) {
        *previous = Object(model::Array{std::move(*previous), Object(model::Name{.bytes = std::move(key->bytes)})});
      } else if (previous != nullptr && previous->as_array() != nullptr) {
        previous->as_array()->emplace_back(model::Name{.bytes = std::move(key->bytes)});
      }
      continue;
    }
    const bool word = value_token->kind == TokenKind::kKeyword;
    auto value = parse_value(std::move(*value_token), depth);
    if (!value && word && value.error().code == ErrorCode::kSyntax && mode_ == ParseMode::kTolerant) {
      // A bare word where a value belongs (e.g. a string that lost its parentheses): null, and the
      // words after it are skipped as stray tokens.
      value = Object(model::Null{});
    }
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    last_key = key->bytes;
    dict.set(std::move(key->bytes), std::move(*value));
    for (auto& [recovered_key, recovered_value] : recovered_pairs_) {
      dict.set(std::move(recovered_key), std::move(recovered_value));
    }
    recovered_pairs_.clear();
  }
}

// Tolerant mode, the repair of a dictionary with tokens where a key belongs:
// - values up to a ']' are an array that lost its '[', together with the value before them when that
//   is a plain value ("/MediaBox 0 0 612 792 ]" -> [0 0 612 792]);
// - "R" after an integer value is a reference that lost its generation ("/Parent 66 R" -> 66 0 R);
// - anything else is skipped up to the next key or the end of the dictionary.
Result<void> ObjectParser::recover_stray_tokens(model::Dict& dict, const std::optional<std::string>& last_key, Token first, std::size_t depth) {
  Object* previous = last_key ? dict.find(*last_key) : nullptr;
  const auto plain = [](const Object& object) {
    switch (object.kind()) {
      case model::ObjectKind::kBoolean:
      case model::ObjectKind::kInteger:
      case model::ObjectKind::kReal:
      case model::ObjectKind::kString:
      case model::ObjectKind::kName:
      case model::ObjectKind::kReference:
        return true;
      default:
        return false;
    }
  };
  model::Array values;
  bool garbage = false;
  Token token = std::move(first);
  for (;;) {
    if (token.kind == TokenKind::kName || token.kind == TokenKind::kDictClose) {
      lookahead_.emplace_front(std::move(token));  // the next key, or the end: parse_dict goes on
      return {};
    }
    if (token.kind == TokenKind::kEnd) {
      return make_error(ErrorCode::kUnexpectedEof, "unterminated dictionary", token.offset);
    }
    if (token.kind == TokenKind::kArrayClose) {
      if (!garbage && !values.empty() && previous != nullptr && plain(*previous)) {
        values.insert(values.begin(), std::move(*previous));
        *previous = Object(std::move(values));
      }
      values.clear();
      garbage = false;
    } else if (is_keyword(token, "R") && values.empty() && previous != nullptr && previous->as_integer() &&
               std::in_range<std::uint32_t>(*previous->as_integer()) && *previous->as_integer() > 0) {
      *previous = Object(model::ObjectRef{.number = static_cast<std::uint32_t>(*previous->as_integer()), .generation = 0});
    } else {
      auto value = parse_value(std::move(token), depth);
      if (value) {
        values.push_back(std::move(*value));
      } else if (value.error().code == ErrorCode::kSyntax) {
        garbage = true;
      } else {
        return std::unexpected(std::move(value.error()));
      }
    }
    auto next = next_token();
    while (!next && skip_unlexable(next.error())) {
      next = next_token();
    }
    if (!next) {
      return std::unexpected(std::move(next.error()));
    }
    token = std::move(*next);
  }
}

bool ObjectParser::skip_unlexable(const Error& error) {
  if (mode_ != ParseMode::kTolerant || error.code != ErrorCode::kSyntax || error.offset == Error::kNoOffset) {
    return false;
  }
  // Tokens already looked ahead were lexed from positions around the bad byte: lex them again.
  lookahead_.clear();
  return reader_->seek(error.offset + 1).has_value();
}

Result<IndirectObject> ObjectParser::parse_indirect_object(const LengthResolver& resolver) {
  recovered_pairs_.clear();
  // "n g obj"
  auto number = next_token();
  // Tolerant mode: a number that cannot be an object number (0, or too large: digits stuck in front of
  // it, even past 64 bits) is unknown — 0 in the result; the cross-reference entry that led here says
  // which object it is.
  const bool overflowing = !number && number.error().code == ErrorCode::kLimitExceeded;
  if (overflowing && mode_ != ParseMode::kTolerant) {
    // Like any number too large to be an object number: an invalid header, not a resource limit.
    return make_error(ErrorCode::kSyntax, "invalid object number (" + number.error().message + ")", number.error().offset);
  }
  const bool unknown_number =
      mode_ == ParseMode::kTolerant &&
      (overflowing || (number && number->kind == TokenKind::kInteger && (number->integer < 1 || !std::in_range<std::uint32_t>(number->integer))));
  if (unknown_number) {
    Token unknown;
    unknown.kind = TokenKind::kInteger;
    unknown.offset = number ? number->offset : number.error().offset;
    number = std::move(unknown);
  }
  if (!number) {
    return std::unexpected(std::move(number.error()));
  }
  auto generation = next_token();
  if (!generation) {
    return std::unexpected(std::move(generation.error()));
  }
  auto obj = next_token();
  if (!obj) {
    return std::unexpected(std::move(obj.error()));
  }
  if (number->kind != TokenKind::kInteger || generation->kind != TokenKind::kInteger || !is_keyword(*obj, "obj")) {
    return make_error(ErrorCode::kSyntax, "expected 'n g obj'", number->offset);
  }
  if ((number->integer < 1 && !unknown_number) || !std::in_range<std::uint32_t>(number->integer) || !std::in_range<std::uint16_t>(generation->integer)) {
    return make_error(ErrorCode::kSyntax, "invalid object number or generation", number->offset);
  }

  IndirectObject result;
  result.offset = number->offset;
  result.ref = model::ObjectRef{.number = static_cast<std::uint32_t>(number->integer), .generation = static_cast<std::uint16_t>(generation->integer)};

  auto first = next_token();
  if (!first) {
    return std::unexpected(std::move(first.error()));
  }
  if (first->kind == TokenKind::kDictOpen) {
    auto dict = parse_dict(1);
    if (!dict) {
      return std::unexpected(std::move(dict.error()));
    }
    const auto& after = peek_token(0);
    if (after && is_keyword(*after, "stream")) {
      const std::uint64_t after_keyword = after->offset + after->length;
      auto stream = parse_stream_data(std::move(*dict), after_keyword, resolver);
      if (!stream) {
        return std::unexpected(std::move(stream.error()));
      }
      stream->origin = result.ref;
      result.object = Object(std::move(*stream));
    } else {
      result.object = Object(std::move(*dict));
    }
  } else {
    auto value = parse_value(std::move(*first), 0);
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    result.object = std::move(*value);
  }

  auto end = next_token();
  if (!end) {
    return std::unexpected(std::move(end.error()));
  }
  if (!is_keyword(*end, "endobj")) {
    if (mode_ == ParseMode::kTolerant) {
      result.end_offset = end->offset;  // the object is complete; whatever follows is not ours
      return result;
    }
    return make_error(end->kind == TokenKind::kEnd ? ErrorCode::kUnexpectedEof : ErrorCode::kSyntax, "expected endobj, found " + describe(*end), end->offset);
  }
  result.end_offset = end->offset + end->length;
  return result;
}

// §7.3.8.1: "stream" is followed by CR LF or LF (a lone CR is not allowed), then exactly /Length
// bytes of data, an optional EOL, and "endstream".
Result<model::Stream> ObjectParser::parse_stream_data(model::Dict dict, std::uint64_t after_keyword, const LengthResolver& resolver) {
  const bool tolerant = mode_ == ParseMode::kTolerant;
  if (auto moved = seek(after_keyword); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  if (tolerant) {
    while (reader_->peek() == ' ' || reader_->peek() == '\t') {
      reader_->advance();
    }
  }
  const int c = reader_->peek();
  if (c == '\r' && reader_->peek(1) == '\n') {
    reader_->advance(2);
  } else if (c == '\n' || (tolerant && c == '\r')) {  // a lone CR only in tolerant mode
    reader_->advance();
  } else if (c == SourceReader::kEof && reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  } else if (!tolerant) {
    return make_error(ErrorCode::kSyntax, "'stream' must be followed by CR LF or LF", after_keyword);
  }
  const std::uint64_t data_offset = reader_->position();

  auto length = declared_length(dict, data_offset, resolver);
  if (length) {
    // The resolver may have moved the reader: position explicitly after the data.
    if (auto moved = seek(data_offset + *length); !moved) {
      return std::unexpected(std::move(moved.error()));
    }
    auto end = next_token();
    if (end && is_keyword(*end, "endstream")) {
      return model::Stream{.dict = std::move(dict), .data_offset = data_offset, .data_length = *length, .memory_data = nullptr, .origin = std::nullopt};
    }
    if (!tolerant) {
      if (!end) {
        return std::unexpected(std::move(end.error()));
      }
      return make_error(ErrorCode::kSyntax, "expected endstream after /Length bytes of data, found " + describe(*end), end->offset);
    }
  } else if (!tolerant || length.error().code == ErrorCode::kIo) {
    return std::unexpected(std::move(length.error()));
  }

  // Tolerant: /Length is missing, invalid or wrong; the data ends at the next "endstream".
  auto scanned = length_by_scanning(data_offset);
  if (!scanned) {
    return std::unexpected(std::move(scanned.error()));
  }
  return model::Stream{.dict = std::move(dict), .data_offset = data_offset, .data_length = *scanned, .memory_data = nullptr, .origin = std::nullopt};
}

Result<std::uint64_t> ObjectParser::declared_length(const model::Dict& dict, std::uint64_t data_offset, const LengthResolver& resolver) {
  const Object* length_object = dict.find("Length");
  if (length_object == nullptr) {
    return make_error(ErrorCode::kSyntax, "stream dictionary has no /Length", data_offset);
  }
  std::int64_t length = 0;
  if (const auto direct = length_object->as_integer()) {
    length = *direct;
  } else if (const auto ref = length_object->as_ref()) {
    if (!resolver) {
      return make_error(ErrorCode::kUnsupported, "indirect /Length needs a resolver", data_offset);
    }
    auto resolved = resolver(*ref);
    if (!resolved) {
      return std::unexpected(std::move(resolved.error()));
    }
    length = *resolved;
  } else {
    return make_error(ErrorCode::kSyntax, "/Length is not an integer", data_offset);
  }
  if (length < 0) {
    return make_error(ErrorCode::kSyntax, "negative stream /Length", data_offset);
  }
  const auto data_length = static_cast<std::uint64_t>(length);
  if (data_length > reader_->size() - data_offset) {
    return make_error(ErrorCode::kOutOfRange, "stream data extends beyond end of input", data_offset);
  }
  return data_length;
}

Result<std::uint64_t> ObjectParser::length_by_scanning(std::uint64_t data_offset) {
  constexpr std::string_view kKeyword = "endstream";
  if (auto moved = seek(data_offset); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  std::size_t matched = 0;
  for (int c = reader_->next(); c != SourceReader::kEof; c = reader_->next()) {
    if (std::cmp_equal(c, static_cast<unsigned char>(kKeyword[matched]))) {
      if (++matched == kKeyword.size()) {
        const std::uint64_t keyword_offset = reader_->position() - kKeyword.size();
        std::uint64_t end = keyword_offset;
        // The EOL before "endstream" is not part of the data.
        if (end > data_offset && reader_->seek(end - 1).has_value() && reader_->peek() == '\n') {
          --end;
        }
        if (end > data_offset && reader_->seek(end - 1).has_value() && reader_->peek() == '\r') {
          --end;
        }
        if (auto moved = seek(keyword_offset + kKeyword.size()); !moved) {
          return std::unexpected(std::move(moved.error()));
        }
        return end - data_offset;
      }
    } else {
      matched = std::cmp_equal(c, static_cast<unsigned char>(kKeyword[0])) ? 1 : 0;
    }
  }
  if (reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  }
  // No "endstream" at all: the file was cut inside the stream. The data runs to the end of the file
  // (the repair layer's partial decoding keeps what of it decodes).
  return reader_->size() - data_offset;
}

}  // namespace capriole::parser
