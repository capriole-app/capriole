#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/annotations.hpp"
#include "capriole/render/raster.hpp"
#include "capriole/writer/save.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

using graphics::Point;
using graphics::Rect;
using model::Dict;
using model::Object;
using model::ObjectRef;

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char c : text) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

Document open(const std::string& pdf) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(pdf)));
  EXPECT_TRUE(document.has_value()) << (document ? "" : document.error().message);
  if (!document) {
    std::abort();
  }
  return std::move(*document);
}

std::string save(Document& document, bool incremental = false) {
  writer::MemorySink sink;
  const auto report = incremental ? writer::save_incremental(document, sink) : writer::save_full(document, sink);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  std::string out;
  for (const std::byte b : sink.data()) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

// A specification of kind, with the fields set does (designated initializers would need every field).
AnnotationSpec spec(AnnotationKind kind, const std::function<void(AnnotationSpec&)>& set = {}) {
  AnnotationSpec s;
  s.kind = kind;
  if (set) {
    set(s);
  }
  return s;
}

AnnotationChange change(const std::function<void(AnnotationChange&)>& set) {
  AnnotationChange c;
  set(c);
  return c;
}

// One page, 200 × 100, white; extra: more page entries (/Rotate, /Annots).
std::string page_pdf(const std::string& extra = "", const std::vector<std::pair<std::uint32_t, std::string>>& objects = {}) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]" + extra + ">>");
  for (const auto& [number, body] : objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 20/Root 1 0 R");
  return b.str();
}

const Dict& dict_of(Document& document, ObjectRef ref) {
  static const Dict kEmpty;
  auto object = document.get(ref);
  return object && (*object)->as_dict() != nullptr ? *(*object)->as_dict() : kEmpty;
}

std::vector<double> numbers(Document& document, const Dict& dict, std::string_view key) {
  std::vector<double> out;
  auto value = document.resolve(*dict.find(key));
  for (const Object& o : *value->as_array()) {
    out.push_back(o.as_number().value_or(0));
  }
  return out;
}

// The page drawn at 72 dpi: how many pixels are not white.
std::size_t ink(Document& document) {
  auto list = content::interpret_page(document, (*document.page_refs())[0]);
  EXPECT_TRUE(list.has_value());
  auto bitmap = render::rasterize(*list, 1);
  EXPECT_TRUE(bitmap.has_value());
  std::size_t count = 0;
  for (std::size_t i = 0; i + 3 < bitmap->rgba.size(); i += 4) {
    count += bitmap->rgba[i] < 250 || bitmap->rgba[i + 1] < 250 || bitmap->rgba[i + 2] < 250 ? 1U : 0U;
  }
  return count;
}

TEST(AnnotationEditing, EveryKindIsAddedWithItsAppearance) {
  const std::vector<AnnotationSpec> specs = {
      spec(AnnotationKind::kHighlight, [](AnnotationSpec& x) { x.quads = {Rect{10, 10, 60, 22}}; }),
      spec(AnnotationKind::kUnderline,
           [](AnnotationSpec& x) {
             x.quads = {Rect{10, 30, 60, 42}};
             x.color = {1, 0, 0};
           }),
      spec(AnnotationKind::kStrikeOut,
           [](AnnotationSpec& x) {
             x.quads = {Rect{70, 10, 120, 22}};
             x.color = {1, 0, 0};
           }),
      spec(AnnotationKind::kSquiggly,
           [](AnnotationSpec& x) {
             x.quads = {Rect{70, 30, 120, 42}};
             x.color = {0, 0, 1};
           }),
      spec(AnnotationKind::kNote,
           [](AnnotationSpec& x) {
             x.rect = Rect{150, 10, 170, 30};
             x.contents = "Una nota";
           }),
      spec(AnnotationKind::kInk,
           [](AnnotationSpec& x) {
             x.strokes = {{Point{10, 60}, Point{30, 80}, Point{50, 60}}};
             x.color = {0, 0.5, 0};
             x.width = 2;
           }),
      spec(AnnotationKind::kSquare,
           [](AnnotationSpec& x) {
             x.rect = Rect{60, 55, 90, 85};
             x.color = {0, 0, 1};
             x.fill = Rgb{0.8, 0.9, 1};
             x.width = 2;
           }),
      spec(AnnotationKind::kCircle,
           [](AnnotationSpec& x) {
             x.rect = Rect{100, 55, 130, 85};
             x.color = {1, 0, 0};
             x.width = 1.5;
           }),
      spec(AnnotationKind::kLine,
           [](AnnotationSpec& x) {
             x.from = Point{140, 60};
             x.to = Point{190, 90};
             x.color = {0, 0, 0};
             x.width = 1;
           }),
      spec(AnnotationKind::kFreeText,
           [](AnnotationSpec& x) {
             x.rect = Rect{130, 35, 195, 52};
             x.color = {0, 0, 0.6};
             x.font_size = 10;
             x.contents = "Testo libero";
           }),
  };
  for (const AnnotationSpec& spec : specs) {
    SCOPED_TRACE(static_cast<int>(spec.kind));
    Document document = open(page_pdf());
    EXPECT_EQ(ink(document), 0U);
    AnnotationSpec dated = spec;
    dated.author = "Nicola";
    dated.date = "D:20261002120000Z";
    const auto ref = add_annotation(document, 0, dated);
    ASSERT_TRUE(ref.has_value()) << ref.error().message;
    const Dict& a = dict_of(document, *ref);
    EXPECT_TRUE(a.contains("AP"));
    EXPECT_EQ(a.find("P")->as_ref()->number, 3U);
    EXPECT_EQ(a.find("T")->as_string()->bytes, "Nicola");
    EXPECT_EQ(a.find("M")->as_string()->bytes, "D:20261002120000Z");
    EXPECT_GT(ink(document), 20U);  // drawn from its appearance
    // Listed, as shown; and it survives a full save.
    const auto listed = list_annotations(document, 0);
    ASSERT_TRUE(listed.has_value());
    ASSERT_EQ(listed->size(), 1U);
    EXPECT_TRUE(listed->front().editable);
    Document saved = open(save(document));
    EXPECT_GT(ink(saved), 20U);
  }
}

TEST(AnnotationEditing, GeometryIsConvertedFromThePageAsShown) {
  // View (10, 10)–(60, 22) on a 100-point-high page: y from 90 down to 78 in page space.
  Document document = open(page_pdf());
  const auto ref = add_annotation(document, 0, spec(AnnotationKind::kHighlight, [](AnnotationSpec& x) { x.quads = {Rect{10, 10, 60, 22}}; }));
  ASSERT_TRUE(ref.has_value());
  EXPECT_EQ(numbers(document, dict_of(document, *ref), "QuadPoints"), (std::vector<double>{10, 90, 60, 90, 10, 78, 60, 78}));
  EXPECT_EQ(numbers(document, dict_of(document, *ref), "Rect"), (std::vector<double>{10, 78, 60, 90}));
  const auto listed = list_annotations(document, 0);
  EXPECT_EQ(listed->front().rect, (Rect{10, 10, 60, 22}));

  // A page turned by 90°: shown 100 × 200; a note (NoRotate) stays upright on the screen with its
  // upper-left corner where it was put, so its rectangle comes back as it was given.
  Document turned = open(page_pdf("/Rotate 90"));
  const auto note = add_annotation(turned, 0, spec(AnnotationKind::kNote, [](AnnotationSpec& x) { x.rect = Rect{10, 20, 30, 40}; }));
  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(list_annotations(turned, 0)->front().rect, (Rect{10, 20, 30, 40}));
  const Object* flags = dict_of(turned, *note).find("F");
  EXPECT_EQ(flags->as_integer(), 4 | 8 | 16);  // Print, NoZoom, NoRotate: as Acrobat writes notes
  EXPECT_GT(ink(turned), 20U);
}

TEST(AnnotationEditing, MoveRecolourRewordRemove) {
  Document document = open(page_pdf());
  const auto square = add_annotation(document, 0, spec(AnnotationKind::kSquare, [](AnnotationSpec& x) {
                                       x.rect = Rect{10, 10, 40, 40};
                                       x.color = {0, 0, 1};
                                       x.width = 2;
                                     }));
  const auto text = add_annotation(document, 0, spec(AnnotationKind::kFreeText, [](AnnotationSpec& x) {
                                     x.rect = Rect{60, 10, 150, 30};
                                     x.contents = "Prima";
                                   }));
  ASSERT_TRUE(square.has_value() && text.has_value());

  // Moved by 20 points right and 10 down, as shown: +20 x, -10 y in page space.
  ASSERT_TRUE(update_annotation(document, 0, *square, change([](AnnotationChange& x) { x.move_by = Point{20, 10}; })).has_value());
  EXPECT_EQ(numbers(document, dict_of(document, *square), "Rect"), (std::vector<double>{30, 50, 60, 80}));

  // A new colour: the appearance is made again.
  const Object before = *dict_of(document, *square).find("AP");
  ASSERT_TRUE(update_annotation(document, 0, *square, change([](AnnotationChange& x) {
                                  x.color = Rgb{1, 0, 0};
                                  x.date = "D:20261002130000Z";
                                }))
                  .has_value());
  EXPECT_NE(*dict_of(document, *square).find("AP"), before);
  EXPECT_EQ(numbers(document, dict_of(document, *square), "C"), (std::vector<double>{1, 0, 0}));
  EXPECT_EQ(dict_of(document, *square).find("M")->as_string()->bytes, "D:20261002130000Z");

  // Free text with accents: a UTF-16 text string, listed back as UTF-8.
  ASSERT_TRUE(update_annotation(document, 0, *text, change([](AnnotationChange& x) { x.contents = std::string("Città perché"); })).has_value());
  EXPECT_EQ(dict_of(document, *text).find("Contents")->as_string()->bytes.substr(0, 2), "\xFE\xFF");
  const auto listed = list_annotations(document, 0);
  ASSERT_EQ(listed->size(), 2U);
  EXPECT_EQ(listed->at(1).contents, "Città perché");

  ASSERT_TRUE(remove_annotation(document, 0, *square).has_value());
  EXPECT_EQ(list_annotations(document, 0)->size(), 1U);
  EXPECT_TRUE((*document.get(*square))->is_null());  // added in this session: gone entirely
  EXPECT_EQ(remove_annotation(document, 0, *square).error().code, ErrorCode::kOutOfRange);

  // Read back after an incremental save: one annotation, as edited.
  Document saved = open(save(document, true));
  const auto after = list_annotations(saved, 0);
  ASSERT_EQ(after->size(), 1U);
  EXPECT_EQ(after->front().contents, "Città perché");
}

TEST(AnnotationEditing, PopupsWidgetsAndDirectAnnotations) {
  // An indirect note with its popup, a direct square, a widget.
  Document document = open(page_pdf("/Annots[10 0 R 11 0 R <</Subtype/Square/Rect[0 0 5 5]>> 12 0 R]",
                                    {{10, "<</Type/Annot/Subtype/Text/Rect[0 0 20 20]/Popup 11 0 R/Contents(note)>>"},
                                     {11, "<</Type/Annot/Subtype/Popup/Rect[20 0 80 40]/Parent 10 0 R>>"},
                                     {12, "<</Type/Annot/Subtype/Widget/FT/Tx/T(f)/Rect[100 0 150 20]>>"}}));
  const auto listed = list_annotations(document, 0);
  ASSERT_TRUE(listed.has_value());
  ASSERT_EQ(listed->size(), 3U);  // the popup is its note's
  EXPECT_EQ(listed->at(0).contents, "note");
  EXPECT_FALSE(listed->at(1).editable);  // direct: nothing refers to it
  EXPECT_FALSE(listed->at(2).editable);  // a widget: phase 8e
  const ObjectRef widget{.number = 12, .generation = 0};
  EXPECT_EQ(update_annotation(document, 0, widget, change([](AnnotationChange& x) { x.move_by = Point{1, 1}; })).error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(remove_annotation(document, 0, widget).error().code, ErrorCode::kUnsupported);
  // A colour the editor cannot make an appearance for.
  EXPECT_EQ(update_annotation(document, 0, ObjectRef{.number = 10, .generation = 0}, change([](AnnotationChange& x) { x.color = Rgb{1, 0, 0}; })).has_value(),
            true);  // a note: it can
  // Removing the note takes its popup with it.
  ASSERT_TRUE(remove_annotation(document, 0, ObjectRef{.number = 10, .generation = 0}).has_value());
  EXPECT_EQ(document.state(11), ObjectState::kDeleted);
  EXPECT_EQ(list_annotations(document, 0)->size(), 2U);
}

TEST(AnnotationEditing, RefusalsComeBeforeAnyChange) {
  Document document = open(page_pdf());
  EXPECT_EQ(add_annotation(document, 1, spec(AnnotationKind::kNote, [](AnnotationSpec& x) { x.rect = Rect{0, 0, 20, 20}; })).error().code,
            ErrorCode::kOutOfRange);
  EXPECT_EQ(add_annotation(document, 0, spec(AnnotationKind::kHighlight)).error().code, ErrorCode::kUnsupported);  // no quads
  EXPECT_EQ(add_annotation(document, 0, spec(AnnotationKind::kInk)).error().code, ErrorCode::kUnsupported);        // no stroke
  EXPECT_EQ(add_annotation(document, 0, spec(AnnotationKind::kSquare, [](AnnotationSpec& x) { x.rect = Rect{5, 5, 5, 20}; })).error().code,
            ErrorCode::kUnsupported);
  EXPECT_FALSE(document.has_changes());
  // A damaged file (its cross-reference table points nowhere): edit after a full save.
  std::string broken = page_pdf();
  broken.replace(broken.rfind("startxref\n") + 10, std::string::npos, "999999\n%%EOF\n");
  Document repaired = open(broken);
  ASSERT_FALSE(repaired.page_refs()->empty());
  EXPECT_EQ(add_annotation(repaired, 0, spec(AnnotationKind::kNote, [](AnnotationSpec& x) { x.rect = Rect{0, 0, 20, 20}; })).error().code,
            ErrorCode::kUnsupported);
}

TEST(AnnotationEditing, RepairsOfSingleObjectsDoNotStopEdits) {
  // tests/regression/annotations (fuzz_annotations): a page whose /Annots array reads only tolerantly
  // (garbage inside). That repair shows up when the array is read — after the check at the start of an
  // edit —, and edits refused from then on: even listing what was just added failed. Only structural
  // repairs stop edits now, and listing never does.
  auto file = FileByteSource::open(std::string(CAPRIOLE_TEST_DATA_DIR) + "/../regression/annotations/tolerant-annots.pdf");
  ASSERT_TRUE(file.has_value());
  auto document = Document::open(std::move(*file));
  ASSERT_TRUE(document.has_value());
  const auto before = list_annotations(*document, 0);
  ASSERT_TRUE(before.has_value());
  const auto added = add_annotation(*document, 0, spec(AnnotationKind::kNote, [](AnnotationSpec& x) { x.rect = Rect{150, 10, 170, 30}; }));
  if (!added) {
    // The file's cross-reference table is rebuilt: a structural repair, refused — and said so.
    EXPECT_EQ(added.error().code, ErrorCode::kUnsupported);
    GTEST_SKIP() << added.error().message;
  }
  const auto after = list_annotations(*document, 0);
  ASSERT_TRUE(after.has_value()) << after.error().message;
  EXPECT_EQ(after->size(), before->size() + 1);
  EXPECT_TRUE(update_annotation(*document, 0, *added, change([](AnnotationChange& x) { x.move_by = Point{1, 1}; })).has_value());
}

// The non-white pixels' bounding box of page 1 drawn at 72 dpi: {x0, y0, x1, y1}.
std::array<int, 4> inked_box(Document& document) {
  auto list = content::interpret_page(document, (*document.page_refs())[0]);
  auto bitmap = render::rasterize(*list, 1);
  std::array<int, 4> box = {bitmap->width, bitmap->height, -1, -1};
  for (int y = 0; y < bitmap->height; ++y) {
    for (int x = 0; x < bitmap->width; ++x) {
      if (bitmap->rgba[static_cast<std::size_t>((y * bitmap->width) + x) * 4] < 128) {
        box = {std::min(box[0], x), std::min(box[1], y), std::max(box[2], x), std::max(box[3], y)};
      }
    }
  }
  return box;
}

TEST(AnnotationRendering, NoRotateKeepsAnnotationsUprightOnTurnedPages) {
  // An appearance 40 × 10 (wider than tall) on a page shown turned by 90°: with NoRotate (/F 16) it
  // stays wider than tall on the screen, its upper-left corner in place; without, it turns with the page.
  const std::string ap = "<</Type/XObject/Subtype/Form/BBox[0 0 40 10]/Length 22>>\nstream\n0 0 0 rg 0 0 40 10 re f\nendstream";
  for (const int flags : {16, 0}) {
    SCOPED_TRACE(flags);
    Document document = open(page_pdf("/Rotate 90/Annots[10 0 R]",
                                      {{10, "<</Type/Annot/Subtype/Text/Rect[50 50 90 60]/AP<</N 11 0 R>>/F " + std::to_string(flags) + ">>"}, {11, ap}}));
    const auto box = inked_box(document);
    const int width = box[2] - box[0] + 1;
    const int height = box[3] - box[1] + 1;
    if (flags == 16) {
      EXPECT_GT(width, height);
      // The upper-left corner of /Rect, (50, 60), as shown (y flipped in the 100-point page, then turned
      // clockwise): (100 − 40, 50) = (60, 50) — where Poppler pins it too.
      EXPECT_NEAR(box[0], 60, 1);
      EXPECT_NEAR(box[1], 50, 1);
    } else {
      EXPECT_GT(height, width);
    }
  }
}

}  // namespace
}  // namespace capriole::edit
