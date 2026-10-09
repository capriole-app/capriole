#include "capriole/edit/sign.hpp"

#include <algorithm>
#include <cmath>
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
#include "capriole/model/text_string.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "common.hpp"
#include "sound.hpp"

namespace capriole::edit {

namespace {

using detail::hex;
using detail::name;
using detail::num;
using graphics::Rect;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// Annotation flags (§12.5.3): Print, Locked.
constexpr std::int64_t kPrintLocked = (1 << 2) | (1 << 7);
// /SigFlags (§12.7.3): SignaturesExist, AppendOnly.
constexpr std::int64_t kSigFlags = 3;
// What /ByteRange holds until the file is signed: four numbers wide enough for any offset of a file
// below 10 GB, replaced in place (padded with spaces).
constexpr std::int64_t kPlaceholder = 9'999'999'999;
// Room for a time stamp token (its TSA's certificates included).
constexpr std::size_t kTimestampRoom = std::size_t{16} * 1024;

model::String text(std::string_view utf8) { return model::String{.bytes = model::encode_text_string(model::from_utf8(utf8))}; }

std::string replaced(std::string line, std::string_view key, std::string_view value) {
  for (std::size_t at = line.find(key); at != std::string::npos; at = line.find(key, at + value.size())) {
    line.replace(at, key.size(), value);
  }
  return line;
}

// "D:20261004143000+02'00'" as "2026-10-04 14:30" (the time as written).
std::string shown_date(std::string_view date) {
  if (date.starts_with("D:")) {
    date.remove_prefix(2);
  }
  if (date.size() < 12) {
    return std::string(date);
  }
  return std::format("{}-{}-{} {}:{}", date.substr(0, 4), date.substr(4, 2), date.substr(6, 2), date.substr(8, 2), date.substr(10, 2));
}

// The appearance's content, in the page's coordinates as shown: the lines, as large as fits (12 pt at
// most), from the top left, inside a thin frame.
std::string appearance(const Rect& r, const std::vector<std::string>& lines) {
  constexpr double kPadding = 3;
  std::vector<std::string> codes;
  codes.reserve(lines.size());
  for (const std::string& line : lines) {
    codes.push_back(detail::win_ansi_bytes(model::from_utf8(line)));
  }
  const double width = std::max(r.width() - (2 * kPadding), 1.0);
  const double height = std::max(r.height() - (2 * kPadding), 1.0);
  double size = std::min(12.0, height / (std::max<double>(static_cast<double>(codes.size()), 1) * 1.2));
  for (const std::string& line : codes) {
    const double w = detail::text_width(line, size, detail::helvetica());
    if (w > width) {
      size *= width / w;
    }
  }
  size = std::max(size, 1.0);
  std::string c = std::format("0.5 G 0.5 w {} {} {} {} re S\n", num(r.x0 + 0.25), num(r.y0 + 0.25), num(r.width() - 0.5), num(r.height() - 0.5));
  c += std::format("{} {} {} {} re W n\nBT /Helv {} Tf 0 g\n", num(r.x0), num(r.y0), num(r.width()), num(r.height()), num(size));
  double baseline = r.y0 + kPadding + (size * 0.9);
  for (const std::string& line : codes) {
    c += std::format("1 0 0 -1 {} {} Tm {} Tj\n", num(r.x0 + kPadding), num(baseline), hex(line));
    baseline += size * 1.2;
  }
  return c + "ET\n";
}

// The first of Signature1, Signature2… no field of the form is named, or is under.
std::string free_name(const std::vector<FieldInfo>& fields) {
  for (int n = 1;; ++n) {
    const std::string candidate = std::format("Signature{}", n);
    if (std::ranges::none_of(fields, [&](const FieldInfo& f) { return f.name == candidate || f.name.starts_with(candidate + "."); })) {
      return candidate;
    }
  }
}

// The widget added to the form's /Fields, /SigFlags set; the form made when the document has none.
Result<void> add_to_form(Document& document, ObjectRef widget) {
  const Object* root_entry = document.trailer().find("Root");
  const auto root = root_entry != nullptr ? root_entry->as_ref() : std::nullopt;
  auto catalog = document.catalog();
  if (!root || !catalog) {
    return make_error(ErrorCode::kSyntax, "the document has no catalog to add a form to");
  }
  const Object* form_entry = (*catalog)->find("AcroForm");
  auto form_value = form_entry != nullptr ? document.resolve(*form_entry) : Result<Object>(Object());
  const bool has_form = form_value && form_value->as_dict() != nullptr;
  Dict form = has_form ? *form_value->as_dict() : Dict();
  // /Fields: in the form, or its own object.
  const Object* fields_entry = form.find("Fields");
  auto fields_value = fields_entry != nullptr ? document.resolve(*fields_entry) : Result<Object>(Object());
  Array fields = fields_value && fields_value->as_array() != nullptr ? *fields_value->as_array() : Array();
  fields.emplace_back(widget);
  const auto fields_ref = fields_entry != nullptr ? fields_entry->as_ref() : std::nullopt;
  if (fields_ref && fields_value && fields_value->as_array() != nullptr) {
    if (auto set = document.set(fields_ref->number, Object(std::move(fields))); !set) {
      return set;
    }
  } else {
    form.set("Fields", std::move(fields));
  }
  const auto flags = form.find("SigFlags") != nullptr ? form.find("SigFlags")->as_integer() : std::nullopt;
  form.set("SigFlags", Object(flags.value_or(0) | kSigFlags));
  if (const auto form_ref = has_form ? form_entry->as_ref() : std::nullopt) {
    return document.set(form_ref->number, Object(std::move(form)));
  }
  Dict updated = **catalog;
  if (has_form) {
    updated.set("AcroForm", std::move(form));
  } else {
    auto ref = document.add(Object(std::move(form)));
    if (!ref) {
      return std::unexpected(std::move(ref.error()));
    }
    updated.set("AcroForm", *ref);
  }
  return document.set(root->number, Object(std::move(updated)));
}

// Where a placeholder is in what was appended: the one place after the original bytes.
std::optional<std::size_t> find_once(std::string_view file, std::string_view what, std::size_t from) {
  const std::size_t at = file.find(what, from);
  if (at == std::string_view::npos || file.find(what, at + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  return at;
}

}  // namespace

Result<PreparedSignature> prepare_signature(Document& document, const SignerCertificates& signer_certificates, const SignRequest& request) {
  if (document.security() != nullptr) {
    return make_error(ErrorCode::kUnsupported, "signing an encrypted document is not supported yet");
  }
  if (auto sound = detail::require_sound_structure(document, "signatures"); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  if (request.date.empty() || !crypto::der::generalized_time(request.date)) {
    return make_error(ErrorCode::kUnsupported, "a signature needs its date (a PDF date)");
  }
  auto signer = crypto::certificate_info(signer_certificates.certificate);
  if (!signer) {
    return std::unexpected(std::move(signer.error()));
  }
  auto page = detail::page_at(document, request.page, "signatures");
  if (!page) {
    return std::unexpected(std::move(page.error()));
  }
  auto fields = list_fields(document);
  if (!fields) {
    return std::unexpected(std::move(fields.error()));
  }
  const std::string field_name = request.field_name.empty() ? free_name(*fields) : request.field_name;
  if (std::ranges::any_of(*fields, [&](const FieldInfo& f) { return f.name == field_name; })) {
    return make_error(ErrorCode::kUnsupported, std::format("the form already has a field named {}", field_name));
  }

  // The room /Contents needs: the signature with the longest value the key can give, with some margin.
  const std::vector<std::byte> zero(32);
  auto trial = crypto::unsigned_detached(signer_certificates.certificate, signer_certificates.others, zero);
  if (!trial) {
    return std::unexpected(std::move(trial.error()));
  }
  trial->signature.assign(crypto::longest_value(signer_certificates.certificate), std::byte{0xFF});
  const std::size_t capacity = crypto::encode(*trial).size() + 64 + (request.timestamp ? kTimestampRoom : 0);

  // The appearance: lines in the rectangle as shown, in page space (/BBox is /Rect); none for an invisible one.
  Rect rect{};
  std::string content;
  model::Dict resources;
  if (request.rect) {
    const Rect& r = *request.rect;
    if (!std::isfinite(r.x0) || !std::isfinite(r.y0) || !std::isfinite(r.x1) || !std::isfinite(r.y1) || r.width() < 1 || r.height() < 1) {
      return make_error(ErrorCode::kUnsupported, "a visible signature needs a rectangle");
    }
    rect = page->user(r);
    std::vector<std::string> lines = request.lines;
    if (lines.empty()) {
      lines = {"Digitally signed by {signer}", std::format("Date: {}", shown_date(request.date))};
    }
    for (std::string& line : lines) {
      line = replaced(std::move(line), "{signer}", signer->common_name);
    }
    content = detail::in_view(*page, appearance(r, lines));
    resources.set("Font", detail::helvetica_font());
  }
  auto ap = document.add(Object(detail::form_xobject(rect, std::move(resources), content)));
  if (!ap) {
    return std::unexpected(std::move(ap.error()));
  }

  Dict signature;
  signature.set("Type", name("Sig"));
  signature.set("Filter", name("Adobe.PPKLite"));
  signature.set("SubFilter", name("ETSI.CAdES.detached"));
  signature.set("ByteRange", Array{Object(std::int64_t{0}), Object(kPlaceholder), Object(kPlaceholder), Object(kPlaceholder)});
  signature.set("Contents", model::String{.bytes = std::string(capacity, '\0'), .form = model::StringForm::kHex});
  signature.set("M", model::String{.bytes = request.date});
  signature.set("Name", text(request.name.empty() ? signer->common_name : request.name));
  for (const auto& [key, value] :
       {std::pair{"Reason", &request.reason}, std::pair{"Location", &request.location}, std::pair{"ContactInfo", &request.contact_info}}) {
    if (!value->empty()) {
      signature.set(key, text(*value));
    }
  }
  auto signature_ref = document.add(Object(std::move(signature)));
  if (!signature_ref) {
    return std::unexpected(std::move(signature_ref.error()));
  }

  // The field and its widget, merged (§12.7.4.5).
  Dict widget;
  widget.set("Type", name("Annot"));
  widget.set("Subtype", name("Widget"));
  widget.set("FT", name("Sig"));
  widget.set("T", text(field_name));
  widget.set("V", *signature_ref);
  widget.set("F", Object(kPrintLocked));
  widget.set("P", page->ref);
  widget.set("Rect", detail::rect_array(rect));
  Dict appearances;
  appearances.set("N", *ap);
  widget.set("AP", std::move(appearances));
  auto widget_ref = document.add(Object(std::move(widget)));
  if (!widget_ref) {
    return std::unexpected(std::move(widget_ref.error()));
  }
  detail::Annots annots = detail::annots_of(document, *page);
  annots.list.emplace_back(*widget_ref);
  if (auto written = detail::write_annots(document, *page, std::move(annots)); !written) {
    return std::unexpected(std::move(written.error()));
  }
  if (auto added = add_to_form(document, *widget_ref); !added) {
    return std::unexpected(std::move(added.error()));
  }

  // The revision, then where its placeholders landed.
  const std::uint64_t original = document.source().size();
  writer::MemorySink sink;
  writer::SaveOptions options;
  options.modification_date = request.date;
  if (auto saved = writer::save_incremental(document, sink, options); !saved) {
    return std::unexpected(std::move(saved.error()));
  }
  PreparedSignature out;
  out.file = sink.take();
  out.field_name = field_name;
  const std::string_view file = crypto::der::chars_of(out.file);
  // The original bytes start the output, which is in memory: their count fits.
  if (original > out.file.size()) {
    return make_error(ErrorCode::kUnsupported, "the saved revision is shorter than the document");
  }
  const auto original_size = static_cast<std::size_t>(original);
  const auto contents = find_once(file, "<" + std::string(capacity * 2, '0') + ">", original_size);
  const std::string placeholder = std::format("{} {} {}]", kPlaceholder, kPlaceholder, kPlaceholder);
  const auto range_end = find_once(file, placeholder, original_size);
  const std::size_t range_begin = range_end ? file.rfind('[', *range_end) : std::string_view::npos;
  if (!contents || !range_end || range_begin == std::string_view::npos) {
    return make_error(ErrorCode::kUnsupported, "the signature's room was not found in the saved revision");
  }
  const std::size_t gap_end = *contents + (capacity * 2) + 2;
  const std::string range = std::format("[0 {} {} {}", *contents, gap_end, out.file.size() - gap_end);
  const std::size_t width = *range_end + placeholder.size() - range_begin;
  if (range.size() + 1 > width) {
    return make_error(ErrorCode::kLimitExceeded, "the file is too large to sign");
  }
  const std::string padded = range + std::string(width - range.size() - 1, ' ') + "]";
  std::ranges::copy(std::as_bytes(std::span(padded)), out.file.begin() + static_cast<std::ptrdiff_t>(range_begin));

  crypto::Digest digest(crypto::DigestAlgorithm::kSha256);
  digest.update(std::span(out.file).first(*contents));
  digest.update(std::span(out.file).subspan(gap_end));
  auto cms = crypto::unsigned_detached(signer_certificates.certificate, signer_certificates.others, digest.finish());
  if (!cms) {
    return std::unexpected(std::move(cms.error()));
  }
  out.cms = std::move(*cms);
  out.contents_offset = *contents + 1;
  out.contents_capacity = capacity;
  out.timestamp = request.timestamp;
  return out;
}

Result<PreparedSignature> prepare_signature(Document& document, const crypto::Credentials& credentials, const SignRequest& request) {
  const SignerCertificates certificates{.certificate = {credentials.certificate().begin(), credentials.certificate().end()}, .others = credentials.chain()};
  auto out = prepare_signature(document, certificates, request);
  if (!out) {
    return out;
  }
  if (auto signed_ = crypto::sign_with(out->cms, credentials); !signed_) {
    return std::unexpected(std::move(signed_.error()));
  }
  if (out->timestamp) {
    out->timestamp_request = crypto::timestamp_request(out->cms.signature);
  }
  return out;
}

Result<void> set_signature_value(PreparedSignature& prepared, std::span<const std::byte> value) {
  if (prepared.cms.certificates.empty()) {
    return make_error(ErrorCode::kOutOfRange, "not a prepared signature");
  }
  if (auto set = crypto::set_value(prepared.cms, prepared.cms.certificates.front(), value); !set) {
    return set;
  }
  if (prepared.timestamp) {
    prepared.timestamp_request = crypto::timestamp_request(prepared.cms.signature);
  }
  return {};
}

Result<std::vector<std::byte>> finish_signature(PreparedSignature prepared, std::span<const std::byte> timestamp_response) {
  if (prepared.cms.signature.empty()) {
    return make_error(ErrorCode::kUnsupported, "the signature has no value yet (set_signature_value)");
  }
  std::vector<std::byte> token;
  if (!timestamp_response.empty()) {
    auto t = crypto::timestamp_token(timestamp_response, prepared.cms.signature);
    if (!t) {
      return std::unexpected(std::move(t.error()));
    }
    token = std::move(*t);
  }
  const auto der = crypto::encode(prepared.cms, token);
  if (der.size() > prepared.contents_capacity) {
    return make_error(ErrorCode::kLimitExceeded, "the signature does not fit the room kept for it (a time stamp needs SignRequest::timestamp)");
  }
  if (prepared.contents_offset + (prepared.contents_capacity * 2) > prepared.file.size()) {
    return make_error(ErrorCode::kOutOfRange, "not a prepared signature");
  }
  const std::string digits = hex(crypto::der::chars_of(der));
  // hex() writes "<…>": its digits go in.
  const std::string_view inner = std::string_view(digits).substr(1, digits.size() - 2);
  std::ranges::copy(std::as_bytes(std::span(inner)), prepared.file.begin() + static_cast<std::ptrdiff_t>(prepared.contents_offset));
  return std::move(prepared.file);
}

}  // namespace capriole::edit
