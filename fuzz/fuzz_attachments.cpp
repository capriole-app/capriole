// Fuzz target for attachments and Factur-X through the C API (capi/README.md), as the apps use them.
// Two modes, by the input's first bytes:
// - a PDF ("%PDF"): a document that needed repairs is saved fully and read back first, as the apps do;
//   its attachments are listed and read, a file is attached, an invoice embedded as Factur-X, one of
//   the attachments removed; then the document is saved fully and incrementally and read back.
// - anything else is an invoice's XML: embedded as Factur-X in a small fixed document, which is read
//   back.
// The seed corpus is fuzz/corpus/attachments/.
//
// Invariants beyond "no crash, no UB, no hang": every call returns a status; JSON results start as
// promised (an array; null or an object).

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
  if (capriole_document_attachments_json(document, &json) != CAPRIOLE_OK) {
    return {};
  }
  std::string out(json);
  capriole_free(json);
  check(!out.empty() && out.front() == '[' && out.back() == ']');
  return out;
}

void facturx(capriole_document* document) {
  char* json = nullptr;
  if (capriole_document_facturx_json(document, &json) == CAPRIOLE_OK) {
    const std::string_view out(json);
    check(out == "null" || (out.front() == '{' && out.back() == '}'));
    capriole_free(json);
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

// The object numbers a list names ("ref":[number,generation]).
std::set<std::uint32_t> numbers(const std::string& json) {
  std::set<std::uint32_t> out;
  constexpr std::string_view kRef = R"("ref":[)";
  for (std::size_t at = json.find(kRef); at != std::string::npos && out.size() < 16; at = json.find(kRef, at + 1)) {
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

constexpr std::string_view kInvoice =
    R"(<rsm:CrossIndustryInvoice xmlns:rsm="urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100"><rsm:ExchangedDocumentContext>)"
    R"(<ram:GuidelineSpecifiedDocumentContextParameter><ram:ID>urn:cen.eu:en16931:2017</ram:ID>)"
    R"(</ram:GuidelineSpecifiedDocumentContextParameter></rsm:ExchangedDocumentContext></rsm:CrossIndustryInvoice>)";

const std::uint8_t* bytes_of(std::string_view text) {
  return reinterpret_cast<const std::uint8_t*>(text.data());
}  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

void edit(capriole_document* document) {
  const std::string listed = list(document);
  for (const std::uint32_t number : numbers(listed)) {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
    if (capriole_document_attachment_data(document, number, 0, &data, &size, nullptr) == CAPRIOLE_OK) {
      capriole_free(data);
    }
  }
  facturx(document);
  std::uint32_t number = 0;
  std::uint32_t generation = 0;
  constexpr std::string_view kData = "dati dell'allegato";
  if (capriole_document_attachment_add(document, R"({"fileName":"allegato è.txt","mimeType":"text/plain","relationship":"Supplement"})", bytes_of(kData),
                                       kData.size(), &number, &generation) == CAPRIOLE_OK) {
    check(list(document).find(std::to_string(number)) != std::string::npos);
  }
  char* json = nullptr;
  if (capriole_document_facturx_embed(document, bytes_of(kInvoice), kInvoice.size(), "D:20261003120000Z", &json) == CAPRIOLE_OK) {
    capriole_free(json);
  }
  facturx(document);
  for (const std::uint32_t n : numbers(list(document))) {
    (void)capriole_document_attachment_remove(document, n, 0);
    break;
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
    edit(document);
    for (const int incremental : {0, 1}) {
      capriole_document* reopened = reopen(document, incremental);
      if (reopened != nullptr) {
        (void)list(reopened);
        facturx(reopened);
        capriole_document_close(reopened);
      }
    }
    capriole_document_close(document);
    return 0;
  }
  // An invoice's XML, in a small fixed document.
  static const std::string kPage = [] {
    std::string pdf =
        "%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n"
        "3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>\nendobj\n";
    const std::array<std::size_t, 3> offsets = {pdf.find("1 0 obj"), pdf.find("2 0 obj"), pdf.find("3 0 obj")};
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 4\n0000000000 65535 f \n";
    for (const std::size_t o : offsets) {
      const std::string line = std::to_string(o);
      pdf += std::string(10 - line.size(), '0') + line + " 00000 n \n";
    }
    pdf += "trailer\n<</Size 4/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
  }();
  capriole_document* document = nullptr;
  check(capriole_document_open(bytes_of(kPage), kPage.size(), nullptr, 1, &document) == CAPRIOLE_OK);
  char* json = nullptr;
  if (capriole_document_facturx_embed(document, data, size, "D:20261003120000Z", &json) == CAPRIOLE_OK) {
    capriole_free(json);
    capriole_document* reopened = reopen(document, 1);
    if (reopened != nullptr) {
      facturx(reopened);
      (void)list(reopened);
      capriole_document_close(reopened);
    }
  }
  capriole_document_close(document);
  return 0;
}
