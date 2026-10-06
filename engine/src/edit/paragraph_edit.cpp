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
#include "capriole/model/object.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"
#include "common.hpp"
#include "font/font.hpp"
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

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0;
  }
  std::ranges::sort(values);
  return values[values.size() / 2];
}

// The paragraph as it is on the page (as shown: y down).
struct Paragraph {
  std::vector<std::size_t> lines;  // in the page's text
  std::vector<std::size_t> chars;
  std::vector<double> baselines;  // of each line
  double left = 0;                // of the lines after the first
  double indent = 0;              // of the first line, from left (negative: hanging)
  double right = 0;
  double top = 0;     // of the first line's glyph boxes
  double bottom = 0;  // of the last line's
  double ascent = 0;  // above a baseline, below it (the glyph boxes')
  double descent = 0;
  double pitch = 0;  // from baseline to baseline
  double size = 0;
  double gap = 0;  // between words, as set (not justified)
  ParagraphAlignment alignment = ParagraphAlignment::kLeft;
};

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

// The text in a font: the codes of each letter (`length` bytes each) and its width at size 1, horizontal
// scaling included.
struct Written {
  TextEditMethod method = TextEditMethod::kSameFont;
  std::string font;  // its name, for the result
  std::optional<ObjectRef> ref;
  Dict standard;  // the standard font's dictionary (similar font)
  std::string codes;
  std::size_t length = 1;
  std::vector<double> widths;
  std::map<std::uint32_t, double> missing_widths;
};

ParagraphAlignment alignment_of(const text::PageText& text, const Paragraph& p) {
  const std::size_t n = p.lines.size();
  if (n < 2) {
    return ParagraphAlignment::kLeft;
  }
  const double tolerance = std::max(1.0, 0.15 * p.size);
  bool lefts = true;   // the lines after the first start at the left margin
  bool rights = true;  // the lines before the last end at the right margin
  bool ends = true;    // every line ends at the right margin
  bool centres = true;
  bool varied = false;  // the starts differ
  const double middle = (text.lines[p.lines.front()].box.x0 + text.lines[p.lines.front()].box.x1) / 2;
  for (std::size_t k = 0; k < n; ++k) {
    const Rect& b = text.lines[p.lines[k]].box;
    if (k > 0 && std::abs(b.x0 - p.left) > tolerance) {
      lefts = false;
    }
    if (k + 1 < n && std::abs(b.x1 - p.right) > tolerance) {
      rights = false;
    }
    if (std::abs(b.x1 - p.right) > tolerance) {
      ends = false;
    }
    if (std::abs(((b.x0 + b.x1) / 2) - middle) > tolerance) {
      centres = false;
    }
    if (std::abs(b.x0 - text.lines[p.lines.front()].box.x0) > tolerance) {
      varied = true;
    }
  }
  // Justified: at least two lines (not the last) end at the margin — with two lines in all, the first
  // always does (the margin is where the longest ends).
  if (lefts && rights && n >= 3) {
    return ParagraphAlignment::kJustified;
  }
  if (varied && centres) {
    return ParagraphAlignment::kCentre;
  }
  if (varied && ends) {
    return ParagraphAlignment::kRight;
  }
  return ParagraphAlignment::kLeft;
}

// The text's letters in the paragraph's own font (nullopt when it cannot write them all).
std::optional<Written> in_own_font(Document& document, const graphics::DisplayList& list, ObjectRef ref, std::u32string_view letters, double th) {
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  auto font = font::detail::Font::load(document, Object(ref), context, warnings);
  if (!font || font->vertical() || font->kind() == font::detail::Font::Kind::kType3) {
    return std::nullopt;
  }
  auto loaded = document.get(ref);
  const Dict* dict = loaded && (*loaded)->as_dict() != nullptr ? (*loaded)->as_dict() : nullptr;
  const bool two_byte = font->kind() == font::detail::Font::Kind::kComposite;
  // Composite fonts only when their codes are two bytes, as Identity-H: the codes then are what is written.
  if (dict == nullptr || (two_byte && name_of(dict->find("Encoding")) != "Identity-H")) {
    return std::nullopt;
  }
  Written w;
  w.font = std::string(font::detail::without_subset_tag(name_of(dict->find("BaseFont"))));
  w.ref = ref;
  w.length = two_byte ? 2U : 1U;
  auto codes = detail::encode_in(*font, context, letters, two_byte, w.missing_widths);
  if (!codes || !detail::widths_writable(document, ref, w.missing_widths, list)) {
    return std::nullopt;
  }
  w.codes = std::move(*codes);
  std::vector<font::detail::Font::Code> split;
  font->codes(w.codes, split);
  if (split.size() != letters.size()) {
    return std::nullopt;
  }
  for (const auto& c : split) {
    const auto given = w.missing_widths.find(c.code);
    w.widths.push_back((given != w.missing_widths.end() ? given->second : font->width(c)) * font->scale() * th);
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
  w.codes = codes;
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

// Lays the words out in lines (segments: the text's lines, each its words), the text at `size`.
std::vector<Placed> lay_out(const std::vector<std::vector<Piece>>& segments, const Written& w, const Paragraph& p, double size, double gap) {
  const auto width_of = [&](const Piece& piece) {
    double sum = 0;
    for (std::size_t i = piece.from; i < piece.from + piece.count; ++i) {
      sum += w.widths[i];
    }
    return sum * size;
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
        while (take < rest.count && used + (w.widths[rest.from + take] * size) <= line_room + 0.01) {
          used += w.widths[rest.from + take] * size;
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
  Paragraph p;
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
  p.lines.assign(touched.begin(), touched.end());
  const auto operators = detail::content_operators(list);
  std::optional<std::tuple<std::optional<ObjectRef>, long, graphics::Rgb>> style;
  std::vector<double> ascents;
  std::vector<double> descents;
  std::vector<double> gaps;
  for (const std::size_t l : p.lines) {
    const text::Line& line = text.lines[l];
    std::optional<double> baseline;
    for (std::size_t w = line.first; w < line.first + line.count && w < text.words.size(); ++w) {
      const text::Word& word = text.words[w];
      if (w > line.first) {
        gaps.push_back(word.box.x0 - text.words[w - 1].box.x1);
      }
      for (std::size_t c = word.first; c < word.first + word.count && c < text.chars.size(); ++c) {
        const text::Char& ch = text.chars[c];
        if (ch.invisible) {
          return make_error(ErrorCode::kUnsupported, "the text there is invisible (a scan's text layer)");
        }
        if (!detail::of_content(operators, ch.provenance)) {
          return make_error(ErrorCode::kUnsupported, "the text there is an annotation's (a form field, a signature): it is edited there");
        }
        if (ch.angle != 0) {
          return make_error(ErrorCode::kUnsupported, "only horizontal paragraphs are laid out anew");
        }
        const auto key = std::make_tuple(ch.provenance.font, std::lround(ch.size * 10), detail::fill_of(list, ch.provenance));
        if (style && *style != key) {
          return make_error(ErrorCode::kUnsupported, "the paragraph has more than one font, size or colour: edit its lines one by one");
        }
        style = key;
        if (!baseline) {
          baseline = ch.origin.y;
        }
        ascents.push_back(ch.origin.y - ch.box.y0);
        descents.push_back(ch.box.y1 - ch.origin.y);
        p.chars.push_back(c);
      }
    }
    p.baselines.push_back(baseline.value_or(line.box.y1));
  }
  p.size = text.chars[p.chars.front()].size;
  p.ascent = *std::ranges::max_element(ascents);
  p.descent = *std::ranges::max_element(descents);
  const Rect& first = text.lines[p.lines.front()].box;
  p.right = first.x1;
  p.left = first.x0;
  for (std::size_t k = 0; k < p.lines.size(); ++k) {
    const Rect& b = text.lines[p.lines[k]].box;
    p.right = std::max(p.right, b.x1);
    if (k == 1) {
      p.left = b.x0;
    } else if (k > 1) {
      p.left = std::min(p.left, b.x0);
    }
  }
  p.indent = first.x0 - p.left;
  p.top = p.baselines.front() - p.ascent;
  p.bottom = p.baselines.back() + p.descent;
  std::vector<double> pitches;
  for (std::size_t k = 1; k < p.baselines.size(); ++k) {
    pitches.push_back(p.baselines[k] - p.baselines[k - 1]);
  }
  p.pitch = pitches.empty() ? 1.2 * p.size : median(pitches);
  if (p.pitch <= 0) {
    return make_error(ErrorCode::kUnsupported, "the paragraph's lines do not run downwards");
  }
  p.alignment = alignment_of(text, p);
  // The gap between words as set: a justified paragraph's last line, or any line of another.
  if (p.alignment == ParagraphAlignment::kJustified) {
    gaps.clear();
    const text::Line& last = text.lines[p.lines.back()];
    for (std::size_t w = last.first + 1; w < last.first + last.count && w < text.words.size(); ++w) {
      gaps.push_back(text.words[w].box.x0 - text.words[w - 1].box.x1);
    }
  }
  const double gap = median(gaps);
  p.gap = gap > 0.05 * p.size && gap < p.size ? gap : 0.25 * p.size;
  return p;
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
  const text::Char& start = page_text.chars[p.chars.front()];

  // The old text, its lines joined.
  std::string replaced;
  for (const std::size_t l : p.lines) {
    const text::Line& line = page_text.lines[l];
    for (std::size_t w = line.first; w < line.first + line.count && w < page_text.words.size(); ++w) {
      if (!replaced.empty()) {
        replaced += ' ';
      }
      replaced += model::to_utf8(page_text.words[w].text);
    }
  }

  // The new text: its lines, their words, its letters.
  const std::u32string replacement = model::from_utf8(text);
  std::u32string letters;
  std::vector<std::vector<Piece>> segments(1);
  std::optional<Piece> word;
  const auto end_word = [&] {
    if (word) {
      segments.back().push_back(*word);
      word.reset();
    }
  };
  for (const char32_t c : replacement) {
    if (c == U'\n') {
      end_word();
      segments.emplace_back();
    } else if (c == U' ' || c == U'\t' || c == U'\r' || c == U' ') {
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

  // The horizontal scaling of the paragraph's text.
  double th = 1;
  for (const auto& command : list->commands) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
    if (t != nullptr && (*t)->stream == start.provenance.stream && (*t)->operation == start.provenance.operation) {
      th = (*t)->horizontal_scale > 0 ? (*t)->horizontal_scale : 1;
      break;
    }
  }
  std::optional<Written> written;
  if (start.provenance.font && !letters.empty()) {
    written = in_own_font(document, *list, *start.provenance.font, letters, th);
  }
  if (!written) {
    auto similar = in_similar_font(document, start.provenance.font, letters, th);
    if (!similar) {
      return std::unexpected(std::move(similar.error()));
    }
    written = std::move(*similar);
  }
  const Written& w = *written;

  // Laid out at the paragraph's size, or smaller when allowed and needed; it must fit.
  const double limit = std::max(p.bottom, room_below(page_text, *list, *page, p) - (0.1 * p.size));
  const auto bottom_of = [&](std::size_t lines, double f) {
    const double first = p.top + (p.ascent * f);
    return first + (static_cast<double>(lines - 1) * p.pitch * f) + (p.descent * f);
  };
  double factor = 1;
  std::vector<Placed> placed = lay_out(segments, w, p, p.size, p.gap);
  const auto fits = [&](const std::vector<Placed>& lines, double f) { return lines.size() <= kMaxLines && bottom_of(lines.size(), f) <= limit + 0.01; };
  if (!fits(placed, 1)) {
    bool done = false;
    if (options.shrink) {
      for (int step = 1;; ++step) {
        const double f = 1 - (step * kShrinkStep);
        if (f < kMinShrink - 1e-9) {
          break;
        }
        auto attempt = lay_out(segments, w, p, p.size * f, p.gap * f);
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

  // The old glyphs go, the rest staying where it is.
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
  ParagraphEditResult result{.method = w.method,
                             .font = w.font,
                             .replaced = std::move(replaced),
                             .old_lines = static_cast<std::uint32_t>(p.lines.size()),
                             .lines = letters.empty() ? 0U : static_cast<std::uint32_t>(placed.size()),
                             .size = p.size * factor,
                             .scale = factor,
                             .alignment = p.alignment};
  if (letters.empty()) {
    return result;
  }

  // The new text: its font a resource of the page, its lines drawn on the paragraph's baselines.
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
  auto updated = detail::page_at(document, page_index, "the page's text");
  if (!updated) {
    return std::unexpected(std::move(updated.error()));
  }
  const graphics::Matrix& m = updated->to_user;
  // Rightwards and upwards on the page as shown, in page space.
  const Point along{m.a, m.b};
  const Point up{-m.c, -m.d};
  const double size = p.size * factor;
  const graphics::Rgb colour = detail::fill_of(*list, start.provenance);
  std::string operators =
      std::format("q {} rg BT /{} {} Tf {} Tz 0 Tc 0 Tw 0 Ts 0 Tr\n", detail::rgb_operands({colour.r, colour.g, colour.b}), *key, num(size), num(th * 100));
  for (std::size_t k = 0; k < placed.size(); ++k) {
    const Placed& line = placed[k];
    if (line.words.empty()) {
      continue;
    }
    const Point origin = updated->user(Point{line.x, p.top + (p.ascent * factor) + (static_cast<double>(k) * p.pitch * factor)});
    operators += std::format("{} {} {} {} {} {} Tm [", num(along.x), num(along.y), num(up.x), num(up.y), num(origin.x), num(origin.y));
    for (std::size_t i = 0; i < line.words.size(); ++i) {
      const Piece& piece = line.words[i];
      if (i > 0) {
        // The gap, in thousandths of text space (TJ moves left by a positive number).
        operators += num(-line.gap / (size * th) * 1000) + " ";
      }
      operators += detail::hex(std::string_view(w.codes).substr(piece.from * w.length, piece.count * w.length)) + " ";
    }
    operators += "] TJ\n";
  }
  operators += "ET Q\n";
  if (auto appended = detail::append_content(document, page_index, operators); !appended) {
    return std::unexpected(std::move(appended.error()));
  }
  return result;
}

}  // namespace capriole::edit
