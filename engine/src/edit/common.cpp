#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/text_string.hpp"
#include "font/glyph_tables.hpp"
#include "font/program.hpp"
#include "font/standard_fonts.hpp"
#include "sound.hpp"

namespace capriole::edit::detail {

using graphics::Point;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

namespace {

constexpr std::size_t kMaxParentChain = 256;

// Unicode → WinAnsi code (the encoding the appearances' Helvetica uses), from the glyph names.
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

}  // namespace

model::Name name(std::string text) { return model::Name{.bytes = std::move(text)}; }

std::string num(double v) {
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

std::string rgb_operands(const Rgb& c) { return num(c[0]) + " " + num(c[1]) + " " + num(c[2]); }

Rect bounds(std::span<const Point> points) {
  Rect r{points.front().x, points.front().y, points.front().x, points.front().y};
  for (const Point p : points) {
    r = {std::min(r.x0, p.x), std::min(r.y0, p.y), std::max(r.x1, p.x), std::max(r.y1, p.y)};
  }
  return r;
}

Rect transformed(const Rect& r, const graphics::Matrix& m) {
  const std::array<Point, 4> corners = {m.apply(Point{r.x0, r.y0}), m.apply(Point{r.x1, r.y0}), m.apply(Point{r.x0, r.y1}), m.apply(Point{r.x1, r.y1})};
  return bounds(corners);
}

std::optional<Rect> path_bounds(const graphics::Path& path) {
  std::vector<Point> points;
  path.for_each([&](graphics::SegmentKind kind, const Point* at) {
    for (std::size_t i = 0; i < graphics::Path::points_of(kind); ++i) {
      points.push_back(at[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): the path's own points
    }
  });
  if (points.empty()) {
    return std::nullopt;
  }
  return bounds(points);
}

Rect grow(const Rect& r, double by) { return {r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by}; }

std::string hex(std::string_view bytes) {
  std::string out = "<";
  for (const char c : bytes) {
    out += std::format("{:02X}", static_cast<unsigned char>(c));
  }
  return out + ">";
}

Rect Page::user(const Rect& r) const {
  const std::array<Point, 4> corners = {user(Point{r.x0, r.y0}), user(Point{r.x1, r.y0}), user(Point{r.x0, r.y1}), user(Point{r.x1, r.y1})};
  return bounds(corners);
}

Rect Page::view(const Rect& r) const {
  const std::array<Point, 4> corners = {to_view.apply(Point{r.x0, r.y0}), to_view.apply(Point{r.x1, r.y0}), to_view.apply(Point{r.x0, r.y1}),
                                        to_view.apply(Point{r.x1, r.y1})};
  return bounds(corners);
}

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

Result<Page> page_at(Document& document, std::uint32_t index, std::optional<std::string_view> edit) {
  auto pages = document.page_refs();
  if (!pages) {
    return std::unexpected(std::move(pages.error()));
  }
  if (auto sound = edit ? require_sound_structure(document, *edit) : Result<void>(); !sound) {
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

std::string subtype_of(const Dict& annotation) {
  const Object* s = annotation.find("Subtype");
  return s != nullptr && s->as_name() != nullptr ? s->as_name()->bytes : std::string();
}

std::string utf8_of(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_string() != nullptr ? model::to_utf8(model::decode_text_string(value->as_string()->bytes)) : std::string();
}

// ── Simple fonts ─────────────────────────────────────────────────────────────────────────────────

const Metrics& helvetica() {
  static const Metrics kMetrics = [] {
    Metrics out;
    out.widths.fill(0.556);
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
        out.widths.at(code) = *advance / 1000;
      }
    }
    return out;
  }();
  return kMetrics;
}

std::string win_ansi_bytes(std::u32string_view text) {
  std::string out;
  for (const char32_t c : text) {
    const auto found = win_ansi().find(c);
    if (c == U'\n') {
      out.push_back('\n');
    } else {
      out.push_back(static_cast<char>(found != win_ansi().end() ? found->second : '?'));
    }
  }
  return out;
}

Dict helvetica_font() {
  Dict font;
  font.set("Type", name("Font"));
  font.set("Subtype", name("Type1"));
  font.set("BaseFont", name("Helvetica"));
  font.set("Encoding", name("WinAnsiEncoding"));
  Dict fonts;
  fonts.set("Helv", std::move(font));
  return fonts;
}

double text_width(std::string_view codes, double size, const Metrics& metrics) {
  double w = 0;
  for (const char c : codes) {
    w += metrics.widths.at(static_cast<unsigned char>(c));
  }
  return w * size;
}

std::vector<std::string> wrap(std::string_view codes, double size, double width, const Metrics& metrics) {
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
      if (text_width(candidate, size, metrics) <= width || line.empty()) {
        line = candidate;
        // A word wider than the box is cut where it stops fitting (in one pass: values come from users).
        std::size_t begin = 0;
        while (line.size() - begin > 1 && text_width(std::string_view(line).substr(begin), size, metrics) > width && lines.size() < kMaxLines) {
          double used = 0;
          std::size_t fits = begin;
          while (fits < line.size() && (fits == begin || used + (metrics.widths.at(static_cast<unsigned char>(line[fits])) * size) <= width)) {
            used += metrics.widths.at(static_cast<unsigned char>(line[fits])) * size;
            ++fits;
          }
          lines.push_back(line.substr(begin, fits - begin));
          begin = fits;
        }
        line.erase(0, begin);
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

DefaultAppearance parse_da(std::string_view da) {
  // A fragment of content (§12.7.4.3), read as content is — the lexer's white-space, names and numbers
  // —, tolerantly; the last Tf and the last colour operator count. Bounded: a /DA is a few operators.
  constexpr std::size_t kMaxLength = 4096;
  constexpr std::size_t kMaxOperations = 64;
  DefaultAppearance out;
  const std::string_view text = da.substr(0, kMaxLength);
  const auto bytes = std::as_bytes(std::span(text));
  content::ContentParser parser(bytes, kDefaultParseLimits, parser::ParseMode::kTolerant);
  const auto unit = [](const Object& o) { return std::clamp(o.as_number().value_or(0), 0.0, 1.0); };
  const auto numbers = [](const std::vector<Object>& operands, std::size_t count) {
    return operands.size() == count && std::ranges::all_of(operands, [](const Object& o) { return o.as_number().has_value(); });
  };
  for (std::size_t i = 0; i < kMaxOperations; ++i) {
    auto next = parser.next();
    if (!next || !*next) {
      break;
    }
    const content::Operation& op = **next;
    const auto& v = op.operands;
    const std::optional<double> tf_size = op.name == "Tf" && v.size() == 2 ? v[1].as_number() : std::nullopt;
    if (tf_size && v[0].as_name() != nullptr) {
      out.font = v[0].as_name()->bytes;
      out.size = std::isfinite(*tf_size) ? std::clamp(*tf_size, 0.0, 500.0) : 0;
    } else if (op.name == "g" && numbers(v, 1)) {
      out.rgb = {unit(v[0]), unit(v[0]), unit(v[0])};
      out.color = num(out.rgb[0]) + " g";
    } else if (op.name == "rg" && numbers(v, 3)) {
      out.rgb = {unit(v[0]), unit(v[1]), unit(v[2])};
      out.color = rgb_operands(out.rgb) + " rg";
    } else if (op.name == "k" && numbers(v, 4)) {
      const std::array<double, 4> c = {unit(v[0]), unit(v[1]), unit(v[2]), unit(v[3])};
      out.color = std::format("{} {} {} {} k", num(c[0]), num(c[1]), num(c[2]), num(c[3]));
      out.rgb = {(1 - c[0]) * (1 - c[3]), (1 - c[1]) * (1 - c[3]), (1 - c[2]) * (1 - c[3])};
    }
  }
  return out;
}

model::Stream form_xobject(const Rect& bbox, Dict resources, std::string_view content, const std::optional<graphics::Matrix>& matrix) {
  model::Stream stream;
  stream.dict.set("Type", name("XObject"));
  stream.dict.set("Subtype", name("Form"));
  stream.dict.set("BBox", rect_array(bbox));
  if (matrix) {
    stream.dict.set("Matrix", Array{number_object(matrix->a), number_object(matrix->b), number_object(matrix->c), number_object(matrix->d),
                                    number_object(matrix->e), number_object(matrix->f)});
  }
  stream.dict.set("Resources", std::move(resources));
  std::vector<std::byte> data(content.size());
  std::ranges::transform(content, data.begin(), [](char ch) { return static_cast<std::byte>(ch); });
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(data));
  return stream;
}

std::string in_view(const Page& page, const std::string& content) {
  const graphics::Matrix& m = page.to_user;
  return std::format("q {} {} {} {} {} {} cm\n{}Q\n", num(m.a), num(m.b), num(m.c), num(m.d), num(m.e), num(m.f), content);
}

Annots annots_of(Document& document, const Page& page) {
  const model::Object* entry = page.dict.find("Annots");
  if (entry == nullptr) {
    return {};
  }
  auto resolved = document.resolve(*entry);
  const bool array = resolved && resolved->as_array() != nullptr;
  // The array's own object, when the page's reference reaches it (a reference whose generation does not
  // match reads as null, §7.3.10: written there, the change would be out of reach).
  return {.list = array ? *resolved->as_array() : model::Array{}, .ref = array ? entry->as_ref() : std::nullopt};
}

Result<void> write_annots(Document& document, const Page& page, Annots annots) {
  if (annots.ref) {
    return document.set(annots.ref->number, model::Object(std::move(annots.list)));
  }
  model::Dict dict = page.dict;
  if (annots.list.empty()) {
    dict.erase("Annots");
  } else {
    dict.set("Annots", std::move(annots.list));
  }
  return document.set(page.ref.number, model::Object(std::move(dict)));
}

bool lowercase(char32_t c) {
  if (c >= U'a' && c <= U'z') {
    return true;
  }
  if (c >= 0xDF && c <= 0xFF) {
    return c != 0xF7;  // ß…ÿ, but ÷
  }
  if (c >= 0x100 && c <= 0x17F) {
    // Latin Extended-A pairs its letters, the lowercase odd — but where the pairs shift by one.
    const bool shifted = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
    return c == 0x138 || c == 0x149 || c == 0x17F || (shifted ? c % 2 == 0 : c % 2 == 1);
  }
  return (c >= 0x3B1 && c <= 0x3C9) || (c >= 0x430 && c <= 0x45F);
}

}  // namespace capriole::edit::detail
