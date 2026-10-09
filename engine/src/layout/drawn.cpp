#include "drawn.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include "capriole/content/appearance.hpp"

namespace capriole::layout::detail {

using graphics::Matrix;
using graphics::Rect;
using model::Object;

std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> content_operators(const graphics::DisplayList& list) {
  std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> out;
  for (std::size_t c = 0; c < list.commands.size() && c < list.origins.size(); ++c) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&list.commands[c]);
    if (t == nullptr) {
      continue;
    }
    bool content = true;
    for (std::uint32_t at = list.origins[c].invocation, steps = 0; at != 0 && steps < list.invocations.size(); at = list.invocations[at].parent, ++steps) {
      if (at >= list.invocations.size() || list.invocations[at].kind != graphics::Invocation::Kind::kForm) {
        content = false;
        break;
      }
    }
    if (content) {
      out.emplace((*t)->stream ? std::optional((*t)->stream->number) : std::nullopt, (*t)->operation);
    }
  }
  return out;
}

bool of_content(const std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>>& operators, const text::Provenance& p) {
  return operators.contains({p.stream ? std::optional(p.stream->number) : std::nullopt, p.operation});
}

bool finite(const Rect& r) { return std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1); }

namespace {

Rect united(const Rect& a, const Rect& b) { return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)}; }

}  // namespace

std::optional<Rect> run_box(const graphics::ShowText& run, const Matrix& to_view) {
  std::optional<Rect> out;
  for (const graphics::Glyph& glyph : run.glyphs) {
    const Rect box = run.vertical
                         ? Rect{glyph.origin.x - (run.font_size / 2), glyph.origin.y - run.font_size, glyph.origin.x + (run.font_size / 2), glyph.origin.y}
                         : Rect{glyph.origin.x, glyph.origin.y + (run.descent * run.font_size), glyph.origin.x + std::max(glyph.width, 0.0),
                                glyph.origin.y + (run.ascent * run.font_size)};
    const Rect view = edit::detail::transformed(box.normalized(), run.text_matrix.then(to_view));
    if (finite(view)) {
      out = out ? united(*out, view) : view;
    }
  }
  return out;
}

bool rewritable(const graphics::DisplayList& list, std::uint32_t invocation) {
  const auto& invocations = list.invocations;
  for (std::size_t depth = 0; depth < invocations.size() && invocation < invocations.size(); ++depth) {
    const graphics::Invocation& inv = invocations[invocation];
    if (inv.kind == graphics::Invocation::Kind::kPage) {
      return true;
    }
    if (inv.kind != graphics::Invocation::Kind::kForm || !inv.form) {
      return false;
    }
    invocation = inv.parent;
  }
  return false;
}

Drawn drawn_items(Document& document, const edit::detail::Page& page, const graphics::DisplayList& list) {
  using edit::detail::path_bounds;
  using edit::detail::transformed;
  const Matrix& to_view = page.to_view;
  const Rect shown = transformed(list.crop_box, to_view).normalized();
  Drawn out;
  auto& items = out.items;
  auto& clips = out.clips;
  struct Frame {
    std::vector<std::size_t> clips;
    bool masked = false;
  };
  std::vector<Frame> frames(1);
  std::size_t in_mask = 0;  // the commands of a soft mask's group: not drawn on the page
  const auto clip_box = [&]() {
    Rect box = shown;
    for (const std::size_t c : frames.back().clips) {
      box = Rect{std::max(box.x0, clips[c].box.x0), std::max(box.y0, clips[c].box.y0), std::min(box.x1, clips[c].box.x1), std::min(box.y1, clips[c].box.y1)};
    }
    return box;
  };
  for (std::size_t i = 0; i < list.commands.size() && i < list.origins.size(); ++i) {
    const auto& command = list.commands[i];
    const graphics::Origin origin = list.origins[i];
    if (std::holds_alternative<graphics::Save>(command)) {
      frames.push_back(frames.back());
      continue;
    }
    if (std::holds_alternative<graphics::Restore>(command)) {
      if (frames.size() > 1) {
        frames.pop_back();
      }
      continue;
    }
    if (std::holds_alternative<graphics::BeginSoftMask>(command)) {
      ++in_mask;
      continue;
    }
    if (std::holds_alternative<graphics::EndSoftMask>(command)) {
      in_mask -= in_mask > 0 ? 1 : 0;
      frames.back().masked = true;
      continue;
    }
    if (std::holds_alternative<graphics::ClearSoftMask>(command)) {
      frames.back().masked = false;
      continue;
    }
    if (in_mask > 0) {
      continue;
    }
    const bool own = rewritable(list, origin.invocation);
    if (const auto* clip = std::get_if<graphics::ClipPath>(&command)) {
      const auto bounds = path_bounds(clip->path);
      const Rect box = bounds ? transformed(*bounds, clip->ctm.then(to_view)).normalized() : Rect{};
      clips.push_back(
          {.box = finite(box) ? box : shown, .invocation = origin.invocation, .operation = origin.operation, .command = i, .rewritable = own, .under = {}});
      frames.back().clips.push_back(clips.size() - 1);
      continue;
    }
    if (!own) {
      continue;  // a Type 3 glyph's commands: they go with their run; an annotation's appearance
    }
    std::optional<Rect> box;
    Item::Kind kind = Item::Kind::kFill;
    bool text_clip = false;
    bool plain = false;
    if (const auto* fill = std::get_if<graphics::FillPath>(&command)) {
      if (const auto b = path_bounds(fill->path)) {
        box = transformed(*b, fill->ctm.then(to_view)).normalized();
      }
      // One shape (a rectangle, a polygon of a few sides): not the lines of a table, drawn as one path.
      plain = fill->path.size() <= 8 && std::ranges::count(fill->path.verbs(), graphics::SegmentKind::kMove) == 1;
    } else if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
      kind = Item::Kind::kStroke;
      if (const auto b = path_bounds(stroke->path)) {
        box = transformed(*b, stroke->ctm.then(to_view)).normalized();
      }
    } else if (const auto* image = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      kind = Item::Kind::kImage;
      box = transformed(Rect{0, 0, 1, 1}, (*image)->ctm.then(to_view)).normalized();
      plain = true;
    } else if (const auto* run = std::get_if<graphics::Boxed<graphics::ShowText>>(&command)) {
      kind = Item::Kind::kText;
      box = run_box(**run, to_view);
      text_clip = (*run)->render_mode >= 4;
    } else if (std::holds_alternative<graphics::Boxed<graphics::PaintShading>>(command) ||
               std::holds_alternative<graphics::Boxed<graphics::PaintTiling>>(command)) {
      kind = Item::Kind::kShading;
      box = clip_box();  // a shading or a pattern fills what the clip leaves
      plain = true;
    }
    if (!box || !finite(*box)) {
      continue;
    }
    items.push_back({.kind = kind,
                     .box = *box,
                     .invocation = origin.invocation,
                     .operation = origin.operation,
                     .command = i,
                     .clips = frames.back().clips,
                     .masked = frames.back().masked,
                     .text_clip = text_clip,
                     .plain = plain,
                     .annotation = std::nullopt,
                     .subtype = {}});
    for (const std::size_t c : frames.back().clips) {
      clips[c].under.push_back(items.size() - 1);
    }
  }
  out.operators = items.size();
  if (const Object* entry = page.dict.find("Annots")) {
    auto annots = document.resolve(*entry);
    if (annots && annots->as_array() != nullptr) {
      for (const Object& element : *annots->as_array()) {
        auto annotation = document.resolve(element);
        if (!annotation || annotation->as_dict() == nullptr) {
          continue;
        }
        std::string subtype = edit::detail::subtype_of(*annotation->as_dict());
        if (subtype == "Popup") {
          continue;
        }
        const std::vector<double> r = content::numbers(document, *annotation->as_dict(), "Rect");
        if (r.size() != 4) {
          continue;
        }
        const Rect box = page.view(Rect{r[0], r[1], r[2], r[3]}.normalized());
        if (!finite(box)) {
          continue;
        }
        items.push_back({.kind = Item::Kind::kAnnotation,
                         .box = box,
                         .invocation = 0,
                         .operation = 0,
                         .command = 0,
                         .clips = {},
                         .masked = false,
                         .text_clip = false,
                         .plain = false,
                         .annotation = element.as_ref(),
                         .subtype = std::move(subtype)});
      }
    }
  }
  return out;
}

}  // namespace capriole::layout::detail
