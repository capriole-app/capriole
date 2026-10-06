// Fuzz target for editing a paragraph through the C API (capi/README.md), as the apps use it: the input is
// a PDF (a document that needed repairs is saved fully and read back first, as the apps do); on its first
// page the paragraphs in a few areas are written anew — longer (shrinking allowed or not), shorter, with a
// line break, deleted — and the document is saved and read back. The seed corpus is
// fuzz/corpus/paragraph_edit/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; an edited document saves and
// opens.

#include <array>
#include <cstddef>
#include <cstdint>

#include "capriole/capriole.h"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

capriole_document* reopen(capriole_document* document) {
  std::uint8_t* data = nullptr;
  std::size_t size = 0;
  capriole_document* reopened = nullptr;
  if (capriole_document_save(document, 0, nullptr, &data, &size) == CAPRIOLE_OK) {
    (void)capriole_document_open(data, size, nullptr, 1, &reopened);
    capriole_free(data);
  }
  return reopened;
}

constexpr std::array<const char*, 4> kEdits = {
    R"({"area":[0,60,600,200],"text":"Un testo pi\u00f9 lungo di quello che c'era, che deve andare a capo pi\u00f9 volte dentro il paragrafo \u20ac","shrink":true})",
    R"({"area":[0,200,600,400],"text":"Breve.\nDue righe.","shrink":false})",
    R"({"area":[0,0,300,120],"text":"Unaparolalunghissimachenonstainnessunarigaedeveesserespezzataneipunticheservono"})",
    R"({"area":[100,400,500,700],"text":""})",
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  capriole_document* opened = nullptr;
  if (capriole_document_open(data, size, nullptr, 1, &opened) != CAPRIOLE_OK) {
    return 0;
  }
  capriole_document* document = reopen(opened);
  capriole_document_close(opened);
  if (document == nullptr) {
    return 0;
  }
  bool edited = false;
  for (const char* spec : kEdits) {
    char* json = nullptr;
    if (capriole_document_page_replace_paragraph(document, 0, spec, &json) == CAPRIOLE_OK) {
      capriole_free(json);
      edited = true;
    }
  }
  if (edited) {
    std::uint8_t* out = nullptr;
    std::size_t out_size = 0;
    check(capriole_document_save(document, 1, nullptr, &out, &out_size) == CAPRIOLE_OK);
    capriole_document* again = nullptr;
    check(capriole_document_open(out, out_size, nullptr, 1, &again) == CAPRIOLE_OK);
    capriole_document_close(again);
    capriole_free(out);
  }
  capriole_document_close(document);
  return 0;
}
