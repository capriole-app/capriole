#pragma once

// Internal to the C API: the JSON the document functions return (format in capi/README.md).

#include <cstdint>
#include <string>
#include <string_view>

#include "capriole/model/object.hpp"

namespace capriole::capi {

// A JSON string literal for bytes: printable ASCII as is, every other byte as \u00XX. Each byte maps
// to the code point of the same value, so the text is valid UTF-8 JSON and the bytes come back
// exactly (PDF strings and names are bytes, not UTF-8).
void append_json_string(std::string& out, std::string_view bytes);

// A JSON string literal for Unicode text: printable ASCII as is, newlines as \n, everything else as \uXXXX (UTF-16
// surrogate pairs beyond the BMP), so the JSON stays ASCII. Not characters (surrogates, beyond
// U+10FFFF) become U+FFFD.
void append_json_text(std::string& out, std::u32string_view text);

// A PDF object as JSON: {"t": kind, ...} (see capi/README.md).
void append_json_object(std::string& out, const model::Object& object);

}  // namespace capriole::capi
