#include "capriole/content/interpreter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capriole/content/appearance.hpp"
#include "capriole/content/content_parser.hpp"
#include "capriole/content/function.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/filters/decode.hpp"
#include "capriole/model/text_string.hpp"
#include "color_space.hpp"
#include "font/font.hpp"
#include "image.hpp"
#include "shading.hpp"

namespace capriole::content {

namespace {

using detail::ColorSpace;
using detail::Warnings;
using font::detail::Font;
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
  double fill_alpha = 1;                                     // ca
  double stroke_alpha = 1;                                   // CA
  graphics::BlendMode blend = graphics::BlendMode::kNormal;  // BM
  // Text state (§9.3), part of the graphics state.
  double char_spacing = 0;  // Tc
  double word_spacing = 0;  // Tw
  double scale = 1;         // Tz / 100
  double leading = 0;       // TL
  double rise = 0;          // Ts
  std::int32_t render_mode = 0;
  std::string font_name;
  std::optional<model::ObjectRef> font_ref;
  std::shared_ptr<Font> font;
  double font_size = 0;
};

class Interpreter {
 public:
  Interpreter(Document& document, const InterpreterLimits& limits, const font::Resources* resources, graphics::DisplayList& out, Purpose purpose)
      : document_(&document), limits_(&limits), out_(&out), fonts_(resources), colors_(document, resources), text_(purpose == Purpose::kText) {}

  // stream: the object the content comes from, for the provenance of text (none for the page's).
  void run(std::span<const std::byte> content, const model::Dict* resources, std::size_t form_depth, std::optional<model::ObjectRef> stream = std::nullopt) {
    ContentParser parser(content, limits_->parse, parser::ParseMode::kTolerant);
    const model::Dict* saved_resources = resources_;
    resources_ = resources;
    const auto saved_stream = std::exchange(stream_, stream);
    const std::uint64_t saved_offset = op_offset_;
    const std::uint32_t saved_index = op_index_;
    const std::size_t marked_depth = marked_.size();
    std::uint32_t index = 0;
    // Pattern space is relative to the CTM at the start of the content stream that uses the pattern
    // (§8.7.3.1): the page's, a form's (its /Matrix included), a glyph's.
    const Matrix saved_base = std::exchange(base_ctm_, state_.ctm);
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
      op_offset_ = (*operation)->offset;
      op_index_ = index++;
      execute(**operation, form_depth);
    }
    if (parser.recovered_errors() > 0) {
      warnings_.add("syntax errors skipped in a content stream");
    }
    // A content stream leaves the graphics state as it found it (§7.8.2): close unbalanced q.
    while (stack_.size() > base_depth) {
      restore();
    }
    base_ctm_ = saved_base;
    resources_ = saved_resources;
    // Marked content does not continue past the end of its stream (§14.6).
    marked_.resize(std::min(marked_.size(), marked_depth));
    stream_ = saved_stream;
    op_offset_ = saved_offset;
    op_index_ = saved_index;
  }

  std::vector<std::string> take_warnings() { return warnings_.take(); }
  [[nodiscard]] std::vector<std::string> missing_resources() const {
    std::set<std::string> all;
    for (std::string& name : fonts_.missing()) {
      all.insert(std::move(name));
    }
    for (std::string& name : colors_.missing()) {
      all.insert(std::move(name));
    }
    return {all.begin(), all.end()};
  }

  // The page's annotations (§12.5), over its content: each one's normal appearance (/AP /N, the one /AS
  // names when there are several), placed on its /Rect as §12.5.5 says and with its opacity /CA; for the
  // markup annotations that come without one, an appearance made here from their dictionary (§12.5.6).
  // Hidden and NoView annotations, and popups (a viewer's windows), are not drawn.
  void draw_annotations(const Object& annots, const model::Dict* page_resources) {
    // Annotations are drawn after the page's content, which they are not part of.
    in_annotations_ = true;
    static_cast<void>(enter(graphics::Invocation::Kind::kOther, std::nullopt));
    auto list = document_->resolve(annots);
    if (!list || list->as_array() == nullptr) {
      return;
    }
    constexpr std::size_t kMaxAnnotations = 100'000;
    std::size_t drawn = 0;
    for (const Object& entry : *list->as_array()) {
      if (stopped_ || drawn++ >= kMaxAnnotations) {
        break;
      }
      auto annotation = document_->resolve(entry);
      if (annotation && annotation->as_dict() != nullptr) {
        resources_ = page_resources;
        draw_annotation(*annotation->as_dict());
      }
    }
  }

 private:
  // Colours and stroke styles go into the list's tables once; a command refers to them by index. The
  // last index of each kind is remembered: consecutive commands almost always repeat it.
  // Two spaces of a family (two spot colours, two ICC profiles) can give the same components different
  // colours: the colour itself is compared too.
  static bool same(const Color& a, const Color& b) { return a.family == b.family && a.components == b.components && a.pattern == b.pattern && a.rgb == b.rgb; }
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
    out_->origins.push_back(graphics::Origin{.invocation = invocation_, .operation = op_index_});
  }

  // Runs what follows as a new invocation (graphics::Invocation): the content of a form drawn by the
  // current operator (kForm), or another. Returns the invocation to go back to.
  std::uint32_t enter(graphics::Invocation::Kind kind, std::optional<model::ObjectRef> form) {
    const std::uint32_t parent = invocation_;
    invocation_ = static_cast<std::uint32_t>(out_->invocations.size());
    out_->invocations.push_back(graphics::Invocation{.kind = kind, .parent = parent, .form = form, .operation = op_index_, .ctm = {}});
    return parent;
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
    if (!path_.empty() && state_.fill.family == graphics::ColorSpaceFamily::kPattern) {
      fill_with_pattern(path_, state_.ctm, rule, state_.fill, state_.fill_space, state_.fill_alpha);
      return;
    }
    if (!path_.empty()) {
      emit(graphics::FillPath{
          .path = path_, .ctm = state_.ctm, .rule = rule, .blend = state_.blend, .color = intern(state_.fill, last_fill_), .alpha = state_.fill_alpha});
    }
  }

  void stroke() {
    if (!path_.empty() && state_.stroke.family == graphics::ColorSpaceFamily::kPattern) {
      warnings_.add("strokes with a pattern are drawn in a plain colour");
    }
    if (!path_.empty()) {
      emit(graphics::StrokePath{.path = path_,
                                .ctm = state_.ctm,
                                .style = intern(state_.stroke_style),
                                .color = intern(state_.stroke, last_stroke_),
                                .alpha = state_.stroke_alpha,
                                .blend = state_.blend});
    }
  }

  void close_path() {
    if (has_current_point_) {
      path_.close();
      current_point_ = subpath_start_;
    }
  }

  void set_color(const Operation& op, bool for_fill) {
    if (uncolored_glyph_) {
      return;
    }
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
    if (uncolored_glyph_) {
      return;
    }
    const auto values = numbers(op, count);
    if (!values) {
      return;
    }
    ColorSpace space;
    space.family = family;
    space.components = count;
    if (family == graphics::ColorSpaceFamily::kCMYK) {
      space = detail::device_cmyk(&colors_);  // k and K: DeviceCMYK, through the default CMYK profile
    }
    (for_fill ? state_.fill_space : state_.stroke_space) = space;
    (for_fill ? state_.fill : state_.stroke) = detail::make_color(space, *values, {}, warnings_);
  }

  void set_color_space(const Operation& op, bool for_fill) {
    if (uncolored_glyph_) {
      return;
    }
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    ColorSpace space = detail::resolve_color_space(*document_, op.operands.back(), resources_, warnings_, &colors_);
    (for_fill ? state_.fill : state_.stroke) = detail::initial_color(space);
    (for_fill ? state_.fill_space : state_.stroke_space) = std::move(space);
  }

  void set_ext_gstate(const Operation& op, std::size_t form_depth) {
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
        state_.font = load_font(font);
        state_.font_size = number((*value->as_array())[1]).value_or(0);
      } else if (key == "BM") {
        set_blend_mode(*value);
      } else if (key == "SMask") {
        set_soft_mask(*value, form_depth);
      }
    }
  }

  // /BM: a name, or an array of names of which the first known one counts (§11.6.3).
  void set_blend_mode(const Object& value) {
    const model::Array* names = value.as_array();
    const std::span<const Object> candidates = names != nullptr ? std::span<const Object>(*names) : std::span<const Object>(&value, 1);
    for (const Object& candidate : candidates) {
      const model::Name* name = candidate.as_name();
      if (const auto mode = name != nullptr ? graphics::blend_mode_named(name->bytes) : std::nullopt) {
        state_.blend = *mode;
        return;
      }
    }
    warnings_.add("unknown blend mode: Normal used");
    state_.blend = graphics::BlendMode::kNormal;
  }

  // /SMask (§11.6.5.2): /None, or a mask dictionary whose group /G is drawn, with the CTM in force now
  // and otherwise the initial graphics state, into the mask the renderer applies from here on.
  void set_soft_mask(const Object& value, std::size_t form_depth) {
    if (value.is_name("None")) {
      emit(graphics::ClearSoftMask{});
      return;
    }
    const model::Dict* mask = value.as_dict();
    const Object* subtype = mask != nullptr ? mask->find("S") : nullptr;
    const Object* group = mask != nullptr ? mask->find("G") : nullptr;
    if (subtype == nullptr || group == nullptr || (!subtype->is_name("Alpha") && !subtype->is_name("Luminosity"))) {
      warnings_.add("soft mask without a valid /S and /G: ignored");
      return;
    }
    auto form = document_->resolve(*group);
    const model::Stream* stream = form ? form->as_stream() : nullptr;
    if (stream == nullptr) {
      warnings_.add("soft mask group /G is not a stream: ignored");
      return;
    }
    graphics::BeginSoftMask begin{.kind = subtype->is_name("Luminosity") ? graphics::SoftMaskKind::kLuminosity : graphics::SoftMaskKind::kAlpha,
                                  .backdrop = graphics::Rgb{},
                                  .transfer = nullptr};
    if (const Object* bc = mask->find("BC"); bc != nullptr && begin.kind == graphics::SoftMaskKind::kLuminosity) {
      // The backdrop is in the group's colour space (/Group /CS); DeviceGray when it has none.
      auto components = document_->resolve(*bc);
      ColorSpace space;
      if (auto attributes = stream->dict.find("Group") != nullptr ? document_->resolve(*stream->dict.find("Group")) : Result<Object>(Object());
          attributes && attributes->as_dict() != nullptr && attributes->as_dict()->find("CS") != nullptr) {
        space = detail::resolve_color_space(*document_, *attributes->as_dict()->find("CS"), resources_, warnings_, &colors_);
      }
      std::vector<double> values;
      if (components && components->as_array() != nullptr) {
        for (const Object& component : *components->as_array()) {
          values.push_back(number(component).value_or(0));
        }
      }
      begin.backdrop = detail::make_color(space, values, {}, warnings_).rgb;
    }
    if (const Object* transfer = mask->find("TR"); transfer != nullptr && !transfer->is_name("Identity")) {
      // A function of one input and one output (§11.6.5.2), sampled once for every 8-bit mask value.
      if (auto function = load_function(*document_, *transfer); function && function->inputs() == 1 && function->outputs() >= 1) {
        auto table = std::make_shared<std::vector<std::uint8_t>>(256);
        for (std::size_t v = 0; v < table->size(); ++v) {
          const double in = static_cast<double>(v) / 255;
          const double out = function->evaluate(std::span<const double>(&in, 1))[0];
          (*table)[v] = static_cast<std::uint8_t>(std::lround(std::clamp(out, 0.0, 1.0) * 255));
        }
        begin.transfer = std::move(table);
      } else {
        warnings_.add("soft mask transfer function unusable: identity used");
      }
    }
    emit(begin);
    save();
    const Matrix ctm = state_.ctm;
    state_ = GraphicsState{};
    state_.ctm = ctm;
    draw_form(group->as_ref(), *stream, form_depth, FormRole::kSoftMask);
    restore();
    emit(graphics::EndSoftMask{});
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

  // A font resource, loaded once per page when it is an indirect object (the usual case).
  std::shared_ptr<Font> load_font(const Object& entry) {
    const auto ref = entry.as_ref();
    if (ref) {
      if (const auto found = font_cache_.find(ref->number); found != font_cache_.end()) {
        return found->second;
      }
    }
    std::vector<std::string> problems;
    auto font = Font::load(*document_, entry, fonts_, problems);
    for (std::string& problem : problems) {
      warnings_.add(std::move(problem));
    }
    if (ref) {
      font_cache_.emplace(ref->number, font);
    }
    return font;
  }

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
      warnings_.add("font /" + name + " not found in the resources: a substitute is used");
      state_.font = load_font(Object());
      state_.font_ref.reset();
      return;
    }
    state_.font_ref = entry->as_ref();
    state_.font = load_font(*entry);
  }

  // Moves to the start of the next line, offset by (tx, ty) from the current one (Td).
  void next_line(double tx, double ty) {
    line_matrix_ = Matrix::translate(tx, ty).then(line_matrix_);
    text_matrix_ = line_matrix_;
  }

  // BMC and BDC (§14.6): the properties (inline, or named in /Properties) may carry /ActualText, the
  // text the sequence stands for (§14.9.4).
  void begin_marked(const Object* properties) {
    constexpr std::size_t kMaxMarkedDepth = 256;
    if (marked_.size() >= kMaxMarkedDepth) {
      return;  // unbalanced by design: EMC then pops an outer one, which only loses an /ActualText
    }
    std::int32_t index = -1;
    std::optional<Object> named;
    if (properties != nullptr && properties->as_name() != nullptr) {
      named = resource("Properties", properties->as_name()->bytes);
      properties = named ? &*named : nullptr;
    }
    if (properties != nullptr) {
      auto dict = document_->resolve(*properties);
      const model::Dict* d = dict ? dict->as_dict() : nullptr;
      if (d != nullptr) {
        if (const Object* actual = d->find("ActualText")) {
          if (auto text = document_->resolve(*actual); text && text->as_string() != nullptr) {
            constexpr std::size_t kMaxActualTexts = 100'000;
            if (out_->actual_texts.size() < kMaxActualTexts) {
              index = static_cast<std::int32_t>(out_->actual_texts.size());
              out_->actual_texts.push_back(model::decode_text_string(text->as_string()->bytes));
            }
          }
        }
      }
    }
    marked_.push_back(index);
  }

  // Tj, TJ, ' and ": a run of glyphs, advancing the text matrix (§9.4.4).
  void show(const Object& operand, std::size_t form_depth) {
    if (!in_text_) {
      warnings_.add("text shown outside BT/ET: drawn anyway");
    }
    if (!state_.font) {
      warnings_.add("text shown before any Tf: a substitute is used");
      state_.font = load_font(Object());
    }
    Font& font = *state_.font;
    graphics::ShowText run;
    run.text_matrix = text_matrix_.then(state_.ctm);
    run.tm = text_matrix_;
    run.ctm = state_.ctm;
    run.font = state_.font_name;
    run.font_ref = state_.font_ref;
    run.font_size = state_.font_size;
    run.horizontal_scale = state_.scale;
    run.render_mode = state_.render_mode;
    run.fill = intern(state_.fill, last_fill_);
    run.stroke = intern(state_.stroke, last_stroke_);
    run.stroke_style = intern(state_.stroke_style);
    run.alpha = state_.fill_alpha;
    run.stroke_alpha = state_.stroke_alpha;
    run.blend = state_.blend;
    run.widths_known = font.widths_known();
    run.latin_codes = font.latin_codes();
    run.glyph_procedures = font.kind() == Font::Kind::kType3;
    run.ascent = font.ascent();
    run.descent = font.descent();
    run.vertical = font.vertical();
    run.stream = stream_;
    run.offset = op_offset_;
    run.operation = op_index_;
    for (const std::int32_t actual : std::views::reverse(marked_)) {
      if (actual >= 0) {
        run.actual_text = actual;  // the innermost /ActualText
        break;
      }
    }

    // Horizontal: tx = ((w0 · Tfs) + Tc + Tw) · Th. Vertical: ty = (w1 · Tfs) + Tc + Tw (§9.4.4); Tw for
    // the single-byte code 32 only.
    const bool vertical = font.vertical();
    const bool outlines = font.kind() != Font::Kind::kType3;
    double x = 0;
    double y = 0;
    std::vector<Font::Code> codes;
    const auto add_string = [&](const std::string& bytes) {
      const auto start = static_cast<std::uint32_t>(run.bytes.size());
      run.bytes += bytes;
      codes.clear();
      font.codes(bytes, codes);
      std::uint32_t at = start;
      for (const Font::Code& code : codes) {
        const double width =
            vertical ? font.vertical_metrics(code).advance * 0.001 * state_.font_size : font.width(code) * font.scale() * state_.font_size * state_.scale;
        graphics::Glyph glyph{.code = code.code,
                              .cid = code.cid,
                              .origin = {x, y + state_.rise},
                              .advance = 0,
                              .width = width,
                              .byte = at,
                              .text = text_ ? font.text(code) : std::u32string(),
                              .outline = nullptr};
        at += code.length;
        const double spacing = state_.char_spacing + (Font::is_word_space(code) ? state_.word_spacing : 0);
        if (vertical) {
          glyph.advance = (font.vertical_metrics(code).advance * 0.001 * state_.font_size) + spacing;
          y += glyph.advance;
        } else {
          glyph.advance = ((font.width(code) * font.scale() * state_.font_size) + spacing) * state_.scale;
          x += glyph.advance;
        }
        if (outlines) {
          glyph.outline = font.outline(code);
        }
        run.glyphs.push_back(std::move(glyph));
      }
    };
    if (const model::String* string = operand.as_string()) {
      add_string(string->bytes);
    } else if (const model::Array* array = operand.as_array()) {
      for (const Object& element : *array) {
        if (const model::String* part = element.as_string()) {
          add_string(part->bytes);
        } else if (const auto adjust = number(element)) {
          // TJ numbers move back along the writing direction (§9.4.3).
          if (vertical) {
            y -= (*adjust / 1000) * state_.font_size;
          } else {
            x -= (*adjust / 1000) * state_.font_size * state_.scale;
          }
        }
      }
    } else {
      warnings_.add("text operator without a string: skipped");
      return;
    }
    text_matrix_ = Matrix::translate(x, y).then(text_matrix_);
    if (run.glyphs.empty()) {
      return;
    }
    // Filled with a pattern: the glyphs are the area the pattern paints; the run keeps only its stroke.
    const bool fills = state_.render_mode == 0 || state_.render_mode == 2 || state_.render_mode == 4 || state_.render_mode == 6;
    if (fills && !run.glyph_procedures && state_.fill.family == graphics::ColorSpaceFamily::kPattern) {
      if (const graphics::Path area = graphics::glyph_run_path(run); !area.empty()) {
        fill_with_pattern(area, state_.ctm, graphics::FillRule::kNonZero, state_.fill, state_.fill_space, state_.fill_alpha);
      }
      constexpr std::array<std::int32_t, 8> kWithoutFill = {3, 1, 1, 3, 7, 5, 5, 7};
      run.render_mode = kWithoutFill.at(static_cast<std::size_t>(state_.render_mode));
    }
    // Modes 4–7 add the glyphs to the clip that ET sets (§9.3.6).
    if (state_.render_mode >= 4) {
      append_transformed(text_clip_, graphics::glyph_run_path(run), state_.ctm);
    }
    if (run.glyph_procedures && state_.render_mode != 3 && state_.render_mode != 7) {
      const std::vector<graphics::Glyph> glyphs = run.glyphs;
      const Matrix placement = run.text_matrix;
      emit(graphics::Boxed<graphics::ShowText>(std::move(run)));
      draw_type3_glyphs(font, glyphs, placement, form_depth);
      return;
    }
    emit(graphics::Boxed<graphics::ShowText>(std::move(run)));
  }

  static void append_transformed(graphics::Path& into, const graphics::Path& path, const Matrix& m) {
    path.for_each([&](graphics::SegmentKind kind, const Point* p) {
      switch (kind) {
        case graphics::SegmentKind::kMove:
          into.move_to(m.apply(p[0]));
          break;
        case graphics::SegmentKind::kLine:
          into.line_to(m.apply(p[0]));
          break;
        case graphics::SegmentKind::kCubic:
          into.cubic_to(m.apply(p[0]), m.apply(p[1]), m.apply(p[2]));
          break;
        case graphics::SegmentKind::kClose:
          into.close();
          break;
      }
    });
  }

  // ET: the clip of the glyphs shown in modes 4–7, if any (§9.3.6).
  void end_text() {
    in_text_ = false;
    if (!text_clip_.empty()) {
      emit(graphics::ClipPath{.path = std::exchange(text_clip_, {}), .ctm = Matrix{}, .rule = graphics::FillRule::kNonZero});
    }
  }

  // Type 3 glyphs (§9.6.4): each glyph's procedure runs like a form, with the glyph matrix — FontMatrix
  // × the glyph's place in the run — as its CTM, and the font's resources (the page's when it has none).
  void draw_type3_glyphs(const Font& font, const std::vector<graphics::Glyph>& glyphs, const Matrix& placement, std::size_t form_depth) {
    if (form_depth + 1 >= limits_->max_form_depth) {
      warnings_.add("Type 3 glyphs nested too deeply: skipped");
      return;
    }
    const model::Dict* resources = font.type3_resources() != nullptr ? font.type3_resources() : resources_;
    const double size = state_.font_size;
    const double h = state_.scale;
    for (const graphics::Glyph& glyph : glyphs) {
      const Object* proc = font.char_proc(glyph.code);
      if (proc == nullptr || stopped_) {
        continue;
      }
      const auto ref = proc->as_ref();
      auto object = document_->resolve(*proc);
      const model::Stream* stream = object ? object->as_stream() : nullptr;
      if (stream == nullptr || (ref && active_forms_.contains(ref->number))) {
        continue;
      }
      auto content = document_->decode_stream(*stream);
      if (!content) {
        warnings_.add("Type 3 glyph procedure unreadable: " + content.error().message);
        continue;
      }
      if (ref) {
        active_forms_.insert(ref->number);
      }
      save();
      state_.ctm = font.font_matrix().then(Matrix{size * h, 0, 0, size, glyph.origin.x, glyph.origin.y}).then(placement);
      const graphics::Path saved_path = std::exchange(path_, {});
      const bool saved_in_text = std::exchange(in_text_, false);
      const Matrix saved_text = text_matrix_;
      const Matrix saved_line = line_matrix_;
      graphics::Path saved_clip = std::exchange(text_clip_, {});
      const bool saved_uncolored = std::exchange(uncolored_glyph_, cell_uncoloured_);
      ++type3_depth_;
      const std::uint32_t outer = enter(graphics::Invocation::Kind::kOther, ref);
      run(*content, resources, form_depth + 1, ref);
      invocation_ = outer;
      --type3_depth_;
      uncolored_glyph_ = saved_uncolored;
      text_clip_ = std::move(saved_clip);
      line_matrix_ = saved_line;
      text_matrix_ = saved_text;
      in_text_ = saved_in_text;
      path_ = saved_path;
      restore();
      if (ref) {
        active_forms_.erase(ref->number);
      }
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
      image.blend = state_.blend;
      image.interpolate = interpolates(stream->dict);
      image.pixels = xobject_pixels(image.xobject, *stream, image.mask);
      emit(graphics::Boxed<graphics::DrawImage>(std::move(image)));
      return;
    }
    if (subtype != nullptr && subtype->is_name("Form")) {
      draw_form(entry->as_ref(), *stream, form_depth);
      return;
    }
    warnings_.add("XObject /" + name->bytes + " of an unsupported kind: skipped");
  }

  // A form drawn by Do, or as the group of a soft mask (whose group is the mask itself, not a group
  // composited onto the page).
  enum class FormRole : std::uint8_t { kDo, kSoftMask };

  void draw_form(std::optional<model::ObjectRef> ref, const model::Stream& form, std::size_t form_depth, FormRole role = FormRole::kDo) {
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
    const Matrix drawn_with = state_.ctm;  // the CTM of the Do
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
    // A transparency group (§11.6.6): composited as a whole with the alpha, blend mode and soft mask in
    // force now; inside, those start again from their initial values.
    bool group = false;
    if (const Object* entry = form.dict.find("Group"); entry != nullptr && role == FormRole::kDo) {
      auto attributes = document_->resolve(*entry);
      const model::Dict* dict = attributes ? attributes->as_dict() : nullptr;
      if (dict != nullptr && dict->find("S") != nullptr && dict->find("S")->is_name("Transparency")) {
        const auto flag = [&](std::string_view key) {
          const Object* value = dict->find(key);
          return value != nullptr && std::holds_alternative<bool>(value->value()) && std::get<bool>(value->value());
        };
        const graphics::BeginGroup begin{.alpha = state_.fill_alpha, .blend = state_.blend, .isolated = flag("I"), .knockout = flag("K")};
        if (begin.knockout) {
          warnings_.add("knockout groups are drawn as ordinary groups");
        }
        emit(begin);
        state_.fill_alpha = 1;
        state_.stroke_alpha = 1;
        state_.blend = graphics::BlendMode::kNormal;
        group = true;
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
    // A form drawn by a Do of page content is its own invocation; a soft mask's, or one drawn for an
    // annotation, is not content the page's edits rewrite.
    const bool content_form = role == FormRole::kDo && !in_annotations_;
    const std::uint32_t outer = enter(content_form ? graphics::Invocation::Kind::kForm : graphics::Invocation::Kind::kOther, ref);
    out_->invocations.back().ctm = drawn_with;
    run(*content, resources, form_depth + 1, ref);
    invocation_ = outer;
    path_ = saved_path;
    in_text_ = saved_in_text;
    if (group) {
      emit(graphics::EndGroup{});
    }
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
    image.blend = state_.blend;
    const model::Dict dict = detail::expand_inline_dict(op.image->dict);
    image.interpolate = interpolates(dict);
    if (text_) {
      // Extracting text: the image is not looked at.
    } else if (auto data = filters::decode(op.image->data, dict, limits_->parse); data) {
      image.pixels = pixels_of(dict, *data);
    } else {
      warnings_.add("inline image not decoded: " + data.error().message);
    }
    emit(graphics::Boxed<graphics::DrawImage>(std::move(image)));
  }

  // /Interpolate is a hint; left out, an image is smoothed when drawn smaller only.
  static bool interpolates(const model::Dict& dict) {
    const Object* value = dict.find("Interpolate");
    return value != nullptr && value->as_bool().value_or(false);
  }

  // The pixels of an image XObject, decoded once per page (stencil masks carry the fill colour, so
  // they are decoded each time).
  std::shared_ptr<const graphics::ImagePixels> xobject_pixels(std::optional<model::ObjectRef> ref, const model::Stream& stream, bool stencil) {
    if (text_) {
      return nullptr;  // extracting text: the image is not looked at
    }
    const bool cached = ref.has_value() && !stencil;
    if (cached) {
      if (const auto found = images_.find(ref->number); found != images_.end()) {
        return found->second;
      }
    }
    std::shared_ptr<const graphics::ImagePixels> pixels;
    if (detail::is_jpx(*document_, stream.dict)) {
      pixels = jpx_pixels(stream);
    } else {
      auto data = document_->decode_stream(stream);
      if (!data) {
        warnings_.add("image not decoded: " + data.error().message);
        return nullptr;
      }
      pixels = pixels_of(stream.dict, *data);
    }
    if (cached) {
      images_.emplace(ref->number, pixels);
    }
    return pixels;
  }

  // JPEG 2000: from the stored bytes (decrypted, not filtered), with the page's image budget.
  std::shared_ptr<const graphics::ImagePixels> jpx_pixels(const model::Stream& stream) {
    detail::ImageLimits limits;
    limits.max_pixels = std::min(limits_->max_image_pixels, limits_->max_page_image_pixels - image_pixels_);
    constexpr std::int64_t kMinUsefulPixels = 65'536;
    if (limits.max_pixels < kMinUsefulPixels) {
      warnings_.add("too many image pixels on the page: the rest are placeholders");
      return nullptr;
    }
    auto data = document_->encoded_stream_data(stream);
    if (!data) {
      warnings_.add("image not decoded: " + data.error().message);
      return nullptr;
    }
    auto pixels = detail::decode_jpx_image(*document_, stream.dict, *data, resources_, warnings_, limits, &colors_);
    if (!pixels) {
      warnings_.add("image not decoded: " + pixels.error().message);
      return nullptr;
    }
    image_pixels_ += static_cast<std::int64_t>((*pixels)->width) * (*pixels)->height;
    return *pixels;
  }

  std::shared_ptr<const graphics::ImagePixels> pixels_of(const model::Dict& dict, const std::vector<std::byte>& data) {
    detail::ImageLimits limits;
    limits.max_pixels = std::min(limits_->max_image_pixels, limits_->max_page_image_pixels - image_pixels_);
    // Large images are read at a reduced size within the limit; once the page's budget is nearly spent,
    // placeholders.
    constexpr std::int64_t kMinUsefulPixels = 65'536;
    if (limits.max_pixels < kMinUsefulPixels) {
      warnings_.add("too many image pixels on the page: the rest are placeholders");
      return nullptr;
    }
    auto pixels = detail::decode_image(*document_, dict, data, resources_, state_.fill.rgb, warnings_, limits, &colors_);
    if (!pixels) {
      warnings_.add("image not decoded: " + pixels.error().message);
      return nullptr;
    }
    image_pixels_ += static_cast<std::int64_t>((*pixels)->width) * (*pixels)->height;
    return *pixels;
  }

  // The shading of a resource or pattern, prepared once per page (per object).
  std::shared_ptr<const graphics::Shading> shading_of(const Object& entry, const Matrix& ctm) {
    const auto ref = entry.as_ref();
    if (ref) {
      if (const auto found = shadings_.find(ref->number); found != shadings_.end()) {
        return found->second;
      }
    }
    auto shading = detail::load_shading(*document_, entry, resources_, std::sqrt(std::abs(ctm.determinant())), warnings_, {}, &colors_);
    if (ref) {
      shadings_.emplace(ref->number, shading);
    }
    return shading;
  }

  // sh (§8.7.4.2): the shading over the current clip, in the current user space.
  void paint_shading(const Operation& op) {
    const model::Name* name = last_name(op);
    if (name == nullptr) {
      return;
    }
    const auto entry = resource("Shading", name->bytes);
    if (!entry) {
      warnings_.add("shading /" + name->bytes + " not found");
      return;
    }
    if (auto shading = shading_of(*entry, state_.ctm)) {
      emit(graphics::Boxed<graphics::PaintShading>(
          graphics::PaintShading{.ctm = state_.ctm, .shading = std::move(shading), .alpha = state_.fill_alpha, .blend = state_.blend, .background = false}));
    }
  }

  // An area filled with a pattern colour (§8.7.3): the area becomes the clip, the pattern is painted
  // over it — a shading pattern's shading (with its background), or a tiling pattern's repeated cell.
  void fill_with_pattern(const graphics::Path& area, const Matrix& ctm, graphics::FillRule rule, const Color& color, const ColorSpace& space, double alpha) {
    const auto entry = resource("Pattern", color.pattern);
    auto pattern = entry ? document_->resolve(*entry) : Result<Object>(Object());
    const model::Dict* dict = nullptr;
    if (pattern) {
      dict = pattern->as_stream() != nullptr ? &pattern->as_stream()->dict : pattern->as_dict();
    }
    if (dict == nullptr) {
      warnings_.add("pattern /" + color.pattern + " not found: not painted");
      return;
    }
    Matrix matrix;
    if (const Object* m = dict->find("Matrix")) {
      if (auto values = document_->resolve(*m); values && values->as_array() != nullptr && values->as_array()->size() == 6) {
        std::array<double, 6> v{};
        for (std::size_t i = 0; i < v.size(); ++i) {
          v.at(i) = number((*values->as_array())[i]).value_or(0);
        }
        matrix = {v[0], v[1], v[2], v[3], v[4], v[5]};
      }
    }
    matrix = matrix.then(base_ctm_);  // pattern space → user space
    const Object* type = dict->find("PatternType");
    const std::int64_t pattern_type = type != nullptr ? type->as_integer().value_or(0) : 0;
    if (pattern_type == 2) {
      const Object* shading_entry = dict->find("Shading");
      auto shading = shading_entry != nullptr ? shading_of(*shading_entry, matrix) : nullptr;
      if (!shading) {
        return;
      }
      if (dict->contains("ExtGState")) {
        warnings_.add("the graphics state of shading patterns is not applied");
      }
      emit(graphics::Save{});
      emit(graphics::ClipPath{.path = area, .ctm = ctm, .rule = rule});
      emit(graphics::Boxed<graphics::PaintShading>(
          graphics::PaintShading{.ctm = matrix, .shading = std::move(shading), .alpha = alpha, .blend = state_.blend, .background = true}));
      emit(graphics::Restore{});
      return;
    }
    if (pattern_type == 1 && pattern->as_stream() != nullptr) {
      auto tiling = tiling_cell(entry ? entry->as_ref() : std::nullopt, *pattern->as_stream(), color, space);
      if (!tiling) {
        return;
      }
      tiling->matrix = matrix;
      tiling->alpha = alpha;
      tiling->blend = state_.blend;
      emit(graphics::Save{});
      emit(graphics::ClipPath{.path = area, .ctm = ctm, .rule = rule});
      emit(graphics::Boxed<graphics::PaintTiling>(std::move(*tiling)));
      emit(graphics::Restore{});
      return;
    }
    warnings_.add("pattern /" + color.pattern + " of an unknown type: not painted");
  }

  // A tiling pattern's cell (§8.7.3.2): its content run on its own, in pattern space, into a display list
  // of its own. Coloured cells are made once per page; an uncoloured one (PaintType 2) is painted in the
  // colour given with the pattern, its own colour operators ignored.
  std::optional<graphics::PaintTiling> tiling_cell(std::optional<model::ObjectRef> ref, const model::Stream& stream, const Color& color,
                                                   const ColorSpace& space) {
    const model::Dict& dict = stream.dict;
    const Object* paint_type = dict.find("PaintType");
    const bool uncoloured = paint_type != nullptr && paint_type->as_integer().value_or(1) == 2;
    std::vector<double> box;
    if (const Object* b = dict.find("BBox")) {
      if (auto values = document_->resolve(*b); values && values->as_array() != nullptr) {
        for (const Object& v : *values->as_array()) {
          box.push_back(number(v).value_or(0));
        }
      }
    }
    const auto step = [&](std::string_view key) {
      const Object* v = dict.find(key);
      auto resolved = v != nullptr ? document_->resolve(*v) : Result<Object>(Object());
      return resolved ? number(*resolved).value_or(0) : 0.0;
    };
    const double xstep = std::abs(step("XStep"));
    const double ystep = std::abs(step("YStep"));
    if (box.size() != 4 || xstep == 0 || ystep == 0 || !std::isfinite(xstep) || !std::isfinite(ystep)) {
      warnings_.add("tiling pattern without a valid /BBox, /XStep and /YStep: not painted");
      return std::nullopt;
    }
    const graphics::Rect bbox = graphics::Rect{box[0], box[1], box[2], box[3]}.normalized();
    graphics::PaintTiling tiling{
        .matrix = {}, .cell = nullptr, .bbox = bbox, .xstep = xstep, .ystep = ystep, .alpha = 1, .blend = graphics::BlendMode::kNormal};

    const bool cacheable = ref.has_value() && !uncoloured;
    if (cacheable) {
      if (const auto found = tiles_.find(ref->number); found != tiles_.end()) {
        tiling.cell = found->second;
        return tiling;
      }
    }
    if (tile_depth_ + 1 >= limits_->max_form_depth || (ref && active_forms_.contains(ref->number))) {
      warnings_.add("tiling patterns nested too deeply: not painted");
      return std::nullopt;
    }
    auto content = document_->decode_stream(stream);
    if (!content) {
      warnings_.add("tiling pattern content unreadable: " + content.error().message);
      return std::nullopt;
    }
    std::optional<Object> own;
    const model::Dict* resources = resources_;
    if (const Object* r = dict.find("Resources")) {
      if (auto resolved = document_->resolve(*r); resolved && resolved->as_dict() != nullptr) {
        own = std::move(*resolved);
        resources = own->as_dict();
      }
    }
    auto cell = std::make_shared<graphics::DisplayList>();
    cell->media_box = bbox;
    cell->crop_box = bbox;
    // The cell is clipped to its bounding box (§8.7.3.2).
    graphics::Path clip;
    clip.move_to({bbox.x0, bbox.y0});
    clip.line_to({bbox.x1, bbox.y0});
    clip.line_to({bbox.x1, bbox.y1});
    clip.line_to({bbox.x0, bbox.y1});
    clip.close();
    cell->commands.emplace_back(graphics::ClipPath{.path = std::move(clip), .ctm = {}, .rule = graphics::FillRule::kNonZero});
    cell->origins.emplace_back();
    InterpreterLimits limits = *limits_;
    limits.max_operations = limits_->max_operations > operations_ ? limits_->max_operations - operations_ : 0;
    Interpreter inner(*document_, limits, fonts_.resources(), *cell, text_ ? Purpose::kText : Purpose::kDraw);
    inner.tile_depth_ = tile_depth_ + 1;
    inner.active_forms_ = active_forms_;
    if (ref) {
      inner.active_forms_.insert(ref->number);
    }
    if (uncoloured) {
      // The colour comes with the pattern, in its underlying space (§8.7.3.3).
      const Color paint = space.base ? detail::make_color(*space.base, color.components, {}, warnings_)
                                     : Color{.family = graphics::ColorSpaceFamily::kGray, .components = {0}, .pattern = {}, .rgb = {}};
      inner.state_.fill = paint;
      inner.state_.stroke = paint;
      inner.uncolored_glyph_ = true;
      inner.cell_uncoloured_ = true;
    }
    inner.run(*content, resources, 0, ref);
    operations_ += inner.operations_;
    for (std::string& warning : inner.take_warnings()) {
      warnings_.add(std::move(warning));
    }
    tiling.cell = cell;
    if (cacheable) {
      tiles_.emplace(ref->number, cell);
    }
    return tiling;
  }

  // ── Annotations ────────────────────────────────────────────────────────────────────────

  std::vector<double> numbers_of(const model::Dict& dict, std::string_view key) {
    std::vector<double> out;
    const Object* entry = dict.find(key);
    auto value = entry != nullptr ? document_->resolve(*entry) : Result<Object>(Object());
    if (value && value->as_array() != nullptr) {
      for (const Object& item : *value->as_array()) {
        auto resolved = document_->resolve(item);
        out.push_back(resolved ? number(*resolved).value_or(0) : 0);
      }
    }
    return out;
  }

  void draw_annotation(const model::Dict& annotation) {
    const Object* subtype_entry = annotation.find("Subtype");
    const std::string subtype = subtype_entry != nullptr && subtype_entry->as_name() != nullptr ? subtype_entry->as_name()->bytes : std::string();
    const Object* flags_entry = annotation.find("F");
    const std::int64_t flags = flags_entry != nullptr ? flags_entry->as_integer().value_or(0) : 0;
    constexpr std::int64_t kHidden = 1 << 1;
    constexpr std::int64_t kNoView = 1 << 5;
    if ((flags & (kHidden | kNoView)) != 0 || subtype == "Popup") {
      return;
    }
    const std::vector<double> r = numbers_of(annotation, "Rect");
    if (r.size() != 4) {
      return;
    }
    const graphics::Rect rect = graphics::Rect{r[0], r[1], r[2], r[3]}.normalized();
    // NoRotate (§12.5.3): on a page shown turned by /Rotate, the annotation stays upright on the
    // screen, turned back about the upper-left corner of its rectangle, which keeps its place.
    constexpr std::int64_t kNoRotate = 1 << 4;
    Matrix upright;
    if ((flags & kNoRotate) != 0 && out_->rotate != 0) {
      const double radians = out_->rotate * std::numbers::pi / 180;
      const double cos = std::cos(radians);
      const double sin = std::sin(radians);
      upright = Matrix::translate(-rect.x0, -rect.y1).then(Matrix{cos, sin, -sin, cos, 0, 0}).then(Matrix::translate(rect.x0, rect.y1));
    }
    double opacity = 1;
    if (const Object* ca = annotation.find("CA")) {
      opacity = std::clamp(number(*ca).value_or(1), 0.0, 1.0);
    }

    // The normal appearance: a stream, or one per state (/AS).
    std::optional<model::ObjectRef> ref;
    std::optional<Object> appearance;
    if (const Object* ap_entry = annotation.find("AP")) {
      auto ap = document_->resolve(*ap_entry);
      const Object* normal = ap && ap->as_dict() != nullptr ? ap->as_dict()->find("N") : nullptr;
      auto n = normal != nullptr ? document_->resolve(*normal) : Result<Object>(Object());
      if (n && n->as_stream() != nullptr) {
        ref = normal->as_ref();
        appearance = std::move(*n);
      } else if (n && n->as_dict() != nullptr) {
        const Object* state = annotation.find("AS");
        const Object* chosen = state != nullptr && state->as_name() != nullptr ? n->as_dict()->find(state->as_name()->bytes) : nullptr;
        auto s = chosen != nullptr ? document_->resolve(*chosen) : Result<Object>(Object());
        if (s && s->as_stream() != nullptr) {
          ref = chosen->as_ref();
          appearance = std::move(*s);
        }
      }
    }
    if (appearance) {
      draw_appearance(ref, *appearance->as_stream(), rect, opacity, upright);
      return;
    }
    if (const std::string content = markup_appearance(*document_, subtype, annotation, rect); !content.empty()) {
      save();
      const Matrix ctm = state_.ctm;
      state_ = GraphicsState{};
      state_.ctm = upright.then(ctm);
      const bool grouped = opacity < 1;
      if (grouped) {
        emit(graphics::BeginGroup{.alpha = opacity, .blend = graphics::BlendMode::kNormal, .isolated = true, .knockout = false});
      }
      if (subtype == "Highlight") {
        state_.blend = graphics::BlendMode::kMultiply;  // the text stays readable under it
      }
      run(std::as_bytes(std::span(content)), nullptr, 1);
      if (grouped) {
        emit(graphics::EndGroup{});
      }
      restore();
    } else if (subtype == "Widget" || subtype == "FreeText" || subtype == "Text" || subtype == "Stamp") {
      warnings_.add("/" + subtype + " annotations without an appearance are not drawn");
    }
  }

  // §12.5.5: the appearance's /BBox, through its /Matrix, is mapped onto /Rect.
  void draw_appearance(std::optional<model::ObjectRef> ref, const model::Stream& form, const graphics::Rect& rect, double opacity, const Matrix& upright) {
    const std::vector<double> box = numbers_of(form.dict, "BBox");
    if (box.size() != 4) {
      warnings_.add("annotation appearance without /BBox: not drawn");
      return;
    }
    Matrix matrix;
    if (const std::vector<double> m = numbers_of(form.dict, "Matrix"); m.size() == 6) {
      matrix = {m[0], m[1], m[2], m[3], m[4], m[5]};
    }
    const graphics::Rect placed = matrix.apply(graphics::Rect{box[0], box[1], box[2], box[3]}.normalized());
    if (placed.width() <= 0 || placed.height() <= 0) {
      return;  // an empty appearance
    }
    const double sx = rect.width() / placed.width();
    const double sy = rect.height() / placed.height();
    save();
    state_ = GraphicsState{};
    state_.ctm = Matrix{sx, 0, 0, sy, rect.x0 - (placed.x0 * sx), rect.y0 - (placed.y0 * sy)}.then(upright);
    const bool grouped = opacity < 1;
    if (grouped) {
      emit(graphics::BeginGroup{.alpha = opacity, .blend = graphics::BlendMode::kNormal, .isolated = true, .knockout = false});
    }
    draw_form(ref, form, 0);
    if (grouped) {
      emit(graphics::EndGroup{});
    }
    restore();
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
        set_ext_gstate(op, form_depth);
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
        text_clip_ = {};
        break;
      case kEndText:
        end_text();
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
          show(op.operands.back(), form_depth);
        }
        break;
      case kNextLineShowText:
        next_line(0, -state_.leading);
        if (!op.operands.empty()) {
          show(op.operands.back(), form_depth);
        }
        break;
      case kNextLineSpacingShowText:  // aw ac string " (§9.4.3)
        if (op.operands.size() >= 3) {
          const std::size_t n = op.operands.size();
          state_.word_spacing = number(op.operands[n - 3]).value_or(state_.word_spacing);
          state_.char_spacing = number(op.operands[n - 2]).value_or(state_.char_spacing);
          next_line(0, -state_.leading);
          show(op.operands.back(), form_depth);
        }
        break;
      // Type 3 glyph descriptions: meaningful only inside a glyph procedure. After d1 the glyph is a
      // shape painted with the colour of the text: its own colour operators are ignored (§9.6.4).
      case kSetCharWidth:
        break;
      case kSetCacheDevice:
        uncolored_glyph_ = type3_depth_ > 0;
        break;
      // Colour (ignored inside an uncoloured Type 3 glyph)
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
        begin_marked(nullptr);
        break;
      case kBeginMarkedContentProperties:
        begin_marked(op.operands.size() == 2 ? &op.operands[1] : nullptr);
        break;
      case kEndMarkedContent:
        if (!marked_.empty()) {
          marked_.pop_back();
        }
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
  graphics::Path text_clip_;  // glyphs shown in modes 4–7 since BT, in default user space
  font::detail::FontContext fonts_;
  detail::ColorContext colors_;
  std::map<std::uint32_t, std::shared_ptr<Font>> font_cache_;
  std::size_t type3_depth_ = 0;
  // Provenance of the operation being executed, and the marked-content sequences open (the /ActualText
  // index of each, -1 without).
  std::optional<model::ObjectRef> stream_;
  std::uint64_t op_offset_ = 0;
  std::uint32_t op_index_ = 0;
  std::uint32_t invocation_ = 0;  // of the commands emitted now (graphics::Invocation)
  bool in_annotations_ = false;
  std::vector<std::int32_t> marked_;
  bool text_ = false;  // Purpose::kText
  bool uncolored_glyph_ = false;
  bool cell_uncoloured_ = false;  // an uncoloured tiling pattern's cell: colour operators stay ignored
  Matrix base_ctm_;               // the CTM at the start of the content stream being run (pattern space)
  std::map<std::uint32_t, std::shared_ptr<const graphics::Shading>> shadings_;
  std::map<std::uint32_t, std::shared_ptr<const graphics::DisplayList>> tiles_;
  std::size_t tile_depth_ = 0;
  std::size_t compatibility_ = 0;
  std::set<std::uint32_t> active_forms_;
  std::map<std::uint32_t, std::shared_ptr<const graphics::ImagePixels>> images_;
  std::int64_t image_pixels_ = 0;
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

Result<graphics::DisplayList> interpret_page(Document& document, model::ObjectRef page, const InterpreterLimits& limits, const font::Resources* resources,
                                             Purpose purpose) {
  auto node = document.get(page);
  if (!node) {
    return std::unexpected(std::move(node.error()));
  }
  if ((*node)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "page " + std::to_string(page.number) + " is not a dictionary");
  }
  std::optional<Object> annots;
  if (const Object* entry = (*node)->as_dict()->find("Annots")) {
    annots = *entry;  // copied: loading other objects may move the page's
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
    content = std::vector<std::byte>{};  // the annotations are still drawn
  }
  const auto page_resources = inherited(document, page, "Resources");
  Interpreter interpreter(document, limits, resources, list, purpose);
  const model::Dict* resources_dict = page_resources && page_resources->as_dict() != nullptr ? page_resources->as_dict() : nullptr;
  interpreter.run(*content, resources_dict, 0);
  if (annots) {
    interpreter.draw_annotations(*annots, resources_dict);
  }
  list.missing_resources = interpreter.missing_resources();
  auto found = interpreter.take_warnings();
  list.warnings.insert(list.warnings.end(), found.begin(), found.end());
  return list;
}

}  // namespace capriole::content
