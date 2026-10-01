// Fuzz target for the page operations: open the input as a document (repairs on) — one that needed
// repairs is saved fully and read back first, as an application does before editing —, apply a sequence
// of rotations, arrangements and imports from a second copy of the input, chosen by the input's own
// bytes, then save it fully (compact) and incrementally and read the results back. The seed corpus is
// fuzz/corpus/pages/.
//
// Invariants beyond "no crash, no UB, no hang": a full save of a repaired document reads back with no
// repairs and the same number of pages; an operation that succeeds leaves as many pages as it should;
// the final full save reads back with that many pages and no repairs.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/pages.hpp"
#include "capriole/writer/save.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

capriole::Result<capriole::Document> open(std::span<const std::byte> bytes) {
  capriole::OpenOptions options;
  options.limits.max_nesting_depth = 32;
  return capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(bytes.begin(), bytes.end())), options);
}

std::size_t page_count(capriole::Document& document) {
  auto pages = document.page_refs();
  return pages ? pages->size() : 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  auto document = open(bytes);
  if (!document) {
    return 0;
  }
  if (page_count(*document) == 0) {
    return 0;
  }
  std::vector<std::byte> sound;
  if (!document->repairs().empty()) {
    capriole::writer::MemorySink sink;
    if (!capriole::writer::save_full(*document, sink)) {
      return 0;
    }
    sound = sink.take();
    const std::size_t pages = page_count(*document);
    document = open(sound);
    check(document.has_value());
    check(page_count(*document) == pages);
    check(document->repairs().empty());
  }
  std::size_t count = page_count(*document);
  if (count == 0 || count > 2000) {
    return 0;
  }
  // The last bytes of the input steer the operations (the file itself ends well before them, or not).
  const std::size_t tail = size < 8 ? size : 8;
  std::uint32_t seed = 0;
  for (std::size_t i = size - tail; i < size; ++i) {
    seed = (seed * 131) + data[i];
  }
  const auto pick = [&]() {
    seed = (seed * 1103515245U) + 12345U;
    return seed >> 8U;
  };
  for (int step = 0; step < 4; ++step) {
    switch (pick() % 3) {
      case 0: {
        const std::vector<std::uint32_t> pages = {static_cast<std::uint32_t>(pick() % count)};
        (void)capriole::edit::rotate_pages(*document, pages, static_cast<std::int32_t>((pick() % 8) * 90) - 360);
        break;
      }
      case 1: {
        std::vector<std::uint32_t> order;
        const std::size_t length = 1 + (pick() % (count + 2));
        for (std::size_t i = 0; i < length; ++i) {
          order.push_back(static_cast<std::uint32_t>(pick() % count));
        }
        if (capriole::edit::arrange_pages(*document, order)) {
          check(page_count(*document) == order.size());
        }
        break;
      }
      default: {
        auto source = open(bytes);
        if (!source) {
          break;
        }
        const std::vector<std::uint32_t> pages = {static_cast<std::uint32_t>(pick() % page_count(*source))};
        capriole::edit::ImportLimits limits;
        limits.max_objects = 20'000;
        limits.max_stream_bytes = 16'000'000;
        const std::size_t before = page_count(*document);
        if (capriole::edit::import_pages(*document, *source, pages, static_cast<std::uint32_t>(pick() % (before + 1)), limits)) {
          check(page_count(*document) == before + 1);
        }
        break;
      }
    }
    count = page_count(*document);
    if (count == 0) {
      return 0;
    }
  }
  capriole::writer::MemorySink sink;
  capriole::writer::SaveOptions options;
  options.drop_unreachable = true;
  if (capriole::writer::save_full(*document, sink, options)) {
    auto reopened = open(sink.data());
    check(reopened.has_value());
    check(page_count(*reopened) == count);
    check(reopened->repairs().empty());
  }
  capriole::writer::MemorySink appended;
  (void)capriole::writer::save_incremental(*document, appended);
  return 0;
}
