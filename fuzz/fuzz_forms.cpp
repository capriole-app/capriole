// Fuzz target for form filling through the C API (capi/README.md), as the apps use it: the input is a
// PDF; a document that needed repairs is saved fully and read back first, as the apps do. Its fields are
// listed, and every object the list names (fields and widgets alike) is filled with each of a few values
// — text with characters beyond ASCII and a line break, several strings, null, check box states, an
// empty string —, then the document is saved fully and incrementally and read back.
// The seed corpus is fuzz/corpus/forms/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; the list parses as what the
// C API promises (it starts as an array).

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include "capriole/capriole.h"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

std::string list(capriole_document* document) {
  char* json = nullptr;
  if (capriole_document_form_fields_json(document, &json) != CAPRIOLE_OK) {
    return {};
  }
  std::string out(json);
  capriole_free(json);
  check(!out.empty() && out.front() == '[' && out.back() == ']');
  return out;
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

// The object numbers a list names ("ref":[number,generation]).
std::set<std::uint32_t> numbers(const std::string& json) {
  std::set<std::uint32_t> out;
  constexpr std::string_view kRef = R"("ref":[)";
  for (std::size_t at = json.find(kRef); at != std::string::npos && out.size() < 64; at = json.find(kRef, at + 1)) {
    std::uint64_t n = 0;
    for (std::size_t i = at + kRef.size(); i < json.size() && json[i] >= '0' && json[i] <= '9' && n <= 0xFFFFFFFFULL; ++i) {
      n = (n * 10) + static_cast<std::uint64_t>(json[i] - '0');
    }
    if (n <= 0xFFFFFFFFULL) {
      out.insert(static_cast<std::uint32_t>(n));
    }
  }
  return out;
}

constexpr std::array<const char*, 6> kValues = {
    R"("Testo è € 1\n2 — ok")", R"(["a","b"])", "null", R"("Yes")", R"("Off")", R"("")",
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
  for (const std::uint32_t number : numbers(list(document))) {
    for (const char* value : kValues) {
      (void)capriole_document_form_field_fill(document, number, 0, value);
    }
  }
  (void)list(document);
  for (const int incremental : {0, 1}) {
    capriole_document* reopened = reopen(document, incremental);
    if (reopened != nullptr) {
      (void)list(reopened);
      capriole_document_close(reopened);
    }
  }
  capriole_document_close(document);
  return 0;
}
