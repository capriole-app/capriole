// Fuzz target for the conversion to PDF/A through the C API (capi/README.md), as the apps use it: the
// input is a PDF (a document that needed repairs is written anew and read back by the C API first); it
// is converted (PDF/A-2b, or -3b with embedded files), and the file written is read back and converted
// again. The seed corpus is fuzz/corpus/pdfa/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; a converted file opens.

#include <cstddef>
#include <cstdint>

#include "capriole/capriole.h"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

constexpr const char* kSpec = R"({"part":0,"date":"D:20261004150000Z"})";

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  capriole_document* document = nullptr;
  if (capriole_document_open(data, size, nullptr, 1, &document) != CAPRIOLE_OK) {
    return 0;
  }
  std::uint8_t* out = nullptr;
  std::size_t out_size = 0;
  char* json = nullptr;
  if (capriole_document_convert_pdfa(document, kSpec, &out, &out_size, &json) == CAPRIOLE_OK) {
    capriole_free(json);
    capriole_document* again = nullptr;
    check(capriole_document_open(out, out_size, nullptr, 0, &again) == CAPRIOLE_OK);
    capriole_free(out);
    out = nullptr;
    if (capriole_document_convert_pdfa(again, kSpec, &out, &out_size, &json) == CAPRIOLE_OK) {
      capriole_free(json);
    }
    capriole_document_close(again);
  }
  capriole_free(out);
  capriole_document_close(document);
  return 0;
}
