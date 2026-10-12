#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/forms.hpp"
#include "capriole/render/raster.hpp"
#include "capriole/writer/save.hpp"
#include "edit/common.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::edit {
namespace {

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

std::string save(Document& document, bool incremental = true) {
  writer::MemorySink sink;
  const auto report = incremental ? writer::save_incremental(document, sink) : writer::save_full(document, sink);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  std::string out;
  for (const std::byte b : sink.data()) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

// One page, 300 × 400, with a field of every kind (form: an extra for /AcroForm, e.g. /XFA).
std::string form_pdf(const std::string& form_extra = "") {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm 4 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3,
               "<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Annots[10 0 R 12 0 R 13 0 R 14 0 R 15 0 R 16 0 R 17 0 R 18 0 R 20 0 R 21 0 R 22 0 R 23 0 R 24 "
               "0 R 25 0 R]>>");
  b.add_object(4,
               "<</Fields[10 0 R 11 0 R 13 0 R 14 0 R 15 0 R 16 0 R 19 0 R 20 0 R 21 0 R 22 0 R 23 0 R 24 0 R 25 0 R]"
               "/DR<</Font<</Helv 5 0 R/F1 6 0 R>>>>/DA(/Helv 0 Tf 0 g)" +
                   form_extra + ">>");
  b.add_object(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>");
  b.add_object(6, "<</Type/Font/Subtype/Type1/BaseFont/Times-Roman>>");  // StandardEncoding: no €
  const std::string widget = "/Subtype/Widget/P 3 0 R";
  b.add_object(10, "<</FT/Tx/T(name)/TU(Your name)/Ff 2" + widget + "/Rect[20 360 220 380]/MK<</BC[0 0 0]/BG[0.9 0.9 1]>>>>");
  b.add_object(11, "<</FT/Tx/T(person)/Kids[12 0 R]>>");
  b.add_object(12, "<</T(address)/Parent 11 0 R/Ff 4096/Q 1/DA(/F1 10 Tf 0 0 1 rg)" + widget + "/Rect[20 300 220 350]>>");
  b.add_object(13, "<</FT/Tx/T(code)/Ff 16777216/MaxLen 5" + widget + "/Rect[20 270 120 290]>>");
  b.add_object(14, "<</FT/Tx/T(pin)/Ff 8192" + widget + "/Rect[130 270 220 290]>>");
  b.add_object(15, "<</FT/Btn/T(agree)/V/Off/AS/Off/AP<</N<</Yes 30 0 R/Off 31 0 R>>>>" + widget + "/Rect[20 240 34 254]>>");
  b.add_object(16, "<</FT/Btn/T(bare)/DA(0 0 1 rg)" + widget + "/Rect[40 240 54 254]/MK<</BC[0 0 0]>>>>");
  b.add_object(17, "<</Parent 19 0 R/AS/Off/AP<</N<</Red 30 0 R/Off 31 0 R>>>>" + widget + "/Rect[20 210 34 224]>>");
  b.add_object(18, "<</Parent 19 0 R/AS/Off/AP<</N<</Blue 30 0 R/Off 31 0 R>>>>" + widget + "/Rect[40 210 54 224]>>");
  b.add_object(19, "<</FT/Btn/Ff 49152/T(color)/Kids[17 0 R 18 0 R]/V/Red>>");
  b.add_object(20, "<</FT/Ch/Ff 131072/T(city)/Opt[(Roma)(Milano)]" + widget + "/Rect[20 180 120 200]>>");
  b.add_object(21, "<</FT/Ch/Ff 2097152/T(langs)/Opt[[(it)(Italiano)][(en)(English)][(de)(Deutsch)]]" + widget + "/Rect[130 150 220 200]>>");
  b.add_object(22, "<</FT/Tx/Ff 1/T(fixed)/V(no)" + widget + "/Rect[20 120 120 140]>>");
  b.add_object(23, "<</FT/Sig/T(sig)" + widget + "/Rect[130 120 220 140]>>");
  b.add_object(24, "<</FT/Btn/Ff 65536/T(push)" + widget + "/Rect[20 90 60 110]>>");
  b.add_object(25, "<</FT/Tx/T(turned)/DA(/Helv 12 Tf 0 g)" + widget + "/Rect[250 200 270 350]/MK<</R 90>>>>");
  b.add_stream(30, "0 g 2 2 10 10 re f", "/Type/XObject/Subtype/Form/BBox[0 0 14 14]");
  b.add_stream(31, "", "/Type/XObject/Subtype/Form/BBox[0 0 14 14]");
  b.finish_table("/Size 32/Root 1 0 R");
  return b.str();
}

const FieldInfo& field(const std::vector<FieldInfo>& fields, std::string_view name) {
  const auto found = std::ranges::find_if(fields, [&](const FieldInfo& f) { return f.name == name; });
  EXPECT_NE(found, fields.end()) << name;
  if (found == fields.end()) {
    std::abort();
  }
  return *found;
}

std::vector<FieldInfo> fields_of(Document& document) {
  auto fields = list_fields(document);
  EXPECT_TRUE(fields.has_value()) << (fields ? "" : fields.error().message);
  return fields ? std::move(*fields) : std::vector<FieldInfo>{};
}

const Dict& dict_of(Document& document, std::uint32_t number) {
  static const Dict kEmpty;
  auto object = document.get(number);
  return object && (*object)->as_dict() != nullptr ? *(*object)->as_dict() : kEmpty;
}

// The normal appearance of a widget: its stream's dictionary and content.
std::pair<Dict, std::string> appearance(Document& document, std::uint32_t widget) {
  const Object* ap = dict_of(document, widget).find("AP");
  if (ap == nullptr) {
    return {};
  }
  auto resolved = document.resolve(*ap);
  auto normal = document.resolve(*resolved->as_dict()->find("N"));
  if (!normal || normal->as_stream() == nullptr) {
    return {};
  }
  auto data = document.decode_stream(*normal->as_stream());
  std::string text;
  for (const std::byte c : *data) {
    text.push_back(static_cast<char>(c));
  }
  return {normal->as_stream()->dict, text};
}

std::string hex(std::string_view text) {
  std::string out = "<";
  for (const char c : text) {
    out += std::format("{:02X}", static_cast<unsigned char>(c));
  }
  return out + ">";
}

// Dark pixels of the page drawn at 72 dpi inside a box (as shown).
std::size_t ink(Document& document, const Rect& box) {
  auto list = content::interpret_page(document, (*document.page_refs())[0]);
  EXPECT_TRUE(list.has_value());
  auto bitmap = render::rasterize(*list, 1);
  EXPECT_TRUE(bitmap.has_value());
  const auto width = static_cast<std::size_t>(bitmap->width);
  const auto height = static_cast<std::size_t>(bitmap->height);
  std::size_t count = 0;
  for (auto y = static_cast<std::size_t>(box.y0); y < static_cast<std::size_t>(box.y1) && y < height; ++y) {
    for (auto x = static_cast<std::size_t>(box.x0); x < static_cast<std::size_t>(box.x1) && x < width; ++x) {
      const std::size_t i = ((y * width) + x) * 4;
      count += bitmap->rgba[i] < 128 && bitmap->rgba[i + 1] < 128 ? 1U : 0U;
    }
  }
  return count;
}

TEST(Forms, ListsEveryTerminalFieldWithItsAttributes) {
  Document document = open(form_pdf());
  const auto fields = fields_of(document);
  std::vector<std::string> names;
  for (const FieldInfo& f : fields) {
    names.push_back(f.name);
  }
  EXPECT_EQ(names,
            (std::vector<std::string>{"name", "person.address", "code", "pin", "agree", "bare", "color", "city", "langs", "fixed", "sig", "push", "turned"}));
  const FieldInfo& name = field(fields, "name");
  EXPECT_EQ(name.type, FieldType::kText);
  EXPECT_EQ(name.label, "Your name");
  EXPECT_TRUE(name.required);
  EXPECT_TRUE(name.fillable);
  ASSERT_EQ(name.widgets.size(), 1U);
  EXPECT_EQ(name.widgets[0].ref, (ObjectRef{10, 0}));
  EXPECT_EQ(name.widgets[0].page, 0U);
  EXPECT_EQ(name.widgets[0].rect, (Rect{20, 20, 220, 40}));  // as shown: y down from the top

  const FieldInfo& address = field(fields, "person.address");
  EXPECT_EQ(address.type, FieldType::kText);  // /FT inherited from "person"
  EXPECT_TRUE(address.multiline);
  const FieldInfo& code = field(fields, "code");
  EXPECT_TRUE(code.comb);
  EXPECT_EQ(code.max_length, 5U);
  EXPECT_TRUE(field(fields, "pin").password);

  EXPECT_EQ(field(fields, "agree").type, FieldType::kCheckBox);
  EXPECT_EQ(field(fields, "agree").value, std::vector<std::string>{"Off"});
  ASSERT_EQ(field(fields, "agree").options.size(), 1U);
  EXPECT_EQ(field(fields, "agree").options[0].value, "Yes");
  const FieldInfo& color = field(fields, "color");
  EXPECT_EQ(color.type, FieldType::kRadio);
  EXPECT_EQ(color.value, std::vector<std::string>{"Red"});
  ASSERT_EQ(color.widgets.size(), 2U);
  EXPECT_EQ(color.widgets[1].on_state, "Blue");

  EXPECT_EQ(field(fields, "city").type, FieldType::kComboBox);
  const FieldInfo& langs = field(fields, "langs");
  EXPECT_EQ(langs.type, FieldType::kListBox);
  EXPECT_TRUE(langs.multi_select);
  ASSERT_EQ(langs.options.size(), 3U);
  EXPECT_EQ(langs.options[1].value, "en");
  EXPECT_EQ(langs.options[1].display, "English");

  EXPECT_FALSE(field(fields, "fixed").fillable);
  EXPECT_EQ(field(fields, "fixed").value, std::vector<std::string>{"no"});
  EXPECT_EQ(field(fields, "sig").type, FieldType::kSignature);
  EXPECT_FALSE(field(fields, "sig").fillable);
  EXPECT_EQ(field(fields, "push").type, FieldType::kPushButton);
  EXPECT_FALSE(field(fields, "push").fillable);
}

TEST(Forms, TextFieldsGetTheValueAndItsAppearance) {
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {10, 0}, {"Ada Lovelace"}).has_value());
  auto [dict, content] = appearance(document, 10);
  EXPECT_NE(content.find("/Tx BMC"), std::string::npos);
  EXPECT_NE(content.find(hex("Ada Lovelace")), std::string::npos);
  EXPECT_NE(content.find("/Helv "), std::string::npos);
  EXPECT_NE(content.find("0.9 0.9 1 rg"), std::string::npos);  // /MK /BG
  EXPECT_EQ(dict.find("BBox")->as_array()->size(), 4U);
  EXPECT_GT(ink(document, Rect{20, 20, 220, 40}), 30U);

  // The field's own font when it has every character; Helvetica when it does not.
  ASSERT_TRUE(fill_field(document, {12, 0}, {"Via Roma 1\nTorino"}).has_value());
  EXPECT_NE(appearance(document, 12).second.find("/F1 10 Tf 0 0 1 rg"), std::string::npos);
  EXPECT_NE(appearance(document, 12).second.find(hex("Torino")), std::string::npos);  // the second line
  ASSERT_TRUE(fill_field(document, {12, 0}, {"10 \xE2\x82\xAC"}).has_value());        // "10 €"
  EXPECT_NE(appearance(document, 12).second.find("/Helv 10 Tf"), std::string::npos);
  EXPECT_NE(appearance(document, 12).second.find(hex("10 \x80")), std::string::npos);  // € in WinAnsi

  // A comb: a character per cell; a password: asterisks.
  ASSERT_TRUE(fill_field(document, {13, 0}, {"AB12"}).has_value());
  const std::string comb = appearance(document, 13).second;
  std::size_t shown = 0;
  for (std::size_t at = comb.find(" Tj"); at != std::string::npos; at = comb.find(" Tj", at + 1)) {
    ++shown;
  }
  EXPECT_EQ(shown, 4U);
  EXPECT_NE(appearance(document, 13).second.find(" Tm <41> Tj"), std::string::npos);
  ASSERT_TRUE(fill_field(document, {14, 0}, {"secret"}).has_value());
  EXPECT_NE(appearance(document, 14).second.find(hex("******")), std::string::npos);
  EXPECT_EQ(appearance(document, 14).second.find(hex("secret")), std::string::npos);

  // /MK /R: the appearance turned onto /Rect.
  ASSERT_TRUE(fill_field(document, {25, 0}, {"Su"}).has_value());
  const Dict turned = appearance(document, 25).first;
  ASSERT_NE(turned.find("Matrix"), nullptr);
  EXPECT_EQ(turned.find("BBox")->as_array()->at(2).as_number(), 150.0);  // upright: as long as the rect is tall

  // Read back after an incremental save.
  Document reread = open(save(document));
  const auto fields = fields_of(reread);
  EXPECT_EQ(field(fields, "name").value, std::vector<std::string>{"Ada Lovelace"});
  EXPECT_EQ(field(fields, "person.address").value, std::vector<std::string>{"10 \xE2\x82\xAC"});
  EXPECT_EQ(field(fields, "code").value, std::vector<std::string>{"AB12"});

  // Cleared.
  ASSERT_TRUE(fill_field(reread, {10, 0}, {}).has_value());
  EXPECT_EQ(dict_of(reread, 10).find("V"), nullptr);
}

TEST(Forms, SingleLineFieldsFlattenLineBreaks) {
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {10, 0}, {"a\r\nb"}).has_value());
  EXPECT_EQ(field(fields_of(document), "name").value, std::vector<std::string>{"a b"});
}

TEST(Forms, ButtonsTurnOnTheirState) {
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {15, 0}, {"Yes"}).has_value());
  EXPECT_EQ(dict_of(document, 15).find("V")->as_name()->bytes, "Yes");
  EXPECT_EQ(dict_of(document, 15).find("AS")->as_name()->bytes, "Yes");
  ASSERT_TRUE(fill_field(document, {15, 0}, {"Off"}).has_value());
  EXPECT_EQ(dict_of(document, 15).find("AS")->as_name()->bytes, "Off");
  EXPECT_EQ(fill_field(document, {15, 0}, {"Maybe"}).error().code, ErrorCode::kOutOfRange);

  // A check box without appearance states gets drawn ones.
  ASSERT_TRUE(fill_field(document, {16, 0}, {"Yes"}).has_value());
  const Object* ap = dict_of(document, 16).find("AP");
  ASSERT_NE(ap, nullptr);
  auto normal = document.resolve(*ap->as_dict()->find("N"));
  ASSERT_TRUE(normal && normal->as_dict() != nullptr);
  EXPECT_TRUE(normal->as_dict()->contains("Yes"));
  EXPECT_TRUE(normal->as_dict()->contains("Off"));
  EXPECT_GT(ink(document, Rect{40, 146, 54, 160}), 10U);

  // Radio buttons: the widget with the state comes on, the others go off.
  ASSERT_TRUE(fill_field(document, {19, 0}, {"Blue"}).has_value());
  EXPECT_EQ(dict_of(document, 19).find("V")->as_name()->bytes, "Blue");
  EXPECT_EQ(dict_of(document, 17).find("AS")->as_name()->bytes, "Off");
  EXPECT_EQ(dict_of(document, 18).find("AS")->as_name()->bytes, "Blue");
  EXPECT_EQ(fill_field(document, {19, 0}, {"Green"}).error().code, ErrorCode::kOutOfRange);

  Document reread = open(save(document));
  EXPECT_EQ(field(fields_of(reread), "color").value, std::vector<std::string>{"Blue"});
  EXPECT_EQ(field(fields_of(reread), "agree").value, std::vector<std::string>{"Off"});
  EXPECT_EQ(field(fields_of(reread), "bare").value, std::vector<std::string>{"Yes"});
}

TEST(Forms, ChoicesTakeTheirOptions) {
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {20, 0}, {"Milano"}).has_value());
  EXPECT_NE(appearance(document, 20).second.find(hex("Milano")), std::string::npos);
  EXPECT_EQ(fill_field(document, {20, 0}, {"Napoli"}).error().code, ErrorCode::kOutOfRange);  // not editable
  EXPECT_EQ(fill_field(document, {20, 0}, {"Roma", "Milano"}).error().code, ErrorCode::kOutOfRange);

  ASSERT_TRUE(fill_field(document, {21, 0}, {"it", "de"}).has_value());
  const Dict& langs = dict_of(document, 21);
  ASSERT_NE(langs.find("V")->as_array(), nullptr);
  EXPECT_EQ(langs.find("V")->as_array()->size(), 2U);
  ASSERT_NE(langs.find("I"), nullptr);
  EXPECT_EQ(langs.find("I")->as_array()->at(1).as_integer(), 2);
  const std::string list = appearance(document, 21).second;
  EXPECT_NE(list.find(hex("Italiano")), std::string::npos);  // the options are shown by their display text
  EXPECT_NE(list.find(hex("English")), std::string::npos);
  EXPECT_NE(list.find("0.6 0.75 0.85 rg"), std::string::npos);  // the chosen ones highlighted

  Document reread = open(save(document));
  EXPECT_EQ(field(fields_of(reread), "langs").value, (std::vector<std::string>{"it", "de"}));
  EXPECT_EQ(field(fields_of(reread), "city").value, std::vector<std::string>{"Milano"});
}

TEST(Forms, RefusalsChangeNothing) {
  Document document = open(form_pdf());
  EXPECT_EQ(fill_field(document, {22, 0}, {"si"}).error().code, ErrorCode::kUnsupported);     // read-only
  EXPECT_EQ(fill_field(document, {23, 0}, {"x"}).error().code, ErrorCode::kUnsupported);      // signature
  EXPECT_EQ(fill_field(document, {24, 0}, {"x"}).error().code, ErrorCode::kUnsupported);      // push button
  EXPECT_EQ(fill_field(document, {13, 0}, {"ABCDEF"}).error().code, ErrorCode::kOutOfRange);  // /MaxLen 5
  EXPECT_EQ(fill_field(document, {11, 0}, {"x"}).error().code, ErrorCode::kOutOfRange);       // not terminal
  EXPECT_EQ(fill_field(document, {5, 0}, {"x"}).error().code, ErrorCode::kOutOfRange);        // not a field
  EXPECT_FALSE(document.has_changes());
}

TEST(Forms, IncrementalSaveAppendsOnlyWhatChanged) {
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {10, 0}, {"x"}).has_value());
  // The field (its widget too) and its new appearance.
  EXPECT_EQ(document.changed_objects(), (std::vector<std::uint32_t>{10, 32}));
}

TEST(Forms, XfaIsRemovedWhenFilling) {
  Document document = open(form_pdf("/XFA 7 0 R"));
  ASSERT_TRUE(fill_field(document, {10, 0}, {"x"}).has_value());
  EXPECT_EQ(dict_of(document, 4).find("XFA"), nullptr);
  EXPECT_NE(dict_of(document, 4).find("Fields"), nullptr);
  EXPECT_EQ(document.state(1), ObjectState::kUnmodified);  // the catalog had nothing to change
}

TEST(Forms, ButtonValuesWrittenAsStrings) {
  // Some producers write a check box's /V as a string: read as the state.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[4 0 R]/XFA 5 0 R>>/NeedsRendering true>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Annots[4 0 R]>>");
  b.add_object(4, "<</FT/Btn/T(c)/V(Yes)/Subtype/Widget/P 3 0 R/Rect[10 10 24 24]>>");
  b.add_object(5, "<<>>");
  b.finish_table("/Size 6/Root 1 0 R");
  Document document = open(b.str());
  EXPECT_EQ(field(fields_of(document), "c").value, std::vector<std::string>{"Yes"});
  ASSERT_TRUE(fill_field(document, {4, 0}, {"Off"}).has_value());
  EXPECT_EQ(field(fields_of(document), "c").value, std::vector<std::string>{"Off"});
  // XFA went, and the catalog's /NeedsRendering with it.
  EXPECT_EQ(dict_of(document, 1).find("NeedsRendering"), nullptr);
  EXPECT_EQ(dict_of(document, 1).find("AcroForm")->as_dict()->find("XFA"), nullptr);
}

TEST(Forms, LongValuesWrapInLinearTime) {
  // A value of 200 000 characters without a space in a multiline field: cut line by line, at most
  // 1000 lines, quickly (the cut used to measure every prefix: quadratic).
  Document document = open(form_pdf());
  ASSERT_TRUE(fill_field(document, {12, 0}, {std::string(200'000, 'W')}).has_value());
  const std::string content = appearance(document, 12).second;
  std::size_t lines = 0;
  for (std::size_t at = content.find(" Tj"); at != std::string::npos; at = content.find(" Tj", at + 1)) {
    ++lines;
  }
  EXPECT_GT(lines, 2U);
  EXPECT_LE(lines, 1000U);
}

TEST(Forms, HostileTreesAreBounded) {
  // A cycle through /Kids, a kid that is not a dictionary, a field without a type; the AcroForm direct in
  // the catalog, with XFA.
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[4 0 R 6 0 R 7 0 R]/XFA[(a)9 0 R]>>>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]>>");
  b.add_object(4, "<</FT/Tx/T(a)/Kids[5 0 R 8 0 R 10 0 R]>>");
  b.add_object(5, "<</T(b)/Kids[4 0 R]>>");
  b.add_object(6, "<</T(untyped)/Subtype/Widget/Rect[0 0 10 10]>>");
  b.add_object(7, "<</FT/Tx/T(ok)/Subtype/Widget/Rect[0 0 10 10]>>");
  b.add_object(8, "42");
  b.add_object(9, "<<>>");
  b.add_object(10, "<</T null/Kids null/Subtype/Widget/Rect[0 20 10 30]>>");  // null entries: as if absent, a widget of "a"
  b.finish_table("/Size 11/Root 1 0 R");
  Document document = open(b.str());
  const auto fields = fields_of(document);
  ASSERT_EQ(fields.size(), 2U);
  EXPECT_EQ(fields[0].name, "a");
  ASSERT_EQ(fields[0].widgets.size(), 1U);
  EXPECT_EQ(fields[0].widgets[0].ref, (ObjectRef{10, 0}));
  EXPECT_EQ(fields[1].name, "ok");
  EXPECT_FALSE(fields[1].widgets[0].page.has_value());  // on no page: /Rect as it is
  EXPECT_EQ(fields[1].widgets[0].rect, (Rect{0, 0, 10, 10}));
  ASSERT_TRUE(fill_field(document, {7, 0}, {"x"}).has_value());
  const Object* form = dict_of(document, 1).find("AcroForm");
  ASSERT_NE(form, nullptr);
  EXPECT_EQ(form->as_dict()->find("XFA"), nullptr);
  EXPECT_NE(form->as_dict()->find("Fields"), nullptr);
}

TEST(Forms, RepairsFoundWhileFillingRefuseIt) {
  // Regression (unsafe/x008 corpus): the page tree is rebuilt only when the pages are first walked — by
  // the fill, looking for its widgets' pages. The structure is checked again before writing: refused,
  // nothing changed (filling field after field used to change the first and refuse the others).
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R/AcroForm<</Fields[4 0 R 5 0 R]>>>>");
  b.add_object(2, "42");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Annots[4 0 R 5 0 R]>>");
  b.add_object(4, "<</FT/Tx/T(a)/Subtype/Widget/P 3 0 R/Rect[10 10 90 30]>>");
  b.add_object(5, "<</FT/Tx/T(b)/Subtype/Widget/P 3 0 R/Rect[10 40 90 60]>>");
  b.finish_table("/Size 6/Root 1 0 R");
  Document document = open(b.str());
  EXPECT_EQ(fill_field(document, {4, 0}, {"x"}).error().code, ErrorCode::kUnsupported);
  EXPECT_FALSE(document.has_changes());
  EXPECT_EQ(fill_field(document, {5, 0}, {"y"}).error().code, ErrorCode::kUnsupported);
  EXPECT_FALSE(document.has_changes());
}

TEST(Forms, DefaultAppearancesAreReadAsContent) {
  using detail::parse_da;
  EXPECT_EQ(parse_da("/Helv 0 Tf 0 0 1 rg").font, "Helv");
  EXPECT_EQ(parse_da("/Helv 0 Tf 0 0 1 rg").size, 0);
  EXPECT_EQ(parse_da("/Helv 0 Tf 0 0 1 rg").color, "0 0 1 rg");
  EXPECT_EQ(parse_da("/F1 9.5 Tf .5 g").color, "0.5 g");
  EXPECT_EQ(parse_da("/F1 9.5 Tf 1 0 0 0 k").color, "1 0 0 0 k");
  EXPECT_EQ(parse_da("/F1 9 Tf 2 g").color, "1 g");     // clamped
  EXPECT_EQ(parse_da("/F1 9 Tf 0 0 rg").color, "0 g");  // too few operands: black
  EXPECT_EQ(parse_da("").font, "");
  EXPECT_EQ(parse_da("/A#20B 7 Tf").font, "A B");  // a name as the lexer reads it
  // Regression (unsafe/x008 corpus): "/Helv<HT>12<NUL>Tf0 g". NUL is white-space (§7.2.3); "Tf0" is an
  // unknown operator: no font, no size (auto), and the g has no operand: black — read as text, "12"
  // became the grey of a white "1 g", and the value did not show.
  const detail::DefaultAppearance broken = parse_da(std::string_view("/Helv\t12\0Tf0 g", 15));
  EXPECT_EQ(broken.font, "");
  EXPECT_EQ(broken.size, 0);
  EXPECT_EQ(broken.color, "0 g");
}

TEST(Forms, NoFormNoFields) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R>>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]>>");
  b.finish_table("/Size 4/Root 1 0 R");
  Document document = open(b.str());
  EXPECT_TRUE(fields_of(document).empty());
  EXPECT_EQ(fill_field(document, {1, 0}, {"x"}).error().code, ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace capriole::edit
