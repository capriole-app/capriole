// Fuzz target for the signatures of a document through the C API (capi/README.md), as the apps use it:
// the input is a PDF; its signatures are listed and verified (with the test CA as trusted root), then it
// is signed (an invisible signature, then a visible one with room for a time stamp) and the signed file's
// signatures listed again. The seed corpus is fuzz/corpus/signatures/ (signed files).
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; a signature just made
// verifies.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "capriole/capriole.h"

// The test key (tests/data/signing/ec.p12, password "capriole") and CA (ca.pem), built in by
// fuzz/CMakeLists.txt (cmake/EmbedFiles.cmake).
namespace capriole::font::detail {
std::span<const unsigned char> fuzz_signing_file(std::size_t index);
}  // namespace capriole::font::detail

namespace {

const std::span<const unsigned char> kKey = capriole::font::detail::fuzz_signing_file(0);
const std::span<const unsigned char> kCa = capriole::font::detail::fuzz_signing_file(1);

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

void list(capriole_document* document) {
  char* json = nullptr;
  if (capriole_document_signatures_json(document, kCa.data(), kCa.size(), &json) == CAPRIOLE_OK) {
    capriole_free(json);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  capriole_document* document = nullptr;
  if (capriole_document_open(data, size, nullptr, 1, &document) != CAPRIOLE_OK) {
    return 0;
  }
  list(document);
  for (const char* spec : {R"({"date":"D:20261004150000Z"})", R"({"date":"D:20261004150000Z","rect":[10,10,200,60],"timestamp":true})"}) {
    capriole_signing* signing = nullptr;
    if (capriole_document_sign_begin(document, spec, kKey.data(), kKey.size(), "capriole", &signing) != CAPRIOLE_OK) {
      continue;
    }
    std::uint8_t* out = nullptr;
    std::size_t out_size = 0;
    if (capriole_document_sign_finish(document, signing, nullptr, 0, &out, &out_size) == CAPRIOLE_OK) {
      capriole_free(out);
      char* json = nullptr;
      check(capriole_document_signatures_json(document, nullptr, 0, &json) == CAPRIOLE_OK);
      // The last signature is the one just made: it verifies.
      const std::string_view text(json);
      check(text.rfind(R"("integrity":"valid")") != std::string_view::npos && text.rfind(R"("integrity":"valid")") > text.rfind(R"("field":)"));
      capriole_free(json);
    }
    capriole_signing_free(signing);
  }
  capriole_document_close(document);
  return 0;
}
