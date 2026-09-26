// Fuzz target for the header parser. The seed corpus is fuzz/corpus/header/.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/parser/header.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  const auto header = capriole::parser::find_header(source);
  if (header) {
    // Invariants the parser promises: the header lies inside the window and inside the input.
    if (header->offset > capriole::parser::kHeaderSearchWindow || header->offset >= size) {
      __builtin_trap();
    }
  }
  return 0;
}
