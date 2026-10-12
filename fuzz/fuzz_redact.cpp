// Fuzz target for redaction through the C API (capi/README.md), as the apps use it: the input is a PDF; a
// document that needed repairs is saved fully and read back first, as the apps do. Its first page is
// redacted in a few areas (fixed: across the page, a corner, the middle), a redaction mark is added and
// applied, then the document is saved fully and read back. An incremental save must be refused.
// The seed corpus is fuzz/corpus/redact/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; a redacted document is not
// saved incrementally.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "capriole/capriole.h"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

capriole_document* reopen(capriole_document* document, int incremental) {
  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  capriole_document* reopened = nullptr;
  if (capriole_document_save(document, incremental, nullptr, &data, &size) == CAPRIOLE_OK) {
    (void)capriole_document_open(data, size, nullptr, 1, &reopened);
    capriole_free(data);
  }
  return reopened;
}

constexpr std::array<const char*, 3> kRedactions = {
    R"({"areas":[[0,100,600,140]]})",
    R"({"areas":[[0,0,80,80],[500,700,620,800]],"fill":null})",
    R"({"areas":[[200,300,400,500]],"fill":[1,0,0]})",
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  capriole_document* document = nullptr;
  if (capriole_document_open(data, size, nullptr, 1, &document) != CAPRIOLE_OK) {
    return 0;
  }
  char* summary = nullptr;
  if (capriole_document_summary_json(document, &summary) == CAPRIOLE_OK) {
    const bool repaired = std::string_view(summary).find(R"("repairs":[])") == std::string_view::npos;
    capriole_free(summary);
    if (repaired) {
      capriole_document* sound = reopen(document, 0);
      capriole_document_close(document);
      document = sound;
    }
  }
  if (document == nullptr) {
    return 0;
  }
  bool redacted = false;
  for (const char* spec : kRedactions) {
    char* json = nullptr;
    if (capriole_document_page_redact(document, 0, spec, &json) == CAPRIOLE_OK) {
      redacted = true;
      check(json != nullptr && json[0] == '{');
      capriole_free(json);
    }
  }
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  if (capriole_document_annotation_add(document, 0, R"({"kind":"redact","rect":[50,50,150,90],"fill":[0,0,0]})", &number, &generation) == CAPRIOLE_OK) {
    char* json = nullptr;
    if (capriole_document_page_apply_redactions(document, 0, &json) == CAPRIOLE_OK) {
      redacted = true;
      capriole_free(json);
    }
  }
  if (redacted) {
    std::uint8_t* out = nullptr;
    std::size_t out_size = 0;
    check(capriole_document_save(document, 1, nullptr, &out, &out_size) != CAPRIOLE_OK);  // incremental: refused
  }
  capriole_document* reopened = reopen(document, 0);
  if (reopened != nullptr) {
    capriole_document_close(reopened);
  }
  capriole_document_close(document);
  return 0;
}
