#include "capriole/edit/facturx.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/edit/attachments.hpp"
#include "common.hpp"
#include "sound.hpp"
#include "xmp.hpp"

namespace capriole::edit {

namespace {

using detail::name;
using model::Dict;
using model::Object;

constexpr std::string_view kNamespace = "urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#";
constexpr std::string_view kFileName = "factur-x.xml";
constexpr std::array<std::string_view, 4> kInvoiceNames = {"factur-x.xml", "zugferd-invoice.xml", "xrechnung.xml", "order-x.xml"};
// Bounds: an invoice's XML, a document's XMP packet.
constexpr std::size_t kMaxXml = std::size_t{64} << 20U;
constexpr std::size_t kMaxXmp = std::size_t{8} << 20U;
constexpr std::size_t kMaxBlocks = 10'000;

std::string lower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string_view text_of(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes as text
}

// The text of the first element named local (any prefix) at or after from: "<x:local ...>text<".
std::optional<std::string> element_text(std::string_view xml, std::string_view local, std::size_t from, std::size_t within) {
  const std::size_t end = std::min(xml.size(), from + within);
  for (std::size_t at = xml.find('<', from); at != std::string_view::npos && at < end; at = xml.find('<', at + 1)) {
    std::size_t n = at + 1;
    while (n < end && (std::isalnum(static_cast<unsigned char>(xml[n])) != 0 || xml[n] == ':' || xml[n] == '_' || xml[n] == '-' || xml[n] == '.')) {
      ++n;
    }
    std::string_view tag = xml.substr(at + 1, n - at - 1);
    if (const std::size_t colon = tag.rfind(':'); colon != std::string_view::npos) {
      tag = tag.substr(colon + 1);
    }
    if (tag != local || n >= end || (xml[n] != '>' && xml[n] != ' ' && xml[n] != '\t' && xml[n] != '\r' && xml[n] != '\n')) {
      continue;
    }
    const std::size_t open = xml.find('>', n);
    const std::size_t close = open == std::string_view::npos ? open : xml.find('<', open + 1);
    if (close == std::string_view::npos || close > end) {
      return std::nullopt;
    }
    std::string_view text = xml.substr(open + 1, close - open - 1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
      text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
      text.remove_suffix(1);
    }
    return std::string(text);
  }
  return std::nullopt;
}

// An XMP value: <fx:Name>value</fx:Name>, or the attribute fx:Name="value".
std::string xmp_value(std::string_view xmp, std::string_view qualified) {
  if (const std::size_t at = xmp.find(std::format("<{}>", qualified)); at != std::string_view::npos) {
    const std::size_t begin = at + qualified.size() + 2;
    const std::size_t end = xmp.find('<', begin);
    return end == std::string_view::npos ? std::string() : std::string(xmp.substr(begin, end - begin));
  }
  for (const char quote : {'"', '\''}) {
    const std::string attribute = std::format("{}={}", qualified, quote);
    if (const std::size_t at = xmp.find(attribute); at != std::string_view::npos) {
      const std::size_t begin = at + attribute.size();
      const std::size_t end = xmp.find(quote, begin);
      return end == std::string_view::npos ? std::string() : std::string(xmp.substr(begin, end - begin));
    }
  }
  return {};
}

// The catalog's XMP packet as text; empty when none (or unreadable, or too large).
std::string catalog_xmp(Document& document) {
  auto catalog = document.catalog();
  const Object* entry = catalog ? (*catalog)->find("Metadata") : nullptr;
  auto stream = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!stream || stream->as_stream() == nullptr) {
    return {};
  }
  auto data = document.decode_stream(*stream->as_stream());
  if (!data || data->size() > kMaxXmp) {
    return {};
  }
  return std::string(text_of(*data));
}

bool declares_pdfa3(std::string_view xmp) { return xmp_value(xmp, "pdfaid:part") == "3"; }

std::string escape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string fx_description(std::string_view level) {
  return std::format(
      "<rdf:Description rdf:about=\"\" xmlns:fx=\"{}\">\n"
      "<fx:DocumentType>INVOICE</fx:DocumentType>\n"
      "<fx:DocumentFileName>{}</fx:DocumentFileName>\n"
      "<fx:Version>1.0</fx:Version>\n"
      "<fx:ConformanceLevel>{}</fx:ConformanceLevel>\n"
      "</rdf:Description>\n",
      kNamespace, kFileName, escape(level));
}

// The PDF/A extension schema that declares the fx properties (Factur-X 1.0, §6.2.2).
std::string fx_extension() {
  std::string properties;
  for (const auto& [property, description] : std::array<std::pair<std::string_view, std::string_view>, 4>{
           {{"DocumentFileName", "The name of the embedded XML document"},
            {"DocumentType", "The type of the hybrid document in capital letters, e.g. INVOICE or ORDER"},
            {"Version", "The actual version of the standard applying to the embedded XML document"},
            {"ConformanceLevel", "The conformance level of the embedded XML document"}}}) {
    properties += std::format(
        "<rdf:li rdf:parseType=\"Resource\">\n<pdfaProperty:name>{}</pdfaProperty:name>\n<pdfaProperty:valueType>Text</pdfaProperty:valueType>\n"
        "<pdfaProperty:category>external</pdfaProperty:category>\n<pdfaProperty:description>{}</pdfaProperty:description>\n</rdf:li>\n",
        property, description);
  }
  return std::format(
      "<rdf:Description rdf:about=\"\" xmlns:pdfaExtension=\"http://www.aiim.org/pdfa/ns/extension/\" "
      "xmlns:pdfaSchema=\"http://www.aiim.org/pdfa/ns/schema#\" xmlns:pdfaProperty=\"http://www.aiim.org/pdfa/ns/property#\">\n"
      "<pdfaExtension:schemas>\n<rdf:Bag>\n<rdf:li rdf:parseType=\"Resource\">\n"
      "<pdfaSchema:schema>Factur-X PDFA Extension Schema</pdfaSchema:schema>\n"
      "<pdfaSchema:namespaceURI>{}</pdfaSchema:namespaceURI>\n<pdfaSchema:prefix>fx</pdfaSchema:prefix>\n"
      "<pdfaSchema:property>\n<rdf:Seq>\n{}</rdf:Seq>\n</pdfaSchema:property>\n</rdf:li>\n</rdf:Bag>\n</pdfaExtension:schemas>\n</rdf:Description>\n",
      kNamespace, properties);
}

// The packet with the Factur-X properties (replacing any written before) and, unless declared already,
// the extension schema; a new packet when there was none. nullopt for a packet without </rdf:RDF>.
std::optional<std::string> with_facturx(std::string xmp, std::string_view level) {
  if (xmp.empty()) {
    return std::format(
        "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n{}{}</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>",
        fx_description(level), fx_extension());
  }
  // Descriptions holding fx properties go (they are written again).
  std::size_t at = 0;
  for (std::size_t blocks = 0; blocks < kMaxBlocks; ++blocks) {
    const std::size_t begin = xmp.find("<rdf:Description", at);
    if (begin == std::string::npos) {
      break;
    }
    const std::size_t tag_end = xmp.find('>', begin);
    if (tag_end == std::string::npos) {
      break;
    }
    std::size_t end = tag_end + 1;
    if (xmp[tag_end - 1] != '/') {
      const std::size_t close = xmp.find("</rdf:Description>", tag_end);
      if (close == std::string::npos) {
        break;
      }
      end = close + std::string_view("</rdf:Description>").size();
    }
    const std::string_view block = std::string_view(xmp).substr(begin, end - begin);
    const bool fx_properties = block.find(kNamespace) != std::string_view::npos && block.find("pdfaSchema:namespaceURI") == std::string_view::npos &&
                               block.find("fx:") != std::string_view::npos;
    if (fx_properties) {
      xmp.erase(begin, end - begin);
      at = begin;
    } else {
      at = end;
    }
  }
  const std::size_t rdf_end = xmp.rfind("</rdf:RDF>");
  if (rdf_end == std::string::npos) {
    return std::nullopt;
  }
  const bool declared = xmp.find(std::format("<pdfaSchema:namespaceURI>{}</pdfaSchema:namespaceURI>", kNamespace)) != std::string::npos;
  xmp.insert(rdf_end, fx_description(level) + (declared ? std::string() : fx_extension()));
  return xmp;
}

}  // namespace

namespace detail {

std::optional<std::string> with_facturx_properties(std::string xmp, std::string_view level) { return with_facturx(std::move(xmp), level); }

}  // namespace detail

Result<std::string> facturx_profile(std::span<const std::byte> xml) {
  const std::string_view text = text_of(xml.first(std::min(xml.size(), kMaxXml)));
  const std::size_t root = text.find("CrossIndustryInvoice");
  if (root > 4096) {  // npos too
    return make_error(ErrorCode::kUnsupported, "not a Cross Industry Invoice (the XML of Factur-X)");
  }
  const std::size_t context = text.find("GuidelineSpecifiedDocumentContextParameter");
  const auto id = context == std::string_view::npos ? std::nullopt : element_text(text, "ID", context, 4096);
  if (!id) {
    return make_error(ErrorCode::kUnsupported, "the invoice does not say its guideline (GuidelineSpecifiedDocumentContextParameter)");
  }
  // The guideline identifiers of Factur-X 1.0 (and ZUGFeRD 2.x, which shares them).
  const std::string guideline = lower(*id);
  if (guideline.find("xrechnung") != std::string::npos) {
    return std::string("XRECHNUNG");
  }
  if (guideline.ends_with("factur-x.eu:1p0:minimum")) {
    return std::string("MINIMUM");
  }
  if (guideline.ends_with("factur-x.eu:1p0:basicwl")) {
    return std::string("BASIC WL");
  }
  if (guideline.ends_with("factur-x.eu:1p0:basic")) {
    return std::string("BASIC");
  }
  if (guideline.find("factur-x.eu:1p0:extended") != std::string::npos) {
    return std::string("EXTENDED");
  }
  if (guideline == "urn:cen.eu:en16931:2017") {
    return std::string("EN 16931");
  }
  return make_error(ErrorCode::kUnsupported, "unknown guideline " + *id);
}

Result<std::optional<FacturxInfo>> find_facturx(Document& document) {
  auto list = list_attachments(document);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  const AttachmentInfo* invoice = nullptr;
  for (const AttachmentInfo& a : *list) {
    if (a.page || std::ranges::find(kInvoiceNames, lower(a.file_name)) == kInvoiceNames.end()) {
      continue;
    }
    if (invoice == nullptr || (a.associated && !invoice->associated)) {
      invoice = &a;
    }
  }
  if (invoice == nullptr) {
    return std::optional<FacturxInfo>();
  }
  const std::string xmp = catalog_xmp(document);
  return std::optional<FacturxInfo>(FacturxInfo{.spec = invoice->spec,
                                                .file_name = invoice->file_name,
                                                .conformance_level = xmp_value(xmp, "fx:ConformanceLevel"),
                                                .version = xmp_value(xmp, "fx:Version"),
                                                .pdfa3 = declares_pdfa3(xmp)});
}

Result<FacturxResult> embed_facturx(Document& document, std::span<const std::byte> xml, const std::string& date) {
  if (auto sound = detail::require_sound_structure(document, "the invoice"); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  if (date.empty()) {
    // PDF/A-3 asks every associated file for its modification date (/Params /ModDate); the engine reads
    // no clock (determinism): the caller gives it.
    return make_error(ErrorCode::kUnsupported, "the invoice needs a modification date");
  }
  if (xml.size() > kMaxXml) {
    return make_error(ErrorCode::kLimitExceeded, "the invoice's XML is too large");
  }
  auto level = facturx_profile(xml);
  if (!level) {
    return std::unexpected(std::move(level.error()));
  }
  // Everything that can fail is worked out before the document changes.
  const std::string current = catalog_xmp(document);
  const auto xmp = with_facturx(current, *level);
  if (!xmp) {
    return make_error(ErrorCode::kUnsupported, "the document's XMP metadata cannot be read (no </rdf:RDF>)");
  }
  auto list = list_attachments(document);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  for (const AttachmentInfo& a : *list) {
    if (!a.page && std::ranges::find(kInvoiceNames, lower(a.file_name)) != kInvoiceNames.end()) {
      if (auto removed = remove_attachment(document, a.spec); !removed) {
        return std::unexpected(std::move(removed.error()));
      }
    }
  }
  // MINIMUM and BASIC WL are not invoices in law: their XML is data; the others are the invoice (§6.1).
  AttachmentSpec spec;
  spec.file_name = std::string(kFileName);
  spec.description = "Factur-X invoice";
  spec.mime_type = "text/xml";
  spec.relationship = *level == "MINIMUM" || *level == "BASIC WL" ? "Data" : "Alternative";
  spec.creation_date = date;
  spec.modification_date = date;
  auto ref = add_attachment(document, spec, xml);
  if (!ref) {
    return std::unexpected(std::move(ref.error()));
  }
  // The XMP packet: written over the catalog's metadata stream, or a new one.
  model::Stream metadata;
  metadata.dict.set("Type", name("Metadata"));
  metadata.dict.set("Subtype", name("XML"));
  std::vector<std::byte> data(xmp->size());
  std::ranges::transform(*xmp, data.begin(), [](char c) { return static_cast<std::byte>(c); });
  metadata.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(data));
  auto catalog = document.catalog();
  const Object* root = document.trailer().find("Root");
  const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
  if (!catalog || !root_ref) {
    return make_error(ErrorCode::kSyntax, "the document catalog cannot be read");
  }
  // Written over the metadata stream only when the catalog's reference reaches it (§7.3.10).
  const Object* entry = (*catalog)->find("Metadata");
  auto found = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  const std::optional<model::ObjectRef> metadata_ref = entry != nullptr && found && found->as_stream() != nullptr ? entry->as_ref() : std::nullopt;
  if (metadata_ref) {
    if (auto set = document.set(metadata_ref->number, Object(std::move(metadata))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  } else {
    auto added = document.add(Object(std::move(metadata)));
    if (!added) {
      return std::unexpected(std::move(added.error()));
    }
    Dict updated = **catalog;
    updated.set("Metadata", *added);
    if (auto set = document.set(root_ref->number, Object(std::move(updated))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  }
  return FacturxResult{.spec = *ref, .conformance_level = *level, .pdfa3 = declares_pdfa3(current)};
}

}  // namespace capriole::edit
