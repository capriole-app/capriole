#include "capriole/writer/serializer.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>
#include <variant>

#include "capriole/parser/lexer.hpp"

namespace capriole::writer {

namespace {

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

void append_hex_byte(std::string& out, unsigned char byte) {
  out.push_back(kHexDigits[byte >> 4U]);
  out.push_back(kHexDigits[byte & 0x0FU]);
}

// §7.3.4.2: parentheses and backslash are escaped (balanced parentheses would not need it, but
// escaping always is simpler and still exact). CR is escaped because a raw CR reads back as LF;
// every other byte, LF included, is written as is.
void append_literal_string(std::string& out, std::string_view bytes) {
  out.push_back('(');
  for (const char ch : bytes) {
    switch (ch) {
      case '(':
      case ')':
      case '\\':
        out.push_back('\\');
        out.push_back(ch);
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  out.push_back(')');
}

void append_hex_string(std::string& out, std::string_view bytes) {
  out.push_back('<');
  for (const char ch : bytes) {
    append_hex_byte(out, static_cast<unsigned char>(ch));
  }
  out.push_back('>');
}

// §7.3.5: regular printable characters as themselves, everything else — delimiters, whitespace,
// '#', bytes outside 0x21..0x7E — as #xx.
void append_name(std::string& out, std::string_view bytes) {
  out.push_back('/');
  for (const char ch : bytes) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x21 && byte <= 0x7E && byte != '#' && !parser::is_delimiter(byte)) {
      out.push_back(ch);
    } else {
      out.push_back('#');
      append_hex_byte(out, byte);
    }
  }
}

struct Writer {
  std::string* out;

  void operator()(model::Null /*unused*/) const { *out += "null"; }
  void operator()(bool value) const { *out += value ? "true" : "false"; }
  void operator()(std::int64_t value) const { *out += std::to_string(value); }
  void operator()(double value) const { *out += format_real(value); }
  void operator()(const model::String& value) const {
    if (value.form == model::StringForm::kHex) {
      append_hex_string(*out, value.bytes);
    } else {
      append_literal_string(*out, value.bytes);
    }
  }
  void operator()(const model::Name& value) const { append_name(*out, value.bytes); }
  void operator()(const model::Array& value) const {
    out->push_back('[');
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i > 0) {
        out->push_back(' ');
      }
      serialize(value[i], *out);
    }
    out->push_back(']');
  }
  void operator()(const model::Dict& value) const {
    *out += "<<";
    bool first = true;
    for (const model::DictEntry& entry : value.entries()) {
      if (!first) {
        out->push_back(' ');
      }
      first = false;
      append_name(*out, entry.key);
      out->push_back(' ');
      serialize(entry.value, *out);
    }
    *out += ">>";
  }
  void operator()(const model::Stream& value) const { (*this)(value.dict); }
  void operator()(model::ObjectRef value) const {
    *out += std::to_string(value.number);
    out->push_back(' ');
    *out += std::to_string(value.generation);
    *out += " R";
  }
};

}  // namespace

std::string format_real(double value) {
  if (!std::isfinite(value)) {
    return "0.0";
  }
  // Fixed notation with the shortest digits that round-trip: large magnitudes get long, but PDF has
  // no exponent notation to fall back on.
  std::array<char, 512> buffer{};
  const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed);
  if (ec != std::errc{}) {
    return "0.0";
  }
  std::string text(buffer.data(), end);
  if (text.find('.') == std::string::npos) {
    text += ".0";
  }
  return text;
}

void serialize(const model::Object& object, std::string& out) { std::visit(Writer{&out}, object.value()); }

std::string serialize(const model::Object& object) {
  std::string out;
  serialize(object, out);
  return out;
}

}  // namespace capriole::writer
