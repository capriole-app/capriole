#include "text_font.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace capriole::content::detail {

namespace {

using model::Object;

std::optional<double> number(const Object& object) {
  if (const auto i = object.as_integer()) {
    return static_cast<double>(*i);
  }
  if (const auto* r = std::get_if<double>(&object.value())) {
    return std::isfinite(*r) ? std::optional<double>(*r) : std::nullopt;
  }
  return std::nullopt;
}

std::optional<Object> resolved(Document& document, const model::Dict& dict, std::string_view key) {
  const Object* entry = dict.find(key);
  if (entry == nullptr) {
    return std::nullopt;
  }
  auto value = document.resolve(*entry);
  return value ? std::optional<Object>(std::move(*value)) : std::nullopt;
}

// Hostile fonts can declare ranges and lists of any size: past this many entries /W is cut short.
constexpr std::size_t kMaxWidthEntries = 1 << 20;

void read_cid_widths(Document& document, const model::Array& w, TextFont& font) {
  // Two forms (§9.7.4.3): c [w1 w2 ...]  and  c_first c_last w.
  for (std::size_t i = 0; i < w.size() && font.cid_widths.size() < kMaxWidthEntries;) {
    const auto first = w[i].as_integer();
    if (!first || *first < 0 || i + 1 >= w.size()) {
      break;
    }
    auto next = document.resolve(w[i + 1]);
    if (next && next->as_array() != nullptr) {
      std::uint32_t code = static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX));
      for (const Object& value : *next->as_array()) {
        if (const auto width = number(value); width && font.cid_widths.size() < kMaxWidthEntries) {
          font.cid_widths.push_back({code, code, *width});
        }
        ++code;
      }
      i += 2;
      continue;
    }
    const auto last = w[i + 1].as_integer();
    const auto width = i + 2 < w.size() ? number(w[i + 2]) : std::nullopt;
    if (!last || !width || *last < *first) {
      break;
    }
    font.cid_widths.push_back({static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX)),
                               static_cast<std::uint32_t>(std::min<std::int64_t>(*last, UINT32_MAX)), *width});
    i += 3;
  }
}

}  // namespace

std::vector<std::uint32_t> TextFont::codes(std::string_view bytes) const {
  std::vector<std::uint32_t> out;
  if (two_byte) {
    out.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
      out.push_back((static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 8U) | static_cast<unsigned char>(bytes[i + 1]));
    }
  } else {
    out.reserve(bytes.size());
    for (const char c : bytes) {
      out.push_back(static_cast<unsigned char>(c));
    }
  }
  return out;
}

double TextFont::width(std::uint32_t code) const {
  if (two_byte) {
    // Ranges sorted by first code (load_text_font): the last one starting at or before code.
    const auto after = std::ranges::upper_bound(cid_widths, code, {}, &Range::first);
    if (after != cid_widths.begin() && code <= std::prev(after)->last) {
      return std::prev(after)->width;
    }
    return default_width;
  }
  if (code >= first_char && code - first_char < widths.size()) {
    return widths[code - first_char];
  }
  return default_width;
}

TextFont load_text_font(Document& document, const Object& font_object, Warnings& warnings) {
  TextFont font;
  font.default_width = 500;  // an estimate, for fonts that give no widths
  auto resolved_font = document.resolve(font_object);
  const model::Dict* dict = resolved_font ? resolved_font->as_dict() : nullptr;
  if (dict == nullptr) {
    warnings.add("a font is not a dictionary: text advances estimated");
    return font;
  }
  const Object* subtype = dict->find("Subtype");
  std::string base_font;
  if (const Object* base = dict->find("BaseFont"); base != nullptr && base->as_name() != nullptr) {
    base_font = base->as_name()->bytes;
  }

  if (subtype != nullptr && subtype->is_name("Type0")) {
    // Composite font: the codes depend on the CMap. Identity-H/V: 2 bytes, CID = code (§9.7.5.2).
    const auto encoding = resolved(document, *dict, "Encoding");
    const bool identity = encoding && (encoding->is_name("Identity-H") || encoding->is_name("Identity-V"));
    font.two_byte = true;  // most CMaps in real files are 2-byte; others are estimated
    if (!identity) {
      warnings.add("composite fonts with a CMap other than Identity: codes read as 2 bytes, advances estimated");
    }
    if (encoding && encoding->is_name("Identity-V")) {
      warnings.add("vertical text (Identity-V) is laid out horizontally for now");
    }
    font.default_width = 1000;
    const auto descendants = resolved(document, *dict, "DescendantFonts");
    const model::Array* list = descendants ? descendants->as_array() : nullptr;
    auto cid_font = list != nullptr && !list->empty() ? document.resolve(list->front()) : Result<Object>(Object());
    if (cid_font && cid_font->as_dict() != nullptr) {
      const model::Dict& cid = *cid_font->as_dict();
      if (const auto dw = resolved(document, cid, "DW")) {
        font.default_width = number(*dw).value_or(1000);
      }
      if (const auto w = resolved(document, cid, "W"); w && w->as_array() != nullptr) {
        read_cid_widths(document, *w->as_array(), font);
        std::ranges::stable_sort(font.cid_widths, {}, &TextFont::Range::first);
      }
      font.widths_known = identity;
    }
    return font;
  }

  // Simple fonts (§9.6): one byte per code, /Widths from /FirstChar.
  if (subtype != nullptr && subtype->is_name("Type3")) {
    if (const auto matrix = resolved(document, *dict, "FontMatrix"); matrix && matrix->as_array() != nullptr && !matrix->as_array()->empty()) {
      font.scale = number(matrix->as_array()->front()).value_or(0.001);
    }
  }
  if (const auto first = resolved(document, *dict, "FirstChar")) {
    font.first_char = static_cast<std::uint32_t>(std::clamp<std::int64_t>(first->as_integer().value_or(0), 0, 255));
  }
  if (const auto widths = resolved(document, *dict, "Widths"); widths && widths->as_array() != nullptr) {
    for (const Object& value : *widths->as_array()) {
      if (font.widths.size() >= 256) {
        break;
      }
      auto width = document.resolve(value);
      font.widths.push_back(width ? number(*width).value_or(0) : 0);
    }
    font.widths_known = true;
  }
  bool embedded = false;
  if (const auto descriptor = resolved(document, *dict, "FontDescriptor"); descriptor && descriptor->as_dict() != nullptr) {
    if (const Object* missing = descriptor->as_dict()->find("MissingWidth")) {
      font.default_width = number(*missing).value_or(font.default_width);
    }
    embedded = descriptor->as_dict()->contains("FontFile") || descriptor->as_dict()->contains("FontFile2") || descriptor->as_dict()->contains("FontFile3");
  }
  const auto standard_encoding = [](const Object& name) {
    return name.is_name("WinAnsiEncoding") || name.is_name("MacRomanEncoding") || name.is_name("StandardEncoding");
  };
  if (const auto encoding = resolved(document, *dict, "Encoding")) {
    if (encoding->as_name() != nullptr) {
      font.latin_codes = standard_encoding(*encoding);
    } else if (const model::Dict* differences = encoding->as_dict()) {
      const Object* base = differences->find("BaseEncoding");
      font.latin_codes = !differences->contains("Differences") && (base == nullptr || standard_encoding(*base));
    }
  } else {
    const bool symbolic = base_font.find("Symbol") != std::string::npos || base_font.find("Dingbats") != std::string::npos;
    font.latin_codes = !embedded && !symbolic && subtype != nullptr && !subtype->is_name("Type3");
  }
  if (!font.widths_known) {
    // The standard 14 fonts may come without widths (§9.6.2.2). Courier is monospaced: exact.
    if (base_font.find("Courier") != std::string::npos) {
      font.default_width = 600;
      font.widths_known = true;
    } else {
      warnings.add("font /" + (base_font.empty() ? std::string("?") : base_font) + " has no /Widths: text advances estimated");
    }
  }
  return font;
}

}  // namespace capriole::content::detail
