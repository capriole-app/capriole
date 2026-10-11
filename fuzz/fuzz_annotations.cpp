// Fuzz target for annotation editing through the C API (capi/README.md), as the apps use it. Two modes,
// by the input's first bytes:
// - a PDF ("%PDF"): its first page gets one annotation of every kind (fixed specifications), each is
//   moved, recoloured, reworded and listed, then the document is saved fully and incrementally and read
//   back; a document that needed repairs is saved fully and read back first, as the apps do;
// - anything else is a JSON specification: added to a small fixed page, listed, changed with the same
//   text as a change, removed; then added again as a stamp's, the input's bytes its picture.
// The seed corpus is fuzz/corpus/annotations/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; JSON results parse as what
// the C API promises (they start as arrays); what is added is listed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
  if (capriole_document_page_annotations_json(document, 0, &json) != CAPRIOLE_OK) {
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

constexpr std::array<const char*, 10> kSpecs = {
    R"({"kind":"highlight","quads":[[10,10,60,22],[10,30,80,42]]})",
    R"({"kind":"underline","quads":[[10,50,60,62]],"color":[1,0,0]})",
    R"({"kind":"strikeout","quads":[[70,50,120,62]]})",
    R"({"kind":"squiggly","quads":[[70,10,1e6,22]]})",
    R"({"kind":"note","rect":[150,10,170,30],"contents":"nòta"})",
    R"({"kind":"ink","strokes":[[[10,70],[30,90],[50,70]],[]],"width":2})",
    R"({"kind":"square","rect":[60,70,90,95],"fill":[0.8,0.9,1],"width":2})",
    R"({"kind":"circle","rect":[100,70,130,95],"opacity":0.5})",
    R"({"kind":"line","from":[140,60],"to":[190,90]})",
    R"({"kind":"freetext","rect":[130,30,195,50],"contents":"testo libero lungo da andare a capo","fontSize":7})",
};

void edit_document(capriole_document* document) {
  for (const char* spec : kSpecs) {
    std::uint32_t number = 0;
    std::uint32_t generation = 0;
    if (capriole_document_annotation_add(document, 0, spec, &number, &generation) != CAPRIOLE_OK) {
      continue;
    }
    check(list(document).find(std::to_string(number)) != std::string::npos);
    (void)capriole_document_annotation_update(document, 0, number, generation, R"({"moveBy":[3,-2],"color":[0,0.5,0],"contents":"x","opacity":0.7})");
  }
  // A stamp: a 2 × 2 picture with transparency, moved and given another rectangle.
  constexpr std::array<std::uint8_t, 16> kPicture = {0, 0, 0, 255, 255, 0, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255};
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  if (capriole_document_stamp_add(document, 0, R"({"rect":[20,20,80,50],"author":"f"})", kPicture.data(), kPicture.size(), 2, 2, &number, &generation) ==
      CAPRIOLE_OK) {
    check(list(document).find(std::to_string(number)) != std::string::npos);
    (void)capriole_document_annotation_update(document, 0, number, generation, R"({"moveBy":[3,-2],"rect":[10,10,90,40]})");
  }
  (void)list(document);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  if (input.starts_with("%PDF")) {
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
    edit_document(document);
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
  // A JSON specification on a small page.
  static const std::string kPage = [] {
    const std::string body =
        "%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n"
        "3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Rotate 90>>\nendobj\n";
    std::string pdf = body;
    const std::array<std::size_t, 3> offsets = {pdf.find("1 0 obj"), pdf.find("2 0 obj"), pdf.find("3 0 obj")};
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 4\n0000000000 65535 f \n";
    for (const std::size_t o : offsets) {
      std::string line = std::to_string(o);
      pdf += std::string(10 - line.size(), '0') + line + " 00000 n \n";
    }
    pdf += "trailer\n<</Size 4/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
  }();
  capriole_document* document = nullptr;
  check(capriole_document_open(reinterpret_cast<const std::uint8_t*>(kPage.data()), kPage.size(), nullptr, 1, &document) ==
        CAPRIOLE_OK);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string json(input);
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  if (capriole_document_annotation_add(document, 0, json.c_str(), &number, &generation) == CAPRIOLE_OK) {
    check(list(document).find(std::to_string(number)) != std::string::npos);
    (void)capriole_document_annotation_update(document, 0, number, generation, json.c_str());
    capriole_document* reopened = reopen(document, 1);
    if (reopened != nullptr) {
      (void)list(reopened);
      capriole_document_close(reopened);
    }
    check(capriole_document_annotation_remove(document, 0, number, generation) == CAPRIOLE_OK);
  } else {
    (void)capriole_document_annotation_update(document, 0, 1, 0, json.c_str());
  }
  // The same specification as a stamp's, with the input's first bytes as its picture (as many whole
  // RGBA pixels as there are, in one row).
  const auto pixels = static_cast<std::uint32_t>(std::min<std::size_t>(size / 4, 256));
  if (pixels > 0 && capriole_document_stamp_add(document, 0, json.c_str(), data, std::size_t{pixels} * 4, pixels, 1, &number, &generation) == CAPRIOLE_OK) {
    check(list(document).find(std::to_string(number)) != std::string::npos);
    (void)capriole_document_annotation_update(document, 0, number, generation, json.c_str());
  }
  capriole_document_close(document);
  return 0;
}
