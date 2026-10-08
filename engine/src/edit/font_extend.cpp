#include "font_extend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/crypto/der.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/model/text_string.hpp"
#include "common.hpp"
#include "font/font.hpp"
#include "font/sfnt.hpp"

namespace capriole::edit {

namespace {

using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// How many glyphs of a subset are compared with the face (all of them must be among its outlines).
constexpr std::size_t kCompared = 48;
// The glyph ranges the CIDs and glyph indices of a font can take.
constexpr std::uint32_t kMaxGlyph = 0xFFFF;

std::string name_of(const Object* value) { return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string(); }

// A PostScript name as compared: letters and digits only, lowercase ("DejaVuSans,Bold" is DejaVuSans-Bold).
std::string key_of(std::string_view name) {
  std::string out;
  for (const char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

Object stream_of(Dict dict, std::span<const std::byte> data) {
  model::Stream stream;
  stream.dict = std::move(dict);
  auto compressed = filters::flate_encode(data);
  if (compressed) {
    stream.dict.set("Filter", detail::name("FlateDecode"));
    stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  } else {
    stream.dict.erase("Filter");
    stream.memory_data = std::make_shared<const std::vector<std::byte>>(data.begin(), data.end());
  }
  stream.dict.erase("DecodeParms");
  return {std::move(stream)};
}

// An entry resolved, and the reference it came through (none: it is direct).
struct Entry {
  Object value;
  std::optional<ObjectRef> ref;
};

std::optional<Entry> entry(Document& document, const Dict& dict, std::string_view key) {
  const Object* e = dict.find(key);
  if (e == nullptr) {
    return std::nullopt;
  }
  auto resolved = document.resolve(*e);
  if (!resolved) {
    return std::nullopt;
  }
  return {{.value = std::move(*resolved), .ref = e->as_ref()}};
}

std::optional<std::vector<std::byte>> decoded(Document& document, const Object& value) {
  if (value.as_stream() == nullptr) {
    return std::nullopt;
  }
  auto data = document.decode_stream(*value.as_stream());
  return data ? std::optional(std::move(*data)) : std::nullopt;
}

// UTF-16BE hex of a character (a /ToUnicode destination).
std::string utf16_hex(char32_t c) {
  if (c < 0x10000) {
    return std::format("{:04X}", static_cast<std::uint32_t>(c));
  }
  const std::uint32_t v = static_cast<std::uint32_t>(c) - 0x10000;
  return std::format("{:04X}{:04X}", 0xD800 + (v >> 10U), 0xDC00 + (v & 0x3FFU));
}

}  // namespace

FontLibrary::FontLibrary() = default;

Result<std::vector<FontFace>> FontLibrary::add(std::vector<std::byte> data) {
  const std::uint32_t count = font::detail::face_count(data);
  if (count == 0) {
    auto refused = font::detail::read_sfnt(data);
    return std::unexpected(refused ? Error{.code = ErrorCode::kUnsupported, .message = "not a TrueType font"} : std::move(refused.error()));
  }
  std::vector<FontFace> out;
  for (std::uint32_t f = 0; f < count; ++f) {
    auto sfnt = font::detail::read_sfnt(data, f);
    if (!sfnt) {
      return std::unexpected(std::move(sfnt.error()));
    }
    auto face = std::make_shared<Face>();
    face->sfnt = std::move(*sfnt);
    face->info.postscript_name = font::detail::postscript_name(face->sfnt).value_or("");
    if (face->info.postscript_name.empty()) {
      continue;
    }
    face->info.family = font::detail::name_string(face->sfnt, 16).value_or(font::detail::name_string(face->sfnt, 1).value_or(""));
    face->info.style = font::detail::name_string(face->sfnt, 17).value_or(font::detail::name_string(face->sfnt, 2).value_or(""));
    // Installable (0) or editable (8): documents with it may be edited; not "no subsetting", not bitmaps only.
    const std::uint16_t fs = font::detail::fs_type(face->sfnt);
    const unsigned usage = fs & 0xFU;
    face->info.embeddable = (usage == 0 || (usage & 0x8U) != 0) && (fs & 0x300U) == 0;
    face->units = font::detail::units_per_em(face->sfnt).value_or(0);
    face->cmap = font::detail::unicode_cmap(face->sfnt);
    const std::uint16_t glyphs = font::detail::glyph_count(face->sfnt).value_or(0);
    for (std::uint32_t g = 0; g < glyphs; ++g) {
      if (const auto data_of = font::detail::glyph_data(face->sfnt, static_cast<std::uint16_t>(g))) {
        if (auto key = font::detail::outline_key(*data_of)) {
          face->outlines.insert(std::move(*key));
        }
      }
    }
    std::erase_if(faces_, [&](const auto& other) { return other->info.postscript_name == face->info.postscript_name; });
    out.push_back(face->info);
    faces_.push_back(std::move(face));
  }
  if (out.empty()) {
    return make_error(ErrorCode::kUnsupported, "a font without a PostScript name");
  }
  return out;
}

std::vector<FontFace> FontLibrary::faces() const {
  std::vector<FontFace> out;
  out.reserve(faces_.size());
  for (const auto& face : faces_) {
    out.push_back(face->info);
  }
  return out;
}

void FontLibrary::clear() { faces_.clear(); }

const FontLibrary::Face* FontLibrary::find(std::string_view postscript_name) const {
  const std::string key = key_of(postscript_name);
  for (const auto& face : faces_) {
    if (key_of(face->info.postscript_name) == key) {
      return face.get();
    }
  }
  return nullptr;
}

namespace detail {

std::string font_name(Document& document, ObjectRef font) {
  auto loaded = document.get(font);
  const Dict* dict = loaded && (*loaded)->as_dict() != nullptr ? (*loaded)->as_dict() : nullptr;
  return dict != nullptr ? std::string(font::detail::without_subset_tag(name_of(dict->find("BaseFont")))) : std::string();
}

Result<std::optional<FontExtension>> extend_font(Document& document, ObjectRef font_ref, std::u32string_view letters, const FontLibrary& library) {
  const auto none = [] { return Result<std::optional<FontExtension>>(std::nullopt); };
  auto loaded = document.get(font_ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return none();
  }
  Dict top = *(*loaded)->as_dict();
  const std::string base(font::detail::without_subset_tag(name_of(top.find("BaseFont"))));
  const FontLibrary::Face* face = library.find(base);
  if (face == nullptr || !face->info.embeddable) {
    return none();
  }
  const bool composite = name_of(top.find("Subtype")) == "Type0";
  // The dictionary that holds the descriptor: the descendant (composite), or the font.
  Dict holder = top;
  std::optional<ObjectRef> holder_ref = font_ref;
  if (composite) {
    if (name_of(top.find("Encoding")) != "Identity-H" || top.find("ToUnicode") == nullptr) {
      return none();
    }
    auto list = entry(document, top, "DescendantFonts");
    if (!list || list->value.as_array() == nullptr || list->value.as_array()->size() != 1 || !list->value.as_array()->front().as_ref()) {
      return none();
    }
    holder_ref = *list->value.as_array()->front().as_ref();
    auto descendant = document.get(*holder_ref);
    if (!descendant || (*descendant)->as_dict() == nullptr || name_of((*descendant)->as_dict()->find("Subtype")) != "CIDFontType2") {
      return none();
    }
    holder = *(*descendant)->as_dict();
  } else if (name_of(top.find("Subtype")) != "TrueType") {
    return none();
  }
  // A simple font: WinAnsiEncoding (its codes are WinAnsi's characters), or no /Encoding and symbolic (its
  // codes the program's own, a /ToUnicode saying what they are: as LibreOffice and Cairo embed subsets).
  const bool symbolic = !composite && top.find("Encoding") == nullptr;
  if (!composite && !symbolic && name_of(top.find("Encoding")) != "WinAnsiEncoding") {
    return none();
  }
  if (symbolic && top.find("ToUnicode") == nullptr) {
    return none();
  }
  const Object* descriptor_entry = holder.find("FontDescriptor");
  if (descriptor_entry == nullptr || !descriptor_entry->as_ref()) {
    return none();
  }
  const ObjectRef descriptor_ref = *descriptor_entry->as_ref();
  auto descriptor_loaded = document.get(descriptor_ref);
  if (!descriptor_loaded || (*descriptor_loaded)->as_dict() == nullptr) {
    return none();
  }
  Dict descriptor = *(*descriptor_loaded)->as_dict();
  const auto flags = descriptor.find("Flags") != nullptr ? descriptor.find("Flags")->as_integer().value_or(0) : 0;
  if (!composite && ((flags & 4) != 0) != symbolic) {
    return none();  // WinAnsi codes in a symbolic font, or a font without an encoding that is not symbolic
  }
  const Object* file_entry = descriptor.find("FontFile2");
  if (file_entry == nullptr || !file_entry->as_ref()) {
    return none();
  }
  const ObjectRef file_ref = *file_entry->as_ref();
  auto file = document.get(file_ref);
  if (!file) {
    return none();
  }
  const auto program = decoded(document, **file);
  if (!program) {
    return none();
  }
  auto subset = font::detail::read_sfnt(*program);
  if (!subset || font::detail::units_per_em(*subset) != face->units || face->units == 0) {
    return none();
  }
  // The same font: the subset's outlines (some of them) are among the face's, every one compared.
  std::size_t same = 0;
  const std::uint16_t subset_glyphs = font::detail::glyph_count(*subset).value_or(0);
  for (std::uint32_t g = 1; g < subset_glyphs && same < kCompared; ++g) {
    const auto data = font::detail::glyph_data(*subset, static_cast<std::uint16_t>(g));
    const auto key = data ? font::detail::outline_key(*data) : std::nullopt;
    if (!key) {
      continue;
    }
    if (!face->outlines.contains(*key)) {
      return none();  // another version, or another font
    }
    ++same;
  }
  if (same < std::min<std::size_t>(3, subset_glyphs > 1 ? subset_glyphs - 1U : 0U) || same == 0) {
    return none();
  }
  // The letters the face has (and, WinAnsi fonts, its encoding).
  std::vector<char32_t> wanted;
  std::vector<std::uint16_t> glyphs;
  for (const char32_t c : letters) {
    if (std::ranges::find(wanted, c) != wanted.end()) {
      continue;
    }
    const auto found = face->cmap.find(c);
    if (found == face->cmap.end() || found->second == 0) {
      continue;
    }
    if (!composite && !symbolic) {
      const std::string code = win_ansi_bytes(std::u32string_view(&c, 1));
      if (code.empty() || (code[0] == '?' && c != U'?')) {
        continue;
      }
    }
    wanted.push_back(c);
    glyphs.push_back(found->second);
  }
  if (wanted.empty()) {
    return none();
  }
  auto extended = font::detail::extend(*subset, face->sfnt, glyphs);
  if (!extended) {
    return none();
  }
  const auto width_of = [&](std::uint16_t glyph) {
    return number_object(std::round(font::detail::advance(face->sfnt, glyph).value_or(0) * 1000.0 / face->units));
  };

  FontExtension out;
  out.added = wanted.size();
  Dict preview_descriptor = descriptor;
  if (composite) {
    // New CIDs: their glyphs' indices (an Identity CIDToGIDMap), or the next ones of a map stream.
    std::vector<std::uint32_t> cids;
    auto map = entry(document, holder, "CIDToGIDMap");
    std::optional<std::vector<std::byte>> map_data;
    if (map && map->value.as_name() == nullptr) {
      map_data = decoded(document, map->value);
      if (!map_data) {
        return none();
      }
    } else if (map && map->value.as_name()->bytes != "Identity") {
      return none();
    }
    // A map stream: a CID it maps to no glyph (the letter's Unicode value first, as some writers number
    // CIDs; else the first free one), or the next after its end.
    const auto free_at = [&](std::size_t cid) {
      return cid >= 1 && cid * 2 + 1 < map_data->size() && (*map_data)[cid * 2] == std::byte{0} && (*map_data)[(cid * 2) + 1] == std::byte{0} &&
             std::ranges::find(cids, static_cast<std::uint32_t>(cid)) == cids.end();
    };
    std::size_t next_free = 1;
    for (std::size_t k = 0; k < glyphs.size(); ++k) {
      const std::uint16_t index = extended->glyphs.at(glyphs[k]);
      if (!map_data) {
        cids.push_back(index);
        continue;
      }
      auto cid = static_cast<std::size_t>(wanted[k]);
      if (!free_at(cid)) {
        while (next_free * 2 < map_data->size() && !free_at(next_free)) {
          ++next_free;
        }
        cid = next_free * 2 < map_data->size() ? next_free : map_data->size() / 2;
      }
      if (cid > kMaxGlyph) {
        return none();
      }
      if (cid * 2 >= map_data->size()) {
        map_data->resize((cid + 1) * 2);
      }
      (*map_data)[cid * 2] = static_cast<std::byte>(index >> 8U);
      (*map_data)[(cid * 2) + 1] = static_cast<std::byte>(index & 0xFFU);
      cids.push_back(static_cast<std::uint32_t>(cid));
    }
    // Widths.
    Array widths;
    auto w = entry(document, holder, "W");
    if (w && w->value.as_array() != nullptr) {
      widths = *w->value.as_array();
    }
    for (std::size_t k = 0; k < cids.size(); ++k) {
      widths.emplace_back(static_cast<std::int64_t>(cids[k]));
      widths.emplace_back(Array{width_of(glyphs[k])});
    }
    // What the new CIDs are (every reader reads /ToUnicode).
    auto to_unicode = entry(document, top, "ToUnicode");
    auto cmap_text = to_unicode ? decoded(document, to_unicode->value) : std::nullopt;
    if (!cmap_text) {
      return none();
    }
    std::string text(crypto::der::chars_of(*cmap_text));
    const std::size_t end = text.rfind("endcmap");
    if (end == std::string::npos) {
      return none();
    }
    std::string chars = std::format("\n{} beginbfchar\n", cids.size());
    for (std::size_t k = 0; k < cids.size(); ++k) {
      chars += std::format("<{:04X}> <{}>\n", cids[k], utf16_hex(wanted[k]));
    }
    text.insert(end, chars + "endbfchar\n");
    const Object new_to_unicode = stream_of(Dict(), std::as_bytes(std::span(text)));
    // CIDSet, when there is one: the new CIDs in it.
    std::optional<Object> new_cid_set;
    auto cid_set = entry(document, descriptor, "CIDSet");
    if (cid_set) {
      auto bits = decoded(document, cid_set->value).value_or(std::vector<std::byte>{});
      for (const std::uint32_t cid : cids) {
        if (bits.size() <= cid / 8) {
          bits.resize((cid / 8) + 1);
        }
        bits[cid / 8] |= static_cast<std::byte>(0x80U >> (cid % 8));
      }
      new_cid_set = stream_of(Dict(), bits);
    }
    // The program.
    const auto sfnt = font::detail::write_sfnt(extended->font);
    Dict file_dict;
    file_dict.set("Length1", Object(static_cast<std::int64_t>(sfnt.size())));
    const Object new_file = stream_of(file_dict, sfnt);
    const std::optional<Object> new_map = map_data ? std::optional(stream_of(Dict(), *map_data)) : std::nullopt;

    // The writes.
    out.writes.emplace_back(file_ref.number, new_file);
    Dict new_holder = holder;
    if (w && w->ref) {
      out.writes.emplace_back(w->ref->number, Object(widths));
    } else {
      new_holder.set("W", widths);
    }
    if (new_map) {
      if (map->ref) {
        out.writes.emplace_back(map->ref->number, *new_map);
      } else {
        new_holder.set("CIDToGIDMap", *new_map);
      }
    }
    out.writes.emplace_back(holder_ref->number, Object(new_holder));
    Dict new_top = top;
    if (to_unicode->ref) {
      out.writes.emplace_back(to_unicode->ref->number, new_to_unicode);
    } else {
      new_top.set("ToUnicode", new_to_unicode);
      out.writes.emplace_back(font_ref.number, Object(new_top));
    }
    if (new_cid_set) {
      if (cid_set->ref) {
        out.writes.emplace_back(cid_set->ref->number, *new_cid_set);
      } else {
        Dict new_descriptor = descriptor;
        new_descriptor.set("CIDSet", *new_cid_set);
        out.writes.emplace_back(descriptor_ref.number, Object(new_descriptor));
      }
    }
    // The font to load and measure: everything inline.
    preview_descriptor.set("FontFile2", new_file);
    preview_descriptor.erase("CIDSet");
    Dict preview_holder = holder;
    preview_holder.set("W", widths);
    preview_holder.set("FontDescriptor", Object(preview_descriptor));
    if (new_map) {
      preview_holder.set("CIDToGIDMap", *new_map);
    }
    Dict preview = top;
    preview.set("DescendantFonts", Array{Object(preview_holder)});
    preview.set("ToUnicode", new_to_unicode);
    out.font = Object(std::move(preview));
    return std::optional(std::move(out));
  }

  // A simple TrueType font: each letter's code — WinAnsi's, or one the font does not use yet (after its
  // last, else in a gap) —, the program's cmap mapping it, /Widths and /ToUnicode saying it.
  std::int64_t first = top.find("FirstChar") != nullptr ? top.find("FirstChar")->as_integer().value_or(-1) : -1;
  auto widths_entry = entry(document, top, "Widths");
  if (first < 0 || !widths_entry || widths_entry->value.as_array() == nullptr) {
    return none();
  }
  Array widths = *widths_entry->value.as_array();
  std::vector<std::uint8_t> codes;
  if (symbolic) {
    const auto mapped = font::detail::symbol_cmap(*subset);
    const auto used = [&](std::int64_t code) {
      const auto at = code - first;
      const bool wide = at >= 0 && std::cmp_less(at, widths.size()) && widths[static_cast<std::size_t>(at)].as_number().value_or(0) != 0;
      return code == 0 || wide || mapped.contains(static_cast<std::uint8_t>(code)) || std::ranges::find(codes, code) != codes.end();
    };
    const std::int64_t last = first + static_cast<std::int64_t>(widths.size()) - 1;
    for (std::size_t k = 0; k < wanted.size(); ++k) {
      std::int64_t code = last + 1;
      while (code <= 255 && used(code)) {
        ++code;
      }
      if (code > 255) {
        code = 1;
        while (code <= 255 && used(code)) {
          ++code;
        }
      }
      if (code > 255) {
        wanted.resize(k);  // no code left for the others
        break;
      }
      codes.push_back(static_cast<std::uint8_t>(code));
    }
    if (wanted.empty()) {
      return none();
    }
    std::map<std::uint8_t, std::uint16_t> added;
    for (std::size_t k = 0; k < wanted.size(); ++k) {
      added.emplace(codes[k], extended->glyphs.at(glyphs[k]));
    }
    auto cmap = font::detail::symbol_cmap_with(extended->font, added);
    if (!cmap) {
      return none();
    }
    extended->font.tables["cmap"] = std::move(*cmap);
  } else {
    std::map<char32_t, std::uint16_t> added;
    for (std::size_t k = 0; k < wanted.size(); ++k) {
      added.emplace(wanted[k], extended->glyphs.at(glyphs[k]));
      codes.push_back(static_cast<std::uint8_t>(win_ansi_bytes(std::u32string_view(&wanted[k], 1))[0]));
    }
    auto cmap = font::detail::cmap_with(extended->font, added);
    if (!cmap) {
      return none();
    }
    extended->font.tables["cmap"] = std::move(*cmap);
  }
  out.added = wanted.size();
  for (std::size_t k = 0; k < wanted.size(); ++k) {
    const std::int64_t code = codes[k];
    while (code < first) {
      widths.insert(widths.begin(), Object(std::int64_t{0}));
      --first;
    }
    while (code >= first + static_cast<std::int64_t>(widths.size())) {
      widths.emplace_back(std::int64_t{0});
    }
    widths[static_cast<std::size_t>(code - first)] = width_of(glyphs[k]);
  }
  // What the new codes are, when the font says it in a /ToUnicode.
  std::optional<Object> new_to_unicode;
  auto to_unicode = entry(document, top, "ToUnicode");
  if (to_unicode) {
    auto cmap_text = decoded(document, to_unicode->value);
    std::string text = cmap_text ? std::string(crypto::der::chars_of(*cmap_text)) : std::string();
    const std::size_t end = text.rfind("endcmap");
    if (end == std::string::npos) {
      return none();
    }
    std::string chars = std::format("\n{} beginbfchar\n", wanted.size());
    for (std::size_t k = 0; k < wanted.size(); ++k) {
      chars += std::format("<{:02X}> <{}>\n", codes[k], utf16_hex(wanted[k]));
    }
    text.insert(end, chars + "endbfchar\n");
    new_to_unicode = stream_of(Dict(), std::as_bytes(std::span(text)));
  }
  const auto sfnt = font::detail::write_sfnt(extended->font);
  Dict file_dict;
  file_dict.set("Length1", Object(static_cast<std::int64_t>(sfnt.size())));
  const Object new_file = stream_of(file_dict, sfnt);
  out.writes.emplace_back(file_ref.number, new_file);
  Dict new_top = top;
  new_top.set("FirstChar", Object(first));
  new_top.set("LastChar", Object(first + static_cast<std::int64_t>(widths.size()) - 1));
  if (widths_entry->ref) {
    out.writes.emplace_back(widths_entry->ref->number, Object(widths));
  } else {
    new_top.set("Widths", widths);
  }
  if (new_to_unicode) {
    if (to_unicode->ref) {
      out.writes.emplace_back(to_unicode->ref->number, *new_to_unicode);
    } else {
      new_top.set("ToUnicode", *new_to_unicode);
    }
  }
  out.writes.emplace_back(font_ref.number, Object(new_top));
  preview_descriptor.set("FontFile2", new_file);
  Dict preview = new_top;
  preview.set("Widths", widths);
  if (new_to_unicode) {
    preview.set("ToUnicode", *new_to_unicode);
  }
  preview.set("FontDescriptor", Object(preview_descriptor));
  out.font = Object(std::move(preview));
  return std::optional(std::move(out));
}

Result<void> commit(Document& document, const FontExtension& extension) {
  for (const auto& [number, value] : extension.writes) {
    if (auto set = document.set(number, value); !set) {
      return set;
    }
  }
  return {};
}

}  // namespace detail
}  // namespace capriole::edit
