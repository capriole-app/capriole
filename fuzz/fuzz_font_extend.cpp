// Fuzz target for extending TrueType subsets (font/sfnt.hpp, edit/font_library.hpp; PLAN.md phase 9c):
// the input is two fonts — a subset and a full font — split at the offset its first four bytes give (big
// endian, modulo its length). Both are read, the full one is added to a library (its faces, names,
// permissions, outlines), the subset is extended with a few of the full font's glyphs, written, read back,
// and given a cmap with those characters. The seed corpus is fuzz/corpus/font_extend/.
//
// Invariants beyond "no crash, no UB, no hang": a font extended is written as a font that reads back with
// the glyph count it was given.

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "capriole/edit/font_library.hpp"
#include "font/sfnt.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) {
    return 0;
  }
  const auto* bytes = reinterpret_cast<const std::byte*>(data);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes
  const std::span<const std::byte> input(bytes + 4, size - 4);   // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const std::size_t split =
      input.empty()
          ? 0
          : ((static_cast<std::size_t>(data[0]) << 24U) | (static_cast<std::size_t>(data[1]) << 16U) | (static_cast<std::size_t>(data[2]) << 8U) | data[3]) %
                input.size();
  const auto subset_bytes = input.first(split);
  const auto full_bytes = input.subspan(split);

  capriole::edit::FontLibrary library;
  (void)library.add(std::vector<std::byte>(full_bytes.begin(), full_bytes.end()));
  (void)library.find("CprSquare");

  auto subset = capriole::font::detail::read_sfnt(subset_bytes);
  auto full = capriole::font::detail::read_sfnt(full_bytes);
  if (!subset || !full) {
    return 0;
  }
  (void)capriole::font::detail::postscript_name(*full);
  const auto cmap = capriole::font::detail::unicode_cmap(*full);
  std::vector<std::uint16_t> glyphs;
  std::map<char32_t, std::uint16_t> wanted;
  for (const auto& [c, g] : cmap) {
    if (glyphs.size() >= 4) {
      break;
    }
    glyphs.push_back(g);
    wanted.emplace(c, g);
  }
  auto extended = capriole::font::detail::extend(*subset, *full, glyphs);
  if (!extended) {
    return 0;
  }
  const auto written = capriole::font::detail::write_sfnt(extended->font);
  auto again = capriole::font::detail::read_sfnt(written);
  check(again.has_value());
  check(capriole::font::detail::glyph_count(*again) == capriole::font::detail::glyph_count(extended->font));
  std::map<char32_t, std::uint16_t> added;
  for (const auto& [c, g] : wanted) {
    added.emplace(c, extended->glyphs.at(g));
  }
  (void)capriole::font::detail::cmap_with(*again, added);
  for (const auto& [from, to] : extended->glyphs) {
    (void)capriole::font::detail::glyph_data(*again, to);
  }
  return 0;
}
