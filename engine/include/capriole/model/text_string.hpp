#pragma once

// Text strings (ISO 32000-2 §7.9.2.2): the strings a PDF means as text — /ActualText, outlines, document
// information — decoded to Unicode, and Unicode written as UTF-8.

#include <string>
#include <string_view>

namespace capriole::model {

// UTF-16BE (with its byte order mark FE FF), UTF-8 (with EF BB BF, PDF 2.0), else PDFDocEncoding (Annex
// D.3). Unpaired surrogates and undefined PDFDocEncoding codes become U+FFFD.
[[nodiscard]] std::u32string decode_text_string(std::string_view bytes);

// UTF-8 of Unicode text; code points that are not characters (surrogates, beyond U+10FFFF) become U+FFFD.
[[nodiscard]] std::string to_utf8(std::u32string_view text);

// Unicode of UTF-8 text (no byte order mark expected); malformed sequences become U+FFFD.
[[nodiscard]] std::u32string from_utf8(std::string_view text);

// A text string for Unicode text: printable ASCII as it is (it reads the same in PDFDocEncoding),
// otherwise UTF-16BE with its byte order mark.
[[nodiscard]] std::string encode_text_string(std::u32string_view text);

}  // namespace capriole::model
