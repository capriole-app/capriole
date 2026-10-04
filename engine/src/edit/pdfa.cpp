#include "capriole/edit/pdfa.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lcms2.h>

#include "capriole/content/appearance.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/attachments.hpp"
#include "capriole/edit/facturx.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "common.hpp"
#include "font/font.hpp"
#include "sound.hpp"
#include "xmp.hpp"

namespace capriole::edit {

namespace {

using detail::name;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// Annotation flags (§12.5.3).
constexpr std::int64_t kInvisible = 1;
constexpr std::int64_t kHidden = 1 << 1;
constexpr std::int64_t kPrint = 1 << 2;
constexpr std::int64_t kNoView = 1 << 5;
constexpr std::int64_t kToggleNoView = 1 << 8;
// How deep resources and field trees are walked (hostile input).
constexpr int kMaxDepth = 32;

// What was put right, by kind, and what stands in the way.
class Log {
 public:
  void change(const std::string& kind, const std::string& what = {}) {
    auto& list = changes_[kind];
    if (!what.empty() && std::ranges::find(list, what) == list.end() && list.size() < 20) {
      list.push_back(what);
    }
    ++counts_[kind];
  }
  void problem(std::string what) {
    if (std::ranges::find(problems_, what) == problems_.end() && problems_.size() < 50) {
      problems_.push_back(std::move(what));
    }
  }
  [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return problems_; }
  [[nodiscard]] std::vector<std::string> lines() const {
    std::vector<std::string> out;
    for (const auto& [kind, list] : changes_) {
      std::string line = std::format("{}: {}", kind, counts_.at(kind));
      if (!list.empty()) {
        line += " (";
        for (std::size_t i = 0; i < list.size(); ++i) {
          line += (i == 0 ? "" : ", ") + list[i];
        }
        line += ")";
      }
      out.push_back(std::move(line));
    }
    return out;
  }

 private:
  std::map<std::string, std::vector<std::string>> changes_;
  std::map<std::string, std::size_t> counts_;
  std::vector<std::string> problems_;
};

std::string name_of(const Object* value) { return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string(); }

// A dictionary object to change: read, changed by fn, written back when fn says so.
Result<void> change_dict(Document& document, ObjectRef ref, const std::function<bool(Dict&)>& fn) {
  auto loaded = document.get(ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return {};
  }
  Dict dict = *(*loaded)->as_dict();
  if (!fn(dict)) {
    return {};
  }
  return document.set(ref.number, Object(std::move(dict)));
}

// ── What the pages use ─────────────────────────────────────────────────────────────────────────────

struct FontUse {
  std::set<std::pair<std::uint32_t, std::uint32_t>> codes;  // (code, CID) shown visibly (not in invisible rendering modes)
};

struct Usage {
  std::map<std::uint32_t, std::pair<ObjectRef, FontUse>> fonts;  // by object number
  std::string direct_font;                                       // a font shown that is not its own object (where), empty when none
  std::map<std::uint32_t, ObjectRef> invisible_fonts;            // used only in invisible rendering modes
  bool cmyk = false;
  bool rgb = false;
  bool inline_interpolate = false;
};

void scan_page(const graphics::DisplayList& list, Usage& usage) {
  for (const graphics::Color& c : list.colors) {
    usage.cmyk = usage.cmyk || c.family == graphics::ColorSpaceFamily::kCMYK;
    usage.rgb = usage.rgb || c.family == graphics::ColorSpaceFamily::kRGB;
  }
  for (const auto& command : list.commands) {
    if (const auto* text = std::get_if<graphics::Boxed<graphics::ShowText>>(&command)) {
      const graphics::ShowText& run = **text;
      const bool visible = run.render_mode != 3 && run.render_mode != 7;
      if (!visible) {
        if (run.font_ref) {
          usage.invisible_fonts.emplace(run.font_ref->number, *run.font_ref);
        }
        continue;
      }
      if (!run.font_ref) {
        usage.direct_font = std::format("/{} in {}", run.font,
                                        run.stream ? std::format("the content of {} {} R", run.stream->number, run.stream->generation) : "the page's content");
        continue;
      }
      auto& [ref, use] = usage.fonts[run.font_ref->number];
      ref = *run.font_ref;
      for (const graphics::Glyph& g : run.glyphs) {
        use.codes.emplace(g.code, g.cid);
      }
    } else if (const auto* image = std::get_if<graphics::Boxed<graphics::DrawImage>>(&command)) {
      usage.inline_interpolate = usage.inline_interpolate || (!(*image)->xobject && (*image)->interpolate);
    }
  }
}

// Whether /DeviceCMYK or /DeviceRGB is named anywhere under an object (colour spaces of images, shadings,
// separations' alternates, transparency groups), following references; fonts are not walked.
struct Names {
  bool cmyk = false;
  bool rgb = false;
};
void scan_names(Document& document, const Object& object, std::set<std::uint32_t>& seen, Names& names, int depth) {
  if (depth > kMaxDepth) {
    return;
  }
  if (const auto ref = object.as_ref()) {
    if (!seen.insert(ref->number).second || seen.size() > 100'000) {
      return;
    }
    auto loaded = document.get(*ref);
    if (loaded) {
      scan_names(document, **loaded, seen, names, depth + 1);
    }
    return;
  }
  if (const auto* n = object.as_name()) {
    names.cmyk = names.cmyk || n->bytes == "DeviceCMYK" || n->bytes == "CMYK";
    names.rgb = names.rgb || n->bytes == "DeviceRGB" || n->bytes == "RGB";
    return;
  }
  const Dict* dict = object.as_dict();
  if (dict == nullptr && object.as_stream() != nullptr) {
    dict = &object.as_stream()->dict;
  }
  if (dict != nullptr) {
    if (name_of(dict->find("Type")) == "Font" || name_of(dict->find("Type")) == "Page" || name_of(dict->find("Type")) == "Pages") {
      return;
    }
    for (const auto& [key, value] : dict->entries()) {
      if (key == "Parent" || key == "P" || key == "Font" || key == "Popup" || key == "IRT") {
        continue;
      }
      scan_names(document, value, seen, names, depth + 1);
    }
    return;
  }
  if (const auto* array = object.as_array()) {
    for (const Object& item : *array) {
      scan_names(document, item, seen, names, depth + 1);
    }
  }
}

// Fonts written in place in a resource dictionary become objects of their own, so that they can be
// told apart, checked and embedded like the others.
bool font_like(const Dict& d) {
  const std::string subtype = name_of(d.find("Subtype"));
  return name_of(d.find("Type")) == "Font" ||
         ((subtype == "Type1" || subtype == "TrueType" || subtype == "Type0" || subtype == "Type3" || subtype == "MMType1") &&
          (d.find("BaseFont") != nullptr || d.find("FontDescriptor") != nullptr || d.find("Widths") != nullptr));
}

// Replaces the fonts written in place under a dictionary's /Font (and its /Resources), recursively
// through direct dictionaries; true when anything changed.
Result<bool> indirect_fonts_in(Document& document, Dict& dict, int depth) {
  if (depth > 4) {
    return false;
  }
  bool changed = false;
  for (const char* key : {"Font", "Resources"}) {
    const Object* value = dict.find(key);
    if (value == nullptr || value->as_dict() == nullptr) {
      continue;
    }
    Dict inner = *value->as_dict();
    bool inner_changed = false;
    if (std::string_view(key) == "Font") {
      for (const auto& entry : value->as_dict()->entries()) {
        if (entry.value.as_dict() != nullptr && font_like(*entry.value.as_dict())) {
          auto ref = document.add(entry.value);
          if (!ref) {
            return std::unexpected(std::move(ref.error()));
          }
          inner.set(entry.key, Object(*ref));
          inner_changed = true;
        }
      }
    } else {
      auto nested = indirect_fonts_in(document, inner, depth + 1);
      if (!nested) {
        return nested;
      }
      inner_changed = *nested;
    }
    if (inner_changed) {
      dict.set(key, Object(std::move(inner)));
      changed = true;
    }
  }
  return changed;
}

Result<void> indirect_fonts(Document& document, Log& log) {
  for (const ObjectRef ref : document.live_objects()) {
    auto loaded = document.get(ref);
    if (!loaded) {
      continue;
    }
    std::optional<Object> updated;
    if (const Dict* d = (*loaded)->as_dict()) {
      Dict copy = *d;
      // A resource dictionary of its own (the fonts in its /Font), or a dictionary holding one.
      auto inner = indirect_fonts_in(document, copy, 0);
      if (!inner) {
        return std::unexpected(std::move(inner.error()));
      }
      if (*inner) {
        updated = Object(std::move(copy));
      }
    } else if (const model::Stream* st = (*loaded)->as_stream()) {
      model::Stream copy = *st;
      auto inner = indirect_fonts_in(document, copy.dict, 0);
      if (!inner) {
        return std::unexpected(std::move(inner.error()));
      }
      if (*inner) {
        updated = Object(std::move(copy));
      }
    }
    if (updated) {
      if (auto set = document.set(ref.number, std::move(*updated)); !set) {
        return set;
      }
      log.change("fonts written in place made objects of their own");
    }
  }
  return {};
}

bool valid_utf8(std::string_view text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto c = static_cast<unsigned char>(text[i]);
    std::size_t continuation = 0;  // bytes that follow the first
    if (c >= 0x80) {
      if (c >= 0xC2 && c < 0xE0) {
        continuation = 1;
      } else if (c >= 0xE0 && c < 0xF0) {
        continuation = 2;
      } else if (c >= 0xF0 && c <= 0xF4) {
        continuation = 3;
      } else {
        return false;
      }
    }
    if (i + continuation >= text.size() && continuation > 0) {
      return false;
    }
    for (std::size_t k = 1; k <= continuation; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) {
        return false;
      }
    }
    i += continuation + 1;
  }
  return true;
}

// Names PDF/A wants in UTF-8 (6.1.8): the names of resources, and of fonts — under a resource
// dictionary, through the form XObjects it holds.
void check_names(Document& document, const Object& resources, std::set<std::uint32_t>& seen, Log& log, int depth) {
  if (depth > 8) {
    return;
  }
  if (const auto ref = resources.as_ref(); ref && !seen.insert(ref->number).second) {
    return;
  }
  auto r = document.resolve(resources);
  const Dict* dict = r ? r->as_dict() : nullptr;
  if (dict == nullptr) {
    return;
  }
  for (const auto& [category, map] : dict->entries()) {
    auto m = document.resolve(map);
    if (!m || m->as_dict() == nullptr) {
      continue;
    }
    for (const auto& [key, value] : m->as_dict()->entries()) {
      if (!valid_utf8(key)) {
        log.problem("resource names that are not UTF-8 (renaming them would mean rewriting the content)");
      }
      auto v = document.resolve(value);
      if (category == "Font" && v && v->as_dict() != nullptr && !valid_utf8(name_of(v->as_dict()->find("BaseFont")))) {
        log.problem("a font name that is not UTF-8");
      }
      if (category == "XObject" && v && v->as_stream() != nullptr && name_of(v->as_stream()->dict.find("Subtype")) == "Form") {
        if (const Object* inner = v->as_stream()->dict.find("Resources")) {
          check_names(document, *inner, seen, log, depth + 1);
        }
      }
    }
  }
}

// ── Colour: the output intent ───────────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>> srgb_profile() {
  cmsHPROFILE profile = cmsCreate_sRGBProfile();
  if (profile == nullptr) {
    return make_error(ErrorCode::kUnsupported, "the sRGB profile could not be made");
  }
  cmsUInt32Number size = 0;
  std::vector<std::byte> out;
  if (cmsSaveProfileToMem(profile, nullptr, &size) != 0 && size > 0) {
    out.resize(size);
    if (cmsSaveProfileToMem(profile, out.data(), &size) == 0) {
      out.clear();
    }
  }
  cmsCloseProfile(profile);
  if (out.empty()) {
    return make_error(ErrorCode::kUnsupported, "the sRGB profile could not be written");
  }
  return out;
}

// The output intent: the document's own profile when it has one (the first of its output intents),
// else sRGB. Returns the number of components of the profile.
Result<int> output_intent(Document& document, Dict& catalog, Log& log) {
  std::optional<Object> profile;
  int components = 0;
  if (const Object* entry = catalog.find("OutputIntents")) {
    auto intents = document.resolve(*entry);
    if (intents && intents->as_array() != nullptr) {
      for (const Object& item : *intents->as_array()) {
        auto intent = document.resolve(item);
        const Dict* d = intent ? intent->as_dict() : nullptr;
        const Object* dest = d != nullptr ? d->find("DestOutputProfile") : nullptr;
        auto stream = dest != nullptr ? document.resolve(*dest) : Result<Object>(Object());
        if (stream && stream->as_stream() != nullptr) {
          const auto n = stream->as_stream()->dict.find("N") != nullptr ? stream->as_stream()->dict.find("N")->as_integer() : std::nullopt;
          if (n == 1 || n == 3 || n == 4) {
            profile = *dest;
            components = static_cast<int>(*n);
            break;
          }
        }
      }
    }
  }
  if (!profile) {
    auto icc = srgb_profile();
    if (!icc) {
      return std::unexpected(std::move(icc.error()));
    }
    auto compressed = filters::flate_encode(*icc);
    if (!compressed) {
      return std::unexpected(std::move(compressed.error()));
    }
    model::Stream stream;
    stream.dict.set("N", Object(3));
    stream.dict.set("Filter", name("FlateDecode"));
    stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
    auto ref = document.add(Object(std::move(stream)));
    if (!ref) {
      return std::unexpected(std::move(ref.error()));
    }
    profile = Object(*ref);
    components = 3;
    log.change("output intent added", "sRGB IEC61966-2.1");
  } else {
    log.change("output intent kept", std::format("{} components", components));
  }
  Dict intent;
  intent.set("Type", name("OutputIntent"));
  intent.set("S", name("GTS_PDFA1"));
  intent.set("OutputConditionIdentifier", model::String{.bytes = components == 3 ? "sRGB IEC61966-2.1" : "Custom"});
  intent.set("Info", model::String{.bytes = components == 3 ? "sRGB IEC61966-2.1" : "The document's output profile"});
  intent.set("DestOutputProfile", std::move(*profile));
  catalog.set("OutputIntents", Array{Object(std::move(intent))});
  return components;
}

// ── Fonts ───────────────────────────────────────────────────────────────────────────────────────────

struct StandardMetrics {
  std::array<double, 4> bbox{};
  double italic_angle = 0;
  double cap_height = 700;
  double stem_v = 80;
  std::int64_t flags = 32;
};

// Adobe's core-font metrics (their AFM files), which the substitutes share.
StandardMetrics metrics_of(font::detail::StandardFont f) {
  using enum font::detail::StandardFont;
  switch (f) {
    case kSans:
      return {.bbox = {-166, -225, 1000, 931}, .italic_angle = 0, .cap_height = 718, .stem_v = 88, .flags = 32};
    case kSansBold:
      return {.bbox = {-170, -228, 1003, 962}, .italic_angle = 0, .cap_height = 718, .stem_v = 140, .flags = 32 | 262144};
    case kSansItalic:
      return {.bbox = {-170, -225, 1116, 931}, .italic_angle = -12, .cap_height = 718, .stem_v = 88, .flags = 32 | 64};
    case kSansBoldItalic:
      return {.bbox = {-174, -228, 1114, 962}, .italic_angle = -12, .cap_height = 718, .stem_v = 140, .flags = 32 | 64 | 262144};
    case kSerif:
      return {.bbox = {-168, -218, 1000, 898}, .italic_angle = 0, .cap_height = 662, .stem_v = 84, .flags = 32 | 2};
    case kSerifBold:
      return {.bbox = {-168, -218, 1000, 935}, .italic_angle = 0, .cap_height = 676, .stem_v = 139, .flags = 32 | 2 | 262144};
    case kSerifItalic:
      return {.bbox = {-169, -217, 1010, 883}, .italic_angle = -15.5, .cap_height = 653, .stem_v = 76, .flags = 32 | 2 | 64};
    case kSerifBoldItalic:
      return {.bbox = {-200, -218, 996, 921}, .italic_angle = -15, .cap_height = 669, .stem_v = 121, .flags = 32 | 2 | 64 | 262144};
    case kFixed:
      return {.bbox = {-23, -250, 715, 805}, .italic_angle = 0, .cap_height = 562, .stem_v = 51, .flags = 32 | 1};
    case kFixedBold:
      return {.bbox = {-113, -250, 749, 801}, .italic_angle = 0, .cap_height = 562, .stem_v = 106, .flags = 32 | 1 | 262144};
    case kFixedItalic:
      return {.bbox = {-27, -250, 849, 805}, .italic_angle = -12, .cap_height = 562, .stem_v = 51, .flags = 32 | 1 | 64};
    case kFixedBoldItalic:
      return {.bbox = {-57, -250, 869, 801}, .italic_angle = -12, .cap_height = 562, .stem_v = 106, .flags = 32 | 1 | 64 | 262144};
    case kSymbol:
      return {.bbox = {-180, -293, 1090, 1010}, .italic_angle = 0, .cap_height = 700, .stem_v = 85, .flags = 4};
    case kDingbats:
      return {.bbox = {-1, -143, 981, 820}, .italic_angle = 0, .cap_height = 700, .stem_v = 90, .flags = 4};
  }
  return {};
}

std::string base_font(const Dict& font) { return std::string(font::detail::without_subset_tag(name_of(font.find("BaseFont")))); }

// The substitute's program as the font's own: /FontFile3 (CFF), a new descriptor, /Widths of the
// program, an /Encoding naming every code shown (non-symbolic fonts), /Subtype /Type1.
Result<void> embed_substitute(Document& document, ObjectRef ref, Dict font_dict, font::detail::Font& font, font::detail::FontContext& context,
                              font::detail::StandardFont standard, const std::set<std::pair<std::uint32_t, std::uint32_t>>& shown, Log& log) {
  std::set<std::uint32_t> codes;
  for (const auto& [code, cid] : shown) {
    codes.insert(code & 0xFFU);
  }
  const auto data = font::detail::standard_font_data(standard);
  const font::detail::FontProgram* program = context.standard(standard);
  if (data.empty() || program == nullptr) {
    return make_error(ErrorCode::kUnsupported, "substitute font missing");
  }
  const bool symbolic = font::detail::is_symbolic(standard);
  // Every code shown must have its glyph in the substitute.
  // The encoding written whole — every code the font's encoding names, whose glyph the substitute has —,
  // not only the codes seen shown: a code the analysis did not see (content it could not read) must
  // still find its glyph.
  const auto name_of_code = [&](std::uint32_t c, const font::detail::Font::GlyphSource& source) {
    std::string glyph = font.glyph_name(c);
    if (glyph.empty() && source.program == program && source.glyph != 0) {
      glyph = program->glyph_name(source.glyph).value_or("");
    }
    return glyph;
  };
  std::map<std::uint32_t, std::pair<std::string, double>> encoded;  // code → (glyph name, width)
  for (std::uint32_t c = 0; c < 256; ++c) {
    const auto source = font.glyph_source({.code = c, .length = 1, .cid = c}, context);
    if (source.program != program || source.glyph == 0) {
      continue;
    }
    const std::string glyph = symbolic ? std::string() : name_of_code(c, source);
    if (!symbolic && glyph.empty()) {
      continue;
    }
    encoded[c] = {glyph, program->advance(source.glyph).value_or(0)};
  }
  // Every code shown must be there, as wide as the dictionary says (a font of other metrics — Arial
  // Narrow — would move the text): to within 2/1000 of an em.
  const bool given_widths = font_dict.find("Widths") != nullptr;
  for (const std::uint32_t c : codes) {
    const auto found = encoded.find(c);
    if (found == encoded.end()) {
      return make_error(ErrorCode::kUnsupported,
                        std::format("font {}: a character it shows (code {}) has no glyph in its substitute", base_font(font_dict), c));
    }
    if (given_widths && std::abs(found->second.second - font.width({.code = c, .length = 1, .cid = c})) > 2.0) {
      return make_error(ErrorCode::kUnsupported,
                        std::format("font {} is not embedded and its metrics are not those of its substitute (code {})", base_font(font_dict), c));
    }
  }
  if (encoded.empty()) {
    return make_error(ErrorCode::kUnsupported, std::format("font {}: no character of it is in its substitute", base_font(font_dict)));
  }
  const std::uint32_t first = encoded.begin()->first;
  const std::uint32_t last = encoded.rbegin()->first;
  Array widths;
  Array differences;
  std::optional<std::uint32_t> run_end;
  for (std::uint32_t c = first; c <= last; ++c) {
    const auto found = encoded.find(c);
    widths.push_back(detail::number_object(found == encoded.end() ? 0 : std::round(found->second.second * 1000) / 1000));
    if (!symbolic && found != encoded.end()) {
      if (run_end != c) {
        differences.emplace_back(static_cast<std::int64_t>(c));
      }
      differences.emplace_back(model::Name{.bytes = found->second.first});
      run_end = c + 1;
    }
  }

  auto compressed = filters::flate_encode(std::as_bytes(std::span(data.data(), data.size())));
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  model::Stream file;
  file.dict.set("Subtype", name("Type1C"));
  file.dict.set("Filter", name("FlateDecode"));
  file.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto file_ref = document.add(Object(std::move(file)));
  if (!file_ref) {
    return std::unexpected(std::move(file_ref.error()));
  }
  const StandardMetrics m = metrics_of(standard);
  const auto [ascent, descent] = program->ascent_descent();
  std::string font_name = base_font(font_dict);
  std::ranges::replace(font_name, ',', '-');
  std::erase_if(font_name, [](char c) { return c == ' '; });
  Dict descriptor;
  descriptor.set("Type", name("FontDescriptor"));
  descriptor.set("FontName", name(font_name));
  descriptor.set("Flags", Object(m.flags));
  descriptor.set("FontBBox",
                 Array{detail::number_object(m.bbox[0]), detail::number_object(m.bbox[1]), detail::number_object(m.bbox[2]), detail::number_object(m.bbox[3])});
  descriptor.set("ItalicAngle", detail::number_object(m.italic_angle));
  descriptor.set("Ascent", detail::number_object(std::round(ascent)));
  descriptor.set("Descent", detail::number_object(std::round(descent)));
  descriptor.set("CapHeight", detail::number_object(m.cap_height));
  descriptor.set("StemV", detail::number_object(m.stem_v));
  descriptor.set("FontFile3", *file_ref);
  auto descriptor_ref = document.add(Object(std::move(descriptor)));
  if (!descriptor_ref) {
    return std::unexpected(std::move(descriptor_ref.error()));
  }
  font_dict.set("Subtype", name("Type1"));
  font_dict.set("BaseFont", name(font_name));
  font_dict.set("FontDescriptor", *descriptor_ref);
  font_dict.set("FirstChar", Object(static_cast<std::int64_t>(first)));
  font_dict.set("LastChar", Object(static_cast<std::int64_t>(last)));
  font_dict.set("Widths", std::move(widths));
  if (!symbolic) {
    Dict encoding;
    encoding.set("Type", name("Encoding"));
    encoding.set("Differences", std::move(differences));
    font_dict.set("Encoding", std::move(encoding));
  }
  log.change("fonts embedded (substitutes with the same metrics)", base_font(font_dict));
  return document.set(ref.number, Object(std::move(font_dict)));
}

// An embedded font: its program must draw every code shown with the width the dictionary gives, and no
// code may show .notdef (§6.2.11.5, 6.2.11.8).
// A TrueType program (an sfnt) with the advances of some glyphs set (font units from 1000ths of an em):
// its hmtx entries rewritten, nothing else. nullopt when it is not one that can be (a collection, a
// glyph past numberOfHMetrics, which shares the last advance), or is damaged.
std::optional<std::vector<std::byte>> with_advances(std::span<const std::byte> sfnt, const std::map<std::uint32_t, double>& advances) {
  const auto u8 = [&](std::size_t at) { return std::to_integer<std::uint32_t>(sfnt[at]); };
  const auto u16 = [&](std::size_t at) { return (u8(at) << 8U) | u8(at + 1); };
  const auto u32 = [&](std::size_t at) { return (u16(at) << 16U) | u16(at + 2); };
  if (sfnt.size() < 12 || u32(0) == 0x74746366U) {  // 'ttcf'
    return std::nullopt;
  }
  const std::uint32_t tables = u16(4);
  if (12 + (static_cast<std::size_t>(tables) * 16) > sfnt.size()) {
    return std::nullopt;
  }
  std::optional<std::pair<std::size_t, std::size_t>> head;
  std::optional<std::pair<std::size_t, std::size_t>> hhea;
  std::optional<std::pair<std::size_t, std::size_t>> hmtx;
  for (std::uint32_t t = 0; t < tables; ++t) {
    const std::size_t record = 12 + (static_cast<std::size_t>(t) * 16);
    const std::uint32_t tag = u32(record);
    const std::size_t offset = u32(record + 8);
    const std::size_t length = u32(record + 12);
    if (offset > sfnt.size() || length > sfnt.size() - offset) {
      return std::nullopt;
    }
    if (tag == 0x68656164U) {
      head = {offset, length};
    } else if (tag == 0x68686561U) {
      hhea = {offset, length};
    } else if (tag == 0x686D7478U) {
      hmtx = {offset, length};
    }
  }
  if (!head || !hhea || !hmtx || head->second < 20 || hhea->second < 36) {
    return std::nullopt;
  }
  const std::uint32_t units = u16(head->first + 18);
  const std::uint32_t metrics = u16(hhea->first + 34);
  if (units == 0 || static_cast<std::size_t>(metrics) * 4 > hmtx->second) {
    return std::nullopt;
  }
  std::vector<std::byte> out(sfnt.begin(), sfnt.end());
  for (const auto& [glyph, advance] : advances) {
    if (glyph >= metrics) {
      return std::nullopt;
    }
    const auto value = static_cast<std::uint32_t>(std::clamp(std::round(advance * units / 1000.0), 0.0, 65535.0));
    const std::size_t at = hmtx->first + (static_cast<std::size_t>(glyph) * 4);
    out[at] = static_cast<std::byte>(value >> 8U);
    out[at + 1] = static_cast<std::byte>(value & 0xFFU);
  }
  return out;
}

// An embedded font: its program must draw every code shown with the width the dictionary gives, and no
// code may show .notdef (6.2.11.5, 6.2.11.8). Widths that differ in a TrueType program are put right
// in the program (the dictionary's width places the text: the drawing does not change); in another
// program they refuse the conversion.
Result<void> check_embedded(Document& document, ObjectRef holder, font::detail::Font& font, font::detail::FontContext& context, const std::string& label,
                            const std::set<std::pair<std::uint32_t, std::uint32_t>>& codes, Log& log) {
  if (font.kind() == font::detail::Font::Kind::kType3) {
    return {};
  }
  std::map<std::uint32_t, double> mismatched;  // glyph → the dictionary's width
  for (const auto& [c, cid] : codes) {
    const font::detail::Font::Code code{.code = c, .length = 1, .cid = cid};
    const auto source = font.glyph_source(code, context);
    if (!source.embedded) {
      continue;
    }
    if (source.glyph == 0) {
      log.problem(std::format("font {}: a character it shows has no glyph in its embedded program (.notdef, code {})", label, c));
      return {};
    }
    const auto advance = source.program->advance(source.glyph);
    if (advance && std::abs(*advance - font.width(code)) > 1.0) {
      mismatched[source.glyph] = font.width(code);
    }
  }
  if (mismatched.empty()) {
    return {};
  }
  // The program: /FontFile2 of the descriptor of the holder (the font, or a Type 0 font's CIDFont).
  auto holder_loaded = document.get(holder);
  const Dict* holder_dict = holder_loaded && (*holder_loaded)->as_dict() != nullptr ? (*holder_loaded)->as_dict() : nullptr;
  const Object* descriptor_entry = holder_dict != nullptr ? holder_dict->find("FontDescriptor") : nullptr;
  auto descriptor = descriptor_entry != nullptr ? document.resolve(*descriptor_entry) : Result<Object>(Object());
  const Object* file_entry = descriptor && descriptor->as_dict() != nullptr ? descriptor->as_dict()->find("FontFile2") : nullptr;
  auto file = file_entry != nullptr ? document.resolve(*file_entry) : Result<Object>(Object());
  auto data = file && file->as_stream() != nullptr ? document.decode_stream(*file->as_stream()) : Result<std::vector<std::byte>>(std::vector<std::byte>());
  std::optional<std::vector<std::byte>> patched;
  if (data && !data->empty()) {
    patched = with_advances(*data, mismatched);
  }
  if (!patched) {
    log.problem(std::format("font {}: the widths in its dictionary differ from its program's", label));
    return {};
  }
  const std::size_t size = patched->size();
  auto compressed = filters::flate_encode(*patched);
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  model::Stream stream;
  stream.dict.set("Filter", name("FlateDecode"));
  stream.dict.set("Length1", Object(static_cast<std::int64_t>(size)));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto stream_ref = document.add(Object(std::move(stream)));
  if (!stream_ref) {
    return std::unexpected(std::move(stream_ref.error()));
  }
  Dict updated = *descriptor->as_dict();
  updated.set("FontFile2", *stream_ref);
  if (const auto ref = descriptor_entry->as_ref()) {
    if (auto set = document.set(ref->number, Object(std::move(updated))); !set) {
      return set;
    }
  } else {
    Dict h = *holder_dict;
    h.set("FontDescriptor", std::move(updated));
    if (auto set = document.set(holder.number, Object(std::move(h))); !set) {
      return set;
    }
  }
  log.change("TrueType programs given the widths their dictionaries say", label);
  return {};
}

Result<void> fonts(Document& document, const Usage& usage, Log& log) {
  font::detail::FontContext context(nullptr);
  std::vector<std::string> warnings;
  for (const auto& [number, entry] : usage.fonts) {
    const auto& [ref, use] = entry;
    auto loaded = document.get(ref);
    if (!loaded || (*loaded)->as_dict() == nullptr) {
      log.problem(std::format("a font resource that is not a font ({} {} R)", ref.number, ref.generation));
      continue;
    }
    Dict dict = *(*loaded)->as_dict();
    const std::string subtype = name_of(dict.find("Subtype"));
    const std::string label = base_font(dict).empty() ? std::format("{} {} R", ref.number, ref.generation) : base_font(dict);
    if (subtype == "Type3") {
      continue;
    }
    auto font = font::detail::Font::load(document, Object(ref), context, warnings);
    if (!font) {
      log.problem(std::format("font {} cannot be read", label));
      continue;
    }
    if (subtype == "Type0") {
      const Object* descendants = dict.find("DescendantFonts");
      auto array = descendants != nullptr ? document.resolve(*descendants) : Result<Object>(Object());
      const Object* first = array && array->as_array() != nullptr && !array->as_array()->empty() ? &array->as_array()->front() : nullptr;
      const auto cid_ref = first != nullptr ? first->as_ref() : std::nullopt;
      auto cid = first != nullptr ? document.resolve(*first) : Result<Object>(Object());
      const Dict* cid_dict = cid ? cid->as_dict() : nullptr;
      if (cid_dict == nullptr || !font->embedded()) {
        log.problem(std::format("font {} is not embedded (a composite font: no substitute with the same metrics)", label));
        continue;
      }
      // An embedded TrueType CIDFont names its CID → glyph mapping (§9.7.4.2); /CIDSet, optional, goes.
      if (cid_ref && name_of(cid_dict->find("Subtype")) == "CIDFontType2" && cid_dict->find("CIDToGIDMap") == nullptr) {
        if (auto set = change_dict(document, *cid_ref,
                                   [](Dict& d) {
                                     d.set("CIDToGIDMap", name("Identity"));
                                     return true;
                                   });
            !set) {
          return set;
        }
        log.change("TrueType CIDFonts given /CIDToGIDMap", label);
      }
      if (const auto descriptor = cid_dict->find("FontDescriptor") != nullptr ? cid_dict->find("FontDescriptor")->as_ref() : std::nullopt) {
        if (auto set = change_dict(document, *descriptor,
                                   [&](Dict& d) {
                                     if (d.find("CIDSet") == nullptr) {
                                       return false;
                                     }
                                     d.erase("CIDSet");
                                     log.change("/CIDSet removed", label);
                                     return true;
                                   });
            !set) {
          return set;
        }
      }
      if (cid_ref) {
        if (auto checked = check_embedded(document, *cid_ref, *font, context, label, use.codes, log); !checked) {
          return checked;
        }
      }
      continue;
    }
    // Embedded: a program the engine can open (a /FontFile that is not a stream, or not a font, is not).
    if (font->embedded()) {
      if (const auto descriptor = dict.find("FontDescriptor")->as_ref()) {
        if (auto set = change_dict(document, *descriptor,
                                   [&](Dict& d) {
                                     if (d.find("CharSet") == nullptr) {
                                       return false;
                                     }
                                     d.erase("CharSet");
                                     log.change("/CharSet removed", label);
                                     return true;
                                   });
            !set) {
          return set;
        }
      }
      if (auto checked = check_embedded(document, ref, *font, context, label, use.codes, log); !checked) {
        return checked;
      }
      // A simple font must give its widths (§9.6.2): those of its program, which is what draws it.
      if (auto reread = document.get(ref); reread && (*reread)->as_dict() != nullptr) {
        dict = *(*reread)->as_dict();
      }
      if (font->kind() == font::detail::Font::Kind::kSimple &&
          (dict.find("Widths") == nullptr || dict.find("FirstChar") == nullptr || dict.find("LastChar") == nullptr)) {
        Array widths;
        for (std::uint32_t c = 0; c < 256; ++c) {
          const auto source = font->glyph_source({.code = c, .length = 1, .cid = c}, context);
          const double w = source.program != nullptr && source.glyph != 0 ? source.program->advance(source.glyph).value_or(0) : 0;
          widths.push_back(detail::number_object(std::round(w * 1000) / 1000));
        }
        dict.set("FirstChar", Object(0));
        dict.set("LastChar", Object(255));
        dict.set("Widths", std::move(widths));
        if (auto set = document.set(ref.number, Object(dict)); !set) {
          return set;
        }
        log.change("widths written for embedded fonts that had none", label);
      }
      continue;
    }
    const auto standard = font::detail::standard_font_named(base_font(dict));
    if (!standard) {
      log.problem(std::format("font {} is not embedded and no font with the same metrics can stand in for it", label));
      continue;
    }
    if (auto embedded = embed_substitute(document, ref, dict, *font, context, *standard, use.codes, log); !embedded) {
      if (embedded.error().code != ErrorCode::kUnsupported) {
        return embedded;
      }
      log.problem(embedded.error().message);
    }
  }
  // Fonts only used for invisible text need no program (6.2.11.4.1), but a whole dictionary (6.2.11.2): a
  // simple one without its widths gets those it is drawn with.
  for (const auto& [number, ref] : usage.invisible_fonts) {
    if (usage.fonts.contains(number)) {
      continue;
    }
    auto loaded = document.get(ref);
    if (!loaded || (*loaded)->as_dict() == nullptr) {
      continue;
    }
    Dict dict = *(*loaded)->as_dict();
    const std::string subtype = name_of(dict.find("Subtype"));
    if ((subtype != "Type1" && subtype != "TrueType" && subtype != "MMType1") ||
        (dict.find("Widths") != nullptr && dict.find("FirstChar") != nullptr && dict.find("LastChar") != nullptr)) {
      continue;
    }
    auto font = font::detail::Font::load(document, Object(ref), context, warnings);
    if (!font) {
      continue;
    }
    const std::string label = base_font(dict);
    Array widths;
    for (std::uint32_t c = 0; c < 256; ++c) {
      widths.push_back(detail::number_object(std::round(font->width({.code = c, .length = 1, .cid = c}) * 1000) / 1000));
    }
    dict.set("FirstChar", Object(0));
    dict.set("LastChar", Object(255));
    dict.set("Widths", std::move(widths));
    if (auto set = document.set(ref.number, Object(std::move(dict))); !set) {
      return set;
    }
    log.change("widths written for fonts of invisible text", label);
  }
  if (!usage.direct_font.empty()) {
    log.problem(
        std::format("a font that is not an object of its own in the resources (or not a font at all) cannot be checked or embedded: {}", usage.direct_font));
  }
  return {};
}

// ── Actions, annotations, forms ─────────────────────────────────────────────────────────────────────

bool forbidden_action(Document& document, const Object* action, int depth = 0) {
  if (action == nullptr || depth > kMaxDepth) {
    return false;
  }
  auto resolved = document.resolve(*action);
  if (resolved && resolved->as_array() != nullptr) {
    return std::ranges::any_of(*resolved->as_array(), [&](const Object& a) { return forbidden_action(document, &a, depth + 1); });
  }
  const Dict* d = resolved ? resolved->as_dict() : nullptr;
  if (d == nullptr) {
    return false;
  }
  static constexpr std::array<std::string_view, 11> kForbidden = {"Launch",      "Sound",     "Movie", "ResetForm",  "ImportData", "Hide",
                                                                  "SetOCGState", "Rendition", "Trans", "GoTo3DView", "JavaScript"};
  const std::string kind = name_of(d->find("S"));
  if (std::ranges::find(kForbidden, kind) != kForbidden.end() ||
      (kind != "GoTo" && kind != "GoToR" && kind != "GoToE" && kind != "Thread" && kind != "URI" && kind != "Named" && kind != "SubmitForm" && !kind.empty())) {
    return true;
  }
  if (kind == "Named") {
    const std::string n = name_of(d->find("N"));
    if (n != "NextPage" && n != "PrevPage" && n != "FirstPage" && n != "LastPage") {
      return true;
    }
  }
  return forbidden_action(document, d->find("Next"), depth + 1);
}

// Removes /AA, and /A when the action (or one it leads to) is forbidden; true when anything went.
bool strip_actions(Document& document, Dict& dict, Log& log) {
  bool changed = false;
  if (dict.find("AA") != nullptr) {
    dict.erase("AA");
    log.change("additional actions (/AA) removed");
    changed = true;
  }
  if (forbidden_action(document, dict.find("A"))) {
    dict.erase("A");
    log.change("actions PDF/A forbids removed (JavaScript, Launch...)");
    changed = true;
  }
  return changed;
}

// The form's fields: /AA and forbidden /A go (6.4.1); NeedAppearances and XFA go.
Result<void> form(Document& document, Dict& catalog, Log& log) {
  const Object* entry = catalog.find("AcroForm");
  if (entry == nullptr) {
    return {};
  }
  auto resolved = document.resolve(*entry);
  if (!resolved || resolved->as_dict() == nullptr) {
    return {};
  }
  Dict acro = *resolved->as_dict();
  bool changed = false;
  for (const char* key : {"NeedAppearances", "XFA"}) {
    if (acro.find(key) != nullptr) {
      acro.erase(key);
      log.change(std::format("/{} removed from the form", key));
      changed = true;
    }
  }
  std::set<std::uint32_t> seen;
  std::function<Result<void>(const Object&, int)> walk = [&](const Object& field, int depth) -> Result<void> {
    const auto ref = field.as_ref();
    if (!ref || depth > kMaxDepth || !seen.insert(ref->number).second) {
      return {};
    }
    std::optional<Array> kids;
    if (auto set = change_dict(document, *ref,
                               [&](Dict& d) {
                                 if (const Object* k = d.find("Kids")) {
                                   auto r = document.resolve(*k);
                                   if (r && r->as_array() != nullptr) {
                                     kids = *r->as_array();
                                   }
                                 }
                                 return strip_actions(document, d, log);
                               });
        !set) {
      return set;
    }
    if (kids) {
      for (const Object& kid : *kids) {
        if (auto w = walk(kid, depth + 1); !w) {
          return w;
        }
      }
    }
    return {};
  };
  if (const Object* fields = acro.find("Fields")) {
    auto list = document.resolve(*fields);
    if (list && list->as_array() != nullptr) {
      // Only entries that are fields (a damaged file's may lead nowhere).
      Array kept;
      for (const Object& field : *list->as_array()) {
        auto f = document.resolve(field);
        if (!f || f->as_dict() == nullptr) {
          changed = true;
          log.change("form entries that are not fields removed");
          continue;
        }
        kept.push_back(field);
        if (auto w = walk(field, 0); !w) {
          return w;
        }
      }
      if (changed) {
        acro.set("Fields", std::move(kept));
      }
    }
  }
  if (!changed) {
    return {};
  }
  if (const auto ref = entry->as_ref()) {
    return document.set(ref->number, Object(std::move(acro)));
  }
  catalog.set("AcroForm", std::move(acro));
  return {};
}

std::optional<std::string> inherited_field_type(Document& document, const Dict& widget) {
  const Dict* d = &widget;
  Result<Object> holder = Object();
  for (int depth = 0; d != nullptr && depth < kMaxDepth; ++depth) {
    if (const Object* ft = d->find("FT"); ft != nullptr && ft->as_name() != nullptr) {
      return ft->as_name()->bytes;
    }
    const Object* parent = d->find("Parent");
    if (parent == nullptr) {
      break;
    }
    holder = document.resolve(*parent);
    d = holder ? holder->as_dict() : nullptr;
  }
  return std::nullopt;
}

// The annotations of a page (6.3): kept, put right, or removed.
Result<void> annotations(Document& document, std::uint32_t index, bool allow_files, Log& log) {
  auto page = detail::page_at(document, index, "PDF/A");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  // The page's own /AA; /Contents only streams (a damaged file's may hold other things).
  bool page_changed = false;
  Dict page_dict = page->dict;
  if (page_dict.find("AA") != nullptr) {
    page_dict.erase("AA");
    log.change("additional actions (/AA) removed");
    page_changed = true;
  }
  if (const Object* contents = page_dict.find("Contents")) {
    auto c = document.resolve(*contents);
    if (c && c->as_array() != nullptr) {
      Array streams;
      for (const Object& item : *c->as_array()) {
        auto v = document.resolve(item);
        if (v && v->as_stream() != nullptr) {
          streams.push_back(item);
        }
      }
      if (streams.size() != c->as_array()->size()) {
        page_dict.set("Contents", std::move(streams));
        log.change("page contents that are not streams removed");
        page_changed = true;
      }
    } else if (!c || c->as_stream() == nullptr) {
      page_dict.erase("Contents");
      log.change("page contents that are not streams removed");
      page_changed = true;
    }
  }
  if (page_changed) {
    if (auto set = document.set(page->ref.number, Object(std::move(page_dict))); !set) {
      return set;
    }
    page = detail::page_at(document, index, "PDF/A");
    if (!page) {
      return std::unexpected(std::move(page.error()));
    }
  }
  detail::Annots annots = detail::annots_of(document, *page);
  Array kept;
  bool removed = false;
  static constexpr std::array<std::string_view, 21> kKnown = {"Text",     "Link",      "FreeText",       "Line",     "Square",      "Circle",    "Polygon",
                                                              "PolyLine", "Highlight", "Underline",      "Squiggly", "StrikeOut",   "Stamp",     "Caret",
                                                              "Ink",      "Popup",     "FileAttachment", "Widget",   "PrinterMark", "Watermark", "Redact"};
  for (const Object& original : annots.list) {
    // An annotation written in place in /Annots becomes an object of its own (to be put right like the others).
    Object item = original;
    if (!item.as_ref() && item.as_dict() != nullptr) {
      auto added = document.add(item);
      if (!added) {
        return std::unexpected(std::move(added.error()));
      }
      item = Object(*added);
      removed = true;  // the list is written anew
    }
    const auto ref = item.as_ref();
    auto resolved = document.resolve(item);
    const Dict* a = resolved ? resolved->as_dict() : nullptr;
    if (a == nullptr) {
      removed = true;
      continue;
    }
    const std::string subtype = name_of(a->find("Subtype"));
    const auto flags = a->find("F") != nullptr ? a->find("F")->as_integer().value_or(0) : 0;
    const auto rect = detail::rect_of(a->find("Rect") != nullptr ? std::optional<Object>(*a->find("Rect")) : std::nullopt);
    const bool shown_nowhere = (flags & (kHidden | kInvisible | kNoView)) != 0;
    if (std::ranges::find(kKnown, subtype) == kKnown.end() || (subtype == "FileAttachment" && !allow_files) || (shown_nowhere && subtype != "Popup")) {
      log.change("annotations removed (hidden, or of types PDF/A forbids)", subtype.empty() ? "?" : subtype);
      removed = true;
      continue;
    }
    if (!ref) {
      kept.push_back(item);
      continue;
    }
    Dict d = *a;
    bool changed = strip_actions(document, d, log);
    if (subtype != "Popup" && ((flags & kPrint) == 0 || (flags & kToggleNoView) != 0)) {
      d.set("F", Object((flags | kPrint) & ~kToggleNoView));
      log.change("annotations made printable");
      changed = true;
    }
    // The appearance (6.3.3): only /N — for a button widget a dictionary of states (streams), for every
    // other annotation a stream. One that is not so counts as none: required but for popups, links and
    // annotations of no size, it is made when the engine can, else the annotation goes.
    const bool sized = rect && rect->width() > 0 && rect->height() > 0;
    const bool button = subtype == "Widget" && inherited_field_type(document, d) == std::optional<std::string>("Btn");
    const Object* ap_entry = d.find("AP");
    auto ap = ap_entry != nullptr ? document.resolve(*ap_entry) : Result<Object>(Object());
    const Dict* ap_dict = ap ? ap->as_dict() : nullptr;
    const Object* n_entry = ap_dict != nullptr ? ap_dict->find("N") : nullptr;
    auto n = n_entry != nullptr ? document.resolve(*n_entry) : Result<Object>(Object());
    std::optional<Object> normal;  // the /N to write
    if (n && n->as_stream() != nullptr) {
      if (button) {
        const std::string state = name_of(d.find("AS")).empty() ? "Off" : name_of(d.find("AS"));
        Dict states;
        states.set(state, *n_entry);
        normal = Object(std::move(states));
        d.set("AS", name(state));
      } else {
        normal = *n_entry;
      }
    } else if (n && n->as_dict() != nullptr) {
      const Dict& states = *n->as_dict();
      if (button) {
        Dict streams;
        for (const auto& [state, value] : states.entries()) {
          auto v = document.resolve(value);
          if (v && v->as_stream() != nullptr) {
            streams.set(state, value);
          }
        }
        if (!streams.entries().empty()) {
          if (streams.find(name_of(d.find("AS"))) == nullptr) {
            d.set("AS", name(streams.entries().front().key));
          }
          normal = Object(std::move(streams));
        }
      } else {
        const Object* chosen = states.find(name_of(d.find("AS")));
        for (const auto& entry : states.entries()) {
          if (chosen != nullptr) {
            break;
          }
          chosen = &entry.value;
        }
        auto v = chosen != nullptr ? document.resolve(*chosen) : Result<Object>(Object());
        if (v && v->as_stream() != nullptr) {
          normal = *chosen;
        }
      }
    }
    if (!normal && subtype != "Popup" && subtype != "Link" && sized) {
      const std::string content = subtype == "Widget" ? std::string() : content::markup_appearance(document, subtype, d, *rect);
      if (content.empty()) {
        log.change("annotations removed (no appearance the engine can make)", subtype);
        removed = true;
        continue;
      }
      auto stream = document.add(Object(detail::form_xobject(*rect, Dict(), content)));
      if (!stream) {
        return std::unexpected(std::move(stream.error()));
      }
      normal = Object(*stream);
      log.change("annotation appearances made", subtype);
    }
    if (normal) {
      const bool same =
          ap_entry != nullptr && !ap_entry->as_ref() && ap_dict != nullptr && ap_dict->entries().size() == 1 && n_entry != nullptr && *n_entry == *normal;
      if (!same) {
        Dict only;
        only.set("N", std::move(*normal));
        d.set("AP", std::move(only));
        changed = true;
      }
    } else if (ap_entry != nullptr) {
      d.erase("AP");  // nothing to show (a popup, a link, an annotation of no size)
      changed = true;
    }
    if (changed) {
      if (auto set = document.set(ref->number, Object(std::move(d))); !set) {
        return set;
      }
    }
    kept.push_back(item);
  }
  if (removed) {
    annots.list = std::move(kept);
    return detail::write_annots(document, *page, std::move(annots));
  }
  return {};
}

// Removes /Metadata from a dictionary and the dictionaries written in place in it (marked-content property
// lists in resources, ...); true when any went.
bool strip_metadata(Dict& dict, int depth) {
  bool changed = false;
  if (dict.find("Metadata") != nullptr) {
    dict.erase("Metadata");
    changed = true;
  }
  if (depth > 6) {
    return changed;
  }
  const Dict before = dict;
  for (const auto& entry : before.entries()) {
    if (entry.value.as_dict() != nullptr) {
      Dict inner = *entry.value.as_dict();
      if (strip_metadata(inner, depth + 1)) {
        dict.set(entry.key, Object(std::move(inner)));
        changed = true;
      }
    }
  }
  return changed;
}

// ── Resources: graphics states, images, forms ───────────────────────────────────────────────────────

Result<void> resources(Document& document, Log& log) {
  for (const ObjectRef ref : document.live_objects()) {
    auto loaded = document.get(ref);
    if (!loaded) {
      continue;
    }
    const Object& object = **loaded;
    if (const auto* stream = object.as_stream()) {
      const Dict& dict = stream->dict;
      const std::string subtype = name_of(dict.find("Subtype"));
      const std::string type = name_of(dict.find("Type"));
      std::optional<model::Stream> changed;
      const auto edit = [&]() -> model::Stream& {
        if (!changed) {
          changed = *stream;
        }
        return *changed;
      };
      // Metadata of objects (an image's, a form's): their XMP is not the document's, and PDF/A checks every
      // property of it (6.6.2.3.1); it goes.
      if (type != "Metadata") {
        Dict d = dict;
        if (strip_metadata(d, 0)) {
          edit().dict = std::move(d);
          log.change("metadata of objects removed");
        }
      }
      if (subtype == "Image" && (type.empty() || type == "XObject")) {
        for (const char* key : {"Alternates", "OPI"}) {
          if (dict.find(key) != nullptr) {
            edit().dict.erase(key);
            log.change(std::format("image /{} removed", key));
          }
        }
        if (const Object* i = dict.find("Interpolate"); i != nullptr && i->as_bool() == std::optional<bool>(true)) {
          edit().dict.set("Interpolate", Object(false));
          log.change("image interpolation turned off");
        }
      } else if (subtype == "Form" && (type.empty() || type == "XObject")) {
        if (dict.find("OPI") != nullptr) {
          edit().dict.erase("OPI");
          log.change("form /OPI removed");
        }
        if (dict.find("Ref") != nullptr) {
          log.problem("a reference XObject (content from another file)");
        }
        if (name_of(dict.find("Subtype2")) == "PS") {
          log.problem("a PostScript XObject");
        }
      } else if (subtype == "PS") {
        log.problem("a PostScript XObject");
      }
      if (dict.find("F") != nullptr && dict.find("Length") != nullptr && dict.find("F")->as_dict() == nullptr && name_of(dict.find("Type")) != "EmbeddedFile") {
        log.problem("a stream whose data is in another file");
      }
      // Only the standard filters (6.1.7.2).
      {
        static constexpr std::array<std::string_view, 9> kStandard = {"ASCIIHexDecode", "ASCII85Decode", "LZWDecode", "FlateDecode", "RunLengthDecode",
                                                                      "CCITTFaxDecode", "JBIG2Decode",   "DCTDecode", "JPXDecode"};
        const auto standard = [&](const Object& f) { return std::ranges::find(kStandard, name_of(&f)) != kStandard.end(); };
        const Object* f = dict.find("Filter");
        const bool other = f != nullptr && (f->as_array() != nullptr ? !std::ranges::all_of(*f->as_array(), standard) : !standard(*f));
        if (other) {
          log.problem("a stream with a filter that is not a standard one (or not a name)");
        }
      }
      // LZW (6.1.7.2): decoded and compressed anew with Flate, when that is all its filters do.
      const Object* filter = dict.find("Filter");
      bool lzw = name_of(filter) == "LZWDecode";
      bool only_lossless = true;
      if (filter != nullptr && filter->as_array() != nullptr) {
        for (const Object& f : *filter->as_array()) {
          lzw = lzw || name_of(&f) == "LZWDecode";
          const std::string n = name_of(&f);
          only_lossless = only_lossless && (n == "LZWDecode" || n == "FlateDecode" || n == "ASCIIHexDecode" || n == "ASCII85Decode" || n == "RunLengthDecode");
        }
      }
      if (lzw) {
        if (!only_lossless) {
          log.problem("an LZW stream with other filters");
        } else {
          auto data = document.decode_stream(*stream);
          auto compressed = data ? filters::flate_encode(*data) : Result<std::vector<std::byte>>(std::unexpected(data.error()));
          if (!compressed) {
            log.problem("an LZW stream that cannot be decoded");
          } else {
            model::Stream& s = edit();
            s.dict.set("Filter", name("FlateDecode"));
            s.dict.erase("DecodeParms");
            s.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
            log.change("LZW streams compressed with Flate");
          }
        }
      }
      if (changed) {
        if (auto set = document.set(ref.number, Object(std::move(*changed))); !set) {
          return set;
        }
      }
      continue;
    }
    const Dict* dict = object.as_dict();
    if (dict == nullptr) {
      continue;
    }
    Dict d = *dict;
    bool changed = false;
    // Metadata of objects other than the catalog (a page's, a font descriptor's): as for streams above.
    if (name_of(d.find("Type")) != "Catalog" && strip_metadata(d, 0)) {
      log.change("metadata of objects removed");
      changed = true;
    }
    // A graphics state (6.2.5): no transfer functions (/TR, /TR2 other than /Default).
    const bool tr = d.find("TR") != nullptr;
    const bool tr2 = d.find("TR2") != nullptr && name_of(d.find("TR2")) != "Default";
    if (tr || tr2) {
      d.erase("TR");
      if (tr2) {
        d.erase("TR2");
      }
      log.change("transfer functions removed");
      changed = true;
    }
    if (changed) {
      if (auto set = document.set(ref.number, Object(std::move(d))); !set) {
        return set;
      }
    }
  }
  return {};
}

// ── Metadata ───────────────────────────────────────────────────────────────────────────────────────

std::string xml_text(std::string_view utf8) {
  std::string out;
  for (const char c : utf8) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      continue;
    }
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// A PDF date as XMP's (ISO 8601), as precise as it is; empty when it is not one.
std::string xmp_date(std::string_view pdf) {
  if (pdf.starts_with("D:")) {
    pdf.remove_prefix(2);
  }
  const auto digits = [&](std::size_t at, std::size_t n) {
    if (at + n > pdf.size()) {
      return false;
    }
    return std::ranges::all_of(pdf.substr(at, n), [](char c) { return c >= '0' && c <= '9'; });
  };
  if (!digits(0, 4)) {
    return {};
  }
  std::string out(pdf.substr(0, 4));
  std::size_t at = 4;
  for (const char* sep : {"-", "-", "T", ":", ":"}) {
    if (!digits(at, 2)) {
      break;
    }
    out += sep;
    out += pdf.substr(at, 2);
    at += 2;
  }
  if (at <= 8) {
    return out;  // a date without its time
  }
  if (at == 10) {
    out += ":00";
  }
  if (at < pdf.size() && pdf[at] == 'Z') {
    out += 'Z';
  } else if (at < pdf.size() && (pdf[at] == '+' || pdf[at] == '-') && digits(at + 1, 2)) {
    std::string_view rest = pdf.substr(at + 3);
    if (rest.starts_with('\'')) {
      rest.remove_prefix(1);
    }
    out += std::format("{}{}:{}", pdf[at], pdf.substr(at + 1, 2),
                       rest.size() >= 2 && std::isdigit(static_cast<unsigned char>(rest[0])) != 0 ? rest.substr(0, 2) : "00");
  }
  return out;
}

Result<void> metadata(Document& document, Dict& catalog, int part, const std::string& date, Log& log) {
  // /Info: its text entries, its dates; /ModDate the conversion's, /Trapped (which XMP must then hold) goes.
  std::map<std::string, std::string> info;
  const Object* info_entry = document.trailer().find("Info");
  auto info_value = info_entry != nullptr ? document.resolve(*info_entry) : Result<Object>(Object());
  if (info_value && info_value->as_dict() != nullptr) {
    Dict d = *info_value->as_dict();
    for (const char* key : {"Title", "Author", "Subject", "Keywords", "Creator", "Producer", "CreationDate"}) {
      if (d.find(key) != nullptr) {
        if (std::string_view(key).ends_with("Date")) {
          info[key] = d.find(key)->as_string() != nullptr ? d.find(key)->as_string()->bytes : std::string();
        } else {
          info[key] = detail::utf8_of(d, key);
        }
      }
    }
    if (info.contains("CreationDate") && xmp_date(info["CreationDate"]).empty()) {
      d.erase("CreationDate");
      info.erase("CreationDate");
    }
    // Entries that are not text strings would not agree with XMP: they go.
    for (const char* key : {"Title", "Author", "Subject", "Keywords", "Creator", "Producer"}) {
      if (d.find(key) != nullptr && d.find(key)->as_string() == nullptr) {
        d.erase(key);
        info.erase(key);
      }
    }
    d.erase("Trapped");
    d.set("ModDate", model::String{.bytes = date});
    if (const auto ref = info_entry->as_ref()) {
      if (auto set = document.set(ref->number, Object(std::move(d))); !set) {
        return set;
      }
    } else {
      document.set_trailer_entry("Info", Object(std::move(d)));
    }
  }
  std::string xmp =
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
  xmp += std::format(
      "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n<pdfaid:part>{}</pdfaid:part>\n"
      "<pdfaid:conformance>B</pdfaid:conformance>\n</rdf:Description>\n",
      part);
  std::string dc;
  if (info.contains("Title")) {
    dc += std::format("<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">{}</rdf:li></rdf:Alt></dc:title>\n", xml_text(info["Title"]));
  }
  if (info.contains("Author")) {
    dc += std::format("<dc:creator><rdf:Seq><rdf:li>{}</rdf:li></rdf:Seq></dc:creator>\n", xml_text(info["Author"]));
  }
  if (info.contains("Subject")) {
    dc += std::format("<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">{}</rdf:li></rdf:Alt></dc:description>\n", xml_text(info["Subject"]));
  }
  if (!dc.empty()) {
    xmp += "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n" + dc + "</rdf:Description>\n";
  }
  xmp += "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">\n";
  if (info.contains("CreationDate")) {
    xmp += std::format("<xmp:CreateDate>{}</xmp:CreateDate>\n", xmp_date(info["CreationDate"]));
  }
  xmp += std::format("<xmp:ModifyDate>{0}</xmp:ModifyDate>\n<xmp:MetadataDate>{0}</xmp:MetadataDate>\n", xmp_date(date));
  if (info.contains("Creator")) {
    xmp += std::format("<xmp:CreatorTool>{}</xmp:CreatorTool>\n", xml_text(info["Creator"]));
  }
  xmp += "</rdf:Description>\n";
  if (info.contains("Producer") || info.contains("Keywords")) {
    xmp += "<rdf:Description rdf:about=\"\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n";
    if (info.contains("Producer")) {
      xmp += std::format("<pdf:Producer>{}</pdf:Producer>\n", xml_text(info["Producer"]));
    }
    if (info.contains("Keywords")) {
      xmp += std::format("<pdf:Keywords>{}</pdf:Keywords>\n", xml_text(info["Keywords"]));
    }
    xmp += "</rdf:Description>\n";
  }
  xmp += "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
  // A Factur-X invoice keeps its properties (and their extension schema).
  if (auto invoice = find_facturx(document); invoice && *invoice) {
    if (auto with = detail::with_facturx_properties(xmp, (*invoice)->conformance_level)) {
      xmp = std::move(*with);
      log.change("Factur-X properties kept in the metadata");
    }
  }
  model::Stream stream;
  stream.dict.set("Type", name("Metadata"));
  stream.dict.set("Subtype", name("XML"));
  const auto bytes = std::as_bytes(std::span(xmp));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
  auto ref = document.add(Object(std::move(stream)));
  if (!ref) {
    return std::unexpected(std::move(ref.error()));
  }
  catalog.set("Metadata", *ref);
  log.change("metadata written (XMP, PDF/A identification)", std::format("PDF/A-{}b", part));
  return {};
}

// ── Embedded files (PDF/A-3) ────────────────────────────────────────────────────────────────────────

Result<void> embedded_files(Document& document, Dict& catalog, const std::string& date, Log& log) {
  auto list = list_attachments(document);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  Array af;
  if (const Object* entry = catalog.find("AF")) {
    auto existing = document.resolve(*entry);
    if (existing && existing->as_array() != nullptr) {
      af = *existing->as_array();
    }
  }
  for (const AttachmentInfo& a : *list) {
    // The specification: /AFRelationship (Unspecified when it says none), /UF.
    std::optional<ObjectRef> file_ref;
    if (auto set = change_dict(document, a.spec,
                               [&](Dict& d) {
                                 bool changed = false;
                                 if (d.find("AFRelationship") == nullptr) {
                                   d.set("AFRelationship", name("Unspecified"));
                                   changed = true;
                                 }
                                 if (d.find("UF") == nullptr && d.find("F") != nullptr && d.find("F")->as_string() != nullptr) {
                                   d.set("UF", *d.find("F"));
                                   changed = true;
                                 }
                                 if (const Object* ef = d.find("EF")) {
                                   auto efd = document.resolve(*ef);
                                   if (efd && efd->as_dict() != nullptr) {
                                     const Object* f = efd->as_dict()->find("F") != nullptr ? efd->as_dict()->find("F") : efd->as_dict()->find("UF");
                                     file_ref = f != nullptr ? f->as_ref() : std::nullopt;
                                   }
                                 }
                                 return changed;
                               });
        !set) {
      return set;
    }
    // The file: a MIME type, a modification date.
    if (file_ref) {
      if (auto set = change_dict(document, *file_ref, [&](Dict&) { return false; }); !set) {
        return set;
      }
      auto loaded = document.get(*file_ref);
      if (loaded && (*loaded)->as_stream() != nullptr) {
        model::Stream s = *(*loaded)->as_stream();
        bool changed = false;
        if (s.dict.find("Subtype") == nullptr || s.dict.find("Subtype")->as_name() == nullptr) {
          s.dict.set("Subtype", name("application/octet-stream"));
          changed = true;
        }
        Dict params;
        if (const Object* p = s.dict.find("Params")) {
          auto pd = document.resolve(*p);
          if (pd && pd->as_dict() != nullptr) {
            params = *pd->as_dict();
          }
        }
        if (params.find("ModDate") == nullptr) {
          params.set("ModDate", model::String{.bytes = date});
          s.dict.set("Params", std::move(params));
          changed = true;
        }
        if (changed) {
          if (auto set = document.set(file_ref->number, Object(std::move(s))); !set) {
            return set;
          }
        }
      }
    }
    if (std::ranges::none_of(af, [&](const Object& o) { return o.as_ref() == std::optional<ObjectRef>(a.spec); })) {
      af.emplace_back(a.spec);
    }
    log.change("embedded files made PDF/A-3 attachments", a.file_name.empty() ? a.name : a.file_name);
  }
  if (!af.empty()) {
    catalog.set("AF", std::move(af));
  }
  return {};
}

}  // namespace

namespace {

Result<PdfaResult> convert_written(Document& document, const PdfaOptions& options);

}  // namespace

Result<PdfaResult> convert_to_pdfa(Document& document, const PdfaOptions& options) {
  if (options.part != 0 && options.part != 2 && options.part != 3) {
    return make_error(ErrorCode::kUnsupported, "PDF/A part 2 or 3");
  }
  if (options.date.empty() || !crypto::der::generalized_time(options.date)) {
    return make_error(ErrorCode::kUnsupported, "a conversion to PDF/A needs its date (a PDF date)");
  }
  if (document.security() != nullptr) {
    return make_error(ErrorCode::kUnsupported,
                      "the document cannot be made PDF/A: the document is encrypted (PDF/A forbids encryption; removing it is not done yet)");
  }
  // The conversion works on the document as it is written in full and read back — what a damaged file's
  // repairs make of it, every stream readable as the output will have it —, so that what is checked is
  // what is written (and a converted file converts to itself).
  writer::MemorySink sink;
  writer::SaveOptions save;
  save.drop_unreachable = true;
  if (auto saved = writer::save_full(document, sink, save); !saved) {
    return std::unexpected(std::move(saved.error()));
  }
  auto written = Document::open(std::make_unique<MemoryByteSource>(sink.take()), OpenOptions{.limits = kDefaultParseLimits, .password = "", .repair = true});
  if (!written) {
    return std::unexpected(std::move(written.error()));
  }
  return convert_written(*written, options);
}

namespace {

Result<PdfaResult> convert_written(Document& document, const PdfaOptions& options) {
  if (auto sound = detail::require_sound_structure(document, "a conversion to PDF/A"); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  Log log;
  auto catalog_ptr = document.catalog();
  if (!catalog_ptr) {
    return std::unexpected(std::move(catalog_ptr.error()));
  }
  Dict catalog = **catalog_ptr;
  const Object* root_entry = document.trailer().find("Root");
  const auto root = root_entry != nullptr ? root_entry->as_ref() : std::nullopt;
  if (!root) {
    return make_error(ErrorCode::kUnsupported, "the catalog is not its own object");
  }
  if (const Object* acro = catalog.find("AcroForm")) {
    auto a = document.resolve(*acro);
    if (a && a->as_dict() != nullptr && (a->as_dict()->find("SigFlags") != nullptr)) {
      const auto flags = a->as_dict()->find("SigFlags")->as_integer().value_or(0);
      if ((flags & 1) != 0) {
        log.problem("the document is signed (converting would break its signatures: convert first, then sign)");
      }
    }
  }

  if (auto f = indirect_fonts(document, log); !f) {
    return std::unexpected(std::move(f.error()));
  }

  // What the pages use, as the interpreter draws them (annotations included).
  auto pages = document.page_refs();
  if (!pages) {
    return std::unexpected(std::move(pages.error()));
  }
  Usage usage;
  Names names;
  std::set<std::uint32_t> seen;
  for (const ObjectRef page : *pages) {
    auto list = content::interpret_page(document, page, {}, nullptr, content::Purpose::kText);
    if (!list) {
      return std::unexpected(std::move(list.error()));
    }
    scan_page(*list, usage);
    // Operators PDF does not define (6.2.2: not even between BX and EX).
    if (std::ranges::any_of(list->warnings, [](const std::string& w) { return w.starts_with("unknown operator"); })) {
      log.problem("content with operators PDF does not define");
    }
    auto loaded = document.get(page);
    if (loaded && (*loaded)->as_dict() != nullptr) {
      // Resources the page only inherits are given to it (a content stream needs its own, 6.2.2).
      if ((*loaded)->as_dict()->find("Resources") == nullptr) {
        if (auto inherited = detail::inherited(document, *(*loaded)->as_dict(), "Resources")) {
          Dict d = *(*loaded)->as_dict();
          d.set("Resources", std::move(*inherited));
          if (auto set = document.set(page.number, Object(std::move(d))); !set) {
            return std::unexpected(std::move(set.error()));
          }
          log.change("inherited resources given to their pages");
          loaded = document.get(page);
          if (!loaded || (*loaded)->as_dict() == nullptr) {
            continue;
          }
        }
      }
      if (const Object* resources = (*loaded)->as_dict()->find("Resources")) {
        std::set<std::uint32_t> visited;
        check_names(document, *resources, visited, log, 0);
      }
      for (const char* key : {"Resources", "Annots", "Group"}) {
        if (const Object* value = (*loaded)->as_dict()->find(key)) {
          scan_names(document, *value, seen, names, 0);
        }
      }
    }
  }
  if (usage.inline_interpolate) {
    log.problem("an inline image asks for interpolation");
  }

  // Embedded files decide the part.
  auto attachments = list_attachments(document);
  const bool files = attachments && !attachments->empty();
  const int automatic = files ? 3 : 2;
  const int part = options.part != 0 ? options.part : automatic;
  if (files && part == 2) {
    log.problem("the document has embedded files: PDF/A-2 allows only PDF/A ones (PDF/A-3 allows any)");
  }

  auto components = output_intent(document, catalog, log);
  if (!components) {
    return std::unexpected(std::move(components.error()));
  }
  if ((usage.cmyk || names.cmyk) && *components != 4) {
    log.problem("the document uses DeviceCMYK colour (with an RGB output intent a CMYK profile would be needed)");
  }
  if ((usage.rgb || names.rgb) && *components != 3) {
    log.problem("the document uses DeviceRGB colour, and its output intent is not RGB");
  }

  if (auto f = fonts(document, usage, log); !f) {
    return std::unexpected(std::move(f.error()));
  }
  if (!log.problems().empty()) {
    std::string message = "the document cannot be made PDF/A: ";
    for (std::size_t i = 0; i < log.problems().size(); ++i) {
      message += (i == 0 ? "" : "; ") + log.problems()[i];
    }
    return make_error(ErrorCode::kUnsupported, message);
  }

  for (std::uint32_t i = 0; i < pages->size(); ++i) {
    if (auto a = annotations(document, i, part == 3, log); !a) {
      return std::unexpected(std::move(a.error()));
    }
  }
  if (auto r = resources(document, log); !r) {
    return std::unexpected(std::move(r.error()));
  }
  if (auto f = form(document, catalog, log); !f) {
    return std::unexpected(std::move(f.error()));
  }
  // The catalog: no document actions PDF/A forbids, no JavaScript, no rendering requirements.
  if (catalog.find("AA") != nullptr) {
    catalog.erase("AA");
    log.change("additional actions (/AA) removed");
  }
  if (forbidden_action(document, catalog.find("OpenAction"))) {
    catalog.erase("OpenAction");
    log.change("actions PDF/A forbids removed (JavaScript, Launch...)");
  }
  for (const char* key : {"NeedsRendering", "Requirements"}) {
    if (catalog.find(key) != nullptr) {
      catalog.erase(key);
      log.change(std::format("/{} removed", key));
    }
  }
  if (const Object* names_entry = catalog.find("Names")) {
    auto n = document.resolve(*names_entry);
    if (n && n->as_dict() != nullptr && n->as_dict()->find("JavaScript") != nullptr) {
      Dict d = *n->as_dict();
      d.erase("JavaScript");
      log.change("document JavaScript removed");
      if (const auto ref = names_entry->as_ref()) {
        if (auto set = document.set(ref->number, Object(std::move(d))); !set) {
          return std::unexpected(std::move(set.error()));
        }
      } else {
        catalog.set("Names", std::move(d));
      }
    }
  }
  if (name_of(catalog.find("Version")) > "1.7") {
    catalog.erase("Version");
  }
  // Optional content: every configuration named, none with /AS (6.9).
  if (const Object* oc = catalog.find("OCProperties")) {
    auto ocp = document.resolve(*oc);
    if (ocp && ocp->as_dict() != nullptr) {
      Dict d = *ocp->as_dict();
      Result<void> written;
      const auto fix = [&](const Object& config, int index) -> std::optional<Object> {
        auto c = document.resolve(config);
        if (!c || c->as_dict() == nullptr) {
          return std::nullopt;
        }
        Dict cd = *c->as_dict();
        bool changed = cd.find("AS") != nullptr;
        cd.erase("AS");
        if (cd.find("Name") == nullptr) {
          cd.set("Name", model::String{.bytes = std::format("Configuration {}", index)});
          changed = true;
        }
        if (!changed) {
          return std::nullopt;
        }
        log.change("optional content configurations named, /AS removed");
        if (const auto ref = config.as_ref()) {
          if (auto set = document.set(ref->number, Object(cd)); !set) {
            written = std::move(set);
          }
          return std::nullopt;
        }
        return Object(std::move(cd));
      };
      bool changed = false;
      if (const Object* dconf = d.find("D")) {
        if (auto fixed = fix(*dconf, 0)) {
          d.set("D", std::move(*fixed));
          changed = true;
        }
      }
      if (const Object* configs = d.find("Configs")) {
        auto list = document.resolve(*configs);
        if (list && list->as_array() != nullptr) {
          Array out = *list->as_array();
          for (std::size_t i = 0; i < out.size(); ++i) {
            if (auto fixed = fix(out[i], static_cast<int>(i + 1))) {
              out[i] = std::move(*fixed);
              changed = true;
            }
          }
          if (changed) {
            d.set("Configs", std::move(out));
          }
        }
      }
      if (changed) {
        if (const auto ref = oc->as_ref()) {
          written = document.set(ref->number, Object(d));
        } else {
          catalog.set("OCProperties", std::move(d));
        }
      }
      if (!written) {
        return std::unexpected(std::move(written.error()));
      }
    }
  }
  // What the passes over objects found that cannot be put right.
  if (!log.problems().empty()) {
    std::string message = "the document cannot be made PDF/A: ";
    for (std::size_t i = 0; i < log.problems().size(); ++i) {
      message += (i == 0 ? "" : "; ") + log.problems()[i];
    }
    return make_error(ErrorCode::kUnsupported, message);
  }
  if (part == 3 && files) {
    if (auto e = embedded_files(document, catalog, options.date, log); !e) {
      return std::unexpected(std::move(e.error()));
    }
  }
  if (auto m = metadata(document, catalog, part, options.date, log); !m) {
    return std::unexpected(std::move(m.error()));
  }
  if (auto set = document.set(root->number, Object(std::move(catalog))); !set) {
    return std::unexpected(std::move(set.error()));
  }

  writer::MemorySink sink;
  writer::SaveOptions save;
  save.drop_unreachable = true;
  const auto& header = document.header();
  if (header.major > 1 || header.minor > 7) {
    save.version = std::pair{1, 7};
  }
  if (auto saved = writer::save_full(document, sink, save); !saved) {
    return std::unexpected(std::move(saved.error()));
  }
  return PdfaResult{.file = sink.take(), .part = part, .changes = log.lines()};
}

}  // namespace

}  // namespace capriole::edit
