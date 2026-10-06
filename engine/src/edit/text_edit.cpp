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
#include "text_common.hpp"
#include "text_removal.hpp"

namespace capriole::edit {

namespace {

using content::Operator;
using detail::encode_in;
using detail::name;
using detail::num;
using detail::set_widths;
using detail::similar_font;
using detail::widths_writable;
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

// ── The same font ─────────────────────────────────────────────────────────────────────────────────

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
  const graphics::Rgb colour = detail::fill_of(list, start.provenance);
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
  auto key = detail::add_font_resource(document, page_index, *font_ref);
  if (!key) {
    return std::unexpected(std::move(key.error()));
  }
  auto page = detail::page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  // Where it goes: the first old glyph's origin and direction, from the page as shown to page space.
  const double radians = start.angle * std::numbers::pi / 180.0;
  const Point along_view{std::cos(radians), std::sin(radians)};
  const Point up_view{std::sin(radians), -std::cos(radians)};
  const graphics::Matrix& m = page->to_user;
  const Point along{(m.a * along_view.x) + (m.c * along_view.y), (m.b * along_view.x) + (m.d * along_view.y)};
  const Point up{(m.a * up_view.x) + (m.c * up_view.y), (m.b * up_view.x) + (m.d * up_view.y)};
  const Point origin = page->user(start.origin);
  const std::string text_operator =
      std::format("q {} rg BT /{} {} Tf {} Tz {} {} {} {} {} {} Tm {} Tj ET Q\n", detail::rgb_operands({colour.r, colour.g, colour.b}), *key, num(start.size),
                  num(result.scale * 100), num(along.x), num(along.y), num(up.x), num(up.y), num(origin.x), num(origin.y), detail::hex(codes));
  if (auto appended = detail::append_content(document, page_index, text_operator); !appended) {
    return std::unexpected(std::move(appended.error()));
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
  // Lines as laid out on the page (not in the content's order): text drawn later where a line is — the
  // new text of an earlier edit, written at the end of the content — is on that line.
  const text::PageText page_text = text::extract(*list, text::Order::kLayout);

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
  // neither way could edit it.
  const auto content_ops = detail::content_operators(*list);
  for (const std::size_t i : selection.chars) {
    if (!detail::of_content(content_ops, page_text.chars[i].provenance)) {
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
