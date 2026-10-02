#include "capriole/edit/annotations.hpp"

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
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/text_string.hpp"
#include "font/glyph_tables.hpp"
#include "font/program.hpp"
#include "font/standard_fonts.hpp"
#include "sound.hpp"

namespace capriole::edit {

namespace {

using graphics::Matrix;
using graphics::Point;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Name;
using model::Object;
using model::ObjectRef;

constexpr std::size_t kMaxParentChain = 256;
// Annotation flags (§12.5.3).
constexpr std::int64_t kHiddenFlag = 1 << 1;
constexpr std::int64_t kPrintFlag = 1 << 2;
constexpr std::int64_t kNoZoomFlag = 1 << 3;
constexpr std::int64_t kNoRotateFlag = 1 << 4;
constexpr std::int64_t kNoViewFlag = 1 << 5;

Name name(std::string text) { return Name{.bytes = std::move(text)}; }

std::string num(double v) {
  // Short and exact enough for page coordinates; no "-0".
  std::string out = std::format("{:.3f}", std::abs(v) < 0.0005 ? 0.0 : v);
  while (out.back() == '0') {
    out.pop_back();
  }
  if (out.back() == '.') {
    out.pop_back();
  }
  return out;
}

Object number_object(double v) { return std::abs(v - std::round(v)) < 1e-9 ? Object(static_cast<std::int64_t>(std::round(v))) : Object(v); }

Array rect_array(const Rect& r) { return Array{number_object(r.x0), number_object(r.y0), number_object(r.x1), number_object(r.y1)}; }

Array rgb_array(const Rgb& c) { return Array{number_object(c[0]), number_object(c[1]), number_object(c[2])}; }

Rgb clamp(Rgb c) {
  for (double& v : c) {
    v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
  }
  return c;
}

Rect bounds(std::span<const Point> points) {
  Rect r{points.front().x, points.front().y, points.front().x, points.front().y};
  for (const Point p : points) {
    r = {std::min(r.x0, p.x), std::min(r.y0, p.y), std::max(r.x1, p.x), std::max(r.y1, p.y)};
  }
  return r;
}

Rect grow(const Rect& r, double by) { return {r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by}; }

// A page, and how its space maps onto the page as shown.
struct Page {
  ObjectRef ref;
  Dict dict;
  Matrix to_view;  // page space → as shown
  Matrix to_user;  // as shown → page space

  [[nodiscard]] Point user(Point p) const { return to_user.apply(p); }
  [[nodiscard]] Rect user(const Rect& r) const {
    const std::array<Point, 4> corners = {user(Point{r.x0, r.y0}), user(Point{r.x1, r.y0}), user(Point{r.x0, r.y1}), user(Point{r.x1, r.y1})};
    return bounds(corners);
  }
  [[nodiscard]] Rect view(const Rect& r) const {
    const std::array<Point, 4> corners = {to_view.apply(Point{r.x0, r.y0}), to_view.apply(Point{r.x1, r.y0}), to_view.apply(Point{r.x0, r.y1}),
                                          to_view.apply(Point{r.x1, r.y1})};
    return bounds(corners);
  }
};

Result<void> require_sound(Document& document) { return detail::require_sound_structure(document, "annotations"); }

// An attribute the page has or inherits (§7.7.3.4).
std::optional<Object> inherited(Document& document, const Dict& page, std::string_view key) {
  const Dict* node = &page;
  std::optional<Object> holder;
  std::set<std::uint32_t> seen;
  for (std::size_t depth = 0; depth < kMaxParentChain && node != nullptr; ++depth) {
    if (const Object* value = node->find(key)) {
      auto resolved = document.resolve(*value);
      return resolved ? std::optional<Object>(std::move(*resolved)) : std::nullopt;
    }
    const Object* parent = node->find("Parent");
    const auto ref = parent != nullptr ? parent->as_ref() : std::nullopt;
    if (!ref || !seen.insert(ref->number).second) {
      break;
    }
    auto next = document.get(*ref);
    holder = next ? std::optional<Object>(**next) : std::nullopt;
    node = holder ? holder->as_dict() : nullptr;
  }
  return std::nullopt;
}

std::optional<Rect> rect_of(const std::optional<Object>& value) {
  const Array* a = value ? value->as_array() : nullptr;
  if (a == nullptr || a->size() != 4) {
    return std::nullopt;
  }
  std::array<double, 4> v{};
  for (std::size_t i = 0; i < 4; ++i) {
    v.at(i) = (*a)[i].as_number().value_or(0);
  }
  return Rect{v[0], v[1], v[2], v[3]}.normalized();
}

// A page to read (list) or to edit (edit: the structure must be sound, sound.hpp).
Result<Page> page_at(Document& document, std::uint32_t index, bool edit = true) {
  auto pages = document.page_refs();
  if (!pages) {
    return std::unexpected(std::move(pages.error()));
  }
  if (auto sound = edit ? require_sound(document) : Result<void>(); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  if (index >= pages->size()) {
    return make_error(ErrorCode::kOutOfRange, "page index " + std::to_string(index) + " out of 0.." + std::to_string(pages->size()));
  }
  const ObjectRef ref = (*pages)[index];
  auto loaded = document.get(ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "page " + std::to_string(index + 1) + " cannot be read");
  }
  Page page{.ref = ref, .dict = *(*loaded)->as_dict(), .to_view = {}, .to_user = {}};
  // The same geometry as a rendering (graphics::output_geometry): the crop box, y flipped, /Rotate.
  graphics::DisplayList shape;
  shape.media_box = rect_of(inherited(document, page.dict, "MediaBox")).value_or(Rect{0, 0, 612, 792});
  const Rect crop = rect_of(inherited(document, page.dict, "CropBox")).value_or(shape.media_box);
  shape.crop_box = {std::max(crop.x0, shape.media_box.x0), std::max(crop.y0, shape.media_box.y0), std::min(crop.x1, shape.media_box.x1),
                    std::min(crop.y1, shape.media_box.y1)};
  if (shape.crop_box.width() <= 0 || shape.crop_box.height() <= 0) {
    shape.crop_box = shape.media_box;
  }
  if (const auto rotate = inherited(document, page.dict, "Rotate")) {
    const std::int64_t degrees = rotate->as_integer().value_or(0);
    shape.rotate = static_cast<std::int32_t>((((degrees % 360) + 360) % 360) / 90 * 90);
  }
  page.to_view = graphics::output_geometry(shape, 1).page_to_output;
  const auto inverse = page.to_view.inverse();
  if (!inverse) {
    return make_error(ErrorCode::kSyntax, "the page has an empty box");
  }
  page.to_user = *inverse;
  return page;
}

// ── Helvetica, for free text ─────────────────────────────────────────────────────────────────────

// Unicode → WinAnsi code (the encoding the appearance's Helvetica uses), from the glyph names.
const std::map<char32_t, std::uint8_t>& win_ansi() {
  static const std::map<char32_t, std::uint8_t> kTable = [] {
    std::map<char32_t, std::uint8_t> out;
    const auto names = font::detail::encoding_table(font::detail::BaseEncoding::kWinAnsi);
    for (std::size_t code = 32; code < names.size(); ++code) {
      if (const auto unicode = names[code].empty() ? std::nullopt : font::detail::glyph_unicode(names[code])) {
        out.emplace(*unicode, static_cast<std::uint8_t>(code));
      }
    }
    return out;
  }();
  return kTable;
}

// The advance of each WinAnsi code in Helvetica (the built-in substitute's metrics, Adobe's), per em.
const std::array<double, 256>& helvetica_widths() {
  static const std::array<double, 256> kWidths = [] {
    std::array<double, 256> out{};
    out.fill(0.556);
    const auto data = font::detail::standard_font_data(font::detail::StandardFont::kSans);
    auto bytes = std::make_shared<std::vector<std::byte>>(data.size());
    std::ranges::transform(data, bytes->begin(), [](unsigned char c) { return static_cast<std::byte>(c); });
    auto program = font::detail::FontProgram::open(bytes);
    if (!program) {
      return out;
    }
    const auto names = font::detail::encoding_table(font::detail::BaseEncoding::kWinAnsi);
    for (std::size_t code = 0; code < names.size(); ++code) {
      if (names[code].empty()) {
        continue;
      }
      if (const auto advance = (*program)->advance((*program)->glyph_named(names[code]))) {
        out.at(code) = *advance / 1000;
      }
    }
    return out;
  }();
  return kWidths;
}

// Text as WinAnsi codes for Helvetica: characters it lacks become '?'.
std::string win_ansi_bytes(std::u32string_view text) {
  std::string out;
  for (const char32_t c : text) {
    const auto found = win_ansi().find(c);
    out.push_back(static_cast<char>(found != win_ansi().end() ? found->second : '?'));
  }
  return out;
}

double text_width(std::string_view codes, double size) {
  double w = 0;
  for (const char c : codes) {
    w += helvetica_widths().at(static_cast<unsigned char>(c));
  }
  return w * size;
}

// Lines of text wrapped to width at spaces (a word longer than the width is cut).
std::vector<std::string> wrap(std::string_view codes, double size, double width) {
  std::vector<std::string> lines;
  constexpr std::size_t kMaxLines = 1000;
  std::size_t start = 0;
  while (start <= codes.size() && lines.size() < kMaxLines) {
    const std::size_t end = std::min(codes.find('\n', start), codes.size());
    const std::string_view paragraph = codes.substr(start, end - start);
    std::string line;
    std::size_t at = 0;
    while (at < paragraph.size()) {
      std::size_t next = paragraph.find(' ', at);
      next = next == std::string_view::npos ? paragraph.size() : next;
      const std::string word(paragraph.substr(at, next - at));
      std::string candidate = line;
      if (!candidate.empty()) {
        candidate += ' ';
      }
      candidate += word;
      if (text_width(candidate, size) <= width || line.empty()) {
        line = candidate;
        while (line.size() > 1 && text_width(line, size) > width) {  // a word wider than the box
          std::size_t fits = line.size() - 1;
          while (fits > 1 && text_width(std::string_view(line).substr(0, fits), size) > width) {
            --fits;
          }
          lines.push_back(line.substr(0, fits));
          line = line.substr(fits);
        }
      } else {
        lines.push_back(line);
        line = word;
      }
      at = next + 1;
    }
    lines.push_back(line);
    start = end + 1;
  }
  return lines;
}

std::string hex(std::string_view bytes) {
  std::string out = "<";
  for (const char c : bytes) {
    out += std::format("{:02X}", static_cast<unsigned char>(c));
  }
  return out + ">";
}

Dict helvetica_resources() {
  Dict helvetica;
  helvetica.set("Type", name("Font"));
  helvetica.set("Subtype", name("Type1"));
  helvetica.set("BaseFont", name("Helvetica"));
  helvetica.set("Encoding", name("WinAnsiEncoding"));
  Dict fonts;
  fonts.set("Helv", std::move(helvetica));
  Dict resources;
  resources.set("Font", std::move(fonts));
  return resources;
}

// "/Helv 12 Tf 0 0 1 rg" → size and colour (the parts the editor writes; defaults otherwise).
std::pair<double, Rgb> parse_da(std::string_view da) {
  double size = 12;
  Rgb color{0, 0, 0};
  std::vector<std::string> tokens;
  for (std::size_t at = 0; at < da.size();) {
    const std::size_t end = std::min(da.find(' ', at), da.size());
    if (end > at) {
      tokens.emplace_back(da.substr(at, end - at));
    }
    at = end + 1;
  }
  const auto value = [](const std::string& t) {
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end != nullptr && *end == '\0' && std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
  };
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "Tf" && i >= 1) {
      size = std::clamp(value(tokens[i - 1]).value_or(12), 1.0, 500.0);
    } else if (tokens[i] == "rg" && i >= 3) {
      color = clamp({value(tokens[i - 3]).value_or(0), value(tokens[i - 2]).value_or(0), value(tokens[i - 1]).value_or(0)});
    } else if (tokens[i] == "g" && i >= 1) {
      const double g = value(tokens[i - 1]).value_or(0);
      color = clamp({g, g, g});
    }
  }
  return {size, color};
}

std::string rgb_operands(const Rgb& c) { return num(c[0]) + " " + num(c[1]) + " " + num(c[2]); }

// Content drawn in the page's coordinates as shown (y down), placed into page space.
std::string in_view(const Page& page, const std::string& content) {
  const Matrix& m = page.to_user;
  return std::format("q {} {} {} {} {} {} cm\n{}Q\n", num(m.a), num(m.b), num(m.c), num(m.d), num(m.e), num(m.f), content);
}

// A note's icon in its rectangle as shown: a page of paper in the note's colour with lines of text.
std::string note_icon(const Rect& r, const Rgb& color) {
  const double w = r.width();
  const double h = r.height();
  const double fold = std::min(w, h) * 0.3;
  std::string c = rgb_operands(color) + " rg 0.25 0.25 0.25 RG 1 w 1 j\n";
  c += std::format("{} {} m {} {} l {} {} l {} {} l {} {} l h B\n", num(r.x0 + 1), num(r.y0 + 1), num(r.x1 - 1 - fold), num(r.y0 + 1), num(r.x1 - 1),
                   num(r.y0 + 1 + fold), num(r.x1 - 1), num(r.y1 - 1), num(r.x0 + 1), num(r.y1 - 1));
  c += std::format("{} {} m {} {} l {} {} l S\n", num(r.x1 - 1 - fold), num(r.y0 + 1), num(r.x1 - 1 - fold), num(r.y0 + 1 + fold), num(r.x1 - 1),
                   num(r.y0 + 1 + fold));
  for (int line = 1; line <= 3; ++line) {
    const double y = r.y0 + (h * (0.25 + (0.2 * line)));
    c += std::format("{} {} m {} {} l S\n", num(r.x0 + (w * 0.22)), num(y), num(r.x1 - (w * 0.22)), num(y));
  }
  return c;
}

// Free text in its rectangle as shown: wrapped lines of Helvetica, from the top left.
std::string free_text(const Rect& r, std::u32string_view text, double size, const Rgb& color) {
  constexpr double kPadding = 2;
  const std::string codes = win_ansi_bytes(text);
  std::string c =
      std::format("{} {} {} {} re W n\nBT /Helv {} Tf {} rg\n", num(r.x0), num(r.y0), num(r.width()), num(r.height()), num(size), rgb_operands(color));
  double baseline = r.y0 + kPadding + (size * 0.9);
  for (const std::string& line : wrap(codes, size, std::max(r.width() - (2 * kPadding), size))) {
    if (baseline - size > r.y1) {
      break;
    }
    c += std::format("1 0 0 -1 {} {} Tm {} Tj\n", num(r.x0 + kPadding), num(baseline), hex(line));
    baseline += size * 1.2;
  }
  return c + "ET\n";
}

std::string subtype_of(const Dict& annotation) {
  const Object* s = annotation.find("Subtype");
  return s != nullptr && s->as_name() != nullptr ? s->as_name()->bytes : std::string();
}

bool generated_kind(std::string_view subtype) {
  constexpr std::array<std::string_view, 12> kKinds = {"Highlight", "Underline", "StrikeOut", "Squiggly", "Text",    "FreeText",
                                                       "Ink",       "Square",    "Circle",    "Line",     "Polygon", "PolyLine"};
  return std::ranges::find(kKinds, subtype) != kKinds.end();
}

// The normal appearance the editor makes for an annotation of a kind it knows; nullopt otherwise.
std::optional<model::Stream> appearance_for(Document& document, const Page& page, const Dict& annotation) {
  const std::string subtype = subtype_of(annotation);
  const std::vector<double> r = content::numbers(document, annotation, "Rect");
  if (r.size() != 4 || !generated_kind(subtype)) {
    return std::nullopt;
  }
  const Rect rect = Rect{r[0], r[1], r[2], r[3]}.normalized();
  Dict resources;
  std::string content;
  if (subtype == "Text") {
    // Upright in page space (the reader keeps it upright on the screen: NoRotate): drawn y down in the
    // rectangle's own box, whose top left is /Rect's upper-left corner.
    const std::vector<double> c = content::numbers(document, annotation, "C");
    const std::string icon = note_icon(Rect{0, 0, rect.width(), rect.height()}, c.size() == 3 ? Rgb{c[0], c[1], c[2]} : Rgb{1, 0.82, 0});
    content = std::format("q 1 0 0 -1 {} {} cm\n{}Q\n", num(rect.x0), num(rect.y1), icon);
  } else if (subtype == "FreeText") {
    const Object* da = annotation.find("DA");
    const auto [size, color] = parse_da(da != nullptr && da->as_string() != nullptr ? da->as_string()->bytes : std::string());
    const Object* text = annotation.find("Contents");
    const std::u32string contents = text != nullptr && text->as_string() != nullptr ? model::decode_text_string(text->as_string()->bytes) : std::u32string();
    content = in_view(page, free_text(page.view(rect), contents, size, color));
    resources = helvetica_resources();
  } else {
    content = content::markup_appearance(document, subtype, annotation, rect);
    if (content.empty()) {
      return std::nullopt;
    }
    if (subtype == "Highlight") {
      // Multiplied onto the page, so that the text stays readable under it (as the renderer does).
      Dict multiply;
      multiply.set("BM", name("Multiply"));
      Dict states;
      states.set("GS0", std::move(multiply));
      resources.set("ExtGState", std::move(states));
      content = "/GS0 gs " + content;
    }
  }
  model::Stream stream;
  stream.dict.set("Type", name("XObject"));
  stream.dict.set("Subtype", name("Form"));
  stream.dict.set("BBox", rect_array(rect));  // the content is in page space: /BBox is /Rect, no /Matrix
  stream.dict.set("Resources", std::move(resources));
  std::vector<std::byte> data(content.size());
  std::ranges::transform(content, data.begin(), [](char ch) { return static_cast<std::byte>(ch); });
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(data));
  return stream;
}

// Writes the appearance (a new object) into the annotation; false when the editor cannot make one.
Result<bool> set_appearance(Document& document, const Page& page, Dict& annotation) {
  auto stream = appearance_for(document, page, annotation);
  if (!stream) {
    return false;
  }
  auto ref = document.add(Object(std::move(*stream)));
  if (!ref) {
    return std::unexpected(std::move(ref.error()));
  }
  Dict ap;
  ap.set("N", *ref);
  annotation.set("AP", std::move(ap));
  annotation.erase("AS");
  return true;
}

// The page's /Annots as an array, and where it lives (the page, or its own object).
struct Annots {
  Array list;
  std::optional<ObjectRef> ref;
};

Annots annots_of(Document& document, const Page& page) {
  const Object* entry = page.dict.find("Annots");
  if (entry == nullptr) {
    return {};
  }
  auto resolved = document.resolve(*entry);
  return {.list = resolved && resolved->as_array() != nullptr ? *resolved->as_array() : Array{}, .ref = entry->as_ref()};
}

Result<void> write_annots(Document& document, const Page& page, Annots annots) {
  if (annots.ref) {
    return document.set(annots.ref->number, Object(std::move(annots.list)));
  }
  Dict dict = page.dict;
  if (annots.list.empty()) {
    dict.erase("Annots");
  } else {
    dict.set("Annots", std::move(annots.list));
  }
  return document.set(page.ref.number, Object(std::move(dict)));
}

Result<Dict> annotation_of(Document& document, const Page& page, ObjectRef ref) {
  const Annots annots = annots_of(document, page);
  if (std::ranges::none_of(annots.list, [&](const Object& o) { return o.as_ref() == std::optional<ObjectRef>(ref); })) {
    return make_error(ErrorCode::kOutOfRange, std::format("annotation {} {} R is not on this page", ref.number, ref.generation));
  }
  auto loaded = document.get(ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the annotation cannot be read");
  }
  return *(*loaded)->as_dict();
}

std::string utf8_of(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_string() != nullptr ? model::to_utf8(model::decode_text_string(value->as_string()->bytes)) : std::string();
}

// Moves the coordinates of an annotation's geometry by (dx, dy) in page space.
void shift(Document& document, Dict& annotation, double dx, double dy) {
  for (const std::string_view key : {"Rect", "QuadPoints", "L", "Vertices", "CL"}) {
    std::vector<double> v = content::numbers(document, annotation, key);
    if (v.empty()) {
      continue;
    }
    Array moved;
    for (std::size_t i = 0; i < v.size(); ++i) {
      moved.emplace_back(number_object(v[i] + (i % 2 == 0 ? dx : dy)));
    }
    annotation.set(std::string(key), std::move(moved));
  }
  if (const Object* ink = annotation.find("InkList")) {
    auto resolved = document.resolve(*ink);
    if (resolved && resolved->as_array() != nullptr) {
      Array strokes;
      for (const Object& stroke : *resolved->as_array()) {
        auto points = document.resolve(stroke);
        Array moved;
        if (points && points->as_array() != nullptr) {
          for (std::size_t i = 0; i < points->as_array()->size(); ++i) {
            moved.emplace_back(number_object((*points->as_array())[i].as_number().value_or(0) + (i % 2 == 0 ? dx : dy)));
          }
        }
        strokes.emplace_back(std::move(moved));
      }
      annotation.set("InkList", std::move(strokes));
    }
  }
}

std::string_view subtype_name(AnnotationKind kind) {
  switch (kind) {
    case AnnotationKind::kHighlight:
      return "Highlight";
    case AnnotationKind::kUnderline:
      return "Underline";
    case AnnotationKind::kStrikeOut:
      return "StrikeOut";
    case AnnotationKind::kSquiggly:
      return "Squiggly";
    case AnnotationKind::kNote:
      return "Text";
    case AnnotationKind::kInk:
      return "Ink";
    case AnnotationKind::kSquare:
      return "Square";
    case AnnotationKind::kCircle:
      return "Circle";
    case AnnotationKind::kLine:
      return "Line";
    case AnnotationKind::kFreeText:
      return "FreeText";
  }
  return "Text";
}

bool valid(const Rect& r) {
  return std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1) && r.width() > 0 && r.height() > 0;
}

bool valid(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }

}  // namespace

Result<std::vector<AnnotationInfo>> list_annotations(Document& document, std::uint32_t page_index) {
  auto page = page_at(document, page_index, false);
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  std::vector<AnnotationInfo> out;
  for (const Object& entry : annots_of(document, *page).list) {
    auto annotation = document.resolve(entry);
    const Dict* dict = annotation ? annotation->as_dict() : nullptr;
    if (dict == nullptr) {
      continue;
    }
    const std::string subtype = subtype_of(*dict);
    const std::vector<double> r = content::numbers(document, *dict, "Rect");
    if (subtype == "Popup" || r.size() != 4) {
      continue;
    }
    const std::vector<double> c = content::numbers(document, *dict, "C");
    const Object* flags = dict->find("F");
    const std::int64_t f = flags != nullptr ? flags->as_integer().value_or(0) : 0;
    Rect shown = page->view(Rect{r[0], r[1], r[2], r[3]}.normalized());
    if ((f & kNoRotateFlag) != 0) {
      // Upright on the screen from the upper-left corner of /Rect (see the renderer).
      const Rect user = Rect{r[0], r[1], r[2], r[3]}.normalized();
      const Point corner = page->to_view.apply(Point{user.x0, user.y1});
      shown = Rect{corner.x, corner.y, corner.x + user.width(), corner.y + user.height()};
    }
    out.push_back(AnnotationInfo{
        .ref = entry.as_ref(),
        .subtype = subtype,
        .rect = shown,
        .contents = utf8_of(*dict, "Contents"),
        .author = utf8_of(*dict, "T"),
        .color = c.size() == 3 ? std::optional<Rgb>(Rgb{c[0], c[1], c[2]}) : std::nullopt,
        .hidden = (f & (kHiddenFlag | kNoViewFlag)) != 0,
        .editable = entry.as_ref().has_value() && subtype != "Widget",
    });
  }
  return out;
}

Result<model::ObjectRef> add_annotation(Document& document, std::uint32_t page_index, const AnnotationSpec& spec) {
  auto page = page_at(document, page_index);
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  const double width = std::isfinite(spec.width) ? std::clamp(spec.width, 0.0, 100.0) : 1.0;
  Dict a;
  a.set("Type", name("Annot"));
  a.set("Subtype", name(std::string(subtype_name(spec.kind))));
  a.set("P", page->ref);
  std::int64_t flags = kPrintFlag;
  Rect rect;
  switch (spec.kind) {
    case AnnotationKind::kHighlight:
    case AnnotationKind::kUnderline:
    case AnnotationKind::kStrikeOut:
    case AnnotationKind::kSquiggly: {
      Array quads;
      std::vector<Point> corners;
      for (const Rect& q : spec.quads) {
        if (!valid(q)) {
          continue;
        }
        // Upper left, upper right, lower left, lower right as the text reads (as Acrobat writes them).
        for (const Point p : {Point{q.x0, q.y0}, Point{q.x1, q.y0}, Point{q.x0, q.y1}, Point{q.x1, q.y1}}) {
          const Point u = page->user(p);
          corners.push_back(u);
          quads.emplace_back(number_object(u.x));
          quads.emplace_back(number_object(u.y));
        }
      }
      if (corners.empty()) {
        return make_error(ErrorCode::kUnsupported, "text markup needs boxes of text");
      }
      rect = bounds(corners);
      a.set("QuadPoints", std::move(quads));
      break;
    }
    case AnnotationKind::kInk: {
      Array strokes;
      std::vector<Point> all;
      for (const auto& stroke : spec.strokes) {
        Array points;
        for (const Point p : stroke) {
          if (!valid(p)) {
            continue;
          }
          const Point u = page->user(p);
          all.push_back(u);
          points.emplace_back(number_object(u.x));
          points.emplace_back(number_object(u.y));
        }
        if (!points.empty()) {
          strokes.emplace_back(std::move(points));
        }
      }
      if (all.empty() || width <= 0) {
        return make_error(ErrorCode::kUnsupported, "ink needs a stroke and a width");
      }
      rect = grow(bounds(all), width);
      a.set("InkList", std::move(strokes));
      break;
    }
    case AnnotationKind::kLine: {
      if (!valid(spec.from) || !valid(spec.to) || width <= 0) {
        return make_error(ErrorCode::kUnsupported, "a line needs two points and a width");
      }
      const Point from = page->user(spec.from);
      const Point to = page->user(spec.to);
      const std::array<Point, 2> ends = {from, to};
      rect = grow(bounds(ends), width);
      a.set("L", Array{number_object(from.x), number_object(from.y), number_object(to.x), number_object(to.y)});
      break;
    }
    case AnnotationKind::kNote:
    case AnnotationKind::kSquare:
    case AnnotationKind::kCircle:
    case AnnotationKind::kFreeText: {
      if (!valid(spec.rect)) {
        return make_error(ErrorCode::kUnsupported, "this annotation needs a rectangle");
      }
      rect = page->user(spec.rect);
      if (spec.kind == AnnotationKind::kNote) {
        // As Acrobat writes notes: NoZoom and NoRotate (§12.5.3; readers keep notes so even without the
        // flags), the icon drawn upright in page space. A reader shows it upright on the screen with the
        // upper-left corner of /Rect where it is: that corner goes where the note was put.
        const Point corner = page->user(Point{spec.rect.x0, spec.rect.y0});
        rect = Rect{corner.x, corner.y - spec.rect.height(), corner.x + spec.rect.width(), corner.y};
        a.set("Name", name("Comment"));
        flags |= kNoZoomFlag | kNoRotateFlag;
      }
      break;
    }
  }
  a.set("Rect", rect_array(rect));
  if (spec.kind == AnnotationKind::kFreeText) {
    const double size = std::isfinite(spec.font_size) ? std::clamp(spec.font_size, 1.0, 500.0) : 12;
    a.set("DA", model::String{.bytes = std::format("/Helv {} Tf {} rg", num(size), rgb_operands(clamp(spec.color)))});
  } else {
    a.set("C", rgb_array(clamp(spec.color)));
  }
  if (spec.fill && (spec.kind == AnnotationKind::kSquare || spec.kind == AnnotationKind::kCircle)) {
    a.set("IC", rgb_array(clamp(*spec.fill)));
  }
  if (spec.kind == AnnotationKind::kInk || spec.kind == AnnotationKind::kLine || spec.kind == AnnotationKind::kSquare || spec.kind == AnnotationKind::kCircle) {
    Dict border;
    border.set("W", number_object(width));
    border.set("S", name("S"));
    a.set("BS", std::move(border));
  }
  if (std::isfinite(spec.opacity) && spec.opacity < 1) {
    a.set("CA", number_object(std::clamp(spec.opacity, 0.0, 1.0)));
  }
  if (!spec.contents.empty()) {
    a.set("Contents", model::String{.bytes = model::encode_text_string(model::from_utf8(spec.contents))});
  }
  if (!spec.author.empty()) {
    a.set("T", model::String{.bytes = model::encode_text_string(model::from_utf8(spec.author))});
  }
  if (!spec.date.empty()) {
    a.set("M", model::String{.bytes = spec.date});
    a.set("CreationDate", model::String{.bytes = spec.date});
  }
  a.set("F", flags);
  // A name unique in the document (§12.5.2 /NM): the object number it will have is.
  a.set("NM", model::String{.bytes = std::format("capriole-{}", document.next_object_number())});
  auto ref = document.add(Object());
  if (!ref) {
    return ref;
  }
  if (auto made = set_appearance(document, *page, a); !made) {
    return std::unexpected(std::move(made.error()));
  }
  if (auto set = document.set(ref->number, Object(std::move(a))); !set) {
    return std::unexpected(std::move(set.error()));
  }
  Annots annots = annots_of(document, *page);
  annots.list.emplace_back(*ref);
  if (auto written = write_annots(document, *page, std::move(annots)); !written) {
    return std::unexpected(std::move(written.error()));
  }
  return *ref;
}

Result<void> update_annotation(Document& document, std::uint32_t page_index, model::ObjectRef ref, const AnnotationChange& change) {
  auto page = page_at(document, page_index);
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto annotation = annotation_of(document, *page, ref);
  if (!annotation) {
    return std::unexpected(std::move(annotation.error()));
  }
  const std::string subtype = subtype_of(*annotation);
  if (subtype == "Widget") {
    return make_error(ErrorCode::kUnsupported, "form widgets are changed through their fields");
  }
  Dict a = std::move(*annotation);
  bool regenerate = false;
  if (change.move_by) {
    if (!valid(*change.move_by)) {
      return make_error(ErrorCode::kUnsupported, "a move needs a finite offset");
    }
    // An offset as shown, in page space: the linear part of the inverse geometry.
    const Matrix& m = page->to_user;
    const double dx = (m.a * change.move_by->x) + (m.c * change.move_by->y);
    const double dy = (m.b * change.move_by->x) + (m.d * change.move_by->y);
    shift(document, a, dx, dy);
    // The appearance follows /Rect; one of the editor's own, drawn in page space, is made again there.
    regenerate = generated_kind(subtype) && a.contains("AP");
  }
  if (change.contents) {
    if (change.contents->empty()) {
      a.erase("Contents");
    } else {
      a.set("Contents", model::String{.bytes = model::encode_text_string(model::from_utf8(*change.contents))});
    }
    regenerate = regenerate || subtype == "FreeText";
  }
  if (change.color) {
    if (!generated_kind(subtype)) {
      return make_error(ErrorCode::kUnsupported, "the colour of a /" + subtype + " annotation is in its own appearance");
    }
    if (subtype == "FreeText") {
      const Object* da = a.find("DA");
      const double size = parse_da(da != nullptr && da->as_string() != nullptr ? da->as_string()->bytes : std::string()).first;
      a.set("DA", model::String{.bytes = std::format("/Helv {} Tf {} rg", num(size), rgb_operands(clamp(*change.color)))});
    } else {
      a.set("C", rgb_array(clamp(*change.color)));
    }
    regenerate = true;
  }
  if (change.opacity && std::isfinite(*change.opacity)) {
    if (*change.opacity < 1) {
      a.set("CA", number_object(std::clamp(*change.opacity, 0.0, 1.0)));
    } else {
      a.erase("CA");
    }
  }
  if (!change.date.empty()) {
    a.set("M", model::String{.bytes = change.date});
  }
  if (regenerate) {
    if (auto made = set_appearance(document, *page, a); !made) {
      return std::unexpected(std::move(made.error()));
    }
  }
  return document.set(ref.number, Object(std::move(a)));
}

Result<void> remove_annotation(Document& document, std::uint32_t page_index, model::ObjectRef ref) {
  auto page = page_at(document, page_index);
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto annotation = annotation_of(document, *page, ref);
  if (!annotation) {
    return std::unexpected(std::move(annotation.error()));
  }
  if (subtype_of(*annotation) == "Widget") {
    return make_error(ErrorCode::kUnsupported, "form widgets are removed with their fields");
  }
  std::optional<ObjectRef> popup;
  if (const Object* p = annotation->find("Popup")) {
    popup = p->as_ref();
  }
  Annots annots = annots_of(document, *page);
  std::erase_if(annots.list, [&](const Object& o) {
    const auto r = o.as_ref();
    return r == std::optional<ObjectRef>(ref) || (popup && r == popup);
  });
  if (auto written = write_annots(document, *page, std::move(annots)); !written) {
    return written;
  }
  // A popup that cannot be removed is gone already: nothing to do.
  if (popup && !document.remove(popup->number)) {
    popup.reset();
  }
  return document.remove(ref.number);
}

}  // namespace capriole::edit
