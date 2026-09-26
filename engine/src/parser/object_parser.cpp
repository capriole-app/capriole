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

ObjectParser::ObjectParser(SourceReader& reader, const ParseLimits& limits) : reader_(&reader), lexer_(reader, limits), limits_(limits) {}

Result<void> ObjectParser::seek(std::uint64_t offset) {
  lookahead_.clear();
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

Result<Object> ObjectParser::parse_object() {
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
  for (;;) {
    auto key = next_token();
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
      return make_error(ErrorCode::kSyntax, "dictionary key must be a name, found " + describe(*key), key->offset);
    }
    if (dict.contains(key->bytes)) {
      // §7.3.7: keys are unique. Which duplicate wins is a repair decision, not the parser's.
      return make_error(ErrorCode::kSyntax, "duplicate dictionary key /" + key->bytes, key->offset);
    }

    auto value_token = next_token();
    if (!value_token) {
      return std::unexpected(std::move(value_token.error()));
    }
    if (value_token->kind == TokenKind::kDictClose) {
      return make_error(ErrorCode::kSyntax, "missing value for key /" + key->bytes, value_token->offset);
    }
    auto value = parse_value(std::move(*value_token), depth);
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    dict.set(std::move(key->bytes), std::move(*value));
  }
}

Result<IndirectObject> ObjectParser::parse_indirect_object(const LengthResolver& resolver) {
  // "n g obj"
  auto number = next_token();
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
  if (number->integer < 1 || !std::in_range<std::uint32_t>(number->integer) || !std::in_range<std::uint16_t>(generation->integer)) {
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
    return make_error(end->kind == TokenKind::kEnd ? ErrorCode::kUnexpectedEof : ErrorCode::kSyntax, "expected endobj, found " + describe(*end), end->offset);
  }
  result.end_offset = end->offset + end->length;
  return result;
}

// §7.3.8.1: "stream" is followed by CR LF or LF (a lone CR is not allowed), then exactly /Length
// bytes of data, an optional EOL, and "endstream".
Result<model::Stream> ObjectParser::parse_stream_data(model::Dict dict, std::uint64_t after_keyword, const LengthResolver& resolver) {
  if (auto moved = seek(after_keyword); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  const int c = reader_->peek();
  if (c == '\r' && reader_->peek(1) == '\n') {
    reader_->advance(2);
  } else if (c == '\n') {
    reader_->advance();
  } else if (c == SourceReader::kEof && reader_->io_error()) {
    return std::unexpected(*reader_->io_error());
  } else {
    return make_error(ErrorCode::kSyntax, "'stream' must be followed by CR LF or LF", after_keyword);
  }
  const std::uint64_t data_offset = reader_->position();

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

  // The resolver may have moved the reader: position explicitly after the data.
  if (auto moved = seek(data_offset + data_length); !moved) {
    return std::unexpected(std::move(moved.error()));
  }
  auto end = next_token();
  if (!end) {
    return std::unexpected(std::move(end.error()));
  }
  if (!is_keyword(*end, "endstream")) {
    return make_error(ErrorCode::kSyntax, "expected endstream after /Length bytes of data, found " + describe(*end), end->offset);
  }
  return model::Stream{.dict = std::move(dict), .data_offset = data_offset, .data_length = data_length};
}

}  // namespace capriole::parser
