#include "sfnt.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace capriole::font::detail {

namespace {

// Bounds on what a font may make the engine do.
constexpr std::uint32_t kMaxFaces = 256;
constexpr std::uint16_t kMaxTables = 256;
constexpr std::size_t kMaxAdded = 4096;  // glyphs added at once
constexpr std::size_t kMaxDepth = 8;     // of composite glyphs
constexpr std::size_t kMaxCmapGroups = 100'000;

using Bytes = std::vector<std::byte>;

std::optional<std::uint16_t> u16(std::span<const std::byte> d, std::size_t at) {
  if (at > d.size() || d.size() - at < 2) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>((std::to_integer<unsigned>(d[at]) << 8U) | std::to_integer<unsigned>(d[at + 1]));
}

std::optional<std::uint32_t> u32(std::span<const std::byte> d, std::size_t at) {
  const auto hi = u16(d, at);
  const auto lo = u16(d, at + 2);
  if (!hi || !lo) {
    return std::nullopt;
  }
  return (static_cast<std::uint32_t>(*hi) << 16U) | *lo;
}

std::optional<std::int16_t> i16(std::span<const std::byte> d, std::size_t at) {
  const auto v = u16(d, at);
  return v ? std::optional(static_cast<std::int16_t>(*v)) : std::nullopt;
}

void put16(Bytes& d, std::size_t at, std::uint16_t v) {
  d[at] = static_cast<std::byte>(v >> 8U);
  d[at + 1] = static_cast<std::byte>(v & 0xFFU);
}

void put32(Bytes& d, std::size_t at, std::uint32_t v) {
  put16(d, at, static_cast<std::uint16_t>(v >> 16U));
  put16(d, at + 2, static_cast<std::uint16_t>(v & 0xFFFFU));
}

void append16(Bytes& d, std::uint16_t v) {
  d.push_back(static_cast<std::byte>(v >> 8U));
  d.push_back(static_cast<std::byte>(v & 0xFFU));
}

void append32(Bytes& d, std::uint32_t v) {
  append16(d, static_cast<std::uint16_t>(v >> 16U));
  append16(d, static_cast<std::uint16_t>(v & 0xFFFFU));
}

std::uint32_t checksum(std::span<const std::byte> d) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < d.size(); i += 4) {
    std::uint32_t word = 0;
    for (std::size_t k = 0; k < 4; ++k) {
      word = (word << 8U) | (i + k < d.size() ? std::to_integer<std::uint32_t>(d[i + k]) : 0U);
    }
    sum += word;
  }
  return sum;
}

const Bytes* table(const Sfnt& font, const char* tag) {
  const auto found = font.tables.find(tag);
  return found != font.tables.end() ? &found->second : nullptr;
}

// The glyph offsets of loca (count + 1 of them, each within glyf); nullopt when they are not.
std::optional<std::vector<std::uint32_t>> loca_of(const Sfnt& font) {
  const Bytes* head = table(font, "head");
  const Bytes* loca = table(font, "loca");
  const Bytes* glyf = table(font, "glyf");
  const auto count = glyph_count(font);
  const auto format = head != nullptr ? i16(*head, 50) : std::nullopt;
  if (loca == nullptr || glyf == nullptr || !count || !format) {
    return std::nullopt;
  }
  std::vector<std::uint32_t> out;
  out.reserve(static_cast<std::size_t>(*count) + 1);
  for (std::size_t i = 0; i <= *count; ++i) {
    std::optional<std::uint32_t> v;
    if (*format != 0) {
      v = u32(*loca, i * 4);
    } else if (const auto half = u16(*loca, i * 2)) {
      v = *half * 2U;
    }
    if (!v || *v > glyf->size() || (!out.empty() && *v < out.back())) {
      return std::nullopt;
    }
    out.push_back(*v);
  }
  return out;
}

struct Metrics {
  std::vector<std::uint16_t> advances;
  std::vector<std::int16_t> bearings;
};

std::optional<Metrics> metrics_of(const Sfnt& font) {
  const Bytes* hhea = table(font, "hhea");
  const Bytes* hmtx = table(font, "hmtx");
  const auto count = glyph_count(font);
  const auto longs = hhea != nullptr ? u16(*hhea, 34) : std::nullopt;
  if (hmtx == nullptr || !count || !longs || *longs == 0 || *longs > *count) {
    return std::nullopt;
  }
  Metrics m;
  for (std::size_t i = 0; i < *count; ++i) {
    const std::size_t at = i < *longs ? i * 4 : (static_cast<std::size_t>(*longs) * 4) + ((i - *longs) * 2);
    const auto advance = i < *longs ? u16(*hmtx, at) : std::optional(m.advances.back());
    const auto bearing = i16(*hmtx, i < *longs ? at + 2 : at);
    if (!advance || !bearing) {
      return std::nullopt;
    }
    m.advances.push_back(*advance);
    m.bearings.push_back(*bearing);
  }
  return m;
}

// A glyph without its instructions; and, for a composite one, where its components' glyph indices are.
struct Stripped {
  Bytes data;
  std::vector<std::size_t> components;  // offsets of the component glyph indices in data
};

std::optional<Stripped> stripped(std::span<const std::byte> glyph) {
  Stripped out;
  if (glyph.empty()) {
    return out;
  }
  const auto contours = i16(glyph, 0);
  if (!contours) {
    return std::nullopt;
  }
  if (*contours >= 0) {
    const std::size_t ends = 10 + (static_cast<std::size_t>(*contours) * 2);
    const auto length = u16(glyph, ends);
    if (!length || ends + 2 + *length > glyph.size()) {
      return std::nullopt;
    }
    out.data.assign(glyph.begin(), glyph.begin() + static_cast<std::ptrdiff_t>(ends));
    append16(out.data, 0);
    out.data.insert(out.data.end(), glyph.begin() + static_cast<std::ptrdiff_t>(ends + 2 + *length), glyph.end());
    return out;
  }
  std::size_t at = 10;
  std::size_t last_flags = 0;
  std::uint16_t last_value = 0;
  for (std::size_t n = 0;; ++n) {
    const auto flags = u16(glyph, at);
    if (!flags || n > 1000) {
      return std::nullopt;
    }
    last_flags = at;
    last_value = *flags;
    out.components.push_back(at + 2);
    std::size_t size = 4 + ((*flags & 0x1U) != 0 ? 4 : 2);
    if ((*flags & 0x8U) != 0) {
      size += 2;
    } else if ((*flags & 0x40U) != 0) {
      size += 4;
    } else if ((*flags & 0x80U) != 0) {
      size += 8;
    }
    if (at + size > glyph.size()) {
      return std::nullopt;
    }
    at += size;
    if ((*flags & 0x20U) == 0) {
      break;
    }
  }
  out.data.assign(glyph.begin(), glyph.begin() + static_cast<std::ptrdiff_t>(at));
  put16(out.data, last_flags, static_cast<std::uint16_t>(last_value & ~0x100U));  // no instructions
  return out;
}

// A simple glyph's points: (x, y, on curve) absolute, and its contours' last points.
struct Outline {
  std::vector<std::uint16_t> ends;
  std::vector<std::array<int, 3>> points;
  bool operator==(const Outline&) const = default;
};

std::optional<Outline> outline_of(std::span<const std::byte> glyph) {
  Outline out;
  if (glyph.empty()) {
    return out;
  }
  const auto contours = i16(glyph, 0);
  if (!contours || *contours < 0) {
    return std::nullopt;
  }
  std::size_t at = 10;
  for (int c = 0; c < *contours; ++c, at += 2) {
    const auto end = u16(glyph, at);
    if (!end || (!out.ends.empty() && *end < out.ends.back())) {
      return std::nullopt;
    }
    out.ends.push_back(*end);
  }
  const auto length = u16(glyph, at);
  if (!length) {
    return std::nullopt;
  }
  at += std::size_t{2} + *length;
  const std::size_t count = out.ends.empty() ? 0 : static_cast<std::size_t>(out.ends.back()) + 1;
  std::vector<std::uint8_t> flags;
  while (flags.size() < count) {
    if (at >= glyph.size()) {
      return std::nullopt;
    }
    const auto flag = std::to_integer<std::uint8_t>(glyph[at++]);
    flags.push_back(flag);
    if ((flag & 0x8U) != 0) {
      if (at >= glyph.size()) {
        return std::nullopt;
      }
      for (auto r = std::to_integer<unsigned>(glyph[at++]); r > 0 && flags.size() < count; --r) {
        flags.push_back(flag);
      }
    }
  }
  const auto coordinates = [&](std::uint8_t short_bit, std::uint8_t same_bit, std::size_t axis) -> bool {
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if ((flags[i] & short_bit) != 0) {
        if (at >= glyph.size()) {
          return false;
        }
        const int d = std::to_integer<int>(glyph[at++]);
        value += (flags[i] & same_bit) != 0 ? d : -d;
      } else if ((flags[i] & same_bit) == 0) {
        const auto d = i16(glyph, at);
        if (!d) {
          return false;
        }
        value += *d;
        at += 2;
      }
      if (axis == 0) {
        out.points.push_back({value, 0, (flags[i] & 0x1U) != 0 ? 1 : 0});
      } else {
        out.points[i][1] = value;
      }
    }
    return true;
  };
  if (!coordinates(0x2, 0x10, 0) || !coordinates(0x4, 0x20, 1)) {
    return std::nullopt;
  }
  return out;
}

}  // namespace

std::uint32_t face_count(std::span<const std::byte> data) {
  const auto tag = u32(data, 0);
  if (!tag) {
    return 0;
  }
  if (*tag == 0x74746366) {  // 'ttcf'
    const auto n = u32(data, 8);
    return n && *n <= kMaxFaces ? *n : 0;
  }
  return *tag == 0x00010000 || *tag == 0x74727565 ? 1 : 0;  // TrueType ('true': Apple)
}

Result<Sfnt> read_sfnt(std::span<const std::byte> data, std::uint32_t face) {
  const std::uint32_t faces = face_count(data);
  if (faces == 0) {
    return make_error(ErrorCode::kUnsupported,
                      u32(data, 0) == 0x4F54544FU ? "a font with PostScript (CFF) outlines, not TrueType ones" : "not a TrueType font");
  }
  if (face >= faces) {
    return make_error(ErrorCode::kOutOfRange, "no such face in the font collection");
  }
  std::size_t base = 0;
  if (u32(data, 0) == 0x74746366U) {
    const auto offset = u32(data, 12 + (static_cast<std::size_t>(face) * 4));
    if (!offset || *offset >= data.size()) {
      return make_error(ErrorCode::kSyntax, "a font collection's face beyond its data");
    }
    base = *offset;
  }
  Sfnt font;
  const auto version = u32(data, base);
  const auto tables = u16(data, base + 4);
  if (!version || !tables || *tables > kMaxTables || (*version != 0x00010000 && *version != 0x74727565)) {
    return make_error(ErrorCode::kUnsupported, "not a TrueType font");
  }
  font.version = *version;
  for (std::size_t i = 0; i < *tables; ++i) {
    const std::size_t entry = base + 12 + (i * 16);
    const auto offset = u32(data, entry + 8);
    const auto length = u32(data, entry + 12);
    if (!offset || !length || *offset > data.size() || *length > data.size() - *offset) {
      return make_error(ErrorCode::kSyntax, "a font table beyond the font's data");
    }
    std::string tag(4, ' ');
    for (std::size_t k = 0; k < 4; ++k) {
      tag[k] = static_cast<char>(std::to_integer<unsigned char>(data[entry + k]));
    }
    font.tables[tag] =
        Bytes(data.begin() + static_cast<std::ptrdiff_t>(*offset), data.begin() + static_cast<std::ptrdiff_t>(*offset) + static_cast<std::ptrdiff_t>(*length));
  }
  return font;
}

std::vector<std::byte> write_sfnt(const Sfnt& font) {
  const auto n = static_cast<std::uint16_t>(font.tables.size());
  std::uint16_t power = 1;
  std::uint16_t selector = 0;
  while (static_cast<unsigned>(power) * 2 <= n) {
    power = static_cast<std::uint16_t>(power * 2);
    ++selector;
  }
  Bytes out;
  append32(out, font.version);
  append16(out, n);
  append16(out, static_cast<std::uint16_t>(power * 16));
  append16(out, selector);
  append16(out, static_cast<std::uint16_t>((n * 16) - (power * 16)));
  const std::size_t offset = 12 + (static_cast<std::size_t>(n) * 16);
  std::size_t head_at = 0;
  Bytes bodies;
  for (const auto& [tag, data] : font.tables) {
    Bytes body = data;
    if (tag == "head" && body.size() >= 12) {
      put32(body, 8, 0);  // checkSumAdjustment: 0 while summing
      head_at = offset + bodies.size();
    }
    for (std::size_t k = 0; k < 4; ++k) {
      out.push_back(static_cast<std::byte>(k < tag.size() ? tag[k] : ' '));
    }
    append32(out, checksum(body));
    append32(out, static_cast<std::uint32_t>(offset + bodies.size()));
    append32(out, static_cast<std::uint32_t>(body.size()));
    bodies.insert(bodies.end(), body.begin(), body.end());
    bodies.resize((bodies.size() + 3) & ~std::size_t{3});
  }
  out.insert(out.end(), bodies.begin(), bodies.end());
  if (head_at != 0) {
    put32(out, head_at + 8, 0xB1B0AFBAU - checksum(out));
  }
  return out;
}

std::optional<std::string> postscript_name(const Sfnt& font) { return name_string(font, 6); }

std::optional<std::string> name_string(const Sfnt& font, std::uint16_t wanted) {
  const Bytes* name = table(font, "name");
  if (name == nullptr) {
    return std::nullopt;
  }
  const auto count = u16(*name, 2);
  const auto strings = u16(*name, 4);
  if (!count || !strings) {
    return std::nullopt;
  }
  std::optional<std::string> mac;
  for (std::size_t i = 0; i < *count; ++i) {
    const std::size_t record = 6 + (i * 12);
    const auto platform = u16(*name, record);
    const auto id = u16(*name, record + 6);
    const auto length = u16(*name, record + 8);
    const auto offset = u16(*name, record + 10);
    if (!platform || !id || !length || !offset) {
      return std::nullopt;
    }
    const std::size_t at = static_cast<std::size_t>(*strings) + *offset;
    if (*id != wanted || at > name->size() || *length > name->size() - at) {
      continue;
    }
    std::string text;
    if (*platform == 3 || *platform == 0) {  // UTF-16BE; a PostScript name is ASCII
      for (std::size_t k = 0; k + 1 < *length; k += 2) {
        text.push_back(static_cast<char>(*u16(*name, at + k) & 0x7FU));
      }
      return text;
    }
    if (*platform == 1) {
      for (std::size_t k = 0; k < *length; ++k) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>((*name)[at + k]) & 0x7FU));
      }
      mac = text;
    }
  }
  return mac;
}

std::optional<std::uint16_t> units_per_em(const Sfnt& font) {
  const Bytes* head = table(font, "head");
  return head != nullptr ? u16(*head, 18) : std::nullopt;
}

std::optional<std::uint16_t> glyph_count(const Sfnt& font) {
  const Bytes* maxp = table(font, "maxp");
  return maxp != nullptr ? u16(*maxp, 4) : std::nullopt;
}

std::optional<std::uint16_t> advance(const Sfnt& font, std::uint16_t glyph) {
  const auto m = metrics_of(font);
  return m && glyph < m->advances.size() ? std::optional(m->advances[glyph]) : std::nullopt;
}

std::uint16_t fs_type(const Sfnt& font) {
  const Bytes* os2 = table(font, "OS/2");
  return os2 != nullptr ? u16(*os2, 8).value_or(0) : 0;
}

namespace {
std::map<std::uint32_t, std::uint16_t> subtable_map(const Bytes& cmap, std::uint32_t at);
}  // namespace

std::map<char32_t, std::uint16_t> unicode_cmap(const Sfnt& font) {
  std::map<char32_t, std::uint16_t> out;
  const Bytes* cmap = table(font, "cmap");
  if (cmap == nullptr) {
    return out;
  }
  const auto count = u16(*cmap, 2);
  std::optional<std::uint32_t> twelve;
  std::optional<std::uint32_t> four;
  for (std::size_t i = 0; count && i < *count; ++i) {
    const auto platform = u16(*cmap, 4 + (i * 8));
    const auto encoding = u16(*cmap, 6 + (i * 8));
    const auto offset = u32(*cmap, 8 + (i * 8));
    if (!platform || !encoding || !offset) {
      break;
    }
    const auto format = u16(*cmap, *offset);
    if (format == 12 && ((*platform == 3 && *encoding == 10) || *platform == 0)) {
      twelve = offset;
    } else if (format == 4 && ((*platform == 3 && *encoding == 1) || *platform == 0)) {
      four = four.value_or(*offset);
    }
  }
  if (twelve) {
    const auto groups = u32(*cmap, *twelve + 12);
    for (std::size_t g = 0; groups && g < std::min<std::size_t>(*groups, kMaxCmapGroups); ++g) {
      const std::size_t at = *twelve + 16 + (g * 12);
      const auto start = u32(*cmap, at);
      const auto end = u32(*cmap, at + 4);
      const auto glyph = u32(*cmap, at + 8);
      if (!start || !end || !glyph || *end < *start || *end > 0x10FFFF || out.size() + (*end - *start) > 0x110000) {
        break;
      }
      for (std::uint32_t c = *start; c <= *end; ++c) {
        out.emplace(static_cast<char32_t>(c), static_cast<std::uint16_t>(*glyph + (c - *start)));
      }
    }
    return out;
  }
  if (!four) {
    return out;
  }
  for (const auto& [c, g] : subtable_map(*cmap, *four)) {
    out.emplace(static_cast<char32_t>(c), g);
  }
  return out;
}

std::optional<std::vector<std::byte>> glyph_data(const Sfnt& font, std::uint16_t glyph) {
  const auto loca = loca_of(font);
  if (!loca || static_cast<std::size_t>(glyph) + 1 >= loca->size()) {
    return std::nullopt;
  }
  const Bytes& glyf = *table(font, "glyf");
  return Bytes(glyf.begin() + static_cast<std::ptrdiff_t>((*loca)[glyph]), glyf.begin() + static_cast<std::ptrdiff_t>((*loca)[glyph + 1]));
}

std::optional<std::string> outline_key(std::span<const std::byte> glyph) {
  const auto o = outline_of(glyph);
  if (!o || o->points.empty()) {
    return std::nullopt;
  }
  std::string key;
  for (const std::uint16_t e : o->ends) {
    key += std::to_string(e) + ';';
  }
  key += '|';
  for (const auto& p : o->points) {
    key += std::to_string(p[0]) + ',' + std::to_string(p[1]) + (p[2] != 0 ? '+' : '-');
  }
  return key;
}

bool same_outline(std::span<const std::byte> a, std::span<const std::byte> b) {
  const auto x = outline_of(a);
  const auto y = outline_of(b);
  return x && y && *x == *y;
}

Result<Extended> extend(const Sfnt& subset, const Sfnt& full, std::span<const std::uint16_t> glyphs) {
  if (table(subset, "fvar") != nullptr || table(full, "fvar") != nullptr) {
    return make_error(ErrorCode::kUnsupported, "a variable font");
  }
  const auto sub_loca = loca_of(subset);
  const auto full_loca = loca_of(full);
  auto sub_metrics = metrics_of(subset);
  const auto full_metrics = metrics_of(full);
  const Bytes* head = table(subset, "head");
  const Bytes* hhea = table(subset, "hhea");
  const Bytes* maxp = table(subset, "maxp");
  if (!sub_loca || !full_loca || !sub_metrics || !full_metrics || head == nullptr || head->size() < 54 || hhea == nullptr || hhea->size() < 36 ||
      maxp == nullptr || maxp->size() < 6) {
    return make_error(ErrorCode::kUnsupported, "a TrueType font without the tables its glyphs need");
  }
  const Bytes& full_glyf = *table(full, "glyf");
  const Bytes& sub_glyf = *table(subset, "glyf");
  Extended out;
  std::vector<Bytes> added;
  std::size_t next = sub_loca->size() - 1;
  // Adds a glyph of the full font (its components first); its index in the font now.
  const auto add = [&](const auto& self, std::uint16_t glyph, std::size_t depth) -> Result<std::uint16_t> {
    if (const auto found = out.glyphs.find(glyph); found != out.glyphs.end()) {
      return found->second;
    }
    if (depth > kMaxDepth || static_cast<std::size_t>(glyph) + 1 >= full_loca->size() || added.size() >= kMaxAdded || next >= 0xFFFF) {
      return make_error(ErrorCode::kLimitExceeded, "glyphs that cannot be added (too many, too deep, or none such)");
    }
    auto data = stripped(std::span(full_glyf).subspan((*full_loca)[glyph], (*full_loca)[glyph + 1] - (*full_loca)[glyph]));
    if (!data) {
      return make_error(ErrorCode::kSyntax, "a glyph of the font cannot be read");
    }
    for (const std::size_t at : data->components) {
      auto component = self(self, *u16(data->data, at), depth + 1);
      if (!component) {
        return std::unexpected(std::move(component.error()));
      }
      put16(data->data, at, *component);
    }
    const auto index = static_cast<std::uint16_t>(next++);
    out.glyphs.emplace(glyph, index);
    added.push_back(std::move(data->data));
    sub_metrics->advances.push_back(full_metrics->advances[glyph]);
    sub_metrics->bearings.push_back(full_metrics->bearings[glyph]);
    return index;
  };
  for (const std::uint16_t glyph : glyphs) {
    if (auto index = add(add, glyph, 0); !index) {
      return std::unexpected(std::move(index.error()));
    }
  }
  const std::size_t count = next;
  out.font = subset;
  auto& tables = out.font.tables;
  for (const char* gone : {"hdmx", "LTSH", "vmtx", "vhea", "DSIG"}) {
    tables.erase(gone);
  }
  // glyf and a long loca: the subset's glyphs as they are, the new ones after, each 4-byte aligned.
  Bytes glyf(sub_glyf.begin(), sub_glyf.begin() + static_cast<std::ptrdiff_t>(sub_loca->back()));
  Bytes loca;
  for (const std::uint32_t offset : *sub_loca) {
    append32(loca, offset);
  }
  for (const Bytes& data : added) {
    glyf.resize((glyf.size() + 3) & ~std::size_t{3});
    put32(loca, loca.size() - 4, static_cast<std::uint32_t>(glyf.size()));  // the previous end, aligned
    glyf.insert(glyf.end(), data.begin(), data.end());
    append32(loca, static_cast<std::uint32_t>(glyf.size()));
  }
  tables["glyf"] = std::move(glyf);
  tables["loca"] = std::move(loca);
  // hmtx: every glyph a long metric.
  Bytes hmtx;
  std::uint16_t widest = 0;
  for (std::size_t i = 0; i < count; ++i) {
    append16(hmtx, sub_metrics->advances[i]);
    append16(hmtx, static_cast<std::uint16_t>(sub_metrics->bearings[i]));
    widest = std::max(widest, sub_metrics->advances[i]);
  }
  tables["hmtx"] = std::move(hmtx);
  put16(tables["hhea"], 34, static_cast<std::uint16_t>(count));
  put16(tables["hhea"], 10, widest);
  // maxp: the glyph count, and the larger of each limit (the full font's glyphs are within its own).
  Bytes& m = tables["maxp"];
  put16(m, 4, static_cast<std::uint16_t>(count));
  if (const Bytes* fm = table(full, "maxp"); fm != nullptr && u32(m, 0) == 0x00010000U && m.size() >= 32 && fm->size() >= 32) {
    for (std::size_t at = 6; at <= 30; at += 2) {
      put16(m, at, std::max(u16(m, at).value_or(0), u16(*fm, at).value_or(0)));
    }
  }
  // head: long loca, a bounding box that holds the new glyphs.
  Bytes& h = tables["head"];
  put16(h, 50, 1);
  if (const Bytes* fh = table(full, "head"); fh != nullptr && fh->size() >= 54) {
    put16(h, 36, static_cast<std::uint16_t>(std::min(i16(h, 36).value_or(0), i16(*fh, 36).value_or(0))));
    put16(h, 38, static_cast<std::uint16_t>(std::min(i16(h, 38).value_or(0), i16(*fh, 38).value_or(0))));
    put16(h, 40, static_cast<std::uint16_t>(std::max(i16(h, 40).value_or(0), i16(*fh, 40).value_or(0))));
    put16(h, 42, static_cast<std::uint16_t>(std::max(i16(h, 42).value_or(0), i16(*fh, 42).value_or(0))));
  }
  // post: glyph names would no longer cover every glyph — none (version 3).
  if (auto post = tables.find("post"); post != tables.end() && post->second.size() >= 32) {
    post->second.resize(32);
    put32(post->second, 0, 0x00030000);
  }
  return out;
}

namespace {

// A cmap subtable (the offset of its format word in cmap) of formats 0 and 4: code → glyph.
std::map<std::uint32_t, std::uint16_t> subtable_map(const Bytes& cmap, std::uint32_t at) {
  std::map<std::uint32_t, std::uint16_t> out;
  const auto format = u16(cmap, at);
  if (format == 0) {
    for (std::uint32_t c = 0; c < 256; ++c) {
      if (at + 6 + c >= cmap.size()) {
        break;
      }
      if (const auto g = std::to_integer<std::uint16_t>(cmap[at + 6 + c]); g != 0) {
        out.emplace(c, g);
      }
    }
    return out;
  }
  if (format != 4) {
    return out;
  }
  const auto doubled = u16(cmap, at + 6);
  if (!doubled) {
    return out;
  }
  const std::size_t segments = *doubled / 2U;
  const std::size_t ends = at + 14;
  const std::size_t starts = ends + (segments * 2) + 2;
  const std::size_t deltas = starts + (segments * 2);
  const std::size_t ranges = deltas + (segments * 2);
  for (std::size_t s = 0; s < segments; ++s) {
    const auto end = u16(cmap, ends + (s * 2));
    const auto start = u16(cmap, starts + (s * 2));
    const auto delta = u16(cmap, deltas + (s * 2));
    const auto range = u16(cmap, ranges + (s * 2));
    if (!end || !start || !delta || !range || *end < *start) {
      break;
    }
    for (std::uint32_t c = *start; c <= *end && c != 0xFFFF; ++c) {
      std::uint16_t glyph = 0;
      if (*range == 0) {
        glyph = static_cast<std::uint16_t>(c + *delta);
      } else {
        const auto g = u16(cmap, ranges + (s * 2) + *range + (static_cast<std::size_t>(c - *start) * 2));
        if (!g) {
          break;
        }
        glyph = *g == 0 ? 0 : static_cast<std::uint16_t>(*g + *delta);
      }
      if (glyph != 0) {
        out.emplace(c, glyph);
      }
    }
  }
  return out;
}

// Where the (platform, encoding) subtable of a cmap is; nullopt when there is none.
std::optional<std::uint32_t> subtable_at(const Bytes& cmap, std::uint16_t platform, std::uint16_t encoding) {
  const auto count = u16(cmap, 2);
  for (std::size_t i = 0; count && i < *count; ++i) {
    const auto p = u16(cmap, 4 + (i * 8));
    const auto e = u16(cmap, 6 + (i * 8));
    const auto offset = u32(cmap, 8 + (i * 8));
    if (!p || !e || !offset) {
      break;
    }
    if (*p == platform && *e == encoding && *offset < cmap.size()) {
      return offset;
    }
  }
  return std::nullopt;
}

// A format 4 subtable mapping codes (below 0xFFFF) to glyphs.
std::optional<Bytes> format4(const std::map<std::uint32_t, std::uint16_t>& map) {
  struct Segment {
    std::uint16_t start;
    std::uint16_t end;
    std::uint16_t delta;
  };
  std::vector<Segment> segments;
  for (const auto& [c, g] : map) {
    if (c >= 0xFFFF) {
      return std::nullopt;
    }
    const auto code = static_cast<std::uint16_t>(c);
    const auto delta = static_cast<std::uint16_t>(g - code);
    if (!segments.empty() && segments.back().end + 1 == code && segments.back().delta == delta) {
      segments.back().end = code;
    } else {
      segments.push_back({code, code, delta});
    }
  }
  segments.push_back({0xFFFF, 0xFFFF, 1});
  const std::size_t n = segments.size();
  if (16 + (n * 8) > 0xFFFF) {
    return std::nullopt;
  }
  std::uint16_t power = 1;
  std::uint16_t selector = 0;
  while (static_cast<std::size_t>(power) * 2 <= n) {
    power = static_cast<std::uint16_t>(power * 2);
    ++selector;
  }
  Bytes out;
  append16(out, 4);
  append16(out, static_cast<std::uint16_t>(16 + (n * 8)));
  append16(out, 0);  // language
  append16(out, static_cast<std::uint16_t>(n * 2));
  append16(out, static_cast<std::uint16_t>(power * 2));
  append16(out, selector);
  append16(out, static_cast<std::uint16_t>((n * 2) - (static_cast<std::size_t>(power) * 2)));
  for (const Segment& s : segments) {
    append16(out, s.end);
  }
  append16(out, 0);
  for (const Segment& s : segments) {
    append16(out, s.start);
  }
  for (const Segment& s : segments) {
    append16(out, s.delta);
  }
  for (std::size_t i = 0; i < n; ++i) {
    append16(out, 0);
  }
  return out;
}

// A cmap of these subtables ((platform, encoding) → data), in order.
Bytes cmap_of(const std::vector<std::pair<std::pair<std::uint16_t, std::uint16_t>, Bytes>>& subtables) {
  Bytes out;
  append16(out, 0);
  append16(out, static_cast<std::uint16_t>(subtables.size()));
  std::uint32_t offset = 4 + static_cast<std::uint32_t>(subtables.size() * 8);
  for (const auto& [id, data] : subtables) {
    append16(out, id.first);
    append16(out, id.second);
    append32(out, offset);
    offset += static_cast<std::uint32_t>(data.size());
  }
  for (const auto& entry : subtables) {
    out.insert(out.end(), entry.second.begin(), entry.second.end());
  }
  return out;
}

}  // namespace

std::map<std::uint8_t, std::uint16_t> symbol_cmap(const Sfnt& font) {
  std::map<std::uint8_t, std::uint16_t> out;
  const Bytes* cmap = table(font, "cmap");
  if (cmap == nullptr) {
    return out;
  }
  if (const auto mac = subtable_at(*cmap, 1, 0)) {
    for (const auto& [c, g] : subtable_map(*cmap, *mac)) {
      if (c < 256) {
        out.emplace(static_cast<std::uint8_t>(c), g);
      }
    }
    return out;
  }
  if (const auto symbol = subtable_at(*cmap, 3, 0)) {
    for (const auto& [c, g] : subtable_map(*cmap, *symbol)) {
      if ((c & 0xFF00U) == 0xF000U || c < 256) {
        out.emplace(static_cast<std::uint8_t>(c & 0xFFU), g);
      }
    }
  }
  return out;
}

std::optional<std::vector<std::byte>> symbol_cmap_with(const Sfnt& font, const std::map<std::uint8_t, std::uint16_t>& added) {
  const Bytes* cmap = table(font, "cmap");
  if (cmap == nullptr) {
    return std::nullopt;
  }
  const bool mac = subtable_at(*cmap, 1, 0).has_value();
  const bool symbol = subtable_at(*cmap, 3, 0).has_value();
  if (!mac && !symbol) {
    return std::nullopt;
  }
  auto map = symbol_cmap(font);
  for (const auto& [c, g] : added) {
    map[c] = g;
  }
  std::vector<std::pair<std::pair<std::uint16_t, std::uint16_t>, Bytes>> subtables;
  if (mac) {
    Bytes zero;
    append16(zero, 0);
    append16(zero, 262);
    append16(zero, 0);
    for (std::uint32_t c = 0; c < 256; ++c) {
      const auto found = map.find(static_cast<std::uint8_t>(c));
      zero.push_back(static_cast<std::byte>(found != map.end() && found->second < 256 ? found->second : 0));
      if (found != map.end() && found->second >= 256) {
        return std::nullopt;  // format 0 holds glyph indices below 256 only
      }
    }
    subtables.emplace_back(std::pair<std::uint16_t, std::uint16_t>{1, 0}, std::move(zero));
  }
  if (symbol) {
    std::map<std::uint32_t, std::uint16_t> high;
    for (const auto& [c, g] : map) {
      high.emplace(0xF000U + c, g);
    }
    auto four = format4(high);
    if (!four) {
      return std::nullopt;
    }
    subtables.emplace_back(std::pair<std::uint16_t, std::uint16_t>{3, 0}, std::move(*four));
  }
  return cmap_of(subtables);
}

std::optional<std::vector<std::byte>> cmap_with(const Sfnt& font, const std::map<char32_t, std::uint16_t>& added) {
  auto map = unicode_cmap(font);
  if (map.empty()) {
    return std::nullopt;
  }
  for (const auto& [c, g] : added) {
    map[c] = g;
  }
  std::map<std::uint32_t, std::uint16_t> codes;
  for (const auto& [c, g] : map) {
    if (c > 0xFFFF) {
      return std::nullopt;
    }
    if (c < 0xFFFF) {
      codes.emplace(static_cast<std::uint32_t>(c), g);
    }
  }
  auto four = format4(codes);
  if (!four) {
    return std::nullopt;
  }
  return cmap_of({{{3, 1}, *four}});
}

}  // namespace capriole::font::detail
