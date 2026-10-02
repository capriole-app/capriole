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
#include "capriole/model/text_string.hpp"
#include "common.hpp"

namespace capriole::edit {

namespace {

using detail::bounds;
using detail::clamp;
using detail::grow;
using detail::hex;
using detail::name;
using detail::num;
using detail::number_object;
using detail::Page;
using detail::rect_array;
using detail::rgb_array;
using detail::rgb_operands;
using detail::subtype_of;
using detail::utf8_of;
using graphics::Matrix;
using graphics::Point;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

Result<Page> page_at(Document& document, std::uint32_t index, bool edit = true) {
  return detail::page_at(document, index, edit ? std::optional<std::string_view>("annotations") : std::nullopt);
}

// Annotation flags (§12.5.3).
constexpr std::int64_t kHiddenFlag = 1 << 1;
constexpr std::int64_t kPrintFlag = 1 << 2;
constexpr std::int64_t kNoZoomFlag = 1 << 3;
constexpr std::int64_t kNoRotateFlag = 1 << 4;
constexpr std::int64_t kNoViewFlag = 1 << 5;

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
  const std::string codes = detail::win_ansi_bytes(text);
  std::string c =
      std::format("{} {} {} {} re W n\nBT /Helv {} Tf {} rg\n", num(r.x0), num(r.y0), num(r.width()), num(r.height()), num(size), rgb_operands(color));
  double baseline = r.y0 + kPadding + (size * 0.9);
  for (const std::string& line : detail::wrap(codes, size, std::max(r.width() - (2 * kPadding), size), detail::helvetica())) {
    if (baseline - size > r.y1) {
      break;
    }
    c += std::format("1 0 0 -1 {} {} Tm {} Tj\n", num(r.x0 + kPadding), num(baseline), hex(line));
    baseline += size * 1.2;
  }
  return c + "ET\n";
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
    const auto parsed = detail::parse_da(da != nullptr && da->as_string() != nullptr ? da->as_string()->bytes : std::string());
    const double size = parsed.size > 0 ? parsed.size : 12;
    const Rgb color = parsed.rgb;
    const Object* text = annotation.find("Contents");
    const std::u32string contents = text != nullptr && text->as_string() != nullptr ? model::decode_text_string(text->as_string()->bytes) : std::u32string();
    content = in_view(page, free_text(page.view(rect), contents, size, color));
    resources.set("Font", detail::helvetica_font());
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
  return detail::form_xobject(rect, std::move(resources), content);  // in page space: /BBox is /Rect, no /Matrix
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
      const double parsed = detail::parse_da(da != nullptr && da->as_string() != nullptr ? da->as_string()->bytes : std::string()).size;
      const double size = parsed > 0 ? parsed : 12;
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
