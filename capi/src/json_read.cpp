#include "json_read.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <utility>

namespace capriole::capi {

const JsonObject* JsonValue::object() const {
  const auto* p = std::get_if<std::shared_ptr<JsonObject>>(&value);
  return p != nullptr ? p->get() : nullptr;
}

const JsonArray* JsonValue::array() const {
  const auto* p = std::get_if<std::shared_ptr<JsonArray>>(&value);
  return p != nullptr ? p->get() : nullptr;
}

std::optional<double> JsonValue::number() const {
  const auto* p = std::get_if<double>(&value);
  return p != nullptr ? std::optional<double>(*p) : std::nullopt;
}

const std::string* JsonValue::string() const { return std::get_if<std::string>(&value); }

std::optional<bool> JsonValue::boolean() const {
  const auto* p = std::get_if<bool>(&value);
  return p != nullptr ? std::optional<bool>(*p) : std::nullopt;
}

const JsonValue* JsonValue::get(std::string_view key) const {
  const JsonObject* o = object();
  if (o == nullptr) {
    return nullptr;
  }
  const auto found = o->find(key);
  return found != o->end() ? &found->second : nullptr;
}

namespace {

class Reader {
 public:
  Reader(std::string_view text, const JsonLimits& limits) : text_(text), limits_(&limits) {}

  Result<JsonValue> document() {
    auto value = parse(0);
    if (!value) {
      return value;
    }
    skip_space();
    if (at_ != text_.size()) {
      return fail("text after the value");
    }
    return value;
  }

 private:
  [[nodiscard]] Result<JsonValue> fail(std::string_view what) const {
    return std::unexpected(Error{.code = ErrorCode::kSyntax, .message = "JSON: " + std::string(what), .offset = at_});
  }

  void skip_space() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
      ++at_;
    }
  }

  bool literal(std::string_view word) {
    if (text_.substr(at_, word.size()) == word) {
      at_ += word.size();
      return true;
    }
    return false;
  }

  Result<JsonValue> parse(std::size_t depth) {
    if (depth > limits_->max_depth) {
      return std::unexpected(Error{.code = ErrorCode::kLimitExceeded, .message = "JSON nested too deeply", .offset = at_});
    }
    if (++values_ > limits_->max_values) {
      return std::unexpected(Error{.code = ErrorCode::kLimitExceeded, .message = "JSON has too many values", .offset = at_});
    }
    skip_space();
    if (at_ >= text_.size()) {
      return fail("unexpected end");
    }
    const char c = text_[at_];
    if (c == '{') {
      return object(depth);
    }
    if (c == '[') {
      return array(depth);
    }
    if (c == '"') {
      auto s = string();
      if (!s) {
        return std::unexpected(std::move(s.error()));
      }
      return JsonValue{std::move(*s)};
    }
    if (literal("true")) {
      return JsonValue{true};
    }
    if (literal("false")) {
      return JsonValue{false};
    }
    if (literal("null")) {
      return JsonValue{nullptr};
    }
    return number();
  }

  Result<JsonValue> object(std::size_t depth) {
    ++at_;  // {
    auto out = std::make_shared<JsonObject>();
    skip_space();
    if (at_ < text_.size() && text_[at_] == '}') {
      ++at_;
      return JsonValue{std::move(out)};
    }
    for (;;) {
      skip_space();
      if (at_ >= text_.size() || text_[at_] != '"') {
        return fail("an object's key must be a string");
      }
      auto key = string();
      if (!key) {
        return std::unexpected(std::move(key.error()));
      }
      skip_space();
      if (at_ >= text_.size() || text_[at_] != ':') {
        return fail("':' expected");
      }
      ++at_;
      auto value = parse(depth + 1);
      if (!value) {
        return value;
      }
      out->insert_or_assign(std::move(*key), std::move(*value));  // a repeated key: the last one counts
      skip_space();
      if (at_ < text_.size() && text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (at_ < text_.size() && text_[at_] == '}') {
        ++at_;
        return JsonValue{std::move(out)};
      }
      return fail("',' or '}' expected");
    }
  }

  Result<JsonValue> array(std::size_t depth) {
    ++at_;  // [
    auto out = std::make_shared<JsonArray>();
    skip_space();
    if (at_ < text_.size() && text_[at_] == ']') {
      ++at_;
      return JsonValue{std::move(out)};
    }
    for (;;) {
      auto value = parse(depth + 1);
      if (!value) {
        return value;
      }
      out->push_back(std::move(*value));
      skip_space();
      if (at_ < text_.size() && text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (at_ < text_.size() && text_[at_] == ']') {
        ++at_;
        return JsonValue{std::move(out)};
      }
      return fail("',' or ']' expected");
    }
  }

  Result<JsonValue> number() {
    const std::size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') {
      ++at_;
    }
    const auto digits = [&] {
      const std::size_t from = at_;
      while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
        ++at_;
      }
      return at_ - from;
    };
    const std::size_t integer_digits = digits();
    if (integer_digits == 0 || (integer_digits > 1 && text_[at_ - integer_digits] == '0')) {
      return fail("malformed number");
    }
    if (at_ < text_.size() && text_[at_] == '.') {
      ++at_;
      if (digits() == 0) {
        return fail("malformed number");
      }
    }
    if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
      ++at_;
      if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
        ++at_;
      }
      if (digits() == 0) {
        return fail("malformed number");
      }
    }
    double value = 0;
    const auto [end, ec] = std::from_chars(text_.data() + start, text_.data() + at_, value);
    if (ec != std::errc() || end != text_.data() + at_ || !std::isfinite(value)) {
      return fail("number out of range");
    }
    return JsonValue{value};
  }

  // Four hex digits after \u.
  std::optional<std::uint32_t> hex4() {
    if (at_ + 4 > text_.size()) {
      return std::nullopt;
    }
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const char c = text_[at_ + i];
      v <<= 4U;
      if (c >= '0' && c <= '9') {
        v |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    at_ += 4;
    return v;
  }

  static void utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0U | (c >> 6U)));
      out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0U | (c >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (c >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((c >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    }
  }

  Result<std::string> string() {
    ++at_;  // "
    std::string out;
    while (at_ < text_.size()) {
      const auto c = static_cast<unsigned char>(text_[at_]);
      if (c == '"') {
        ++at_;
        return out;
      }
      if (c < 0x20) {
        return std::unexpected(fail("control character in a string").error());
      }
      if (c != '\\') {
        out.push_back(static_cast<char>(c));  // UTF-8 passes through (the engine decodes it)
        ++at_;
        continue;
      }
      if (++at_ >= text_.size()) {
        break;
      }
      const char e = text_[at_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          auto unit = hex4();
          if (!unit) {
            return std::unexpected(fail("malformed \\u escape").error());
          }
          std::uint32_t code = *unit;
          if (code >= 0xD800 && code <= 0xDBFF) {
            if (text_.substr(at_, 2) != "\\u") {
              return std::unexpected(fail("unpaired surrogate").error());
            }
            at_ += 2;
            auto low = hex4();
            if (!low || *low < 0xDC00 || *low > 0xDFFF) {
              return std::unexpected(fail("unpaired surrogate").error());
            }
            code = 0x10000 + ((code - 0xD800) << 10U) + (*low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            return std::unexpected(fail("unpaired surrogate").error());
          }
          utf8(out, code);
          break;
        }
        default:
          return std::unexpected(fail("unknown escape").error());
      }
    }
    return std::unexpected(fail("unterminated string").error());
  }

  std::string_view text_;
  const JsonLimits* limits_;
  std::size_t at_ = 0;
  std::size_t values_ = 0;
};

}  // namespace

Result<JsonValue> parse_json(std::string_view text, const JsonLimits& limits) {
  if (text.size() > limits.max_bytes) {
    return make_error(ErrorCode::kLimitExceeded, "JSON text too large");
  }
  return Reader(text, limits).document();
}

}  // namespace capriole::capi
