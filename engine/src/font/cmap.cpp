#include "cmap.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "glyph_tables.hpp"

namespace capriole::font::detail {

namespace {

// ── Tokens of the PostScript subset CMaps are written in ─────────────────────────────────────

struct Token {
  enum class Kind : std::uint8_t { kEnd, kHex, kName, kNumber, kKeyword, kArrayOpen, kArrayClose };
  Kind kind = Kind::kEnd;
  std::string text;  // hex: the bytes; name and keyword: the characters
  std::int64_t number = 0;
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0'; }
bool is_delimiter(char c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }

int hex_digit(char c) {
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

class Tokenizer {
 public:
  explicit Tokenizer(std::string_view text) : text_(text) {}

  Token next() {
    for (;;) {
      skip_space();
      if (at_ >= text_.size()) {
        return {};
      }
      const char c = text_[at_];
      if (c == '%') {
        while (at_ < text_.size() && text_[at_] != '\n' && text_[at_] != '\r') {
          ++at_;
        }
        continue;
      }
      if (c == '(') {
        skip_literal_string();
        continue;
      }
      if (c == '<' && at_ + 1 < text_.size() && text_[at_ + 1] == '<') {
        at_ += 2;
        continue;  // dictionaries do not matter here
      }
      if (c == '>' && at_ + 1 < text_.size() && text_[at_ + 1] == '>') {
        at_ += 2;
        continue;
      }
      if (c == '<') {
        return hex_string();
      }
      if (c == '[') {
        ++at_;
        return {.kind = Token::Kind::kArrayOpen, .text = {}, .number = 0};
      }
      if (c == ']') {
        ++at_;
        return {.kind = Token::Kind::kArrayClose, .text = {}, .number = 0};
      }
      if (c == '/') {
        ++at_;
        return {.kind = Token::Kind::kName, .text = std::string(regular()), .number = 0};
      }
      if (is_delimiter(c)) {
        ++at_;  // a stray delimiter: skipped
        continue;
      }
      const std::string_view word = regular();
      if (const auto value = integer(word)) {
        return {.kind = Token::Kind::kNumber, .text = {}, .number = *value};
      }
      return {.kind = Token::Kind::kKeyword, .text = std::string(word), .number = 0};
    }
  }

 private:
  void skip_space() {
    while (at_ < text_.size() && is_space(text_[at_])) {
      ++at_;
    }
  }

  void skip_literal_string() {
    int depth = 0;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '\\') {
        ++at_;
      } else if (c == '(') {
        ++depth;
      } else if (c == ')' && --depth == 0) {
        return;
      }
    }
  }

  Token hex_string() {
    ++at_;
    Token token{.kind = Token::Kind::kHex, .text = {}, .number = 0};
    int high = -1;
    while (at_ < text_.size() && text_[at_] != '>') {
      const int digit = hex_digit(text_[at_++]);
      if (digit < 0) {
        continue;  // white space inside (and junk, as readers do)
      }
      if (high < 0) {
        high = digit;
      } else {
        token.text.push_back(static_cast<char>((high << 4) | digit));
        high = -1;
      }
    }
    if (high >= 0) {
      token.text.push_back(static_cast<char>(high << 4));  // odd count: a final 0 (§7.3.4.3)
    }
    ++at_;
    return token;
  }

  std::string_view regular() {
    const std::size_t start = at_;
    while (at_ < text_.size() && !is_space(text_[at_]) && !is_delimiter(text_[at_])) {
      ++at_;
    }
    return text_.substr(start, at_ - start);
  }

  static std::optional<std::int64_t> integer(std::string_view word) {
    if (word.empty()) {
      return std::nullopt;
    }
    std::size_t i = word[0] == '-' || word[0] == '+' ? 1 : 0;
    if (i == word.size()) {
      return std::nullopt;
    }
    std::int64_t value = 0;
    for (; i < word.size(); ++i) {
      if (word[i] < '0' || word[i] > '9') {
        return std::nullopt;  // reals do not occur where a CMap wants numbers
      }
      value = std::min<std::int64_t>((value * 10) + (word[i] - '0'), std::numeric_limits<std::uint32_t>::max());
    }
    return word[0] == '-' ? -value : value;
  }

  std::string_view text_;
  std::size_t at_ = 0;
};

// A code as written in a CMap: 1 to 4 bytes, big-endian.
struct HexCode {
  std::uint32_t value = 0;
  std::uint8_t length = 0;
};

std::optional<HexCode> code_of(const Token& token) {
  if (token.kind != Token::Kind::kHex || token.text.empty() || token.text.size() > 4) {
    return std::nullopt;
  }
  HexCode code{.value = 0, .length = static_cast<std::uint8_t>(token.text.size())};
  for (const char c : token.text) {
    code.value = (code.value << 8U) | static_cast<unsigned char>(c);
  }
  return code;
}

// A bfchar/bfrange destination: UTF-16BE bytes (surrogate pairs joined), or a glyph name.
std::u32string text_of(const Token& token, std::size_t max_units) {
  std::u32string out;
  if (token.kind == Token::Kind::kName) {
    if (const auto value = glyph_unicode(token.text)) {
      out.push_back(*value);
    }
    return out;
  }
  if (token.kind != Token::Kind::kHex) {
    return out;
  }
  const std::string& bytes = token.text;
  for (std::size_t i = 0; i + 1 < bytes.size() && i / 2 < max_units; i += 2) {
    const auto unit = static_cast<char32_t>((static_cast<unsigned char>(bytes[i]) << 8U) | static_cast<unsigned char>(bytes[i + 1]));
    if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
      const auto low = static_cast<char32_t>((static_cast<unsigned char>(bytes[i + 2]) << 8U) | static_cast<unsigned char>(bytes[i + 3]));
      if (low >= 0xDC00 && low <= 0xDFFF) {
        out.push_back(0x10000 + ((unit - 0xD800) << 10U) + (low - 0xDC00));
        i += 2;
        continue;
      }
    }
    out.push_back(unit);
  }
  if (bytes.size() == 1) {
    out.push_back(static_cast<unsigned char>(bytes[0]));  // a single byte: taken as the character itself
  }
  return out;
}

template <typename R>
void sort_ranges(std::vector<R>& ranges) {
  std::ranges::stable_sort(ranges, [](const R& a, const R& b) { return a.length != b.length ? a.length < b.length : a.low < b.low; });
}

// The last range starting at or before code among those of its length (a few before it are tried too:
// ranges seldom overlap, but they may).
template <typename R>
const R* find_range(const std::vector<R>& ranges, std::uint32_t code, std::uint8_t length) {
  auto after = std::ranges::upper_bound(ranges, std::pair{length, code}, {}, [](const R& r) { return std::pair{r.length, r.low}; });
  constexpr int kLookBack = 16;
  for (int tried = 0; tried < kLookBack && after != ranges.begin(); ++tried) {
    --after;
    if (after->length != length) {
      break;
    }
    if (code >= after->low && code <= after->high) {
      return &*after;
    }
  }
  return nullptr;
}

}  // namespace

std::shared_ptr<const CMap> CMap::identity(bool vertical) {
  auto cmap = std::make_shared<CMap>();
  cmap->identity_ = true;
  cmap->vertical_ = vertical;
  cmap->codespace_.push_back({.length = 2, .low = {0, 0, 0, 0}, .high = {0xFF, 0xFF, 0, 0}});
  return cmap;
}

Result<CMap> CMap::parse(std::string_view text, const Resolver& resolve, const CMapLimits& limits, std::size_t depth) {
  CMap cmap;
  Tokenizer tokens(text);
  std::size_t mappings = 0;
  const auto room = [&] { return mappings < limits.max_mappings; };
  std::string last_name;
  bool wmode_next = false;
  for (Token token = tokens.next(); token.kind != Token::Kind::kEnd; token = tokens.next()) {
    if (token.kind == Token::Kind::kName) {
      wmode_next = token.text == "WMode";
      last_name = token.text;
      continue;
    }
    if (token.kind == Token::Kind::kNumber && wmode_next) {
      cmap.vertical_ = token.number == 1;
      wmode_next = false;
      continue;
    }
    wmode_next = false;
    if (token.kind != Token::Kind::kKeyword) {
      continue;
    }
    const std::string& word = token.text;
    if (word == "usecmap") {
      if (!last_name.empty() && depth + 1 < limits.max_depth && resolve) {
        cmap.parent_ = resolve(last_name, depth + 1);
      }
    } else if (word == "begincodespacerange") {
      for (Token low = tokens.next(); low.kind == Token::Kind::kHex; low = tokens.next()) {
        const Token high = tokens.next();
        if (low.text.empty() || low.text.size() > 4 || high.text.size() != low.text.size() || cmap.codespace_.size() >= 256) {
          continue;
        }
        Codespace range{.length = static_cast<std::uint8_t>(low.text.size()), .low = {}, .high = {}};
        for (std::size_t i = 0; i < low.text.size(); ++i) {
          range.low.at(i) = static_cast<std::uint8_t>(low.text[i]);
          range.high.at(i) = static_cast<std::uint8_t>(high.text[i]);
        }
        cmap.codespace_.push_back(range);
      }
    } else if (word == "begincidrange" || word == "beginnotdefrange") {
      std::vector<Range>& into = word == "begincidrange" ? cmap.cids_ : cmap.notdefs_;
      for (Token low = tokens.next(); low.kind == Token::Kind::kHex; low = tokens.next()) {
        const Token high = tokens.next();
        const Token cid = tokens.next();
        const auto first = code_of(low);
        const auto last = code_of(high);
        if (first && last && first->length == last->length && first->value <= last->value && cid.kind == Token::Kind::kNumber && cid.number >= 0 && room()) {
          into.push_back({.low = first->value, .high = last->value, .length = first->length, .value = static_cast<std::uint32_t>(cid.number)});
          ++mappings;
        }
      }
    } else if (word == "begincidchar" || word == "beginnotdefchar") {
      std::vector<Range>& into = word == "begincidchar" ? cmap.cids_ : cmap.notdefs_;
      for (Token code = tokens.next(); code.kind == Token::Kind::kHex; code = tokens.next()) {
        const Token cid = tokens.next();
        const auto c = code_of(code);
        if (c && cid.kind == Token::Kind::kNumber && cid.number >= 0 && room()) {
          into.push_back({.low = c->value, .high = c->value, .length = c->length, .value = static_cast<std::uint32_t>(cid.number)});
          ++mappings;
        }
      }
    } else if (word == "beginbfchar") {
      for (Token code = tokens.next(); code.kind == Token::Kind::kHex; code = tokens.next()) {
        const Token destination = tokens.next();
        const auto c = code_of(code);
        if (c && room()) {
          cmap.text_.push_back({.low = c->value, .high = c->value, .length = c->length, .text = text_of(destination, limits.max_text_units)});
          ++mappings;
        }
      }
    } else if (word == "beginbfrange") {
      for (Token low = tokens.next(); low.kind == Token::Kind::kHex; low = tokens.next()) {
        const Token high = tokens.next();
        const auto first = code_of(low);
        const auto last = code_of(high);
        const bool valid = first && last && first->length == last->length && first->value <= last->value;
        Token destination = tokens.next();
        if (destination.kind == Token::Kind::kArrayOpen) {
          // One destination per code of the range.
          std::uint32_t code = valid ? first->value : 0;
          for (destination = tokens.next(); destination.kind != Token::Kind::kArrayClose && destination.kind != Token::Kind::kEnd;
               destination = tokens.next()) {
            if (valid && code <= last->value && room()) {
              cmap.text_.push_back({.low = code, .high = code, .length = first->length, .text = text_of(destination, limits.max_text_units)});
              ++mappings;
              ++code;
            }
          }
        } else if (valid && room()) {
          cmap.text_.push_back({.low = first->value, .high = last->value, .length = first->length, .text = text_of(destination, limits.max_text_units)});
          ++mappings;
        }
      }
    }
  }
  if (cmap.codespace_.empty() && cmap.cids_.empty() && cmap.notdefs_.empty() && cmap.text_.empty() && !cmap.parent_) {
    return make_error(ErrorCode::kSyntax, "CMap: no codespace or mappings");
  }
  sort_ranges(cmap.cids_);
  sort_ranges(cmap.notdefs_);
  sort_ranges(cmap.text_);
  return cmap;
}

bool CMap::in_codespace(std::uint32_t code, std::uint8_t length) const {
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    for (const Codespace& range : c->codespace_) {
      if (range.length != length) {
        continue;
      }
      bool inside = true;
      for (std::uint8_t i = 0; i < length && inside; ++i) {
        const auto byte = static_cast<std::uint8_t>(code >> (8U * static_cast<unsigned>(length - 1 - i)));
        inside = byte >= range.low.at(i) && byte <= range.high.at(i);
      }
      if (inside) {
        return true;
      }
    }
  }
  return false;
}

bool CMap::has_codespace() const {
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    if (!c->codespace_.empty()) {
      return true;
    }
  }
  return false;
}

std::uint8_t CMap::fallback_length(std::uint8_t first) const {
  std::uint8_t fits = 0;
  std::uint8_t shortest = 0;
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    for (const Codespace& range : c->codespace_) {
      shortest = shortest == 0 ? range.length : std::min(shortest, range.length);
      if (first >= range.low[0] && first <= range.high[0]) {
        fits = fits == 0 ? range.length : std::min(fits, range.length);
      }
    }
  }
  if (fits != 0) {
    return fits;
  }
  if (shortest != 0) {
    return shortest;
  }
  // No codespace at all (a broken embedded CMap): the length its mappings use, else two bytes.
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    if (!c->cids_.empty()) {
      return c->cids_.front().length;
    }
  }
  return 2;
}

std::optional<std::uint32_t> CMap::cid(std::uint32_t code, std::uint8_t length) const {
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    if (c->identity_) {
      return code;
    }
    if (const Range* r = find_range(c->cids_, code, length)) {
      return r->value + (code - r->low);
    }
    if (const Range* r = find_range(c->notdefs_, code, length)) {
      return r->value;
    }
  }
  return std::nullopt;
}

CMap::Code CMap::next(std::string_view bytes, std::size_t& at) const {
  const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(bytes[i]); };
  if (at >= bytes.size()) {
    return {};
  }
  if (has_codespace()) {
    std::uint32_t code = 0;
    for (std::uint8_t length = 1; length <= 4 && at + length <= bytes.size(); ++length) {
      code = (code << 8U) | byte(at + length - 1);
      if (in_codespace(code, length)) {
        at += length;
        return {.code = code, .length = length, .cid = cid(code, length).value_or(0)};
      }
    }
  }
  // Partial or no match (§9.7.6.3): the length of the codespace ranges the first byte fits; CID 0 — but
  // a CMap without codespace ranges still maps what it can.
  const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(fallback_length(byte(at)), bytes.size() - at));
  std::uint32_t code = 0;
  for (std::uint8_t i = 0; i < length; ++i) {
    code = (code << 8U) | byte(at + i);
  }
  at += length;
  return {.code = code, .length = length, .cid = has_codespace() ? 0 : cid(code, length).value_or(0)};
}

std::u32string CMap::text(std::uint32_t code, std::uint8_t length) const {
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    // The exact length first; then any (ToUnicode CMaps often write one-byte codes with two bytes).
    const TextRange* r = find_range(c->text_, code, length);
    for (std::uint8_t other = 1; r == nullptr && other <= 4; ++other) {
      r = other != length ? find_range(c->text_, code, other) : nullptr;
    }
    if (r != nullptr) {
      std::u32string out = r->text;
      if (!out.empty() && code > r->low) {
        out.back() += code - r->low;
      }
      return out;
    }
  }
  return {};
}

void CMap::for_each_cid(const std::function<bool(std::uint32_t, std::uint8_t, std::uint32_t)>& f) const {
  for (const CMap* c = this; c != nullptr; c = c->parent_.get()) {
    for (const Range& r : c->cids_) {
      for (std::uint64_t code = r.low; code <= r.high; ++code) {
        if (!f(static_cast<std::uint32_t>(code), r.length, r.value + static_cast<std::uint32_t>(code - r.low))) {
          return;
        }
      }
    }
  }
}

}  // namespace capriole::font::detail
