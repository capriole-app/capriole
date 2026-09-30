#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/model/text_string.hpp"
#include "capriole/text/text.hpp"

namespace capriole::text {

namespace {

using graphics::Matrix;
using graphics::Point;
using graphics::Rect;

// Layout thresholds, in ems of the font size as shown: the usual proportions of set text (line pitch
// about 1.2 em, word spaces about 0.25 em, letter spacing and kerning well under 0.15 em). Gaps are
// measured from the end of a glyph's own width, character spacing (Tc) excluded, as MuPDF does.
constexpr double kWordGap = 0.15;        // a wider gap along the baseline ends a word
constexpr double kWordBaseline = 0.5;    // a word goes on across a smaller shift (a superscript: "word²")
constexpr double kBackwards = 1.0;       // moving back more than this ends a word too (overlaps within a word: CJK punctuation pulled
                                         // back half an em, combining marks)
constexpr double kSameBaseline = 0.35;   // baselines this close are one line
constexpr double kLineGap = 1.0;         // a wider gap on one baseline splits it into two lines (columns)
constexpr double kBlockPitch = 1.7;      // a line this close below a block's last one continues it
constexpr double kBlockSizeRatio = 1.6;  // ... when their sizes are within this ratio
constexpr double kDuplicate = 0.2;       // the same word this close is drawn twice (fake bold, shadows)
constexpr double kDuplicateGlyph = 0.3;  // the same glyph within this part of its width: drawn twice

// A glyph of the page, in its baseline's frame: s along the baseline, t across it (downwards for
// upright text); `order` is its place in the content.
struct Glyph {
  Char c;
  std::int32_t direction = 0;  // the angle, rounded to a degree: glyphs of different directions never join
  double s0 = 0;
  double s1 = 0;
  double t = 0;
  bool space_before = false;  // a space (or an explicit break) came before it
};

struct Frame {
  double ux = 1;
  double uy = 0;
  [[nodiscard]] double s(Point p) const { return (p.x * ux) + (p.y * uy); }
  [[nodiscard]] double t(Point p) const { return (-p.x * uy) + (p.y * ux); }
};

Frame frame_of(std::int32_t direction) {
  const double radians = direction * std::numbers::pi / 180;
  return {.ux = std::cos(radians), .uy = std::sin(radians)};
}

Rect unite(Rect a, Rect b) { return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)}; }

bool is_space(const std::u32string& text) {
  return std::ranges::all_of(text, [](char32_t c) { return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0 || c == 0x3000; });
}

// Latin ligatures (U+FB00–FB06) become their letters, as text is searched and copied.
std::u32string expand_ligatures(const std::u32string& text) {
  static const std::array<std::u32string_view, 7> kLigatures = {U"ff", U"fi", U"fl", U"ffi", U"ffl", U"st", U"st"};
  std::u32string out;
  for (const char32_t c : text) {
    if (c >= 0xFB00 && c <= 0xFB06) {
      out += kLigatures.at(c - 0xFB00);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// The glyphs of the display list in content order, skipping soft masks (they are not seen) and the
// cells of tiling patterns (pdftotext and MuPDF skip them too).
class Collector {
 public:
  Collector(const graphics::DisplayList& list, const TextLimits& limits) : list_(&list), limits_(&limits) {
    const graphics::OutputGeometry geometry = graphics::output_geometry(list, 1);
    view_ = geometry.page_to_output;
    width_ = geometry.width;
    height_ = geometry.height;
  }

  std::vector<Glyph> collect() {
    std::size_t mask_depth = 0;
    for (const graphics::Command& command : list_->commands) {
      if (std::holds_alternative<graphics::BeginSoftMask>(command)) {
        ++mask_depth;
      } else if (std::holds_alternative<graphics::EndSoftMask>(command)) {
        mask_depth = mask_depth > 0 ? mask_depth - 1 : 0;
      } else if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&command); run != nullptr && mask_depth == 0) {
        add(**run);
      }
    }
    flush_actual();
    return std::move(glyphs_);
  }

  [[nodiscard]] double width() const { return width_; }
  [[nodiscard]] double height() const { return height_; }

 private:
  void add(const graphics::ShowText& run) {
    if (run.actual_text != actual_) {
      flush_actual();
    }
    const Matrix m = run.text_matrix.then(view_);
    const auto linear = [&m](double x, double y) { return Point{(m.a * x) + (m.c * y), (m.b * x) + (m.d * y)}; };
    const Point em = linear(0, run.font_size);
    const double size = std::hypot(em.x, em.y);
    // The writing direction: along the text's x axis, or down its y axis for vertical fonts.
    // (Negative sizes and scales turn it around, as they do the advances.)
    const bool forwards = run.vertical ? run.font_size < 0 : run.font_size * run.horizontal_scale >= 0;
    const double sign = forwards ? 1 : -1;
    const Point along = run.vertical ? linear(0, sign) : linear(sign, 0);
    const double length = std::hypot(along.x, along.y);
    if (!(size > 1e-3) || !(length > 0) || !std::isfinite(size)) {
      return;  // text of size 0 (or a singular matrix) is not shown anywhere
    }
    double angle = std::atan2(along.y, along.x) * 180 / std::numbers::pi;
    angle = std::fmod(std::round(angle) + 360, 360);
    const auto direction = static_cast<std::int32_t>(angle);
    const Frame frame = frame_of(direction);
    const double fs = run.font_size;
    const bool invisible = run.render_mode == 3 || run.render_mode == 7;
    for (const graphics::Glyph& g : run.glyphs) {
      if (glyphs_.size() >= limits_->max_chars) {
        return;
      }
      const double ox = g.origin.x;
      const double oy = g.origin.y;
      Rect local;
      Point end;
      if (run.vertical) {
        double extent = g.width != 0 ? g.width : g.advance;
        if (extent == 0) {
          extent = -fs;  // no metrics: an em downwards
        }
        local = Rect{ox - (fs / 2), oy + extent, ox + (fs / 2), oy}.normalized();
        end = {ox, oy + extent};
      } else {
        const double extent = g.width != 0 ? g.width : g.advance;
        local = Rect{ox, oy + (run.descent * fs), ox + extent, oy + (run.ascent * fs)}.normalized();
        end = {ox + extent, oy};
      }
      Rect box{};
      bool first = true;
      for (const Point corner : {Point{local.x0, local.y0}, Point{local.x1, local.y0}, Point{local.x0, local.y1}, Point{local.x1, local.y1}}) {
        const Point p = m.apply(corner);
        box = first ? Rect{p.x, p.y, p.x, p.y} : unite(box, Rect{p.x, p.y, p.x, p.y});
        first = false;
      }
      // Off the page as shown (outside the crop box): not visible, not extracted (as pdftotext).
      const double cx = (box.x0 + box.x1) / 2;
      const double cy = (box.y0 + box.y1) / 2;
      if (!std::isfinite(cx) || !std::isfinite(cy) || cx < -1 || cy < -1 || cx > width_ + 1 || cy > height_ + 1) {
        continue;
      }
      const Point origin = m.apply(g.origin);
      const Point stop = m.apply(end);
      Glyph glyph{
          .c = Char{.text = expand_ligatures(g.text),
                    .box = box,
                    .origin = origin,
                    .size = size,
                    .angle = direction,
                    .invisible = invisible,
                    .provenance = Provenance{.stream = run.stream, .offset = run.offset, .operation = run.operation, .byte = g.byte, .font = run.font_ref}},
          .direction = direction,
          .s0 = std::min(frame.s(origin), frame.s(stop)),
          .s1 = std::max(frame.s(origin), frame.s(stop)),
          .t = frame.t(origin),
          .space_before = false,
      };
      if (run.actual_text >= 0) {
        // Inside /ActualText: the glyphs are one Char with that text (§14.9.4).
        if (actual_glyph_) {
          actual_glyph_->c.box = unite(actual_glyph_->c.box, box);
          actual_glyph_->s0 = std::min(actual_glyph_->s0, glyph.s0);
          actual_glyph_->s1 = std::max(actual_glyph_->s1, glyph.s1);
        } else {
          actual_glyph_ = std::move(glyph);
        }
        continue;
      }
      push(std::move(glyph));
    }
    actual_ = run.actual_text;
  }

  // A Char per glyph; spaces are not Chars but mark the next one. A glyph the font says nothing about
  // is U+FFFD (as MuPDF): the text is there, unreadable.
  void push(Glyph glyph, bool from_actual_text = false) {
    if (glyph.c.text.empty()) {
      if (from_actual_text) {
        return;  // /ActualText () : the glyphs stand for nothing
      }
      glyph.c.text = U"\uFFFD";
    }
    if (is_space(glyph.c.text)) {
      pending_space_ = true;
      return;
    }
    glyph.space_before = std::exchange(pending_space_, false);
    glyphs_.push_back(std::move(glyph));
  }

  void flush_actual() {
    if (actual_glyph_) {
      const std::u32string& text = list_->actual_texts.at(static_cast<std::size_t>(actual_));
      actual_glyph_->c.text = expand_ligatures(text);
      // Surrounding spaces of the replacement text separate it from its neighbours.
      const bool leading = !text.empty() && is_space(text.substr(0, 1));
      const bool trailing = !text.empty() && is_space(text.substr(text.size() - 1));
      std::u32string& t = actual_glyph_->c.text;
      const auto begin = t.find_first_not_of(U" \t\r\n");
      t = begin == std::u32string::npos ? std::u32string() : t.substr(begin, t.find_last_not_of(U" \t\r\n") - begin + 1);
      pending_space_ = pending_space_ || leading;
      push(std::move(*actual_glyph_), true);
      pending_space_ = pending_space_ || trailing;
      actual_glyph_.reset();
    }
    actual_ = -1;
  }

  const graphics::DisplayList* list_;
  const TextLimits* limits_;
  Matrix view_;
  double width_ = 0;
  double height_ = 0;
  std::vector<Glyph> glyphs_;
  bool pending_space_ = false;
  std::int32_t actual_ = -1;
  std::optional<Glyph> actual_glyph_;
};

struct WordBox {
  std::vector<std::size_t> glyphs;  // into the page's glyphs, in content order
  std::int32_t direction = 0;
  double s0 = 0;
  double s1 = 0;
  double t = 0;
  double size = 0;
  std::size_t order = 0;  // of its first glyph in the content
  std::u32string text;
  Rect box;
};

// Glyphs in content order → words: a word ends at a space, a gap, a jump back or off the baseline.
std::vector<WordBox> make_words(const std::vector<Glyph>& glyphs) {
  std::vector<WordBox> words;
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    const Glyph& g = glyphs[i];
    if (!words.empty()) {
      WordBox& w = words.back();
      const Glyph& last = glyphs[w.glyphs.back()];
      const double size = std::max(w.size, g.c.size);
      const bool same_line = g.direction == w.direction && std::abs(g.t - last.t) <= kWordBaseline * size;
      // The same glyph drawn again on itself (fake bold by overprinting): dropped.
      if (same_line && g.c.text == last.c.text && std::abs(g.s0 - last.s0) < kDuplicateGlyph * (last.s1 - last.s0) &&
          std::abs(g.t - last.t) < kDuplicate * size) {
        continue;
      }
      const double gap = g.s0 - w.s1;
      if (!g.space_before && same_line && gap <= kWordGap * size && gap >= -kBackwards * size) {
        w.glyphs.push_back(i);
        w.s1 = std::max(w.s1, g.s1);
        w.s0 = std::min(w.s0, g.s0);
        w.size = size;
        w.text += g.c.text;
        w.box = unite(w.box, g.c.box);
        continue;
      }
    }
    words.push_back(
        WordBox{.glyphs = {i}, .direction = g.direction, .s0 = g.s0, .s1 = g.s1, .t = g.t, .size = g.c.size, .order = i, .text = g.c.text, .box = g.c.box});
  }
  // The same word drawn again at (almost) the same place — fake bold, drop shadows: dropped.
  std::map<std::pair<std::int32_t, std::u32string>, std::vector<std::size_t>> seen;
  std::vector<WordBox> kept;
  kept.reserve(words.size());
  for (WordBox& w : words) {
    auto& same = seen[{w.direction, w.text}];
    const bool duplicate = std::ranges::any_of(same, [&](std::size_t k) {
      const WordBox& o = kept[k];
      const double tolerance = kDuplicate * std::max(o.size, w.size);
      return std::abs(o.s0 - w.s0) < tolerance && std::abs(o.t - w.t) < tolerance;
    });
    constexpr std::size_t kMaxSame = 64;  // bounds the scan for pages repeating one word everywhere
    if (!duplicate) {
      if (same.size() < kMaxSame) {
        same.push_back(kept.size());
      }
      kept.push_back(std::move(w));
    }
  }
  return kept;
}

struct LineBox {
  std::vector<std::size_t> words;  // left to right along the baseline
  std::int32_t direction = 0;
  double s0 = 0;
  double s1 = 0;
  double t = 0;
  double size = 0;
  std::size_t order = 0;
};

// Words → lines: the words on one baseline, split where a gap is wide enough to be between columns.
std::vector<LineBox> make_lines(const std::vector<WordBox>& words) {
  std::vector<std::size_t> sorted(words.size());
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    sorted[i] = i;
  }
  std::ranges::stable_sort(sorted, [&](std::size_t a, std::size_t b) {
    return std::tie(words[a].direction, words[a].t, words[a].s0) < std::tie(words[b].direction, words[b].t, words[b].s0);
  });
  std::vector<LineBox> lines;
  std::size_t begin = 0;
  while (begin < sorted.size()) {
    const WordBox& anchor = words[sorted[begin]];
    std::size_t end = begin + 1;
    while (end < sorted.size() && words[sorted[end]].direction == anchor.direction &&
           words[sorted[end]].t - anchor.t <= kSameBaseline * std::max(anchor.size, words[sorted[end]].size)) {
      ++end;
    }
    std::vector<std::size_t> cluster(sorted.begin() + static_cast<std::ptrdiff_t>(begin), sorted.begin() + static_cast<std::ptrdiff_t>(end));
    std::ranges::stable_sort(cluster, [&](std::size_t a, std::size_t b) { return words[a].s0 < words[b].s0; });
    for (const std::size_t w : cluster) {
      const WordBox& word = words[w];
      if (!lines.empty() && lines.back().direction == word.direction && lines.back().t == anchor.t &&
          word.s0 - lines.back().s1 <= kLineGap * std::max(lines.back().size, word.size)) {
        LineBox& line = lines.back();
        line.words.push_back(w);
        line.s1 = std::max(line.s1, word.s1);
        line.size = std::max(line.size, word.size);
        line.order = std::min(line.order, word.order);
        continue;
      }
      lines.push_back(LineBox{.words = {w}, .direction = word.direction, .s0 = word.s0, .s1 = word.s1, .t = anchor.t, .size = word.size, .order = word.order});
    }
    begin = end;
  }
  return lines;
}

struct BlockBox {
  std::vector<std::size_t> lines;  // top to bottom
  std::int32_t direction = 0;
  double s0 = 0;
  double s1 = 0;
  double t0 = 0;  // the top of its first line
  double t1 = 0;  // the baseline of its last line
  std::size_t order = 0;
};

// Lines → blocks: a line continues the block whose last line is just above it, overlapping it along the
// baseline, in a similar size.
std::vector<BlockBox> make_blocks(const std::vector<LineBox>& lines) {
  std::vector<BlockBox> blocks;
  std::vector<std::size_t> last;  // per block, its last line
  // The blocks a later line may still continue. Lines come sorted by direction and baseline
  // (make_lines), so a block is closed for good once the current line is more than
  // kBlockPitch · kBlockSizeRatio of its last line's size below it (no size within the ratio reaches it).
  std::vector<std::size_t> open;
  for (std::size_t l = 0; l < lines.size(); ++l) {
    const LineBox& line = lines[l];
    std::erase_if(open, [&](std::size_t b) {
      const LineBox& above = lines[last[b]];
      return blocks[b].direction != line.direction || line.t - above.t > kBlockPitch * kBlockSizeRatio * above.size;
    });
    std::optional<std::size_t> best;
    double best_pitch = 0;
    for (const std::size_t b : open) {
      const LineBox& above = lines[last[b]];
      const double pitch = line.t - above.t;
      const double size = std::max(line.size, above.size);
      const double ratio = size / std::max(1e-6, std::min(line.size, above.size));
      const double overlap = std::min(line.s1, above.s1) - std::max(line.s0, above.s0);
      if (pitch <= 0 || pitch > kBlockPitch * size || ratio > kBlockSizeRatio || overlap <= 0) {
        continue;
      }
      if (!best || pitch < best_pitch) {
        best = b;
        best_pitch = pitch;
      }
    }
    if (best) {
      BlockBox& block = blocks[*best];
      block.lines.push_back(l);
      block.s0 = std::min(block.s0, line.s0);
      block.s1 = std::max(block.s1, line.s1);
      block.t1 = line.t;
      block.order = std::min(block.order, line.order);
      last[*best] = l;
    } else {
      open.push_back(blocks.size());
      blocks.push_back(
          BlockBox{.lines = {l}, .direction = line.direction, .s0 = line.s0, .s1 = line.s1, .t0 = line.t - line.size, .t1 = line.t, .order = line.order});
      last.push_back(l);
    }
  }
  return blocks;
}

// kContent: lines and blocks in content order. A word continues the line when it is on its baseline and
// not far back; a line continues the block when it is to the right on the same baseline (a table row)
// or just below, overlapping the block along the baseline, in a similar size.
std::pair<std::vector<LineBox>, std::vector<BlockBox>> content_layout(const std::vector<WordBox>& words) {
  std::vector<LineBox> lines;
  for (std::size_t w = 0; w < words.size(); ++w) {
    const WordBox& word = words[w];
    if (!lines.empty()) {
      LineBox& line = lines.back();
      const double size = std::max(line.size, word.size);
      const double gap = word.s0 - line.s1;
      if (line.direction == word.direction && std::abs(word.t - line.t) <= kSameBaseline * size && gap >= -kBackwards * size && gap <= kLineGap * size) {
        line.words.push_back(w);
        line.s0 = std::min(line.s0, word.s0);
        line.s1 = std::max(line.s1, word.s1);
        line.size = size;
        continue;
      }
    }
    lines.push_back(LineBox{.words = {w}, .direction = word.direction, .s0 = word.s0, .s1 = word.s1, .t = word.t, .size = word.size, .order = word.order});
  }
  std::vector<BlockBox> blocks;
  for (std::size_t l = 0; l < lines.size(); ++l) {
    const LineBox& line = lines[l];
    if (!blocks.empty() && blocks.back().direction == line.direction) {
      BlockBox& block = blocks.back();
      const LineBox& previous = lines[block.lines.back()];
      const double size = std::max(line.size, previous.size);
      const double ratio = size / std::max(1e-6, std::min(line.size, previous.size));
      const bool row = std::abs(line.t - previous.t) <= kSameBaseline * size && line.s0 >= previous.s1 - (kBackwards * size);
      const double pitch = line.t - previous.t;
      const double overlap = std::min(line.s1, block.s1) - std::max(line.s0, block.s0);
      const bool below = pitch > kSameBaseline * size && pitch <= kBlockPitch * size && overlap > 0 && ratio <= kBlockSizeRatio;
      if (row || below) {
        block.lines.push_back(l);
        block.s0 = std::min(block.s0, line.s0);
        block.s1 = std::max(block.s1, line.s1);
        block.t0 = std::min(block.t0, line.t - line.size);
        block.t1 = std::max(block.t1, line.t);
        continue;
      }
    }
    blocks.push_back(
        BlockBox{.lines = {l}, .direction = line.direction, .s0 = line.s0, .s1 = line.s1, .t0 = line.t - line.size, .t1 = line.t, .order = line.order});
  }
  return {std::move(lines), std::move(blocks)};
}

// Reading order of the blocks of one direction (T. Breuel, "High performance document layout analysis",
// 2003): a comes before b when their extents along the baseline overlap and a is above; or when a is
// entirely before b along the baseline and no third block spanning both lies between them vertically.
// A topological sort of that order, ties broken by the content order.
std::vector<std::size_t> reading_order(const std::vector<BlockBox>& blocks, const std::vector<std::size_t>& group, std::size_t max_ordered) {
  std::vector<std::size_t> ordered = group;
  const auto by_position = [&](std::size_t a, std::size_t b) {
    return std::tie(blocks[a].t0, blocks[a].s0, blocks[a].order) < std::tie(blocks[b].t0, blocks[b].s0, blocks[b].order);
  };
  if (group.size() > max_ordered) {
    std::ranges::stable_sort(ordered, by_position);
    return ordered;
  }
  const std::size_t n = group.size();
  const auto overlaps = [&](std::size_t a, std::size_t b) { return std::min(blocks[a].s1, blocks[b].s1) - std::max(blocks[a].s0, blocks[b].s0) > 0; };
  std::vector<std::vector<std::size_t>> after(n);
  std::vector<std::size_t> before_count(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      if (i == j) {
        continue;
      }
      const BlockBox& a = blocks[group[i]];
      const BlockBox& b = blocks[group[j]];
      bool precedes = false;
      if (overlaps(group[i], group[j])) {
        precedes = a.t0 < b.t0 || (a.t0 == b.t0 && a.order < b.order);
      } else if (a.s1 <= b.s0) {
        const double top = std::min(a.t0, b.t0);
        const double bottom = std::max(a.t0, b.t0);
        precedes = std::ranges::none_of(group, [&](std::size_t c) {
          return c != group[i] && c != group[j] && blocks[c].t0 > top && blocks[c].t0 < bottom && overlaps(c, group[i]) && overlaps(c, group[j]);
        });
      }
      if (precedes) {
        after[i].push_back(j);
        ++before_count[j];
      }
    }
  }
  // Kahn's algorithm, the earliest in the content first; a cycle (overlapping blocks) is broken the same way.
  using Entry = std::pair<std::size_t, std::size_t>;  // (content order, index in group)
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> ready;
  std::vector<bool> done(n, false);
  for (std::size_t i = 0; i < n; ++i) {
    if (before_count[i] == 0) {
      ready.emplace(blocks[group[i]].order, i);
    }
  }
  ordered.clear();
  while (ordered.size() < n) {
    if (ready.empty()) {
      std::size_t pick = n;
      for (std::size_t i = 0; i < n; ++i) {
        if (!done[i] && (pick == n || blocks[group[i]].order < blocks[group[pick]].order)) {
          pick = i;
        }
      }
      before_count[pick] = 0;
      ready.emplace(blocks[group[pick]].order, pick);
    }
    const std::size_t i = ready.top().second;
    ready.pop();
    if (done[i]) {
      continue;
    }
    done[i] = true;
    ordered.push_back(group[i]);
    for (const std::size_t j : after[i]) {
      if (done[j] || before_count[j] == 0) {
        continue;
      }
      --before_count[j];
      if (before_count[j] == 0) {
        ready.emplace(blocks[group[j]].order, j);
      }
    }
  }
  return ordered;
}

}  // namespace

PageText extract(const graphics::DisplayList& list, Order order, const TextLimits& limits) {
  Collector collector(list, limits);
  const std::vector<Glyph> glyphs = collector.collect();
  PageText page;
  page.width = collector.width();
  page.height = collector.height();

  const std::vector<WordBox> words = make_words(glyphs);
  std::vector<LineBox> lines;
  std::vector<BlockBox> blocks;
  if (order == Order::kContent) {
    std::tie(lines, blocks) = content_layout(words);
  } else {
    lines = make_lines(words);
    blocks = make_blocks(lines);
  }

  // kContent: the blocks as they come, whatever their direction (a margin note drawn first is read
  // first). kLayout: by direction — the one with the most text first, then the others as they first come
  // in the content — each ordered by columns.
  std::vector<std::size_t> ordered(blocks.size());
  std::ranges::iota(ordered, std::size_t{0});
  if (order == Order::kLayout) {
    std::map<std::int32_t, std::pair<std::size_t, std::size_t>> directions;  // direction → (chars, first order)
    for (const WordBox& w : words) {
      auto [it, inserted] = directions.try_emplace(w.direction, 0, w.order);
      it->second.first += w.glyphs.size();
      it->second.second = std::min(it->second.second, w.order);
    }
    std::vector<std::int32_t> direction_order;
    direction_order.reserve(directions.size());
    for (const auto& [direction, stats] : directions) {
      direction_order.push_back(direction);
    }
    std::ranges::stable_sort(direction_order, [&](std::int32_t a, std::int32_t b) {
      const auto& sa = directions.at(a);
      const auto& sb = directions.at(b);
      return std::make_pair(-static_cast<std::ptrdiff_t>(sa.first), sa.second) < std::make_pair(-static_cast<std::ptrdiff_t>(sb.first), sb.second);
    });
    ordered.clear();
    for (const std::int32_t direction : direction_order) {
      std::vector<std::size_t> group;
      for (std::size_t b = 0; b < blocks.size(); ++b) {
        if (blocks[b].direction == direction) {
          group.push_back(b);
        }
      }
      std::ranges::copy(reading_order(blocks, group, limits.max_ordered_blocks), std::back_inserter(ordered));
    }
  }
  for (const std::size_t b : ordered) {
    Block block{.box = {}, .first = static_cast<std::uint32_t>(page.lines.size()), .count = 0};
    bool first_line = true;
    for (const std::size_t l : blocks[b].lines) {
      Line line{.box = {}, .first = static_cast<std::uint32_t>(page.words.size()), .count = 0};
      for (const std::size_t w : lines[l].words) {
        const WordBox& source = words[w];
        Word word{.text = source.text, .box = source.box, .first = static_cast<std::uint32_t>(page.chars.size()), .count = 0};
        for (const std::size_t g : source.glyphs) {
          page.chars.push_back(glyphs[g].c);
          ++word.count;
        }
        line.box = line.count == 0 ? word.box : unite(line.box, word.box);
        page.words.push_back(std::move(word));
        ++line.count;
      }
      block.box = first_line ? line.box : unite(block.box, line.box);
      first_line = false;
      page.lines.push_back(line);
      ++block.count;
    }
    page.blocks.push_back(block);
  }
  return page;
}

std::string PageText::plain() const {
  std::u32string out;
  for (std::size_t b = 0; b < blocks.size(); ++b) {
    if (b > 0) {
      out += U"\n";
    }
    for (std::uint32_t l = blocks[b].first; l < blocks[b].first + blocks[b].count; ++l) {
      for (std::uint32_t w = lines[l].first; w < lines[l].first + lines[l].count; ++w) {
        if (w > lines[l].first) {
          out += U' ';
        }
        out += words[w].text;
      }
      out += U'\n';
    }
  }
  return model::to_utf8(out);
}

}  // namespace capriole::text
