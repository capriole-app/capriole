#include "font.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "glyph_tables.hpp"

namespace capriole::font::detail {

namespace {

using model::Object;

std::optional<double> number(const Object& object) {
  const auto value = object.as_number();
  return value && std::isfinite(*value) ? value : std::nullopt;
}

std::optional<Object> resolved(Document& document, const model::Dict& dict, std::string_view key) {
  const Object* entry = dict.find(key);
  if (entry == nullptr) {
    return std::nullopt;
  }
  auto value = document.resolve(*entry);
  return value ? std::optional<Object>(std::move(*value)) : std::nullopt;
}

std::string name_value(const model::Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string();
}

std::optional<BaseEncoding> base_encoding(const Object& name) {
  if (name.is_name("WinAnsiEncoding")) {
    return BaseEncoding::kWinAnsi;
  }
  if (name.is_name("MacRomanEncoding")) {
    return BaseEncoding::kMacRoman;
  }
  if (name.is_name("StandardEncoding")) {
    return BaseEncoding::kStandard;
  }
  return std::nullopt;
}

std::string_view text_of(const std::vector<std::byte>& data) {
  return {reinterpret_cast<const char*>(data.data()), data.size()};  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes as text
}

// The font resource pack's names for the substitutes of fonts that are not embedded.
std::string_view liberation_file(StandardFont font) {
  constexpr std::array<std::string_view, 12> kFiles = {
      "LiberationSans-Regular.ttf",  "LiberationSans-Bold.ttf",  "LiberationSans-Italic.ttf",  "LiberationSans-BoldItalic.ttf",
      "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf", "LiberationSerif-Italic.ttf", "LiberationSerif-BoldItalic.ttf",
      "LiberationMono-Regular.ttf",  "LiberationMono-Bold.ttf",  "LiberationMono-Italic.ttf",  "LiberationMono-BoldItalic.ttf",
  };
  const auto index = static_cast<std::size_t>(font);
  return index < kFiles.size() ? kFiles.at(index) : std::string_view();
}

struct Collection {
  std::string_view ordering;
  std::string_view unicode_cmap;  // the collection's UTF-16 CMap: reversed, it gives each CID its character
  std::string_view font;
};
constexpr std::array<Collection, 5> kCollections = {{
    {.ordering = "Japan1", .unicode_cmap = "UniJIS-UTF16-H", .font = "NotoSansJP-Regular.otf"},
    {.ordering = "GB1", .unicode_cmap = "UniGB-UTF16-H", .font = "NotoSansSC-Regular.otf"},
    {.ordering = "CNS1", .unicode_cmap = "UniCNS-UTF16-H", .font = "NotoSansTC-Regular.otf"},
    {.ordering = "Korea1", .unicode_cmap = "UniKS-UTF16-H", .font = "NotoSansKR-Regular.otf"},
    {.ordering = "KR", .unicode_cmap = "UniAKR-UTF16-H", .font = "NotoSansKR-Regular.otf"},
}};

const Collection* collection(std::string_view ordering) {
  for (const Collection& c : kCollections) {
    if (c.ordering == ordering) {
      return &c;
    }
  }
  return nullptr;
}

// The embedded program of a font descriptor (FontFile, FontFile2, FontFile3), or null.
std::unique_ptr<FontProgram> open_embedded(Document& document, const model::Dict* descriptor, const std::string& font_name, const FontLimits& limits,
                                           std::vector<std::string>& warnings) {
  if (descriptor == nullptr) {
    return nullptr;
  }
  for (const std::string_view key : {"FontFile", "FontFile2", "FontFile3"}) {
    const auto stream = resolved(document, *descriptor, key);
    if (!stream || stream->as_stream() == nullptr) {
      continue;
    }
    auto data = document.decode_stream(*stream->as_stream());
    if (!data || data->empty()) {
      warnings.push_back("font /" + font_name + ": embedded program unreadable, a substitute is used");
      return nullptr;
    }
    // Type 1 programs sometimes carry bytes before their "%!" header, which FreeType would refuse.
    if (key == "FontFile" && data->size() > 2 && (*data)[0] != std::byte{0x80} && !text_of(*data).starts_with("%!")) {
      constexpr std::size_t kSearched = 1024;
      const std::size_t start = text_of(*data).substr(0, kSearched).find("%!");
      if (start != std::string_view::npos) {
        data->erase(data->begin(), data->begin() + static_cast<std::ptrdiff_t>(start));
      }
    }
    auto program = FontProgram::open(std::make_shared<const std::vector<std::byte>>(std::move(*data)), limits.program);
    if (!program) {
      warnings.push_back("font /" + font_name + ": embedded program unusable (" + program.error().message + "), a substitute is used");
      return nullptr;
    }
    return std::move(*program);
  }
  return nullptr;
}

struct Descriptor {
  std::uint32_t flags = 0;
  double italic_angle = 0;
  double weight = 400;
  std::optional<double> missing_width;
  std::optional<Object> dict;
};

Descriptor read_descriptor(Document& document, const model::Dict& font) {
  Descriptor d;
  d.dict = resolved(document, font, "FontDescriptor");
  if (!d.dict || d.dict->as_dict() == nullptr) {
    d.dict.reset();
    return d;
  }
  const model::Dict& dict = *d.dict->as_dict();
  if (const auto flags = resolved(document, dict, "Flags")) {
    d.flags = static_cast<std::uint32_t>(std::clamp<std::int64_t>(flags->as_integer().value_or(0), 0, UINT32_MAX));
  }
  if (const auto angle = resolved(document, dict, "ItalicAngle")) {
    d.italic_angle = number(*angle).value_or(0);
  }
  if (const auto weight = resolved(document, dict, "FontWeight")) {
    d.weight = number(*weight).value_or(400);
  }
  if (const auto missing = resolved(document, dict, "MissingWidth")) {
    d.missing_width = number(*missing);
  }
  return d;
}

constexpr std::uint32_t kSymbolicFlag = 1U << 2U;

// A path moved and scaled: x' = (x·sx + dx)·s, y' = (y + dy)·s.
graphics::Path placed(const graphics::Path& path, double sx, double dx, double dy, double s) {
  graphics::Path out;
  const auto at = [&](const graphics::Point& p) { return graphics::Point{((p.x * sx) + dx) * s, (p.y + dy) * s}; };
  path.for_each([&](graphics::SegmentKind kind, const graphics::Point* p) {
    switch (kind) {
      case graphics::SegmentKind::kMove:
        out.move_to(at(p[0]));
        break;
      case graphics::SegmentKind::kLine:
        out.line_to(at(p[0]));
        break;
      case graphics::SegmentKind::kCubic:
        out.cubic_to(at(p[0]), at(p[1]), at(p[2]));
        break;
      case graphics::SegmentKind::kClose:
        out.close();
        break;
    }
  });
  return out;
}

}  // namespace

// ── FontContext ───────────────────────────────────────────────────────────────────────────────

FontContext::FontContext(const Resources* resources, FontLimits limits) : resources_(resources), limits_(limits) {}

Resources::Data FontContext::resource(const std::string& name) {
  Resources::Data data = resources_ != nullptr ? resources_->find(name) : nullptr;
  if (!data) {
    missing_.insert(name);
  }
  return data;
}

std::shared_ptr<const CMap> FontContext::predefined_cmap(std::string_view name, std::size_t depth) {
  if (name == "Identity-H" || name == "Identity-V") {
    return CMap::identity(name == "Identity-V");
  }
  if (const auto found = cmaps_.find(name); found != cmaps_.end()) {
    return found->second;
  }
  // A CMap name is a key of the pack, not a path.
  constexpr std::size_t kMaxName = 64;
  if (name.empty() || name.size() > kMaxName ||
      !std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; })) {
    return nullptr;
  }
  const std::string key(name);
  cmaps_[key] = nullptr;  // while it is parsed: a usecmap cycle finds nothing
  std::shared_ptr<const CMap> cmap;
  if (const Resources::Data data = resource("cmap/" + key)) {
    auto parsed = CMap::parse(text_of(*data), [this](std::string_view n, std::size_t d) { return predefined_cmap(n, d); }, limits_.cmap, depth);
    if (parsed) {
      cmap = std::make_shared<const CMap>(std::move(*parsed));
    }
  }
  cmaps_[key] = cmap;
  return cmap;
}

const FontProgram* FontContext::standard(StandardFont font) {
  if (const auto found = standard_.find(font); found != standard_.end()) {
    return found->second.get();
  }
  const std::span<const unsigned char> data = standard_font_data(font);
  auto bytes = std::make_shared<std::vector<std::byte>>(data.size());
  std::ranges::transform(data, bytes->begin(), [](unsigned char c) { return std::byte{c}; });
  auto program = FontProgram::open(std::move(bytes), limits_.program);
  return standard_.emplace(font, program ? std::move(*program) : nullptr).first->second.get();
}

const FontProgram* FontContext::pack_font(std::string_view file) {
  if (file.empty()) {
    return nullptr;
  }
  if (const auto found = pack_.find(file); found != pack_.end()) {
    return found->second.get();
  }
  std::unique_ptr<FontProgram> program;
  if (Resources::Data data = resource("font/" + std::string(file))) {
    if (auto opened = FontProgram::open(std::move(data), limits_.program)) {
      program = std::move(*opened);
    }
  }
  return pack_.emplace(std::string(file), std::move(program)).first->second.get();
}

const std::unordered_map<std::uint32_t, char32_t>* FontContext::cid_unicode(std::string_view ordering) {
  if (const auto found = cid_unicode_.find(ordering); found != cid_unicode_.end()) {
    return found->second.empty() ? nullptr : &found->second;
  }
  std::unordered_map<std::uint32_t, char32_t> map;
  const Collection* c = collection(ordering);
  if (const auto cmap = c != nullptr ? predefined_cmap(c->unicode_cmap) : nullptr) {
    constexpr std::size_t kMaxEntries = 1'000'000;
    cmap->for_each_cid([&](std::uint32_t code, std::uint8_t length, std::uint32_t cid) {
      char32_t unicode = 0;
      if (length == 2 && (code < 0xD800 || code > 0xDFFF)) {
        unicode = code;
      } else if (length == 4) {
        const std::uint32_t high = code >> 16U;
        const std::uint32_t low = code & 0xFFFFU;
        if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF) {
          unicode = 0x10000 + ((high - 0xD800) << 10U) + (low - 0xDC00);
        }
      }
      if (unicode != 0) {
        // Several characters may share a CID: the ideograph and its compatibility forms (the Kangxi
        // radical ⾺ U+2FBA and 馬 U+99AC; CJK compatibility ideographs). The first ordinary character is
        // kept, a compatibility one only when there is nothing else.
        const auto compatibility = [](char32_t u) { return (u >= 0x2E80 && u <= 0x2FDF) || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x2F800 && u <= 0x2FA1F); };
        const auto [it, inserted] = map.emplace(cid, unicode);
        if (!inserted && compatibility(it->second) && !compatibility(unicode)) {
          it->second = unicode;
        }
      }
      return map.size() < kMaxEntries;
    });
  }
  const auto& stored = cid_unicode_.emplace(std::string(ordering), std::move(map)).first->second;
  return stored.empty() ? nullptr : &stored;
}

// ── Font ──────────────────────────────────────────────────────────────────────────────────────

std::shared_ptr<Font> Font::load(Document& document, const Object& object, FontContext& context, std::vector<std::string>& warnings) {
  auto font = std::make_shared<Font>();
  font->context_ = &context;
  auto resolved_font = document.resolve(object);
  const model::Dict* dict = resolved_font ? resolved_font->as_dict() : nullptr;
  if (dict == nullptr) {
    warnings.emplace_back("a font is not a dictionary: a substitute is used and text advances are estimated");
    font->load_simple(document, model::Dict{}, context, warnings);
    return font;
  }
  const Object* subtype = dict->find("Subtype");
  if (subtype != nullptr && subtype->is_name("Type0")) {
    font->load_composite(document, *dict, context, warnings);
  } else if (subtype != nullptr && subtype->is_name("Type3")) {
    font->load_type3(document, *dict, warnings);
    font->load_to_unicode(document, *dict, context);
  } else {
    font->load_simple(document, *dict, context, warnings);
  }
  return font;
}

void Font::load_simple(Document& document, const model::Dict& dict, FontContext& context, std::vector<std::string>& warnings) {
  kind_ = Kind::kSimple;
  const std::string base_font = name_value(dict, "BaseFont");
  const std::string shown_name = base_font.empty() ? std::string("?") : base_font;
  if (const auto first = resolved(document, dict, "FirstChar")) {
    first_char_ = static_cast<std::uint32_t>(std::clamp<std::int64_t>(first->as_integer().value_or(0), 0, 255));
  }
  if (const auto widths = resolved(document, dict, "Widths"); widths && widths->as_array() != nullptr) {
    for (const Object& value : *widths->as_array()) {
      if (widths_.size() >= 256) {
        break;
      }
      auto width = document.resolve(value);
      widths_.push_back(width ? number(*width).value_or(0) : 0);
    }
    widths_known_ = true;
  }
  const Descriptor descriptor = read_descriptor(document, dict);
  if (descriptor.missing_width) {
    default_width_ = *descriptor.missing_width;
  }

  // The encoding (§9.6.5): a base encoding and /Differences over it.
  bool has_encoding = false;
  bool has_differences = false;
  std::optional<BaseEncoding> base;
  if (const auto encoding = resolved(document, dict, "Encoding")) {
    if (encoding->as_name() != nullptr) {
      has_encoding = true;
      base = base_encoding(*encoding);
      if (!base) {
        warnings.push_back("font /" + shown_name + ": encoding /" + encoding->as_name()->bytes + " not supported, the font's own used");
      }
    } else if (const model::Dict* e = encoding->as_dict()) {
      has_encoding = true;
      if (const Object* b = e->find("BaseEncoding")) {
        base = base_encoding(*b);
      }
      if (const auto differences = resolved(document, *e, "Differences"); differences && differences->as_array() != nullptr) {
        std::int64_t code = -1;
        for (const Object& item : *differences->as_array()) {
          if (const auto start = item.as_integer()) {
            code = *start;
          } else if (item.as_name() != nullptr && code >= 0 && code < 256) {
            names_.at(static_cast<std::size_t>(code++)) = item.as_name()->bytes;
            has_differences = true;
          }
        }
      }
    }
  }
  if (base) {
    const auto table = encoding_table(*base);
    for (std::size_t code = 0; code < names_.size(); ++code) {
      if (names_.at(code).empty()) {
        names_.at(code) = table[code];
      }
    }
  }

  // The program: the embedded one, else a substitute.
  const std::optional<StandardFont> standard = standard_font_named(base_font);
  embedded_ = open_embedded(document, descriptor.dict ? descriptor.dict->as_dict() : nullptr, shown_name, context.limits(), warnings);
  if (embedded_) {
    primary_ = embedded_.get();
  } else {
    substituted_ = true;
    substitute_ = standard.value_or(substitute_font(base_font, descriptor.flags, descriptor.italic_angle, descriptor.weight));
    primary_ = context.standard(substitute_);
    if (!standard && !base_font.empty()) {
      warnings.push_back("font /" + shown_name + " is not embedded: a substitute is used");
    }
  }
  load_to_unicode(document, dict, context);
  set_extent(document, descriptor.dict ? descriptor.dict->as_dict() : nullptr);
  // Symbolic (§9.8.2): the codes select glyphs directly rather than through Latin names. Without a
  // descriptor, the standard Symbol and Dingbats fonts are.
  symbolic_ = descriptor.dict ? (descriptor.flags & kSymbolicFlag) != 0 : standard && is_symbolic(*standard);
  has_encoding_ = has_encoding;
  has_differences_ = has_differences;

  if (!widths_known_ && primary_ != nullptr) {
    // No /Widths (the standard 14 may omit them, §9.6.2.2): the program's advances — exact for the
    // standard fonts, whose substitutes have their metrics; an estimate for the others.
    first_char_ = 0;
    widths_.assign(256, default_width_);
    for (std::uint32_t code = 0; code < 256; ++code) {
      if (const std::uint32_t glyph = simple_glyph(*primary_, code); glyph != 0) {
        widths_[code] = primary_->advance(glyph).value_or(default_width_);
      }
    }
    widths_known_ = embedded_ != nullptr || standard.has_value();
    if (!widths_known_) {
      warnings.push_back("font /" + shown_name + " has no /Widths: text advances estimated");
    }
  }

  // Latin text for previews: a standard encoding without differences, or a standard Latin font's own.
  if (has_encoding) {
    latin_codes_ = base.has_value() && !has_differences;
  } else {
    latin_codes_ = !embedded_ && !(standard && is_symbolic(*standard)) && !symbolic_;
  }
}

void Font::load_composite(Document& document, const model::Dict& dict, FontContext& context, std::vector<std::string>& warnings) {
  kind_ = Kind::kComposite;
  default_width_ = 1000;
  const std::string base_font = name_value(dict, "BaseFont");
  const std::string shown_name = base_font.empty() ? std::string("?") : base_font;
  const auto resolver = [&context](std::string_view name, std::size_t depth) { return context.predefined_cmap(name, depth); };

  // The CMap (§9.7.5): predefined by name, or embedded.
  if (const auto encoding = resolved(document, dict, "Encoding"); encoding && encoding->as_name() != nullptr) {
    const std::string_view name = encoding->as_name()->bytes;
    if (name.starts_with("Uni")) {
      constexpr std::array<std::pair<std::string_view, UnicodeCodes>, 4> kForms = {
          {{"-UCS2-", UnicodeCodes::kUcs2}, {"-UTF16-", UnicodeCodes::kUtf16}, {"-UTF32-", UnicodeCodes::kUtf32}, {"-UTF8-", UnicodeCodes::kUtf8}}};
      for (const auto& [form, codes] : kForms) {
        if (name.contains(form)) {
          unicode_codes_ = codes;
          break;
        }
      }
    }
    cmap_ = context.predefined_cmap(encoding->as_name()->bytes);
    if (!cmap_) {
      warnings.push_back("font /" + shown_name + ": CMap /" + encoding->as_name()->bytes + " not available, its glyphs are not drawn");
    }
  } else if (encoding && encoding->as_stream() != nullptr) {
    const model::Stream& stream = *encoding->as_stream();
    auto data = document.decode_stream(stream);
    auto parsed = data ? CMap::parse(text_of(*data), resolver, context.limits().cmap) : Result<CMap>(std::unexpected(data.error()));
    if (parsed) {
      if (const auto use = resolved(document, stream.dict, "UseCMap"); use && use->as_name() != nullptr) {
        parsed->set_parent(context.predefined_cmap(use->as_name()->bytes, 1));
      } else if (use && use->as_stream() != nullptr) {
        auto used = document.decode_stream(*use->as_stream());
        if (auto parent = used ? CMap::parse(text_of(*used), resolver, context.limits().cmap, 1) : Result<CMap>(std::unexpected(used.error()))) {
          parsed->set_parent(std::make_shared<const CMap>(std::move(*parent)));
        }
      }
      if (const auto wmode = resolved(document, stream.dict, "WMode"); wmode && wmode->as_integer()) {
        parsed->set_vertical(*wmode->as_integer() == 1);
      }
      cmap_ = std::make_shared<const CMap>(std::move(*parsed));
    } else {
      warnings.push_back("font /" + shown_name + ": embedded CMap unreadable, its glyphs are not drawn");
    }
  }
  if (!cmap_) {
    cmap_missing_ = true;
    cmap_ = CMap::identity(false);  // most CMaps are two-byte: the positions stay close
  }
  vertical_ = cmap_->vertical();

  load_to_unicode(document, dict, context);

  // The CIDFont (§9.7.4).
  const auto descendants = resolved(document, dict, "DescendantFonts");
  const model::Array* list = descendants ? descendants->as_array() : nullptr;
  auto cid_font = list != nullptr && !list->empty() ? document.resolve(list->front()) : Result<Object>(Object());
  const model::Dict* cid = cid_font ? cid_font->as_dict() : nullptr;
  if (cid == nullptr) {
    warnings.push_back("font /" + shown_name + " has no CIDFont: its glyphs are not drawn");
    cmap_missing_ = true;
    return;
  }
  const Object* cid_subtype = cid->find("Subtype");
  cid_truetype_ = cid_subtype != nullptr && cid_subtype->is_name("CIDFontType2");
  if (const auto info = resolved(document, *cid, "CIDSystemInfo"); info && info->as_dict() != nullptr) {
    if (const auto ordering = resolved(document, *info->as_dict(), "Ordering"); ordering && ordering->as_string() != nullptr) {
      ordering_ = ordering->as_string()->bytes;
    }
  }
  if (const auto dw = resolved(document, *cid, "DW")) {
    default_width_ = number(*dw).value_or(1000);
  }
  const std::size_t max_entries = context.limits().max_width_entries;
  if (const auto w = resolved(document, *cid, "W"); w && w->as_array() != nullptr) {
    // Two forms (§9.7.4.3): c [w1 w2 ...]  and  c_first c_last w.
    const model::Array& a = *w->as_array();
    for (std::size_t i = 0; i < a.size() && cid_widths_.size() < max_entries;) {
      const auto first = a[i].as_integer();
      if (!first || *first < 0 || i + 1 >= a.size()) {
        break;
      }
      auto next = document.resolve(a[i + 1]);
      if (next && next->as_array() != nullptr) {
        auto code = static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX));
        for (const Object& value : *next->as_array()) {
          if (const auto width = number(value); width && cid_widths_.size() < max_entries) {
            cid_widths_.push_back({code, code, *width});
          }
          ++code;
        }
        i += 2;
        continue;
      }
      const auto last = a[i + 1].as_integer();
      const auto width = i + 2 < a.size() ? number(a[i + 2]) : std::nullopt;
      if (!last || !width || *last < *first) {
        break;
      }
      cid_widths_.push_back({static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX)),
                             static_cast<std::uint32_t>(std::min<std::int64_t>(*last, UINT32_MAX)), *width});
      i += 3;
    }
    std::ranges::stable_sort(cid_widths_, {}, &Range::first);
  }
  if (const auto dw2 = resolved(document, *cid, "DW2"); dw2 && dw2->as_array() != nullptr && dw2->as_array()->size() == 2) {
    default_vertical_.vy = number((*dw2->as_array())[0]).value_or(880);
    default_vertical_.advance = number((*dw2->as_array())[1]).value_or(-1000);
  }
  if (const auto w2 = resolved(document, *cid, "W2"); w2 && w2->as_array() != nullptr) {
    // c [w1y vx vy ...]  and  c_first c_last w1y vx vy.
    const model::Array& a = *w2->as_array();
    for (std::size_t i = 0; i < a.size() && cid_vertical_.size() < max_entries;) {
      const auto first = a[i].as_integer();
      if (!first || *first < 0 || i + 1 >= a.size()) {
        break;
      }
      auto next = document.resolve(a[i + 1]);
      if (next && next->as_array() != nullptr) {
        const model::Array& values = *next->as_array();
        auto code = static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX));
        for (std::size_t j = 0; j + 2 < values.size() && cid_vertical_.size() < max_entries; j += 3, ++code) {
          cid_vertical_.push_back(
              {.first = code,
               .last = code,
               .metrics = {.advance = number(values[j]).value_or(-1000), .vx = number(values[j + 1]).value_or(0), .vy = number(values[j + 2]).value_or(880)}});
        }
        i += 2;
        continue;
      }
      const auto last = a[i + 1].as_integer();
      if (!last || *last < *first || i + 4 >= a.size()) {
        break;
      }
      cid_vertical_.push_back(
          {.first = static_cast<std::uint32_t>(std::min<std::int64_t>(*first, UINT32_MAX)),
           .last = static_cast<std::uint32_t>(std::min<std::int64_t>(*last, UINT32_MAX)),
           .metrics = {.advance = number(a[i + 2]).value_or(-1000), .vx = number(a[i + 3]).value_or(0), .vy = number(a[i + 4]).value_or(880)}});
      i += 5;
    }
    std::ranges::stable_sort(cid_vertical_, {}, &VerticalRange::first);
  }
  if (const auto map = resolved(document, *cid, "CIDToGIDMap"); map && map->as_stream() != nullptr) {
    if (auto data = document.decode_stream(*map->as_stream())) {
      constexpr std::size_t kMaxCids = std::size_t{1} << 20U;
      const std::size_t count = std::min(data->size() / 2, kMaxCids);
      cid_to_gid_.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        cid_to_gid_[i] = static_cast<std::uint16_t>((std::to_integer<unsigned>((*data)[2 * i]) << 8U) | std::to_integer<unsigned>((*data)[(2 * i) + 1]));
      }
    }
  }
  widths_known_ = !cmap_missing_;

  const Descriptor descriptor = read_descriptor(document, *cid);
  const std::string cid_name = name_value(*cid, "BaseFont");
  embedded_ = open_embedded(document, descriptor.dict ? descriptor.dict->as_dict() : nullptr, shown_name, context.limits(), warnings);
  set_extent(document, descriptor.dict ? descriptor.dict->as_dict() : nullptr);
  if (embedded_) {
    primary_ = embedded_.get();
    return;
  }
  substituted_ = true;
  if (const Collection* c = collection(ordering_)) {
    primary_ = context.pack_font(c->font);
    if (primary_ == nullptr) {
      warnings.push_back("font /" + shown_name + " (" + ordering_ + ") is not embedded and no CJK font is available: its glyphs are not drawn");
    }
    return;
  }
  substitute_ = substitute_font(cid_name.empty() ? base_font : cid_name, descriptor.flags, descriptor.italic_angle, descriptor.weight);
  primary_ = context.standard(substitute_);
  warnings.push_back("font /" + shown_name + " is not embedded: a substitute is used" + (to_unicode_ ? "" : ", without /ToUnicode its glyphs are unknown"));
}

void Font::load_type3(Document& document, const model::Dict& dict, std::vector<std::string>& warnings) {
  kind_ = Kind::kType3;
  if (const auto matrix = resolved(document, dict, "FontMatrix"); matrix && matrix->as_array() != nullptr && matrix->as_array()->size() == 6) {
    std::array<double, 6> m{};
    for (std::size_t i = 0; i < m.size(); ++i) {
      m.at(i) = number((*matrix->as_array())[i]).value_or(0);
    }
    font_matrix_ = {m[0], m[1], m[2], m[3], m[4], m[5]};
  }
  scale_ = font_matrix_.a;
  if (const auto first = resolved(document, dict, "FirstChar")) {
    first_char_ = static_cast<std::uint32_t>(std::clamp<std::int64_t>(first->as_integer().value_or(0), 0, 255));
  }
  if (const auto widths = resolved(document, dict, "Widths"); widths && widths->as_array() != nullptr) {
    for (const Object& value : *widths->as_array()) {
      if (widths_.size() >= 256) {
        break;
      }
      auto width = document.resolve(value);
      widths_.push_back(width ? number(*width).value_or(0) : 0);
    }
    widths_known_ = true;
  } else {
    warnings.emplace_back("Type 3 font without /Widths: text advances estimated");
  }
  default_width_ = 0;
  if (const auto encoding = resolved(document, dict, "Encoding"); encoding && encoding->as_dict() != nullptr) {
    if (const auto differences = resolved(document, *encoding->as_dict(), "Differences"); differences && differences->as_array() != nullptr) {
      std::int64_t code = -1;
      for (const Object& item : *differences->as_array()) {
        if (const auto start = item.as_integer()) {
          code = *start;
        } else if (item.as_name() != nullptr && code >= 0 && code < 256) {
          names_.at(static_cast<std::size_t>(code++)) = item.as_name()->bytes;
        }
      }
    }
  }
  if (auto procs = resolved(document, dict, "CharProcs"); procs && procs->as_dict() != nullptr) {
    char_procs_ = std::move(*procs);
  } else {
    warnings.emplace_back("Type 3 font without /CharProcs: its glyphs are not drawn");
  }
  if (auto resources = resolved(document, dict, "Resources"); resources && resources->as_dict() != nullptr) {
    type3_resources_ = std::move(*resources);
  }
  // The glyphs' extent: /FontBBox through the font matrix.
  if (const auto box = resolved(document, dict, "FontBBox"); box && box->as_array() != nullptr && box->as_array()->size() == 4) {
    const double y0 = number((*box->as_array())[1]).value_or(0) * font_matrix_.d;
    const double y1 = number((*box->as_array())[3]).value_or(0) * font_matrix_.d;
    if (y1 > y0 && std::isfinite(y1 - y0)) {
      ascent_ = std::clamp(std::max(y0, y1), 0.1, 1.5);
      descent_ = std::clamp(std::min(y0, y1), -1.0, 0.0);
    }
  }
}

void Font::load_to_unicode(Document& document, const model::Dict& dict, FontContext& context) {
  const auto to_unicode = resolved(document, dict, "ToUnicode");
  if (!to_unicode || to_unicode->as_stream() == nullptr) {
    return;
  }
  const auto resolver = [&context](std::string_view name, std::size_t depth) { return context.predefined_cmap(name, depth); };
  if (auto data = document.decode_stream(*to_unicode->as_stream())) {
    if (auto parsed = CMap::parse(text_of(*data), resolver, context.limits().cmap)) {
      to_unicode_ = std::make_shared<const CMap>(std::move(*parsed));
    }
  }
}

void Font::set_extent(Document& document, const model::Dict* descriptor) {
  // /Ascent and /Descent of the descriptor, else its /FontBBox, else the program's; within sane bounds
  // (writers put anything there).
  double ascent = 0;
  double descent = 0;
  if (descriptor != nullptr) {
    if (const auto a = resolved(document, *descriptor, "Ascent")) {
      ascent = number(*a).value_or(0);
    }
    if (const auto d = resolved(document, *descriptor, "Descent")) {
      descent = number(*d).value_or(0);
    }
    if (ascent <= 0) {
      if (const auto box = resolved(document, *descriptor, "FontBBox"); box && box->as_array() != nullptr && box->as_array()->size() == 4) {
        ascent = number((*box->as_array())[3]).value_or(0);
        descent = std::min(descent, number((*box->as_array())[1]).value_or(0));
      }
    }
  }
  if (ascent <= 0 && primary_ != nullptr) {
    std::tie(ascent, descent) = primary_->ascent_descent();
  }
  if (ascent > 0) {
    ascent_ = std::clamp(ascent / 1000, 0.1, 1.5);
    descent_ = std::clamp(descent / 1000, -1.0, 0.0);
  }
}

std::u32string Font::text(const Code& code) {
  const std::uint64_t key = (std::uint64_t{code.code} << 8U) | code.length;
  if (const auto found = texts_.find(key); found != texts_.end()) {
    return found->second;
  }
  std::u32string text = lookup_text(code);
  constexpr std::size_t kMaxCached = 1U << 16U;
  if (texts_.size() < kMaxCached) {
    texts_.emplace(key, text);
  }
  return text;
}

char32_t Font::unicode_of_code(const Code& code) const {
  const std::uint32_t v = code.code;
  char32_t c = 0;
  switch (unicode_codes_) {
    case UnicodeCodes::kUcs2:
      c = code.length == 2 ? v : 0;
      break;
    case UnicodeCodes::kUtf16:
      if (code.length == 2) {
        c = v;
      } else if (code.length == 4 && (v >> 16U) >= 0xD800 && (v >> 16U) <= 0xDBFF && (v & 0xFFFFU) >= 0xDC00 && (v & 0xFFFFU) <= 0xDFFF) {
        c = 0x10000 + (((v >> 16U) - 0xD800) << 10U) + ((v & 0xFFFFU) - 0xDC00);
      }
      break;
    case UnicodeCodes::kUtf32:
      c = v;
      break;
    case UnicodeCodes::kUtf8: {
      // The code's bytes, big-endian: one to four bytes of UTF-8.
      const auto length = static_cast<std::uint32_t>(code.length);
      const std::uint32_t lead = v >> (8 * (length - 1));
      if (length == 1) {
        c = lead < 0x80 ? v : 0;
      } else {
        c = lead & (0x7FU >> length);
        for (std::uint32_t k = length - 1; k-- > 0;) {
          c = (c << 6U) | ((v >> (8 * k)) & 0x3FU);
        }
      }
      break;
    }
    case UnicodeCodes::kNone:
      break;
  }
  return (c >= 0x20 && c <= 0x10FFFF && (c < 0xD800 || c > 0xDFFF)) ? c : 0;
}

std::u32string Font::to_unicode_text(const Code& code) const { return to_unicode_ ? to_unicode_->text(code.code, code.length) : std::u32string(); }

std::u32string Font::lookup_text(const Code& code) {
  if (to_unicode_) {
    if (std::u32string t = to_unicode_->text(code.code, code.length); !t.empty()) {
      return t;
    }
  }
  const auto from_name = [](std::string_view name) -> std::u32string {
    const auto unicode = name.empty() ? std::nullopt : glyph_unicode(name);
    return unicode ? std::u32string(1, *unicode) : std::u32string();
  };
  if (kind_ != Kind::kComposite) {
    const std::size_t c = code.code & 0xFFU;
    if (std::u32string t = from_name(names_.at(c)); !t.empty()) {
      return t;
    }
    // No name in the PDF: the glyph's own, when the program has names; the standard encoding's for a
    // Latin font without an encoding.
    if (kind_ == Kind::kSimple && context_ != nullptr) {
      const Slot s = slot(code, *context_);
      if (s.program != nullptr && s.glyph != 0) {
        if (const auto name = s.program->glyph_name(s.glyph)) {
          if (std::u32string t = from_name(*name); !t.empty()) {
            return t;
          }
        }
      }
      if (!symbolic_ && !has_encoding_) {
        if (std::u32string t = from_name(encoding_table(BaseEncoding::kStandard)[c]); !t.empty()) {
          return t;
        }
      }
    }
    // The code itself, when it is a printable character (as readers fall back).
    return c >= 0x20 && c != 0x7F ? std::u32string(1, static_cast<char32_t>(c)) : std::u32string();
  }
  // Composite fonts with a Unicode CMap: the code is the character (as the CMap's name says).
  if (const char32_t c = unicode_of_code(code); c != 0) {
    return {c};
  }
  // Else the collection's characters (Adobe-Japan1, ...), else the embedded program's cmap.
  if (collection(ordering_) != nullptr && context_ != nullptr) {
    if (const auto* map = context_->cid_unicode(ordering_)) {
      if (const auto found = map->find(code.cid); found != map->end()) {
        return {found->second};
      }
    }
  }
  if (embedded_) {
    if (!glyph_unicode_) {
      glyph_unicode_ = embedded_->unicode_of_glyphs();
    }
    if (const auto found = glyph_unicode_->find(cid_glyph(code.cid)); found != glyph_unicode_->end()) {
      return {found->second};
    }
  }
  return {};
}

void Font::codes(std::string_view bytes, std::vector<Code>& out) const {
  if (kind_ != Kind::kComposite) {
    out.reserve(out.size() + bytes.size());
    for (const char c : bytes) {
      const auto code = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
      out.push_back({.code = code, .length = 1, .cid = code});
    }
    return;
  }
  for (std::size_t at = 0; at < bytes.size();) {
    const std::size_t before = at;
    const CMap::Code c = cmap_->next(bytes, at);
    if (at == before) {
      break;
    }
    out.push_back({.code = c.code, .length = c.length, .cid = c.cid});
  }
}

double Font::width(const Code& code) const {
  if (kind_ == Kind::kComposite) {
    const auto after = std::ranges::upper_bound(cid_widths_, code.cid, {}, &Range::first);
    if (after != cid_widths_.begin() && code.cid <= std::prev(after)->last) {
      return std::prev(after)->width;
    }
    return default_width_;
  }
  if (code.code >= first_char_ && code.code - first_char_ < widths_.size()) {
    return widths_[code.code - first_char_];
  }
  return default_width_;
}

Font::VerticalMetrics Font::vertical_metrics(const Code& code) const {
  const auto after = std::ranges::upper_bound(cid_vertical_, code.cid, {}, &VerticalRange::first);
  if (after != cid_vertical_.begin() && code.cid <= std::prev(after)->last) {
    return std::prev(after)->metrics;
  }
  return {.advance = default_vertical_.advance, .vx = width(code) / 2, .vy = default_vertical_.vy};
}

const model::Object* Font::char_proc(std::uint32_t code) const {
  if (kind_ != Kind::kType3 || !char_procs_ || code >= names_.size() || names_.at(code).empty()) {
    return nullptr;
  }
  return char_procs_->as_dict()->find(names_.at(code));
}

std::uint32_t Font::simple_glyph(const FontProgram& program, std::uint32_t code) const {
  const std::string& name = names_.at(code);
  const auto named = [&](std::string_view n) -> std::uint32_t {
    if (n.empty()) {
      return 0;
    }
    if (const std::uint32_t glyph = program.glyph_named(n); glyph != 0) {
      return glyph;
    }
    const auto unicode = glyph_unicode(n);
    return unicode ? program.glyph_for(Charmap::kUnicode, *unicode) : 0;
  };
  if (program.is_truetype()) {
    // §9.6.5.4. A non-symbolic font: by glyph name, through the Unicode cmap (3,1), else the Macintosh
    // one (1,0), else the post table. A symbolic one: by the code itself, in the (3,0) cmap (also at
    // 0xF000 and above, where symbol fonts put their glyphs) or the (1,0) one — its /Differences then
    // only name what the codes are, as readers do. Each way falls back on the other.
    std::string_view n = name;
    if (n.empty() && !has_encoding_ && !symbolic_) {
      n = encoding_table(BaseEncoding::kStandard)[code];
    }
    const auto by_name = [&]() -> std::uint32_t {
      if (n.empty()) {
        return 0;
      }
      if (const auto unicode = glyph_unicode(n)) {
        if (const std::uint32_t glyph = program.glyph_for(Charmap::kUnicode, *unicode); glyph != 0) {
          return glyph;
        }
      }
      if (const auto mac = encoding_code(BaseEncoding::kMacRoman, n)) {
        if (const std::uint32_t glyph = program.glyph_for(Charmap::kMacRoman, *mac); glyph != 0) {
          return glyph;
        }
      }
      return program.glyph_named(n);
    };
    const auto by_code = [&]() -> std::uint32_t {
      for (const std::uint32_t base : {0U, 0xF000U, 0xF100U, 0xF200U}) {
        if (const std::uint32_t glyph = program.glyph_for(Charmap::kMicrosoftSymbol, base + code); glyph != 0) {
          return glyph;
        }
      }
      if (const std::uint32_t glyph = program.glyph_for(Charmap::kMacRoman, code); glyph != 0) {
        return glyph;
      }
      return program.glyph_for(Charmap::kUnicode, code);
    };
    const bool symbolic_cmap = program.has_charmap(Charmap::kMicrosoftSymbol) || program.has_charmap(Charmap::kMacRoman);
    const bool names_first = !symbolic_ || !symbolic_cmap;
    if (const std::uint32_t glyph = names_first ? by_name() : by_code(); glyph != 0) {
      return glyph;
    }
    if (const std::uint32_t glyph = names_first ? by_code() : by_name(); glyph != 0) {
      return glyph;
    }
    const bool no_cmap = !symbolic_cmap && !program.has_charmap(Charmap::kUnicode);
    return no_cmap && code < program.glyph_count() ? code : 0;  // a font without cmap: codes are glyph indices
  }
  // Type 1 and CFF (§9.6.5.2): by glyph name; without an encoding in the PDF, the font's own.
  if (const std::uint32_t glyph = named(name); glyph != 0) {
    return glyph;
  }
  if (!has_encoding_ || (substituted_ && is_symbolic(substitute_))) {
    if (const std::uint32_t glyph = program.glyph_for(Charmap::kBuiltin, code); glyph != 0) {
      return glyph;
    }
  }
  if (name.empty()) {
    return named(encoding_table(BaseEncoding::kStandard)[code]);
  }
  return 0;
}

std::uint32_t Font::cid_glyph(std::uint32_t cid) const {
  if (cid_truetype_ || embedded_->is_truetype()) {
    if (cid_to_gid_.empty()) {
      return cid;
    }
    return cid < cid_to_gid_.size() ? cid_to_gid_[cid] : 0;
  }
  return cid;  // CFF: the CID for a CID-keyed font, else the glyph index (§9.7.4.2)
}

Font::GlyphSource Font::glyph_source(const Code& code, FontContext& context) {
  const Slot s = slot(code, context);
  return {.program = s.program, .glyph = s.glyph, .embedded = s.program != nullptr && s.program == embedded_.get()};
}

std::string Font::glyph_name(std::uint32_t code) const {
  const std::string& name = names_.at(code & 0xFFU);
  if (!name.empty() || has_encoding_ || symbolic_) {
    return name;
  }
  return std::string(encoding_table(BaseEncoding::kStandard)[code & 0xFFU]);
}

Font::Slot Font::slot(const Code& code, FontContext& context) {
  Slot s;
  if (kind_ == Kind::kSimple) {
    const std::size_t c = code.code & 0xFFU;
    if (!slot_known_.at(c)) {
      Slot found;
      if (primary_ != nullptr) {
        found = {.program = primary_, .glyph = simple_glyph(*primary_, code.code), .fit = 1};
      }
      if (found.glyph == 0 && substituted_ && !is_symbolic(substitute_)) {
        // A glyph the built-in substitute lacks (Greek, Cyrillic, ...): Liberation, when the host has it.
        if (secondary_ == nullptr) {
          secondary_ = context.pack_font(liberation_file(substitute_));
        }
        if (secondary_ != nullptr) {
          found = {.program = secondary_, .glyph = simple_glyph(*secondary_, code.code), .fit = 1};
        }
      }
      slots_.at(c) = found;
      slot_known_.at(c) = true;
    }
    s = slots_.at(c);
  } else if (kind_ == Kind::kComposite && !cmap_missing_) {
    if (embedded_) {
      s = {.program = embedded_.get(), .glyph = cid_glyph(code.cid), .fit = 1};
    } else if (primary_ != nullptr) {
      char32_t unicode = 0;
      if (collection(ordering_) != nullptr) {
        if (const auto* map = context.cid_unicode(ordering_)) {
          if (const auto found = map->find(code.cid); found != map->end()) {
            unicode = found->second;
          }
        }
      } else if (to_unicode_) {
        const std::u32string text = to_unicode_->text(code.code, code.length);
        unicode = text.empty() ? 0 : text.front();
      }
      s = {.program = primary_, .glyph = unicode != 0 ? primary_->glyph_for(Charmap::kUnicode, unicode) : 0, .fit = 1};
    }
  }
  // A substitute's glyph is fitted to the width the PDF gives, as readers do: the text keeps its layout.
  if (substituted_ && s.program != nullptr && s.glyph != 0 && (widths_known_ || kind_ == Kind::kComposite)) {
    const double wanted = width(code);
    const auto advance = s.program->advance(s.glyph);
    if (wanted > 0 && advance && *advance > 0) {
      constexpr double kMinFit = 0.25;
      constexpr double kMaxFit = 4;
      s.fit = std::clamp(wanted / *advance, kMinFit, kMaxFit);
    }
  }
  return s;
}

std::shared_ptr<const graphics::Path> Font::outline(const Code& code) {
  if (kind_ == Kind::kType3 || context_ == nullptr) {
    return nullptr;
  }
  const std::uint32_t key = kind_ == Kind::kComposite ? code.cid : code.code;
  if (const auto found = outlines_.find(key); found != outlines_.end()) {
    return found->second;
  }
  const Slot s = slot(code, *context_);
  std::shared_ptr<const graphics::Path> result;
  if (s.program != nullptr && s.glyph != 0) {
    if (const auto path = s.program->outline(s.glyph); path && !path->empty()) {
      // Vertical writing: the glyph's vertical origin goes to the current point (§9.7.4.3).
      const VerticalMetrics v = vertical_ ? vertical_metrics(code) : VerticalMetrics{.advance = 0, .vx = 0, .vy = 0};
      result = std::make_shared<const graphics::Path>(placed(*path, s.fit, -v.vx, -v.vy, 0.001));
    }
  }
  if (outlines_.size() < context_->limits().max_cached_outlines) {
    outlines_.emplace(key, result);
  }
  return result;
}

}  // namespace capriole::font::detail
