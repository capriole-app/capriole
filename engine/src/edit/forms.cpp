#include "capriole/edit/forms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/content/appearance.hpp"
#include "capriole/model/text_string.hpp"
#include "common.hpp"
#include "font/font.hpp"
#include "sound.hpp"

namespace capriole::edit {

namespace {

using detail::hex;
using detail::name;
using detail::num;
using graphics::Matrix;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// Bounds against hostile field trees.
constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxNodes = 100'000;
constexpr std::size_t kMaxOptions = 10'000;

// Field flags (§12.7.4.1, Table 227; buttons Table 229; text Table 231; choices Table 233).
constexpr std::int64_t kReadOnly = 1 << 0;
constexpr std::int64_t kRequired = 1 << 1;
constexpr std::int64_t kMultiline = 1 << 12;
constexpr std::int64_t kPassword = 1 << 13;
constexpr std::int64_t kRadio = 1 << 15;
constexpr std::int64_t kPushButton = 1 << 16;
constexpr std::int64_t kCombo = 1 << 17;
constexpr std::int64_t kEdit = 1 << 18;
constexpr std::int64_t kMultiSelect = 1 << 21;
constexpr std::int64_t kComb = 1 << 24;

// The attributes a field inherits from its ancestors (§12.7.4.1; /MaxLen and /Opt as readers do).
constexpr std::array<std::string_view, 8> kInheritable = {"FT", "Ff", "V", "DV", "DA", "Q", "MaxLen", "Opt"};

using Attributes = std::map<std::string, Object, std::less<>>;

// A terminal field as found in the tree, with what it inherits.
struct Terminal {
  FieldInfo info;
  Attributes attributes;
};

// An entry that is there: one whose value is null is as if absent (§7.3.7).
bool has(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && !value->is_null();
}

std::string text_of(const Object& value) {
  return value.as_string() != nullptr ? model::to_utf8(model::decode_text_string(value.as_string()->bytes)) : std::string();
}

std::int64_t integer_of(const Attributes& a, std::string_view key) {
  const auto found = a.find(key);
  return found != a.end() ? found->second.as_integer().value_or(0) : 0;
}

std::string name_of(const Attributes& a, std::string_view key) {
  const auto found = a.find(key);
  return found != a.end() && found->second.as_name() != nullptr ? found->second.as_name()->bytes : std::string();
}

// The names of a widget's appearance states (/AP /N, else /D), "Off" left out.
std::vector<std::string> on_states(Document& document, const Dict& widget) {
  std::vector<std::string> out;
  const Object* ap = widget.find("AP");
  auto resolved = ap != nullptr ? document.resolve(*ap) : Result<Object>(Object());
  if (!resolved || resolved->as_dict() == nullptr) {
    return out;
  }
  for (const std::string_view key : {"N", "D"}) {
    const Object* states = resolved->as_dict()->find(key);
    auto dict = states != nullptr ? document.resolve(*states) : Result<Object>(Object());
    if (dict && dict->as_dict() != nullptr) {
      for (const model::DictEntry& entry : dict->as_dict()->entries()) {
        if (entry.key != "Off") {
          out.push_back(entry.key);
        }
      }
      if (!out.empty()) {
        return out;
      }
    }
  }
  return out;
}

class Walker {
 public:
  explicit Walker(Document& document) : document_(&document) {}

  Result<std::vector<Terminal>> run() {
    auto catalog = document_->catalog();
    if (!catalog) {
      return std::vector<Terminal>{};  // the form hangs from the catalog: none can be read
    }
    const Object* form_entry = (*catalog)->find("AcroForm");
    auto form = form_entry != nullptr ? document_->resolve(*form_entry) : Result<Object>(Object());
    if (!form || form->as_dict() == nullptr) {
      return std::vector<Terminal>{};
    }
    const Object* fields_entry = form->as_dict()->find("Fields");
    auto fields = fields_entry != nullptr ? document_->resolve(*fields_entry) : Result<Object>(Object());
    if (!fields || fields->as_array() == nullptr) {
      return std::vector<Terminal>{};
    }
    index_pages();
    for (const Object& field : *fields->as_array()) {
      if (const auto ref = field.as_ref()) {
        visit(*ref, "", {}, 0);
      }
    }
    return std::move(out_);
  }

 private:
  void index_pages() {
    auto pages = document_->page_refs();
    if (!pages) {
      return;
    }
    for (std::uint32_t i = 0; i < pages->size(); ++i) {
      auto page = detail::page_at(*document_, i, std::nullopt);
      if (!page) {
        continue;
      }
      const Object* annots = page->dict.find("Annots");
      auto list = annots != nullptr ? document_->resolve(*annots) : Result<Object>(Object());
      if (list && list->as_array() != nullptr) {
        for (const Object& a : *list->as_array()) {
          if (const auto ref = a.as_ref()) {
            widget_pages_.emplace(ref->number, i);
          }
        }
      }
      pages_.emplace(i, std::move(*page));
    }
  }

  FieldWidget widget_of(ObjectRef ref, const Dict& dict) {
    FieldWidget w{.ref = ref, .page = std::nullopt, .rect = {}, .on_state = {}};
    const std::vector<double> r = content::numbers(*document_, dict, "Rect");
    const Rect rect = r.size() == 4 ? Rect{r[0], r[1], r[2], r[3]}.normalized() : Rect{};
    w.rect = rect;
    if (const auto found = widget_pages_.find(ref.number); found != widget_pages_.end()) {
      w.page = found->second;
      w.rect = pages_.at(found->second).view(rect);
    }
    const auto states = on_states(*document_, dict);
    if (!states.empty()) {
      w.on_state = states.front();
    }
    return w;
  }

  void visit(ObjectRef ref, const std::string& parent_name, Attributes attributes, std::size_t depth) {
    if (depth > kMaxDepth || ++nodes_ > kMaxNodes || !seen_.insert(ref.number).second) {
      return;
    }
    auto loaded = document_->get(ref);
    if (!loaded || (*loaded)->as_dict() == nullptr) {
      return;
    }
    const Dict dict = *(*loaded)->as_dict();
    for (const std::string_view key : kInheritable) {
      if (const Object* value = dict.find(key)) {
        auto resolved = document_->resolve(*value);
        if (resolved) {
          attributes.insert_or_assign(std::string(key), std::move(*resolved));
        }
      }
    }
    std::string full = parent_name;
    if (const Object* t = dict.find("T"); t != nullptr && t->as_string() != nullptr) {
      full = full.empty() ? text_of(*t) : full + "." + text_of(*t);
    }
    std::vector<FieldWidget> widgets;
    std::vector<ObjectRef> fields;
    const Object* kids_entry = dict.find("Kids");
    auto kids = kids_entry != nullptr ? document_->resolve(*kids_entry) : Result<Object>(Object());
    if (kids && kids->as_array() != nullptr) {
      for (const Object& kid : *kids->as_array()) {
        const auto kid_ref = kid.as_ref();
        if (!kid_ref) {
          continue;
        }
        auto kid_object = document_->get(*kid_ref);
        const Dict* kid_dict = kid_object && *kid_object != nullptr ? (*kid_object)->as_dict() : nullptr;
        if (kid_dict == nullptr) {
          continue;
        }
        // A kid with a partial name, or with kids of its own, is a field; otherwise a widget (§12.7.4).
        if (has(*kid_dict, "T") || has(*kid_dict, "Kids")) {
          fields.push_back(*kid_ref);
        } else if (seen_.insert(kid_ref->number).second) {
          widgets.push_back(widget_of(*kid_ref, *kid_dict));
        }
      }
    } else if (detail::subtype_of(dict) == "Widget") {
      widgets.push_back(widget_of(ref, dict));  // field and widget merged
    }
    if (!widgets.empty() || fields.empty()) {
      add(ref, dict, full, attributes, std::move(widgets));
    }
    for (const ObjectRef field : fields) {
      visit(field, full, attributes, depth + 1);
    }
  }

  void add(ObjectRef ref, const Dict& dict, const std::string& full, const Attributes& attributes, std::vector<FieldWidget> widgets) {
    const std::string ft = name_of(attributes, "FT");
    const std::int64_t ff = integer_of(attributes, "Ff");
    FieldInfo info;
    info.ref = ref;
    info.name = full;
    info.label = detail::utf8_of(dict, "TU");
    if (ft == "Tx") {
      info.type = FieldType::kText;
    } else if (ft == "Btn") {
      info.type = FieldType::kCheckBox;
      if ((ff & kPushButton) != 0) {
        info.type = FieldType::kPushButton;
      } else if ((ff & kRadio) != 0) {
        info.type = FieldType::kRadio;
      }
    } else if (ft == "Ch") {
      info.type = (ff & kCombo) != 0 ? FieldType::kComboBox : FieldType::kListBox;
    } else if (ft == "Sig") {
      info.type = FieldType::kSignature;
    } else {
      return;  // no type: not a field that can be shown or filled
    }
    info.read_only = (ff & kReadOnly) != 0;
    info.required = (ff & kRequired) != 0;
    info.multiline = info.type == FieldType::kText && (ff & kMultiline) != 0;
    info.password = info.type == FieldType::kText && (ff & kPassword) != 0;
    info.editable = info.type == FieldType::kComboBox && (ff & kEdit) != 0;
    info.multi_select = info.type == FieldType::kListBox && (ff & kMultiSelect) != 0;
    if (const auto found = attributes.find("MaxLen"); found != attributes.end()) {
      if (const auto length = found->second.as_integer(); length && *length >= 0) {
        info.max_length = static_cast<std::uint32_t>(std::min<std::int64_t>(*length, 1'000'000));
      }
    }
    info.comb = info.type == FieldType::kText && (ff & kComb) != 0 && info.max_length.value_or(0) > 0 && (ff & (kMultiline | kPassword)) == 0;
    info.fillable = !info.read_only && info.type != FieldType::kSignature && info.type != FieldType::kPushButton;

    // The value.
    const auto v = attributes.find("V");
    if (info.type == FieldType::kCheckBox || info.type == FieldType::kRadio) {
      // A name; some producers write a string.
      const bool named = v != attributes.end() && v->second.as_name() != nullptr;
      const bool written = v != attributes.end() && v->second.as_string() != nullptr && !v->second.as_string()->bytes.empty();
      std::string state = "Off";
      if (named) {
        state = v->second.as_name()->bytes;
      } else if (written) {
        state = v->second.as_string()->bytes;
      }
      info.value = {std::move(state)};
    } else if (v != attributes.end()) {
      if (v->second.as_array() != nullptr) {
        for (const Object& item : *v->second.as_array()) {
          auto resolved = document_->resolve(item);
          if (resolved && resolved->as_string() != nullptr) {
            info.value.push_back(text_of(*resolved));
          }
        }
      } else if (v->second.as_string() != nullptr) {
        info.value.push_back(text_of(v->second));
      }
    }

    // The options.
    if (info.type == FieldType::kComboBox || info.type == FieldType::kListBox) {
      if (const auto opt = attributes.find("Opt"); opt != attributes.end() && opt->second.as_array() != nullptr) {
        for (const Object& item : *opt->second.as_array()) {
          if (info.options.size() >= kMaxOptions) {
            break;
          }
          auto resolved = document_->resolve(item);
          if (!resolved) {
            continue;
          }
          if (resolved->as_array() != nullptr && resolved->as_array()->size() >= 2) {
            auto value = document_->resolve((*resolved->as_array())[0]);
            auto display = document_->resolve((*resolved->as_array())[1]);
            info.options.push_back({.value = value ? text_of(*value) : std::string(), .display = display ? text_of(*display) : std::string()});
          } else if (resolved->as_string() != nullptr) {
            const std::string text = text_of(*resolved);
            info.options.push_back({.value = text, .display = text});
          }
        }
      }
    } else if (info.type == FieldType::kCheckBox || info.type == FieldType::kRadio) {
      // /Opt of a button (§12.7.5.2.3): the export value of each widget, by its place among the kids.
      std::vector<std::string> exports;
      if (const auto opt = attributes.find("Opt"); opt != attributes.end() && opt->second.as_array() != nullptr) {
        for (const Object& item : *opt->second.as_array()) {
          auto resolved = document_->resolve(item);
          exports.push_back(resolved ? text_of(*resolved) : std::string());
        }
      }
      for (std::size_t i = 0; i < widgets.size(); ++i) {
        const std::string& state = widgets[i].on_state;
        if (state.empty() || std::ranges::any_of(info.options, [&](const ChoiceOption& o) { return o.value == state; })) {
          continue;
        }
        info.options.push_back({.value = state, .display = i < exports.size() && !exports[i].empty() ? exports[i] : state});
      }
    }
    info.widgets = std::move(widgets);
    out_.push_back({.info = std::move(info), .attributes = attributes});
  }

  Document* document_;
  std::map<std::uint32_t, std::uint32_t> widget_pages_;  // widget number → page index
  std::map<std::uint32_t, detail::Page> pages_;
  std::set<std::uint32_t> seen_;
  std::size_t nodes_ = 0;
  std::vector<Terminal> out_;
};

// ── Appearances ──────────────────────────────────────────────────────────────────────────────────

// The colour operator for a colour array of /MK (0 components: none).
std::string color_operator(const std::vector<double>& c, bool stroke) {
  switch (c.size()) {
    case 1:
      return std::format("{} {}", num(c[0]), stroke ? "G" : "g");
    case 3:
      return std::format("{} {} {} {}", num(c[0]), num(c[1]), num(c[2]), stroke ? "RG" : "rg");
    case 4:
      return std::format("{} {} {} {} {}", num(c[0]), num(c[1]), num(c[2]), num(c[3]), stroke ? "K" : "k");
    default:
      return {};
  }
}

// The font an appearance writes a value with: the field's (from /DA, in the form's /DR) when it can
// write every character, else Helvetica.
struct AppearanceFont {
  std::string resource = "Helv";
  Object font;  // the font dictionary or its reference, for the appearance's resources
  detail::Metrics metrics;
  std::map<char32_t, std::uint8_t> codes;
  bool helvetica = true;

  [[nodiscard]] std::string encode(std::u32string_view text) const {
    if (helvetica) {
      return detail::win_ansi_bytes(text);
    }
    std::string out;
    for (const char32_t c : text) {
      const auto found = codes.find(c);
      if (c == U'\n') {
        out.push_back('\n');
      } else {
        out.push_back(static_cast<char>(found != codes.end() ? found->second : 0));
      }
    }
    return out;
  }
};

AppearanceFont choose_font(Document& document, const Dict* resources, const std::string& font_name, std::u32string_view text) {
  AppearanceFont helvetica;
  helvetica.metrics = detail::helvetica();
  const Dict standard = detail::helvetica_font();
  helvetica.font = *standard.find("Helv");
  const Object* fonts_entry = resources != nullptr && !font_name.empty() ? resources->find("Font") : nullptr;
  auto fonts = fonts_entry != nullptr ? document.resolve(*fonts_entry) : Result<Object>(Object());
  const Object* entry = fonts && fonts->as_dict() != nullptr ? fonts->as_dict()->find(font_name) : nullptr;
  if (entry == nullptr) {
    return helvetica;
  }
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  const auto font = font::detail::Font::load(document, *entry, context, warnings);
  if (!font || font->kind() != font::detail::Font::Kind::kSimple || !font->widths_known()) {
    return helvetica;
  }
  AppearanceFont chosen;
  chosen.resource = font_name;
  chosen.font = *entry;
  chosen.helvetica = false;
  for (std::uint32_t code = 0; code < 256; ++code) {
    const font::detail::Font::Code c{.code = code, .length = 1, .cid = code};
    chosen.metrics.widths.at(code) = font->width(c) * font->scale();
    if (const std::u32string t = font->text(c); t.size() == 1) {
      chosen.codes.emplace(t[0], static_cast<std::uint8_t>(code));
    }
  }
  // Every character must have a code, and a glyph (an embedded subset may lack those not used before).
  for (const char32_t ch : text) {
    if (ch == U'\n') {
      continue;
    }
    const auto found = chosen.codes.find(ch);
    if (found == chosen.codes.end()) {
      return helvetica;
    }
    const font::detail::Font::Code c{.code = found->second, .length = 1, .cid = found->second};
    if (ch != U' ' && font->outline(c) == nullptr) {
      return helvetica;
    }
  }
  if (font->ascent() > 0 && font->ascent() < 2) {
    chosen.metrics.ascent = font->ascent();
  }
  if (font->descent() < 0 && font->descent() > -1) {
    chosen.metrics.descent = font->descent();
  }
  return chosen;
}

// What a widget's appearance is drawn in: its size upright (as /MK /R turns it), the matrix that turns
// it onto /Rect, its border and background.
struct Box {
  double width = 0;
  double height = 0;
  std::optional<Matrix> matrix;
  double border = 0;
  std::string border_style = "S";
  std::string background;  // fill colour operator, empty for none
  std::string stroke;      // stroke colour operator, empty for none
  std::string da;          // the widget's own /DA, if any
  std::optional<std::int64_t> quadding;
};

Box box_of(Document& document, const Dict& widget) {
  Box box;
  const std::vector<double> r = content::numbers(document, widget, "Rect");
  const Rect rect = r.size() == 4 ? Rect{r[0], r[1], r[2], r[3]}.normalized() : Rect{};
  box.width = rect.width();
  box.height = rect.height();
  const Object* mk_entry = widget.find("MK");
  auto mk = mk_entry != nullptr ? document.resolve(*mk_entry) : Result<Object>(Object());
  if (mk && mk->as_dict() != nullptr) {
    box.background = color_operator(content::numbers(document, *mk->as_dict(), "BG"), false);
    box.stroke = color_operator(content::numbers(document, *mk->as_dict(), "BC"), true);
    const Object* rotation = mk->as_dict()->find("R");
    const std::int64_t degrees = rotation != nullptr ? ((rotation->as_integer().value_or(0) % 360) + 360) % 360 : 0;
    // The appearance turned counterclockwise by /R (§12.5.6.19): its upright box onto /Rect.
    if (degrees == 90) {
      box.matrix = Matrix{0, 1, -1, 0, rect.width(), 0};
      std::swap(box.width, box.height);
    } else if (degrees == 180) {
      box.matrix = Matrix{-1, 0, 0, -1, rect.width(), rect.height()};
    } else if (degrees == 270) {
      box.matrix = Matrix{0, -1, 1, 0, 0, rect.height()};
      std::swap(box.width, box.height);
    }
  }
  // The border (§12.5.4): /BS, else /Border; drawn when it has a colour.
  box.border = box.stroke.empty() ? 0 : 1;
  const Object* bs_entry = widget.find("BS");
  auto bs = bs_entry != nullptr ? document.resolve(*bs_entry) : Result<Object>(Object());
  if (bs && bs->as_dict() != nullptr) {
    if (const Object* w = bs->as_dict()->find("W")) {
      box.border = std::clamp(w->as_number().value_or(1), 0.0, 20.0);
    }
    if (const Object* s = bs->as_dict()->find("S"); s != nullptr && s->as_name() != nullptr) {
      box.border_style = s->as_name()->bytes;
    }
  }
  if (box.stroke.empty()) {
    box.border = 0;
  }
  box.border = std::min({box.border, box.width / 4, box.height / 4});
  if (const Object* da = widget.find("DA"); da != nullptr && da->as_string() != nullptr) {
    box.da = da->as_string()->bytes;
  }
  if (const Object* q = widget.find("Q")) {
    box.quadding = q->as_integer();
  }
  return box;
}

// The background and border of a widget, in its upright box.
std::string frame(const Box& box) {
  std::string c;
  if (!box.background.empty()) {
    c += std::format("{} 0 0 {} {} re f\n", box.background, num(box.width), num(box.height));
  }
  if (box.border > 0 && !box.stroke.empty()) {
    const double half = box.border / 2;
    if (box.border_style == "U") {
      c += std::format("{} {} w 0 {} m {} {} l S\n", box.stroke, num(box.border), num(half), num(box.width), num(half));
    } else {
      c += std::format("{} {} w {}{} {} {} {} re S\n", box.stroke, num(box.border), box.border_style == "D" ? "[3] 0 d " : "", num(half), num(half),
                       num(box.width - box.border), num(box.height - box.border));
    }
  }
  return c;
}

struct TextLayout {
  std::u32string text;
  double size = 0;  // 0: auto
  std::string color = "0 g";
  std::int64_t quadding = 0;
  bool multiline = false;
  std::uint32_t comb = 0;  // cells; 0 when not a comb field
};

// Variable text (§12.7.4.3): the value written in the widget's box, clipped to inside its border.
std::string text_content(const Box& box, const AppearanceFont& font, const TextLayout& layout) {
  const double inset = box.border * ((box.border_style == "B" || box.border_style == "I") ? 2 : 1);
  const double padding = 2;
  const double left = inset + padding;
  const double avail_w = std::max(box.width - (2 * left), 1.0);
  const double avail_h = std::max(box.height - (2 * inset), 1.0);
  const detail::Metrics& m = font.metrics;
  const double extent = std::max(m.ascent - m.descent, 0.5);
  const std::string codes = font.encode(layout.text);

  double size = layout.size;
  std::vector<std::string> lines;
  if (layout.multiline) {
    if (size <= 0) {  // auto: 12 points, smaller until the lines fit
      size = 12;
      while (size > 4 && static_cast<double>(detail::wrap(codes, size, avail_w, m).size()) * size * 1.15 > avail_h - padding) {
        size -= 0.5;
      }
    }
    lines = detail::wrap(codes, size, avail_w, m);
  } else {
    std::string line = codes;
    std::ranges::replace(line, '\n', ' ');
    if (size <= 0) {  // auto: as tall as the box allows, narrower until the text fits
      size = std::clamp((avail_h - padding) / extent, 4.0, 12.0 * 4);
      if (layout.comb == 0) {
        const double width = detail::text_width(line, 1, m);
        if (width * size > avail_w && width > 0) {
          size = std::max(avail_w / width, 4.0);
        }
      }
    }
    lines.push_back(std::move(line));
  }

  std::string c = std::format("/Tx BMC\nq\n{} {} {} {} re W n\nBT\n/{} {} Tf {}\n", num(inset), num(inset), num(box.width - (2 * inset)),
                              num(box.height - (2 * inset)), font.resource, num(size), layout.color);
  if (layout.comb > 0) {
    const double cell = box.width / layout.comb;
    const double baseline = ((box.height - (extent * size)) / 2) - (m.descent * size);
    for (std::size_t i = 0; i < lines.front().size() && i < layout.comb; ++i) {
      const std::string glyph(1, lines.front()[i]);
      const double x = (static_cast<double>(i) * cell) + ((cell - detail::text_width(glyph, size, m)) / 2);
      c += std::format("1 0 0 1 {} {} Tm {} Tj\n", num(x), num(baseline), hex(glyph));
    }
  } else {
    double baseline = layout.multiline ? box.height - inset - padding - (m.ascent * size) : ((box.height - (extent * size)) / 2) - (m.descent * size);
    for (const std::string& line : lines) {
      if (baseline + (m.ascent * size) < 0) {
        break;
      }
      const double width = detail::text_width(line, size, m);
      double x = left;
      if (layout.quadding == 1) {
        x = (box.width - width) / 2;
      } else if (layout.quadding == 2) {
        x = box.width - left - width;
      }
      c += std::format("1 0 0 1 {} {} Tm {} Tj\n", num(x), num(baseline), hex(line));
      baseline -= size * 1.15;
    }
  }
  return c + "ET\nQ\nEMC\n";
}

// A list box (§12.7.5.3): the options from /TI down, the chosen ones on a highlight.
std::string list_content(const Box& box, const AppearanceFont& font, const std::vector<std::u32string>& rows, const std::set<std::size_t>& chosen,
                         std::size_t top, double size, const std::string& color) {
  const double inset = box.border * ((box.border_style == "B" || box.border_style == "I") ? 2 : 1);
  size = size > 0 ? size : 12;
  const double row = size * 1.15;
  std::string c = std::format("/Tx BMC\nq\n{} {} {} {} re W n\n", num(inset), num(inset), num(box.width - (2 * inset)), num(box.height - (2 * inset)));
  const auto row_y = [&](std::size_t i) { return box.height - inset - (row * static_cast<double>(i - top + 1)); };
  for (std::size_t i = top; i < rows.size() && row_y(i) + row > 0; ++i) {
    const double y = row_y(i);
    if (chosen.contains(i)) {
      c += std::format("0.6 0.75 0.85 rg {} {} {} {} re f\n", num(inset), num(y), num(box.width - (2 * inset)), num(row));
    }
  }
  c += std::format("BT\n/{} {} Tf {}\n", font.resource, num(size), color);
  const double baseline = ((row - (size * (font.metrics.ascent - font.metrics.descent))) / 2) - (font.metrics.descent * size);
  for (std::size_t i = top; i < rows.size() && row_y(i) + row > 0; ++i) {
    c += std::format("1 0 0 1 {} {} Tm {} Tj\n", num(inset + 2), num(row_y(i) + baseline), hex(font.encode(rows[i])));
  }
  return c + "ET\nQ\nEMC\n";
}

// A check box's or radio button's "on" mark, drawn when the widget has no appearance of its own.
std::string mark_content(const Box& box, bool radio, const std::string& color) {
  const double s = std::min(box.width, box.height);
  const double cx = box.width / 2;
  const double cy = box.height / 2;
  if (radio) {
    const double r = s * 0.25;
    const double k = r * 0.5523;  // Bézier circle
    return std::format("{}\n{} {} m {} {} {} {} {} {} c {} {} {} {} {} {} c {} {} {} {} {} {} c {} {} {} {} {} {} c f\n", color, num(cx + r), num(cy),
                       num(cx + r), num(cy + k), num(cx + k), num(cy + r), num(cx), num(cy + r), num(cx - k), num(cy + r), num(cx - r), num(cy + k),
                       num(cx - r), num(cy), num(cx - r), num(cy - k), num(cx - k), num(cy - r), num(cx), num(cy - r), num(cx + k), num(cy - r), num(cx + r),
                       num(cy - k), num(cx + r), num(cy));
  }
  // A tick: stroked in the text's colour as a fill colour (the DA sets the non-stroking one).
  std::string stroke = "0 G";
  for (const auto& [fill, line] : {std::pair<std::string_view, std::string_view>{" g", " G"}, {" rg", " RG"}, {" k", " K"}}) {
    if (color.ends_with(fill)) {
      stroke = color.substr(0, color.size() - fill.size()) + std::string(line);
    }
  }
  return std::format("{} {} w 1 J 1 j {} {} m {} {} l {} {} l S\n", stroke, num(std::max(s * 0.1, 0.5)), num(cx - (s * 0.28)), num(cy), num(cx - (s * 0.08)),
                     num(cy - (s * 0.22)), num(cx + (s * 0.3)), num(cy + (s * 0.26)));
}

// The form's /DR and /DA, and whether it has XFA.
struct Form {
  std::optional<ObjectRef> ref;  // the AcroForm's object, when indirect
  Dict dict;
};

Result<Form> form_of(Document& document) {
  auto catalog = document.catalog();
  if (!catalog) {
    return std::unexpected(std::move(catalog.error()));
  }
  const Object* entry = (*catalog)->find("AcroForm");
  auto form = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!form || form->as_dict() == nullptr) {
    return make_error(ErrorCode::kOutOfRange, "the document has no form");
  }
  return Form{.ref = entry->as_ref(), .dict = *form->as_dict()};
}

std::string string_entry(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_string() != nullptr ? value->as_string()->bytes : std::string();
}

// What a fill changes, prepared without touching the document and written at the end: the dictionaries
// it changes (a field and its widget can be one object) and the appearances it makes. Reading can find
// repairs on the way (an object's offset is wrong: the cross-reference table is rebuilt), so the
// structure is checked again once everything is read, before anything is written: a refusal changes
// nothing. New appearances are referred to by placeholders (object 0, which is never in use, with the
// appearance's index as generation) until they have their numbers.
class Changes {
 public:
  explicit Changes(Document& document) : document_(&document) {}

  Result<Dict*> dict(ObjectRef ref) {
    if (const auto found = dicts_.find(ref.number); found != dicts_.end()) {
      return &found->second;
    }
    auto loaded = document_->get(ref);
    if (!loaded || (*loaded)->as_dict() == nullptr) {
      return make_error(ErrorCode::kSyntax, std::format("object {} {} R is not a dictionary", ref.number, ref.generation));
    }
    return &dicts_.emplace(ref.number, *(*loaded)->as_dict()).first->second;
  }

  Result<ObjectRef> stream(model::Stream stream) {
    if (streams_.size() > 0xFFFF) {
      return make_error(ErrorCode::kLimitExceeded, "too many widgets in one field");
    }
    streams_.push_back(std::move(stream));
    return ObjectRef{.number = 0, .generation = static_cast<std::uint16_t>(streams_.size() - 1)};
  }

  Result<void> write() {
    if (auto sound = detail::require_sound_structure(*document_, "form fields"); !sound) {
      return sound;
    }
    std::vector<ObjectRef> refs;
    for (model::Stream& s : streams_) {
      auto ref = document_->add(Object(std::move(s)));
      if (!ref) {
        return std::unexpected(std::move(ref.error()));
      }
      refs.push_back(*ref);
    }
    for (auto& [number, dict] : dicts_) {
      place(dict, refs);
      if (auto set = document_->set(number, Object(std::move(dict))); !set) {
        return set;
      }
    }
    return {};
  }

 private:
  // The placeholders of /AP (/N a stream, or a dictionary of states) replaced by the real references.
  static void place(Dict& dict, const std::vector<ObjectRef>& refs) {
    const Object* ap = dict.find("AP");
    if (ap == nullptr || ap->as_dict() == nullptr) {
      return;
    }
    const auto real = [&](const Object& o) -> Object {
      const auto r = o.as_ref();
      return r && r->number == 0 && r->generation < refs.size() ? Object(refs[r->generation]) : o;
    };
    Dict updated = *ap->as_dict();
    if (const Object* n = updated.find("N")) {
      if (n->as_dict() != nullptr) {
        Dict states;
        for (const model::DictEntry& e : n->as_dict()->entries()) {
          states.set(e.key, real(e.value));
        }
        updated.set("N", std::move(states));
      } else {
        updated.set("N", real(*n));
      }
    }
    dict.set("AP", std::move(updated));
  }

  Document* document_;
  std::map<std::uint32_t, Dict> dicts_;
  std::vector<model::Stream> streams_;
};

}  // namespace

Result<std::vector<FieldInfo>> list_fields(Document& document) {
  auto terminals = Walker(document).run();
  if (!terminals) {
    return std::unexpected(std::move(terminals.error()));
  }
  std::vector<FieldInfo> out;
  out.reserve(terminals->size());
  for (Terminal& t : *terminals) {
    out.push_back(std::move(t.info));
  }
  return out;
}

Result<void> fill_field(Document& document, ObjectRef ref, const std::vector<std::string>& value) {
  if (auto sound = detail::require_sound_structure(document, "form fields"); !sound) {
    return sound;
  }
  auto terminals = Walker(document).run();
  if (!terminals) {
    return std::unexpected(std::move(terminals.error()));
  }
  const auto found = std::ranges::find_if(*terminals, [&](const Terminal& t) { return t.info.ref == ref; });
  if (found == terminals->end()) {
    return make_error(ErrorCode::kOutOfRange, std::format("{} {} R is not a field of the form", ref.number, ref.generation));
  }
  const FieldInfo& info = found->info;
  if (!info.fillable) {
    std::string_view why = "a push button";
    if (info.read_only) {
      why = "read-only";
    } else if (info.type == FieldType::kSignature) {
      why = "a signature: phase 8h";
    }
    return make_error(ErrorCode::kUnsupported, std::format(R"(the field "{}" cannot be filled ({}))", info.name, why));
  }
  auto form = form_of(document);
  if (!form) {
    return std::unexpected(std::move(form.error()));
  }
  const Object* dr_entry = form->dict.find("DR");
  auto dr = dr_entry != nullptr ? document.resolve(*dr_entry) : Result<Object>(Object());
  const Dict* resources = dr && dr->as_dict() != nullptr ? dr->as_dict() : nullptr;
  const auto field_da = found->attributes.find("DA");
  const std::string default_da =
      field_da != found->attributes.end() && field_da->second.as_string() != nullptr ? field_da->second.as_string()->bytes : string_entry(form->dict, "DA");
  const auto q = found->attributes.find("Q");
  std::int64_t default_q = 0;
  if (q != found->attributes.end()) {
    default_q = q->second.as_integer().value_or(0);
  } else if (const Object* form_q = form->dict.find("Q")) {
    default_q = form_q->as_integer().value_or(0);
  }

  Changes changes(document);
  auto field = changes.dict(ref);
  if (!field) {
    return std::unexpected(std::move(field.error()));
  }

  // The appearance of a text-like widget (text, combo box) showing text.
  const auto text_appearance = [&](const Dict& widget, std::u32string text, bool multiline, std::uint32_t comb) -> Result<ObjectRef> {
    const Box box = box_of(document, widget);
    const detail::DefaultAppearance da = detail::parse_da(box.da.empty() ? default_da : box.da);
    const AppearanceFont font = choose_font(document, resources, da.font, text);
    const TextLayout layout{.text = std::move(text),
                            .size = da.size,
                            .color = da.color,
                            .quadding = std::clamp<std::int64_t>(box.quadding.value_or(default_q), 0, 2),
                            .multiline = multiline,
                            .comb = comb};
    Dict fonts;
    fonts.set(font.resource, font.font);
    Dict res;
    res.set("Font", std::move(fonts));
    const std::string content = frame(box) + text_content(box, font, layout);
    return changes.stream(detail::form_xobject(Rect{0, 0, box.width, box.height}, std::move(res), content, box.matrix));
  };
  const auto set_normal = [&](Dict& widget, ObjectRef stream) {
    Dict ap;
    ap.set("N", stream);
    widget.set("AP", std::move(ap));
    widget.erase("AS");
  };

  switch (info.type) {
    case FieldType::kText: {
      std::string text = value.empty() ? std::string() : value.front();
      if (!info.multiline) {
        std::ranges::replace(text, '\n', ' ');
        std::erase(text, '\r');
      }
      const std::u32string unicode = model::from_utf8(text);
      if (info.max_length && unicode.size() > *info.max_length) {
        return make_error(ErrorCode::kOutOfRange, std::format(R"(the field "{}" takes at most {} characters)", info.name, *info.max_length));
      }
      if (text.empty()) {
        (*field)->erase("V");
      } else {
        (*field)->set("V", model::String{.bytes = model::encode_text_string(unicode)});
      }
      (*field)->erase("RV");  // rich text would be shown instead of /V
      const std::u32string shown = info.password ? std::u32string(unicode.size(), U'*') : unicode;
      for (const FieldWidget& w : info.widgets) {
        auto widget = changes.dict(w.ref);
        if (!widget) {
          return std::unexpected(std::move(widget.error()));
        }
        auto stream = text_appearance(**widget, shown, info.multiline, info.comb ? info.max_length.value_or(0) : 0);
        if (!stream) {
          return std::unexpected(std::move(stream.error()));
        }
        set_normal(**widget, *stream);
      }
      break;
    }
    case FieldType::kComboBox:
    case FieldType::kListBox: {
      if (info.type == FieldType::kComboBox && value.size() > 1) {
        return make_error(ErrorCode::kOutOfRange, std::format(R"(the field "{}" takes one value)", info.name));
      }
      if (info.type == FieldType::kListBox && !info.multi_select && value.size() > 1) {
        return make_error(ErrorCode::kOutOfRange, std::format(R"(the field "{}" takes one option)", info.name));
      }
      std::set<std::size_t> chosen;
      for (const std::string& v : value) {
        const auto option = std::ranges::find_if(info.options, [&](const ChoiceOption& o) { return o.value == v; });
        if (option != info.options.end()) {
          chosen.insert(static_cast<std::size_t>(option - info.options.begin()));
        } else if (!info.editable || v.empty()) {
          return make_error(ErrorCode::kOutOfRange, std::format(R"("{}" is not an option of the field "{}")", v, info.name));
        }
      }
      if (value.empty()) {
        (*field)->erase("V");
      } else if (value.size() == 1) {
        (*field)->set("V", model::String{.bytes = model::encode_text_string(model::from_utf8(value.front()))});
      } else {
        Array values;
        for (const std::string& v : value) {
          values.emplace_back(model::String{.bytes = model::encode_text_string(model::from_utf8(v))});
        }
        (*field)->set("V", std::move(values));
      }
      // /I (§12.7.5.3): the indices of the chosen options, for list boxes (they may repeat a value).
      if (info.type == FieldType::kListBox && !chosen.empty()) {
        Array indices;
        for (const std::size_t i : chosen) {
          indices.emplace_back(static_cast<std::int64_t>(i));
        }
        (*field)->set("I", std::move(indices));
      } else {
        (*field)->erase("I");
      }
      for (const FieldWidget& w : info.widgets) {
        auto widget = changes.dict(w.ref);
        if (!widget) {
          return std::unexpected(std::move(widget.error()));
        }
        Result<ObjectRef> stream = ObjectRef{};
        if (info.type == FieldType::kComboBox) {
          const auto option =
              value.empty() ? info.options.end() : std::ranges::find_if(info.options, [&](const ChoiceOption& o) { return o.value == value.front(); });
          std::string shown = value.empty() ? std::string() : value.front();
          if (option != info.options.end()) {
            shown = option->display;
          }
          stream = text_appearance(**widget, model::from_utf8(shown), false, 0);
        } else {
          const Box box = box_of(document, **widget);
          const detail::DefaultAppearance da = detail::parse_da(box.da.empty() ? default_da : box.da);
          std::vector<std::u32string> rows;
          std::u32string all;
          for (const ChoiceOption& o : info.options) {
            rows.push_back(model::from_utf8(o.display));
            all += rows.back();
          }
          const AppearanceFont font = choose_font(document, resources, da.font, all);
          std::size_t top = 0;
          if (const Object* ti = (*field)->find("TI")) {
            top = static_cast<std::size_t>(std::clamp<std::int64_t>(ti->as_integer().value_or(0), 0, static_cast<std::int64_t>(rows.size())));
          }
          if (!chosen.empty() && *chosen.begin() < top) {
            top = *chosen.begin();
          }
          Dict fonts;
          fonts.set(font.resource, font.font);
          Dict res;
          res.set("Font", std::move(fonts));
          const std::string content = frame(box) + list_content(box, font, rows, chosen, top, da.size, da.color);
          stream = changes.stream(detail::form_xobject(Rect{0, 0, box.width, box.height}, std::move(res), content, box.matrix));
        }
        if (!stream) {
          return std::unexpected(std::move(stream.error()));
        }
        set_normal(**widget, *stream);
      }
      break;
    }
    case FieldType::kCheckBox:
    case FieldType::kRadio: {
      const std::string state = value.empty() || value.front().empty() ? std::string("Off") : value.front();
      const bool any_states = std::ranges::any_of(info.widgets, [](const FieldWidget& w) { return !w.on_state.empty(); });
      const bool known = state == "Off" || std::ranges::any_of(info.widgets, [&](const FieldWidget& w) { return w.on_state == state; });
      if (!known && (any_states || info.type == FieldType::kRadio)) {
        return make_error(ErrorCode::kOutOfRange, std::format(R"("{}" is not a state of the field "{}")", state, info.name));
      }
      (*field)->set("V", name(state));
      for (const FieldWidget& w : info.widgets) {
        auto widget = changes.dict(w.ref);
        if (!widget) {
          return std::unexpected(std::move(widget.error()));
        }
        if (w.on_state.empty() && state != "Off") {
          // No appearance states: an "on" and an "Off" one, drawn.
          const Box box = box_of(document, **widget);
          const detail::DefaultAppearance da = detail::parse_da(box.da.empty() ? default_da : box.da);
          auto on = changes.stream(detail::form_xobject(Rect{0, 0, box.width, box.height}, Dict{},
                                                        frame(box) + mark_content(box, info.type == FieldType::kRadio, da.color), box.matrix));
          auto off = on ? changes.stream(detail::form_xobject(Rect{0, 0, box.width, box.height}, Dict{}, frame(box), box.matrix)) : on;
          if (!on || !off) {
            return std::unexpected(std::move(!on ? on.error() : off.error()));
          }
          Dict states;
          states.set(state, *on);
          states.set("Off", *off);
          Dict ap;
          ap.set("N", std::move(states));
          (*widget)->set("AP", std::move(ap));
          (*widget)->set("AS", name(state));
        } else {
          (*widget)->set("AS", name(w.on_state == state ? state : std::string("Off")));
        }
      }
      break;
    }
    case FieldType::kPushButton:
    case FieldType::kSignature:
      break;
  }

  // XFA would be shown instead of the values written here (§12.7.9): it goes, and with it the catalog's
  // /NeedsRendering (which asks readers to draw the XFA form).
  if (has(form->dict, "XFA")) {
    const Object* root = document.trailer().find("Root");
    const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
    // The catalog is changed only when it has to be: /NeedsRendering to remove, or the AcroForm in it.
    auto current = document.catalog();
    const bool needs_rendering = current && (*current)->contains("NeedsRendering");
    Dict* catalog = nullptr;
    if (root_ref && (needs_rendering || !form->ref)) {
      auto loaded = changes.dict(*root_ref);
      catalog = loaded ? *loaded : nullptr;
    }
    if (catalog != nullptr) {
      catalog->erase("NeedsRendering");
    }
    if (form->ref) {
      auto target = changes.dict(*form->ref);
      if (!target) {
        return std::unexpected(std::move(target.error()));
      }
      (*target)->erase("XFA");
    } else if (catalog != nullptr) {
      Dict updated = form->dict;
      updated.erase("XFA");
      catalog->set("AcroForm", std::move(updated));
    } else {
      return make_error(ErrorCode::kSyntax, "the catalog cannot be changed: the trailer has no /Root reference");
    }
  }
  return changes.write();
}

}  // namespace capriole::edit
