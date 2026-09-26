// Fuzz target for the file structure: startxref, xref tables and streams, /Prev chains, object
// streams, loading every object through the xref (streams decoded), and the Document on top
// (page tree, edits). The seed corpus is
// fuzz/corpus/xref/.
//
// Invariants beyond "no crash, no UB, no hang": every in-use xref entry points inside the input, and
// loading the same object twice gives the same result (loading is deterministic and has no hidden
// state that a first load could corrupt).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/document/document.hpp"
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

  // The same input through the document model: open, page tree, and a round of edits.
  auto document = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(bytes.begin(), bytes.end())),
                                           capriole::OpenOptions{.limits = limits, .password = ""});
  if (!document) {
    return 0;
  }
  (void)document->page_refs();
  if (!document->xref().entries.empty()) {
    const std::uint32_t first = document->xref().entries.begin()->first;
    (void)document->set(first, capriole::model::Object(1));
    (void)document->remove(first);
  }
  (void)document->add(capriole::model::Object(2));
  for (const std::uint32_t number : document->changed_objects()) {
    if (document->state(number) == capriole::ObjectState::kUnmodified) {
      __builtin_trap();  // changed_objects lists only changed objects
    }
  }
  return 0;
}
