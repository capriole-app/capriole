#include "capriole/content/interpreter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/content/page_content.hpp"
#include "color_space.hpp"
#include "text_font.hpp"

namespace capriole::content {

namespace {

using detail::ColorSpace;
using detail::TextFont;
using detail::Warnings;
using graphics::Color;
using graphics::Matrix;
using graphics::Point;
using model::Object;

std::optional<double> number(const Object& object) {
  if (const auto i = object.as_integer()) {
    return static_cast<double>(*i);
  }
  if (const auto* r = std::get_if<double>(&object.value())) {
    return std::isfinite(*r) ? std::optional<double>(*r) : std::nullopt;
  }
  return std::nullopt;
}

struct GraphicsState {
  Matrix ctm;
  graphics::StrokeStyle stroke_style;
  ColorSpace fill_space;
  ColorSpace stroke_space;
  Color fill = detail::initial_color(ColorSpace{});
  Color stroke = detail::initial_color(ColorSpace{});
  double fill_alpha = 1;    // ca
  double stroke_alpha = 1;  // CA
  // Text state (§9.3), part of the graphics state.
  double char_spacing = 0;  // Tc
  double word_spacing = 0;  // Tw
  double scale = 1;         // Tz / 100
  double leading = 0;       // TL
  double rise = 0;          // Ts
  std::int32_t render_mode = 0;
  std::string font_name;
  std::optional<model::ObjectRef> font_ref;
  std::shared_ptr<const TextFont> font;
  double font_size = 0;
};

class Interpreter {
 public:
  Interpreter(Document& document, const InterpreterLimits& limits, graphics::DisplayList& out) : document_(&document), limits_(&limits), out_(&out) {}

  void run(std::span<const std::byte> content, const model::Dict* resources, std::size_t form_depth) {
    ContentParser parser(content, limits_->parse, parser::ParseMode::kTolerant);
    const model::Dict* saved_resources = resources_;
    resources_ = resources;
    const std::size_t base_depth = stack_.size();
    for (;;) {
      if (stopped_) {
        break;
      }
      auto operation = parser.next();
      if (!operation) {
        warnings_.add("content stream unreadable: " + operation.error().message);
        break;
      }
      if (!*operation) {
        break;
      }
      if (++operations_ > limits_->max_operations) {
        warnings_.add("too many operations: the rest of the page is not drawn");
        stopped_ = true;
        break;
      }
      execute(**operation, form_depth);
    }
    if (parser.recovered_errors() > 0) {
      warnings_.add("syntax errors skipped in a content stream");
    }
    // A content stream leaves the graphics state as it found it (§7.8.2): close unbalanced q.
    while (stack_.size() > base_depth) {
      restore();
    }
    resources_ = saved_resources;
  }

  std::vector<std::string> take_warnings() { return warnings_.take(); }

 private:
  // Colours and stroke styles go into the list's tables once; a command refers to them by index. The
  // last index of each kind is remembered: consecutive commands almost always repeat it.
  static bool same(const Color& a, const Color& b) { return a.family == b.family && a.components == b.components && a.pattern == b.pattern; }
  static bool same(const graphics::StrokeStyle& a, const graphics::StrokeStyle& b) {
    return a.width == b.width && a.cap == b.cap && a.join == b.join && a.miter_limit == b.miter_limit && a.dash == b.dash && a.dash_phase == b.dash_phase;
  }

  graphics::ColorIndex intern(const Color& color, std::optional<graphics::ColorIndex>& last) {
    if (last && same(out_->colors[*last], color)) {
      return *last;
    }
    last = static_cast<graphics::ColorIndex>(out_->colors.size());
    out_->colors.push_back(color);
    return *last;
  }

  graphics::StyleIndex intern(const graphics::StrokeStyle& style) {
    if (last_style_ && same(out_->styles[*last_style_], style)) {
      return *last_style_;
    }
    last_style_ = static_cast<graphics::StyleIndex>(out_->styles.size());
    out_->styles.push_back(style);
    return *last_style_;
  }

  template <typename T>
  void emit(T command) {
    if (out_->commands.size() >= limits_->max_commands) {
      if (!stopped_) {
        warnings_.add("too many drawing commands: the rest of the page is not drawn");
      }
      stopped_ = true;
      return;
    }
    out_->commands.emplace_back(std::move(command));
  }

  // Operands as numbers; nullopt (and a warning) when there are too few or they are not numbers.
  std::optional<std::vector<double>> numbers(const Operation& op, std::size_t count) {
    if (op.operands.size() < count) {
      warnings_.add("operator " + op.name + " with too few operands: skipped");
      return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t i = op.operands.size() - count; i < op.operands.size(); ++i) {
      const auto value = number(op.operands[i]);
      if (!value) {
        warnings_.add("operator " + op.name + " with a non-numeric operand: skipped");
        return std::nullopt;
      }
      values.push_back(*value);
    }
    return values;
  }

  const model::Name* last_name(const Operation& op) {
    const model::Name* name = op.operands.empty() ? nullptr : op.operands.back().as_name();
    if (name == nullptr) {
      warnings_.add("operator " + op.name + " without its name operand: skipped");
    }
    return name;
  }

  // A named resource of a category (/Font, /XObject, /ExtGState, /Shading, /ColorSpace, /Pattern).
  std::optional<Object> resource(std::string_view category, const std::string& name) {
    if (resources_ == nullptr) {
      return std::nullopt;
    }
    const Object* group = resources_->find(category);
    auto dict = group != nullptr ? document_->resolve(*group) : Result<Object>(Object());
    if (!dict || dict->as_dict() == nullptr) {
      return std::nullopt;
    }
    const Object* entry = dict->as_dict()->find(name);
    if (entry == nullptr) {
      return std::nullopt;
    }
    return *entry;  // possibly a reference: callers resolve, and some need the reference itself
  }

  void save() {
    if (stack_.size() >= limits_->max_save_depth) {
      warnings_.add("graphics state saved too deeply (q): further saves ignored");
      ++ignored_saves_;
      return;
    }
    stack_.push_back(state_);
    emit(graphics::Save{});
  }

  void restore() {
    if (ignored_saves_ > 0) {
      --ignored_saves_;
      return;
    }
    if (stack_.empty()) {
      warnings_.add("Q without a matching q: ignored");
      return;
    }
    state_ = std::move(stack_.back());
    stack_.pop_back();
    emit(graphics::Restore{});
  }

  // After a painting operator: the clip set by W/W* takes effect, and the path is consumed (§8.5.4).
  void end_path() {
    if (pending_clip_) {
      emit(graphics::ClipPath{.path = path_, .ctm = state_.ctm, .rule = *pending_clip_});
      pending_clip_.reset();
    }
    path_ = {};
    has_current_point_ = false;
  }

  void fill(graphics::FillRule rule) {
    if (!path_.empty()) {
      emit(graphics::FillPath{.path = path_, .ctm = state_.ctm, .rule = rule, .color = intern(state_.fill, last_fill_), .alpha = state_.fill_alpha});
    }
  }

  void stroke() {
    if (!path_.empty()) {
      emit(graphics::StrokePath{
          .path = path_, .ctm = state_.ctm, .style = intern(state_.stroke_style), .color = intern(state_.stroke, last_stroke_), .alpha = state_.stroke_alpha});
    }
  }

  void close_path() {
    if (has_current_point_) {
      path_.close();
      current_point_ = subpath_start_;
    }
  }

  void set_color(const Operation& op, bool for_fill) {
    const ColorSpace& space = for_fill ? state_.fill_space : state_.stroke_space;
    std::vector<double> components;
    std::string pattern;
    for (const Object& operand : op.operands) {
      if (const auto value = number(operand)) {
        components.push_back(*value);
      } else if (const model::Name* name = operand.as_name()) {
        pattern = name->bytes;
      }
    }
    (for_fill ? state_.fill : state_.stroke) = detail::make_color(space, components, std::move(pattern), warnings_);
  }

  void set_device_color(const Operation& op, bool for_fill, graphics::ColorSpaceFamily family, std::size_t count) {
    const auto values = numbers(op, count);
    if (!values) {
      return;
    }
    ColorSpace space;
    space.family = family;
    space.components = count;
    (for_fill ? state_.fill_space : state_.stroke_space) = space;
    (for_fill ? state_.fill : state_.stroke) = detail::make_color(space, *values, {}, warnings_);
  }

  void set_color_space(const Operation& op, bool for_fill) {
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    ColorSpace space = detail::resolve_color_space(*document_, op.operands.back(), resources_, warnings_);
    (for_fill ? state_.fill : state_.stroke) = detail::initial_color(space);
    (for_fill ? state_.fill_space : state_.stroke_space) = std::move(space);
  }

  void set_ext_gstate(const Operation& op) {
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    const auto entry = resource("ExtGState", name->bytes);
    auto dict = entry ? document_->resolve(*entry) : Result<Object>(Object());
    if (!dict || dict->as_dict() == nullptr) {
      warnings_.add("graphics state /" + name->bytes + " not found");
      return;
    }
    for (const auto& [key, raw] : dict->as_dict()->entries()) {
      auto value = document_->resolve(raw);
      if (!value) {
        continue;
      }
      if (key == "LW") {
        state_.stroke_style.width = number(*value).value_or(state_.stroke_style.width);
      } else if (key == "LC") {
        state_.stroke_style.cap = static_cast<graphics::LineCap>(std::clamp<std::int64_t>(value->as_integer().value_or(0), 0, 2));
      } else if (key == "LJ") {
        state_.stroke_style.join = static_cast<graphics::LineJoin>(std::clamp<std::int64_t>(value->as_integer().value_or(0), 0, 2));
      } else if (key == "ML") {
        state_.stroke_style.miter_limit = number(*value).value_or(state_.stroke_style.miter_limit);
      } else if (key == "CA") {
        state_.stroke_alpha = std::clamp(number(*value).value_or(1), 0.0, 1.0);
      } else if (key == "ca") {
        state_.fill_alpha = std::clamp(number(*value).value_or(1), 0.0, 1.0);
      } else if (key == "D" && value->as_array() != nullptr && value->as_array()->size() == 2) {
        set_dash((*value->as_array())[0], (*value->as_array())[1]);
      } else if (key == "Font" && value->as_array() != nullptr && value->as_array()->size() == 2) {
        const Object& font = (*value->as_array())[0];
        state_.font_ref = font.as_ref();
        state_.font = std::make_shared<const TextFont>(detail::load_text_font(*document_, font, warnings_));
        state_.font_size = number((*value->as_array())[1]).value_or(0);
      } else if ((key == "BM" && !value->is_name("Normal") && !value->is_name("Compatible")) || (key == "SMask" && !value->is_name("None"))) {
        warnings_.add("blend modes and soft masks are not applied yet");
      }
    }
  }

  void set_dash(const Object& array, const Object& phase) {
    state_.stroke_style.dash.clear();
    if (auto resolved = document_->resolve(array); resolved && resolved->as_array() != nullptr) {
      for (const Object& value : *resolved->as_array()) {
        if (const auto length = number(value); length && *length >= 0 && state_.stroke_style.dash.size() < 64) {
          state_.stroke_style.dash.push_back(*length);
        }
      }
    }
    state_.stroke_style.dash_phase = number(phase).value_or(0);
  }

  // ── Text ─────────────────────────────────────────────────────────────────────────────────

  void set_font(const Operation& op) {
    if (op.operands.size() < 2 || op.operands[0].as_name() == nullptr) {
      warnings_.add("Tf without a font name and size: skipped");
      return;
    }
    const std::string& name = op.operands[0].as_name()->bytes;
    state_.font_name = name;
    state_.font_size = number(op.operands[1]).value_or(0);
    const auto entry = resource("Font", name);
    if (!entry) {
      warnings_.add("font /" + name + " not found in the resources: text advances estimated");
      state_.font = std::make_shared<const TextFont>(detail::load_text_font(*document_, Object(), warnings_));
      state_.font_ref.reset();
      return;
    }
    state_.font_ref = entry->as_ref();
    state_.font = std::make_shared<const TextFont>(detail::load_text_font(*document_, *entry, warnings_));
  }

  // Moves to the start of the next line, offset by (tx, ty) from the current one (Td).
  void next_line(double tx, double ty) {
    line_matrix_ = Matrix::translate(tx, ty).then(line_matrix_);
    text_matrix_ = line_matrix_;
  }

  // Tj, TJ, ' and ": a run of glyphs, advancing the text matrix (§9.4.4).
  void show(const Object& operand) {
    if (!in_text_) {
      warnings_.add("text shown outside BT/ET: drawn anyway");
    }
    if (!state_.font) {
      warnings_.add("text shown before any Tf: advances estimated");
      state_.font = std::make_shared<const TextFont>(detail::load_text_font(*document_, Object(), warnings_));
    }
    const TextFont& font = *state_.font;
    graphics::ShowText run;
    run.text_matrix = text_matrix_.then(state_.ctm);
    run.ctm = state_.ctm;
    run.font = state_.font_name;
    run.font_ref = state_.font_ref;
    run.font_size = state_.font_size;
    run.horizontal_scale = state_.scale;
    run.render_mode = state_.render_mode;
    run.fill = intern(state_.fill, last_fill_);
    run.stroke = intern(state_.stroke, last_stroke_);
    run.alpha = state_.fill_alpha;
    run.widths_known = font.widths_known;
    run.latin_codes = font.latin_codes;

    double x = 0;
    const auto add_string = [&](const std::string& bytes) {
      run.bytes += bytes;
      for (const std::uint32_t code : font.codes(bytes)) {
        // tx = ((w0 · Tfs) + Tc + Tw) · Th, Tw only for the single-byte code 32 (§9.4.4).
        double advance = (font.width(code) * font.scale * state_.font_size) + state_.char_spacing;
        if (!font.two_byte && code == 32) {
          advance += state_.word_spacing;
        }
        advance *= state_.scale;
        run.glyphs.push_back({.code = code, .origin = {x, state_.rise}, .advance = advance});
        x += advance;
      }
    };
    if (const model::String* string = operand.as_string()) {
      add_string(string->bytes);
    } else if (const model::Array* array = operand.as_array()) {
      for (const Object& element : *array) {
        if (const model::String* part = element.as_string()) {
          add_string(part->bytes);
        } else if (const auto adjust = number(element)) {
          x -= (*adjust / 1000) * state_.font_size * state_.scale;  // TJ numbers move back (§9.4.3)
        }
      }
    } else {
      warnings_.add("text operator without a string: skipped");
      return;
    }
    text_matrix_ = Matrix::translate(x, 0).then(text_matrix_);
    if (!run.glyphs.empty()) {
      emit(graphics::Boxed<graphics::ShowText>(std::move(run)));
    }
  }

  // ── XObjects, images, shadings ─────────────────────────────────────────────────────────────

  void draw_xobject(const Operation& op, std::size_t form_depth) {
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    const auto entry = resource("XObject", name->bytes);
    if (!entry) {
      warnings_.add("XObject /" + name->bytes + " not found");
      return;
    }
    auto object = document_->resolve(*entry);
    const model::Stream* stream = object ? object->as_stream() : nullptr;
    if (stream == nullptr) {
      warnings_.add("XObject /" + name->bytes + " is not a stream");
      return;
    }
    const Object* subtype = stream->dict.find("Subtype");
    if (subtype != nullptr && subtype->is_name("Image")) {
      graphics::DrawImage image;
      image.ctm = state_.ctm;
      image.xobject = entry->as_ref();
      image.width = stream->dict.find("Width") != nullptr ? stream->dict.find("Width")->as_integer().value_or(0) : 0;
      image.height = stream->dict.find("Height") != nullptr ? stream->dict.find("Height")->as_integer().value_or(0) : 0;
      const Object* mask = stream->dict.find("ImageMask");
      image.mask = mask != nullptr && std::holds_alternative<bool>(mask->value()) && std::get<bool>(mask->value());
      image.color = intern(state_.fill, last_fill_);
      image.alpha = state_.fill_alpha;
      emit(graphics::Boxed<graphics::DrawImage>(std::move(image)));
      return;
    }
    if (subtype != nullptr && subtype->is_name("Form")) {
      draw_form(entry->as_ref(), *stream, form_depth);
      return;
    }
    warnings_.add("XObject /" + name->bytes + " of an unsupported kind: skipped");
  }

  void draw_form(std::optional<model::ObjectRef> ref, const model::Stream& form, std::size_t form_depth) {
    if (form_depth >= limits_->max_form_depth) {
      warnings_.add("form XObjects nested too deeply: skipped");
      return;
    }
    if (ref && !active_forms_.insert(ref->number).second) {
      warnings_.add("a form XObject draws itself: the cycle is cut");
      return;
    }
    auto content = document_->decode_stream(form);
    if (!content) {
      warnings_.add("form XObject content unreadable: " + content.error().message);
      if (ref) {
        active_forms_.erase(ref->number);
      }
      return;
    }
    // §8.10.1: q, cm /Matrix, clip to /BBox, the content, Q.
    save();
    if (const Object* matrix = form.dict.find("Matrix")) {
      if (auto m = document_->resolve(*matrix); m && m->as_array() != nullptr && m->as_array()->size() == 6) {
        std::vector<double> v;
        for (const Object& element : *m->as_array()) {
          v.push_back(number(element).value_or(0));
        }
        state_.ctm = Matrix{v[0], v[1], v[2], v[3], v[4], v[5]}.then(state_.ctm);
      }
    }
    if (const Object* bbox = form.dict.find("BBox")) {
      if (auto b = document_->resolve(*bbox); b && b->as_array() != nullptr && b->as_array()->size() == 4) {
        std::vector<double> v;
        for (const Object& element : *b->as_array()) {
          v.push_back(number(element).value_or(0));
        }
        const graphics::Rect r = graphics::Rect{v[0], v[1], v[2], v[3]}.normalized();
        graphics::Path box;
        box.move_to({r.x0, r.y0});
        box.line_to({r.x1, r.y0});
        box.line_to({r.x1, r.y1});
        box.line_to({r.x0, r.y1});
        box.close();
        emit(graphics::ClipPath{.path = std::move(box), .ctm = state_.ctm, .rule = graphics::FillRule::kNonZero});
      }
    }
    const model::Dict* resources = resources_;
    std::optional<Object> own;
    if (const Object* entry = form.dict.find("Resources")) {
      if (auto r = document_->resolve(*entry); r && r->as_dict() != nullptr) {
        own = std::move(*r);
        resources = own->as_dict();
      }
    }
    // The form's content runs with its own path and text state.
    const graphics::Path saved_path = std::exchange(path_, {});
    const bool saved_in_text = std::exchange(in_text_, false);
    run(*content, resources, form_depth + 1);
    path_ = saved_path;
    in_text_ = saved_in_text;
    restore();
    if (ref) {
      active_forms_.erase(ref->number);
    }
  }

  void draw_inline_image(const Operation& op) {
    if (!op.image) {
      return;
    }
    const auto integer = [&](std::string_view full, std::string_view abbreviated) -> std::int64_t {
      const Object* value = op.image->dict.find(full) != nullptr ? op.image->dict.find(full) : op.image->dict.find(abbreviated);
      return value != nullptr ? value->as_integer().value_or(0) : 0;
    };
    graphics::DrawImage image;
    image.ctm = state_.ctm;
    image.inline_dict = op.image->dict;
    image.inline_data = std::make_shared<const std::vector<std::byte>>(op.image->data);
    image.width = integer("Width", "W");
    image.height = integer("Height", "H");
    const Object* mask = op.image->dict.find("ImageMask") != nullptr ? op.image->dict.find("ImageMask") : op.image->dict.find("IM");
    image.mask = mask != nullptr && std::holds_alternative<bool>(mask->value()) && std::get<bool>(mask->value());
    image.color = intern(state_.fill, last_fill_);
    image.alpha = state_.fill_alpha;
    emit(graphics::Boxed<graphics::DrawImage>(std::move(image)));
  }

  void paint_shading(const Operation& op) {
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    const auto entry = resource("Shading", name->bytes);
    auto shading = entry ? document_->resolve(*entry) : Result<Object>(Object());
    if (!shading || shading->is_null()) {
      warnings_.add("shading /" + name->bytes + " not found");
      return;
    }
    warnings_.add("shadings are placed but not drawn yet");
    emit(graphics::Boxed<graphics::PaintShading>(graphics::PaintShading{.ctm = state_.ctm, .shading = std::move(*shading)}));
  }

  // ── Dispatch ───────────────────────────────────────────────────────────────────────────────

  void execute(const Operation& op, std::size_t form_depth) {
    using enum Operator;
    switch (op.op) {
      // General graphics state
      case kSetLineWidth:
        if (const auto v = numbers(op, 1)) {
          state_.stroke_style.width = (*v)[0];
        }
        break;
      case kSetLineCap:
        if (const auto v = numbers(op, 1)) {
          state_.stroke_style.cap = static_cast<graphics::LineCap>(static_cast<int>(std::clamp((*v)[0], 0.0, 2.0)));
        }
        break;
      case kSetLineJoin:
        if (const auto v = numbers(op, 1)) {
          state_.stroke_style.join = static_cast<graphics::LineJoin>(static_cast<int>(std::clamp((*v)[0], 0.0, 2.0)));
        }
        break;
      case kSetMiterLimit:
        if (const auto v = numbers(op, 1)) {
          state_.stroke_style.miter_limit = (*v)[0];
        }
        break;
      case kSetDash:
        if (op.operands.size() >= 2) {
          set_dash(op.operands[op.operands.size() - 2], op.operands.back());
        }
        break;
      case kSetRenderingIntent:
      case kSetFlatness:
        break;  // device details: no effect on the display list
      case kSetExtGState:
        set_ext_gstate(op);
        break;
      // Special graphics state
      case kSave:
        save();
        break;
      case kRestore:
        restore();
        break;
      case kConcatMatrix:
        if (const auto v = numbers(op, 6)) {
          state_.ctm = Matrix{(*v)[0], (*v)[1], (*v)[2], (*v)[3], (*v)[4], (*v)[5]}.then(state_.ctm);
        }
        break;
      // Path construction
      case kMoveTo:
        if (const auto v = numbers(op, 2)) {
          current_point_ = subpath_start_ = {(*v)[0], (*v)[1]};
          path_.move_to(current_point_);
          has_current_point_ = true;
        }
        break;
      case kLineTo:
        if (const auto v = numbers(op, 2); v && has_current_point_) {
          current_point_ = {(*v)[0], (*v)[1]};
          path_.line_to(current_point_);
        }
        break;
      case kCurveTo:
        if (const auto v = numbers(op, 6); v && has_current_point_) {
          path_.cubic_to({(*v)[0], (*v)[1]}, {(*v)[2], (*v)[3]}, {(*v)[4], (*v)[5]});
          current_point_ = {(*v)[4], (*v)[5]};
        }
        break;
      case kCurveToV:  // first control point = current point
        if (const auto v = numbers(op, 4); v && has_current_point_) {
          path_.cubic_to(current_point_, {(*v)[0], (*v)[1]}, {(*v)[2], (*v)[3]});
          current_point_ = {(*v)[2], (*v)[3]};
        }
        break;
      case kCurveToY:  // second control point = end point
        if (const auto v = numbers(op, 4); v && has_current_point_) {
          path_.cubic_to({(*v)[0], (*v)[1]}, {(*v)[2], (*v)[3]}, {(*v)[2], (*v)[3]});
          current_point_ = {(*v)[2], (*v)[3]};
        }
        break;
      case kClosePath:
        close_path();
        break;
      case kRectangle:
        if (const auto v = numbers(op, 4)) {
          const double x = (*v)[0];
          const double y = (*v)[1];
          const double w = (*v)[2];
          const double h = (*v)[3];
          path_.move_to({x, y});
          path_.line_to({x + w, y});
          path_.line_to({x + w, y + h});
          path_.line_to({x, y + h});
          path_.close();
          current_point_ = subpath_start_ = {x, y};
          has_current_point_ = true;
        }
        break;
      // Path painting
      case kStroke:
        stroke();
        end_path();
        break;
      case kCloseStroke:
        close_path();
        stroke();
        end_path();
        break;
      case kFill:
      case kFillObsolete:
        fill(graphics::FillRule::kNonZero);
        end_path();
        break;
      case kFillEvenOdd:
        fill(graphics::FillRule::kEvenOdd);
        end_path();
        break;
      case kFillStroke:
        fill(graphics::FillRule::kNonZero);
        stroke();
        end_path();
        break;
      case kFillStrokeEvenOdd:
        fill(graphics::FillRule::kEvenOdd);
        stroke();
        end_path();
        break;
      case kCloseFillStroke:
        close_path();
        fill(graphics::FillRule::kNonZero);
        stroke();
        end_path();
        break;
      case kCloseFillStrokeEvenOdd:
        close_path();
        fill(graphics::FillRule::kEvenOdd);
        stroke();
        end_path();
        break;
      case kEndPath:
        end_path();
        break;
      // Clipping
      case kClip:
        pending_clip_ = graphics::FillRule::kNonZero;
        break;
      case kClipEvenOdd:
        pending_clip_ = graphics::FillRule::kEvenOdd;
        break;
      // Text objects
      case kBeginText:
        in_text_ = true;
        text_matrix_ = line_matrix_ = Matrix{};
        break;
      case kEndText:
        in_text_ = false;
        break;
      // Text state
      case kSetCharSpacing:
        if (const auto v = numbers(op, 1)) {
          state_.char_spacing = (*v)[0];
        }
        break;
      case kSetWordSpacing:
        if (const auto v = numbers(op, 1)) {
          state_.word_spacing = (*v)[0];
        }
        break;
      case kSetHorizontalScaling:
        if (const auto v = numbers(op, 1)) {
          state_.scale = (*v)[0] / 100;
        }
        break;
      case kSetLeading:
        if (const auto v = numbers(op, 1)) {
          state_.leading = (*v)[0];
        }
        break;
      case kSetFont:
        set_font(op);
        break;
      case kSetTextRender:
        if (const auto v = numbers(op, 1)) {
          state_.render_mode = static_cast<std::int32_t>(std::clamp((*v)[0], 0.0, 7.0));
        }
        break;
      case kSetTextRise:
        if (const auto v = numbers(op, 1)) {
          state_.rise = (*v)[0];
        }
        break;
      // Text positioning
      case kMoveText:
        if (const auto v = numbers(op, 2)) {
          next_line((*v)[0], (*v)[1]);
        }
        break;
      case kMoveTextSetLeading:
        if (const auto v = numbers(op, 2)) {
          state_.leading = -(*v)[1];
          next_line((*v)[0], (*v)[1]);
        }
        break;
      case kSetTextMatrix:
        if (const auto v = numbers(op, 6)) {
          text_matrix_ = line_matrix_ = Matrix{(*v)[0], (*v)[1], (*v)[2], (*v)[3], (*v)[4], (*v)[5]};
        }
        break;
      case kNextLine:
        next_line(0, -state_.leading);
        break;
      // Text showing
      case kShowText:
      case kShowTextArray:
        if (!op.operands.empty()) {
          show(op.operands.back());
        }
        break;
      case kNextLineShowText:
        next_line(0, -state_.leading);
        if (!op.operands.empty()) {
          show(op.operands.back());
        }
        break;
      case kNextLineSpacingShowText:  // aw ac string " (§9.4.3)
        if (op.operands.size() >= 3) {
          const std::size_t n = op.operands.size();
          state_.word_spacing = number(op.operands[n - 3]).value_or(state_.word_spacing);
          state_.char_spacing = number(op.operands[n - 2]).value_or(state_.char_spacing);
          next_line(0, -state_.leading);
          show(op.operands.back());
        }
        break;
      // Type 3 glyph descriptions: meaningful only inside a glyph procedure.
      case kSetCharWidth:
      case kSetCacheDevice:
        break;
      // Colour
      case kSetStrokeColorSpace:
        set_color_space(op, false);
        break;
      case kSetFillColorSpace:
        set_color_space(op, true);
        break;
      case kSetStrokeColor:
      case kSetStrokeColorN:
        set_color(op, false);
        break;
      case kSetFillColor:
      case kSetFillColorN:
        set_color(op, true);
        break;
      case kSetStrokeGray:
        set_device_color(op, false, graphics::ColorSpaceFamily::kGray, 1);
        break;
      case kSetFillGray:
        set_device_color(op, true, graphics::ColorSpaceFamily::kGray, 1);
        break;
      case kSetStrokeRGB:
        set_device_color(op, false, graphics::ColorSpaceFamily::kRGB, 3);
        break;
      case kSetFillRGB:
        set_device_color(op, true, graphics::ColorSpaceFamily::kRGB, 3);
        break;
      case kSetStrokeCMYK:
        set_device_color(op, false, graphics::ColorSpaceFamily::kCMYK, 4);
        break;
      case kSetFillCMYK:
        set_device_color(op, true, graphics::ColorSpaceFamily::kCMYK, 4);
        break;
      // Shading, images, XObjects
      case kShadingFill:
        paint_shading(op);
        break;
      case kInlineImage:
        draw_inline_image(op);
        break;
      case kDoXObject:
        draw_xobject(op, form_depth);
        break;
      // Marked content: structure, not drawing.
      case kMarkPoint:
      case kMarkPointProperties:
      case kBeginMarkedContent:
      case kBeginMarkedContentProperties:
      case kEndMarkedContent:
        break;
      case kBeginCompatibility:
        ++compatibility_;
        break;
      case kEndCompatibility:
        compatibility_ = compatibility_ > 0 ? compatibility_ - 1 : 0;
        break;
      case kUnknown:
        if (compatibility_ == 0) {
          warnings_.add("unknown operator " + op.name + ": ignored");
        }
        break;
    }
  }

  Document* document_;
  const InterpreterLimits* limits_;
  graphics::DisplayList* out_;
  Warnings warnings_;
  const model::Dict* resources_ = nullptr;
  GraphicsState state_;
  std::vector<GraphicsState> stack_;
  std::size_t ignored_saves_ = 0;
  graphics::Path path_;
  Point current_point_;
  Point subpath_start_;
  bool has_current_point_ = false;
  std::optional<graphics::FillRule> pending_clip_;
  bool in_text_ = false;
  Matrix text_matrix_;
  Matrix line_matrix_;
  std::size_t compatibility_ = 0;
  std::set<std::uint32_t> active_forms_;
  std::optional<graphics::ColorIndex> last_fill_;
  std::optional<graphics::ColorIndex> last_stroke_;
  std::optional<graphics::StyleIndex> last_style_;
  std::size_t operations_ = 0;
  bool stopped_ = false;
};

// An inheritable page attribute (§7.7.3.4): on the page or the nearest ancestor that has it.
std::optional<Object> inherited(Document& document, model::ObjectRef page, std::string_view key) {
  constexpr std::size_t kMaxChain = 256;
  std::set<std::uint32_t> seen;
  std::optional<model::ObjectRef> node = page;
  for (std::size_t depth = 0; node && depth < kMaxChain && seen.insert(node->number).second; ++depth) {
    auto object = document.get(*node);
    const model::Dict* dict = object ? (*object)->as_dict() : nullptr;
    if (dict == nullptr) {
      return std::nullopt;
    }
    if (const Object* value = dict->find(key)) {
      auto resolved = document.resolve(*value);
      return resolved ? std::optional<Object>(std::move(*resolved)) : std::nullopt;
    }
    const Object* parent = dict->find("Parent");
    node = parent != nullptr ? parent->as_ref() : std::nullopt;
  }
  return std::nullopt;
}

std::optional<graphics::Rect> rect_of(const std::optional<Object>& object) {
  const model::Array* array = object ? object->as_array() : nullptr;
  if (array == nullptr || array->size() != 4) {
    return std::nullopt;
  }
  std::array<double, 4> v{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto value = number((*array)[i]);
    if (!value) {
      return std::nullopt;
    }
    v[i] = *value;
  }
  return graphics::Rect{v[0], v[1], v[2], v[3]}.normalized();
}

}  // namespace

Result<graphics::DisplayList> interpret_page(Document& document, model::ObjectRef page, const InterpreterLimits& limits) {
  auto node = document.get(page);
  if (!node) {
    return std::unexpected(std::move(node.error()));
  }
  if ((*node)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "page " + std::to_string(page.number) + " is not a dictionary");
  }

  graphics::DisplayList list;
  // The boxes (§14.11.2): MediaBox required (US Letter when missing or broken, as readers do), CropBox
  // defaulting to it and clipped by it.
  list.media_box = rect_of(inherited(document, page, "MediaBox")).value_or(graphics::Rect{0, 0, 612, 792});
  const graphics::Rect crop = rect_of(inherited(document, page, "CropBox")).value_or(list.media_box);
  list.crop_box = {std::max(crop.x0, list.media_box.x0), std::max(crop.y0, list.media_box.y0), std::min(crop.x1, list.media_box.x1),
                   std::min(crop.y1, list.media_box.y1)};
  if (list.crop_box.width() <= 0 || list.crop_box.height() <= 0) {
    list.crop_box = list.media_box;
  }
  if (const auto rotate = inherited(document, page, "Rotate")) {
    const std::int64_t degrees = rotate->as_integer().value_or(0);
    list.rotate = static_cast<std::int32_t>((((degrees % 360) + 360) % 360) / 90 * 90);
  }

  auto content = page_content(document, page);
  if (!content) {
    list.warnings.push_back("page content unreadable: " + content.error().message);
    return list;
  }
  const auto resources = inherited(document, page, "Resources");
  Interpreter interpreter(document, limits, list);
  interpreter.run(*content, resources && resources->as_dict() != nullptr ? resources->as_dict() : nullptr, 0);
  auto found = interpreter.take_warnings();
  list.warnings.insert(list.warnings.end(), found.begin(), found.end());
  return list;
}

}  // namespace capriole::content
