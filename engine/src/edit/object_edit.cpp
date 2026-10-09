#include "capriole/edit/object_edit.hpp"

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

#include "capriole/content/page_content.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/layout/page_model.hpp"
#include "common.hpp"
#include "content_move.hpp"
#include "content_rewrite.hpp"
#include "layout/drawn.hpp"

namespace capriole::edit {

namespace {

using graphics::Matrix;
using graphics::Point;
using graphics::Rect;

// Within this, a thing is inside a clip (points as shown).
constexpr double kSlack = 0.5;

Result<ObjectEditResult> refused(std::string_view why) { return make_error(ErrorCode::kUnsupported, std::format("cannot edit the object: {}", why)); }

bool path_painting(std::string_view n) {
  return n == "f" || n == "F" || n == "f*" || n == "S" || n == "s" || n == "B" || n == "B*" || n == "b" || n == "b*" || n == "n";
}

bool within(const Rect& inner, const Rect& outer) {
  return inner.x0 >= outer.x0 - kSlack && inner.y0 >= outer.y0 - kSlack && inner.x1 <= outer.x1 + kSlack && inner.y1 <= outer.y1 + kSlack;
}

Rect apply(const Matrix& m, const Rect& r) { return detail::transformed(r, m).normalized(); }

// The CTM a command is drawn with (nullopt: one that has none of its own, a tiling's).
std::optional<Matrix> ctm_of(const graphics::Command& command) {
  if (const auto* fill = std::get_if<graphics::FillPath>(&command)) {
    return fill->ctm;
  }
  if (const auto* stroke = std::get_if<graphics::StrokePath>(&command)) {
    return stroke->ctm;
  }
  if (const auto* clip = std::get_if<graphics::ClipPath>(&command)) {
    return clip->ctm;
  }
  if (const auto* image = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
    return (*image)->ctm;
  }
  if (const auto* shading = std::get_if<graphics::Boxed<graphics::PaintShading>>(&command)) {
    return (*shading)->ctm;
  }
  return std::nullopt;
}

}  // namespace

Result<ObjectEditResult> edit_object(Document& document, std::uint32_t page_index, const ObjectEdit& edit) {
  auto page = detail::page_at(document, page_index, "the page's content");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto model = layout::page_model(document, page_index);
  if (!model) {
    return std::unexpected(std::move(model.error()));
  }
  if (edit.element >= model->elements.size()) {
    return make_error(ErrorCode::kOutOfRange, std::format("no element {} on the page (it has {})", edit.element, model->elements.size()));
  }
  const layout::Element& element = model->elements[edit.element];
  if (element.kind == layout::Kind::kParagraph || element.kind == layout::Kind::kGlyphs) {
    return refused("it is text: a paragraph is edited as text");
  }
  if (element.kind == layout::Kind::kAnnotation) {
    return refused("it is an annotation: it is edited as one");
  }
  const Rect& from = element.box;
  // Where it goes, as shown: by a matrix of the page as shown.
  Matrix view;
  switch (edit.action) {
    case ObjectAction::kMove:
      if (!std::isfinite(edit.by.x) || !std::isfinite(edit.by.y)) {
        return refused("a move must be finite");
      }
      view = Matrix{1, 0, 0, 1, edit.by.x, edit.by.y};
      break;
    case ObjectAction::kResize: {
      const Rect to = edit.box.normalized();
      if (!layout::detail::finite(to) || to.width() <= 0 || to.height() <= 0 || from.width() <= 0 || from.height() <= 0) {
        return refused("a size must be finite and not empty");
      }
      const double sx = to.width() / from.width();
      const double sy = to.height() / from.height();
      view = Matrix{sx, 0, 0, sy, to.x0 - (sx * from.x0), to.y0 - (sy * from.y0)};
      break;
    }
    case ObjectAction::kDelete:
      break;
  }
  const Rect box = edit.action == ObjectAction::kDelete ? from : apply(view, from);
  if (edit.action != ObjectAction::kDelete && (box.x1 <= 0 || box.y1 <= 0 || box.x0 >= model->width || box.y0 >= model->height)) {
    return refused("it would leave the page");
  }

  const graphics::DisplayList& list = model->list;
  const Matrix& to_view = page->to_view;
  const layout::detail::Drawn drawn = layout::detail::drawn_items(document, *page, list);
  const std::set<std::size_t> commands(element.commands.begin(), element.commands.end());
  std::vector<bool> mine(drawn.operators, false);
  for (std::size_t k = 0; k < drawn.operators; ++k) {
    mine[k] = commands.contains(drawn.items[k].command);
  }
  const auto& invocations = list.invocations;

  // The operators' names, by invocation (the page's content, its forms').
  std::map<std::uint32_t, std::vector<content::Operation>> operations;
  const auto name_of = [&](std::uint32_t inv, std::uint32_t op) -> std::string {
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
      found = operations.emplace(inv, detail::operations_of(source)).first;
    }
    return op < found->second.size() ? found->second[op].name : std::string();
  };

  // Forms all of whose drawing is the object's: edited whole, by their Do (the outermost).
  std::vector<int> form_state(invocations.size(), -1);  // -1: nothing drawn, 0: not all the object's, 1: all
  for (std::size_t k = 0; k < drawn.operators; ++k) {
    for (std::uint32_t inv = drawn.items[k].invocation, depth = 0; inv != 0 && inv < invocations.size() && depth < invocations.size(); ++depth) {
      int& state = form_state[inv];
      state = state == -1 ? (mine[k] ? 1 : 0) : std::min(state, mine[k] ? 1 : 0);
      inv = invocations[inv].parent;
    }
  }
  const auto whole = [&](std::uint32_t inv) {
    std::uint32_t found = 0;
    for (std::size_t depth = 0; inv != 0 && inv < invocations.size() && depth < invocations.size(); ++depth) {
      if (form_state[inv] == 1) {
        found = inv;
      }
      inv = invocations[inv].parent;
    }
    return found;
  };

  detail::MovePlan plan;
  plan.invocations = invocations;
  std::uint32_t changed = 0;
  // The matrix that does in an operator's user space what `view` does as shown.
  const auto in_user = [&](const Matrix& ctm) -> std::optional<Matrix> {
    const Matrix shown = ctm.then(to_view);
    const auto back = shown.inverse();
    if (!back) {
      return std::nullopt;
    }
    return shown.then(view).then(*back);
  };
  const auto set = [&](std::uint32_t inv, std::uint32_t op, detail::OpMove move) {
    auto [at, added] = plan.edits[inv].try_emplace(op, move);
    if (added) {
      ++changed;
    }
  };
  for (std::size_t k = 0; k < drawn.operators; ++k) {
    if (!mine[k]) {
      continue;
    }
    const layout::detail::Item& item = drawn.items[k];
    if (edit.action != ObjectAction::kDelete && item.masked) {
      return refused("it is drawn through a soft mask that does not move with it");
    }
    if (const std::uint32_t form = whole(item.invocation); form != 0) {
      const graphics::Invocation& outer = invocations[form];
      if (edit.action == ObjectAction::kDelete) {
        set(outer.parent, outer.operation, {.kind = detail::OpMove::Kind::kDelete, .matrix = {}, .tm = {}, .moved = true});
        continue;
      }
      const auto m = in_user(outer.ctm);
      if (!m) {
        return refused("a form it is in has a singular matrix");
      }
      set(outer.parent, outer.operation, {.kind = detail::OpMove::Kind::kWrap, .matrix = *m, .tm = {}, .moved = true});
      continue;
    }
    const std::string n = name_of(item.invocation, item.operation);
    const bool path = path_painting(n);
    if (!path && n != "Do" && n != "BI" && n != "sh") {
      return refused(std::format("an operator of it ({}) cannot be edited", n));
    }
    if (edit.action == ObjectAction::kDelete) {
      set(item.invocation, item.operation, {.kind = detail::OpMove::Kind::kDelete, .matrix = {}, .tm = {}, .moved = true});
      continue;
    }
    const auto ctm = ctm_of(list.commands[item.command]);
    const auto m = ctm ? in_user(*ctm) : std::nullopt;
    if (!m) {
      return refused("it has a singular matrix, or one that cannot be told");
    }
    set(item.invocation, item.operation, {.kind = path ? detail::OpMove::Kind::kPath : detail::OpMove::Kind::kWrap, .matrix = *m, .tm = {}, .moved = true});
  }
  // Clips: one its own path sets (W f) must clip only it; one that clips only it goes with it; it must stay
  // inside the others.
  for (std::size_t c = 0; c < drawn.clips.size(); ++c) {
    const layout::detail::Clip& clip = drawn.clips[c];
    if (!clip.rewritable) {
      continue;
    }
    const bool own_path =
        std::ranges::any_of(element.sources, [&](const layout::Source& s) { return s.invocation == clip.invocation && s.operation == clip.operation; });
    const bool only_mine = !clip.under.empty() && std::ranges::all_of(clip.under, [&](std::size_t k) { return k < drawn.operators && mine[k]; });
    if (own_path && !only_mine && edit.action != ObjectAction::kDelete) {
      return refused("its path also clips what is drawn after it");
    }
    if (only_mine && whole(clip.invocation) == 0 && path_painting(name_of(clip.invocation, clip.operation)) && !own_path) {
      if (edit.action == ObjectAction::kDelete) {
        continue;  // the clip stays, clipping nothing more
      }
      const auto ctm = ctm_of(list.commands[clip.command]);
      const auto m = ctm ? in_user(*ctm) : std::nullopt;
      if (!m) {
        return refused("a clip of it has a singular matrix");
      }
      set(clip.invocation, clip.operation, {.kind = detail::OpMove::Kind::kPath, .matrix = *m, .tm = {}, .moved = true});
    }
  }
  if (edit.action != ObjectAction::kDelete) {
    for (std::size_t k = 0; k < drawn.operators; ++k) {
      if (!mine[k]) {
        continue;
      }
      const Rect to = apply(view, drawn.items[k].box);
      for (const std::size_t c : drawn.items[k].clips) {
        const layout::detail::Clip& clip = drawn.clips[c];
        // It moves when its operator is edited: the object's own path, a clip only of it, the Do of a form moved whole.
        const auto edits = plan.edits.find(clip.invocation);
        const bool moves = edits != plan.edits.end() && edits->second.contains(clip.operation);
        if (!moves && within(drawn.items[k].box, clip.box) && !within(to, clip.box)) {
          return refused("it would leave what clips it (a frame, a form's bounding box)");
        }
      }
    }
  }
  if (auto applied = detail::apply_move(document, page_index, plan); !applied) {
    return std::unexpected(std::move(applied.error()));
  }
  return ObjectEditResult{.operators = changed, .box = box};
}

}  // namespace capriole::edit
