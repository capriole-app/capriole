// Fuzz target for CMaps (font/cmap): the input is CMap text — an embedded CMap, a ToUnicode CMap —
// whose usecmap names resolve to a small fixed CMap; the input's bytes are then split into codes by it.
// The seed corpus is fuzz/corpus/cmap/.
//
// Invariants beyond "no crash, no UB, no hang": every code is 1 to 4 bytes and advances through the
// string.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "font/cmap.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

using capriole::font::detail::CMap;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes as text
  const CMap::Resolver resolve = [](std::string_view, std::size_t depth) -> std::shared_ptr<const CMap> {
    auto parent = CMap::parse("1 begincodespacerange <00> <7F> endcodespacerange 1 begincidrange <20> <7E> 1 endcidrange", nullptr, {}, depth);
    return parent ? std::make_shared<const CMap>(std::move(*parent)) : nullptr;
  };
  auto cmap = CMap::parse(text, resolve, {.max_mappings = 100'000, .max_depth = 8, .max_text_units = 256});
  if (!cmap) {
    return 0;
  }
  for (std::size_t at = 0; at < text.size();) {
    const std::size_t before = at;
    const CMap::Code code = cmap->next(text, at);
    check(code.length >= 1 && code.length <= 4 && at > before && at <= text.size());
    (void)cmap->text(code.code, code.length);
  }
  return 0;
}
