#include "capriole/edit/redact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/content/appearance.hpp"
#include "capriole/content/content_parser.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/edit/annotations.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/writer/serializer.hpp"
#include "common.hpp"
#include "content_rewrite.hpp"
#include "sound.hpp"
#include "text_removal.hpp"

namespace capriole::edit {

namespace {

using detail::add_content;
using detail::fresh_name;
using detail::name;
using detail::num;
using detail::op_text;
using detail::operand_text;
using detail::path_bounds;
using detail::transformed;
using detail::with_xobjects;
using graphics::Matrix;
using graphics::Point;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;
using Rgb = std::array<double, 3>;

constexpr std::size_t kMaxAreas = 10'000;
// A glyph with this much of its box in an area (or its centre) leaves its string.
constexpr double kGlyphShare = 0.2;
// A path lying at least this much (of its bounds) in an area is not painted any more.
constexpr double kPathShare = 0.5;
// A path lying this much in the areas is not painted, however large.
constexpr double kPathAll = 0.9;

struct Area {
  Rect rect;  // as shown
  std::optional<Rgb> fill;
};

bool finite(const Rect& r) { return std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1); }

double area_of(const Rect& r) { return std::max(0.0, r.width()) * std::max(0.0, r.height()); }

Rect intersection(const Rect& a, const Rect& b) { return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)}; }

bool meets(const Rect& a, const Rect& b) {
  const Rect i = intersection(a, b);
  return i.width() > 0 && i.height() > 0;
}

bool inside(Point p, const Rect& r) { return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1; }

Rect bounds_of(std::span<const Point> points) {
  Rect r{points.front().x, points.front().y, points.front().x, points.front().y};
  for (const Point p : points) {
    r = {std::min(r.x0, p.x), std::min(r.y0, p.y), std::max(r.x1, p.x), std::max(r.y1, p.y)};
  }
  return r;
}

// ── What changes in each content stream ─────────────────────────────────────────────────────────

struct TextCut {
  std::uint32_t byte = 0;    // where the glyph's code starts in the operator's strings, joined
  std::uint32_t length = 0;  // the code's bytes
  double advance = 0;        // the displacement it made, in text space
};

struct OpEdit {
  std::vector<TextCut> cuts;  // a text-showing operator: glyphs out
  double size = 0;            // its font size, horizontal scale, writing mode (for the displacements)
  double hscale = 1;
  bool vertical = false;
  bool unpaint = false;                     // a path-painting operator becomes n
  std::string clip;                         // or: the path is painted clipped to outside the areas (this clip, in its space)
  bool drop = false;                        // an image (Do, inline) goes
  std::optional<std::string> replace_name;  // Do this XObject instead (an image written anew, a form copied)
};

using Edits = std::map<std::uint32_t, OpEdit>;  // by operator ordinal

// ── Images painted over ───────────────────────────────────────────────────────────────────────

// Which pixels of an image (w × h, its first row at the top of its unit square) lie in an area: the
// area's index, or -1.
std::vector<int> covered(std::size_t w, std::size_t h, const Matrix& unit_to_view, const std::vector<Area>& areas) {
  std::vector<int> out(w * h, -1);
  for (std::size_t row = 0; row < h; ++row) {
    for (std::size_t col = 0; col < w; ++col) {
      const Point p =
          unit_to_view.apply(Point{(static_cast<double>(col) + 0.5) / static_cast<double>(w), 1 - ((static_cast<double>(row) + 0.5) / static_cast<double>(h))});
      const auto hit = std::ranges::find_if(areas, [&](const Area& a) { return inside(p, a.rect); });
      if (hit != areas.end()) {
        out[(row * w) + col] = static_cast<int>(hit - areas.begin());
      }
    }
  }
  return out;
}

// An image XObject written anew in its own colour space: 8-bit samples of DeviceGray, DeviceRGB,
// DeviceCMYK or an ICC space of 1, 3 or 4 components, without /Decode or a /Mask — the pixels in the
// areas painted with the fill (in that space; white when none), the rest as they were, so that every
// reader shows it as before. Its soft mask, when it has one, is written anew the same way, opaque in the
// areas (so that the fill shows, and the shape of what was there does not stay). nullopt for another
// image (cleared_image writes it in RGB).
std::optional<Object> cleared_native(Document& document, ObjectRef ref, const Matrix& unit_to_view, const std::vector<Area>& areas, std::size_t depth = 0) {
  auto loaded = document.get(ref);
  const model::Stream* stream = loaded && *loaded != nullptr ? (*loaded)->as_stream() : nullptr;
  if (stream == nullptr) {
    return std::nullopt;
  }
  const Dict& d = stream->dict;
  for (const std::string_view key : {"Decode", "Mask", "ImageMask", "SMaskInData"}) {
    if (d.contains(key)) {
      return std::nullopt;
    }
  }
  // The soft mask (§11.6.5.3): written anew too, opaque in the areas.
  std::optional<ObjectRef> soft_mask;
  if (const Object* smask = d.find("SMask")) {
    const auto smask_ref = smask->as_ref();
    if (!smask_ref || depth > 0) {
      return std::nullopt;
    }
    std::vector<Area> opaque = areas;
    for (Area& a : opaque) {
      a.fill = Rgb{1, 1, 1};
    }
    auto cleared = cleared_native(document, *smask_ref, unit_to_view, opaque, depth + 1);
    if (!cleared) {
      return std::nullopt;
    }
    auto added = document.add(std::move(*cleared));
    if (!added) {
      return std::nullopt;
    }
    soft_mask = *added;
  }
  const Object* bpc = d.find("BitsPerComponent");
  const Object* filter = d.find("Filter");
  auto filters = filter != nullptr ? document.resolve(*filter) : Result<Object>(Object());
  const auto names_jpx = [&](const Object& o) { return o.as_name() != nullptr && o.as_name()->bytes == "JPXDecode"; };
  if (bpc == nullptr || bpc->as_integer().value_or(0) != 8 ||
      (filters && (names_jpx(*filters) || (filters->as_array() != nullptr && std::ranges::any_of(*filters->as_array(), names_jpx))))) {
    return std::nullopt;
  }
  // The number of components of its colour space.
  std::size_t n = 0;
  const Object* space_entry = d.find("ColorSpace");
  auto space = space_entry != nullptr ? document.resolve(*space_entry) : Result<Object>(Object());
  if (space && space->as_name() != nullptr) {
    const std::string& s = space->as_name()->bytes;
    if (s == "DeviceGray") {
      n = 1;
    } else if (s == "DeviceRGB") {
      n = 3;
    } else if (s == "DeviceCMYK") {
      n = 4;
    }
  } else if (space && space->as_array() != nullptr && space->as_array()->size() == 2 && (*space->as_array())[0].as_name() != nullptr &&
             (*space->as_array())[0].as_name()->bytes == "ICCBased") {
    auto profile = document.resolve((*space->as_array())[1]);
    const Object* count = profile && profile->as_stream() != nullptr ? profile->as_stream()->dict.find("N") : nullptr;
    const std::int64_t components = count != nullptr ? count->as_integer().value_or(0) : 0;
    n = components == 1 || components == 3 || components == 4 ? static_cast<std::size_t>(components) : 0;
  }
  const std::int64_t width = d.find("Width") != nullptr ? d.find("Width")->as_integer().value_or(0) : 0;
  const std::int64_t height = d.find("Height") != nullptr ? d.find("Height")->as_integer().value_or(0) : 0;
  if (n == 0 || width <= 0 || height <= 0) {
    return std::nullopt;
  }
  auto samples = document.decode_stream(*stream);
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  if (!samples || samples->size() < w * h * n) {
    return std::nullopt;
  }
  std::vector<std::byte> data(samples->begin(), samples->begin() + static_cast<std::ptrdiff_t>(w * h * n));
  const std::vector<int> hits = covered(w, h, unit_to_view, areas);
  for (std::size_t i = 0; i < w * h; ++i) {
    if (hits[i] < 0) {
      continue;
    }
    const Rgb c = detail::clamp(areas[static_cast<std::size_t>(hits[i])].fill.value_or(Rgb{1, 1, 1}));
    std::array<double, 4> v{};
    if (n == 1) {
      v[0] = (0.3 * c[0]) + (0.59 * c[1]) + (0.11 * c[2]);
    } else if (n == 3) {
      v = {c[0], c[1], c[2], 0};
    } else {
      const double k = 1 - std::max({c[0], c[1], c[2]});
      const double rest = 1 - k;
      v = {rest > 0 ? (rest - c[0]) / rest : 0, rest > 0 ? (rest - c[1]) / rest : 0, rest > 0 ? (rest - c[2]) / rest : 0, k};
    }
    for (std::size_t k = 0; k < n; ++k) {
      data[(i * n) + k] = static_cast<std::byte>(std::lround(std::clamp(v.at(k), 0.0, 1.0) * 255));
    }
  }
  auto packed = filters::flate_encode(data);
  if (!packed) {
    return std::nullopt;
  }
  model::Stream out;
  for (const std::string_view key : {"Type", "Subtype", "Width", "Height", "ColorSpace", "BitsPerComponent", "Intent", "Interpolate", "Matte"}) {
    if (const Object* value = d.find(key)) {
      out.dict.set(std::string(key), *value);
    }
  }
  if (soft_mask) {
    out.dict.set("SMask", *soft_mask);
  }
  out.dict.set("Filter", name("FlateDecode"));
  out.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*packed));
  return Object(std::move(out));
}

// The image's pixels with those in the areas painted over (the colour, else white; a mask's left
// unpainted), as a new image XObject: in its own colour space when it can (cleared_native), else 8-bit
// DeviceRGB with an /SMask for transparency, or a 1-bit mask.
std::optional<Object> cleared_image(Document& document, const graphics::DrawImage& image, const Matrix& to_view, const std::vector<Area>& areas,
                                    std::optional<ObjectRef>& soft_mask_out) {
  if (image.xobject && !image.mask) {
    if (auto native = cleared_native(document, *image.xobject, image.ctm.then(to_view), areas)) {
      return native;
    }
  }
  const auto& pixels = image.pixels;
  if (!pixels || pixels->width <= 0 || pixels->height <= 0) {
    return std::nullopt;
  }
  const auto w = static_cast<std::size_t>(pixels->width);
  const auto h = static_cast<std::size_t>(pixels->height);
  std::vector<std::uint8_t> rgba = pixels->rgba;
  const Matrix unit_to_view = image.ctm.then(to_view);
  for (std::size_t row = 0; row < h; ++row) {
    for (std::size_t col = 0; col < w; ++col) {
      // The first row is at the top of the unit square (§8.9.4).
      const Point p =
          unit_to_view.apply(Point{(static_cast<double>(col) + 0.5) / static_cast<double>(w), 1 - ((static_cast<double>(row) + 0.5) / static_cast<double>(h))});
      const auto hit = std::ranges::find_if(areas, [&](const Area& a) { return inside(p, a.rect); });
      if (hit == areas.end()) {
        continue;
      }
      const std::size_t i = ((row * w) + col) * 4;
      const Rgb colour = hit->fill.value_or(Rgb{1, 1, 1});
      for (std::size_t c = 0; c < 3; ++c) {
        rgba.at(i + c) = static_cast<std::uint8_t>(std::lround(std::clamp(colour.at(c), 0.0, 1.0) * 255));
      }
      rgba.at(i + 3) = image.mask ? 0 : 255;
    }
  }
  model::Stream stream;
  stream.dict.set("Type", name("XObject"));
  stream.dict.set("Subtype", name("Image"));
  stream.dict.set("Width", static_cast<std::int64_t>(w));
  stream.dict.set("Height", static_cast<std::int64_t>(h));
  std::vector<std::byte> raw;
  if (image.mask) {
    // A stencil mask: 1 bit, 0 painted (the default /Decode [0 1]).
    const std::size_t stride = (w + 7) / 8;
    raw.assign(stride * h, std::byte{0xFF});
    for (std::size_t row = 0; row < h; ++row) {
      for (std::size_t col = 0; col < w; ++col) {
        if (rgba.at((((row * w) + col) * 4) + 3) > 127) {
          raw.at((row * stride) + (col / 8)) &= static_cast<std::byte>(~(0x80U >> (col % 8)));
        }
      }
    }
    stream.dict.set("ImageMask", true);
    stream.dict.set("BitsPerComponent", std::int64_t{1});
  } else {
    raw.reserve(w * h * 3);
    std::vector<std::byte> alpha;
    alpha.reserve(w * h);
    bool opaque = true;
    for (std::size_t i = 0; i < w * h; ++i) {
      raw.push_back(static_cast<std::byte>(rgba[(i * 4)]));
      raw.push_back(static_cast<std::byte>(rgba[(i * 4) + 1]));
      raw.push_back(static_cast<std::byte>(rgba[(i * 4) + 2]));
      alpha.push_back(static_cast<std::byte>(rgba[(i * 4) + 3]));
      opaque = opaque && rgba[(i * 4) + 3] == 255;
    }
    stream.dict.set("ColorSpace", name("DeviceRGB"));
    stream.dict.set("BitsPerComponent", std::int64_t{8});
    if (!opaque) {
      auto packed = filters::flate_encode(alpha);
      if (!packed) {
        return std::nullopt;
      }
      model::Stream mask;
      mask.dict.set("Type", name("XObject"));
      mask.dict.set("Subtype", name("Image"));
      mask.dict.set("Width", static_cast<std::int64_t>(w));
      mask.dict.set("Height", static_cast<std::int64_t>(h));
      mask.dict.set("ColorSpace", name("DeviceGray"));
      mask.dict.set("BitsPerComponent", std::int64_t{8});
      mask.dict.set("Filter", name("FlateDecode"));
      mask.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*packed));
      auto ref = document.add(Object(std::move(mask)));
      if (!ref) {
        return std::nullopt;
      }
      soft_mask_out = *ref;
      stream.dict.set("SMask", *ref);
    }
  }
  auto packed = filters::flate_encode(raw);
  if (!packed) {
    return std::nullopt;
  }
  stream.dict.set("Filter", name("FlateDecode"));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*packed));
  return Object(std::move(stream));
}

// ── Rewriting a content stream ─────────────────────────────────────────────────────────────────

// A text-showing operator without some of its glyphs, as TJ (after T* for ' and ", Tw and Tc for ").
std::string text_without(const content::Operation& op, const OpEdit& edit) {
  std::string prefix;
  std::vector<const Object*> elements;  // the strings and numbers shown
  if (op.name == "TJ" && !op.operands.empty() && op.operands.back().as_array() != nullptr) {
    for (const Object& o : *op.operands.back().as_array()) {
      elements.push_back(&o);
    }
  } else if (!op.operands.empty()) {
    elements.push_back(&op.operands.back());
    if (op.name == "'") {
      prefix = "T* ";
    } else if (op.name == "\"" && op.operands.size() >= 3) {
      prefix = std::format("{} Tw {} Tc T* ", operand_text(op.operands[0]), operand_text(op.operands[1]));
    }
  }
  const double scale = edit.vertical ? edit.size : edit.size * edit.hscale;
  std::string out = prefix + "[";
  std::uint32_t joined = 0;
  auto cut = edit.cuts.begin();
  for (const Object* element : elements) {
    const model::String* string = element->as_string();
    if (string == nullptr) {
      if (element->as_number()) {
        out += operand_text(*element) + " ";
      }
      continue;
    }
    std::string kept;
    const std::string& bytes = string->bytes;
    for (std::uint32_t at = 0; at < bytes.size();) {
      const std::uint32_t position = joined + at;
      while (cut != edit.cuts.end() && cut->byte < position) {
        ++cut;
      }
      if (cut != edit.cuts.end() && cut->byte == position) {
        if (!kept.empty()) {
          out += operand_text(Object(model::String{.bytes = std::exchange(kept, {})})) + " ";
        }
        // The displacement the glyph made, without it (§9.4.3: tx = −(n/1000)·Tfs·Th; vertically ty).
        const double n = scale == 0 ? 0 : (edit.vertical ? 1 : -1) * cut->advance * 1000 / scale;
        out += num(n) + " ";
        at += std::max<std::uint32_t>(cut->length, 1);
        ++cut;
      } else {
        kept.push_back(bytes[at]);
        ++at;
      }
    }
    if (!kept.empty()) {
      out += operand_text(Object(model::String{.bytes = std::move(kept)})) + " ";
    }
    joined += static_cast<std::uint32_t>(bytes.size());
  }
  return out + "] TJ";
}

// The content without what the edits take out. Marked-content sequences in which something changed lose
// the text they carry for it (/ActualText, /Alt, /E in an inline property list).
// What a rewrite leaves: the content, the MCIDs of the marked content that changed, and the XObject names
// the content no longer draws (replaced or dropped, and not drawn elsewhere in it).
struct Rewrite {
  std::string content;
  std::set<std::int64_t> mcids;
  std::set<std::string> unused;
};

Rewrite rewritten(std::span<const std::byte> content, const Edits& edits) {
  Rewrite result;
  std::set<std::int64_t>& mcids = result.mcids;
  std::set<std::string> gone;   // names of Do operators replaced or dropped
  std::set<std::string> drawn;  // names the new content draws
  const std::vector<content::Operation> ops = detail::operations_of(content);
  // Where each painted path starts (its first construction operator), and whether a clip (W, W*) is set
  // on it: a path painted clipped to outside the areas is wrapped in q … Q from there, unless it sets a
  // clip itself (Q would undo it) — it is then painted as it was.
  std::map<std::size_t, std::size_t> path_start;  // painting operator → first construction operator
  std::set<std::size_t> clipping;
  {
    std::optional<std::size_t> begin;
    bool clips = false;
    for (std::size_t i = 0; i < ops.size(); ++i) {
      const std::string& n = ops[i].name;
      if (n == "m" || n == "l" || n == "c" || n == "v" || n == "y" || n == "h" || n == "re") {
        begin = begin.value_or(i);
      } else if (n == "W" || n == "W*") {
        clips = true;
      } else if (n == "f" || n == "F" || n == "f*" || n == "S" || n == "s" || n == "B" || n == "B*" || n == "b" || n == "b*" || n == "n") {
        if (begin) {
          path_start[i] = *begin;
          if (clips) {
            clipping.insert(i);
          }
        }
        begin.reset();
        clips = false;
      }
    }
  }
  std::map<std::size_t, std::string> prefix;
  for (const auto& [index, edit] : edits) {
    const auto start = path_start.find(index);
    if (!edit.clip.empty() && !edit.unpaint && start != path_start.end() && !clipping.contains(index)) {
      prefix[start->second] += "q " + edit.clip + "\n";
    }
  }
  // Which BDC (by ordinal) open a sequence where something changed.
  std::set<std::size_t> touched;
  std::vector<std::size_t> open;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].name == "BDC" || ops[i].name == "BMC") {
      open.push_back(i);
    } else if (ops[i].name == "EMC" && !open.empty()) {
      open.pop_back();
    } else if (const auto e = edits.find(static_cast<std::uint32_t>(i));
               e != edits.end() && (!e->second.cuts.empty() || e->second.unpaint || e->second.drop || e->second.replace_name)) {
      touched.insert(open.begin(), open.end());
    }
  }
  // The marked-content identifiers of the sequences that changed (§14.7.4): their structure elements lose
  // their text too (strip_structure).
  for (const std::size_t i : touched) {
    if (ops[i].name == "BDC" && ops[i].operands.size() == 2 && ops[i].operands[1].as_dict() != nullptr) {
      if (const Object* mcid = ops[i].operands[1].as_dict()->find("MCID"); mcid != nullptr && mcid->as_integer()) {
        mcids.insert(*mcid->as_integer());
      }
    }
  }
  std::string out;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const content::Operation& op = ops[i];
    if (const auto p = prefix.find(i); p != prefix.end()) {
      out += p->second;
    }
    const auto found = edits.find(static_cast<std::uint32_t>(i));
    const bool do_op = op.name == "Do" && !op.operands.empty() && op.operands.back().as_name() != nullptr;
    if (found != edits.end()) {
      const OpEdit& edit = found->second;
      if (do_op && (edit.replace_name || edit.drop)) {
        gone.insert(op.operands.back().as_name()->bytes);
      }
      if (edit.replace_name) {
        drawn.insert(*edit.replace_name);
        out += operand_text(Object(name(*edit.replace_name))) + " Do\n";
      } else if (edit.drop) {
        // gone
      } else if (edit.unpaint) {
        out += "n\n";
      } else if (!edit.clip.empty() && path_start.contains(i) && !clipping.contains(i)) {
        out += op_text(op) + "\nQ\n";
      } else if (!edit.cuts.empty()) {
        out += text_without(op, edit) + "\n";
      } else {
        out += op_text(op) + "\n";
      }
      continue;
    }
    if (op.name == "BDC" && touched.contains(i) && op.operands.size() == 2 && op.operands[1].as_dict() != nullptr) {
      Dict properties = *op.operands[1].as_dict();
      for (const std::string_view key : {"ActualText", "Alt", "E"}) {
        properties.erase(key);
      }
      out += operand_text(op.operands[0]) + " " + operand_text(Object(std::move(properties))) + " BDC\n";
      continue;
    }
    if (do_op) {
      drawn.insert(op.operands.back().as_name()->bytes);
    }
    out += op_text(op) + "\n";
  }
  for (const std::string& n : gone) {
    if (!drawn.contains(n)) {
      result.unused.insert(n);
    }
  }
  result.content = std::move(out);
  return result;
}

// ── The structure tree ────────────────────────────────────────────────────────────────────────

// The value of key in a number tree (§7.9.7), bounded against hostile trees.
std::optional<Object> number_tree_value(Document& document, const Object& node, std::int64_t key, std::size_t depth = 0) {
  if (depth > 32) {
    return std::nullopt;
  }
  auto resolved = document.resolve(node);
  const Dict* dict = resolved && resolved->as_dict() != nullptr ? resolved->as_dict() : nullptr;
  if (dict == nullptr) {
    return std::nullopt;
  }
  if (const Object* nums = dict->find("Nums")) {
    auto list = document.resolve(*nums);
    if (list && list->as_array() != nullptr) {
      const Array& pairs = *list->as_array();
      for (std::size_t i = 0; i + 1 < pairs.size(); i += 2) {
        if (pairs[i].as_integer() == key) {
          return pairs[i + 1];
        }
      }
    }
  }
  if (const Object* kids = dict->find("Kids")) {
    auto list = document.resolve(*kids);
    if (list && list->as_array() != nullptr) {
      for (const Object& kid : *list->as_array()) {
        auto child = document.resolve(kid);
        const Dict* c = child && child->as_dict() != nullptr ? child->as_dict() : nullptr;
        const std::vector<double> limits = c != nullptr ? content::numbers(document, *c, "Limits") : std::vector<double>{};
        if (limits.size() == 2 && (static_cast<double>(key) < limits[0] || static_cast<double>(key) > limits[1])) {
          continue;
        }
        if (auto value = number_tree_value(document, kid, key, depth + 1)) {
          return value;
        }
      }
    }
  }
  return std::nullopt;
}

// The structure elements of a page's marked content that lost glyphs (their MCIDs, through the
// page's /StructParents in the /ParentTree, §14.7.5.4) lose /ActualText, /Alt and /E — and so do their
// ancestors, whose text holds theirs.
Result<void> strip_structure(Document& document, const Dict& page, const std::set<std::int64_t>& mcids) {
  if (mcids.empty()) {
    return {};
  }
  const Object* parents_key = page.find("StructParents");
  auto catalog = document.catalog();
  const Object* root_entry = catalog ? (*catalog)->find("StructTreeRoot") : nullptr;
  auto root = root_entry != nullptr ? document.resolve(*root_entry) : Result<Object>(Object());
  const Object* tree = root && root->as_dict() != nullptr ? root->as_dict()->find("ParentTree") : nullptr;
  const std::optional<std::int64_t> parents = parents_key != nullptr ? parents_key->as_integer() : std::nullopt;
  if (!parents || tree == nullptr) {
    return {};
  }
  const auto value = number_tree_value(document, *tree, *parents);
  auto elements = value ? document.resolve(*value) : Result<Object>(Object());
  if (!elements || elements->as_array() == nullptr) {
    return {};
  }
  std::set<std::uint32_t> done;
  for (const std::int64_t mcid : mcids) {
    if (mcid < 0 || static_cast<std::size_t>(mcid) >= elements->as_array()->size()) {
      continue;
    }
    auto element = (*elements->as_array())[static_cast<std::size_t>(mcid)].as_ref();
    for (std::size_t depth = 0; element && depth < 64 && done.insert(element->number).second; ++depth) {
      auto loaded = document.get(*element);
      const Dict* dict = loaded && *loaded != nullptr ? (*loaded)->as_dict() : nullptr;
      if (dict == nullptr) {
        break;
      }
      const auto parent = dict->find("P") != nullptr ? dict->find("P")->as_ref() : std::nullopt;
      if (dict->contains("ActualText") || dict->contains("Alt") || dict->contains("E")) {
        Dict updated = *dict;
        for (const std::string_view key : {"ActualText", "Alt", "E"}) {
          updated.erase(key);
        }
        if (auto set = document.set(element->number, Object(std::move(updated))); !set) {
          return set;
        }
      }
      element = parent;
    }
  }
  return {};
}

// ── Removing annotations ───────────────────────────────────────────────────────────────────────

// Takes a form widget out of its page's /Annots and out of the form (its parent's /Kids, or /Fields).
Result<void> remove_widget(Document& document, const detail::Page& page, ObjectRef widget) {
  const Object* annots_entry = page.dict.find("Annots");
  auto annots = annots_entry != nullptr ? document.resolve(*annots_entry) : Result<Object>(Object());
  if (annots && annots->as_array() != nullptr) {
    Array list = *annots->as_array();
    std::erase_if(list, [&](const Object& o) { return o.as_ref() == std::optional<ObjectRef>(widget); });
    const auto own = annots_entry->as_ref();
    if (own) {
      if (auto set = document.set(own->number, Object(std::move(list))); !set) {
        return set;
      }
    } else {
      auto current = document.get(page.ref);
      Dict dict = current && (*current)->as_dict() != nullptr ? *(*current)->as_dict() : page.dict;
      dict.set("Annots", std::move(list));
      if (auto set = document.set(page.ref.number, Object(std::move(dict))); !set) {
        return set;
      }
    }
  }
  // Out of the form: the parent's /Kids, else the AcroForm's /Fields.
  auto loaded = document.get(widget);
  const Dict* dict = loaded && *loaded != nullptr ? (*loaded)->as_dict() : nullptr;
  const Object* parent = dict != nullptr ? dict->find("Parent") : nullptr;
  const auto parent_ref = parent != nullptr ? parent->as_ref() : std::nullopt;
  const auto drop_from = [&](ObjectRef holder, std::string_view key) -> Result<void> {
    auto object = document.get(holder);
    if (!object || (*object)->as_dict() == nullptr) {
      return {};
    }
    Dict updated = *(*object)->as_dict();
    const Object* list = updated.find(key);
    auto resolved = list != nullptr ? document.resolve(*list) : Result<Object>(Object());
    if (!resolved || resolved->as_array() == nullptr) {
      return {};
    }
    Array kept = *resolved->as_array();
    std::erase_if(kept, [&](const Object& o) { return o.as_ref() == std::optional<ObjectRef>(widget); });
    updated.set(std::string(key), std::move(kept));
    return document.set(holder.number, Object(std::move(updated)));
  };
  if (parent_ref) {
    return drop_from(*parent_ref, "Kids");
  }
  auto catalog = document.catalog();
  const Object* form = catalog ? (*catalog)->find("AcroForm") : nullptr;
  if (const auto form_ref = form != nullptr ? form->as_ref() : std::nullopt) {
    return drop_from(*form_ref, "Fields");
  }
  return {};
}

// text_only: only the glyphs in the areas leave their strings (edit/text_edit: text written anew) —
// paths, images and annotations stay, and the document is not redacted (its earlier revisions may keep
// the old text: nothing is hidden).
Result<RedactionReport> redact_areas(Document& document, std::uint32_t page_index, const std::vector<Area>& areas,
                                     const std::vector<ObjectRef>& marks_to_remove, bool text_only = false) {
  if (areas.empty()) {
    return make_error(ErrorCode::kUnsupported, "redaction needs an area");
  }
  if (areas.size() > kMaxAreas) {
    return make_error(ErrorCode::kLimitExceeded, "too many areas");
  }
  for (const Area& a : areas) {
    if (!finite(a.rect) || a.rect.width() <= 0 || a.rect.height() <= 0) {
      return make_error(ErrorCode::kUnsupported, "an area must be finite and not empty");
    }
  }
  auto page = detail::page_at(document, page_index, "the page's content");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto list = content::interpret_page(document, page->ref);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  const Matrix& to_view = page->to_view;
  const auto hit = [&](const Rect& view) { return std::ranges::any_of(areas, [&](const Area& a) { return meets(view, a.rect); }); };

  // 1. What each command of the page's content meets.
  RedactionReport report;
  std::map<std::uint32_t, Edits> plan;
  struct ImageJob {
    std::uint32_t invocation;
    std::uint32_t operation;
    std::size_t command;
  };
  std::vector<ImageJob> images;
  const auto& invocations = list->invocations;
  const auto rewritable = [&](std::uint32_t invocation) {
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
  };
  for (std::size_t i = 0; i < list->commands.size() && i < list->origins.size(); ++i) {
    const graphics::Origin origin = list->origins[i];
    if (!rewritable(origin.invocation)) {
      continue;
    }
    const auto& command = list->commands[i];
    if (const auto* boxed = std::get_if<graphics::Boxed<graphics::ShowText>>(&command)) {
      const graphics::ShowText& run = **boxed;
      for (std::size_t g = 0; g < run.glyphs.size(); ++g) {
        const graphics::Glyph& glyph = run.glyphs[g];
        const Rect box = run.vertical
                             ? Rect{glyph.origin.x - (run.font_size / 2), glyph.origin.y - run.font_size, glyph.origin.x + (run.font_size / 2), glyph.origin.y}
                             : Rect{glyph.origin.x, glyph.origin.y + (run.descent * run.font_size), glyph.origin.x + std::max(glyph.width, 0.0),
                                    glyph.origin.y + (run.ascent * run.font_size)};
        const Rect view = transformed(box.normalized(), run.text_matrix.then(to_view));
        // In an area: its centre, or a fifth of its box (a glyph of the line next to an area, whose box
        // only grazes it, stays). A glyph of no area (no width: a zero-width space, text scaled to nothing)
        // by its centre only: a fifth of nothing is anywhere.
        const Point centre{(view.x0 + view.x1) / 2, (view.y0 + view.y1) / 2};
        const double box_area = area_of(view);
        if (std::ranges::none_of(areas, [&](const Area& a) {
              return inside(centre, a.rect) || (box_area > 0 && area_of(intersection(view, a.rect)) >= kGlyphShare * box_area);
            })) {
          continue;
        }
        const std::uint32_t end = g + 1 < run.glyphs.size() ? run.glyphs[g + 1].byte : static_cast<std::uint32_t>(run.bytes.size());
        OpEdit& edit = plan[origin.invocation][origin.operation];
        edit.size = run.font_size;
        edit.hscale = run.horizontal_scale;
        edit.vertical = run.vertical;
        edit.cuts.push_back({.byte = glyph.byte, .length = end > glyph.byte ? end - glyph.byte : 1, .advance = glyph.advance});
        ++report.glyphs;
      }
    } else if (!text_only && (std::holds_alternative<graphics::FillPath>(command) || std::holds_alternative<graphics::StrokePath>(command))) {
      const auto* fill = std::get_if<graphics::FillPath>(&command);
      const graphics::Path& path = fill != nullptr ? fill->path : std::get<graphics::StrokePath>(command).path;
      const Matrix& ctm = fill != nullptr ? fill->ctm : std::get<graphics::StrokePath>(command).ctm;
      const auto bounds = path_bounds(path);
      if (!bounds) {
        continue;
      }
      const Rect view = transformed(*bounds, ctm.then(to_view));
      const Rect grown{view.x0 - 0.5, view.y0 - 0.5, view.x1 + 0.5, view.y1 + 0.5};
      // A path (nearly) all in an area — or half in one it is not larger than: a glyph drawn as a path, a
      // mark — is not painted. A larger one an area crosses — a background, a rule, an overlay — is
      // painted outside the areas only (clipped), so that the page keeps its look around them.
      double share = 0;
      bool small = false;
      for (const Area& a : areas) {
        const double part = area_of(intersection(grown, a.rect));
        share += part;
        small = small || (part > 0 && area_of(grown) <= area_of(a.rect));
      }
      share /= std::max(area_of(grown), 1e-9);
      OpEdit& edit = plan[origin.invocation][origin.operation];
      if (share >= kPathAll || (share >= kPathShare && small)) {
        report.paths += edit.unpaint ? 0 : 1;
        edit.unpaint = true;
        edit.clip.clear();
      } else if (share > 0 && !edit.unpaint && edit.clip.empty()) {
        // The areas, in the path's space, cut out of its bounds (even-odd).
        const auto inverse = ctm.then(to_view).inverse();
        if (!inverse) {
          continue;
        }
        const Rect outer = transformed(Rect{view.x0 - 2, view.y0 - 2, view.x1 + 2, view.y1 + 2}, *inverse);
        std::string clip = std::format("{} {} {} {} re", num(outer.x0), num(outer.y0), num(outer.width()), num(outer.height()));
        for (const Area& a : areas) {
          if (!meets(grown, a.rect)) {
            continue;
          }
          const std::array<Point, 4> corners = {inverse->apply(Point{a.rect.x0, a.rect.y0}), inverse->apply(Point{a.rect.x1, a.rect.y0}),
                                                inverse->apply(Point{a.rect.x1, a.rect.y1}), inverse->apply(Point{a.rect.x0, a.rect.y1})};
          clip += std::format(" {} {} m {} {} l {} {} l {} {} l h", num(corners[0].x), num(corners[0].y), num(corners[1].x), num(corners[1].y),
                              num(corners[2].x), num(corners[2].y), num(corners[3].x), num(corners[3].y));
        }
        edit.clip = clip + " W* n";
      }
    } else if (const auto* drawn = text_only ? nullptr : std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      const Rect view = transformed(Rect{0, 0, 1, 1}, (*drawn)->ctm.then(to_view));
      if (std::ranges::any_of(areas,
                              [&](const Area& a) { return view.x0 >= a.rect.x0 && view.y0 >= a.rect.y0 && view.x1 <= a.rect.x1 && view.y1 <= a.rect.y1; })) {
        plan[origin.invocation][origin.operation].drop = true;
        ++report.images_removed;
      } else if (hit(view)) {
        images.push_back({.invocation = origin.invocation, .operation = origin.operation, .command = i});
      }
    }
  }
  for (auto& [invocation, edits] : plan) {
    for (auto& [operation, edit] : edits) {
      std::ranges::sort(edit.cuts, [](const TextCut& a, const TextCut& b) { return a.byte < b.byte; });
    }
  }

  // 2. Annotations meeting an area, and the marks applied.
  auto annotations = list_annotations(document, page_index);
  std::vector<std::pair<ObjectRef, bool>> doomed;  // reference, widget
  if (annotations && !text_only) {
    for (const AnnotationInfo& a : *annotations) {
      if (!a.ref) {
        continue;
      }
      const bool mark = std::ranges::find(marks_to_remove, *a.ref) != marks_to_remove.end();
      if (mark || (a.subtype != "Popup" && hit(a.rect))) {
        doomed.emplace_back(*a.ref, a.subtype == "Widget");
      }
    }
  }

  // 3. Invocations to write anew: those with edits and their ancestors, deepest first.
  std::set<std::uint32_t> needed;
  for (const auto& [invocation, edits] : plan) {
    needed.insert(invocation);
  }
  for (const ImageJob& job : images) {
    needed.insert(job.invocation);
  }
  for (std::uint32_t inv : std::set<std::uint32_t>(needed)) {
    for (std::size_t depth = 0; inv != 0 && depth < invocations.size(); ++depth) {
      inv = invocations[inv].parent;
      needed.insert(inv);
    }
  }
  needed.insert(0);  // the page's content is always written anew (wrapped, and the areas filled)

  // Each invocation's resources (the page's inherited ones; a form's own).
  const auto resources_of = [&](std::uint32_t invocation) -> Dict {
    if (invocation == 0) {
      const auto r = detail::inherited(document, page->dict, "Resources");
      return r && r->as_dict() != nullptr ? *r->as_dict() : Dict{};
    }
    auto form = document.get(*invocations[invocation].form);
    const model::Stream* stream = form && *form != nullptr ? (*form)->as_stream() : nullptr;
    const Object* entry = stream != nullptr ? stream->dict.find("Resources") : nullptr;
    auto resolved = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
    return resolved && resolved->as_dict() != nullptr ? *resolved->as_dict() : Dict{};
  };
  std::map<std::uint32_t, std::set<std::string>> taken;

  // Images an area crosses: written anew under a new name (made when written, below).
  struct ImageName {
    std::size_t command;
    std::string name;
  };
  std::map<std::uint32_t, std::vector<ImageName>> new_images;
  for (const ImageJob& job : images) {
    const auto& drawn = std::get<graphics::Boxed<graphics::DrawImage>>(list->commands[job.command]);
    OpEdit& edit = plan[job.invocation][job.operation];
    if (!drawn->pixels || drawn->pixels->width <= 0 || drawn->pixels->height <= 0) {
      edit.drop = true;  // undecodable: it goes
      ++report.images_removed;
      continue;
    }
    edit.replace_name = fresh_name(document, resources_of(job.invocation), taken[job.invocation], "CprRedacted");
    new_images[job.invocation].push_back({.command = job.command, .name = *edit.replace_name});
    ++report.images_cleared;
  }
  // A form changed is drawn by its parent under a new name. (A child's invocation always comes after its
  // parent's: going from the last, each content is written once every name in it is known.)
  for (const std::uint32_t inv : std::views::reverse(needed)) {
    if (inv != 0) {
      const std::uint32_t parent = invocations[inv].parent;
      plan[parent][invocations[inv].operation].replace_name = fresh_name(document, resources_of(parent), taken[parent], "CprRedacted");
    }
  }

  // 4. Written, deepest first: each invocation's new images, its content anew (a form: a new form XObject,
  // which its parent adds to its resources), the page's content and resources last.
  std::map<std::uint32_t, std::vector<std::pair<std::string, ObjectRef>>> added;
  std::set<std::int64_t> mcids;  // of the page's marked content that changed
  for (const std::uint32_t inv : std::views::reverse(needed)) {
    for (const ImageName& image : new_images[inv]) {
      const auto& drawn = std::get<graphics::Boxed<graphics::DrawImage>>(list->commands[image.command]);
      std::optional<ObjectRef> soft_mask;
      auto object = cleared_image(document, *drawn, to_view, areas, soft_mask);
      if (!object) {
        return make_error(ErrorCode::kLimitExceeded, "an image could not be written anew");
      }
      auto ref = document.add(std::move(*object));
      if (!ref) {
        return std::unexpected(std::move(ref.error()));
      }
      added[inv].emplace_back(image.name, *ref);
    }
    std::vector<std::byte> source;
    Dict dict;
    if (inv == 0) {
      auto content = content::page_content(document, page->ref);
      source = content ? std::move(*content) : std::vector<std::byte>{};
    } else {
      const std::optional<ObjectRef> form_ref = invocations[inv].form;
      if (!form_ref) {
        continue;  // a form invocation always names its form
      }
      auto form = document.get(*form_ref);
      const model::Stream* stream = form && *form != nullptr ? (*form)->as_stream() : nullptr;
      auto decoded = stream != nullptr ? document.decode_stream(*stream) : Result<std::vector<std::byte>>(std::vector<std::byte>{});
      source = decoded ? std::move(*decoded) : std::vector<std::byte>{};
      dict = stream != nullptr ? stream->dict : Dict{};
    }
    Rewrite rewrite = rewritten(source, plan[inv]);
    std::string content = std::move(rewrite.content);
    if (inv == 0) {
      mcids = std::move(rewrite.mcids);
    }
    // The XObjects it no longer draws leave its resources: kept there, they would keep in the file what
    // was redacted out of them.
    Dict resources = with_xobjects(document, resources_of(inv), added[inv], rewrite.unused);
    if (inv != 0) {
      dict.set("Resources", std::move(resources));
      auto ref = add_content(document, std::move(dict), content);
      if (!ref) {
        return std::unexpected(std::move(ref.error()));
      }
      const std::uint32_t parent = invocations[inv].parent;
      const std::optional<std::string>& name = plan[parent][invocations[inv].operation].replace_name;
      if (name) {
        added[parent].emplace_back(*name, *ref);
      }
      continue;
    }
    // The page: its content wrapped (a state left open must not reach the fill), then the areas filled.
    content.insert(0, "q\n");
    content += "Q\n";
    for (const Area& a : areas) {
      if (!a.fill) {
        continue;
      }
      const Rect& r = a.rect;
      const std::array<Point, 4> corners = {page->user(Point{r.x0, r.y0}), page->user(Point{r.x1, r.y0}), page->user(Point{r.x1, r.y1}),
                                            page->user(Point{r.x0, r.y1})};
      content +=
          std::format("q {} rg {} {} m {} {} l {} {} l {} {} l h f Q\n", detail::rgb_operands(detail::clamp(*a.fill)), num(corners[0].x), num(corners[0].y),
                      num(corners[1].x), num(corners[1].y), num(corners[2].x), num(corners[2].y), num(corners[3].x), num(corners[3].y));
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
  // The structure elements of the page's marked content that changed lose their text.
  if (auto stripped = strip_structure(document, page->dict, mcids); !stripped) {
    return std::unexpected(std::move(stripped.error()));
  }
  // Annotations meeting an area, and the marks, go.
  for (const auto& [ref, widget] : doomed) {
    auto current = detail::page_at(document, page_index, std::nullopt);
    if (!current) {
      return std::unexpected(std::move(current.error()));
    }
    auto removed = widget ? remove_widget(document, *current, ref) : remove_annotation(document, page_index, ref);
    if (!removed) {
      return std::unexpected(std::move(removed.error()));
    }
    if (std::ranges::find(marks_to_remove, ref) == marks_to_remove.end()) {
      ++report.annotations;
    }
  }
  if (!text_only) {
    document.mark_redacted();
  }
  return report;
}

}  // namespace

namespace detail {

Result<std::uint32_t> remove_glyphs(Document& document, std::uint32_t page_index, std::span<const Rect> areas) {
  std::vector<Area> list;
  for (const Rect& r : areas) {
    list.push_back({.rect = r.normalized(), .fill = std::nullopt});
  }
  auto report = redact_areas(document, page_index, list, {}, true);
  if (!report) {
    return std::unexpected(std::move(report.error()));
  }
  return report->glyphs;
}

}  // namespace detail

Result<RedactionReport> redact(Document& document, std::uint32_t page_index, std::span<const Rect> areas, const RedactionOptions& options) {
  std::vector<Area> list;
  for (const Rect& r : areas) {
    list.push_back({.rect = r.normalized(), .fill = options.fill});
  }
  return redact_areas(document, page_index, list, {});
}

Result<RedactionReport> apply_redactions(Document& document, std::uint32_t page_index) {
  auto page = detail::page_at(document, page_index, "the page's content");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  const Object* annots_entry = page->dict.find("Annots");
  auto annots = annots_entry != nullptr ? document.resolve(*annots_entry) : Result<Object>(Object());
  std::vector<Area> areas;
  std::vector<ObjectRef> marks;
  if (annots && annots->as_array() != nullptr) {
    for (const Object& entry : *annots->as_array()) {
      const auto ref = entry.as_ref();
      if (!ref) {
        continue;
      }
      auto resolved = document.resolve(entry);
      const Dict* a = resolved && resolved->as_dict() != nullptr ? resolved->as_dict() : nullptr;
      if (a == nullptr || detail::subtype_of(*a) != "Redact") {
        continue;
      }
      marks.push_back(*ref);
      std::optional<Rgb> fill;
      if (const std::vector<double> ic = content::numbers(document, *a, "IC"); ic.size() == 3) {
        fill = Rgb{ic[0], ic[1], ic[2]};
      }
      // /QuadPoints: one area per quadrilateral (its bounds); else /Rect.
      const std::vector<double> q = content::numbers(document, *a, "QuadPoints");
      if (q.size() >= 8) {
        for (std::size_t i = 0; i + 8 <= q.size(); i += 8) {
          const std::array<Point, 4> corners = {Point{q[i], q[i + 1]}, Point{q[i + 2], q[i + 3]}, Point{q[i + 4], q[i + 5]}, Point{q[i + 6], q[i + 7]}};
          areas.push_back({.rect = page->view(bounds_of(corners)), .fill = fill});
        }
      } else if (const std::vector<double> r = content::numbers(document, *a, "Rect"); r.size() == 4) {
        areas.push_back({.rect = page->view(Rect{r[0], r[1], r[2], r[3]}.normalized()), .fill = fill});
      }
    }
  }
  if (marks.empty()) {
    return make_error(ErrorCode::kOutOfRange, "the page has no redaction marks (/Redact annotations)");
  }
  std::erase_if(areas, [](const Area& a) { return !finite(a.rect) || a.rect.width() <= 0 || a.rect.height() <= 0; });
  if (areas.empty()) {
    return make_error(ErrorCode::kUnsupported, "the page's redaction marks have no area");
  }
  return redact_areas(document, page_index, areas, marks);
}

}  // namespace capriole::edit
