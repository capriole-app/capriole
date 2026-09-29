// Fuzz target for font programs (font/program: FreeType and our outline conversion): the input is a font
// file of any kind a PDF embeds (TrueType/OpenType, Type 1, CFF bare or CID-keyed). The seed corpus is
// fuzz/corpus/font/ (tests/data/fonts).
//
// Invariants beyond "no crash, no UB, no hang, no leak": outlines and advances are finite.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "font/program.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

using capriole::font::detail::Charmap;
using capriole::font::detail::FontProgram;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  auto bytes = std::make_shared<std::vector<std::byte>>(size);
  for (std::size_t i = 0; i < size; ++i) {
    (*bytes)[i] = static_cast<std::byte>(data[i]);
  }
  auto program = FontProgram::open(std::move(bytes), {.max_memory = std::size_t{64} << 20U});
  if (!program) {
    return 0;
  }
  const FontProgram& p = **program;
  (void)p.is_truetype();
  (void)p.is_cid_keyed();
  constexpr std::uint32_t kGlyphs = 64;
  for (std::uint32_t glyph = 0; glyph < kGlyphs; ++glyph) {
    if (const auto outline = p.outline(glyph)) {
      outline->for_each([](capriole::graphics::SegmentKind kind, const capriole::graphics::Point* points) {
        for (std::size_t i = 0; i < capriole::graphics::Path::points_of(kind); ++i) {
          check(std::isfinite(points[i].x) && std::isfinite(points[i].y));
        }
      });
    }
    if (const auto advance = p.advance(glyph)) {
      check(std::isfinite(*advance));
    }
  }
  for (const char* name : {"A", "space", ".notdef", "uni4E00"}) {
    (void)p.glyph_named(name);
  }
  for (const Charmap charmap : {Charmap::kUnicode, Charmap::kMicrosoftSymbol, Charmap::kMacRoman, Charmap::kBuiltin}) {
    for (const std::uint32_t code : {0x20U, 0x41U, 0xF041U, 0x4E00U}) {
      (void)p.glyph_for(charmap, code);
    }
  }
  return 0;
}
