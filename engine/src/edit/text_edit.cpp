#include "capriole/edit/text_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/operators.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/document/document.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"
#include "capriole/writer/serializer.hpp"
#include "common.hpp"
#include "font/font.hpp"
#include "font/standard_fonts.hpp"
#include "text_removal.hpp"

namespace capriole::edit {

namespace {

using content::Operator;
using detail::name;
using detail::num;
using graphics::Point;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// How far new text may be narrowed to keep to its room (similar font).
constexpr double kMinScale = 0.5;
// How many characters a replacement may have.
constexpr std::size_t kMaxText = 10'000;

std::string name_of(const Object* value) { return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string(); }

Point centre(const Rect& r) { return {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}; }

bool inside(const Point& p, const Rect& r) { return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1; }

// The text-showing operators whose strings hold codes.
bool shows_text(Operator op) {
  return op == Operator::kShowText || op == Operator::kShowTextArray || op == Operator::kNextLineShowText || op == Operator::kNextLineSpacingShowText;
}

// The standard font nearest a font (its name, its descriptor's flags, italic angle and weight).
std::string similar_font(Document& document, const std::optional<ObjectRef>& ref) {
  std::string base;
  std::uint32_t flags = 0;
  double italic = 0;
  double weight = 400;
  auto loaded = ref ? document.get(*ref) : Result<const Object*>(nullptr);
  const Dict* font = loaded && *loaded != nullptr ? (*loaded)->as_dict() : nullptr;
  if (font != nullptr) {
    base = std::string(font::detail::without_subset_tag(name_of(font->find("BaseFont"))));
    const Dict* holder = font;
    Result<Object> descendant = Object();
    if (name_of(font->find("Subtype")) == "Type0") {
      if (const Object* d = font->find("DescendantFonts")) {
        auto list = document.resolve(*d);
        if (list && list->as_array() != nullptr && !list->as_array()->empty()) {
          descendant = document.resolve(list->as_array()->front());
          holder = descendant && descendant->as_dict() != nullptr ? descendant->as_dict() : font;
        }
      }
    }
    if (const Object* d = holder->find("FontDescriptor")) {
      auto descriptor = document.resolve(*d);
      if (descriptor && descriptor->as_dict() != nullptr) {
        const Dict& dd = *descriptor->as_dict();
        flags = static_cast<std::uint32_t>(dd.find("Flags") != nullptr ? dd.find("Flags")->as_integer().value_or(0) : 0);
        italic = dd.find("ItalicAngle") != nullptr ? dd.find("ItalicAngle")->as_number().value_or(0) : 0;
        weight = dd.find("FontWeight") != nullptr ? dd.find("FontWeight")->as_number().value_or(400) : 400;
      }
    }
  }
  using enum font::detail::StandardFont;
  static constexpr std::array<std::string_view, 14> kNames = {
      "Helvetica",        "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Times-Roman",         "Times-Bold", "Times-Italic",
      "Times-BoldItalic", "Courier",        "Courier-Bold",      "Courier-Oblique",       "Courier-BoldOblique", "Helvetica",  "Helvetica"};
  // Symbol and Dingbats cannot write Latin text: Helvetica in their place.
  const auto standard = font::detail::substitute_font(base, flags, italic, weight);
  return std::string(kNames.at(static_cast<std::size_t>(standard)));
}

// ── The same font ─────────────────────────────────────────────────────────────────────────────────

// The codes that write a text in a font: each character's code, the one whose glyph the font draws and
// whose meaning (ToUnicode, encoding) is that character. nullopt when a character has none. widths: the
// codes whose width the dictionary leaves at 0 (a code the document never used), with the program's.
std::optional<std::string> encode_in(font::detail::Font& font, font::detail::FontContext& context, std::u32string_view text, bool two_byte,
                                     std::map<std::uint32_t, double>& widths) {
  std::map<char32_t, std::string> codes;
  const auto add = [&](std::uint32_t code, std::uint8_t length) {
    const font::detail::Font::Code c{.code = code, .length = length, .cid = code};
    const std::u32string meaning = font.text(c);
    if (meaning.size() != 1 || codes.contains(meaning[0])) {
      return;
    }
    if (font.glyph_source(c, context).glyph == 0 && font.kind() != font::detail::Font::Kind::kType3) {
      return;
    }
    std::string bytes;
    if (length == 2) {
      bytes.push_back(static_cast<char>((code >> 8U) & 0xFFU));
    }
    bytes.push_back(static_cast<char>(code & 0xFFU));
    codes.emplace(meaning[0], std::move(bytes));
  };
  if (two_byte) {
    for (std::uint32_t code = 0; code < 0x10000; ++code) {
      add(code, 2);
    }
  } else {
    for (std::uint32_t code = 0; code < 256; ++code) {
      add(code, 1);
    }
  }
  std::string out;
  for (const char32_t c : text) {
    const auto found = codes.find(c);
    if (found == codes.end()) {
      return std::nullopt;
    }
    out += found->second;
    if (!two_byte && font.kind() == font::detail::Font::Kind::kSimple) {
      const auto code = static_cast<std::uint8_t>(found->second.back());
      const font::detail::Font::Code fc{.code = code, .length = 1, .cid = code};
      if (font.width(fc) == 0 && c != U' ') {
        const auto source = font.glyph_source(fc, context);
        const auto advance = source.program != nullptr ? source.program->advance(source.glyph) : std::nullopt;
        if (!advance || *advance <= 0) {
          return std::nullopt;
        }
        widths[code] = *advance;
      }
    }
  }
  return out;
}

// Whether the widths of codes a font's /Widths leaves at 0 can be given: its FirstChar and Widths are
// there and hold the codes, and the page shows none of them in that font (a damaged font draws its text
// so, overlapped: giving them widths would move it).
bool widths_writable(Document& document, ObjectRef font_ref, const std::map<std::uint32_t, double>& widths, const graphics::DisplayList& list) {
  if (widths.empty()) {
    return true;
  }
  auto loaded = document.get(font_ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return false;
  }
  const Dict& font = *(*loaded)->as_dict();
  const Object* first_entry = font.find("FirstChar");
  const Object* widths_entry = font.find("Widths");
  const std::optional<std::int64_t> first_char = first_entry != nullptr ? first_entry->as_integer() : std::nullopt;
  if (!first_char || widths_entry == nullptr) {
    return false;
  }
  auto array = document.resolve(*widths_entry);
  if (!array || array->as_array() == nullptr) {
    return false;
  }
  const std::int64_t first = *first_char;
  for (const auto& entry : widths) {
    const std::int64_t at = static_cast<std::int64_t>(entry.first) - first;
    if (at < 0 || std::cmp_greater_equal(at, array->as_array()->size())) {
      return false;
    }
  }
  for (const auto& command : list.commands) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
    if (t != nullptr && (*t)->font_ref == font_ref && std::ranges::any_of((*t)->glyphs, [&](const graphics::Glyph& g) { return widths.contains(g.code); })) {
      return false;
    }
  }
  return true;
}

// A simple font's /Widths given the widths of codes it left at 0.
Result<void> set_widths(Document& document, ObjectRef font_ref, const std::map<std::uint32_t, double>& widths) {
  if (widths.empty()) {
    return {};
  }
  auto loaded = document.get(font_ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the font is not a dictionary");
  }
  Dict font = *(*loaded)->as_dict();
  const auto first = font.find("FirstChar") != nullptr ? font.find("FirstChar")->as_integer().value_or(0) : 0;
  const Object* entry = font.find("Widths");
  auto resolved = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!resolved || resolved->as_array() == nullptr) {
    return make_error(ErrorCode::kUnsupported, "the font gives no widths");
  }
  Array array = *resolved->as_array();
  for (const auto& [code, width] : widths) {
    const std::int64_t at = static_cast<std::int64_t>(code) - first;
    if (at < 0 || std::cmp_greater_equal(at, array.size())) {
      return make_error(ErrorCode::kUnsupported, "a code outside the font's widths");
    }
    array[static_cast<std::size_t>(at)] = detail::number_object(std::round(width * 1000) / 1000);
  }
  if (const auto ref = entry->as_ref()) {
    return document.set(ref->number, Object(std::move(array)));
  }
  font.set("Widths", std::move(array));
  return document.set(font_ref.number, Object(std::move(font)));
}

// An operator's operands written anew: the bytes [from, to) of its strings (joined) replaced.
std::optional<std::string> rewritten_operands(const content::Operation& op, std::size_t from, std::size_t to, const std::string& codes) {
  const auto hex = [](std::string bytes) { return writer::serialize(Object(model::String{.bytes = std::move(bytes), .form = model::StringForm::kHex})); };
  std::string out;
  if (op.op == Operator::kShowTextArray) {
    const Array* array = !op.operands.empty() ? op.operands.back().as_array() : nullptr;
    if (array == nullptr) {
      return std::nullopt;
    }
    std::string written = "[";
    std::size_t at = 0;
    bool inserted = false;
    for (const Object& item : *array) {
      if (const auto* s = item.as_string()) {
        const std::size_t begin = at;
        const std::size_t end = at + s->bytes.size();
        at = end;
        std::string piece;
        if (begin < from) {
          piece += s->bytes.substr(0, std::min(end, from) - begin);
        }
        if (!inserted && from >= begin && from <= end) {
          piece += codes;
          inserted = true;
        }
        if (end > to) {
          piece += s->bytes.substr(std::max(begin, to) - begin);
        }
        if (!piece.empty()) {
          written += hex(std::move(piece));
        }
      } else if (const auto n = item.as_number()) {
        // A displacement inside the text replaced goes with it.
        if (at > from && at < to) {
          continue;
        }
        written += " " + num(*n) + " ";
      }
    }
    if (!inserted) {
      return std::nullopt;
    }
    return written + "]";
  }
  const model::String* s = !op.operands.empty() ? op.operands.back().as_string() : nullptr;
  if (s == nullptr || from > s->bytes.size() || to > s->bytes.size()) {
    return std::nullopt;
  }
  if (op.op == Operator::kNextLineSpacingShowText) {
    if (op.operands.size() != 3) {
      return std::nullopt;
    }
    out = writer::serialize(op.operands[0]) + " " + writer::serialize(op.operands[1]) + " ";
  }
  return out + hex(s->bytes.substr(0, from) + codes + s->bytes.substr(to));
}

struct Selection {
  std::vector<std::size_t> chars;  // indices in the page's text, in reading order
  std::u32string old_text;
};

// The bytes the chosen glyphs take in their operator's strings, [low, high] (where the last one's code
// starts): when they all come from one operator of the page's own content, in one font, contiguously.
std::optional<std::pair<std::uint32_t, std::uint32_t>> chosen_bytes(const text::PageText& text, const Selection& selection) {
  const auto& first = text.chars[selection.chars.front()].provenance;
  std::uint32_t low = UINT32_MAX;
  std::uint32_t high = 0;
  for (const std::size_t i : selection.chars) {
    const auto& p = text.chars[i].provenance;
    if (p.stream || first.stream || p.operation != first.operation || p.font != first.font || !p.font) {
      return std::nullopt;
    }
    low = std::min(low, p.byte);
    high = std::max(high, p.byte);
  }
  const std::set<std::size_t> chosen(selection.chars.begin(), selection.chars.end());
  for (std::size_t i = 0; i < text.chars.size(); ++i) {
    const auto& p = text.chars[i].provenance;
    if (!p.stream && p.operation == first.operation && p.byte >= low && p.byte <= high && !chosen.contains(i)) {
      return std::nullopt;  // a glyph of the operator inside the selection that is not chosen
    }
  }
  return std::pair{low, high};
}

// Whether new text of that width keeps clear of the text further on the line that other operators show.
bool fits_on_line(const text::PageText& text, const Selection& selection, std::size_t line_end, double new_width) {
  double old_width = 0;
  for (const std::size_t i : selection.chars) {
    old_width += text.chars[i].box.width();
  }
  if (new_width <= old_width + 0.5) {
    return true;
  }
  const auto& last = text.chars[selection.chars.back()];
  const std::uint32_t operation = text.chars[selection.chars.front()].provenance.operation;
  for (std::size_t i = selection.chars.back() + 1; i < line_end; ++i) {
    const auto& next = text.chars[i];
    if (next.provenance.stream || next.provenance.operation != operation) {
      return next.box.x0 - last.box.x1 >= new_width - old_width;
    }
  }
  return true;
}

// The page's content with one operator's operands written anew, every other byte kept; nullopt when the
// operator cannot be found and rewritten so.
Result<std::optional<std::string>> content_with(Document& document, const detail::Page& page, std::uint32_t operation, std::size_t from, std::size_t to,
                                                const std::string& codes) {
  auto data = content::page_content(document, page.ref);
  if (!data) {
    return std::unexpected(std::move(data.error()));
  }
  auto operations = content::parse_content(*data);
  if (!operations || operation >= operations->size()) {
    return std::optional<std::string>();
  }
  const content::Operation& op = (*operations)[operation];
  if (!shows_text(op.op)) {
    return std::optional<std::string>();
  }
  // Where its operands begin: just after the operator before it (none after an inline image, whose data
  // the parser does not place).
  std::size_t begin = 0;
  if (operation > 0) {
    const content::Operation& previous = (*operations)[operation - 1];
    if (previous.op == Operator::kInlineImage) {
      return std::optional<std::string>();
    }
    begin = static_cast<std::size_t>(previous.offset) + previous.name.size();
  }
  const std::size_t end = static_cast<std::size_t>(op.offset) + op.name.size();
  if (begin > end || end > data->size()) {
    return std::optional<std::string>();
  }
  auto operands = rewritten_operands(op, from, to, codes);
  if (!operands) {
    return std::optional<std::string>();
  }
  const std::string_view bytes = crypto::der::chars_of(*data);
  std::string written(bytes.substr(0, begin));
  written += " " + *operands + " " + op.name;
  written += bytes.substr(end);
  return std::optional<std::string>(std::move(written));
}

// The page's content replaced by one stream of that content.
Result<void> set_content(Document& document, const detail::Page& page, const std::string& content) {
  auto compressed = filters::flate_encode(std::as_bytes(std::span(content)));
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  model::Stream stream;
  stream.dict.set("Filter", name("FlateDecode"));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto ref = document.add(Object(std::move(stream)));
  if (!ref) {
    return std::unexpected(std::move(ref.error()));
  }
  Dict updated = page.dict;
  updated.set("Contents", *ref);
  return document.set(page.ref.number, Object(std::move(updated)));
}

Result<std::optional<TextEditResult>> same_font(Document& document, const detail::Page& page, const text::PageText& text, const graphics::DisplayList& list,
                                                const Selection& selection, std::u32string_view replacement, std::size_t line_end) {
  const auto bytes = chosen_bytes(text, selection);
  if (!bytes) {
    return std::optional<TextEditResult>();
  }
  const auto& first = text.chars[selection.chars.front()].provenance;
  // The run the operator showed, for the font size.
  const graphics::ShowText* run = nullptr;
  for (std::size_t c = 0; c < list.commands.size() && run == nullptr; ++c) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&list.commands[c]);
    if (t != nullptr && !(*t)->stream && (*t)->operation == first.operation) {
      run = &**t;
    }
  }
  if (run == nullptr || run->glyphs.empty() || !first.font) {
    return std::optional<TextEditResult>();
  }
  const ObjectRef font_ref = *first.font;
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  auto font = font::detail::Font::load(document, Object(font_ref), context, warnings);
  if (!font || font->vertical()) {
    return std::optional<TextEditResult>();
  }
  auto loaded = document.get(font_ref);
  const Dict* dict = loaded && (*loaded)->as_dict() != nullptr ? (*loaded)->as_dict() : nullptr;
  const bool two_byte = font->kind() == font::detail::Font::Kind::kComposite;
  // Composite fonts only when their codes are two bytes, as Identity-H: the codes then are what is written.
  if (two_byte && (dict == nullptr || name_of(dict->find("Encoding")) != "Identity-H")) {
    return std::optional<TextEditResult>();
  }
  const std::string base_font = dict != nullptr ? std::string(font::detail::without_subset_tag(name_of(dict->find("BaseFont")))) : std::string();
  std::map<std::uint32_t, double> missing_widths;
  const auto codes = encode_in(*font, context, replacement, two_byte, missing_widths);
  if (!codes || !widths_writable(document, font_ref, missing_widths, list)) {
    return std::optional<TextEditResult>();
  }
  double new_width = 0;
  std::vector<font::detail::Font::Code> split;
  font->codes(*codes, split);
  for (const auto& c : split) {
    const auto given = missing_widths.find(c.code);
    new_width += (given != missing_widths.end() ? given->second : font->width(c)) * font->scale() * run->font_size * run->horizontal_scale;
  }
  if (!fits_on_line(text, selection, line_end, new_width)) {
    return std::optional<TextEditResult>();
  }
  // The last glyph chosen ends a code's length after it starts.
  const std::size_t length = two_byte ? 2 : 1;
  auto content = content_with(document, page, first.operation, bytes->first, bytes->second + length, *codes);
  if (!content) {
    return std::unexpected(std::move(content.error()));
  }
  if (!*content) {
    return std::optional<TextEditResult>();
  }
  if (auto set = set_content(document, page, **content); !set) {
    return std::unexpected(std::move(set.error()));
  }
  if (auto set = set_widths(document, font_ref, missing_widths); !set) {
    return std::unexpected(std::move(set.error()));
  }
  return std::optional<TextEditResult>(
      TextEditResult{.method = TextEditMethod::kSameFont, .font = base_font, .replaced = model::to_utf8(selection.old_text), .scale = 1});
}

// ── A similar font ────────────────────────────────────────────────────────────────────────────────

Result<TextEditResult> similar(Document& document, std::uint32_t page_index, const text::PageText& text, const graphics::DisplayList& list,
                               const Selection& selection, std::u32string_view replacement, std::size_t line_end) {
  // What WinAnsi cannot write cannot be written so.
  const std::string codes = detail::win_ansi_bytes(replacement);
  for (std::size_t i = 0; i < replacement.size(); ++i) {
    if (codes[i] == '?' && replacement[i] != U'?') {
      return make_error(ErrorCode::kUnsupported, "the new text has characters the standard fonts cannot write, and the font of the old text has not");
    }
  }
  const text::Char& start = text.chars[selection.chars.front()];
  const std::string font_name = similar_font(document, start.provenance.font);
  // The colour of the run the first character was shown by.
  graphics::Rgb colour{0, 0, 0};
  for (const auto& command : list.commands) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
    if (t != nullptr && (*t)->stream == start.provenance.stream && (*t)->operation == start.provenance.operation) {
      if ((*t)->fill < list.colors.size()) {
        colour = list.colors[(*t)->fill].rgb;
      }
      break;
    }
  }
  // The room: up to the next text on the line (other than what is replaced), else the old width.
  double room = 0;
  for (const std::size_t i : selection.chars) {
    room += text.chars[i].box.width();
  }
  const text::Char& last = text.chars[selection.chars.back()];
  if (selection.chars.back() + 1 < line_end) {
    room = std::max(room, text.chars[selection.chars.back() + 1].box.x0 - start.box.x0 - (start.size * 0.15));
  } else {
    room = std::max(room, last.box.x1 - start.box.x0);
  }

  // The old glyphs go, the rest staying where it is.
  std::vector<Rect> areas;
  for (const std::size_t i : selection.chars) {
    const Rect& b = text.chars[i].box;
    const Point c = centre(b);
    const double w = std::max(b.width() * 0.25, 0.05);
    const double h = std::max(b.height() * 0.25, 0.05);
    areas.emplace_back(c.x - w, c.y - h, c.x + w, c.y + h);
  }
  auto removed = detail::remove_glyphs(document, page_index, areas);
  if (!removed) {
    return std::unexpected(std::move(removed.error()));
  }
  TextEditResult result{.method = TextEditMethod::kSimilarFont, .font = font_name, .replaced = model::to_utf8(selection.old_text), .scale = 1};
  if (replacement.empty()) {
    return result;
  }

  // The new text: the standard font, in the page's resources under a name of its own.
  Dict font;
  font.set("Type", name("Font"));
  font.set("Subtype", name("Type1"));
  font.set("BaseFont", name(font_name));
  font.set("Encoding", name("WinAnsiEncoding"));
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  auto loaded_font = font::detail::Font::load(document, Object(font), context, warnings);
  double width = 0;
  if (loaded_font) {
    for (const char c : codes) {
      width += loaded_font->width({.code = static_cast<std::uint8_t>(c), .length = 1, .cid = static_cast<std::uint8_t>(c)}) * 0.001 * start.size;
    }
  }
  if (width > room && width > 0) {
    result.scale = std::max(kMinScale, room / width);
  }
  auto font_ref = document.add(Object(std::move(font)));
  if (!font_ref) {
    return std::unexpected(std::move(font_ref.error()));
  }
  auto page = detail::page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  Dict updated = page->dict;
  Dict resources;
  if (auto r = detail::inherited(document, page->dict, "Resources")) {
    auto resolved = document.resolve(*r);
    if (resolved && resolved->as_dict() != nullptr) {
      resources = *resolved->as_dict();
    }
  }
  Dict fonts;
  if (const Object* f = resources.find("Font")) {
    auto resolved = document.resolve(*f);
    if (resolved && resolved->as_dict() != nullptr) {
      fonts = *resolved->as_dict();
    }
  }
  std::string key;
  for (int n = 0;; ++n) {
    key = std::format("CprText{}", n);
    if (fonts.find(key) == nullptr) {
      break;
    }
  }
  fonts.set(key, *font_ref);
  resources.set("Font", std::move(fonts));
  updated.set("Resources", std::move(resources));

  // Where it goes: the first old glyph's origin and direction, from the page as shown to page space.
  const double radians = start.angle * std::numbers::pi / 180.0;
  const Point along_view{std::cos(radians), std::sin(radians)};
  const Point up_view{std::sin(radians), -std::cos(radians)};
  const graphics::Matrix& m = page->to_user;
  const Point along{(m.a * along_view.x) + (m.c * along_view.y), (m.b * along_view.x) + (m.d * along_view.y)};
  const Point up{(m.a * up_view.x) + (m.c * up_view.y), (m.b * up_view.x) + (m.d * up_view.y)};
  const Point origin = page->user(start.origin);
  // The page's content may leave graphics states open (q without Q): they are closed first, so that the
  // new text is drawn in the page's own space.
  std::size_t open_states = 0;
  if (auto data = content::page_content(document, page->ref)) {
    if (auto operations = content::parse_content(*data)) {
      for (const content::Operation& op : *operations) {
        if (op.op == Operator::kSave) {
          ++open_states;
        } else if (op.op == Operator::kRestore && open_states > 0) {
          --open_states;
        }
      }
    }
  }
  std::string closing;
  for (std::size_t i = 0; i < std::min<std::size_t>(open_states, 64); ++i) {
    closing += "Q\n";
  }
  const std::string text_operator = closing + std::format("q {} rg BT /{} {} Tf {} Tz {} {} {} {} {} {} Tm {} Tj ET Q\n",
                                                          detail::rgb_operands({colour.r, colour.g, colour.b}), key, num(start.size), num(result.scale * 100),
                                                          num(along.x), num(along.y), num(up.x), num(up.y), num(origin.x), num(origin.y), detail::hex(codes));
  model::Stream stream;
  auto compressed = filters::flate_encode(std::as_bytes(std::span(text_operator)));
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  stream.dict.set("Filter", name("FlateDecode"));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto stream_ref = document.add(Object(std::move(stream)));
  if (!stream_ref) {
    return std::unexpected(std::move(stream_ref.error()));
  }
  Array contents;
  if (const Object* c = page->dict.find("Contents")) {
    auto resolved = document.resolve(*c);
    if (resolved && resolved->as_array() != nullptr) {
      contents = *resolved->as_array();
    } else {
      contents.push_back(*c);
    }
  }
  contents.emplace_back(*stream_ref);
  updated.set("Contents", std::move(contents));
  if (auto set = document.set(page->ref.number, Object(std::move(updated))); !set) {
    return std::unexpected(std::move(set.error()));
  }
  return result;
}

}  // namespace

Result<TextEditResult> replace_text(Document& document, std::uint32_t page_index, const Rect& area, std::string_view text) {
  const Rect r = area.normalized();
  if (!std::isfinite(r.x0) || !std::isfinite(r.y0) || !std::isfinite(r.x1) || !std::isfinite(r.y1) || r.width() <= 0 || r.height() <= 0) {
    return make_error(ErrorCode::kUnsupported, "an area must be finite and not empty");
  }
  if (text.size() > kMaxText * 4) {
    return make_error(ErrorCode::kLimitExceeded, "the new text is too long");
  }
  const std::u32string replacement = model::from_utf8(text);
  if (std::ranges::any_of(replacement, [](char32_t c) { return c == U'\n' || c == U'\r'; })) {
    return make_error(ErrorCode::kUnsupported, "one line at a time: the new text has a line break");
  }
  auto page = detail::page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto list = content::interpret_page(document, page->ref, {}, nullptr, content::Purpose::kText);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  const text::PageText page_text = text::extract(*list, text::Order::kContent);

  // The characters chosen, and the line they are on (one).
  Selection selection;
  std::optional<std::size_t> line;
  std::size_t line_begin = 0;
  std::size_t line_end = 0;
  for (std::size_t l = 0; l < page_text.lines.size(); ++l) {
    const text::Line& ln = page_text.lines[l];
    if (ln.count == 0 || ln.first + ln.count > page_text.words.size()) {
      continue;
    }
    const std::size_t first_char = page_text.words[ln.first].first;
    const text::Word& last_word = page_text.words[ln.first + ln.count - 1];
    const std::size_t end_char = last_word.first + last_word.count;
    for (std::size_t i = first_char; i < end_char && i < page_text.chars.size(); ++i) {
      if (!inside(centre(page_text.chars[i].box), r)) {
        continue;
      }
      if (page_text.chars[i].invisible) {
        return make_error(ErrorCode::kUnsupported, "the text there is invisible (a scan's text layer)");
      }
      if (line && *line != l) {
        return make_error(ErrorCode::kUnsupported, "one line at a time: the area holds text of several lines");
      }
      line = l;
      line_begin = first_char;
      line_end = end_char;
      selection.chars.push_back(i);
      selection.old_text += page_text.chars[i].text;
    }
  }
  if (selection.chars.empty()) {
    return make_error(ErrorCode::kOutOfRange, "no text in the area");
  }
  // Text an annotation draws (a form field's value, a signature's appearance) is not the page's content:
  // neither way could edit it. A character is the page's when an operator of the page's content, or of a
  // form it draws, shows it.
  std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> content_text;
  for (std::size_t c = 0; c < list->commands.size() && c < list->origins.size(); ++c) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&list->commands[c]);
    if (t == nullptr) {
      continue;
    }
    bool of_content = true;
    for (std::uint32_t at = list->origins[c].invocation, steps = 0; at != 0 && steps < list->invocations.size(); at = list->invocations[at].parent, ++steps) {
      if (at >= list->invocations.size() || list->invocations[at].kind != graphics::Invocation::Kind::kForm) {
        of_content = false;
        break;
      }
    }
    if (of_content) {
      content_text.emplace((*t)->stream ? std::optional((*t)->stream->number) : std::nullopt, (*t)->operation);
    }
  }
  for (const std::size_t i : selection.chars) {
    const auto& p = page_text.chars[i].provenance;
    if (!content_text.contains({p.stream ? std::optional(p.stream->number) : std::nullopt, p.operation})) {
      return make_error(ErrorCode::kUnsupported, "the text there is an annotation's (a form field, a signature): it is edited there");
    }
  }
  (void)line_begin;
  auto same = same_font(document, *page, page_text, *list, selection, replacement, line_end);
  if (!same) {
    return std::unexpected(std::move(same.error()));
  }
  if (*same) {
    return std::move(**same);
  }
  return similar(document, page_index, page_text, *list, selection, replacement, line_end);
}

}  // namespace capriole::edit
