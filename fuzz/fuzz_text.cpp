// Fuzz target for text extraction: open the input as a document (repairs on), interpret its first pages
// for their text, extract it; and decode the input as a PDF text string. The seed corpus is
// fuzz/corpus/text/.
//
// Invariants beyond "no crash, no UB, no hang": the characters stay within the limit, words, lines and
// blocks are consecutive ranges that cover exactly the characters, words and lines, boxes are ordered,
// and the text written out is valid UTF-8.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

// Valid UTF-8: well-formed sequences, no surrogates, nothing beyond U+10FFFF, no overlong forms.
bool valid_utf8(std::string_view s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    const std::size_t length = c < 0x80 ? 1 : (c >> 5U) == 0x6 ? 2 : (c >> 4U) == 0xE ? 3 : (c >> 3U) == 0x1E ? 4 : 0;
    if (length == 0 || i + length > s.size()) {
      return false;
    }
    char32_t v = length == 1 ? c : c & (0x7FU >> length);
    for (std::size_t k = 1; k < length; ++k) {
      const auto b = static_cast<unsigned char>(s[i + k]);
      if ((b & 0xC0U) != 0x80U) {
        return false;
      }
      v = (v << 6U) | (b & 0x3FU);
    }
    constexpr std::array<char32_t, 5> kMinimum = {0, 0, 0x80, 0x800, 0x10000};
    if (v < kMinimum.at(length) || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
      return false;
    }
    i += length;
  }
  return true;
}

template <typename Range>
void check_ranges(const std::vector<Range>& ranges, std::size_t total) {
  std::size_t next = 0;
  for (const Range& r : ranges) {
    check(r.first == next && r.count > 0);
    next += r.count;
    check(r.box.x0 <= r.box.x1 && r.box.y0 <= r.box.y1);
  }
  check(next == total);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  check(valid_utf8(capriole::model::to_utf8(capriole::model::decode_text_string(
      std::string_view(reinterpret_cast<const char*>(data), size)))));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

  capriole::OpenOptions options;
  options.limits.max_nesting_depth = 32;
  auto document = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(bytes.begin(), bytes.end())), options);
  if (!document) {
    return 0;
  }
  auto pages = document->page_refs();
  if (!pages) {
    return 0;
  }
  capriole::content::InterpreterLimits limits;
  limits.parse = options.limits;
  limits.max_operations = 100'000;
  limits.max_commands = 20'000;
  limits.max_form_depth = 8;
  capriole::text::TextLimits text_limits;
  text_limits.max_chars = 20'000;
  text_limits.max_ordered_blocks = 64;
  for (std::size_t i = 0; i < pages->size() && i < 3; ++i) {
    auto list = capriole::content::interpret_page(*document, (*pages)[i], limits, nullptr, capriole::content::Purpose::kText);
    if (!list) {
      continue;
    }
    for (const auto order : {capriole::text::Order::kContent, capriole::text::Order::kLayout}) {
      const capriole::text::PageText page = capriole::text::extract(*list, order, text_limits);
      check(page.chars.size() <= text_limits.max_chars);
      for (const auto& c : page.chars) {
        check(!c.text.empty() && c.box.x0 <= c.box.x1 && c.box.y0 <= c.box.y1);
      }
      check_ranges(page.words, page.chars.size());
      check_ranges(page.lines, page.words.size());
      check_ranges(page.blocks, page.lines.size());
      check(valid_utf8(page.plain()));
    }
  }
  return 0;
}
