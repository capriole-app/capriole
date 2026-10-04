#include "capriole/crypto/der.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto::der {

namespace {

auto u8(std::byte b) { return std::to_integer<std::uint8_t>(b); }

std::unexpected<Error> malformed(std::string_view what) { return make_error(ErrorCode::kSyntax, std::format("ASN.1: {}", what)); }

std::unexpected<Error> too_deep() { return make_error(ErrorCode::kLimitExceeded, "ASN.1: nested too deep"); }

struct Header {
  std::uint8_t tag = 0;
  std::size_t header_size = 0;
  std::optional<std::size_t> length;  // none: indefinite
};

Result<Header> header_of(std::span<const std::byte> data) {
  if (data.size() < 2) {
    return (malformed("truncated element"));
  }
  Header h;
  h.tag = u8(data[0]);
  if ((h.tag & 0x1FU) == 0x1FU) {
    return (malformed("multi-byte tag"));
  }
  const std::uint8_t first = u8(data[1]);
  if (first < 0x80) {
    h.header_size = 2;
    h.length = first;
    return h;
  }
  if (first == 0x80) {
    if ((h.tag & kConstructed) == 0) {
      return (malformed("indefinite length of a primitive element"));
    }
    h.header_size = 2;
    return h;
  }
  const std::size_t count = first & 0x7FU;
  if (count > 4 || data.size() < 2 + count) {
    return (malformed("bad length"));
  }
  std::size_t length = 0;
  for (std::size_t i = 0; i < count; ++i) {
    length = (length << 8U) | u8(data[2 + i]);
  }
  h.header_size = 2 + count;
  h.length = length;
  return h;
}

// One element at the start of data: its content and its whole encoding.
Result<Element> element_at(std::span<const std::byte> data, int depth) {
  if (depth > kMaxDepth) {
    return too_deep();
  }
  auto h = header_of(data);
  if (!h) {
    return std::unexpected(std::move(h.error()));
  }
  const auto rest = data.subspan(h->header_size);
  if (h->length) {
    if (*h->length > rest.size()) {
      return (malformed("element longer than what holds it"));
    }
    return Element{.tag = h->tag, .content = rest.first(*h->length), .whole = data.first(h->header_size + *h->length)};
  }
  // Indefinite: the elements inside up to an end-of-contents (00 00).
  std::size_t at = 0;
  while (true) {
    if (rest.size() - at < 2) {
      return (malformed("indefinite length without its end"));
    }
    if (u8(rest[at]) == 0 && u8(rest[at + 1]) == 0) {
      return Element{.tag = h->tag, .content = rest.first(at), .whole = data.first(h->header_size + at + 2)};
    }
    auto inner = element_at(rest.subspan(at), depth + 1);
    if (!inner) {
      return std::unexpected(std::move(inner.error()));
    }
    at += inner->whole.size();
  }
}

void append(Bytes& out, std::span<const std::byte> part) { out.insert(out.end(), part.begin(), part.end()); }

void append_length(Bytes& out, std::size_t length) {
  if (length < 0x80) {
    out.push_back(static_cast<std::byte>(length));
    return;
  }
  std::uint8_t count = 0;
  for (std::size_t l = length; l != 0; l >>= 8U) {
    ++count;
  }
  out.push_back(static_cast<std::byte>(0x80U | count));
  for (int i = count - 1; i >= 0; --i) {
    out.push_back(static_cast<std::byte>((length >> (8U * static_cast<unsigned>(i))) & 0xFFU));
  }
}

std::optional<int> digits(std::string_view text, std::size_t at, std::size_t count) {
  if (at + count > text.size()) {
    return std::nullopt;
  }
  int value = 0;
  const auto [end, ec] = std::from_chars(text.data() + at, text.data() + at + count, value);
  if (ec != std::errc() || end != text.data() + at + count) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

Result<Reader> Reader::into(const Element& element, int depth) {
  if (!element.constructed()) {
    return (malformed("a primitive element has no elements"));
  }
  if (depth + 1 > kMaxDepth) {
    return too_deep();
  }
  return Reader(element.content, depth + 1);
}

Result<Element> Reader::next() {
  auto element = element_at(data_, depth_);
  if (!element) {
    return element;
  }
  data_ = data_.subspan(element->whole.size());
  return element;
}

Result<Element> Reader::expect(std::uint8_t tag) {
  if (peek() != tag) {
    return (malformed(std::format("expected tag {:#04x}", tag)));
  }
  return next();
}

Result<std::optional<Element>> Reader::next_if(std::uint8_t tag) {
  if (peek() != tag) {
    return std::optional<Element>();
  }
  auto element = next();
  if (!element) {
    return std::unexpected(std::move(element.error()));
  }
  return std::optional<Element>(*element);
}

std::optional<std::uint8_t> Reader::peek() const noexcept {
  if (data_.empty()) {
    return std::nullopt;
  }
  return u8(data_[0]);
}

Result<Bytes> string_bytes(const Element& element, int depth) {
  if (!element.constructed()) {
    return Bytes(element.content.begin(), element.content.end());
  }
  auto reader = Reader::into(element, depth);
  if (!reader) {
    return std::unexpected(std::move(reader.error()));
  }
  Bytes out;
  while (!reader->empty()) {
    auto part = reader->next();
    if (!part) {
      return std::unexpected(std::move(part.error()));
    }
    auto bytes = string_bytes(*part, reader->depth());
    if (!bytes) {
      return bytes;
    }
    append(out, *bytes);
  }
  return out;
}

std::string oid_text(std::span<const std::byte> content) {
  std::string out;
  std::uint64_t value = 0;
  bool first = true;
  for (std::size_t i = 0; i < content.size(); ++i) {
    const std::uint8_t b = u8(content[i]);
    if (value > (UINT64_MAX >> 7U)) {
      return {};
    }
    value = (value << 7U) | (b & 0x7FU);
    if ((b & 0x80U) != 0) {
      if (i + 1 == content.size()) {
        return {};
      }
      continue;
    }
    if (first) {
      std::uint64_t head = 2;
      if (value < 40) {
        head = 0;
      } else if (value < 80) {
        head = 1;
      }
      out = std::format("{}.{}", head, value - (head * 40));
      first = false;
    } else {
      out += std::format(".{}", value);
    }
    value = 0;
  }
  return out;
}

Result<std::uint64_t> small_integer(const Element& element) {
  if (element.tag != kInteger || element.content.empty() || element.content.size() > 9 || (u8(element.content[0]) & 0x80U) != 0) {
    return (malformed("not a small non-negative integer"));
  }
  std::uint64_t value = 0;
  for (const std::byte b : element.content) {
    if (value > (UINT64_MAX >> 8U)) {
      return (malformed("integer too large"));
    }
    value = (value << 8U) | u8(b);
  }
  return value;
}

Result<std::string> time_text(const Element& element) {
  const std::string_view text = chars_of(element.content);
  std::string full;
  if (element.tag == kUtcTime) {
    // YYMMDDHHMM[SS]Z: 1950–2049 (RFC 5280 §4.1.2.5.1).
    const auto yy = digits(text, 0, 2);
    if (!yy) {
      return (malformed("bad UTCTime"));
    }
    full = std::format("{}{}", *yy < 50 ? "20" : "19", text);
  } else if (element.tag == kGeneralizedTime) {
    full = std::string(text);
  } else {
    return (malformed("not a time"));
  }
  // YYYYMMDDHHMM[SS][.fff]Z
  if (full.size() < 13 || full.back() != 'Z') {
    return (malformed("time not in UTC"));
  }
  for (const std::size_t at : {std::size_t{0}, std::size_t{4}, std::size_t{6}, std::size_t{8}, std::size_t{10}}) {
    if (!digits(full, at, at == 0 ? 4 : 2)) {
      return (malformed("bad time"));
    }
  }
  const auto seconds = digits(full, 12, 2);
  return std::format("D:{}{}Z", full.substr(0, 12), seconds ? full.substr(12, 2) : "00");
}

Bytes tlv(std::uint8_t tag, std::span<const std::byte> content) { return tlv(tag, {content}); }

Bytes tlv(std::uint8_t tag, std::initializer_list<std::span<const std::byte>> parts) {
  std::size_t length = 0;
  for (const auto& part : parts) {
    length += part.size();
  }
  Bytes out;
  out.reserve(length + 6);
  out.push_back(static_cast<std::byte>(tag));
  append_length(out, length);
  for (const auto& part : parts) {
    append(out, part);
  }
  return out;
}

Bytes sequence(std::initializer_list<std::span<const std::byte>> parts) { return tlv(kSequence, parts); }

Bytes set_of(std::vector<Bytes> elements) {
  std::ranges::sort(elements);
  Bytes content;
  for (const Bytes& e : elements) {
    append(content, e);
  }
  return tlv(kSet, content);
}

Bytes oid(std::string_view dotted) {
  std::vector<std::uint64_t> arcs;
  std::size_t at = 0;
  while (at <= dotted.size()) {
    const std::size_t dot = std::min(dotted.find('.', at), dotted.size());
    std::uint64_t arc = 0;
    std::from_chars(dotted.data() + at, dotted.data() + dot, arc);
    arcs.push_back(arc);
    at = dot + 1;
  }
  Bytes content;
  const auto put = [&](std::uint64_t v) {
    std::array<std::byte, 10> buf{};
    std::size_t n = 0;
    buf.at(n++) = static_cast<std::byte>(v & 0x7FU);
    for (v >>= 7U; v != 0; v >>= 7U) {
      buf.at(n++) = static_cast<std::byte>(v & 0x7FU);
    }
    for (std::size_t i = n; i-- > 0;) {
      content.push_back(buf.at(i) | (i != 0 ? std::byte{0x80} : std::byte{0}));
    }
  };
  if (arcs.size() >= 2) {
    put((arcs[0] * 40) + arcs[1]);
    for (std::size_t i = 2; i < arcs.size(); ++i) {
      put(arcs[i]);
    }
  }
  return tlv(kOid, content);
}

Bytes integer(std::span<const std::byte> magnitude) {
  std::size_t skip = 0;
  while (skip + 1 < magnitude.size() && u8(magnitude[skip]) == 0) {
    ++skip;
  }
  const auto m = magnitude.subspan(skip);
  Bytes content;
  if (m.empty() || (u8(m[0]) & 0x80U) != 0) {
    content.push_back(std::byte{0});
  }
  append(content, m);
  return tlv(kInteger, content);
}

Bytes integer(std::uint64_t value) {
  std::array<std::byte, 8> be{};
  for (std::size_t i = 0; i < 8; ++i) {
    be.at(7 - i) = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  }
  return integer(be);
}

Bytes octet_string(std::span<const std::byte> content) { return tlv(kOctetString, content); }

Bytes null() { return Bytes{std::byte{kNull}, std::byte{0}}; }

Bytes boolean(bool value) { return Bytes{std::byte{kBoolean}, std::byte{1}, value ? std::byte{0xFF} : std::byte{0}}; }

Result<Bytes> generalized_time(std::string_view pdf_date) {
  // D:YYYYMMDDHHmmSSOHH'mm (§7.9.4); what is missing reads as its least value, the offset as UTC.
  std::string_view s = pdf_date;
  if (s.starts_with("D:")) {
    s.remove_prefix(2);
  }
  const auto year = digits(s, 0, 4);
  if (!year) {
    return make_error(ErrorCode::kUnsupported, "not a PDF date");
  }
  const auto part = [&](std::size_t at, int fallback) { return digits(s, at, 2).value_or(fallback); };
  const int month = part(4, 1);
  const int day = part(6, 1);
  const int hour = part(8, 0);
  const int minute = part(10, 0);
  const int second = part(12, 0);
  int offset_minutes = 0;
  if (s.size() > 14 && (s[14] == '+' || s[14] == '-')) {
    offset_minutes = (digits(s, 15, 2).value_or(0) * 60) + digits(s, 18, 2).value_or(0);
    if (s[14] == '+') {
      offset_minutes = -offset_minutes;  // local = UTC + offset
    }
  }
  const std::chrono::year_month_day ymd{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(month)),
                                        std::chrono::day(static_cast<unsigned>(day))};
  if (!ymd.ok() || hour > 23 || minute > 59 || second > 60) {
    return make_error(ErrorCode::kUnsupported, "not a PDF date");
  }
  const auto utc =
      std::chrono::sys_days(ymd) + std::chrono::hours(hour) + std::chrono::minutes(minute + offset_minutes) + std::chrono::seconds(std::min(second, 59));
  const auto text = std::format("{:%Y%m%d%H%M%S}Z", std::chrono::floor<std::chrono::seconds>(utc));
  return tlv(kGeneralizedTime, bytes_of(text));
}

std::span<const std::byte> bytes_of(std::string_view text) noexcept { return std::as_bytes(std::span(text.data(), text.size())); }

std::string_view chars_of(std::span<const std::byte> bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes as characters
}

}  // namespace capriole::crypto::der
