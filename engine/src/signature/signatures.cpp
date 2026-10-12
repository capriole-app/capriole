#include "capriole/signature/signatures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "capriole/crypto/revocation.hpp"
#include "capriole/document/document.hpp"
#include "capriole/edit/forms.hpp"
#include "capriole/model/object.hpp"
#include "edit/common.hpp"

namespace capriole::signature {

namespace {

using crypto::DigestAlgorithm;
using model::Dict;
using model::Object;

// /Contents larger than this is refused (a signature is a few kilobytes; with certificates and
// revocation data, tens).
constexpr std::uint64_t kMaxContents = std::uint64_t{8} * 1024 * 1024;
constexpr std::size_t kChunk = std::size_t{64} * 1024;

// A /DSS keeps at most this many of each kind; a chain is at most this long (hostile input).
constexpr std::size_t kMaxValidationItems = 512;
constexpr std::size_t kMaxChain = 10;

using Bytes = std::vector<std::byte>;

// What verifying a signature found that checking its revocation needs.
struct Found {
  std::vector<Bytes> certificates;
  std::optional<std::size_t> signer;
  std::vector<Bytes> archived_ocsps;
  std::vector<Bytes> archived_crls;
  // The signature time stamp's: the authority's certificates, its among them.
  std::vector<Bytes> timestamp_certificates;
  std::optional<std::size_t> timestamp_signer;
};

struct ByteRange {
  std::array<std::uint64_t, 4> v{};
  [[nodiscard]] std::uint64_t gap_begin() const { return v[0] + v[1]; }
  [[nodiscard]] std::uint64_t gap_end() const { return v[2]; }
  [[nodiscard]] std::uint64_t end() const { return v[2] + v[3]; }
};

std::string name_of(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string();
}

std::string bytes_of(const Dict& dict, std::string_view key) {
  const Object* value = dict.find(key);
  return value != nullptr && value->as_string() != nullptr ? value->as_string()->bytes : std::string();
}

Result<ByteRange> byte_range_of(Document& document, const Dict& signature, std::uint64_t file_size) {
  const Object* entry = signature.find("ByteRange");
  auto value = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  const model::Array* array = value ? value->as_array() : nullptr;
  if (array == nullptr || array->size() != 4) {
    return make_error(ErrorCode::kSyntax, "the signature has no /ByteRange of four numbers");
  }
  ByteRange range;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto n = (*array)[i].as_integer();
    if (!n || *n < 0) {
      return make_error(ErrorCode::kSyntax, "the signature's /ByteRange is not four non-negative integers");
    }
    range.v.at(i) = static_cast<std::uint64_t>(*n);
  }
  if (range.v[0] != 0 || range.gap_begin() >= range.gap_end() || range.end() > file_size || range.v[2] < range.v[1]) {
    return make_error(ErrorCode::kSyntax, "the signature's /ByteRange does not fit the file");
  }
  if (range.gap_end() - range.gap_begin() > kMaxContents) {
    return make_error(ErrorCode::kLimitExceeded, "the signature's /Contents is too large");
  }
  return range;
}

Result<std::vector<std::byte>> read_at(ByteSource& source, std::uint64_t offset, std::size_t size) {
  std::vector<std::byte> out(size);
  auto read = source.read(offset, out);
  if (!read) {
    return std::unexpected(std::move(read.error()));
  }
  if (*read != size) {
    return make_error(ErrorCode::kUnexpectedEof, "the file ends before the signature's byte range");
  }
  return out;
}

// The digest of the two ranges, read in pieces.
Result<std::vector<std::byte>> hash_ranges(ByteSource& source, const ByteRange& range, DigestAlgorithm algorithm) {
  crypto::Digest digest(algorithm);
  for (const auto& [offset, length] : {std::pair{range.v[0], range.v[1]}, std::pair{range.v[2], range.v[3]}}) {
    for (std::uint64_t done = 0; done < length;) {
      const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, length - done));
      auto piece = read_at(source, offset + done, size);
      if (!piece) {
        return std::unexpected(std::move(piece.error()));
      }
      digest.update(*piece);
      done += size;
    }
  }
  return digest.finish();
}

// /Contents as the file has it between the ranges: a hexadecimal string, "<…>".
Result<std::vector<std::byte>> contents_of(ByteSource& source, const ByteRange& range) {
  auto gap = read_at(source, range.gap_begin(), static_cast<std::size_t>(range.gap_end() - range.gap_begin()));
  if (!gap) {
    return std::unexpected(std::move(gap.error()));
  }
  if (gap->size() < 2 || gap->front() != std::byte{'<'} || gap->back() != std::byte{'>'}) {
    return make_error(ErrorCode::kSyntax, "the byte range does not leave out exactly the signature's /Contents");
  }
  std::vector<std::byte> out;
  out.reserve(gap->size() / 2);
  int high = -1;
  for (std::size_t i = 1; i + 1 < gap->size(); ++i) {
    const auto c = std::to_integer<char>((*gap)[i]);
    int nibble = -1;
    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      nibble = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') {
      continue;
    } else {
      return make_error(ErrorCode::kSyntax, "the signature's /Contents is not hexadecimal");
    }
    if (high < 0) {
      high = nibble;
    } else {
      out.push_back(static_cast<std::byte>((high << 4) | nibble));
      high = -1;
    }
  }
  return out;
}

// The time stamp of a CMS signature (its unsigned attribute), verified against its signature value.
void read_timestamp(const crypto::CmsVerification& cms, SignatureInfo& info) {
  if (!cms.timestamp_token) {
    return;
  }
  auto token = crypto::verify_cms(*cms.timestamp_token);
  auto ts = token && token->digest_matches && token->signature_valid ? crypto::timestamp_info(*token) : Result<crypto::TimestampInfo>(std::unexpected(Error{}));
  if (ts && ts->imprint == crypto::Digest::of(ts->imprint_algorithm, cms.signature)) {
    info.timestamp = ts->time;
  } else {
    info.timestamp_invalid = true;
  }
}

void read_signer(const std::vector<crypto::der::Bytes>& certificates, std::optional<std::size_t> signer, SignatureInfo& info, const VerifyOptions& options) {
  if (!signer || *signer >= certificates.size()) {
    return;
  }
  if (auto c = crypto::certificate_info(certificates[*signer])) {
    info.signer = std::move(*c);
  }
  if (!options.trust_anchors.empty()) {
    const auto chain = crypto::check_chain(certificates, *signer, options.trust_anchors);
    info.trust = chain.trusted ? Trust::kTrusted : Trust::kUntrusted;
    info.trust_problem = chain.problem;
  }
}

// Verifies one signature dictionary.
void verify(Document& document, const Dict& signature, SignatureInfo& info, const VerifyOptions& options, Found& found) {
  ByteSource& source = document.source();
  auto range = byte_range_of(document, signature, source.size());
  if (!range) {
    info.integrity = range.error().code == ErrorCode::kSyntax ? Integrity::kInvalid : Integrity::kUnsupported;
    info.problem = range.error().message;
    return;
  }
  info.signed_bytes = range->end();
  info.covers_whole_file = range->end() == source.size();
  auto contents = contents_of(source, *range);
  if (!contents) {
    info.integrity = Integrity::kInvalid;
    info.problem = contents.error().message;
    return;
  }
  const auto ranges_digest = [&](DigestAlgorithm a) -> std::vector<std::byte> {
    auto h = hash_ranges(source, *range, a);
    return h ? std::move(*h) : std::vector<std::byte>();
  };

  const std::string& sf = info.sub_filter;
  if (sf == "adbe.x509.rsa_sha1") {
    // Raw PKCS#1 v1.5 (§12.8.3.2): /Contents an OCTET STRING, the signer's certificate first in /Cert.
    const Object* cert_entry = signature.find("Cert");
    auto cert = cert_entry != nullptr ? document.resolve(*cert_entry) : Result<Object>(Object());
    std::vector<crypto::der::Bytes> certificates;
    const auto add = [&](const Object& o) {
      if (o.as_string() != nullptr) {
        const auto b = crypto::der::bytes_of(o.as_string()->bytes);
        certificates.emplace_back(b.begin(), b.end());
      }
    };
    if (cert && cert->as_array() != nullptr) {
      for (const Object& o : *cert->as_array()) {
        add(o);
      }
    } else if (cert) {
      add(*cert);
    }
    crypto::der::Reader reader(*contents);
    auto octets = reader.expect(crypto::der::kOctetString);
    if (certificates.empty() || !octets) {
      info.integrity = Integrity::kInvalid;
      info.problem = "the signature or its certificate cannot be read";
      return;
    }
    for (const DigestAlgorithm a :
         {DigestAlgorithm::kSha1, DigestAlgorithm::kSha256, DigestAlgorithm::kSha384, DigestAlgorithm::kSha512, DigestAlgorithm::kMd5}) {
      if (crypto::verify_pkcs1(certificates[0], a, ranges_digest(a), octets->content)) {
        info.integrity = Integrity::kValid;
        info.digest = a;
        break;
      }
    }
    if (info.integrity != Integrity::kValid) {
      info.integrity = Integrity::kInvalid;
      info.problem = "the signature does not verify (the document was changed after it was signed, or the signature is damaged)";
    }
    read_signer(certificates, 0, info, options);
    found.certificates = std::move(certificates);
    found.signer = 0;
    return;
  }

  const bool detached = sf == "adbe.pkcs7.detached" || sf == "ETSI.CAdES.detached";
  const bool sha1 = sf == "adbe.pkcs7.sha1";
  const bool timestamp = sf == "ETSI.RFC3161";
  if (!detached && !sha1 && !timestamp) {
    info.integrity = Integrity::kUnsupported;
    info.problem = sf.empty() ? "the signature has no /SubFilter" : std::format("signature format {} not supported", sf);
    return;
  }
  auto cms = crypto::verify_cms(*contents, detached ? crypto::ContentDigest(ranges_digest) : nullptr);
  if (!cms) {
    info.integrity = cms.error().code == ErrorCode::kUnsupported ? Integrity::kUnsupported : Integrity::kInvalid;
    info.problem = cms.error().message;
    return;
  }
  info.digest = cms->digest;
  bool content_ok = cms->digest_matches;
  if (sha1) {
    // The encapsulated content is the SHA-1 digest of the ranges (§12.8.3.3.1).
    content_ok = content_ok && cms->content && *cms->content == ranges_digest(DigestAlgorithm::kSha1);
  } else if (timestamp) {
    auto ts = crypto::timestamp_info(*cms);
    content_ok = content_ok && ts && ts->imprint == ranges_digest(ts->imprint_algorithm);
    if (ts) {
      info.timestamp = ts->time;
    }
  }
  if (content_ok && cms->signature_valid) {
    info.integrity = Integrity::kValid;
  } else {
    info.integrity = Integrity::kInvalid;
    info.problem = !content_ok ? "the document was changed after it was signed (its digest is not the one signed)" : cms->problem;
  }
  if (!timestamp) {
    read_timestamp(*cms, info);
  }
  read_signer(cms->certificates, cms->signer, info, options);
  found.certificates = cms->certificates;
  found.signer = cms->signer;
  found.archived_ocsps = cms->archived_ocsps;
  found.archived_crls = cms->archived_crls;
  if (cms->timestamp_token) {
    if (auto token = crypto::verify_cms(*cms->timestamp_token)) {
      found.timestamp_certificates = std::move(token->certificates);
      found.timestamp_signer = token->signer;
    }
  }
  if (sf == "ETSI.CAdES.detached" && cms->signing_certificate) {
    info.level = info.timestamp ? "B-T" : "B-B";
  }
}

// What the answers say of one certificate, issued by issuer: the first revoked, else the first good.
struct Answer {
  crypto::RevocationStatus status = crypto::RevocationStatus::kUnknown;
  std::string revoked_at;
  bool in_file = false;
};

Answer answer_for(const Bytes& certificate, const Bytes& issuer, const std::vector<const Bytes*>& ocsps, const std::vector<const Bytes*>& crls,
                  std::size_t from_file_ocsps, std::size_t from_file_crls) {
  Answer best;
  const auto take = [&](const Result<crypto::RevocationInfo>& r, bool in_file) {
    if (!r || r->status == crypto::RevocationStatus::kUnknown) {
      return false;
    }
    if (r->status == crypto::RevocationStatus::kRevoked) {
      best = {.status = r->status, .revoked_at = r->revoked_at, .in_file = in_file};
      return true;
    }
    if (best.status != crypto::RevocationStatus::kGood || (in_file && !best.in_file)) {
      best = {.status = r->status, .revoked_at = {}, .in_file = in_file};
    }
    return false;
  };
  for (std::size_t i = 0; i < ocsps.size(); ++i) {
    if (take(crypto::check_ocsp(*ocsps[i], certificate, issuer), i < from_file_ocsps)) {
      return best;
    }
  }
  for (std::size_t i = 0; i < crls.size(); ++i) {
    if (take(crypto::check_crl(*crls[i], certificate, issuer), i < from_file_crls)) {
      return best;
    }
  }
  return best;
}

void check_revocation(const Found& found, const ValidationData& file, const VerifyOptions& options, const std::vector<Bytes>& anchors, SignatureInfo& info) {
  if (!found.signer || *found.signer >= found.certificates.size()) {
    return;
  }
  // The file's answers first (long-term validation rests on them), then the host's.
  std::vector<const Bytes*> ocsps;
  std::vector<const Bytes*> crls;
  for (const auto* list : {&file.ocsp_responses, &found.archived_ocsps}) {
    for (const Bytes& b : *list) {
      ocsps.push_back(&b);
    }
  }
  for (const auto* list : {&file.crls, &found.archived_crls}) {
    for (const Bytes& b : *list) {
      crls.push_back(&b);
    }
  }
  const std::size_t file_ocsps = ocsps.size();
  const std::size_t file_crls = crls.size();
  for (const Bytes& b : options.ocsp_responses) {
    ocsps.push_back(&b);
  }
  for (const Bytes& b : options.crls) {
    crls.push_back(&b);
  }
  if (ocsps.empty() && crls.empty()) {
    return;
  }
  std::vector<Bytes> known = found.certificates;
  for (const auto* list : {&file.certificates, &options.certificates, &anchors}) {
    known.insert(known.end(), list->begin(), list->end());
  }
  const Chain chain = chain_of(found.certificates[*found.signer], known);
  info.revocation = Revocation::kGood;
  info.revocation_in_file = true;
  for (std::size_t i = 0; i < chain.certificates.size(); ++i) {
    const Bytes& certificate = chain.certificates[i];
    if (std::ranges::find(anchors, certificate) != anchors.end() || (chain.complete && i + 1 == chain.certificates.size())) {
      break;  // a trusted root, or the root: nothing to ask about it
    }
    const auto name = [&] {
      auto c = crypto::certificate_info(certificate);
      return c ? c->common_name : std::string("a certificate");
    };
    if (i + 1 >= chain.certificates.size()) {
      info.revocation = Revocation::kUnknown;
      info.revocation_problem = std::format("the issuer of {} is not known: its revocation cannot be checked", name());
      info.revocation_in_file = false;
      return;
    }
    const Answer a = answer_for(certificate, chain.certificates[i + 1], ocsps, crls, file_ocsps, file_crls);
    if (a.status == crypto::RevocationStatus::kRevoked) {
      // A verified time stamp from before the revocation: the signature came first, and stands.
      if (info.timestamp && !a.revoked_at.empty() && *info.timestamp < a.revoked_at) {
        info.revocation_problem = std::format("{} was revoked on {}, after the signature's time stamp", name(), a.revoked_at);
        info.revocation_in_file = info.revocation_in_file && a.in_file;
        continue;
      }
      info.revocation = Revocation::kRevoked;
      info.revoked_at = a.revoked_at;
      info.revocation_problem = std::format("{} is revoked (since {})", name(), a.revoked_at.empty() ? std::string("a time not said") : a.revoked_at);
      info.revocation_in_file = false;
      return;
    }
    if (a.status != crypto::RevocationStatus::kGood && info.revocation == Revocation::kGood) {
      info.revocation = Revocation::kUnknown;
      info.revocation_problem = std::format("no OCSP response or CRL that can be used says whether {} is revoked", name());
    }
    info.revocation_in_file = info.revocation_in_file && a.in_file;
  }
  if (info.revocation != Revocation::kGood) {
    info.revocation_in_file = false;
  }
}

}  // namespace

Result<ValidationData> document_validation_data(Document& document) {
  ValidationData out;
  auto catalog = document.catalog();
  if (!catalog) {
    return std::unexpected(std::move(catalog.error()));
  }
  const Object* entry = (*catalog)->find("DSS");
  auto dss = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!dss || dss->as_dict() == nullptr) {
    return out;
  }
  for (const auto& [key, to] : {std::pair{"Certs", &out.certificates}, std::pair{"OCSPs", &out.ocsp_responses}, std::pair{"CRLs", &out.crls}}) {
    const Object* list_entry = dss->as_dict()->find(key);
    auto list = list_entry != nullptr ? document.resolve(*list_entry) : Result<Object>(Object());
    if (!list || list->as_array() == nullptr) {
      continue;
    }
    for (const Object& item : *list->as_array()) {
      if (to->size() >= kMaxValidationItems) {
        break;
      }
      auto value = document.resolve(item);
      if (!value || value->as_stream() == nullptr) {
        continue;
      }
      if (auto data = document.decode_stream(*value->as_stream())) {
        to->push_back(std::move(*data));
      }
    }
  }
  return out;
}

Chain chain_of(std::span<const std::byte> certificate, const std::vector<std::vector<std::byte>>& known) {
  Chain chain;
  chain.certificates.emplace_back(certificate.begin(), certificate.end());
  while (chain.certificates.size() < kMaxChain) {
    const Bytes& current = chain.certificates.back();
    if (crypto::issued_by(current, current)) {
      chain.complete = true;
      break;
    }
    const auto issuer = std::ranges::find_if(known, [&](const Bytes& k) {
      return k != current && std::ranges::find(chain.certificates, k) == chain.certificates.end() && crypto::issued_by(current, k);
    });
    if (issuer == known.end()) {
      break;
    }
    chain.certificates.push_back(*issuer);
  }
  return chain;
}

namespace {

// The signatures, verified; and what verifying found, with each one's /Contents, when asked.
Result<std::vector<SignatureInfo>> walk(Document& document, const VerifyOptions& options, std::vector<SignatureParts>* parts) {
  auto fields = edit::list_fields(document);
  if (!fields) {
    return std::unexpected(std::move(fields.error()));
  }
  std::vector<SignatureInfo> out;
  auto file_data = document_validation_data(document);
  const ValidationData file = file_data ? std::move(*file_data) : ValidationData{};
  const std::vector<Bytes> anchors = options.trust_anchors.empty() ? std::vector<Bytes>() : crypto::certificates_of(options.trust_anchors);
  for (const edit::FieldInfo& field : *fields) {
    if (field.type != edit::FieldType::kSignature) {
      continue;
    }
    auto loaded = document.get(field.ref);
    const Dict* dict = loaded && (*loaded)->as_dict() != nullptr ? (*loaded)->as_dict() : nullptr;
    const Object* v = dict != nullptr ? dict->find("V") : nullptr;
    auto value = v != nullptr ? document.resolve(*v) : Result<Object>(Object());
    const Dict* signature = value ? value->as_dict() : nullptr;
    if (v == nullptr || v->is_null()) {
      continue;  // not signed yet
    }
    SignatureInfo info;
    info.field = field.ref;
    info.field_name = field.name;
    if (signature == nullptr) {
      // A value that is not a dictionary that can be read: signed, but not verifiable.
      info.integrity = Integrity::kInvalid;
      info.problem = "the signature dictionary cannot be read";
      out.push_back(std::move(info));
      continue;
    }
    info.sub_filter = name_of(*signature, "SubFilter");
    info.document_timestamp = name_of(*signature, "Type") == "DocTimeStamp" || info.sub_filter == "ETSI.RFC3161";
    info.name = edit::detail::utf8_of(*signature, "Name");
    info.reason = edit::detail::utf8_of(*signature, "Reason");
    info.location = edit::detail::utf8_of(*signature, "Location");
    info.contact_info = edit::detail::utf8_of(*signature, "ContactInfo");
    info.date = bytes_of(*signature, "M");
    for (const edit::FieldWidget& widget : field.widgets) {
      if (widget.page && widget.rect.width() > 0 && widget.rect.height() > 0) {
        info.page = widget.page;
        info.rect = widget.rect;
        break;
      }
    }
    Found found;
    verify(document, *signature, info, options, found);
    info.signing_time = info.timestamp.value_or(info.date);
    if (info.signer && !info.signing_time.empty()) {
      info.certificate_valid_at_signing = crypto::valid_at(*info.signer, info.signing_time);
    }
    if (info.integrity != Integrity::kUnsupported) {
      check_revocation(found, file, options, anchors, info);
    }
    if (parts != nullptr) {
      const Object* contents = signature->find("Contents");
      const auto bytes =
          contents != nullptr && contents->as_string() != nullptr ? crypto::der::bytes_of(contents->as_string()->bytes) : std::span<const std::byte>();
      parts->push_back({.field_name = info.field_name,
                        .contents = {bytes.begin(), bytes.end()},
                        .certificates = std::move(found.certificates),
                        .signer = found.signer,
                        .timestamp_certificates = std::move(found.timestamp_certificates),
                        .timestamp_signer = found.timestamp_signer,
                        .document_timestamp = info.document_timestamp});
    }
    out.push_back(std::move(info));
  }
  // PAdES levels that depend on the rest of the file: validation data (/DSS), and a document time stamp
  // after it (§5 of ETSI EN 319 142-1).
  auto catalog = document.catalog();
  const bool dss = catalog && (*catalog)->find("DSS") != nullptr;
  for (SignatureInfo& info : out) {
    if (info.level.empty() || !dss) {
      continue;
    }
    info.level = "B-LT";
    if (std::ranges::any_of(out, [&](const SignatureInfo& other) {
          return other.document_timestamp && other.integrity == Integrity::kValid && other.signed_bytes > info.signed_bytes;
        })) {
      info.level = "B-LTA";
    }
  }
  return out;
}

}  // namespace

Result<std::vector<SignatureInfo>> list_signatures(Document& document, const VerifyOptions& options) { return walk(document, options, nullptr); }

Result<std::vector<SignatureParts>> signature_parts(Document& document) {
  std::vector<SignatureParts> parts;
  if (auto listed = walk(document, {}, &parts); !listed) {
    return std::unexpected(std::move(listed.error()));
  }
  return parts;
}

}  // namespace capriole::signature
