#include "capriole/edit/dss.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/crypto/revocation.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"
#include "capriole/signature/signatures.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "common.hpp"
#include "sound.hpp"

namespace capriole::edit {

namespace {

using Bytes = std::vector<std::byte>;
using model::Array;
using model::Dict;
using model::Object;

// A /DSS holds at most this many of each kind (what a document can need, with room to spare).
constexpr std::size_t kMaxItems = 512;

// What answers for a certificate issued by issuer: OCSP responses and CRLs that say good or revoked.
bool answers(const Bytes& certificate, const Bytes& issuer, const std::vector<const Bytes*>& ocsps, const std::vector<const Bytes*>& crls) {
  const auto usable = [](const Result<crypto::RevocationInfo>& r) { return r && r->status != crypto::RevocationStatus::kUnknown; };
  return std::ranges::any_of(ocsps, [&](const Bytes* o) { return usable(crypto::check_ocsp(*o, certificate, issuer)); }) ||
         std::ranges::any_of(crls, [&](const Bytes* c) { return usable(crypto::check_crl(*c, certificate, issuer)); });
}

std::vector<const Bytes*> pointers(std::initializer_list<const std::vector<Bytes>*> lists) {
  std::vector<const Bytes*> out;
  for (const auto* list : lists) {
    for (const Bytes& b : *list) {
      out.push_back(&b);
    }
  }
  return out;
}

// The chains a signature rests on, with the known certificates (its own and the pool's): the signer's, and
// its time stamp authority's when it has one; each the signer's first, the root (self-signed) last
// when it is known.
std::vector<signature::Chain> chains_for(const signature::SignatureParts& part, const std::vector<Bytes>& pool) {
  std::vector<signature::Chain> out;
  for (const auto& [certificates, signer] : {std::pair{&part.certificates, part.signer}, std::pair{&part.timestamp_certificates, part.timestamp_signer}}) {
    if (!signer || *signer >= certificates->size()) {
      continue;
    }
    std::vector<Bytes> known = *certificates;
    known.insert(known.end(), pool.begin(), pool.end());
    out.push_back(signature::chain_of((*certificates)[*signer], known));
  }
  return out;
}

// One kind of item of the /DSS: what it held (references, bytes), and what is added.
struct Items {
  Array refs;
  std::vector<Bytes> bytes;
};

Result<Items> items_of(Document& document, const Dict* dss, std::string_view key) {
  Items out;
  const Object* entry = dss != nullptr ? dss->find(key) : nullptr;
  auto list = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!list || list->as_array() == nullptr) {
    return out;
  }
  for (const Object& item : *list->as_array()) {
    const auto ref = item.as_ref();
    auto value = document.resolve(item);
    if (!ref || !value || value->as_stream() == nullptr || out.refs.size() >= kMaxItems) {
      continue;  // what cannot be read is left out (it would not help a verifier either)
    }
    auto data = document.decode_stream(*value->as_stream());
    if (!data) {
      continue;
    }
    out.refs.emplace_back(*ref);
    out.bytes.push_back(std::move(*data));
  }
  return out;
}

// Adds each of more not already there, as a stream of its own.
Result<void> add_items(Document& document, Items& items, const std::vector<Bytes>& more) {
  for (const Bytes& b : more) {
    if (std::ranges::find(items.bytes, b) != items.bytes.end()) {
      continue;
    }
    if (items.bytes.size() >= kMaxItems) {
      return make_error(ErrorCode::kLimitExceeded, "too much validation data for one document");
    }
    model::Stream stream;
    stream.memory_data = std::make_shared<const std::vector<std::byte>>(b);
    auto ref = document.add(Object(std::move(stream)));
    if (!ref) {
      return std::unexpected(std::move(ref.error()));
    }
    items.refs.emplace_back(*ref);
    items.bytes.push_back(b);
  }
  return {};
}

std::string upper_hex(std::span<const std::byte> bytes) {
  std::string out;
  for (const std::byte b : bytes) {
    out += std::format("{:02X}", static_cast<unsigned>(b));
  }
  return out;
}

}  // namespace

Result<std::vector<NeededCertificate>> validation_needs(Document& document, const signature::ValidationData& known) {
  auto parts = signature::signature_parts(document);
  auto file = parts ? signature::document_validation_data(document) : Result<signature::ValidationData>(std::unexpected(parts.error()));
  if (!file) {
    return std::unexpected(std::move(file.error()));
  }
  std::vector<Bytes> pool = file->certificates;
  pool.insert(pool.end(), known.certificates.begin(), known.certificates.end());
  const auto ocsps = pointers({&file->ocsp_responses, &known.ocsp_responses});
  const auto crls = pointers({&file->crls, &known.crls});

  std::vector<NeededCertificate> out;
  for (const signature::SignatureParts& part : *parts) {
    for (const signature::Chain& chain : chains_for(part, pool)) {
      for (std::size_t i = 0; i < chain.certificates.size(); ++i) {
        const Bytes& certificate = chain.certificates[i];
        if ((chain.complete && i + 1 == chain.certificates.size()) ||
            std::ranges::any_of(out, [&](const NeededCertificate& n) { return n.certificate == certificate; })) {
          continue;  // the root; one already listed
        }
        NeededCertificate need;
        need.certificate = certificate;
        auto info = crypto::certificate_info(certificate);
        need.subject = info ? info->common_name : std::string();
        auto urls = crypto::certificate_urls(certificate);
        need.urls = urls ? std::move(*urls) : crypto::CertificateUrls{};
        if (i + 1 < chain.certificates.size()) {
          need.issuer = chain.certificates[i + 1];
          auto request = crypto::ocsp_request(certificate, *need.issuer);
          need.ocsp_request = request ? std::move(*request) : Bytes();
          need.answered = answers(certificate, *need.issuer, ocsps, crls);
        }
        out.push_back(std::move(need));
      }
    }
  }
  return out;
}

Result<ValidationResult> add_validation_data(Document& document, const signature::ValidationData& data, const std::string& date) {
  if (auto sound = detail::require_sound_structure(document, "validation data"); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  for (const Bytes& c : data.certificates) {
    if (!crypto::certificate_info(c)) {
      return make_error(ErrorCode::kSyntax, "validation data: an item given as a certificate is not one");
    }
  }
  for (const Bytes& o : data.ocsp_responses) {
    if (auto produced = crypto::ocsp_produced_at(o); !produced) {
      return make_error(ErrorCode::kSyntax, std::format("validation data: {}", produced.error().message));
    }
  }
  for (const Bytes& c : data.crls) {
    if (auto updated = crypto::crl_this_update(c); !updated) {
      return make_error(ErrorCode::kSyntax, std::format("validation data: {}", updated.error().message));
    }
  }
  auto parts = signature::signature_parts(document);
  if (!parts) {
    return std::unexpected(std::move(parts.error()));
  }
  if (parts->empty()) {
    return make_error(ErrorCode::kUnsupported, "the document has no signature to keep validation data for");
  }
  const Object* root_entry = document.trailer().find("Root");
  const auto root = root_entry != nullptr ? root_entry->as_ref() : std::nullopt;
  auto catalog = document.catalog();
  if (!root || !catalog) {
    return make_error(ErrorCode::kSyntax, "the document has no catalog");
  }
  const Object* dss_entry = (*catalog)->find("DSS");
  auto dss_value = dss_entry != nullptr ? document.resolve(*dss_entry) : Result<Object>(Object());
  const Dict* old_dss = dss_value && dss_value->as_dict() != nullptr ? dss_value->as_dict() : nullptr;
  Dict dss = old_dss != nullptr ? *old_dss : Dict();

  auto certs = items_of(document, old_dss, "Certs");
  auto ocsps = certs ? items_of(document, old_dss, "OCSPs") : Result<Items>(std::unexpected(certs.error()));
  auto crls = ocsps ? items_of(document, old_dss, "CRLs") : Result<Items>(std::unexpected(ocsps.error()));
  if (!crls) {
    return std::unexpected(std::move(crls.error()));
  }
  // Every signature's chain goes in too: a verifier needs it whole, the signature may not have it.
  std::vector<Bytes> pool = certs->bytes;
  pool.insert(pool.end(), data.certificates.begin(), data.certificates.end());
  std::vector<std::vector<signature::Chain>> chains;
  std::vector<Bytes> chain_certificates;
  for (const signature::SignatureParts& part : *parts) {
    chains.push_back(chains_for(part, pool));
    for (const signature::Chain& chain : chains.back()) {
      chain_certificates.insert(chain_certificates.end(), chain.certificates.begin(), chain.certificates.end());
    }
  }
  using Addition = std::pair<Items*, const std::vector<Bytes>*>;
  for (const auto& [items, more] :
       {Addition{&*certs, &chain_certificates}, Addition{&*certs, &data.certificates}, Addition{&*ocsps, &data.ocsp_responses}, Addition{&*crls, &data.crls}}) {
    if (auto added = add_items(document, *items, *more); !added) {
      return std::unexpected(std::move(added.error()));
    }
  }

  // /VRI: per signature, what is about its chain.
  Dict vri;
  if (const Object* old_vri = dss.find("VRI")) {
    if (auto v = document.resolve(*old_vri); v && v->as_dict() != nullptr) {
      vri = *v->as_dict();
    }
  }
  for (std::size_t s = 0; s < parts->size(); ++s) {
    // An entry the file has stays as it is: verifiers take a changed one for tampering.
    const std::string key = upper_hex(crypto::Digest::of(crypto::DigestAlgorithm::kSha1, (*parts)[s].contents));
    if (chains[s].empty() || vri.find(key) != nullptr) {
      continue;
    }
    Array cert_refs;
    Array ocsp_refs;
    Array crl_refs;
    for (const signature::Chain& chain : chains[s]) {
      for (std::size_t i = 0; i < chain.certificates.size(); ++i) {
        const auto at = std::ranges::find(certs->bytes, chain.certificates[i]);
        if (at != certs->bytes.end() &&
            std::ranges::find(cert_refs, Object(certs->refs[static_cast<std::size_t>(at - certs->bytes.begin())])) == cert_refs.end()) {
          cert_refs.push_back(certs->refs[static_cast<std::size_t>(at - certs->bytes.begin())]);
        }
        if (i + 1 >= chain.certificates.size()) {
          continue;
        }
        const Bytes& issuer = chain.certificates[i + 1];
        for (std::size_t k = 0; k < ocsps->bytes.size(); ++k) {
          if (auto r = crypto::check_ocsp(ocsps->bytes[k], chain.certificates[i], issuer);
              r && std::ranges::find(ocsp_refs, ocsps->refs[k]) == ocsp_refs.end()) {
            ocsp_refs.push_back(ocsps->refs[k]);
          }
        }
        for (std::size_t k = 0; k < crls->bytes.size(); ++k) {
          if (auto r = crypto::check_crl(crls->bytes[k], chain.certificates[i], issuer); r && std::ranges::find(crl_refs, crls->refs[k]) == crl_refs.end()) {
            crl_refs.push_back(crls->refs[k]);
          }
        }
      }
    }
    Dict entry;
    entry.set("Cert", std::move(cert_refs));
    if (!ocsp_refs.empty()) {
      entry.set("OCSP", std::move(ocsp_refs));
    }
    if (!crl_refs.empty()) {
      entry.set("CRL", std::move(crl_refs));
    }
    vri.set(key, std::move(entry));
  }

  dss.set("Type", detail::name("DSS"));
  dss.set("Certs", certs->refs);
  if (!ocsps->refs.empty()) {
    dss.set("OCSPs", ocsps->refs);
  }
  if (!crls->refs.empty()) {
    dss.set("CRLs", crls->refs);
  }
  dss.set("VRI", std::move(vri));
  if (const auto dss_ref = dss_entry != nullptr ? dss_entry->as_ref() : std::nullopt; dss_ref && old_dss != nullptr) {
    if (auto set = document.set(dss_ref->number, Object(std::move(dss))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  } else {
    auto ref = document.add(Object(std::move(dss)));
    if (!ref) {
      return std::unexpected(std::move(ref.error()));
    }
    Dict updated = **catalog;
    updated.set("DSS", *ref);
    if (auto set = document.set(root->number, Object(std::move(updated))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  }

  writer::MemorySink sink;
  writer::SaveOptions options;
  options.modification_date = date;
  if (auto saved = writer::save_incremental(document, sink, options); !saved) {
    return std::unexpected(std::move(saved.error()));
  }
  return ValidationResult{.file = sink.take(), .certificates = certs->refs.size(), .ocsp_responses = ocsps->refs.size(), .crls = crls->refs.size()};
}

}  // namespace capriole::edit
