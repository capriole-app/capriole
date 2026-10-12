#include "capriole/content/content_parser.hpp"

#include <algorithm>
#include <string_view>
#include <utility>
#include <variant>

namespace capriole::content {

namespace {

using model::Object;

// White-space characters (§7.2.3, Table 1).
bool is_white(std::byte b) noexcept {
  const auto c = std::to_integer<unsigned char>(b);
  return c == 0x00 || c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20;
}

bool is_delimiter(std::byte b) noexcept {
  const auto c = std::to_integer<unsigned char>(b);
  return std::string_view("()<>[]{}/%").find(static_cast<char>(c)) != std::string_view::npos;
}

// Whether what follows an "EI" candidate reads as content (operators and operands are text): binary
// bytes mean the "EI" was part of the image data.
bool plausible_content_after(std::span<const std::byte> content, std::size_t from) noexcept {
  constexpr std::size_t kLookahead = 32;
  const std::size_t end = std::min(content.size(), from + kLookahead);
  for (std::size_t i = from; i < end; ++i) {
    const auto c = std::to_integer<unsigned char>(content[i]);
    if (!is_white(content[i]) && (c < 0x20 || c >= 0x7F)) {
      return false;
    }
  }
  return true;
}

// Where the image data that starts at start ends (the offset of its "EI"), or nullopt.
std::optional<std::size_t> find_image_end(std::span<const std::byte> content, std::size_t start) noexcept {
  for (std::size_t i = start; i + 1 < content.size(); ++i) {
    if (content[i] != std::byte{'E'} || content[i + 1] != std::byte{'I'}) {
      continue;
    }
    const bool before = i == start || is_white(content[i - 1]);
    const bool after = i + 2 == content.size() || is_white(content[i + 2]) || is_delimiter(content[i + 2]);
    if (before && after && plausible_content_after(content, i + 2)) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

ContentParser::ContentParser(std::span<const std::byte> content, const ParseLimits& limits, parser::ParseMode mode)
    : content_(content), source_(content), reader_(source_), parser_(reader_, limits, mode), mode_(mode) {}

Result<void> ContentParser::recover(const Error& error) {
  // Only a syntax error at a known place is skipped. An error that ran into the end of the input (a
  // string, array or dictionary never closed) ends the content instead: skipping one byte and trying
  // again would scan the rest of the input once per byte.
  if (mode_ != parser::ParseMode::kTolerant || error.code != ErrorCode::kSyntax || error.offset == Error::kNoOffset) {
    return std::unexpected(error);
  }
  ++recovered_;
  return parser_.seek(error.offset + 1);
}

Result<std::optional<Operation>> ContentParser::next() {
  std::vector<Object> operands;
  for (;;) {
    auto item = parser_.parse_content_item();
    if (!item) {
      if (mode_ == parser::ParseMode::kTolerant && item.error().code == ErrorCode::kUnexpectedEof) {
        ++recovered_;
        return std::nullopt;  // an unterminated operand at the end: what came before stands
      }
      if (auto recovered = recover(item.error()); !recovered) {
        return std::unexpected(std::move(recovered.error()));
      }
      operands.clear();
      continue;
    }
    if (std::holds_alternative<std::monostate>(*item)) {
      return std::nullopt;
    }
    if (auto* object = std::get_if<Object>(&*item)) {
      if (operands.size() >= kMaxOperands) {
        if (mode_ != parser::ParseMode::kTolerant) {
          return make_error(ErrorCode::kLimitExceeded, "more than " + std::to_string(kMaxOperands) + " operands before an operator", reader_.position());
        }
        ++recovered_;
        operands.clear();
      }
      operands.push_back(std::move(*object));
      continue;
    }

    auto& token = std::get<parser::Token>(*item);
    if (token.bytes == "BI") {
      auto image = read_inline_image(token.offset);
      if (!image) {
        if (auto recovered = recover(image.error()); !recovered) {
          return std::unexpected(std::move(recovered.error()));
        }
        operands.clear();
        continue;
      }
      image->operands = std::move(operands);
      return std::move(*image);
    }
    Operation operation;
    const OperatorInfo* known = find_operator(token.bytes);
    operation.op = known != nullptr ? known->op : Operator::kUnknown;
    operation.name = std::move(token.bytes);
    operation.operands = std::move(operands);
    operation.offset = token.offset;
    return operation;
  }
}

Result<Operation> ContentParser::read_inline_image(std::uint64_t bi_offset) {
  // The dictionary: key/value pairs up to the ID operator.
  std::vector<Object> entries;
  std::uint64_t data_start = 0;
  for (;;) {
    auto item = parser_.parse_content_item();
    if (!item) {
      return std::unexpected(std::move(item.error()));
    }
    if (std::holds_alternative<std::monostate>(*item)) {
      return make_error(ErrorCode::kUnexpectedEof, "inline image without ID", bi_offset);
    }
    if (auto* object = std::get_if<Object>(&*item)) {
      if (entries.size() >= 2 * kMaxOperands) {
        return make_error(ErrorCode::kLimitExceeded, "inline image dictionary too large", bi_offset);
      }
      entries.push_back(std::move(*object));
      continue;
    }
    const auto& token = std::get<parser::Token>(*item);
    if (token.bytes != "ID") {
      return make_error(ErrorCode::kSyntax, "operator " + token.bytes + " inside an inline image dictionary", token.offset);
    }
    data_start = token.offset + token.length;
    break;
  }
  auto image = std::make_shared<InlineImage>();
  if (entries.size() % 2 != 0) {
    return make_error(ErrorCode::kSyntax, "inline image dictionary with a key and no value", bi_offset);
  }
  for (std::size_t i = 0; i < entries.size(); i += 2) {
    const model::Name* key = entries[i].as_name();
    if (key == nullptr) {
      return make_error(ErrorCode::kSyntax, "inline image dictionary key is not a name", bi_offset);
    }
    image->dict.set(key->bytes, std::move(entries[i + 1]));
  }

  // The data: after one white-space byte, up to "EI" (§8.9.7).
  std::size_t start = static_cast<std::size_t>(std::min<std::uint64_t>(data_start, content_.size()));
  if (start < content_.size() && is_white(content_[start])) {
    ++start;
  }
  std::optional<std::size_t> end;
  // PDF 2.0 declares the length (/L or /Length): trusted when an EI is where it says.
  const Object* declared = image->dict.find("L") != nullptr ? image->dict.find("L") : image->dict.find("Length");
  if (const auto length = declared != nullptr ? declared->as_integer() : std::nullopt;
      length && *length >= 0 && std::cmp_less_equal(*length, content_.size() - start)) {
    const std::size_t data_end = start + static_cast<std::size_t>(*length);
    std::size_t at = data_end;
    while (at < content_.size() && is_white(content_[at])) {
      ++at;
    }
    if (at + 1 < content_.size() && content_[at] == std::byte{'E'} && content_[at + 1] == std::byte{'I'}) {
      const auto data = content_.subspan(start, data_end - start);
      image->data.assign(data.begin(), data.end());
      end = at;
    }
  }
  if (!end) {
    end = find_image_end(content_, start);
    if (!end) {
      if (mode_ != parser::ParseMode::kTolerant) {
        return make_error(ErrorCode::kSyntax, "inline image without EI", bi_offset);
      }
      ++recovered_;
      end = content_.size();  // the data runs to the end of the content
    }
    std::size_t data_end = *end;
    if (data_end > start && is_white(content_[data_end - 1])) {
      --data_end;  // the white-space before EI separates, it is not data
    }
    const auto data = content_.subspan(start, data_end - start);
    image->data.assign(data.begin(), data.end());
  }
  if (auto moved = parser_.seek(std::min<std::uint64_t>(*end + 2, content_.size())); !moved) {
    return std::unexpected(std::move(moved.error()));
  }

  Operation operation;
  operation.op = Operator::kInlineImage;
  operation.name = "BI";
  operation.offset = bi_offset;
  operation.image = std::move(image);
  return operation;
}

Result<std::vector<Operation>> parse_content(std::span<const std::byte> content, const ParseLimits& limits, parser::ParseMode mode) {
  ContentParser parser(content, limits, mode);
  std::vector<Operation> operations;
  for (;;) {
    auto operation = parser.next();
    if (!operation) {
      return std::unexpected(std::move(operation.error()));
    }
    if (!*operation) {
      return operations;
    }
    operations.push_back(std::move(**operation));
  }
}

}  // namespace capriole::content
