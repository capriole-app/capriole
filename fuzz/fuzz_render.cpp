// Fuzz target for the whole drawing path: open the input as a document (repairs on), interpret its
// first pages and write their SVG preview. The seed corpus is fuzz/corpus/render/.
//
// Invariants beyond "no crash, no UB, no hang": the display list respects the interpreter's limits,
// every Save has its Restore, and the SVG is ASCII.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/svg.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::OpenOptions options;
  options.limits.max_nesting_depth = 32;
  auto document = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(bytes.begin(), bytes.end())), options);
  if (!document) {
    return 0;
  }
  auto pages = document->page_refs();
  if (!pages) {
    return 0;
  }
  capriole::content::InterpreterLimits limits;
  limits.parse = options.limits;
  limits.max_operations = 100'000;
  limits.max_commands = 20'000;
  limits.max_form_depth = 8;
  for (std::size_t i = 0; i < pages->size() && i < 3; ++i) {
    auto list = capriole::content::interpret_page(*document, (*pages)[i], limits);
    if (!list) {
      continue;
    }
    check(list->commands.size() <= limits.max_commands);
    std::size_t saves = 0;
    std::size_t restores = 0;
    for (const auto& command : list->commands) {
      saves += std::holds_alternative<capriole::graphics::Save>(command) ? 1 : 0;
      restores += std::holds_alternative<capriole::graphics::Restore>(command) ? 1 : 0;
    }
    // Balanced, unless the command limit cut the list short.
    check(saves == restores || list->commands.size() == limits.max_commands);
    const std::string svg = capriole::graphics::to_svg(*list);
    for (const char c : svg) {
      check(static_cast<unsigned char>(c) < 0x80);
    }
  }
  return 0;
}
