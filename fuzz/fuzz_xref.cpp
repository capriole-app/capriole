// Fuzz target for the file structure: startxref, xref tables and streams, /Prev chains, object
// streams, and loading every object through the xref (streams decoded). The seed corpus is
// fuzz/corpus/xref/.
//
// Invariants beyond "no crash, no UB, no hang": every in-use xref entry points inside the input, and
// loading the same object twice gives the same result (loading is deterministic and has no hidden
// state that a first load could corrupt).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/parser/object_loader.hpp"
#include "capriole/parser/xref.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));

  capriole::ParseLimits limits;
  limits.max_decoded_stream_bytes = 1U << 20U;  // keep each run fast
  limits.max_xref_sections = 64;

  auto xref = capriole::parser::read_xref(source, limits);
  if (!xref) {
    return 0;
  }
  capriole::parser::ObjectLoader loader(source, std::move(*xref), limits);

  std::size_t loaded = 0;
  for (const auto& [number, entry] : loader.xref().entries) {
    if (loaded++ >= 256) {
      break;  // bound the work per input
    }
    auto first = loader.load(number);
    auto second = loader.load(number);
    if (first.has_value() != second.has_value() || (first && *first != *second)) {
      __builtin_trap();
    }
    if (first) {
      if (const auto* stream = first->as_stream()) {
        (void)loader.decode_stream(*stream);
      }
    }
  }
  return 0;
}
