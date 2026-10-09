#include "capriole/layout/page_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "drawn.hpp"
#include "edit/common.hpp"

namespace capriole::layout {

namespace {

using graphics::Matrix;
using graphics::Point;
using graphics::Rect;

// Within this, two things touch (points as shown).
constexpr double kSlack = 0.5;
// As large as the page (of each side): a background.
constexpr double kBackground = 0.9;
// At most this across: a line (a rule, a table's).
constexpr double kThin = 3;
// The header ends in the page's first eighth, the footer starts in its last, two lines from the rest.
constexpr double kZone = 0.125;
constexpr double kZoneGap = 2;
// How many pairs of paths are compared to group them (dense vector pages: the rest stay alone), of
// paragraphs to find columns, of lines to find a space's.
constexpr std::size_t kMaxPairs = 20'000'000;
// How many places a text operator drawn more than once (a form's) are looked at for a character.
constexpr std::size_t kMaxCandidates = 64;

Rect united(const Rect& a, const Rect& b) { return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)}; }

bool touching(const Rect& a, const Rect& b) {
  return std::min(a.x1, b.x1) - std::max(a.x0, b.x0) >= -kSlack && std::min(a.y1, b.y1) - std::max(a.y0, b.y0) >= -kSlack;
}

// inner lies inside outer, clear of its edges: a shape drawn in a box, not part of it.
bool clear_inside(const Rect& inner, const Rect& outer) {
  return inner.x0 > outer.x0 + kSlack && inner.y0 > outer.y0 + kSlack && inner.x1 < outer.x1 - kSlack && inner.y1 < outer.y1 - kSlack;
}

bool near(const Rect& a, const Rect& b, double by) {
  return std::abs(a.x0 - b.x0) <= by && std::abs(a.y0 - b.y0) <= by && std::abs(a.x1 - b.x1) <= by && std::abs(a.y1 - b.y1) <= by;
}

Rect grown(const Rect& r, double by) { return {r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by}; }

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0;
  }
  std::ranges::sort(values);
  return values[values.size() / 2];
}

struct Groups {
  std::vector<std::size_t> parent;
  explicit Groups(std::size_t n) : parent(n) { std::ranges::iota(parent, std::size_t{0}); }
  std::size_t find(std::size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }
  void join(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a != b) {
      parent[std::max(a, b)] = std::min(a, b);
    }
  }
};

using RunKey = std::pair<std::optional<std::uint32_t>, std::uint32_t>;  // stream number (none: the page's), operator

// What a stroke covers: its path's bounds and half its line's width around (at least half a point).
Rect stroke_ink(const graphics::DisplayList& list, const graphics::StrokePath& stroke, const Rect& box, const Matrix& to_view) {
  const double width = stroke.style < list.styles.size() ? list.styles[stroke.style].width : 1;
  const double scale = std::sqrt(std::abs(stroke.ctm.then(to_view).determinant()));
  const double half = std::isfinite(scale) ? width * scale / 2 : 0;
  return grown(box, std::max(half * 1.5, kSlack));  // a square cap or a miter goes a little further
}

// What a glyph's outline covers as shown; nullopt: it has none (a Type 3 glyph, a missing one).
std::optional<Rect> glyph_ink(const graphics::ShowText& run, const graphics::Glyph& glyph, const Matrix& to_view) {
  if (!glyph.outline) {
    return std::nullopt;
  }
  const auto bounds = edit::detail::path_bounds(*glyph.outline);
  if (!bounds) {
    return std::nullopt;
  }
  const double h = run.horizontal_scale != 0 ? run.horizontal_scale : 1;
  const Matrix m = Matrix{run.font_size * h, 0, 0, run.font_size, glyph.origin.x, glyph.origin.y}.then(run.text_matrix).then(to_view);
  const Rect r = edit::detail::transformed(*bounds, m).normalized();
  return detail::finite(r) ? std::optional(r) : std::nullopt;
}

Role drawing_role(const std::vector<const detail::Item*>& members, const graphics::DisplayList& list, const Rect& group) {
  std::size_t horizontals = 0;
  std::size_t verticals = 0;
  bool all_thin = true;
  for (const detail::Item* item : members) {
    const Rect& b = item->box;
    if (std::min(b.width(), b.height()) <= kThin) {
      (b.width() > b.height() ? horizontals : verticals) += 1;
      continue;
    }
    all_thin = false;
    // A rectangle's outline: two lines each way.
    const auto* stroke = std::get_if<graphics::StrokePath>(&list.commands[item->command]);
    if (stroke != nullptr && stroke->path.size() <= 6 && std::ranges::count(stroke->path.verbs(), graphics::SegmentKind::kMove) == 1) {
      horizontals += 2;
      verticals += 2;
    }
  }
  if (all_thin && std::min(group.width(), group.height()) <= kThin) {
    return Role::kRule;
  }
  if (horizontals >= 3 && verticals >= 3) {
    return Role::kTable;
  }
  // A rectangle as large as the group (filled or outlined; what is drawn in it touches it).
  const auto rectangle = [&](const detail::Item* item) {
    const graphics::Path* path = nullptr;
    if (const auto* fill = std::get_if<graphics::FillPath>(&list.commands[item->command])) {
      path = &fill->path;
    } else if (const auto* stroke = std::get_if<graphics::StrokePath>(&list.commands[item->command])) {
      path = &stroke->path;
    }
    if (path == nullptr || !near(item->box, group, 1)) {
      return false;
    }
    const auto& verbs = path->verbs();
    const auto lines = std::ranges::count(verbs, graphics::SegmentKind::kLine);
    return std::ranges::count(verbs, graphics::SegmentKind::kMove) == 1 && std::ranges::count(verbs, graphics::SegmentKind::kCubic) == 0 && lines >= 3 &&
           lines <= 4;
  };
  if (std::ranges::any_of(members, rectangle)) {
    return Role::kBox;
  }
  return Role::kFigure;
}

}  // namespace

Result<PageModel> page_model(Document& document, std::uint32_t page_index, const font::Resources* resources, const ModelLimits& limits) {
  auto page = edit::detail::page_at(document, page_index, std::nullopt);
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto list = content::interpret_page(document, page->ref, limits.interpreter, resources, content::Purpose::kText);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  PageModel model;
  model.list = std::move(*list);
  model.text = text::extract(model.list, text::Order::kLayout, limits.text);
  model.width = model.text.width;
  model.height = model.text.height;
  const graphics::DisplayList& dl = model.list;
  const Matrix& to_view = page->to_view;

  // The paragraphs: the blocks of the page's text, their characters the content's (an annotation's are its own).
  for (const text::Block& block : model.text.blocks) {
    std::vector<std::size_t> lines(block.count);
    std::ranges::iota(lines, std::size_t{block.first});
    Paragraph p = paragraph_of(model.text, dl, lines);
    if (!p.chars.empty()) {
      model.paragraphs.push_back(std::move(p));
    }
  }

  const detail::Drawn drawn = detail::drawn_items(document, *page, dl);
  std::map<RunKey, std::vector<std::size_t>> runs;  // the items of each text operator (a form drawn twice: two)
  for (std::size_t k = 0; k < drawn.operators; ++k) {
    const detail::Item& item = drawn.items[k];
    model.drawn.push_back(item.command);
    if (item.kind == detail::Item::Kind::kText) {
      if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&dl.commands[item.command])) {
        runs[{(*run)->stream ? std::optional((*run)->stream->number) : std::nullopt, (*run)->operation}].push_back(k);
      }
    }
  }
  std::ranges::sort(model.drawn);

  std::vector<Element> elements;
  const auto element_of = [&](Kind kind, Role role, const Rect& box, std::vector<std::size_t> commands) {
    Element e;
    e.kind = kind;
    e.role = role;
    e.box = box;
    std::ranges::sort(commands);
    const auto [first, last] = std::ranges::unique(commands);
    commands.erase(first, last);
    for (const std::size_t c : commands) {
      const Source s{.invocation = dl.origins[c].invocation, .operation = dl.origins[c].operation};
      if (std::ranges::find(e.sources, s) == e.sources.end()) {
        e.sources.push_back(s);
      }
    }
    e.commands = std::move(commands);
    return e;
  };

  // Each paragraph, with the runs that show its characters and what their glyphs cover.
  std::set<std::size_t> claimed;  // text items
  for (std::size_t pi = 0; pi < model.paragraphs.size(); ++pi) {
    const Paragraph& p = model.paragraphs[pi];
    std::vector<std::size_t> commands;
    std::optional<Rect> box;
    for (const std::size_t c : p.chars) {
      const text::Char& ch = model.text.chars[c];
      box = box ? united(*box, ch.box) : ch.box;
      const auto found = runs.find({ch.provenance.stream ? std::optional(ch.provenance.stream->number) : std::nullopt, ch.provenance.operation});
      if (found == runs.end()) {
        continue;
      }
      // The run that shows it here (a form drawn twice shows it twice).
      const Point centre{(ch.box.x0 + ch.box.x1) / 2, (ch.box.y0 + ch.box.y1) / 2};
      std::size_t k = found->second.front();
      for (const std::size_t candidate : found->second | std::views::take(kMaxCandidates)) {
        const Rect& b = drawn.items[candidate].box;
        if (centre.x >= b.x0 - kSlack && centre.x <= b.x1 + kSlack && centre.y >= b.y0 - kSlack && centre.y <= b.y1 + kSlack) {
          k = candidate;
          break;
        }
      }
      claimed.insert(k);
      const std::size_t command = drawn.items[k].command;
      commands.push_back(command);
      const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&dl.commands[command]);
      if (run == nullptr) {
        continue;
      }
      for (const graphics::Glyph& glyph : (*run)->glyphs) {
        if (glyph.byte == ch.provenance.byte) {
          if (const auto ink = glyph_ink(**run, glyph, to_view)) {
            box = united(*box, *ink);
          }
          break;
        }
      }
    }
    Element e = element_of(Kind::kParagraph, Role::kNone, *box, std::move(commands));
    e.paragraph = pi;
    elements.push_back(std::move(e));
  }

  // Images each on its own; paths and shadings grouped by what they touch (a table's lines, a figure's shapes
  // and gradients) — not what lies clear inside another (a rule in a box) —, a background on its own.
  const auto background = [&](const detail::Item& item) {
    return item.plain && item.box.width() >= kBackground * model.width && item.box.height() >= kBackground * model.height;
  };
  // The paragraph (its element: the first ones) one of whose lines a run is on: across half of the lower's
  // height, at most its height away.
  std::vector<std::pair<Rect, std::size_t>> lines_by_top;  // the paragraphs' lines, by their tops
  double tallest = 0;
  for (std::size_t pi = 0; pi < model.paragraphs.size(); ++pi) {
    for (const std::size_t l : model.paragraphs[pi].lines) {
      lines_by_top.emplace_back(model.text.lines[l].box, pi);
      tallest = std::max(tallest, model.text.lines[l].box.height());
    }
  }
  std::ranges::sort(lines_by_top, {}, [](const auto& entry) { return entry.first.y0; });
  std::size_t line_pairs = 0;
  const auto line_of = [&](const Rect& r) -> std::optional<std::size_t> {
    auto at = std::ranges::lower_bound(lines_by_top, r.y0 - tallest, {}, [](const auto& entry) { return entry.first.y0; });
    for (; at != lines_by_top.end() && at->first.y0 <= r.y1 && ++line_pairs <= kMaxPairs; ++at) {
      const Rect& line = at->first;
      const double shared = std::min(r.y1, line.y1) - std::max(r.y0, line.y0);
      const double lower = std::min(r.height(), line.height());
      const double gap = std::max(r.x0, line.x0) - std::min(r.x1, line.x1);
      if (lower > 0 && shared >= lower / 2 && gap <= std::max(r.height(), 2.0)) {
        return at->second;
      }
    }
    return std::nullopt;
  };
  std::vector<std::size_t> paths;
  std::vector<Rect> ink(drawn.operators);
  for (std::size_t k = 0; k < drawn.operators; ++k) {
    const detail::Item& item = drawn.items[k];
    ink[k] = item.box;
    switch (item.kind) {
      case detail::Item::Kind::kText:
        if (!claimed.contains(k)) {
          // Text with nothing to read in it (spaces, a font that does not say what its glyphs are): the
          // paragraph's whose line it is on, else on its own.
          Rect box = item.box;
          if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&dl.commands[item.command])) {
            for (const graphics::Glyph& glyph : (*run)->glyphs) {
              if (const auto g = glyph_ink(**run, glyph, to_view)) {
                box = united(box, *g);
              }
            }
          }
          if (const auto on = line_of(item.box)) {
            Element& e = elements[*on];
            e.box = united(e.box, box);
            e.commands.insert(std::ranges::upper_bound(e.commands, item.command), item.command);
            const Source source{.invocation = item.invocation, .operation = item.operation};
            if (std::ranges::find(e.sources, source) == e.sources.end()) {
              e.sources.push_back(source);
            }
          } else {
            elements.push_back(element_of(Kind::kGlyphs, Role::kNone, box, {item.command}));
          }
        }
        break;
      case detail::Item::Kind::kImage:
        elements.push_back(element_of(Kind::kImage, background(item) ? Role::kBackground : Role::kFigure, item.box, {item.command}));
        break;
      case detail::Item::Kind::kShading:
        // A shading is often a figure's colour (a logo's gradients, inside its paths): grouped with them.
        if (background(item)) {
          elements.push_back(element_of(Kind::kShading, Role::kBackground, item.box, {item.command}));
        } else {
          paths.push_back(k);
        }
        break;
      case detail::Item::Kind::kStroke:
        if (const auto* stroke = std::get_if<graphics::StrokePath>(&dl.commands[item.command])) {
          ink[k] = stroke_ink(dl, *stroke, item.box, to_view);
        }
        paths.push_back(k);
        break;
      case detail::Item::Kind::kFill:
        if (background(item)) {
          elements.push_back(element_of(Kind::kDrawing, Role::kBackground, item.box, {item.command}));
        } else {
          paths.push_back(k);
        }
        break;
      case detail::Item::Kind::kAnnotation:
        break;
    }
  }
  {
    Groups groups(paths.size());
    std::vector<std::size_t> order(paths.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return drawn.items[paths[a]].box.y0 < drawn.items[paths[b]].box.y0; });
    std::vector<std::size_t> active;
    std::size_t pairs = 0;
    for (const std::size_t i : order) {
      const Rect& a = drawn.items[paths[i]].box;
      std::erase_if(active, [&](std::size_t j) { return drawn.items[paths[j]].box.y1 < a.y0 - kSlack; });
      for (const std::size_t j : active) {
        if (++pairs > kMaxPairs) {
          break;
        }
        const Rect& b = drawn.items[paths[j]].box;
        if (touching(a, b) && !clear_inside(a, b) && !clear_inside(b, a)) {
          groups.join(i, j);
        }
      }
      active.push_back(i);
    }
    std::map<std::size_t, std::vector<std::size_t>> members;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      members[groups.find(i)].push_back(paths[i]);
    }
    for (const auto& [root, ks] : members) {
      std::vector<const detail::Item*> items;
      std::vector<std::size_t> commands;
      Rect box = drawn.items[ks.front()].box;
      Rect covered = ink[ks.front()];
      for (const std::size_t k : ks) {
        items.push_back(&drawn.items[k]);
        commands.push_back(drawn.items[k].command);
        box = united(box, drawn.items[k].box);
        covered = united(covered, ink[k]);
      }
      // Shadings alone are one; with paths, a drawing.
      const bool shadings = std::ranges::all_of(items, [](const detail::Item* item) { return item->kind == detail::Item::Kind::kShading; });
      elements.push_back(
          element_of(shadings ? Kind::kShading : Kind::kDrawing, shadings ? Role::kFigure : drawing_role(items, dl, box), covered, std::move(commands)));
    }
  }
  for (std::size_t k = drawn.operators; k < drawn.items.size(); ++k) {
    const detail::Item& item = drawn.items[k];
    Element e = element_of(Kind::kAnnotation, Role::kNone, item.box, {});
    e.annotation = item.annotation;
    e.subtype = item.subtype;
    elements.push_back(std::move(e));
  }

  // Header and footer: what ends in the page's first eighth, or starts in its last, two lines (of the
  // page's text) apart from the rest.
  std::vector<double> pitches;
  for (const Paragraph& p : model.paragraphs) {
    if (p.lines.size() > 1 && p.refusal.empty()) {
      pitches.push_back(p.pitch);
    } else if (p.size > 0) {
      pitches.push_back(1.2 * p.size);
    }
  }
  const double pitch = pitches.empty() ? 12 : median(pitches);
  {
    std::vector<std::size_t> placed;
    for (std::size_t e = 0; e < elements.size(); ++e) {
      if (elements[e].role != Role::kBackground) {
        placed.push_back(e);
      }
    }
    std::ranges::sort(placed, [&](std::size_t a, std::size_t b) { return elements[a].box.y0 < elements[b].box.y0; });
    double footer = std::numeric_limits<double>::infinity();
    double above = -std::numeric_limits<double>::infinity();
    for (const std::size_t e : placed) {
      const Rect& b = elements[e].box;
      if (b.y0 >= (1 - kZone) * model.height && std::isfinite(above) && b.y0 - above >= kZoneGap * pitch) {
        footer = b.y0;
        break;
      }
      above = std::max(above, b.y1);
    }
    std::ranges::sort(placed, [&](std::size_t a, std::size_t b) { return elements[a].box.y1 > elements[b].box.y1; });
    double header = -std::numeric_limits<double>::infinity();
    double below = std::numeric_limits<double>::infinity();
    for (const std::size_t e : placed) {
      const Rect& b = elements[e].box;
      if (b.y1 <= kZone * model.height && std::isfinite(below) && below - b.y1 >= kZoneGap * pitch) {
        header = b.y1;
        break;
      }
      below = std::min(below, b.y0);
    }
    for (const std::size_t e : placed) {
      Element& element = elements[e];
      if (element.box.y0 >= footer) {
        element.zone = Zone::kFooter;
      } else if (element.box.y1 <= header) {
        element.zone = Zone::kHeader;
      }
    }
  }

  // Ordered by their boxes, which must be numbers (a hostile matrix can make them infinite).
  for (Element& e : elements) {
    if (!detail::finite(e.box)) {
      e.box = Rect{};
    }
  }
  std::ranges::stable_sort(elements, [](const Element& a, const Element& b) {
    if (a.box.y0 != b.box.y0) {
      return a.box.y0 < b.box.y0;
    }
    return a.box.x0 < b.box.x0;
  });

  // Columns: each paragraph of the body joined to the nearest below it across half the narrower's width,
  // when no more than four of its lines apart.
  {
    std::vector<std::size_t> texts;
    for (std::size_t e = 0; e < elements.size(); ++e) {
      if (elements[e].kind == Kind::kParagraph && elements[e].zone == Zone::kBody) {
        texts.push_back(e);
      }
    }
    Groups groups(texts.size());
    std::size_t column_pairs = 0;
    for (std::size_t i = 0; i < texts.size(); ++i) {
      const Element& a = elements[texts[i]];
      const Paragraph& pa = model.paragraphs[a.paragraph];
      const double reach = 4 * std::max(pa.pitch, 1.2 * pa.size);
      std::optional<std::size_t> nearest;
      double nearest_gap = std::numeric_limits<double>::infinity();
      // The elements are by their tops: those below start from the first whose top is at a's bottom.
      const auto from = std::ranges::lower_bound(texts, a.box.y1 - kSlack, {}, [&](std::size_t e) { return elements[e].box.y0; });
      for (auto j_at = from; j_at != texts.end() && elements[*j_at].box.y0 <= a.box.y1 + reach && ++column_pairs <= kMaxPairs; ++j_at) {
        const std::size_t j = static_cast<std::size_t>(j_at - texts.begin());
        const Element& b = elements[texts[j]];
        const double across = std::min(a.box.x1, b.box.x1) - std::max(a.box.x0, b.box.x0);
        const double gap = b.box.y0 - a.box.y1;
        if (j != i && b.box.y0 >= a.box.y1 - kSlack && across >= 0.5 * std::min(a.box.width(), b.box.width()) && gap <= reach && gap < nearest_gap) {
          nearest = j;
          nearest_gap = gap;
        }
      }
      if (nearest) {
        groups.join(i, *nearest);
      }
    }
    std::map<std::size_t, std::size_t> column_of;
    for (std::size_t i = 0; i < texts.size(); ++i) {
      const std::size_t root = groups.find(i);
      auto found = column_of.find(root);
      if (found == column_of.end()) {
        found = column_of.emplace(root, model.columns.size()).first;
        model.columns.push_back({.box = elements[texts[i]].box, .elements = {}});
      }
      Column& column = model.columns[found->second];
      column.box = united(column.box, elements[texts[i]].box);
      column.elements.push_back(texts[i]);
      elements[texts[i]].column = found->second;
    }
  }
  model.elements = std::move(elements);
  return model;
}

}  // namespace capriole::layout
