#include "capriole/model/text_string.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace capriole::model {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// PDFDocEncoding (Annex D.3) where it differs from Latin-1: 0x18–0x1F and 0x80–0xA0; 0x7F, 0x9F and
// 0xAD are undefined (0 here).
constexpr std::array<char32_t, 8> kLow = {0x02D8, 0x02C7, 0x02C6, 0x02D9, 0x02DD, 0x02DB, 0x02DA, 0x02DC};
constexpr std::array<char32_t, 33> kHigh = {0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212,
                                            0x2030, 0x201E, 0x201C, 0x201D, 0x2018, 0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141,
                                            0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, 0,      0x20AC};

// The length of the UTF-8 sequence a lead byte starts; 0 for a byte that cannot start one.
std::size_t utf8_length(unsigned char c) {
  if (c < 0x80) {
    return 1;
  }
  if ((c >> 5U) == 0x6) {
    return 2;
  }
  if ((c >> 4U) == 0xE) {
    return 3;
  }
  return (c >> 3U) == 0x1E ? 4 : 0;
}

char32_t pdf_doc(unsigned char c) {
  if (c >= 0x18 && c <= 0x1F) {
    return kLow.at(c - 0x18U);
  }
  if (c >= 0x80 && c <= 0xA0) {
    const char32_t v = kHigh.at(c - 0x80U);
    return v != 0 ? v : kReplacement;
  }
  if (c == 0x7F || c == 0xAD) {
    return kReplacement;
  }
  return c;
}

}  // namespace

std::u32string from_utf8(std::string_view text) {
  std::string marked = "\xEF\xBB\xBF";
  marked += text;
  return decode_text_string(marked);
}

std::string encode_text_string(std::u32string_view text) {
  if (std::ranges::all_of(text, [](char32_t c) { return c >= 0x20 && c < 0x7F; })) {
    return {text.begin(), text.end()};
  }
  std::string out = "\xFE\xFF";
  const auto unit = [&out](char32_t u) {
    out.push_back(static_cast<char>((u >> 8U) & 0xFFU));
    out.push_back(static_cast<char>(u & 0xFFU));
  };
  for (char32_t c : text) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
      c = kReplacement;
    }
    if (c >= 0x10000) {
      unit(0xD800 + ((c - 0x10000) >> 10U));
      unit(0xDC00 + ((c - 0x10000) & 0x3FFU));
    } else {
      unit(c);
    }
  }
  return out;
}

std::u32string decode_text_string(std::string_view bytes) {
  std::u32string out;
  const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
  if (bytes.size() >= 2 && byte(0) == 0xFE && byte(1) == 0xFF) {
    for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
      const char32_t unit = (static_cast<char32_t>(byte(i)) << 8U) | byte(i + 1);
      if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
        const char32_t low = (static_cast<char32_t>(byte(i + 2)) << 8U) | byte(i + 3);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          out.push_back(0x10000 + ((unit - 0xD800) << 10U) + (low - 0xDC00));
          i += 2;
          continue;
        }
      }
      out.push_back(unit >= 0xD800 && unit <= 0xDFFF ? kReplacement : unit);
    }
    return out;
  }
  if (bytes.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
    for (std::size_t i = 3; i < bytes.size();) {
      const unsigned char c = byte(i);
      const std::size_t length = utf8_length(c);
      if (length == 0 || i + length > bytes.size()) {
        out.push_back(kReplacement);
        ++i;
        continue;
      }
      char32_t v = length == 1 ? c : c & (0x7FU >> length);
      bool valid = true;
      for (std::size_t k = 1; k < length; ++k) {
        valid = valid && (byte(i + k) & 0xC0U) == 0x80U;
        v = (v << 6U) | (byte(i + k) & 0x3FU);
      }
      out.push_back(valid && v <= 0x10FFFF && (v < 0xD800 || v > 0xDFFF) ? v : kReplacement);
      i += valid ? length : 1;
    }
    return out;
  }
  out.reserve(bytes.size());
  for (const char c : bytes) {
    out.push_back(pdf_doc(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string to_utf8(std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char32_t c : text) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
      c = kReplacement;
    }
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
  return out;
}

}  // namespace capriole::model
