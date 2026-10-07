#include "text_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/content/operators.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/filters/flate.hpp"
#include "common.hpp"
#include "font/standard_fonts.hpp"

namespace capriole::edit::detail {

namespace {

using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

std::string name_of(const Object* value) { return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string(); }

}  // namespace

std::string similar_font(Document& document, const std::optional<ObjectRef>& ref) {
  std::string base;
  std::uint32_t flags = 0;
  double italic = 0;
  double weight = 400;
  auto loaded = ref ? document.get(*ref) : Result<const Object*>(nullptr);
  const Dict* font = loaded && *loaded != nullptr ? (*loaded)->as_dict() : nullptr;
  if (font != nullptr) {
    base = std::string(font::detail::without_subset_tag(name_of(font->find("BaseFont"))));
    const Dict* holder = font;
    Result<Object> descendant = Object();
    if (name_of(font->find("Subtype")) == "Type0") {
      if (const Object* d = font->find("DescendantFonts")) {
        auto list = document.resolve(*d);
        if (list && list->as_array() != nullptr && !list->as_array()->empty()) {
          descendant = document.resolve(list->as_array()->front());
          holder = descendant && descendant->as_dict() != nullptr ? descendant->as_dict() : font;
        }
      }
    }
    if (const Object* d = holder->find("FontDescriptor")) {
      auto descriptor = document.resolve(*d);
      if (descriptor && descriptor->as_dict() != nullptr) {
        const Dict& dd = *descriptor->as_dict();
        flags = static_cast<std::uint32_t>(dd.find("Flags") != nullptr ? dd.find("Flags")->as_integer().value_or(0) : 0);
        italic = dd.find("ItalicAngle") != nullptr ? dd.find("ItalicAngle")->as_number().value_or(0) : 0;
        weight = dd.find("FontWeight") != nullptr ? dd.find("FontWeight")->as_number().value_or(400) : 400;
      }
    }
  }
  using enum font::detail::StandardFont;
  static constexpr std::array<std::string_view, 14> kNames = {
      "Helvetica",        "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Times-Roman",         "Times-Bold", "Times-Italic",
      "Times-BoldItalic", "Courier",        "Courier-Bold",      "Courier-Oblique",       "Courier-BoldOblique", "Helvetica",  "Helvetica"};
  // Symbol and Dingbats cannot write Latin text: Helvetica in their place.
  const auto standard = font::detail::substitute_font(base, flags, italic, weight);
  return std::string(kNames.at(static_cast<std::size_t>(standard)));
}

std::optional<std::string> encode_in(font::detail::Font& font, font::detail::FontContext& context, std::u32string_view text, bool two_byte,
                                     std::map<std::uint32_t, double>& widths) {
  std::string out;
  for (const auto& code : encode_each(font, context, text, two_byte, widths)) {
    if (!code) {
      return std::nullopt;
    }
    out += *code;
  }
  return out;
}

std::vector<std::optional<std::string>> encode_each(font::detail::Font& font, font::detail::FontContext& context, std::u32string_view text, bool two_byte,
                                                    std::map<std::uint32_t, double>& widths) {
  std::map<char32_t, std::string> codes;
  const auto add = [&](std::uint32_t code, std::uint8_t length) {
    const font::detail::Font::Code c{.code = code, .length = length, .cid = code};
    // A composite font's code means, for every reader, what its /ToUnicode says (the engine also reads
    // the program's cmap; other readers do not): written only so.
    const std::u32string meaning = length == 2 && font.has_to_unicode() ? font.to_unicode_text(c) : font.text(c);
    if (meaning.size() != 1 || codes.contains(meaning[0])) {
      return;
    }
    if (font.glyph_source(c, context).glyph == 0 && font.kind() != font::detail::Font::Kind::kType3) {
      return;
    }
    std::string bytes;
    if (length == 2) {
      bytes.push_back(static_cast<char>((code >> 8U) & 0xFFU));
    }
    bytes.push_back(static_cast<char>(code & 0xFFU));
    codes.emplace(meaning[0], std::move(bytes));
  };
  if (two_byte) {
    for (std::uint32_t code = 0; code < 0x10000; ++code) {
      add(code, 2);
    }
  } else {
    for (std::uint32_t code = 0; code < 256; ++code) {
      add(code, 1);
    }
  }
  std::vector<std::optional<std::string>> out;
  out.reserve(text.size());
  for (const char32_t c : text) {
    const auto found = codes.find(c);
    if (found == codes.end()) {
      out.emplace_back();
      continue;
    }
    if (!two_byte && font.kind() == font::detail::Font::Kind::kSimple) {
      const auto code = static_cast<std::uint8_t>(found->second.back());
      const font::detail::Font::Code fc{.code = code, .length = 1, .cid = code};
      if (font.width(fc) == 0 && c != U' ') {
        const auto source = font.glyph_source(fc, context);
        const auto advance = source.program != nullptr ? source.program->advance(source.glyph) : std::nullopt;
        if (!advance || *advance <= 0) {
          out.emplace_back();
          continue;
        }
        widths[code] = *advance;
      }
    }
    out.emplace_back(found->second);
  }
  return out;
}

bool widths_writable(Document& document, ObjectRef font_ref, const std::map<std::uint32_t, double>& widths, const graphics::DisplayList& list) {
  if (widths.empty()) {
    return true;
  }
  auto loaded = document.get(font_ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return false;
  }
  const Dict& font = *(*loaded)->as_dict();
  const Object* first_entry = font.find("FirstChar");
  const Object* widths_entry = font.find("Widths");
  const std::optional<std::int64_t> first_char = first_entry != nullptr ? first_entry->as_integer() : std::nullopt;
  if (!first_char || widths_entry == nullptr) {
    return false;
  }
  auto array = document.resolve(*widths_entry);
  if (!array || array->as_array() == nullptr) {
    return false;
  }
  const std::int64_t first = *first_char;
  for (const auto& entry : widths) {
    const std::int64_t at = static_cast<std::int64_t>(entry.first) - first;
    if (at < 0 || std::cmp_greater_equal(at, array->as_array()->size())) {
      return false;
    }
  }
  for (const auto& command : list.commands) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
    if (t != nullptr && (*t)->font_ref == font_ref && std::ranges::any_of((*t)->glyphs, [&](const graphics::Glyph& g) { return widths.contains(g.code); })) {
      return false;
    }
  }
  return true;
}

Result<void> set_widths(Document& document, ObjectRef font_ref, const std::map<std::uint32_t, double>& widths) {
  if (widths.empty()) {
    return {};
  }
  auto loaded = document.get(font_ref);
  if (!loaded || (*loaded)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the font is not a dictionary");
  }
  Dict font = *(*loaded)->as_dict();
  const auto first = font.find("FirstChar") != nullptr ? font.find("FirstChar")->as_integer().value_or(0) : 0;
  const Object* entry = font.find("Widths");
  auto resolved = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!resolved || resolved->as_array() == nullptr) {
    return make_error(ErrorCode::kUnsupported, "the font gives no widths");
  }
  Array array = *resolved->as_array();
  for (const auto& [code, width] : widths) {
    const std::int64_t at = static_cast<std::int64_t>(code) - first;
    if (at < 0 || std::cmp_greater_equal(at, array.size())) {
      return make_error(ErrorCode::kUnsupported, "a code outside the font's widths");
    }
    array[static_cast<std::size_t>(at)] = detail::number_object(std::round(width * 1000) / 1000);
  }
  if (const auto ref = entry->as_ref()) {
    return document.set(ref->number, Object(std::move(array)));
  }
  font.set("Widths", std::move(array));
  return document.set(font_ref.number, Object(std::move(font)));
}

std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> content_operators(const graphics::DisplayList& list) {
  std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>> out;
  for (std::size_t c = 0; c < list.commands.size() && c < list.origins.size(); ++c) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&list.commands[c]);
    if (t == nullptr) {
      continue;
    }
    bool content = true;
    for (std::uint32_t at = list.origins[c].invocation, steps = 0; at != 0 && steps < list.invocations.size(); at = list.invocations[at].parent, ++steps) {
      if (at >= list.invocations.size() || list.invocations[at].kind != graphics::Invocation::Kind::kForm) {
        content = false;
        break;
      }
    }
    if (content) {
      out.emplace((*t)->stream ? std::optional((*t)->stream->number) : std::nullopt, (*t)->operation);
    }
  }
  return out;
}

bool of_content(const std::set<std::pair<std::optional<std::uint32_t>, std::uint32_t>>& operators, const text::Provenance& p) {
  return operators.contains({p.stream ? std::optional(p.stream->number) : std::nullopt, p.operation});
}

graphics::Rgb fill_of(const graphics::DisplayList& list, const text::Provenance& p) {
  for (const auto& command : list.commands) {
    const auto* t = std::get_if<graphics::Boxed<graphics::ShowText>>(&command);
    if (t != nullptr && (*t)->stream == p.stream && (*t)->operation == p.operation) {
      return (*t)->fill < list.colors.size() ? list.colors[(*t)->fill].rgb : graphics::Rgb{0, 0, 0};
    }
  }
  return {0, 0, 0};
}

Result<std::string> add_font_resource(Document& document, std::uint32_t page_index, ObjectRef font) {
  auto page = page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  Dict updated = page->dict;
  Dict resources;
  if (auto r = inherited(document, page->dict, "Resources")) {
    auto resolved = document.resolve(*r);
    if (resolved && resolved->as_dict() != nullptr) {
      resources = *resolved->as_dict();
    }
  }
  Dict fonts;
  if (const Object* f = resources.find("Font")) {
    auto resolved = document.resolve(*f);
    if (resolved && resolved->as_dict() != nullptr) {
      fonts = *resolved->as_dict();
    }
  }
  std::string key;
  for (int n = 0;; ++n) {
    key = std::format("CprText{}", n);
    if (fonts.find(key) == nullptr) {
      break;
    }
  }
  fonts.set(key, font);
  resources.set("Font", std::move(fonts));
  updated.set("Resources", std::move(resources));
  if (auto set = document.set(page->ref.number, Object(std::move(updated))); !set) {
    return std::unexpected(std::move(set.error()));
  }
  return key;
}

Result<void> append_content(Document& document, std::uint32_t page_index, std::string_view operators) {
  auto page = page_at(document, page_index, "the page's text");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  std::size_t open_states = 0;
  if (auto data = content::page_content(document, page->ref)) {
    if (auto operations = content::parse_content(*data)) {
      for (const content::Operation& op : *operations) {
        if (op.op == content::Operator::kSave) {
          ++open_states;
        } else if (op.op == content::Operator::kRestore && open_states > 0) {
          --open_states;
        }
      }
    }
  }
  std::string text;
  for (std::size_t i = 0; i < std::min<std::size_t>(open_states, 64); ++i) {
    text += "Q\n";
  }
  text += operators;
  model::Stream stream;
  auto compressed = filters::flate_encode(std::as_bytes(std::span(text)));
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  stream.dict.set("Filter", name("FlateDecode"));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto stream_ref = document.add(Object(std::move(stream)));
  if (!stream_ref) {
    return std::unexpected(std::move(stream_ref.error()));
  }
  Dict updated = page->dict;
  Array contents;
  if (const Object* c = page->dict.find("Contents")) {
    auto resolved = document.resolve(*c);
    if (resolved && resolved->as_array() != nullptr) {
      contents = *resolved->as_array();
    } else {
      contents.push_back(*c);
    }
  }
  contents.emplace_back(*stream_ref);
  updated.set("Contents", std::move(contents));
  return document.set(page->ref.number, Object(std::move(updated)));
}

}  // namespace capriole::edit::detail
