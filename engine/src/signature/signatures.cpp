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
void verify(Document& document, const Dict& signature, SignatureInfo& info, const VerifyOptions& options) {
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
  if (sf == "ETSI.CAdES.detached" && cms->signing_certificate) {
    info.level = info.timestamp ? "B-T" : "B-B";
  }
}

}  // namespace

Result<std::vector<SignatureInfo>> list_signatures(Document& document, const VerifyOptions& options) {
  auto fields = edit::list_fields(document);
  if (!fields) {
    return std::unexpected(std::move(fields.error()));
  }
  std::vector<SignatureInfo> out;
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
    verify(document, *signature, info, options);
    info.signing_time = info.timestamp.value_or(info.date);
    if (info.signer && !info.signing_time.empty()) {
      info.certificate_valid_at_signing = crypto::valid_at(*info.signer, info.signing_time);
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

}  // namespace capriole::signature
