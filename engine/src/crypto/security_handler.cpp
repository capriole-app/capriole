#include "capriole/crypto/security_handler.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

#include "capriole/crypto/cipher.hpp"
#include "capriole/crypto/hash.hpp"

namespace capriole::crypto {

namespace {

using Bytes = std::vector<std::byte>;

// The padding string of Algorithm 2, step a (§7.6.4.3.2).
constexpr std::array<std::uint8_t, 32> kPadding = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                                   0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
constexpr std::size_t kMaxPasswordR6 = 127;

std::span<const std::byte> as_bytes(std::string_view text) noexcept { return std::as_bytes(std::span(text.data(), text.size())); }

// The 5 bytes of an object's number and generation, low byte first, that key derivation hashes (Algorithm 1).
std::array<std::byte, 5> object_id(model::ObjectRef ref) noexcept {
  return {static_cast<std::byte>(ref.number & 0xFFU), static_cast<std::byte>((ref.number >> 8U) & 0xFFU), static_cast<std::byte>((ref.number >> 16U) & 0xFFU),
          static_cast<std::byte>(ref.generation & 0xFFU), static_cast<std::byte>((ref.generation >> 8U) & 0xFFU)};
}

Bytes concat(std::initializer_list<std::span<const std::byte>> parts) {
  Bytes out;
  for (const auto part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

// Password padded or truncated to exactly 32 bytes (Algorithm 2, step a).
std::array<std::byte, 32> pad_password(std::span<const std::byte> password) {
  std::array<std::byte, 32> out{};
  const std::size_t n = std::min<std::size_t>(password.size(), 32);
  std::copy_n(password.begin(), n, out.begin());
  for (std::size_t i = n; i < 32; ++i) {
    out[i] = static_cast<std::byte>(kPadding[i - n]);
  }
  return out;
}

std::array<std::byte, 4> le32(std::int32_t value) {
  const auto u = static_cast<std::uint32_t>(value);
  return {static_cast<std::byte>(u & 0xFFU), static_cast<std::byte>((u >> 8U) & 0xFFU), static_cast<std::byte>((u >> 16U) & 0xFFU),
          static_cast<std::byte>((u >> 24U) & 0xFFU)};
}

Bytes xor_key(std::span<const std::byte> key, std::uint8_t value) {
  Bytes out(key.begin(), key.end());
  for (std::byte& b : out) {
    b ^= static_cast<std::byte>(value);
  }
  return out;
}

Result<std::string_view> string_entry(const model::Dict& dict, std::string_view key, std::size_t min_size) {
  const model::Object* value = dict.find(key);
  const model::String* string = value != nullptr ? value->as_string() : nullptr;
  if (string == nullptr || string->bytes.size() < min_size) {
    return make_error(ErrorCode::kSyntax, "/Encrypt /" + std::string(key) + " must be a string of at least " + std::to_string(min_size) + " bytes");
  }
  return std::string_view(string->bytes).substr(0, min_size);
}

// ── Revisions 2-4 (Algorithms 2, 3, 4, 5, 7) ────────────────────────────────────────────────

struct LegacyParams {
  int revision;
  std::size_t key_length;
  std::string_view o;
  std::string_view u;
  std::int32_t p;
  std::span<const std::byte> id;
  bool encrypt_metadata;
};

// Algorithm 2: file key from a padded user password.
Bytes legacy_file_key(const std::array<std::byte, 32>& padded, const LegacyParams& params) {
  // Callers validate /V against /R; this keeps a mistake there from reading past the digest.
  if (params.key_length > Md5::kDigestSize) {
    return {};
  }
  Md5 md5_hash;
  md5_hash.update(padded);
  md5_hash.update(as_bytes(params.o));
  md5_hash.update(le32(params.p));
  md5_hash.update(params.id);
  if (params.revision >= 4 && !params.encrypt_metadata) {
    const std::array<std::byte, 4> all_ones = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    md5_hash.update(all_ones);
  }
  auto digest = md5_hash.finish();
  if (params.revision >= 3) {
    for (int i = 0; i < 50; ++i) {
      digest = md5(std::span(digest).first(params.key_length));
    }
  }
  return {digest.begin(), digest.begin() + static_cast<std::ptrdiff_t>(params.key_length)};
}

// Algorithms 4 and 5: does key reproduce /U?
bool legacy_user_matches(const Bytes& key, const LegacyParams& params) {
  const auto u = as_bytes(params.u);
  if (params.revision == 2) {
    std::array<std::byte, 32> data{};
    for (std::size_t i = 0; i < 32; ++i) {
      data[i] = static_cast<std::byte>(kPadding[i]);
    }
    Rc4(key).apply(data);
    return std::ranges::equal(data, u.first(32));
  }
  Md5 md5_hash;
  std::array<std::byte, 32> padding{};
  for (std::size_t i = 0; i < 32; ++i) {
    padding[i] = static_cast<std::byte>(kPadding[i]);
  }
  md5_hash.update(padding);
  md5_hash.update(params.id);
  auto x = md5_hash.finish();
  Rc4(key).apply(x);
  for (std::uint8_t i = 1; i <= 19; ++i) {
    Rc4(xor_key(key, i)).apply(x);
  }
  return std::ranges::equal(x, u.first(16));
}

// Algorithm 7, steps a-b: the user password recovered from /O with an owner password.
std::array<std::byte, 32> legacy_user_password_from_owner(std::span<const std::byte> owner_password, const LegacyParams& params) {
  if (params.key_length > Md5::kDigestSize) {
    return {};
  }
  auto digest = md5(pad_password(owner_password));
  if (params.revision >= 3) {
    for (int i = 0; i < 50; ++i) {
      digest = md5(digest);
    }
  }
  const Bytes key(digest.begin(), digest.begin() + static_cast<std::ptrdiff_t>(params.key_length));
  std::array<std::byte, 32> x{};
  std::ranges::copy(as_bytes(params.o).first(32), x.begin());
  if (params.revision == 2) {
    Rc4(key).apply(x);
  } else {
    for (int i = 19; i >= 0; --i) {
      Rc4(xor_key(key, static_cast<std::uint8_t>(i))).apply(x);
    }
  }
  return x;
}

// ── Revisions 5-6 (Algorithm 2.A) ───────────────────────────────────────────────────────────

Bytes hash_r5_r6(int revision, std::span<const std::byte> password, std::span<const std::byte> salt, std::span<const std::byte> udata) {
  if (revision == 5) {
    const auto digest = sha256(concat({password, salt, udata}));
    return {digest.begin(), digest.end()};
  }
  return hash_r6(password, salt, udata);
}

// The file key from an intermediate key and /UE or /OE (AES-256, zero IV, no padding).
Result<Bytes> unwrap_file_key(std::span<const std::byte> intermediate, std::span<const std::byte> wrapped) {
  auto aes = Aes::create(intermediate);
  if (!aes) {
    return std::unexpected(std::move(aes.error()));
  }
  return aes_cbc_decrypt(*aes, Aes::Block{}, wrapped, false);
}

Result<CryptMethod> method_from_cfm(const model::Dict& filter) {
  const model::Object* cfm = filter.find("CFM");
  if (cfm == nullptr || cfm->is_name("None")) {
    return make_error(ErrorCode::kUnsupported, "crypt filter method /None (application-defined decryption)");
  }
  if (cfm->is_name("V2")) {
    return CryptMethod::kRc4;
  }
  if (cfm->is_name("AESV2")) {
    return CryptMethod::kAesV2;
  }
  if (cfm->is_name("AESV3")) {
    return CryptMethod::kAesV3;
  }
  return make_error(ErrorCode::kUnsupported, "unknown crypt filter method");
}

std::optional<std::int64_t> integer(const model::Dict& dict, std::string_view key) {
  const model::Object* value = dict.find(key);
  return value != nullptr ? value->as_integer() : std::nullopt;
}

}  // namespace

std::vector<std::byte> hash_r6(std::span<const std::byte> password, std::span<const std::byte> salt, std::span<const std::byte> udata) {
  const auto initial = sha256(concat({password, salt, udata}));
  Bytes k(initial.begin(), initial.end());
  for (int round = 0;;) {
    Bytes k1;
    const Bytes unit = concat({password, k, udata});
    k1.reserve(unit.size() * 64);
    for (int i = 0; i < 64; ++i) {
      k1.insert(k1.end(), unit.begin(), unit.end());
    }
    Aes::Block iv{};
    std::copy_n(k.begin() + 16, 16, iv.begin());
    const auto aes = Aes::create(std::span(k).first(16));
    const Bytes e = *aes_cbc_encrypt(*aes, iv, k1);  // k1 is 64 × (a whole number of units): always block-aligned

    unsigned remainder = 0;
    for (std::size_t i = 0; i < 16; ++i) {
      remainder += std::to_integer<unsigned>(e[i]);
    }
    switch (remainder % 3) {
      case 0: {
        const auto d = sha256(e);
        k.assign(d.begin(), d.end());
        break;
      }
      case 1: {
        const auto d = sha384(e);
        k.assign(d.begin(), d.end());
        break;
      }
      default: {
        const auto d = sha512(e);
        k.assign(d.begin(), d.end());
        break;
      }
    }
    ++round;
    if (round >= 64 && std::to_integer<int>(e.back()) <= round - 32) {
      break;
    }
  }
  k.resize(32);
  return k;
}

Result<StandardSecurityHandler> StandardSecurityHandler::create(const model::Dict& encrypt, std::span<const std::byte> first_id, std::string_view password) {
  const model::Object* filter = encrypt.find("Filter");
  if (filter == nullptr || !filter->is_name("Standard")) {
    return make_error(ErrorCode::kUnsupported, "only the standard security handler is supported (not public-key or custom handlers)");
  }
  const auto v = integer(encrypt, "V").value_or(0);
  const auto r = integer(encrypt, "R");
  const auto p = integer(encrypt, "P");
  if (!r || !p) {
    return make_error(ErrorCode::kSyntax, "/Encrypt needs integers /R and /P");
  }
  if (v != 1 && v != 2 && v != 4 && v != 5) {
    return make_error(ErrorCode::kUnsupported, "/Encrypt /V " + std::to_string(v) + " is not supported");
  }
  if (*r < 2 || *r > 6) {
    return make_error(ErrorCode::kUnsupported, "/Encrypt /R " + std::to_string(*r) + " is not supported");
  }
  // /V fixes the key length and /R the algorithm that must produce it: they come in pairs (§7.6.4.2,
  // Table 21). A mismatch such as /V 5 (32-byte key) with /R 3 (MD5, 16-byte digest) is not only
  // invalid but would ask the MD5 path for more key bytes than a digest holds.
  const bool consistent = (v == 1 && *r == 2) || (v == 2 && (*r == 2 || *r == 3)) || (v == 4 && *r == 4) || (v == 5 && (*r == 5 || *r == 6));
  if (!consistent) {
    return make_error(ErrorCode::kSyntax, "/Encrypt /V " + std::to_string(v) + " cannot be used with /R " + std::to_string(*r));
  }
  // /P is a 32-bit two's complement value; some writers store it as unsigned.
  if (*p < std::numeric_limits<std::int32_t>::min() || std::cmp_greater(*p, std::numeric_limits<std::uint32_t>::max())) {
    return make_error(ErrorCode::kSyntax, "/Encrypt /P out of range");
  }

  StandardSecurityHandler handler;
  handler.version_ = static_cast<int>(v);
  handler.revision_ = static_cast<int>(*r);
  handler.permissions_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(*p));
  if (const model::Object* em = encrypt.find("EncryptMetadata"); em != nullptr && v >= 4) {
    handler.encrypt_metadata_ = em->as_bool().value_or(true);
  }

  // Key length in bytes.
  std::size_t key_length = 5;
  if (v == 2 || v == 4) {
    const auto bits = integer(encrypt, "Length").value_or(v == 4 ? 128 : 40);
    if (bits < 40 || bits > 128 || bits % 8 != 0) {
      return make_error(ErrorCode::kSyntax, "/Encrypt /Length must be a multiple of 8 from 40 to 128");
    }
    key_length = static_cast<std::size_t>(bits / 8);
  } else if (v == 5) {
    key_length = 32;
  }

  // Crypt methods: fixed RC4 for V 1-2, named crypt filters for V 4-5 (§7.6.5).
  if (v <= 2) {
    handler.string_method_ = CryptMethod::kRc4;
    handler.stream_method_ = CryptMethod::kRc4;
  } else {
    if (const model::Object* cf = encrypt.find("CF"); cf != nullptr && cf->as_dict() != nullptr) {
      handler.crypt_filters_ = *cf->as_dict();
    }
    const auto name_of = [&](std::string_view key) -> std::string {
      const model::Object* value = encrypt.find(key);
      return value != nullptr && value->as_name() != nullptr ? value->as_name()->bytes : std::string("Identity");
    };
    auto stream_method = handler.crypt_filter(name_of("StmF"));
    auto string_method = handler.crypt_filter(name_of("StrF"));
    if (!stream_method) {
      return std::unexpected(std::move(stream_method.error()));
    }
    if (!string_method) {
      return std::unexpected(std::move(string_method.error()));
    }
    handler.stream_method_ = *stream_method;
    handler.string_method_ = *string_method;
  }

  const auto password_bytes = as_bytes(password);
  if (handler.revision_ <= 4) {
    auto o = string_entry(encrypt, "O", 32);
    auto u = string_entry(encrypt, "U", 32);
    if (!o || !u) {
      return std::unexpected(std::move(!o ? o.error() : u.error()));
    }
    const LegacyParams params{.revision = handler.revision_,
                              .key_length = key_length,
                              .o = *o,
                              .u = *u,
                              .p = handler.permissions_,
                              .id = first_id,
                              .encrypt_metadata = handler.encrypt_metadata_};
    Bytes key = legacy_file_key(pad_password(password_bytes), params);
    if (!legacy_user_matches(key, params)) {
      key = legacy_file_key(legacy_user_password_from_owner(password_bytes, params), params);
      if (!legacy_user_matches(key, params)) {
        return make_error(ErrorCode::kPasswordRequired, "the password does not open this document");
      }
      handler.owner_ = true;
    }
    handler.file_key_ = std::move(key);
    return handler;
  }

  // Revisions 5 and 6.
  auto o = string_entry(encrypt, "O", 48);
  auto u = string_entry(encrypt, "U", 48);
  auto oe = string_entry(encrypt, "OE", 32);
  auto ue = string_entry(encrypt, "UE", 32);
  if (!o || !u || !oe || !ue) {
    return make_error(ErrorCode::kSyntax, "/Encrypt /O, /U (48 bytes) and /OE, /UE (32 bytes) are required for R5 and R6");
  }
  const auto pw = password_bytes.first(std::min(password_bytes.size(), kMaxPasswordR6));
  const auto ob = as_bytes(*o);
  const auto ub = as_bytes(*u);

  // Owner first, as Algorithm 2.A prescribes.
  if (std::ranges::equal(hash_r5_r6(handler.revision_, pw, ob.subspan(32, 8), ub), ob.first(32))) {
    auto key = unwrap_file_key(hash_r5_r6(handler.revision_, pw, ob.subspan(40, 8), ub), as_bytes(*oe));
    if (!key) {
      return std::unexpected(std::move(key.error()));
    }
    handler.file_key_ = std::move(*key);
    handler.owner_ = true;
    return handler;
  }
  if (std::ranges::equal(hash_r5_r6(handler.revision_, pw, ub.subspan(32, 8), {}), ub.first(32))) {
    auto key = unwrap_file_key(hash_r5_r6(handler.revision_, pw, ub.subspan(40, 8), {}), as_bytes(*ue));
    if (!key) {
      return std::unexpected(std::move(key.error()));
    }
    handler.file_key_ = std::move(*key);
    return handler;
  }
  return make_error(ErrorCode::kPasswordRequired, "the password does not open this document");
}

Result<CryptMethod> StandardSecurityHandler::crypt_filter(std::string_view name) const {
  if (name == "Identity") {
    return CryptMethod::kIdentity;
  }
  const model::Object* entry = crypt_filters_.find(name);
  const model::Dict* dict = entry != nullptr ? entry->as_dict() : nullptr;
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "crypt filter /" + std::string(name) + " is not defined in /CF");
  }
  auto method = method_from_cfm(*dict);
  if (method && *method == CryptMethod::kAesV3 && version_ != 5) {
    return make_error(ErrorCode::kSyntax, "/AESV3 crypt filter requires /V 5");
  }
  return method;
}

Bytes StandardSecurityHandler::object_key(model::ObjectRef ref, CryptMethod method) const {
  // Algorithm 1: per-object key for RC4 and AESV2; AESV3 uses the file key as is.
  if (method == CryptMethod::kAesV3) {
    return file_key_;
  }
  Md5 md5_hash;
  md5_hash.update(file_key_);
  md5_hash.update(object_id(ref));
  if (method == CryptMethod::kAesV2) {
    md5_hash.update(as_bytes("sAlT"));
  }
  const auto digest = md5_hash.finish();
  return {digest.begin(), digest.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(file_key_.size() + 5, 16))};
}

Result<std::vector<std::byte>> StandardSecurityHandler::decrypt(std::span<const std::byte> data, model::ObjectRef ref, CryptMethod method) const {
  if (method == CryptMethod::kIdentity) {
    return Bytes(data.begin(), data.end());
  }
  const Bytes key = object_key(ref, method);
  if (method == CryptMethod::kRc4) {
    Bytes out(data.begin(), data.end());
    Rc4(key).apply(out);
    return out;
  }

  // AES: a 16-byte initialisation vector, then CBC blocks with PKCS#7 padding (§7.6.3.1).
  if (data.empty() || data.size() == Aes::kBlockSize) {
    return Bytes{};  // nothing, or an IV alone: an empty string
  }
  if (data.size() % Aes::kBlockSize != 0) {
    return make_error(ErrorCode::kCorruptData, "AES-encrypted data is not a whole number of blocks");
  }
  auto aes = Aes::create(key);
  if (!aes) {
    return std::unexpected(std::move(aes.error()));
  }
  Aes::Block iv{};
  std::copy_n(data.begin(), Aes::kBlockSize, iv.begin());
  return aes_cbc_decrypt(*aes, iv, data.subspan(Aes::kBlockSize), true);
}

Result<std::vector<std::byte>> StandardSecurityHandler::encrypt(std::span<const std::byte> data, model::ObjectRef ref, CryptMethod method) const {
  if (method == CryptMethod::kIdentity) {
    return Bytes(data.begin(), data.end());
  }
  const Bytes key = object_key(ref, method);
  if (method == CryptMethod::kRc4) {
    Bytes out(data.begin(), data.end());
    Rc4(key).apply(out);
    return out;
  }

  // AES: the IV, then the data with PKCS#7 padding (always 1..16 bytes) in CBC mode (§7.6.3.1).
  // The IV is derived — SHA-256 of the object key, the object and the data — instead of random, so
  // that the output stays deterministic (project principle 6). Without the key it is as unpredictable
  // as a random one; the only thing it gives away is that the same data in the same object encrypts
  // the same way.
  Sha256 iv_hash;
  iv_hash.update(key);
  iv_hash.update(object_id(ref));
  iv_hash.update(data);
  const auto digest = iv_hash.finish();
  Aes::Block iv{};
  std::copy_n(digest.begin(), Aes::kBlockSize, iv.begin());

  auto aes = Aes::create(key);
  if (!aes) {
    return std::unexpected(std::move(aes.error()));
  }
  Bytes padded(data.begin(), data.end());
  const std::size_t padding = Aes::kBlockSize - (data.size() % Aes::kBlockSize);
  padded.insert(padded.end(), padding, static_cast<std::byte>(padding));
  auto encrypted = aes_cbc_encrypt(*aes, iv, padded);
  if (!encrypted) {
    return std::unexpected(std::move(encrypted.error()));
  }
  Bytes out(iv.begin(), iv.end());
  out.insert(out.end(), encrypted->begin(), encrypted->end());
  return out;
}

Result<CryptMethod> StandardSecurityHandler::method_for_stream(const model::Dict& stream_dict) const {
  const model::Object* type = stream_dict.find("Type");
  if (type != nullptr && type->is_name("XRef")) {
    return CryptMethod::kIdentity;  // cross-reference streams are never encrypted (§7.5.8.1)
  }
  if (type != nullptr && type->is_name("Metadata") && !encrypt_metadata_) {
    return CryptMethod::kIdentity;
  }
  // A /Crypt filter first in the chain selects the crypt filter for this stream (§7.4.10).
  // /Filter and /DecodeParms are a single value or an array of them: the first element either way.
  const auto first_of = [&](std::string_view key) -> const model::Object* {
    const model::Object* value = stream_dict.find(key);
    if (value == nullptr || value->as_array() == nullptr) {
      return value;
    }
    return value->as_array()->empty() ? nullptr : &value->as_array()->front();
  };
  const model::Object* first_filter = first_of("Filter");
  if (first_filter != nullptr && first_filter->is_name("Crypt")) {
    const model::Object* first_parms = first_of("DecodeParms");
    const model::Object* name = first_parms != nullptr && first_parms->as_dict() != nullptr ? first_parms->as_dict()->find("Name") : nullptr;
    return crypt_filter(name != nullptr && name->as_name() != nullptr ? name->as_name()->bytes : "Identity");
  }
  return stream_method_;
}

}  // namespace capriole::crypto
