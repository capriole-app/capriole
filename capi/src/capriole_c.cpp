#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "capriole/capriole.h"
#include "capriole/content/content_parser.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/version_config.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/pkcs12.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/annotations.hpp"
#include "capriole/edit/attachments.hpp"
#include "capriole/edit/facturx.hpp"
#include "capriole/edit/forms.hpp"
#include "capriole/edit/pages.hpp"
#include "capriole/edit/pdfa.hpp"
#include "capriole/edit/redact.hpp"
#include "capriole/edit/sign.hpp"
#include "capriole/edit/text_edit.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/graphics/svg.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/parser/header.hpp"
#include "capriole/render/raster.hpp"
#include "capriole/repair/repair.hpp"
#include "capriole/signature/signatures.hpp"
#include "capriole/text/text.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "capriole/writer/serializer.hpp"
#include "json.hpp"
#include "json_read.hpp"

namespace {

// Per-thread last error, the errno-style contract documented in capriole.h.
thread_local std::string g_last_error;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// The resources hosts register (capriole_resource_add), shared by every document; and, per thread, what
// the last page interpreted did not find.
struct SharedResources {
  std::shared_mutex mutex;
  capriole::font::Resources resources;
};
SharedResources& shared_resources() {
  static SharedResources shared;
  return shared;
}
thread_local std::vector<std::string> g_missing_resources;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// A page interpreted with the registered resources; what it missed is remembered for this thread.
capriole::Result<capriole::graphics::DisplayList> interpret(capriole::Document& document, capriole::model::ObjectRef page,
                                                            capriole::content::Purpose purpose = capriole::content::Purpose::kDraw) {
  SharedResources& shared = shared_resources();
  const std::shared_lock lock(shared.mutex);
  auto list = capriole::content::interpret_page(document, page, {}, &shared.resources, purpose);
  g_missing_resources = list ? list->missing_resources : std::vector<std::string>{};
  return list;
}

capriole_status to_status(capriole::ErrorCode code) noexcept {
  switch (code) {
    case capriole::ErrorCode::kIo:
      return CAPRIOLE_ERROR_IO;
    case capriole::ErrorCode::kOutOfRange:
      return CAPRIOLE_ERROR_OUT_OF_RANGE;
    case capriole::ErrorCode::kUnexpectedEof:
      return CAPRIOLE_ERROR_UNEXPECTED_EOF;
    case capriole::ErrorCode::kSyntax:
      return CAPRIOLE_ERROR_SYNTAX;
    case capriole::ErrorCode::kUnsupported:
      return CAPRIOLE_ERROR_UNSUPPORTED;
    case capriole::ErrorCode::kLimitExceeded:
      return CAPRIOLE_ERROR_LIMIT_EXCEEDED;
    case capriole::ErrorCode::kCorruptData:
      return CAPRIOLE_ERROR_CORRUPT_DATA;
    case capriole::ErrorCode::kPasswordRequired:
      return CAPRIOLE_ERROR_PASSWORD_REQUIRED;
  }
  return CAPRIOLE_ERROR_INTERNAL;
}

capriole_status fail(capriole_status status, std::string message) {
  g_last_error = std::move(message);
  return status;
}

capriole_status fail(const capriole::Error& error) { return fail(to_status(error.code), error.message); }

// Copies bytes into a buffer the caller releases with capriole_free (malloc: the C side's allocator).
capriole_status hand_over(std::span<const std::byte> bytes, uint8_t** data, size_t* size) {
  *data = nullptr;
  *size = bytes.size();
  if (bytes.empty()) {
    return CAPRIOLE_OK;
  }
  // malloc, and ownership to the caller: the C boundary's contract (released by capriole_free).
  auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  if (buffer == nullptr) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "out of memory");
  }
  std::memcpy(buffer, bytes.data(), bytes.size());
  *data = buffer;
  return CAPRIOLE_OK;
}

capriole_status hand_over(const std::string& text, char** json) {
  auto* buffer = static_cast<char*>(std::malloc(text.size() + 1));  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  if (buffer == nullptr) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "out of memory");
  }
  std::memcpy(buffer, text.c_str(), text.size() + 1);
  *json = buffer;
  return CAPRIOLE_OK;
}

std::string_view method_name(capriole::crypto::CryptMethod method) {
  switch (method) {
    case capriole::crypto::CryptMethod::kIdentity:
      return "Identity";
    case capriole::crypto::CryptMethod::kRc4:
      return "RC4";
    case capriole::crypto::CryptMethod::kAesV2:
      return "AES-128";
    case capriole::crypto::CryptMethod::kAesV3:
      return "AES-256";
  }
  return "?";
}

std::string_view form_name(capriole::parser::XrefForm form) {
  switch (form) {
    case capriole::parser::XrefForm::kTable:
      return "table";
    case capriole::parser::XrefForm::kStream:
      return "stream";
    case capriole::parser::XrefForm::kHybrid:
      return "hybrid";
    case capriole::parser::XrefForm::kReconstructed:
      return "reconstructed";
  }
  return "?";
}

std::string_view state_name(capriole::ObjectState state) {
  switch (state) {
    case capriole::ObjectState::kUnmodified:
      return "unmodified";
    case capriole::ObjectState::kModified:
      return "modified";
    case capriole::ObjectState::kNew:
      return "new";
    case capriole::ObjectState::kDeleted:
      return "deleted";
  }
  return "?";
}

// The summary's letter for an entry: in use, compressed (in an object stream), free.
char entry_letter(capriole::parser::XrefEntryType type) {
  switch (type) {
    case capriole::parser::XrefEntryType::kInUse:
      return 'n';
    case capriole::parser::XrefEntryType::kCompressed:
      return 'c';
    case capriole::parser::XrefEntryType::kFree:
      return 'f';
  }
  return 'f';
}

// The PDF/A part the catalog's XMP declares (pdfaid:part, as an element or an attribute); 0 when none.
int pdfa_part(capriole::Document& document) {
  auto catalog = document.catalog();
  const capriole::model::Object* entry = catalog ? (*catalog)->find("Metadata") : nullptr;
  auto stream = entry != nullptr ? document.resolve(*entry) : capriole::Result<capriole::model::Object>(capriole::model::Object());
  if (!stream || stream->as_stream() == nullptr) {
    return 0;
  }
  auto data = document.decode_stream(*stream->as_stream());
  if (!data || data->size() > std::size_t{4} * 1024 * 1024) {
    return 0;
  }
  const std::string_view xmp = capriole::crypto::der::chars_of(*data);
  for (const std::string_view marker : {"pdfaid:part>", "pdfaid:part=\"", "pdfaid:part='"}) {
    if (const std::size_t at = xmp.find(marker); at != std::string_view::npos && at + marker.size() < xmp.size()) {
      const char digit = xmp[at + marker.size()];
      if (digit >= '1' && digit <= '4') {
        return digit - '0';
      }
    }
  }
  return 0;
}

std::string summary_json(capriole::Document& document) {
  using capriole::capi::append_json_object;
  using capriole::capi::append_json_string;
  // Pages first: reading the page tree can add repairs, which the summary lists.
  const auto pages = document.page_refs();

  std::string out = std::format(R"({{"version":"{}.{}","size":{})", document.header().major, document.header().minor, document.source().size());
  out += R"(,"encryption":)";
  if (const auto* security = document.security()) {
    out += std::format(R"({{"revision":{},"strings":"{}","streams":"{}","owner":{},"permissions":{},"encryptMetadata":{}}})", security->revision(),
                       method_name(security->string_method()), method_name(security->stream_method()), security->owner_authenticated() ? "true" : "false",
                       security->permissions(), security->encrypt_metadata() ? "true" : "false");
  } else {
    out += "null";
  }

  const auto& xref = document.xref();
  out += std::format(R"(,"xref":{{"startxref":{},"sections":[)", xref.startxref);
  for (std::size_t i = 0; i < xref.sections.size(); ++i) {
    const auto& section = xref.sections[i];
    out += std::format(R"({}{{"offset":{},"form":"{}","entries":{}}})", i == 0 ? "" : ",", section.offset, form_name(section.form), section.entry_count);
  }
  // Entries as compact arrays: [number, "n"|"c"|"f", generation or object stream number].
  out += R"(],"objects":[)";
  bool first = true;
  for (const auto& [number, entry] : xref.entries) {
    const char kind = entry_letter(entry.type);
    const std::uint64_t detail = entry.type == capriole::parser::XrefEntryType::kCompressed ? entry.stream_number : entry.generation;
    out += std::format(R"({}[{},"{}",{}])", first ? "" : ",", number, kind, detail);
    first = false;
  }
  out += R"(]},"trailer":)";
  append_json_object(out, capriole::model::Object(document.trailer()));

  out += R"(,"pages":)";
  if (pages) {
    out += '[';
    for (std::size_t i = 0; i < pages->size(); ++i) {
      out += std::format(R"({}[{},{}])", i == 0 ? "" : ",", (*pages)[i].number, (*pages)[i].generation);
    }
    out += ']';
  } else {
    out += "null,\"pagesError\":";
    append_json_string(out, pages.error().message);
  }

  const int pdfa = pdfa_part(document);
  out += pdfa != 0 ? std::format(R"(,"pdfa":{})", pdfa) : std::string(R"(,"pdfa":null)");
  out += R"(,"repairs":[)";
  first = true;
  for (const auto& note : document.repairs()) {
    out += first ? "" : ",";
    first = false;
    out += R"({"kind":")";
    out += capriole::repair::to_string(note.kind);
    out += R"(","description":)";
    append_json_string(out, note.description);
    out += '}';
  }
  out += "]}";
  return out;
}

}  // namespace

// The handle behind capriole_document: the C API's view of a capriole::Document.
struct capriole_document {
  capriole::Document document;
};

struct capriole_signing {
  capriole::edit::PreparedSignature prepared;
};

extern "C" {

const char* capriole_version(void) { return capriole::build::kVersionString.data(); }

int capriole_abi_version(void) { return CAPRIOLE_ABI_VERSION; }

const char* capriole_last_error_message(void) { return g_last_error.c_str(); }

capriole_status capriole_probe_header(const uint8_t* data, size_t size, int* major, int* minor) {
  g_last_error.clear();
  if ((data == nullptr && size != 0) || major == nullptr || minor == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    const auto bytes = std::as_bytes(std::span(data, size));
    capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
    const auto header = capriole::parser::find_header(source);
    if (!header) {
      return fail(header.error());
    }
    *major = header->major;
    *minor = header->minor;
    return CAPRIOLE_OK;
  } catch (...) {
    // Nothing may unwind across the C boundary; allocation failure is the realistic case here.
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_open(const uint8_t* data, size_t size, const char* password, int repair, capriole_document** out) {
  g_last_error.clear();
  if ((data == nullptr && size != 0) || out == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  *out = nullptr;
  try {
    const auto bytes = std::as_bytes(std::span(data, size));
    auto document = capriole::Document::open(
        std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(bytes.begin(), bytes.end())),
        capriole::OpenOptions{.limits = capriole::kDefaultParseLimits, .password = password != nullptr ? password : "", .repair = repair != 0});
    if (!document) {
      return fail(document.error());
    }
    *out = new capriole_document{.document = std::move(*document)};  // NOLINT(cppcoreguidelines-owning-memory): released by capriole_document_close
    return CAPRIOLE_OK;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

void capriole_document_close(capriole_document* document) {
  delete document;  // NOLINT(cppcoreguidelines-owning-memory): the handle made by capriole_document_open
}

void capriole_free(void* buffer) {
  std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): allocated with malloc by hand_over
}

capriole_status capriole_document_summary_json(capriole_document* document, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    return hand_over(summary_json(document->document), json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_object_json(capriole_document* document, uint32_t number, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto& doc = document->document;
    auto object = doc.get(number);
    if (!object) {
      return fail(object.error());
    }
    std::string out = std::format(R"({{"number":{},"generation":{},"state":"{}","object":)", number, doc.generation(number), state_name(doc.state(number)));
    capriole::capi::append_json_object(out, **object);
    out += '}';
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_content_json(capriole_document* document, uint32_t page_index, uint32_t max_operations, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto& doc = document->document;
    auto pages = doc.page_refs();
    if (!pages) {
      return fail(pages.error());
    }
    if (page_index >= pages->size()) {
      return fail(CAPRIOLE_ERROR_OUT_OF_RANGE, std::format("page index {} out of 0..{}", page_index, pages->size()));
    }
    const auto page = (*pages)[page_index];
    auto content = capriole::content::page_content(doc, page);
    if (!content) {
      return fail(content.error());
    }
    capriole::content::ContentParser parser(*content, capriole::kDefaultParseLimits, capriole::parser::ParseMode::kTolerant);
    std::string operations;
    std::size_t total = 0;
    for (;;) {
      auto operation = parser.next();
      if (!operation) {
        return fail(operation.error());
      }
      if (!*operation) {
        break;
      }
      if (total++ >= max_operations) {
        continue;  // counted, not listed
      }
      const auto& op = **operation;
      const auto& known = capriole::content::info(op.op);
      operations += operations.empty() ? "{" : ",{";
      operations += std::format(R"("o":{},"op":)", op.offset);
      capriole::capi::append_json_string(operations, op.name);
      operations += std::format(R"(,"category":"{}","operands":[)", capriole::content::to_string(known.category));
      for (std::size_t i = 0; i < op.operands.size(); ++i) {
        operations += i == 0 ? "" : ",";
        capriole::capi::append_json_object(operations, op.operands[i]);
      }
      operations += ']';
      if (op.image) {
        operations += R"(,"image":{"dict":)";
        capriole::capi::append_json_object(operations, capriole::model::Object(op.image->dict));
        operations += std::format(R"(,"size":{}}})", op.image->data.size());
      }
      operations += '}';
    }
    const std::string out = std::format(R"({{"page":{},"ref":[{},{}],"size":{},"total":{},"recovered":{},"operations":[{}]}})", page_index, page.number,
                                        page.generation, content->size(), total, parser.recovered_errors(), operations);
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_preview_json(capriole_document* document, uint32_t page_index, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto& doc = document->document;
    auto pages = doc.page_refs();
    if (!pages) {
      return fail(pages.error());
    }
    if (page_index >= pages->size()) {
      return fail(CAPRIOLE_ERROR_OUT_OF_RANGE, std::format("page index {} out of 0..{}", page_index, pages->size()));
    }
    auto list = interpret(doc, (*pages)[page_index]);
    if (!list) {
      return fail(list.error());
    }
    constexpr std::array<std::string_view, 11> kKinds = {"save",    "restore", "clip", "fill",  "stroke",   "image",
                                                         "shading", "pattern", "text", "group", "soft_mask"};
    std::array<std::size_t, kKinds.size()> counts{};
    for (const auto& command : list->commands) {
      const std::string_view kind = capriole::graphics::command_kind(command);
      for (std::size_t i = 0; i < kKinds.size(); ++i) {
        counts.at(i) += kKinds.at(i) == kind ? 1U : 0U;
      }
    }
    const bool quarter = list->rotate == 90 || list->rotate == 270;
    std::string out =
        std::format(R"({{"page":{},"width":{},"height":{},"rotate":{},"commands":{{)", page_index, quarter ? list->crop_box.height() : list->crop_box.width(),
                    quarter ? list->crop_box.width() : list->crop_box.height(), list->rotate);
    for (std::size_t i = 0; i < kKinds.size(); ++i) {
      out += std::format(R"({}"{}":{})", i == 0 ? "" : ",", kKinds.at(i), counts.at(i));
    }
    out += R"(},"warnings":[)";
    for (std::size_t i = 0; i < list->warnings.size(); ++i) {
      out += i == 0 ? "" : ",";
      capriole::capi::append_json_string(out, list->warnings[i]);
    }
    out += R"(],"missing_resources":[)";
    for (std::size_t i = 0; i < list->missing_resources.size(); ++i) {
      out += i == 0 ? "" : ",";
      capriole::capi::append_json_string(out, list->missing_resources[i]);
    }
    out += R"(],"svg":)";
    capriole::capi::append_json_string(out, capriole::graphics::to_svg(*list));
    out += '}';
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_text_json(capriole_document* document, uint32_t page_index, int order, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  if (order != CAPRIOLE_TEXT_ORDER_CONTENT && order != CAPRIOLE_TEXT_ORDER_LAYOUT) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "unknown reading order");
  }
  try {
    auto& doc = document->document;
    auto pages = doc.page_refs();
    if (!pages) {
      return fail(pages.error());
    }
    if (page_index >= pages->size()) {
      return fail(CAPRIOLE_ERROR_OUT_OF_RANGE, std::format("page index {} out of 0..{}", page_index, pages->size()));
    }
    auto list = interpret(doc, (*pages)[page_index], capriole::content::Purpose::kText);
    if (!list) {
      return fail(list.error());
    }
    const bool layout = order == CAPRIOLE_TEXT_ORDER_LAYOUT;
    const capriole::text::PageText text = capriole::text::extract(*list, layout ? capriole::text::Order::kLayout : capriole::text::Order::kContent);
    const auto ref = [](const std::optional<capriole::model::ObjectRef>& r) {
      return r ? std::format("[{},{}]", r->number, r->generation) : std::string("null");
    };
    const auto box = [](const capriole::graphics::Rect& b) { return std::format("{:.2f},{:.2f},{:.2f},{:.2f}", b.x0, b.y0, b.x1, b.y1); };
    std::string out =
        std::format(R"({{"page":{},"width":{:.2f},"height":{:.2f},"order":"{}","text":)", page_index, text.width, text.height, layout ? "layout" : "content");
    capriole::capi::append_json_text(out, text.plain_text());
    const auto ranges = [&](std::string_view name, const auto& items) {
      out += std::format(R"(,"{}":[)", name);
      for (std::size_t i = 0; i < items.size(); ++i) {
        out += std::format("{}[{},{},{}]", i == 0 ? "" : ",", box(items[i].box), items[i].first, items[i].count);
      }
      out += ']';
    };
    ranges("blocks", text.blocks);
    ranges("lines", text.lines);
    ranges("words", text.words);
    out += R"(,"chars":[)";
    for (std::size_t i = 0; i < text.chars.size(); ++i) {
      const capriole::text::Char& c = text.chars[i];
      const capriole::text::Provenance& p = c.provenance;
      out += i == 0 ? "[" : ",[";
      capriole::capi::append_json_text(out, c.text);
      out += std::format(",{},{:.2f},{},{},{},{},{},{},{}]", box(c.box), c.size, c.angle, c.invisible ? 1 : 0, ref(p.stream), p.offset, p.operation, p.byte,
                         ref(p.font));
    }
    out += R"(],"warnings":[)";
    for (std::size_t i = 0; i < list->warnings.size(); ++i) {
      out += i == 0 ? "" : ",";
      capriole::capi::append_json_string(out, list->warnings[i]);
    }
    out += R"(],"missing_resources":[)";
    for (std::size_t i = 0; i < list->missing_resources.size(); ++i) {
      out += i == 0 ? "" : ",";
      capriole::capi::append_json_string(out, list->missing_resources[i]);
    }
    out += "]}";
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_render(capriole_document* document, uint32_t page_index, double dpi, uint8_t** rgba, uint32_t* width, uint32_t* height) {
  g_last_error.clear();
  if (document == nullptr || rgba == nullptr || width == nullptr || height == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  *rgba = nullptr;
  *width = 0;
  *height = 0;
  if (!(dpi > 0) || !std::isfinite(dpi)) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "dpi must be a positive number");
  }
  try {
    auto& doc = document->document;
    auto pages = doc.page_refs();
    if (!pages) {
      return fail(pages.error());
    }
    if (page_index >= pages->size()) {
      return fail(CAPRIOLE_ERROR_OUT_OF_RANGE, std::format("page index {} out of 0..{}", page_index, pages->size()));
    }
    auto list = interpret(doc, (*pages)[page_index]);
    if (!list) {
      return fail(list.error());
    }
    // Tighter than the engine's default: in a browser the bitmap and Blend2D's own copy share one heap.
    capriole::render::RenderLimits limits;
    limits.max_pixels = 50'000'000;
    auto bitmap = capriole::render::rasterize(*list, dpi / 72, limits);
    if (!bitmap) {
      return fail(bitmap.error());
    }
    std::size_t size = 0;
    const capriole_status status = hand_over(std::as_bytes(std::span(bitmap->rgba)), rgba, &size);
    if (status == CAPRIOLE_OK) {
      *width = static_cast<uint32_t>(bitmap->width);
      *height = static_cast<uint32_t>(bitmap->height);
    }
    return status;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_resource_add(const char* name, const uint8_t* data, size_t size) {
  g_last_error.clear();
  if (name == nullptr || (data == nullptr && size > 0)) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    std::vector<std::byte> bytes(size);
    if (size > 0) {
      std::memcpy(bytes.data(), data, size);
    }
    SharedResources& shared = shared_resources();
    const std::unique_lock lock(shared.mutex);
    shared.resources.add(name, std::move(bytes));
    return CAPRIOLE_OK;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_last_missing_resources_json(char** json) {
  g_last_error.clear();
  if (json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    std::string out = "[";
    for (std::size_t i = 0; i < g_missing_resources.size(); ++i) {
      out += i == 0 ? "" : ",";
      capriole::capi::append_json_string(out, g_missing_resources[i]);
    }
    out += ']';
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_stream_data(capriole_document* document, uint32_t number, int decoded, uint8_t** data, size_t* size) {
  g_last_error.clear();
  if (document == nullptr || data == nullptr || size == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto& doc = document->document;
    auto object = doc.get(number);
    if (!object) {
      return fail(object.error());
    }
    const capriole::model::Stream* stream = (*object)->as_stream();
    if (stream == nullptr) {
      return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, std::format("object {} is not a stream", number));
    }
    auto bytes = decoded != 0 ? doc.decode_stream(*stream) : doc.encoded_stream_data(*stream);
    if (!bytes) {
      return fail(bytes.error());
    }
    return hand_over(*bytes, data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_pages_rotate(capriole_document* document, const uint32_t* pages, size_t count, int degrees) {
  g_last_error.clear();
  if (document == nullptr || (pages == nullptr && count > 0)) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto done = capriole::edit::rotate_pages(document->document, std::span(pages, count), degrees);
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_pages_arrange(capriole_document* document, const uint32_t* order, size_t count) {
  g_last_error.clear();
  if (document == nullptr || (order == nullptr && count > 0)) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto done = capriole::edit::arrange_pages(document->document, std::span(order, count));
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_pages_import(capriole_document* document, capriole_document* source, const uint32_t* pages, size_t count, uint32_t at) {
  g_last_error.clear();
  if (document == nullptr || source == nullptr || (pages == nullptr && count > 0)) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto added = capriole::edit::import_pages(document->document, source->document, std::span(pages, count), at);
    return added ? CAPRIOLE_OK : fail(added.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

}  // extern "C"

namespace {

// Specifications in JSON → the editor's structures. A key the kind does not use is ignored; a value of
// the wrong shape is an error.
capriole::Result<std::vector<double>> numbers_in(const capriole::capi::JsonValue* value, std::size_t count, std::string_view what) {
  const auto* array = value != nullptr ? value->array() : nullptr;
  if (array == nullptr || array->size() != count) {
    return capriole::make_error(capriole::ErrorCode::kSyntax, std::format("{}: an array of {} numbers expected", what, count));
  }
  std::vector<double> out;
  for (const auto& item : *array) {
    const auto n = item.number();
    if (!n) {
      return capriole::make_error(capriole::ErrorCode::kSyntax, std::format("{}: numbers expected", what));
    }
    out.push_back(*n);
  }
  return out;
}

capriole::Result<capriole::graphics::Rect> rect_in(const capriole::capi::JsonValue* value, std::string_view what) {
  auto v = numbers_in(value, 4, what);
  if (!v) {
    return std::unexpected(std::move(v.error()));
  }
  return capriole::graphics::Rect{(*v)[0], (*v)[1], (*v)[2], (*v)[3]}.normalized();
}

capriole::Result<capriole::graphics::Point> point_in(const capriole::capi::JsonValue* value, std::string_view what) {
  auto v = numbers_in(value, 2, what);
  if (!v) {
    return std::unexpected(std::move(v.error()));
  }
  return capriole::graphics::Point{(*v)[0], (*v)[1]};
}

capriole::Result<capriole::edit::Rgb> rgb_in(const capriole::capi::JsonValue* value, std::string_view what) {
  auto v = numbers_in(value, 3, what);
  if (!v) {
    return std::unexpected(std::move(v.error()));
  }
  return capriole::edit::Rgb{(*v)[0], (*v)[1], (*v)[2]};
}

capriole::Result<capriole::edit::AnnotationSpec> spec_of(const capriole::capi::JsonValue& json) {
  using capriole::ErrorCode;
  using capriole::make_error;
  using capriole::edit::AnnotationKind;
  if (json.object() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the specification is not an object");
  }
  const auto* kind = json.get("kind");
  const std::string* kind_name = kind != nullptr ? kind->string() : nullptr;
  static const std::map<std::string_view, AnnotationKind> kKinds = {
      {"highlight", AnnotationKind::kHighlight}, {"underline", AnnotationKind::kUnderline}, {"strikeout", AnnotationKind::kStrikeOut},
      {"squiggly", AnnotationKind::kSquiggly},   {"note", AnnotationKind::kNote},           {"ink", AnnotationKind::kInk},
      {"square", AnnotationKind::kSquare},       {"circle", AnnotationKind::kCircle},       {"line", AnnotationKind::kLine},
      {"freetext", AnnotationKind::kFreeText},   {"redact", AnnotationKind::kRedact}};
  const auto found = kind_name != nullptr ? kKinds.find(*kind_name) : kKinds.end();
  if (found == kKinds.end()) {
    return make_error(ErrorCode::kSyntax, "unknown or missing \"kind\"");
  }
  capriole::edit::AnnotationSpec spec;
  spec.kind = found->second;
  if (const auto* quads = json.get("quads")) {
    if (quads->array() == nullptr) {
      return make_error(ErrorCode::kSyntax, "quads: an array expected");
    }
    for (const auto& q : *quads->array()) {
      auto r = rect_in(&q, "quads");
      if (!r) {
        return std::unexpected(std::move(r.error()));
      }
      spec.quads.push_back(*r);
    }
  }
  if (const auto* strokes = json.get("strokes")) {
    if (strokes->array() == nullptr) {
      return make_error(ErrorCode::kSyntax, "strokes: an array expected");
    }
    for (const auto& stroke : *strokes->array()) {
      if (stroke.array() == nullptr) {
        return make_error(ErrorCode::kSyntax, "strokes: arrays of points expected");
      }
      std::vector<capriole::graphics::Point> points;
      for (const auto& p : *stroke.array()) {
        auto point = point_in(&p, "strokes");
        if (!point) {
          return std::unexpected(std::move(point.error()));
        }
        points.push_back(*point);
      }
      spec.strokes.push_back(std::move(points));
    }
  }
  const auto read = [&](std::string_view key, auto& target, auto reader) -> capriole::Result<void> {
    if (const auto* value = json.get(key)) {
      auto parsed = reader(value, key);
      if (!parsed) {
        return std::unexpected(std::move(parsed.error()));
      }
      target = *parsed;
    }
    return {};
  };
  for (auto result : {read("rect", spec.rect, rect_in), read("from", spec.from, point_in), read("to", spec.to, point_in), read("color", spec.color, rgb_in)}) {
    if (!result) {
      return std::unexpected(std::move(result.error()));
    }
  }
  if (const auto* fill = json.get("fill"); fill != nullptr && fill->array() != nullptr) {
    auto c = rgb_in(fill, "fill");
    if (!c) {
      return std::unexpected(std::move(c.error()));
    }
    spec.fill = *c;
  }
  for (const auto& [key, target] : {std::pair<std::string_view, double*>{"width", &spec.width}, {"opacity", &spec.opacity}, {"fontSize", &spec.font_size}}) {
    if (const auto* value = json.get(key)) {
      if (!value->number()) {
        return make_error(ErrorCode::kSyntax, std::format("{}: a number expected", key));
      }
      *target = *value->number();
    }
  }
  for (const auto& [key, target] : {std::pair<std::string_view, std::string*>{"contents", &spec.contents}, {"author", &spec.author}, {"date", &spec.date}}) {
    if (const auto* value = json.get(key)) {
      if (value->string() == nullptr) {
        return make_error(ErrorCode::kSyntax, std::format("{}: a string expected", key));
      }
      *target = *value->string();
    }
  }
  return spec;
}

void append_certificate(std::string& out, const capriole::crypto::CertificateInfo& c) {
  const auto text = [&](std::string_view key, const std::string& utf8) {
    out += std::format(R"("{}":)", key);
    capriole::capi::append_json_text(out, capriole::model::from_utf8(utf8));
  };
  out += "{";
  text("name", c.common_name);
  out += ",";
  text("subject", c.subject);
  out += ",";
  text("issuer", c.issuer);
  out += std::format(R"(,"serial":"{}","notBefore":"{}","notAfter":"{}","key":"{}","ca":{}}})", c.serial, c.not_before, c.not_after, c.key, c.ca);
}

std::string_view integrity_name(capriole::signature::Integrity integrity) {
  switch (integrity) {
    case capriole::signature::Integrity::kValid:
      return "valid";
    case capriole::signature::Integrity::kInvalid:
      return "invalid";
    case capriole::signature::Integrity::kUnsupported:
      return "unsupported";
  }
  return "";
}

std::string_view trust_name(capriole::signature::Trust trust) {
  switch (trust) {
    case capriole::signature::Trust::kTrusted:
      return "trusted";
    case capriole::signature::Trust::kUntrusted:
      return "untrusted";
    case capriole::signature::Trust::kNotChecked:
      return "notChecked";
  }
  return "";
}

std::string signatures_json(const std::vector<capriole::signature::SignatureInfo>& list) {
  std::string out = "[";
  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& s = list[i];
    const auto text = [&](std::string_view key, const std::string& utf8) {
      out += std::format(R"(,"{}":)", key);
      capriole::capi::append_json_text(out, capriole::model::from_utf8(utf8));
    };
    out += std::format(R"({}{{"field":[{},{}])", i == 0 ? "" : ",", s.field.number, s.field.generation);
    text("fieldName", s.field_name);
    text("subFilter", s.sub_filter);
    out += std::format(R"(,"documentTimestamp":{})", s.document_timestamp);
    text("name", s.name);
    text("reason", s.reason);
    text("location", s.location);
    text("contactInfo", s.contact_info);
    out += R"(,"date":)";
    capriole::capi::append_json_string(out, s.date);
    out += std::format(R"(,"integrity":"{}")", integrity_name(s.integrity));
    text("problem", s.problem);
    out += s.digest ? std::format(R"(,"digest":"{}")", capriole::crypto::name(*s.digest)) : std::string(R"(,"digest":null)");
    out += std::format(R"(,"coversWholeFile":{},"signedBytes":{},"signer":)", s.covers_whole_file, s.signed_bytes);
    if (s.signer) {
      append_certificate(out, *s.signer);
    } else {
      out += "null";
    }
    out += std::format(R"(,"level":"{}","timestamp":)", s.level);
    capriole::capi::append_json_string(out, s.timestamp.value_or(""));
    out += std::format(R"(,"timestampInvalid":{},"signingTime":)", s.timestamp_invalid);
    capriole::capi::append_json_string(out, s.signing_time);
    out += std::format(R"(,"certificateValidAtSigning":{},"trust":"{}")", s.certificate_valid_at_signing, trust_name(s.trust));
    text("trustProblem", s.trust_problem);
    out += s.page ? std::format(R"(,"page":{})", *s.page) : std::string(R"(,"page":null)");
    out += s.rect ? std::format(R"(,"rect":[{},{},{},{}]}})", capriole::writer::format_real(s.rect->x0), capriole::writer::format_real(s.rect->y0),
                                capriole::writer::format_real(s.rect->x1), capriole::writer::format_real(s.rect->y1))
                  : std::string(R"(,"rect":null})");
  }
  return out + "]";
}

capriole::Result<capriole::edit::SignRequest> sign_request_of(const char* spec_json) {
  auto spec = capriole::capi::parse_json(spec_json);
  if (!spec) {
    return std::unexpected(std::move(spec.error()));
  }
  if (spec->object() == nullptr) {
    return capriole::make_error(capriole::ErrorCode::kSyntax, "a JSON object expected");
  }
  capriole::edit::SignRequest request;
  const auto string_of = [&](std::string_view key, std::string& into) -> capriole::Result<void> {
    if (const auto* v = spec->get(key); v != nullptr && !std::holds_alternative<std::nullptr_t>(v->value)) {
      if (v->string() == nullptr) {
        return capriole::make_error(capriole::ErrorCode::kSyntax, std::format("{}: a string expected", key));
      }
      into = *v->string();
    }
    return {};
  };
  for (const auto& [key, into] : {std::pair<std::string_view, std::string*>{"date", &request.date},
                                  {"fieldName", &request.field_name},
                                  {"name", &request.name},
                                  {"reason", &request.reason},
                                  {"location", &request.location},
                                  {"contactInfo", &request.contact_info}}) {
    if (auto read = string_of(key, *into); !read) {
      return std::unexpected(std::move(read.error()));
    }
  }
  if (const auto* page = spec->get("page")) {
    const auto n = page->number();
    if (!n || *n < 0 || *n > 4294967295.0 || *n != std::floor(*n)) {
      return capriole::make_error(capriole::ErrorCode::kSyntax, "page: an index expected");
    }
    request.page = static_cast<std::uint32_t>(*n);
  }
  if (const auto* rect = spec->get("rect"); rect != nullptr && !std::holds_alternative<std::nullptr_t>(rect->value)) {
    auto r = rect_in(rect, "rect");
    if (!r) {
      return std::unexpected(std::move(r.error()));
    }
    request.rect = *r;
  }
  if (const auto* lines = spec->get("lines")) {
    if (lines->array() == nullptr) {
      return capriole::make_error(capriole::ErrorCode::kSyntax, "lines: an array of strings expected");
    }
    for (const auto& line : *lines->array()) {
      if (line.string() == nullptr) {
        return capriole::make_error(capriole::ErrorCode::kSyntax, "lines: an array of strings expected");
      }
      request.lines.push_back(*line.string());
    }
  }
  if (const auto* timestamp = spec->get("timestamp")) {
    const auto b = timestamp->boolean();
    if (!b) {
      return capriole::make_error(capriole::ErrorCode::kSyntax, "timestamp: a boolean expected");
    }
    request.timestamp = *b;
  }
  return request;
}

std::string redaction_json(const capriole::edit::RedactionReport& r) {
  return std::format(R"({{"glyphs":{},"paths":{},"imagesRemoved":{},"imagesCleared":{},"annotations":{}}})", r.glyphs, r.paths, r.images_removed,
                     r.images_cleared, r.annotations);
}

capriole::Result<capriole::edit::AnnotationChange> change_of(const capriole::capi::JsonValue& json) {
  using capriole::ErrorCode;
  using capriole::make_error;
  if (json.object() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the change is not an object");
  }
  capriole::edit::AnnotationChange change;
  if (const auto* move = json.get("moveBy")) {
    auto p = point_in(move, "moveBy");
    if (!p) {
      return std::unexpected(std::move(p.error()));
    }
    change.move_by = *p;
  }
  if (const auto* rect = json.get("rect")) {
    auto r = rect_in(rect, "rect");
    if (!r) {
      return std::unexpected(std::move(r.error()));
    }
    change.rect = *r;
  }
  if (const auto* size = json.get("fontSize")) {
    if (!size->number()) {
      return make_error(ErrorCode::kSyntax, "fontSize: a number expected");
    }
    change.font_size = size->number();
  }
  if (const auto* color = json.get("color")) {
    auto c = rgb_in(color, "color");
    if (!c) {
      return std::unexpected(std::move(c.error()));
    }
    change.color = *c;
  }
  if (const auto* contents = json.get("contents")) {
    if (contents->string() == nullptr) {
      return make_error(ErrorCode::kSyntax, "contents: a string expected");
    }
    change.contents = *contents->string();
  }
  if (const auto* opacity = json.get("opacity")) {
    if (!opacity->number()) {
      return make_error(ErrorCode::kSyntax, "opacity: a number expected");
    }
    change.opacity = opacity->number();
  }
  if (const auto* date = json.get("date"); date != nullptr && date->string() != nullptr) {
    change.date = *date->string();
  }
  return change;
}

}  // namespace

extern "C" {

capriole_status capriole_document_page_annotations_json(capriole_document* document, uint32_t page_index, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto list = capriole::edit::list_annotations(document->document, page_index);
    if (!list) {
      return fail(list.error());
    }
    std::string out = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
      const auto& a = (*list)[i];
      out += i == 0 ? "{" : ",{";
      out += a.ref ? std::format(R"("ref":[{},{}])", a.ref->number, a.ref->generation) : std::string(R"("ref":null)");
      out += R"(,"subtype":)";
      capriole::capi::append_json_string(out, a.subtype);
      out += std::format(R"(,"rect":[{:.2f},{:.2f},{:.2f},{:.2f}],"contents":)", a.rect.x0, a.rect.y0, a.rect.x1, a.rect.y1);
      capriole::capi::append_json_text(out, capriole::model::from_utf8(a.contents));
      out += R"(,"author":)";
      capriole::capi::append_json_text(out, capriole::model::from_utf8(a.author));
      out += a.color ? std::format(R"(,"color":[{:.3f},{:.3f},{:.3f}])", (*a.color)[0], (*a.color)[1], (*a.color)[2]) : std::string(R"(,"color":null)");
      out += a.font_size ? std::format(R"(,"fontSize":{:.2f})", *a.font_size) : std::string(R"(,"fontSize":null)");
      out += std::format(R"(,"hidden":{},"editable":{}}})", a.hidden, a.editable);
    }
    out += "]";
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_annotation_add(capriole_document* document, uint32_t page_index, const char* spec_json, uint32_t* number,
                                                 uint32_t* generation) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || number == nullptr || generation == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto json = capriole::capi::parse_json(spec_json);
    if (!json) {
      return fail(json.error());
    }
    auto spec = spec_of(*json);
    if (!spec) {
      return fail(spec.error());
    }
    auto ref = capriole::edit::add_annotation(document->document, page_index, *spec);
    if (!ref) {
      return fail(ref.error());
    }
    *number = ref->number;
    *generation = ref->generation;
    return CAPRIOLE_OK;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_annotation_update(capriole_document* document, uint32_t page_index, uint32_t number, uint32_t generation,
                                                    const char* change_json) {
  g_last_error.clear();
  if (document == nullptr || change_json == nullptr || generation > 65535) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument or generation out of range");
  }
  try {
    auto json = capriole::capi::parse_json(change_json);
    if (!json) {
      return fail(json.error());
    }
    auto change = change_of(*json);
    if (!change) {
      return fail(change.error());
    }
    const capriole::model::ObjectRef ref{.number = number, .generation = static_cast<std::uint16_t>(generation)};
    auto done = capriole::edit::update_annotation(document->document, page_index, ref, *change);
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_annotation_remove(capriole_document* document, uint32_t page_index, uint32_t number, uint32_t generation) {
  g_last_error.clear();
  if (document == nullptr || generation > 65535) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument or generation out of range");
  }
  try {
    const capriole::model::ObjectRef ref{.number = number, .generation = static_cast<std::uint16_t>(generation)};
    auto done = capriole::edit::remove_annotation(document->document, page_index, ref);
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_form_fields_json(capriole_document* document, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto fields = capriole::edit::list_fields(document->document);
    if (!fields) {
      return fail(fields.error());
    }
    using capriole::edit::FieldType;
    const auto text = [](std::string& out, const std::string& utf8) { capriole::capi::append_json_text(out, capriole::model::from_utf8(utf8)); };
    std::string out = "[";
    for (std::size_t i = 0; i < fields->size(); ++i) {
      const auto& f = (*fields)[i];
      out += std::format(R"({}{{"ref":[{},{}],"name":)", i == 0 ? "" : ",", f.ref.number, f.ref.generation);
      text(out, f.name);
      out += R"(,"label":)";
      text(out, f.label);
      static constexpr std::array<std::string_view, 7> kTypes = {"text", "checkbox", "radio", "pushbutton", "combobox", "listbox", "signature"};
      out += std::format(R"(,"type":"{}","value":[)", kTypes.at(static_cast<std::size_t>(f.type)));
      for (std::size_t v = 0; v < f.value.size(); ++v) {
        out += v == 0 ? "" : ",";
        text(out, f.value[v]);
      }
      out += R"(],"options":[)";
      for (std::size_t o = 0; o < f.options.size(); ++o) {
        out += o == 0 ? R"({"value":)" : R"(,{"value":)";
        text(out, f.options[o].value);
        out += R"(,"display":)";
        text(out, f.options[o].display);
        out += "}";
      }
      out += f.max_length ? std::format(R"(],"maxLength":{})", *f.max_length) : std::string(R"(],"maxLength":null)");
      out += std::format(R"(,"readOnly":{},"required":{},"multiline":{},"password":{},"comb":{},"editable":{},"multiSelect":{},"fillable":{},"widgets":[)",
                         f.read_only, f.required, f.multiline, f.password, f.comb, f.editable, f.multi_select, f.fillable);
      for (std::size_t w = 0; w < f.widgets.size(); ++w) {
        const auto& widget = f.widgets[w];
        out += std::format(R"({}{{"ref":[{},{}],"page":{},"rect":[{:.2f},{:.2f},{:.2f},{:.2f}],"onState":)", w == 0 ? "" : ",", widget.ref.number,
                           widget.ref.generation, widget.page ? std::to_string(*widget.page) : std::string("null"), widget.rect.x0, widget.rect.y0,
                           widget.rect.x1, widget.rect.y1);
        text(out, widget.on_state);
        out += "}";
      }
      out += "]}";
    }
    out += "]";
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_form_field_fill(capriole_document* document, uint32_t number, uint32_t generation, const char* value_json) {
  g_last_error.clear();
  if (document == nullptr || value_json == nullptr || generation > 65535) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument or generation out of range");
  }
  try {
    auto json = capriole::capi::parse_json(value_json);
    if (!json) {
      return fail(json.error());
    }
    std::vector<std::string> value;
    if (const std::string* one = json->string()) {
      value.push_back(*one);
    } else if (const auto* many = json->array()) {
      for (const auto& item : *many) {
        if (item.string() == nullptr) {
          return fail(CAPRIOLE_ERROR_SYNTAX, "the value: an array of strings expected");
        }
        value.push_back(*item.string());
      }
    } else if (!std::holds_alternative<std::nullptr_t>(json->value)) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "the value: a string, an array of strings or null expected");
    }
    const capriole::model::ObjectRef ref{.number = number, .generation = static_cast<std::uint16_t>(generation)};
    auto done = capriole::edit::fill_field(document->document, ref, value);
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_attachments_json(capriole_document* document, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto list = capriole::edit::list_attachments(document->document);
    if (!list) {
      return fail(list.error());
    }
    const auto text = [](std::string& out, const std::string& utf8) { capriole::capi::append_json_text(out, capriole::model::from_utf8(utf8)); };
    std::string out = "[";
    for (std::size_t i = 0; i < list->size(); ++i) {
      const auto& a = (*list)[i];
      out += std::format(R"({}{{"ref":[{},{}],"name":)", i == 0 ? "" : ",", a.spec.number, a.spec.generation);
      text(out, a.name);
      out += R"(,"fileName":)";
      text(out, a.file_name);
      out += R"(,"description":)";
      text(out, a.description);
      out += R"(,"mimeType":)";
      capriole::capi::append_json_string(out, a.mime_type);
      out += a.size ? std::format(R"(,"size":{})", *a.size) : std::string(R"(,"size":null)");
      out += R"(,"creationDate":)";
      capriole::capi::append_json_string(out, a.creation_date);
      out += R"(,"modificationDate":)";
      capriole::capi::append_json_string(out, a.modification_date);
      out += R"(,"relationship":)";
      capriole::capi::append_json_string(out, a.relationship);
      out += a.page ? std::format(R"(,"page":{})", *a.page) : std::string(R"(,"page":null)");
      out += std::format(R"(,"associated":{}}})", a.associated);
    }
    out += "]";
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_attachment_data(capriole_document* document, uint32_t number, uint32_t generation, uint8_t** data, size_t* size,
                                                  int* checksum) {
  g_last_error.clear();
  if (document == nullptr || data == nullptr || size == nullptr || generation > 65535) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument or generation out of range");
  }
  try {
    const capriole::model::ObjectRef ref{.number = number, .generation = static_cast<std::uint16_t>(generation)};
    auto read = capriole::edit::attachment_data(document->document, ref);
    if (!read) {
      return fail(read.error());
    }
    if (checksum != nullptr) {
      *checksum = -1;
      if (read->checksum_matches) {
        *checksum = *read->checksum_matches ? 1 : 0;
      }
    }
    return hand_over(read->bytes, data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_attachment_add(capriole_document* document, const char* spec_json, const uint8_t* data, size_t size, uint32_t* number,
                                                 uint32_t* generation) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || (data == nullptr && size > 0) || number == nullptr || generation == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto json = capriole::capi::parse_json(spec_json);
    if (!json) {
      return fail(json.error());
    }
    if (json->object() == nullptr) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "the specification is not an object");
    }
    capriole::edit::AttachmentSpec spec;
    for (const auto& [key, target] : {std::pair<std::string_view, std::string*>{"fileName", &spec.file_name},
                                      {"description", &spec.description},
                                      {"mimeType", &spec.mime_type},
                                      {"relationship", &spec.relationship},
                                      {"creationDate", &spec.creation_date},
                                      {"modificationDate", &spec.modification_date}}) {
      if (const auto* value = json->get(key)) {
        if (value->string() == nullptr) {
          return fail(CAPRIOLE_ERROR_SYNTAX, std::format("{}: a string expected", key));
        }
        *target = *value->string();
      }
    }
    const auto bytes = size == 0 ? std::span<const std::byte>() : std::as_bytes(std::span(data, size));
    auto ref = capriole::edit::add_attachment(document->document, spec, bytes);
    if (!ref) {
      return fail(ref.error());
    }
    *number = ref->number;
    *generation = ref->generation;
    return CAPRIOLE_OK;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_attachment_remove(capriole_document* document, uint32_t number, uint32_t generation) {
  g_last_error.clear();
  if (document == nullptr || generation > 65535) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument or generation out of range");
  }
  try {
    const capriole::model::ObjectRef ref{.number = number, .generation = static_cast<std::uint16_t>(generation)};
    auto done = capriole::edit::remove_attachment(document->document, ref);
    return done ? CAPRIOLE_OK : fail(done.error());
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_facturx_json(capriole_document* document, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto found = capriole::edit::find_facturx(document->document);
    if (!found) {
      return fail(found.error());
    }
    if (!*found) {
      return hand_over(std::string("null"), json);
    }
    const auto& f = **found;
    std::string out = std::format(R"({{"ref":[{},{}],"fileName":)", f.spec.number, f.spec.generation);
    capriole::capi::append_json_text(out, capriole::model::from_utf8(f.file_name));
    out += R"(,"conformanceLevel":)";
    capriole::capi::append_json_string(out, f.conformance_level);
    out += R"(,"version":)";
    capriole::capi::append_json_string(out, f.version);
    out += std::format(R"(,"pdfa3":{}}})", f.pdfa3);
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_facturx_embed(capriole_document* document, const uint8_t* xml, size_t size, const char* date, char** json) {
  g_last_error.clear();
  if (document == nullptr || xml == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto result = capriole::edit::embed_facturx(document->document, std::as_bytes(std::span(xml, size)), date != nullptr ? date : "");
    if (!result) {
      return fail(result.error());
    }
    std::string out = std::format(R"({{"ref":[{},{}],"conformanceLevel":)", result->spec.number, result->spec.generation);
    capriole::capi::append_json_string(out, result->conformance_level);
    out += std::format(R"(,"pdfa3":{}}})", result->pdfa3);
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_redact(capriole_document* document, uint32_t page_index, const char* spec_json, char** json) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto spec = capriole::capi::parse_json(spec_json);
    if (!spec) {
      return fail(spec.error());
    }
    const auto* areas = spec->get("areas");
    if (spec->object() == nullptr || areas == nullptr || areas->array() == nullptr) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "areas: an array of rectangles expected");
    }
    std::vector<capriole::graphics::Rect> rects;
    for (const auto& area : *areas->array()) {
      auto r = rect_in(&area, "areas");
      if (!r) {
        return fail(r.error());
      }
      rects.push_back(*r);
    }
    capriole::edit::RedactionOptions options;
    if (const auto* fill = spec->get("fill")) {
      if (std::holds_alternative<std::nullptr_t>(fill->value)) {
        options.fill.reset();
      } else {
        auto c = rgb_in(fill, "fill");
        if (!c) {
          return fail(c.error());
        }
        options.fill = *c;
      }
    }
    auto report = capriole::edit::redact(document->document, page_index, rects, options);
    if (!report) {
      return fail(report.error());
    }
    return hand_over(redaction_json(*report), json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_apply_redactions(capriole_document* document, uint32_t page_index, char** json) {
  g_last_error.clear();
  if (document == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto report = capriole::edit::apply_redactions(document->document, page_index);
    if (!report) {
      return fail(report.error());
    }
    return hand_over(redaction_json(*report), json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_save_compact(capriole_document* document, const char* modification_date, uint8_t** data, size_t* size) {
  g_last_error.clear();
  if (document == nullptr || data == nullptr || size == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    capriole::writer::SaveOptions options;
    options.drop_unreachable = true;
    if (modification_date != nullptr) {
      options.modification_date = modification_date;
    }
    capriole::writer::MemorySink sink;
    auto report = capriole::writer::save_full(document->document, sink, options);
    if (!report) {
      return fail(report.error());
    }
    return hand_over(sink.data(), data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_save(capriole_document* document, int incremental, const char* modification_date, uint8_t** data, size_t* size) {
  g_last_error.clear();
  if (document == nullptr || data == nullptr || size == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    capriole::writer::SaveOptions options;
    if (modification_date != nullptr) {
      options.modification_date = modification_date;
    }
    capriole::writer::MemorySink sink;
    auto report = incremental != 0 ? capriole::writer::save_incremental(document->document, sink, options)
                                   : capriole::writer::save_full(document->document, sink, options);
    if (!report) {
      return fail(report.error());
    }
    return hand_over(sink.data(), data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_pkcs12_info(const uint8_t* p12, size_t size, const char* password, char** json) {
  g_last_error.clear();
  if ((p12 == nullptr && size != 0) || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto credentials = capriole::crypto::read_pkcs12(std::as_bytes(std::span(p12, size)), password != nullptr ? password : "");
    if (!credentials) {
      return fail(credentials.error());
    }
    auto info = capriole::crypto::certificate_info(credentials->certificate());
    if (!info) {
      return fail(info.error());
    }
    std::string out;
    append_certificate(out, *info);
    out.pop_back();
    out += std::format(R"(,"chain":{}}})", credentials->chain().size());
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_sign_begin(capriole_document* document, const char* spec_json, const uint8_t* p12, size_t p12_size, const char* password,
                                             capriole_signing** signing) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || (p12 == nullptr && p12_size != 0) || signing == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  *signing = nullptr;
  try {
    auto request = sign_request_of(spec_json);
    if (!request) {
      return fail(request.error());
    }
    auto credentials = capriole::crypto::read_pkcs12(std::as_bytes(std::span(p12, p12_size)), password != nullptr ? password : "");
    if (!credentials) {
      return fail(credentials.error());
    }
    auto prepared = capriole::edit::prepare_signature(document->document, *credentials, *request);
    if (!prepared) {
      return fail(prepared.error());
    }
    *signing = new capriole_signing{.prepared = std::move(*prepared)};  // NOLINT(cppcoreguidelines-owning-memory): released by capriole_signing_free
    return CAPRIOLE_OK;
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_signing_timestamp_request(capriole_signing* signing, uint8_t** data, size_t* size) {
  g_last_error.clear();
  if (signing == nullptr || data == nullptr || size == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    return hand_over(signing->prepared.timestamp_request, data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_sign_finish(capriole_document* document, capriole_signing* signing, const uint8_t* response, size_t response_size,
                                              uint8_t** data, size_t* size) {
  g_last_error.clear();
  if (document == nullptr || signing == nullptr || (response == nullptr && response_size != 0) || data == nullptr || size == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto file = capriole::edit::finish_signature(signing->prepared, std::as_bytes(std::span(response, response_size)));
    if (!file) {
      return fail(file.error());
    }
    // The document goes on from the signed file: what is edited next is appended after the signature.
    auto reopened = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(*file)),
                                             capriole::OpenOptions{.limits = capriole::kDefaultParseLimits, .password = "", .repair = true});
    if (!reopened) {
      return fail(reopened.error());
    }
    document->document = std::move(*reopened);
    return hand_over(*file, data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

void capriole_signing_free(capriole_signing* signing) {
  delete signing;  // NOLINT(cppcoreguidelines-owning-memory): the handle made by capriole_document_sign_begin
}

capriole_status capriole_document_signatures_json(capriole_document* document, const uint8_t* anchors, size_t anchors_size, char** json) {
  g_last_error.clear();
  if (document == nullptr || (anchors == nullptr && anchors_size != 0) || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    capriole::signature::VerifyOptions options;
    options.trust_anchors = std::as_bytes(std::span(anchors, anchors_size));
    auto list = capriole::signature::list_signatures(document->document, options);
    if (!list) {
      return fail(list.error());
    }
    return hand_over(signatures_json(*list), json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_convert_pdfa(capriole_document* document, const char* spec_json, uint8_t** data, size_t* size, char** json) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || data == nullptr || size == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto spec = capriole::capi::parse_json(spec_json);
    if (!spec) {
      return fail(spec.error());
    }
    if (spec->object() == nullptr) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "a JSON object expected");
    }
    capriole::edit::PdfaOptions options;
    if (const auto* part = spec->get("part")) {
      const auto n = part->number();
      if (!n || (*n != 0 && *n != 2 && *n != 3)) {
        return fail(CAPRIOLE_ERROR_SYNTAX, "part: 0, 2 or 3 expected");
      }
      options.part = static_cast<int>(*n);
    }
    const auto* date = spec->get("date");
    if (date == nullptr || date->string() == nullptr) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "date: a PDF date expected");
    }
    options.date = *date->string();
    const auto result = capriole::edit::convert_to_pdfa(document->document, options);
    if (!result) {
      return fail(result.error());
    }
    std::string report = std::format(R"({{"part":{},"changes":[)", result->part);
    for (std::size_t i = 0; i < result->changes.size(); ++i) {
      report += i == 0 ? "" : ",";
      capriole::capi::append_json_text(report, capriole::model::from_utf8(result->changes[i]));
    }
    report += "]}";
    auto reopened = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::vector<std::byte>(result->file)),
                                             capriole::OpenOptions{.limits = capriole::kDefaultParseLimits, .password = "", .repair = true});
    if (!reopened) {
      return fail(reopened.error());
    }
    document->document = std::move(*reopened);
    if (const capriole_status status = hand_over(report, json); status != CAPRIOLE_OK) {
      return status;
    }
    return hand_over(result->file, data, size);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

capriole_status capriole_document_page_replace_text(capriole_document* document, uint32_t page_index, const char* spec_json, char** json) {
  g_last_error.clear();
  if (document == nullptr || spec_json == nullptr || json == nullptr) {
    return fail(CAPRIOLE_ERROR_INVALID_ARGUMENT, "null pointer argument");
  }
  try {
    auto spec = capriole::capi::parse_json(spec_json);
    if (!spec) {
      return fail(spec.error());
    }
    auto area = rect_in(spec->get("area"), "area");
    if (!area) {
      return fail(area.error());
    }
    const auto* text = spec->get("text");
    if (text == nullptr || text->string() == nullptr) {
      return fail(CAPRIOLE_ERROR_SYNTAX, "text: a string expected");
    }
    auto result = capriole::edit::replace_text(document->document, page_index, *area, *text->string());
    if (!result) {
      return fail(result.error());
    }
    std::string out = std::format(R"({{"method":"{}","font":)", result->method == capriole::edit::TextEditMethod::kSameFont ? "sameFont" : "similarFont");
    capriole::capi::append_json_text(out, capriole::model::from_utf8(result->font));
    out += R"(,"replaced":)";
    capriole::capi::append_json_text(out, capriole::model::from_utf8(result->replaced));
    out += std::format(R"(,"scale":{}}})", capriole::writer::format_real(result->scale));
    return hand_over(out, json);
  } catch (...) {
    return fail(CAPRIOLE_ERROR_INTERNAL, "internal error");
  }
}

}  // extern "C"
