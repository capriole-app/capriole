// Fuzz target for editing objects of a page (edit/object_edit): open the input as a document (repairs on),
// and on its first page move, resize and delete elements of its model, by turns, as the bytes say; the
// document is written after each edit and read again. The seed corpus is fuzz/corpus/object_edit/.
//
// Invariants beyond "no crash, no UB, no hang": an edit done leaves a page whose model can be built and a
// document that can be written.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/object_edit.hpp"
#include "capriole/layout/page_model.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"

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
  // The choices: the last bytes of the input (element, action, amount), three edits at most.
  for (std::size_t round = 0; round < 3 && size >= 3 * (round + 1); ++round) {
    const std::uint8_t* choice = data + size - (3 * (round + 1));
    auto model = capriole::layout::page_model(*document, 0);
    if (!model || model->elements.empty()) {
      return 0;
    }
    capriole::edit::ObjectEdit edit;
    edit.element = choice[0] % model->elements.size();
    edit.action = static_cast<capriole::edit::ObjectAction>(choice[1] % 3);
    const double amount = static_cast<double>(static_cast<std::int8_t>(choice[2]));
    const auto& from = model->elements[edit.element].box;
    edit.by = {amount, -amount / 2};
    edit.box = {from.x0, from.y0, from.x0 + ((from.x1 - from.x0) * (1 + (amount / 256))), from.y0 + ((from.y1 - from.y0) * (1 - (amount / 256)))};
    auto result = capriole::edit::edit_object(*document, 0, edit);
    if (!result) {
      continue;
    }
    auto after = capriole::layout::page_model(*document, 0);
    check(after.has_value());
    capriole::writer::MemorySink sink;
    check(capriole::writer::save_full(*document, sink).has_value());
  }
  return 0;
}
