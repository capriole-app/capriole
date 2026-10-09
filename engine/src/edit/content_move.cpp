#include "content_move.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "capriole/content/appearance.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "common.hpp"
#include "content_rewrite.hpp"
#include "layout/drawn.hpp"

namespace capriole::edit::detail {

namespace {

using graphics::Matrix;
using graphics::Point;
using graphics::Rect;
using model::Dict;
using model::Object;
using model::ObjectRef;

// Within this of the paragraph's bottom or sides, a thing is on that side of it.
constexpr double kSlack = 0.5;
// A footer starts in the page's last eighth, at least two lines below the rest.
constexpr double kFooterZone = 0.875;
constexpr double kFooterGap = 2;
// A thing this large (of the page's width and height) is a background: neither moved nor in the way.
constexpr double kBackground = 0.9;

bool path_painting(std::string_view n) {
  return n == "f" || n == "F" || n == "f*" || n == "S" || n == "s" || n == "B" || n == "B*" || n == "b" || n == "b*" || n == "n";
}
bool path_construction(std::string_view n) { return n == "m" || n == "l" || n == "c" || n == "v" || n == "y" || n == "h" || n == "re"; }
bool text_showing(std::string_view n) { return n == "Tj" || n == "TJ" || n == "'" || n == "\""; }

Result<MovePlan> refused(std::string_view why) { return make_error(ErrorCode::kUnsupported, std::format("cannot move what follows: {}", why)); }

Rect shifted(const Rect& r, double by) { return {r.x0, r.y0 + by, r.x1, r.y1 + by}; }

// The two overlap by more than the slack each way.
bool overlap(const Rect& a, const Rect& b) {
  return std::min(a.x1, b.x1) - std::max(a.x0, b.x0) > kSlack && std::min(a.y1, b.y1) - std::max(a.y0, b.y0) > kSlack;
}

bool within(const Rect& inner, const Rect& outer) {
  return inner.x0 >= outer.x0 - kSlack && inner.y0 >= outer.y0 - kSlack && inner.x1 <= outer.x1 + kSlack && inner.y1 <= outer.y1 + kSlack;
}

Matrix translation(Point by) { return Matrix{1, 0, 0, 1, by.x, by.y}; }

// Down by `by` as shown, in the user space that ctm maps onto the page (nullopt: a singular matrix).
std::optional<Point> shift_in(const Matrix& ctm, const Matrix& to_view, double by) {
  const auto inverse = ctm.then(to_view).inverse();
  if (!inverse) {
    return std::nullopt;
  }
  const Point a = inverse->apply(Point{0, 0});
  const Point b = inverse->apply(Point{0, by});
  return Point{b.x - a.x, b.y - a.y};
}

// Whether an image is of one colour where it lies under an area (the page as shown): a background, not a
// form's or a page's scan that what moves would leave behind. Sampled on a grid of at most 64 × 64 points.
bool uniform_under(const graphics::DrawImage& image, const Matrix& to_view, const Rect& area) {
  const auto& pixels = image.pixels;
  if (!pixels || pixels->width <= 0 || pixels->height <= 0) {
    return false;
  }
  const auto inverse = image.ctm.then(to_view).inverse();
  if (!inverse || area.width() <= 0 || area.height() <= 0) {
    return true;
  }
  const auto w = static_cast<std::size_t>(pixels->width);
  const auto h = static_cast<std::size_t>(pixels->height);
  std::optional<std::array<int, 4>> first;
  std::size_t samples = 0;
  std::size_t different = 0;
  constexpr int kGrid = 64;
  for (int i = 0; i < kGrid; ++i) {
    for (int j = 0; j < kGrid; ++j) {
      const Point p{area.x0 + (area.width() * (i + 0.5) / kGrid), area.y0 + (area.height() * (j + 0.5) / kGrid)};
      const Point u = inverse->apply(p);
      if (u.x < 0 || u.x >= 1 || u.y <= 0 || u.y > 1) {
        continue;
      }
      const auto col = std::min(w - 1, static_cast<std::size_t>(u.x * static_cast<double>(w)));
      const auto row = std::min(h - 1, static_cast<std::size_t>((1 - u.y) * static_cast<double>(h)));
      const std::size_t at = ((row * w) + col) * 4;
      if (at + 3 >= pixels->rgba.size()) {
        continue;
      }
      const std::array<int, 4> c{pixels->rgba[at], pixels->rgba[at + 1], pixels->rgba[at + 2], pixels->rgba[at + 3]};
      first = first.value_or(c);
      ++samples;
      if (std::abs(c[0] - (*first)[0]) > 24 || std::abs(c[1] - (*first)[1]) > 24 || std::abs(c[2] - (*first)[2]) > 24 || std::abs(c[3] - (*first)[3]) > 24) {
        ++different;
      }
    }
  }
  return different * 200 <= samples;  // all but half a percent
}

// What is drawn (layout/drawn), and what moving makes of it.
struct Item : layout::detail::Item {
  bool moved = false;
  bool background = false;
  bool frame = false;  // it holds the paragraph whole (a box around it, a page's border)
};

struct Clip : layout::detail::Clip {
  bool moved = false;
};

using Key = std::pair<std::uint32_t, std::uint32_t>;  // invocation, operator

}  // namespace

Result<MovePlan> plan_move(Document& document, std::uint32_t page_index, const MoveRequest& request) {
  if (!(request.by > 0) || !std::isfinite(request.by)) {
    return refused("nothing to move");
  }
  auto page = page_at(document, page_index, "the page's content");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto list = content::interpret_page(document, page->ref);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  const Matrix& to_view = page->to_view;
  const Rect shown = transformed(list->crop_box, to_view).normalized();
  const auto& invocations = list->invocations;

  // 1. What each operator draws, and the clips and soft masks it is drawn in; the annotations.
  layout::detail::Drawn inventory = layout::detail::drawn_items(document, *page, *list);
  std::vector<Item> items;
  items.reserve(inventory.items.size());
  for (layout::detail::Item& item : inventory.items) {
    items.push_back({std::move(item)});
  }
  std::vector<Clip> clips;
  clips.reserve(inventory.clips.size());
  for (layout::detail::Clip& clip : inventory.clips) {
    clips.push_back({std::move(clip)});
  }
  const std::size_t drawn = inventory.operators;

  // 2. A footer, a background; what is below the paragraph, across its width.
  const double width = shown.width();
  const double height = shown.height();
  for (std::size_t k = 0; k < items.size(); ++k) {
    Item& item = items[k];
    item.background = item.plain && item.box.width() >= kBackground * width && item.box.height() >= kBackground * height;
    // A page-sized image is a background where it is of one colour below the paragraph (else it is what
    // the page shows — a scanned form — and what moves would leave it).
    if (const auto* image = item.background && k < drawn ? std::get_if<graphics::Boxed<graphics::DrawImage>>(&list->commands[item.command]) : nullptr) {
      item.background = uniform_under(**image, to_view, Rect{request.x0, request.cut, request.x1, shown.y1});
    }
    const Rect& b = item.box;
    const bool path = k < drawn && (std::holds_alternative<graphics::FillPath>(list->commands[item.command]) ||
                                    std::holds_alternative<graphics::StrokePath>(list->commands[item.command]));
    item.frame =
        !item.background && path && b.x0 <= request.x0 + kSlack && b.x1 >= request.x1 - kSlack && b.y0 <= request.top + kSlack && b.y1 > request.cut + kSlack;
  }
  // What starts this little above the paragraph's bottom, and goes on below it, follows it.
  const double edge = request.cut - std::max(kSlack, std::min(0.2 * request.pitch, 3.0));
  // The top of its last line.
  double last_top = request.cut;
  for (const Rect& g : request.glyphs) {
    if (g.y1 >= request.cut - (request.pitch / 2)) {
      last_top = std::min(last_top, g.y0);
    }
  }
  const auto is_text = [&](std::size_t k) {
    return k < drawn && std::holds_alternative<graphics::Boxed<graphics::ShowText>>(list->commands[items[k].command]);
  };
  // What starts higher still follows it when it is text whose middle is below its bottom (the next line,
  // set tight), or when it starts beside its last line and touches none of its glyphs (a drawing next to it).
  const auto follows = [&](std::size_t k) {
    const Rect& b = items[k].box;
    if (b.y0 >= edge) {
      return true;
    }
    if (is_text(k) && (b.y0 + b.y1) / 2 >= request.cut) {
      return true;
    }
    return b.y0 >= last_top - kSlack && std::ranges::none_of(request.glyphs, [&](const Rect& g) { return overlap(b, g); });
  };
  double footer = std::numeric_limits<double>::infinity();
  {
    std::vector<Rect> boxes;
    for (const Item& item : items) {
      if (!item.background && !item.frame) {
        boxes.push_back(item.box);
      }
    }
    std::ranges::sort(boxes, [](const Rect& a, const Rect& b) { return a.y0 < b.y0; });
    double above = -std::numeric_limits<double>::infinity();
    for (const Rect& b : boxes) {
      if (b.y0 > request.cut + kSlack && b.y0 >= shown.y0 + (kFooterZone * height) && b.y0 - above >= kFooterGap * request.pitch) {
        footer = b.y0;
        break;
      }
      above = std::max(above, b.y1);
    }
  }
  std::map<Key, std::vector<std::size_t>> by_operator;
  for (std::size_t k = 0; k < items.size(); ++k) {
    Item& item = items[k];
    const Rect& b = item.box;
    if (item.background || item.frame || b.x1 < request.x0 + kSlack || b.x0 > request.x1 - kSlack || b.y1 <= request.cut + kSlack || b.y0 >= footer - kSlack ||
        (k >= drawn && (b.y0 + b.y1) / 2 <= request.cut)) {
      continue;  // a background, a frame, beside it, above it, the footer, the paragraph's own link
    }
    if (!follows(k)) {
      if (k >= drawn) {
        return refused("an annotation lies across the paragraph's bottom");
      }
      return refused(std::holds_alternative<graphics::Boxed<graphics::DrawImage>>(list->commands[item.command])
                         ? "an image lies across the paragraph's bottom (a scanned page, a form's background)"
                         : "something is drawn across the paragraph's bottom (a box, a table, a rule)");
    }
    if (k >= drawn && items[k].annotation == std::nullopt) {
      return refused("an annotation below it cannot be moved");
    }
    item.moved = true;
  }
  // What moves takes along what belongs with it (below the paragraph, out of the footer). A line of text
  // goes whole: what is on a line with text that moves — more of its text (drawn by other operators, beyond
  // the paragraph's width, a word's space away), a list's bullet or number, a check box (small, near it) —
  // moves too; the line of another column beside it does not.
  for (bool more = true; more;) {
    more = false;
    for (std::size_t k = 0; k < items.size(); ++k) {
      Item& item = items[k];
      if (item.moved || item.background || item.frame || !follows(k) || item.box.y0 >= footer - kSlack || (k >= drawn && items[k].annotation == std::nullopt)) {
        continue;
      }
      // Between things that move, above and below it across its width: it goes with them (a line's marker
      // beside the paragraph's width, an empty line's space).
      const auto across = [&](const Rect& a, const Rect& b) { return std::min(a.x1, b.x1) - std::max(a.x0, b.x0) > 0; };
      bool over = false;
      bool under = false;
      for (std::size_t m = 0; m < drawn; ++m) {
        if (items[m].moved && across(items[m].box, item.box)) {
          over = over || items[m].box.y1 <= item.box.y0 + kSlack;
          under = under || items[m].box.y0 >= item.box.y1 - kSlack;
        }
      }
      if (over && under) {
        item.moved = true;
        more = true;
        continue;
      }
      for (std::size_t m = 0; m < drawn && !item.moved; ++m) {
        if (!items[m].moved) {
          continue;
        }
        if (!is_text(m)) {
          // A drawing goes whole: a path or an image touching one that moves (a table's border beyond the
          // paragraph's width) moves too.
          const Rect& a = item.box;
          const Rect& b = items[m].box;
          if (k < drawn && !is_text(k) && std::min(a.x1, b.x1) - std::max(a.x0, b.x0) >= -kSlack && std::min(a.y1, b.y1) - std::max(a.y0, b.y0) >= -kSlack) {
            item.moved = true;
            more = true;
          }
          continue;
        }
        const Rect& a = item.box;
        const Rect& line = items[m].box;
        const double shared = std::min(a.y1, line.y1) - std::max(a.y0, line.y0);
        const double lower = std::min(a.height(), line.height());
        const double gap = std::max(a.x0, line.x0) - std::min(a.x1, line.x1);
        // More of the line (a word's space away), or a mark beside it (a bullet, a number, a check box: not
        // wider than three of its heights, two of them away) — not the line of another column beside it.
        const bool continued = gap <= line.height() / 2;
        const bool mark = a.width() <= 3 * line.height() && gap <= 2 * line.height();
        if (lower > 0 && shared >= lower / 2 && a.height() <= 2 * line.height() && (continued || mark)) {
          item.moved = true;
          more = more || is_text(k);
        }
      }
    }
  }
  for (const Item& item : items) {
    if (item.moved && item.text_clip) {
      return refused("text below it clips what follows it");
    }
    if (item.moved && item.masked) {
      return refused("what is below it is drawn through a soft mask that does not move with it");
    }
  }
  for (std::size_t k = 0; k < drawn; ++k) {
    by_operator[{items[k].invocation, items[k].operation}].push_back(k);
  }
  for (const auto& [key, members] : by_operator) {
    if (!std::ranges::all_of(members, [&](std::size_t k) { return items[k].moved == items[members.front()].moved; })) {
      return refused("something is drawn across the paragraph's bottom (a box, a table, a rule)");
    }
  }

  // 3. Forms all of whose drawing moves are moved whole (their bounding box with them).
  std::vector<int> form_state(invocations.size(), -1);  // -1: nothing drawn, 0: not all moved, 1: all moved
  for (std::size_t k = 0; k < drawn; ++k) {
    for (std::uint32_t inv = items[k].invocation; inv != 0 && inv < invocations.size();) {
      int& state = form_state[inv];
      const int moved = items[k].moved ? 1 : 0;
      state = state == -1 ? moved : std::min(state, moved);
      inv = invocations[inv].parent;
    }
  }
  const auto whole = [&](std::uint32_t inv) {
    // The outermost form drawn all moved that holds inv (0: none).
    std::uint32_t found = 0;
    for (std::size_t depth = 0; inv != 0 && inv < invocations.size() && depth < invocations.size(); ++depth) {
      if (form_state[inv] == 1) {
        found = inv;
      }
      inv = invocations[inv].parent;
    }
    return found;
  };

  // 4. Clips: one set by a path below that clips only what moves moves with it; the bounding box of a form
  // moved whole too. What moves must stay inside the others.
  std::map<std::uint32_t, std::vector<content::Operation>> operations;
  const auto operations_in = [&](std::uint32_t inv) -> const std::vector<content::Operation>& {
    auto found = operations.find(inv);
    if (found == operations.end()) {
      std::vector<std::byte> source;
      if (inv == 0) {
        auto content = content::page_content(document, page->ref);
        source = content ? std::move(*content) : std::vector<std::byte>{};
      } else if (inv < invocations.size() && invocations[inv].form) {
        auto form = document.get(*invocations[inv].form);
        const model::Stream* stream = form && *form != nullptr ? (*form)->as_stream() : nullptr;
        auto decoded = stream != nullptr ? document.decode_stream(*stream) : Result<std::vector<std::byte>>(std::vector<std::byte>{});
        source = decoded ? std::move(*decoded) : std::vector<std::byte>{};
      }
      found = operations.emplace(inv, operations_of(source)).first;
    }
    return found->second;
  };
  const auto name_of = [&](std::uint32_t inv, std::uint32_t op) -> std::string {
    const auto& ops = operations_in(inv);
    return op < ops.size() ? ops[op].name : std::string();
  };
  for (Clip& clip : clips) {
    if (!clip.rewritable) {
      continue;
    }
    // A form's bounding box (the Do of a form moved whole).
    const bool do_op = name_of(clip.invocation, clip.operation) == "Do";
    if (do_op) {
      for (std::uint32_t inv = 1; inv < invocations.size(); ++inv) {
        if (invocations[inv].kind == graphics::Invocation::Kind::kForm && invocations[inv].parent == clip.invocation &&
            invocations[inv].operation == clip.operation && form_state[inv] == 1 && whole(inv) == inv) {
          clip.moved = true;
        }
      }
      continue;
    }
    if (!path_painting(name_of(clip.invocation, clip.operation))) {
      continue;
    }
    const Rect& b = clip.box;
    const bool below = b.y0 >= edge && b.y0 < footer - kSlack && b.x1 >= request.x0 + kSlack && b.x0 <= request.x1 - kSlack;
    clip.moved = below && !clip.under.empty() && std::ranges::all_of(clip.under, [&](std::size_t k) { return items[k].moved; });
  }
  // A path that both clips and paints moves as one.
  for (const Clip& clip : clips) {
    if (!clip.rewritable) {
      continue;
    }
    const auto members = by_operator.find({clip.invocation, clip.operation});
    if (members != by_operator.end() && name_of(clip.invocation, clip.operation) != "Do" &&
        std::ranges::any_of(members->second, [&](std::size_t k) { return items[k].moved != clip.moved; })) {
      return refused("what is below it is clipped by a path that does not move with it");
    }
  }

  // 5. Room: on the page (half its bottom margin may be taken), above the footer, inside what contains the
  // paragraph (a clip around it and what moves), clear of what is beside.
  double lowest = -std::numeric_limits<double>::infinity();       // of all that is drawn, but a footer
  double frame_bottom = std::numeric_limits<double>::infinity();  // the lowest frame's bottom that holds it
  for (const Item& item : items) {
    if (item.frame) {
      frame_bottom = std::min(frame_bottom, item.box.y1);
    } else if (!item.background && item.box.y0 < footer - kSlack) {
      lowest = std::max(lowest, item.box.y1);
    }
  }
  const double margin = std::isfinite(lowest) ? std::max(0.0, shown.y1 - lowest) : 0.0;
  const bool has_footer = std::isfinite(footer);
  const double bound = has_footer ? footer - request.pitch : shown.y1 - (margin / 2);
  double moved_bottom = request.cut;
  for (const Item& item : items) {
    if (item.moved) {
      moved_bottom = std::max(moved_bottom, item.box.y1);
    }
  }
  const double needed = std::max(moved_bottom + request.by, request.bottom);
  if (needed > bound + 0.01) {
    const double free = std::max(0.0, bound - moved_bottom);
    return refused(std::format("{} (it needs {} pt, {} are free)", has_footer ? "it would reach the footer" : "it would go past the bottom of the page",
                               num(std::ceil(request.by)), num(std::floor(free))));
  }
  if (needed > frame_bottom - kSlack) {
    return refused("the paragraph is inside a box (a frame, a cell) that does not grow with it");
  }
  std::uint32_t moved_items = 0;
  for (const Item& item : items) {
    if (!item.moved) {
      continue;
    }
    const Rect to = shifted(item.box, request.by);
    for (const std::size_t c : item.clips) {
      const Rect& clip = clips[c].box;
      if (clips[c].moved || within(to, clip) || !within(item.box, clip)) {
        continue;
      }
      // A clip that holds the paragraph too is what contains it (a frame, a cell): what moves leaves it.
      return refused(clip.y0 <= request.cut && clip.x0 <= request.x0 + kSlack && clip.x1 >= request.x1 - kSlack
                         ? "it would go past the bottom of what contains the paragraph (a frame, a cell)"
                         : "what is below it is clipped by a path that does not move with it");
    }
    for (const Item& other : items) {
      if (!other.moved && !other.background && !other.frame && other.box.y1 > request.cut + kSlack && other.box.y0 < footer - kSlack &&
          overlap(to, other.box)) {
        return refused("it would run into what is beside it");
      }
    }
  }

  // 6. How each operator moves.
  MovePlan plan;
  plan.by = request.by;
  plan.invocations = invocations;
  std::set<std::uint32_t> texts;  // invocations whose text moves
  for (const auto& [key, members] : by_operator) {
    const Item& first = items[members.front()];
    if (!first.moved) {
      continue;
    }
    const auto [inv, op] = key;
    const std::uint32_t form = whole(inv);
    if (form != 0) {
      // Drawn by a form moved whole: its Do moves (once).
      const graphics::Invocation& outer = invocations[form];
      const auto shift = shift_in(outer.ctm, to_view, request.by);
      if (!shift) {
        return refused("a form below it has a singular matrix");
      }
      auto& edit = plan.edits[outer.parent][outer.operation];
      if (edit.kind != OpMove::Kind::kWrap || !edit.moved) {
        edit = OpMove{.kind = OpMove::Kind::kWrap, .matrix = translation(*shift), .tm = {}, .moved = true};
        ++moved_items;
      }
      continue;
    }
    const std::string n = name_of(inv, op);
    const auto& command = list->commands[first.command];
    Matrix ctm;
    if (const auto* fill = std::get_if<graphics::FillPath>(&command)) {
      ctm = fill->ctm;
    } else if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
      ctm = stroke->ctm;
    } else if (const auto* image = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      ctm = (*image)->ctm;
    } else if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&command)) {
      ctm = (*run)->ctm;
    } else if (const auto* shading = std::get_if<graphics::Boxed<graphics::PaintShading>>(&command)) {
      ctm = (*shading)->ctm;
    } else {
      return refused("a pattern below it cannot be moved");
    }
    const auto shift = shift_in(ctm, to_view, request.by);
    if (!shift) {
      return refused("something below it has a singular matrix");
    }
    if (text_showing(n)) {
      const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
      if (run == nullptr) {
        return refused("text below it cannot be moved");
      }
      Matrix tm = (*run)->tm;
      tm.e += shift->x;
      tm.f += shift->y;
      plan.edits[inv][op] = OpMove{.kind = OpMove::Kind::kText, .matrix = translation(*shift), .tm = tm, .moved = true};
      texts.insert(inv);
    } else if (path_painting(n)) {
      plan.edits[inv][op] = OpMove{.kind = OpMove::Kind::kPath, .matrix = translation(*shift), .tm = {}, .moved = true};
    } else if (n == "Do" || n == "sh" || n == "BI") {
      plan.edits[inv][op] = OpMove{.kind = OpMove::Kind::kWrap, .matrix = translation(*shift), .tm = {}, .moved = true};
    } else {
      return refused(std::format("an operator below it ({}) cannot be moved", n));
    }
    ++moved_items;
  }
  // Clips that move alone (n: nothing painted).
  for (const Clip& clip : clips) {
    if (!clip.moved || !clip.rewritable || whole(clip.invocation) != 0 || plan.edits[clip.invocation].contains(clip.operation)) {
      continue;
    }
    const std::string n = name_of(clip.invocation, clip.operation);
    if (!path_painting(n)) {
      continue;  // a form's bounding box: moved with its Do
    }
    const auto* path = std::get_if<graphics::ClipPath>(&list->commands[clip.command]);
    const auto shift = path != nullptr ? shift_in(path->ctm, to_view, request.by) : std::nullopt;
    if (!shift) {
      return refused("a clip below it has a singular matrix");
    }
    plan.edits[clip.invocation][clip.operation] = OpMove{.kind = OpMove::Kind::kPath, .matrix = translation(*shift), .tm = {}, .moved = true};
  }
  // The other text of a content stream whose text moves keeps its place, set where it was.
  for (std::size_t k = 0; k < drawn; ++k) {
    const Item& item = items[k];
    if (item.moved || !texts.contains(item.invocation)) {
      continue;
    }
    if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&list->commands[item.command])) {
      plan.edits[item.invocation].try_emplace(item.operation, OpMove{.kind = OpMove::Kind::kText, .matrix = {}, .tm = (*run)->tm, .moved = false});
    }
  }
  for (const auto& [inv, edits] : plan.edits) {
    for (const auto& [op, edit] : edits) {
      if (edit.kind == OpMove::Kind::kText && !text_showing(name_of(inv, op))) {
        return refused("text below it cannot be moved");
      }
    }
  }
  for (std::size_t k = drawn; k < items.size(); ++k) {
    if (items[k].moved) {
      plan.annotations.push_back(*items[k].annotation);
    }
  }
  const Matrix& m = page->to_user;
  plan.dx = m.c * request.by;
  plan.dy = m.d * request.by;
  plan.items = moved_items;
  return plan;
}

namespace {

// A content stream with the operators moved as the edits say (and Do renamed: forms copied).
std::string moved_content(const std::vector<content::Operation>& ops, const std::map<std::uint32_t, OpMove>& edits,
                          const std::map<std::uint32_t, std::string>& renamed) {
  // The painting operator each path construction operator (and W, W*) belongs to, and those that clip.
  std::map<std::size_t, std::size_t> owner;
  std::set<std::size_t> clipping;
  {
    std::vector<std::size_t> pending;
    bool clips = false;
    for (std::size_t i = 0; i < ops.size(); ++i) {
      if (path_construction(ops[i].name) || ops[i].name == "W" || ops[i].name == "W*") {
        pending.push_back(i);
        clips = clips || ops[i].name == "W" || ops[i].name == "W*";
      } else if (path_painting(ops[i].name)) {
        for (const std::size_t p : pending) {
          owner[p] = i;
        }
        if (clips) {
          clipping.insert(i);
        }
        pending.clear();
        clips = false;
      }
    }
  }
  const auto moved_path = [&](const content::Operation& op, const Matrix& m) {
    if (op.name == "W" || op.name == "W*" || op.name == "h") {
      return op.name;
    }
    std::vector<double> v;
    v.reserve(op.operands.size());
    for (const Object& operand : op.operands) {
      v.push_back(operand.as_number().value_or(0));
    }
    std::string out;
    if (op.name == "re" && v.size() == 4 && (m.b != 0 || m.c != 0)) {
      // Turned: a rectangle no more, its corners.
      const std::array<Point, 4> corners = {m.apply(Point{v[0], v[1]}), m.apply(Point{v[0] + v[2], v[1]}), m.apply(Point{v[0] + v[2], v[1] + v[3]}),
                                            m.apply(Point{v[0], v[1] + v[3]})};
      return std::format("{} {} m {} {} l {} {} l {} {} l h", num(corners[0].x), num(corners[0].y), num(corners[1].x), num(corners[1].y), num(corners[2].x),
                         num(corners[2].y), num(corners[3].x), num(corners[3].y));
    }
    if (op.name == "re" && v.size() == 4) {
      const Point at = m.apply(Point{v[0], v[1]});
      v = {at.x, at.y, m.a * v[2], m.d * v[3]};
    } else {
      for (std::size_t k = 0; (2 * k) + 1 < v.size(); ++k) {
        const Point p = m.apply(Point{v[2 * k], v[(2 * k) + 1]});
        v[2 * k] = p.x;
        v[(2 * k) + 1] = p.y;
      }
    }
    for (const double x : v) {
      out += num(x) + " ";
    }
    return out + op.name;
  };
  const auto drawn = [&](std::size_t i) {
    const auto found = renamed.find(static_cast<std::uint32_t>(i));
    return found != renamed.end() ? operand_text(Object(name(found->second))) + " Do" : op_text(ops[i]);
  };
  std::string out;
  bool text_moved = false;  // in this BT … ET, text moved: every text shown is set where it goes
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const content::Operation& op = ops[i];
    if (op.name == "BT" || op.name == "ET") {
      text_moved = false;
    }
    if (const auto o = owner.find(i); o != owner.end()) {
      const auto e = edits.find(static_cast<std::uint32_t>(o->second));
      if (e != edits.end() && e->second.kind == OpMove::Kind::kPath) {
        out += moved_path(op, e->second.matrix) + "\n";
        continue;
      }
      if (e != edits.end() && e->second.kind == OpMove::Kind::kDelete && !clipping.contains(o->second)) {
        continue;  // the path of a painting taken out
      }
    }
    const auto e = edits.find(static_cast<std::uint32_t>(i));
    if (e == edits.end()) {
      out += drawn(i) + "\n";
      continue;
    }
    const OpMove& edit = e->second;
    if (edit.kind == OpMove::Kind::kDelete) {
      if (clipping.contains(i)) {
        out += "n\n";  // it clips what follows: it keeps clipping, painting nothing
      }
      continue;
    }
    if (edit.kind == OpMove::Kind::kWrap) {
      const Matrix& m = edit.matrix;
      out += std::format("q {} {} {} {} {} {} cm\n{}\nQ\n", num(m.a), num(m.b), num(m.c), num(m.d), num(m.e), num(m.f), drawn(i));
      continue;
    }
    if (edit.kind == OpMove::Kind::kText) {
      text_moved = text_moved || edit.moved;
      if (text_moved && !op.operands.empty()) {
        const Matrix& t = edit.tm;
        std::string prefix;
        std::string shown = op.name;
        if (op.name == "'") {
          shown = "Tj";
        } else if (op.name == "\"") {
          shown = "Tj";
          if (op.operands.size() >= 3) {
            prefix = std::format("{} Tw {} Tc ", operand_text(op.operands[0]), operand_text(op.operands[1]));
          }
        }
        out += std::format("{}{} {} {} {} {} {} Tm {} {}\n", prefix, num(t.a), num(t.b), num(t.c), num(t.d), num(t.e), num(t.f),
                           operand_text(op.operands.back()), shown);
        continue;
      }
    }
    out += drawn(i) + "\n";
  }
  return out;
}

}  // namespace

Result<void> apply_move(Document& document, std::uint32_t page_index, const MovePlan& plan) {
  auto page = page_at(document, page_index, "the page's content");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  const auto& invocations = plan.invocations;
  // Invocations to write anew: those with edits and their ancestors; deepest (last) first.
  std::set<std::uint32_t> needed;
  for (const auto& [inv, edits] : plan.edits) {
    if (!edits.empty() && inv < invocations.size()) {
      needed.insert(inv);
    }
  }
  for (std::uint32_t inv : std::set<std::uint32_t>(needed)) {
    for (std::size_t depth = 0; inv != 0 && depth < invocations.size(); ++depth) {
      inv = invocations[inv].parent;
      needed.insert(inv);
    }
  }
  const auto resources_of = [&](std::uint32_t invocation) -> Dict {
    if (invocation == 0) {
      const auto r = inherited(document, page->dict, "Resources");
      return r && r->as_dict() != nullptr ? *r->as_dict() : Dict{};
    }
    auto form = document.get(*invocations[invocation].form);
    const model::Stream* stream = form && *form != nullptr ? (*form)->as_stream() : nullptr;
    const Object* entry = stream != nullptr ? stream->dict.find("Resources") : nullptr;
    auto resolved = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
    return resolved && resolved->as_dict() != nullptr ? *resolved->as_dict() : Dict{};
  };
  // A form written anew is drawn by its parent under a new name.
  std::map<std::uint32_t, std::set<std::string>> taken;
  std::map<std::uint32_t, std::map<std::uint32_t, std::string>> renamed;
  for (const std::uint32_t inv : needed) {
    if (inv != 0) {
      const std::uint32_t parent = invocations[inv].parent;
      renamed[parent][invocations[inv].operation] = fresh_name(document, resources_of(parent), taken[parent], "CprMoved");
    }
  }
  std::map<std::uint32_t, std::vector<std::pair<std::string, ObjectRef>>> added;
  static const std::map<std::uint32_t, OpMove> kNone;
  for (const std::uint32_t inv : std::views::reverse(needed)) {
    std::vector<std::byte> source;
    Dict dict;
    if (inv == 0) {
      auto content = content::page_content(document, page->ref);
      if (!content) {
        return std::unexpected(std::move(content.error()));
      }
      source = std::move(*content);
    } else {
      const std::optional<ObjectRef> form_ref = invocations[inv].form;
      if (!form_ref) {
        continue;
      }
      auto form = document.get(*form_ref);
      const model::Stream* stream = form && *form != nullptr ? (*form)->as_stream() : nullptr;
      if (stream == nullptr) {
        return make_error(ErrorCode::kSyntax, "a form below the paragraph cannot be read");
      }
      auto decoded = document.decode_stream(*stream);
      if (!decoded) {
        return std::unexpected(std::move(decoded.error()));
      }
      source = std::move(*decoded);
      dict = stream->dict;
    }
    const auto edits = plan.edits.find(inv);
    const std::string content = moved_content(operations_of(source), edits != plan.edits.end() ? edits->second : kNone, renamed[inv]);
    Dict resources = with_xobjects(document, resources_of(inv), added[inv], {});
    if (inv != 0) {
      dict.set("Resources", std::move(resources));
      auto ref = add_content(document, std::move(dict), content);
      if (!ref) {
        return std::unexpected(std::move(ref.error()));
      }
      added[invocations[inv].parent].emplace_back(renamed[invocations[inv].parent][invocations[inv].operation], *ref);
      continue;
    }
    auto content_ref = add_content(document, Dict{}, content);
    if (!content_ref) {
      return std::unexpected(std::move(content_ref.error()));
    }
    auto current = document.get(page->ref);
    Dict page_dict = current && (*current)->as_dict() != nullptr ? *(*current)->as_dict() : page->dict;
    page_dict.set("Contents", *content_ref);
    page_dict.set("Resources", std::move(resources));
    if (auto set = document.set(page->ref.number, Object(std::move(page_dict))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  }
  // The annotations below, and their popups.
  for (const ObjectRef& ref : plan.annotations) {
    std::vector<ObjectRef> refs{ref};
    auto loaded = document.get(ref);
    if (loaded && (*loaded)->as_dict() != nullptr) {
      const Object* popup = (*loaded)->as_dict()->find("Popup");
      if (const auto popup_ref = popup != nullptr ? popup->as_ref() : std::nullopt) {
        refs.push_back(*popup_ref);
      }
    }
    for (const ObjectRef& r : refs) {
      auto annotation = document.get(r);
      if (!annotation || (*annotation)->as_dict() == nullptr) {
        continue;
      }
      Dict moved = *(*annotation)->as_dict();
      shift_annotation(document, moved, plan.dx, plan.dy);
      if (auto set = document.set(r.number, Object(std::move(moved))); !set) {
        return std::unexpected(std::move(set.error()));
      }
    }
  }
  return {};
}

}  // namespace capriole::edit::detail
