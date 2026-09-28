#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capriole/capriole.h"
#include "capriole/content/content_parser.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/version_config.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/svg.hpp"
#include "capriole/parser/header.hpp"
#include "capriole/render/raster.hpp"
#include "capriole/repair/repair.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "json.hpp"

namespace {

// Per-thread last error, the errno-style contract documented in capriole.h.
thread_local std::string g_last_error;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

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
    auto list = capriole::content::interpret_page(doc, (*pages)[page_index]);
    if (!list) {
      return fail(list.error());
    }
    constexpr std::array<std::string_view, 8> kKinds = {"save", "restore", "clip", "fill", "stroke", "image", "shading", "text"};
    std::array<std::size_t, kKinds.size()> counts{};
    for (const auto& command : list->commands) {
      ++counts.at(command.index());
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
    out += R"(],"svg":)";
    capriole::capi::append_json_string(out, capriole::graphics::to_svg(*list));
    out += '}';
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
    auto list = capriole::content::interpret_page(doc, (*pages)[page_index]);
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

}  // extern "C"
