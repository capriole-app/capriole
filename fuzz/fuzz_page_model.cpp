// Fuzz target for the page model (layout/page_model): open the input as a document (repairs on) and build
// the model of its first pages. The seed corpus is fuzz/corpus/page_model/.
//
// Invariants beyond "no crash, no UB, no hang": every command the content draws is in an element (a text
// run in paragraphs only, any other in one exactly), every element's commands are drawn and its sources
// are invocations of the list, no character is in two paragraphs, columns hold body paragraphs, and boxes
// are ordered.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/layout/page_model.hpp"

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
  capriole::layout::ModelLimits limits;
  limits.interpreter.parse = options.limits;
  limits.interpreter.max_operations = 100'000;
  limits.interpreter.max_commands = 20'000;
  limits.interpreter.max_form_depth = 8;
  limits.text.max_chars = 20'000;
  limits.text.max_ordered_blocks = 64;
  for (std::uint32_t i = 0; i < pages->size() && i < 3; ++i) {
    auto model = capriole::layout::page_model(*document, i, nullptr, limits);
    if (!model) {
      continue;
    }
    const std::set<std::size_t> drawn(model->drawn.begin(), model->drawn.end());
    std::map<std::size_t, std::size_t> others;  // elements not paragraphs, of each command
    std::set<std::size_t> in_paragraphs;
    for (const auto& e : model->elements) {
      check(e.box.x0 <= e.box.x1 && e.box.y0 <= e.box.y1);
      for (const std::size_t c : e.commands) {
        check(drawn.contains(c));
        if (e.kind == capriole::layout::Kind::kParagraph) {
          in_paragraphs.insert(c);
        } else {
          ++others[c];
        }
      }
      for (const auto& s : e.sources) {
        check(s.invocation < model->list.invocations.size());
      }
      if (e.kind == capriole::layout::Kind::kParagraph) {
        check(e.paragraph < model->paragraphs.size());
      }
      if (e.column) {
        check(*e.column < model->columns.size());
      }
    }
    for (const std::size_t c : drawn) {
      const auto other = others.find(c);
      const std::size_t n = other != others.end() ? other->second : 0;
      check(in_paragraphs.contains(c) ? n == 0 : n == 1);
    }
    std::set<std::size_t> chars;
    for (const auto& p : model->paragraphs) {
      for (const std::size_t c : p.chars) {
        check(c < model->text.chars.size() && chars.insert(c).second);
      }
    }
    for (const auto& column : model->columns) {
      for (const std::size_t e : column.elements) {
        check(e < model->elements.size() && model->elements[e].kind == capriole::layout::Kind::kParagraph &&
              model->elements[e].zone == capriole::layout::Zone::kBody);
      }
    }
  }
  return 0;
}
