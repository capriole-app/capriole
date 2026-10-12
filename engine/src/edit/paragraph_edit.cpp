#include "capriole/edit/paragraph_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/layout/page_model.hpp"
#include "capriole/model/object.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"
#include "common.hpp"
#include "content_move.hpp"
#include "font/font.hpp"
#include "font_extend.hpp"
#include "text_common.hpp"
#include "text_removal.hpp"

namespace capriole::edit {

namespace {

using detail::name;
using detail::num;
using graphics::Point;
using graphics::Rect;
using model::Dict;
using model::Object;
using model::ObjectRef;

// How many bytes the new text may have, and how many lines it may make.
constexpr std::size_t kMaxText = 40'000;
constexpr std::size_t kMaxLines = 2'000;
// The steps shrinking takes (of the paragraph's size).
constexpr double kShrinkStep = 0.025;

std::string name_of(const Object* value) { return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string(); }

Point centre(const Rect& r) { return {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}; }

bool inside(const Point& p, const Rect& r) { return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1; }

using layout::Paragraph;
using layout::Style;

// The new text's words: letters [from, from + count) of the text without its spaces and line breaks.
struct Piece {
  std::size_t from = 0;
  std::size_t count = 0;
};

// A line laid out: its words, where it starts, the gap between its words (points as shown).
struct Placed {
  std::vector<Piece> words;
  double x = 0;
  double gap = 0;
};

// Letters in a font: each one's code (empty: the font cannot write it) and its width at size 1,
// horizontal scaling included.
struct Written {
  TextEditMethod method = TextEditMethod::kSameFont;
  std::string font;  // its name, for the result
  std::optional<ObjectRef> ref;
  Dict standard;  // the standard font's dictionary (similar font)
  std::vector<std::string> codes;
  std::vector<double> widths;
  std::map<std::uint32_t, double> missing_widths;
};

// The text's letters in the paragraph's own font: those it can write (nullopt when none, or its widths
// cannot be given).
std::optional<Written> in_own_font(Document& document, const graphics::DisplayList& list, ObjectRef ref, std::u32string_view letters, double th,
                                   const Object* extended = nullptr) {
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  // The font as it is, or as extended with glyphs of a font the host gave (edit/font_extend).
  auto font = font::detail::Font::load(document, extended != nullptr ? *extended : Object(ref), context, warnings);
  if (!font || font->vertical() || font->kind() == font::detail::Font::Kind::kType3) {
    return std::nullopt;
  }
  auto loaded = document.get(ref);
  const Dict* dict = loaded && (*loaded)->as_dict() != nullptr ? (*loaded)->as_dict() : nullptr;
  if (extended != nullptr) {
    dict = extended->as_dict();
  }
  const bool two_byte = font->kind() == font::detail::Font::Kind::kComposite;
  // Composite fonts only when their codes are two bytes, as Identity-H: the codes then are what is written.
  if (dict == nullptr || (two_byte && name_of(dict->find("Encoding")) != "Identity-H")) {
    return std::nullopt;
  }
  Written w;
  w.font = std::string(font::detail::without_subset_tag(name_of(dict->find("BaseFont"))));
  w.ref = ref;
  auto codes = detail::encode_each(*font, context, letters, two_byte, w.missing_widths);
  if (!detail::widths_writable(document, ref, w.missing_widths, list)) {
    return std::nullopt;
  }
  std::vector<font::detail::Font::Code> split;
  for (auto& code : codes) {
    double width = 0;
    if (code) {
      split.clear();
      font->codes(*code, split);
      if (split.size() != 1) {
        code.reset();
      } else {
        const auto given = w.missing_widths.find(split.front().code);
        width = (given != w.missing_widths.end() ? given->second : font->width(split.front())) * font->scale() * th;
      }
    }
    w.codes.push_back(code.value_or(std::string()));
    w.widths.push_back(width);
  }
  if (std::ranges::all_of(w.codes, [](const std::string& c) { return c.empty(); })) {
    return std::nullopt;
  }
  return w;
}

// The text's letters in the standard font nearest the paragraph's (what WinAnsi encodes only).
Result<Written> in_similar_font(Document& document, const std::optional<ObjectRef>& ref, std::u32string_view letters, double th) {
  const std::string codes = detail::win_ansi_bytes(letters);
  for (std::size_t i = 0; i < letters.size(); ++i) {
    if (codes[i] == '?' && letters[i] != U'?') {
      return make_error(ErrorCode::kUnsupported, "the new text has characters the standard fonts cannot write, and the font of the paragraph has not");
    }
  }
  Written w;
  w.method = TextEditMethod::kSimilarFont;
  w.font = detail::similar_font(document, ref);
  for (const char c : codes) {
    w.codes.emplace_back(1, c);
  }
  w.standard.set("Type", name("Font"));
  w.standard.set("Subtype", name("Type1"));
  w.standard.set("BaseFont", name(w.font));
  w.standard.set("Encoding", name("WinAnsiEncoding"));
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  auto font = font::detail::Font::load(document, Object(w.standard), context, warnings);
  if (!font) {
    return make_error(ErrorCode::kUnsupported, "the standard font " + w.font + " cannot be loaded");
  }
  for (const char c : codes) {
    const auto code = static_cast<std::uint8_t>(c);
    w.widths.push_back(font->width({.code = code, .length = 1, .cid = code}) * font->scale() * th);
  }
  return w;
}

// Lays the words out in lines (segments: the text's lines, each its words); points: each letter's width.
std::vector<Placed> lay_out(const std::vector<std::vector<Piece>>& segments, const std::vector<double>& points, const Paragraph& p, double gap) {
  const auto width_of = [&](const Piece& piece) {
    double sum = 0;
    for (std::size_t i = piece.from; i < piece.from + piece.count; ++i) {
      sum += points[i];
    }
    return sum;
  };
  const auto start_of = [&](std::size_t line) { return line == 0 ? p.left + p.indent : p.left; };
  std::vector<Placed> out;
  std::vector<bool> ends_segment;
  for (const auto& words : segments) {
    std::vector<Piece> current;
    double current_width = 0;
    const auto flush = [&](bool last) {
      out.push_back({.words = std::move(current)});
      ends_segment.push_back(last);
      current = {};
      current_width = 0;
    };
    for (const Piece& word : words) {
      if (out.size() > kMaxLines) {
        break;
      }
      const double room = p.right - start_of(out.size());
      double ww = width_of(word);
      if (!current.empty() && current_width + gap + ww <= room + 0.01) {
        current.push_back(word);
        current_width += gap + ww;
        continue;
      }
      if (!current.empty()) {
        flush(false);
      }
      // A word wider than a line: split where it no longer fits (no hyphenation).
      Piece rest = word;
      while (ww > p.right - start_of(out.size()) + 0.01 && rest.count > 1 && out.size() <= kMaxLines) {
        const double line_room = p.right - start_of(out.size());
        std::size_t take = 0;
        double used = 0;
        while (take < rest.count && used + points[rest.from + take] <= line_room + 0.01) {
          used += points[rest.from + take];
          ++take;
        }
        take = std::max<std::size_t>(take, 1);
        current = {{.from = rest.from, .count = take}};
        flush(false);
        rest = {.from = rest.from + take, .count = rest.count - take};
        ww = width_of(rest);
      }
      current = {rest};
      current_width = ww;
    }
    flush(true);
  }
  // Where each line starts, and its gaps (justified: the line spread to the margin, but a text's last).
  for (std::size_t k = 0; k < out.size(); ++k) {
    Placed& line = out[k];
    const double start = start_of(k);
    const double room = p.right - start;
    double natural = 0;
    for (const Piece& word : line.words) {
      natural += width_of(word);
    }
    const std::size_t gaps = line.words.empty() ? 0 : line.words.size() - 1;
    natural += gap * static_cast<double>(gaps);
    line.gap = gap;
    line.x = start;
    switch (p.alignment) {
      case ParagraphAlignment::kJustified:
        if (!ends_segment[k] && gaps > 0) {
          line.gap = gap + ((room - natural) / static_cast<double>(gaps));
        }
        break;
      case ParagraphAlignment::kCentre:
        line.x = start + ((room - natural) / 2);
        break;
      case ParagraphAlignment::kRight:
        line.x = p.right - natural;
        break;
      case ParagraphAlignment::kLeft:
        break;
    }
  }
  return out;
}

// How far down (as shown) new lines may go: to the first thing drawn below the paragraph, across its
// width — text, a path, an image —, or the bottom of what contains it (a cell, a box), or of the page.
double room_below(const text::PageText& text, const graphics::DisplayList& list, const detail::Page& page, const Paragraph& p) {
  const double x0 = std::min(p.left, p.left + p.indent);
  const double x1 = p.right;
  double limit = text.height;
  const auto consider = [&](const Rect& b) {
    if (b.x1 < x0 + 0.5 || b.x0 > x1 - 0.5) {
      return;  // beside the paragraph
    }
    if (b.y0 >= p.bottom - 0.5) {
      limit = std::min(limit, b.y0);  // below it
    } else if (b.y1 > p.bottom + 0.5 && b.y0 <= p.top + 0.5) {
      limit = std::min(limit, b.y1);  // around it: what contains it
    }
  };
  const std::set<std::size_t> own(p.chars.begin(), p.chars.end());
  for (std::size_t i = 0; i < text.chars.size(); ++i) {
    if (!own.contains(i) && !text.chars[i].invisible) {
      consider(text.chars[i].box);
    }
  }
  for (const auto& command : list.commands) {
    if (const auto* fill = std::get_if<graphics::FillPath>(&command)) {
      if (const auto b = detail::path_bounds(fill->path)) {
        consider(detail::transformed(*b, fill->ctm.then(page.to_view)));
      }
    } else if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
      if (const auto b = detail::path_bounds(stroke->path)) {
        consider(detail::transformed(*b, stroke->ctm.then(page.to_view)));
      }
    } else if (const auto* image = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      consider(detail::transformed(Rect{0, 0, 1, 1}, (*image)->ctm.then(page.to_view)));
    }
  }
  return limit;
}

// Selects the paragraph: the lines the area touches, whole; checks they can be edited.
Result<Paragraph> paragraph_in(const text::PageText& text, const graphics::DisplayList& list, const Rect& area) {
  std::set<std::size_t> touched;
  for (std::size_t l = 0; l < text.lines.size(); ++l) {
    const text::Line& line = text.lines[l];
    for (std::size_t w = line.first; w < line.first + line.count && w < text.words.size(); ++w) {
      for (std::size_t c = text.words[w].first; c < text.words[w].first + text.words[w].count && c < text.chars.size(); ++c) {
        if (inside(centre(text.chars[c].box), area)) {
          touched.insert(l);
        }
      }
    }
  }
  if (touched.empty()) {
    return make_error(ErrorCode::kOutOfRange, "no text in the area");
  }
  // One block, its lines consecutive.
  std::optional<std::size_t> block;
  for (std::size_t b = 0; b < text.blocks.size(); ++b) {
    const text::Block& bl = text.blocks[b];
    if (*touched.begin() >= bl.first && *touched.rbegin() < bl.first + bl.count) {
      block = b;
    }
  }
  if (!block || *touched.rbegin() - *touched.begin() + 1 != touched.size()) {
    return make_error(ErrorCode::kUnsupported, "one paragraph at a time: the area holds lines of several paragraphs, or skips a line");
  }
  const std::vector<std::size_t> lines(touched.begin(), touched.end());
  Paragraph p = layout::paragraph_of(text, list, lines);
  if (!p.refusal.empty()) {
    return make_error(ErrorCode::kUnsupported, p.refusal);
  }
  return p;
}

// The style of each letter of the new text (its words, each its letters): a word kept from the old text
// keeps its letters' styles; a word in place of an old one takes that one's (the style most of its letters
// have); a word added takes the previous word's last letter's (at the start, the next kept word's).
std::vector<std::size_t> styles_for(const Paragraph& p, const std::vector<std::u32string>& words) {
  const std::size_t n = p.words.size();
  const std::size_t m = words.size();
  // The longest run of words both share (in order), word for word; on very long texts only what they
  // share at the start and the end.
  std::vector<std::optional<std::size_t>> kept(m);
  if ((n + 1) * (m + 1) <= 4'000'000) {
    std::vector<std::uint32_t> table((n + 1) * (m + 1), 0);
    const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return table[(i * (m + 1)) + j]; };
    for (std::size_t i = n; i-- > 0;) {
      for (std::size_t j = m; j-- > 0;) {
        at(i, j) = p.words[i].text == words[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
      }
    }
    for (std::size_t i = 0, j = 0; i < n && j < m;) {
      if (p.words[i].text == words[j]) {
        kept[j] = i++;
        ++j;
      } else if (at(i + 1, j) >= at(i, j + 1)) {
        ++i;
      } else {
        ++j;
      }
    }
  } else {
    std::size_t front = 0;
    while (front < n && front < m && p.words[front].text == words[front]) {
      kept[front] = front;
      ++front;
    }
    for (std::size_t back = 1; back <= n - front && back <= m - front && p.words[n - back].text == words[m - back]; ++back) {
      kept[m - back] = n - back;
    }
  }
  const auto most = [&](const layout::Word& w) {
    std::map<std::size_t, std::size_t> count;
    for (const std::size_t s : w.styles) {
      ++count[s];
    }
    return std::ranges::max_element(count, {}, [](const auto& e) { return e.second; })->first;
  };
  std::vector<std::vector<std::size_t>> styles(m);
  std::optional<std::size_t> previous_old;  // the last old word kept so far
  std::size_t in_gap = 0;                   // words since it
  for (std::size_t j = 0; j < m; ++j) {
    if (kept[j]) {
      styles[j] = p.words[*kept[j]].styles;
      previous_old = kept[j];
      in_gap = 0;
      continue;
    }
    // The old words between the kept ones around it: the k-th new word there replaces the k-th old one.
    const std::size_t from = previous_old ? *previous_old + 1 : 0;
    std::size_t to = n;
    for (std::size_t k = j + 1; k < m; ++k) {
      if (kept[k]) {
        to = *kept[k];
        break;
      }
    }
    std::size_t style = p.main_style;
    if (from < to) {
      style = most(p.words[std::min(from + in_gap, to - 1)]);
    } else if (j > 0 && !styles[j - 1].empty()) {
      style = styles[j - 1].back();
    } else {
      for (std::size_t k = j + 1; k < m; ++k) {
        if (kept[k]) {
          style = p.words[*kept[k]].styles.front();
          break;
        }
      }
    }
    styles[j].assign(words[j].size(), style);
    ++in_gap;
  }
  std::vector<std::size_t> out;
  for (const auto& w : styles) {
    out.insert(out.end(), w.begin(), w.end());
  }
  return out;
}

// The objects numbered from `first` on that nothing from the page reaches any more go: what moving what
// follows wrote and the removal of the old glyphs then wrote anew (the page's content, a form's copy).
Result<void> drop_unreached(Document& document, ObjectRef page_ref, std::uint32_t first, std::uint32_t end) {
  std::set<std::uint32_t> reached;
  std::vector<const Object*> pending;
  auto loaded = document.get(page_ref);
  if (!loaded || *loaded == nullptr) {
    return {};
  }
  pending.push_back(*loaded);
  while (!pending.empty()) {
    const Object* object = pending.back();
    pending.pop_back();
    if (const auto ref = object->as_ref()) {
      if (ref->number >= first && ref->number < end && reached.insert(ref->number).second) {
        if (auto target = document.get(*ref); target && *target != nullptr) {
          pending.push_back(*target);
        }
      }
    } else if (const auto* array = object->as_array()) {
      for (const Object& item : *array) {
        pending.push_back(&item);
      }
    } else if (const auto* dict = object->as_dict()) {
      for (const auto& entry : dict->entries()) {
        pending.push_back(&entry.value);
      }
    } else if (const auto* stream = object->as_stream()) {
      for (const auto& entry : stream->dict.entries()) {
        pending.push_back(&entry.value);
      }
    }
  }
  for (std::uint32_t n = first; n < end; ++n) {
    if (!reached.contains(n) && document.state(n) == ObjectState::kNew) {
      if (auto removed = document.remove(n); !removed) {
        return removed;
      }
    }
  }
  return {};
}

}  // namespace

Result<ParagraphEditResult> replace_paragraph(Document& document, std::uint32_t page_index, const Rect& area, std::string_view text,
                                              const ParagraphOptions& options) {
  const Rect r = area.normalized();
  if (!std::isfinite(r.x0) || !std::isfinite(r.y0) || !std::isfinite(r.x1) || !std::isfinite(r.y1) || r.width() <= 0 || r.height() <= 0) {
    return make_error(ErrorCode::kUnsupported, "an area must be finite and not empty");
  }
  if (text.size() > kMaxText) {
    return make_error(ErrorCode::kLimitExceeded, "the new text is too long");
  }
  auto page = detail::page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto list = content::interpret_page(document, page->ref, {}, nullptr, content::Purpose::kText);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  // Lines as laid out (as edit/text_edit reads them): text an earlier edit drew is on its line.
  const text::PageText page_text = text::extract(*list, text::Order::kLayout);
  auto selected = paragraph_in(page_text, *list, r);
  if (!selected) {
    return std::unexpected(std::move(selected.error()));
  }
  const Paragraph& p = *selected;

  // The old text, its words joined.
  std::string replaced;
  for (const layout::Word& w : p.words) {
    if (!replaced.empty()) {
      replaced += ' ';
    }
    replaced += model::to_utf8(w.text);
  }

  // The new text: its lines, their words, its letters.
  const std::u32string replacement = model::from_utf8(text);
  std::u32string letters;
  std::vector<std::vector<Piece>> segments(1);
  std::vector<std::u32string> words;
  std::optional<Piece> word;
  const auto end_word = [&] {
    if (word) {
      segments.back().push_back(*word);
      words.push_back(letters.substr(word->from, word->count));
      word.reset();
    }
  };
  for (const char32_t c : replacement) {
    if (c == U'\n') {
      end_word();
      segments.emplace_back();
    } else if (c == U' ' || c == U'\t' || c == U'\r' || c == U'\u00A0') {
      end_word();
    } else {
      if (!word) {
        word = Piece{.from = letters.size(), .count = 0};
      }
      ++word->count;
      letters += c;
    }
  }
  end_word();

  // Each letter's style; each style's letters written in its own font when it can — extended with the
  // glyphs it lacks from a font the host gave, when one is the same font —, those it cannot in the
  // standard font nearest it. A writer: 2 · style (its own font), 2 · style + 1 (the similar one).
  const std::vector<std::size_t> style_of = styles_for(p, words);
  std::vector<std::u32string> style_letters(p.styles.size());
  std::vector<std::vector<std::size_t>> style_at(p.styles.size());
  for (std::size_t i = 0; i < letters.size(); ++i) {
    style_letters[style_of[i]] += letters[i];
    style_at[style_of[i]].push_back(i);
  }
  // What each font lacks (styles may share a font: extended once, with all they lack).
  std::map<std::uint32_t, detail::FontExtension> extensions;  // by the font's object number
  if (options.fonts != nullptr) {
    std::map<std::uint32_t, std::pair<ObjectRef, std::u32string>> lacking;
    for (std::size_t s = 0; s < p.styles.size(); ++s) {
      const Style& style = p.styles[s];
      if (!style.font || style_letters[s].empty()) {
        continue;
      }
      const auto written = in_own_font(document, *list, *style.font, style_letters[s], style.th);
      for (std::size_t k = 0; k < style_letters[s].size(); ++k) {
        if (!written || written->codes[k].empty()) {
          auto& [ref, missing] = lacking.try_emplace(style.font->number, *style.font, std::u32string()).first->second;
          missing += style_letters[s][k];
        }
      }
    }
    for (const auto& [number, need] : lacking) {
      auto extension = detail::extend_font(document, need.first, need.second, *options.fonts);
      if (!extension) {
        return std::unexpected(std::move(extension.error()));
      }
      if (*extension) {
        extensions.emplace(number, std::move(**extension));
      }
    }
  }
  std::vector<std::string> codes(letters.size());
  std::vector<double> widths(letters.size());  // at size 1, horizontal scaling included
  std::vector<std::size_t> writer_of(letters.size());
  std::map<std::size_t, Written> writers;
  std::set<std::string> missing_fonts;
  for (std::size_t s = 0; s < p.styles.size(); ++s) {
    const std::u32string& own = style_letters[s];
    const std::vector<std::size_t>& at = style_at[s];
    if (own.empty()) {
      continue;
    }
    const Style& style = p.styles[s];
    std::optional<Written> written;
    if (style.font) {
      const auto extension = extensions.find(style.font->number);
      written = in_own_font(document, *list, *style.font, own, style.th, extension != extensions.end() ? &extension->second.font : nullptr);
    }
    std::u32string rest;
    std::vector<std::size_t> rest_at;
    for (std::size_t k = 0; k < at.size(); ++k) {
      if (written && !written->codes[k].empty()) {
        codes[at[k]] = written->codes[k];
        widths[at[k]] = written->widths[k];
        writer_of[at[k]] = 2 * s;
      } else {
        rest += own[k];
        rest_at.push_back(at[k]);
      }
    }
    if (written) {
      writers.emplace(2 * s, std::move(*written));
    }
    if (rest.empty()) {
      continue;
    }
    if (style.font && detail::extendable(document, *style.font)) {
      missing_fonts.insert(detail::font_name(document, *style.font));  // a font the host could give
    }
    auto similar = in_similar_font(document, style.font, rest, style.th);
    if (!similar) {
      return std::unexpected(std::move(similar.error()));
    }
    for (std::size_t k = 0; k < rest_at.size(); ++k) {
      codes[rest_at[k]] = similar->codes[k];
      widths[rest_at[k]] = similar->widths[k];
      writer_of[rest_at[k]] = (2 * s) + 1;
    }
    writers.emplace((2 * s) + 1, std::move(*similar));
  }
  const auto points_at = [&](double f) {
    std::vector<double> points(letters.size());
    for (std::size_t i = 0; i < letters.size(); ++i) {
      points[i] = widths[i] * p.styles[style_of[i]].size * f;
    }
    return points;
  };

  // Laid out at the paragraph's sizes, or smaller when allowed and needed; it must fit.
  // New lines keep above what follows the space the paragraph left before it, up to a line's pitch: the
  // paragraph stays apart from what is below (else it reads as one with it, and touches it).
  const double below = room_below(page_text, *list, *page, p);
  const double keep = std::max(0.1 * p.size, std::min(std::max(below - p.bottom, 0.0), p.pitch));
  const double limit = std::max(p.bottom, below - keep);
  const auto bottom_of = [&](std::size_t lines, double f) {
    const double first = p.top + (p.ascent * f);
    return first + (static_cast<double>(lines - 1) * p.pitch * f) + (p.descent * f);
  };
  double factor = 1;
  std::vector<Placed> placed = lay_out(segments, points_at(1), p, p.gap);
  const auto fits = [&](const std::vector<Placed>& lines, double f) { return lines.size() <= kMaxLines && bottom_of(lines.size(), f) <= limit + 0.01; };
  std::optional<detail::MovePlan> move;
  if (!fits(placed, 1) && options.move && placed.size() <= kMaxLines) {
    // What follows moves down as far as the paragraph grows (keeping the space it had above it), at least
    // as far as the new lines need.
    const double grown = static_cast<double>(placed.size()) - static_cast<double>(p.lines.size());
    const double by = std::max(grown * p.pitch, bottom_of(placed.size(), 1) - limit);
    std::vector<Rect> glyphs;
    glyphs.reserve(p.chars.size());
    for (const std::size_t i : p.chars) {
      glyphs.push_back(page_text.chars[i].box);
    }
    auto planned = detail::plan_move(document, page_index,
                                     {.top = p.top,
                                      .cut = p.bottom,
                                      .x0 = std::min(p.left, p.left + p.indent),
                                      .x1 = p.right,
                                      .by = by,
                                      .bottom = bottom_of(placed.size(), 1),
                                      .pitch = p.pitch,
                                      .glyphs = glyphs});
    if (!planned) {
      return std::unexpected(std::move(planned.error()));
    }
    move = std::move(*planned);
  } else if (!fits(placed, 1)) {
    bool done = false;
    if (options.shrink) {
      for (int step = 1;; ++step) {
        const double f = 1 - (step * kShrinkStep);
        if (f < kMinShrink - 1e-9) {
          break;
        }
        auto attempt = lay_out(segments, points_at(f), p, p.gap * f);
        if (fits(attempt, f)) {
          placed = std::move(attempt);
          factor = f;
          done = true;
          break;
        }
      }
    }
    if (!done) {
      std::size_t room = 0;
      while (room < kMaxLines && bottom_of(room + 1, 1) <= limit + 0.01) {
        ++room;
      }
      return make_error(ErrorCode::kUnsupported, std::format("no room: the new text needs {} lines, {} fit", placed.size(), room));
    }
  }

  // The fonts extended (only now, the edit being done), then the old glyphs go, the rest staying where it is.
  std::size_t added_glyphs = 0;
  for (const auto& [number, extension] : extensions) {
    if (auto committed = detail::commit(document, extension); !committed) {
      return std::unexpected(std::move(committed.error()));
    }
    added_glyphs += extension.added;
  }
  const std::uint32_t move_first = document.next_object_number();
  if (move) {
    if (auto moved = detail::apply_move(document, page_index, *move); !moved) {
      return std::unexpected(std::move(moved.error()));
    }
  }
  const std::uint32_t move_end = document.next_object_number();
  std::vector<Rect> areas;
  for (const std::size_t i : p.chars) {
    const Rect& b = page_text.chars[i].box;
    const Point c = centre(b);
    const double half_w = std::max(b.width() * 0.25, 0.05);
    const double half_h = std::max(b.height() * 0.25, 0.05);
    areas.emplace_back(c.x - half_w, c.y - half_h, c.x + half_w, c.y + half_h);
  }
  auto removed = detail::remove_glyphs(document, page_index, areas);
  if (!removed) {
    return std::unexpected(std::move(removed.error()));
  }
  if (auto dropped = drop_unreached(document, page->ref, move_first, move_end); !dropped) {
    return std::unexpected(std::move(dropped.error()));
  }
  // Styles written, and of them those all in their own font.
  std::set<std::size_t> written_styles;
  std::set<std::size_t> partly_similar;
  for (const auto& entry : writers) {
    written_styles.insert(entry.first / 2);
    if (entry.first % 2 == 1) {
      partly_similar.insert(entry.first / 2);
    }
  }
  const std::size_t same_fonts = written_styles.size() - partly_similar.size();
  const auto main = writers.contains(2 * p.main_style) ? writers.find(2 * p.main_style) : writers.begin();
  ParagraphEditResult result{.method = partly_similar.empty() ? TextEditMethod::kSameFont : TextEditMethod::kSimilarFont,
                             .font = main != writers.end() ? main->second.font : std::string(),
                             .replaced = std::move(replaced),
                             .old_lines = static_cast<std::uint32_t>(p.lines.size()),
                             .lines = letters.empty() ? 0U : static_cast<std::uint32_t>(placed.size()),
                             .size = p.size * factor,
                             .scale = factor,
                             .alignment = p.alignment,
                             .styles = static_cast<std::uint32_t>(written_styles.size()),
                             .same_font_styles = static_cast<std::uint32_t>(same_fonts),
                             .added_glyphs = static_cast<std::uint32_t>(added_glyphs),
                             .missing_fonts = std::vector<std::string>(missing_fonts.begin(), missing_fonts.end()),
                             .moved = move ? move->by : 0,
                             .moved_items = move ? move->items : 0,
                             .moved_annotations = move ? static_cast<std::uint32_t>(move->annotations.size()) : 0};
  if (letters.empty()) {
    return result;
  }

  // Each style's font a resource of the page.
  std::map<std::size_t, std::string> keys;
  for (auto& [s, w] : writers) {
    ObjectRef font_ref{};
    if (w.ref) {
      font_ref = *w.ref;
      if (auto set = detail::set_widths(document, font_ref, w.missing_widths); !set) {
        return std::unexpected(std::move(set.error()));
      }
    } else {
      auto added = document.add(Object(w.standard));
      if (!added) {
        return std::unexpected(std::move(added.error()));
      }
      font_ref = *added;
    }
    auto key = detail::add_font_resource(document, page_index, font_ref);
    if (!key) {
      return std::unexpected(std::move(key.error()));
    }
    keys.emplace(s, std::move(*key));
  }
  auto updated = detail::page_at(document, page_index, "the page's text");
  if (!updated) {
    return std::unexpected(std::move(updated.error()));
  }
  const graphics::Matrix& m = updated->to_user;
  // Rightwards and upwards on the page as shown, in page space.
  const Point along{m.a, m.b};
  const Point up{-m.c, -m.d};

  // The lines on the paragraph's baselines; the font, size, scaling and colour set where the writer changes.
  std::string operators = "q BT 0 Tc 0 Tw 0 Ts 0 Tr\n";
  std::optional<std::size_t> current;
  for (std::size_t k = 0; k < placed.size(); ++k) {
    const Placed& line = placed[k];
    if (line.words.empty()) {
      continue;
    }
    const Point origin = updated->user(Point{line.x, p.top + (p.ascent * factor) + (static_cast<double>(k) * p.pitch * factor)});
    operators += std::format("{} {} {} {} {} {} Tm\n", num(along.x), num(along.y), num(up.x), num(up.y), num(origin.x), num(origin.y));
    for (std::size_t i = 0; i < line.words.size(); ++i) {
      const Piece& piece = line.words[i];
      for (std::size_t at = piece.from; at < piece.from + piece.count;) {
        const std::size_t id = writer_of[at];
        std::size_t end = at;
        std::string run;
        while (end < piece.from + piece.count && writer_of[end] == id) {
          run += codes[end++];
        }
        const Style& style = p.styles[id / 2];
        if (current != id) {
          operators += std::format("/{} {} Tf {} Tz {} rg\n", keys.at(id), num(style.size * factor), num(style.th * 100),
                                   detail::rgb_operands({style.colour.r, style.colour.g, style.colour.b}));
          current = id;
        }
        operators += "[";
        if (i > 0 && at == piece.from) {
          // The gap before the word, in thousandths of its text space (TJ moves left by a positive number).
          operators += num(-line.gap / (style.size * factor * style.th) * 1000) + " ";
        }
        operators += detail::hex(run) + "] TJ\n";
        at = end;
      }
    }
  }
  operators += "ET Q\n";
  if (auto appended = detail::append_content(document, page_index, operators); !appended) {
    return std::unexpected(std::move(appended.error()));
  }
  return result;
}

}  // namespace capriole::edit
