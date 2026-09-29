#include <algorithm>
#include <cstddef>

#include "glyph_tables.hpp"

namespace capriole::font::detail {

namespace {

// Hexadecimal digits, exactly: nullopt for anything else or an empty string.
std::optional<std::uint32_t> hex(std::string_view digits) {
  if (digits.empty() || digits.size() > 8) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : digits) {
    std::uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<std::uint32_t>(c - 'A' + 10);
    } else {
      return std::nullopt;  // the AGL specification wants upper-case digits
    }
    value = (value << 4U) | digit;
  }
  return value;
}

bool is_scalar(std::uint32_t value) { return value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF); }

}  // namespace

std::optional<char32_t> glyph_unicode(std::string_view name) {
  if (const std::size_t period = name.find('.'); period != std::string_view::npos) {
    name = name.substr(0, period);
  }
  if (name.empty()) {
    return std::nullopt;
  }
  const auto list = glyph_list();
  if (const auto found = std::ranges::lower_bound(list, name, {}, &GlyphUnicode::name); found != list.end() && found->name == name) {
    return found->unicode;
  }
  // uniXXXX (the first of a ligature's values), and uXXXX to uXXXXXX.
  if (name.starts_with("uni") && name.size() >= 7) {
    if (const auto value = hex(name.substr(3, 4)); value && is_scalar(*value)) {
      return static_cast<char32_t>(*value);
    }
  } else if (name.starts_with('u') && name.size() >= 5 && name.size() <= 7) {
    if (const auto value = hex(name.substr(1)); value && is_scalar(*value)) {
      return static_cast<char32_t>(*value);
    }
  }
  return std::nullopt;
}

std::optional<std::uint8_t> encoding_code(BaseEncoding encoding, std::string_view name) {
  const auto table = encoding_table(encoding);
  for (std::size_t code = 0; code < table.size(); ++code) {
    if (!name.empty() && table[code] == name) {
      return static_cast<std::uint8_t>(code);
    }
  }
  return std::nullopt;
}

}  // namespace capriole::font::detail
