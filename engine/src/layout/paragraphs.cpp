#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/graphics/display_list.hpp"
#include "capriole/layout/page_model.hpp"
#include "capriole/text/text.hpp"
#include "drawn.hpp"
#include "edit/common.hpp"

namespace capriole::layout {

namespace {

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0;
  }
  std::ranges::sort(values);
  return values[values.size() / 2];
}

Alignment alignment_of(const text::PageText& text, const Paragraph& p) {
  const std::size_t n = p.lines.size();
  if (n < 2) {
    return Alignment::kLeft;
  }
  const double tolerance = std::max(1.0, 0.15 * p.size);
  bool lefts = true;   // the lines after the first start at the left margin
  bool rights = true;  // the lines before the last end at the right margin
  bool ends = true;    // every line ends at the right margin
  bool centres = true;
  bool varied = false;  // the starts differ
  const double middle = (text.lines[p.lines.front()].box.x0 + text.lines[p.lines.front()].box.x1) / 2;
  for (std::size_t k = 0; k < n; ++k) {
    const graphics::Rect& b = text.lines[p.lines[k]].box;
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
    return Alignment::kJustified;
  }
  if (varied && centres) {
    return Alignment::kCentre;
  }
  if (varied && ends) {
    return Alignment::kRight;
  }
  return Alignment::kLeft;
}

}  // namespace

Paragraph paragraph_of(const text::PageText& text, const graphics::DisplayList& list, std::span<const std::size_t> lines) {
  Paragraph p;
  const auto refuse = [&](const char* why) {
    if (p.refusal.empty()) {
      p.refusal = why;
    }
  };
  for (const std::size_t l : lines) {
    if (l < text.lines.size()) {
      p.lines.push_back(l);
    }
  }
  if (p.lines.empty()) {
    p.refusal = "no text in the area";
    return p;
  }
  const auto operators = detail::content_operators(list);
  // The run each character was shown by (its colour and horizontal scaling).
  std::map<std::pair<std::optional<std::uint32_t>, std::uint32_t>, const graphics::ShowText*> runs;
  for (const auto& command : list.commands) {
    if (const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command)) {
      runs.emplace(std::pair{(*t)->stream ? std::optional((*t)->stream->number) : std::nullopt, (*t)->operation}, &**t);
    }
  }
  const auto style_of = [&](const text::Char& ch) {
    Style style{.font = ch.provenance.font, .size = ch.size};
    const auto found = runs.find({ch.provenance.stream ? std::optional(ch.provenance.stream->number) : std::nullopt, ch.provenance.operation});
    if (found != runs.end()) {
      if (found->second->fill < list.colors.size()) {
        style.colour = list.colors[found->second->fill].rgb;
      }
      style.th = found->second->horizontal_scale > 0 ? found->second->horizontal_scale : 1;
    }
    const auto same = [&](const Style& o) {
      return o.font == style.font && std::abs(o.size - style.size) < 0.05 && std::abs(o.colour.r - style.colour.r) < 0.004 &&
             std::abs(o.colour.g - style.colour.g) < 0.004 && std::abs(o.colour.b - style.colour.b) < 0.004 && std::abs(o.th - style.th) < 0.005;
    };
    const auto at = std::ranges::find_if(p.styles, same);
    if (at != p.styles.end()) {
      return static_cast<std::size_t>(at - p.styles.begin());
    }
    p.styles.push_back(style);
    return p.styles.size() - 1;
  };
  std::vector<std::size_t> letters_of_style;
  std::vector<double> ascents;
  std::vector<double> descents;
  std::vector<double> gaps;
  for (const std::size_t l : p.lines) {
    const text::Line& line = text.lines[l];
    std::vector<double> origins;
    for (std::size_t w = line.first; w < line.first + line.count && w < text.words.size(); ++w) {
      const text::Word& word = text.words[w];
      if (w > line.first) {
        gaps.push_back(word.box.x0 - text.words[w - 1].box.x1);
      }
      Word old;
      for (std::size_t c = word.first; c < word.first + word.count && c < text.chars.size(); ++c) {
        const text::Char& ch = text.chars[c];
        if (ch.invisible) {
          refuse("the text there is invisible (a scan's text layer)");
        }
        if (!detail::of_content(operators, ch.provenance)) {
          // An annotation's appearance (a form field, a signature): its text is the annotation's.
          refuse("the text there is an annotation's (a form field, a signature): it is edited there");
          continue;
        }
        if (ch.angle != 0) {
          refuse("only horizontal paragraphs are laid out anew");
        }
        const std::size_t style = style_of(ch);
        if (letters_of_style.size() < p.styles.size()) {
          letters_of_style.resize(p.styles.size());
        }
        letters_of_style[style] += ch.text.size();
        old.text += ch.text;
        old.styles.insert(old.styles.end(), ch.text.size(), style);
        origins.push_back(ch.origin.y);
        ascents.push_back(ch.origin.y - ch.box.y0);
        descents.push_back(ch.box.y1 - ch.origin.y);
        p.chars.push_back(c);
      }
      // A word broken at the previous line's end ("infor-" + "mazione"): one word again.
      const bool continues = w == line.first && !p.words.empty() && !p.words.back().text.empty() &&
                             (p.words.back().text.back() == U'-' || p.words.back().text.back() == U'­') && !old.text.empty() &&
                             edit::detail::lowercase(old.text.front());
      if (continues) {
        p.words.back().text.pop_back();
        p.words.back().styles.pop_back();
        p.words.back().text += old.text;
        p.words.back().styles.insert(p.words.back().styles.end(), old.styles.begin(), old.styles.end());
      } else if (!old.text.empty()) {
        p.words.push_back(std::move(old));
      }
    }
    const double baseline = origins.empty() ? line.box.y1 : median(origins);
    // Superscripts and subscripts: off the baseline, which plain text cannot say.
    for (std::size_t i = p.chars.size() - origins.size(); i < p.chars.size(); ++i) {
      const text::Char& ch = text.chars[p.chars[i]];
      if (std::abs(ch.origin.y - baseline) > 0.15 * ch.size) {
        refuse("the paragraph has superscripts or subscripts: edit its lines one by one");
      }
    }
    p.baselines.push_back(baseline);
  }
  if (p.chars.empty()) {
    refuse("no text in the area");
    return p;
  }
  p.main_style = static_cast<std::size_t>(std::ranges::max_element(letters_of_style) - letters_of_style.begin());
  p.size = p.styles[p.main_style].size;
  p.ascent = *std::ranges::max_element(ascents);
  p.descent = *std::ranges::max_element(descents);
  const graphics::Rect& first = text.lines[p.lines.front()].box;
  p.right = first.x1;
  p.left = first.x0;
  for (std::size_t k = 0; k < p.lines.size(); ++k) {
    const graphics::Rect& b = text.lines[p.lines[k]].box;
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
    refuse("the paragraph's lines do not run downwards");
  }
  p.alignment = alignment_of(text, p);
  // The gap between words as set: a justified paragraph's last line, or any line of another.
  if (p.alignment == Alignment::kJustified) {
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

}  // namespace capriole::layout
