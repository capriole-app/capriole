#include "capriole/crypto/pkcs12.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mbedtls/asn1.h>
#include <mbedtls/cipher.h>
#include <mbedtls/pkcs12.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"
#include "mbed.hpp"
#include "rc2.hpp"

namespace capriole::crypto {

namespace {

using der::Bytes;
using der::Element;
using der::Reader;

// Object identifiers (RFC 7292 Appendix D, RFC 5652, RFC 8018).
constexpr std::string_view kData = "1.2.840.113549.1.7.1";
constexpr std::string_view kEncryptedData = "1.2.840.113549.1.7.6";
constexpr std::string_view kKeyBag = "1.2.840.113549.1.12.10.1.1";
constexpr std::string_view kShroudedKeyBag = "1.2.840.113549.1.12.10.1.2";
constexpr std::string_view kCertBag = "1.2.840.113549.1.12.10.1.3";
constexpr std::string_view kX509Certificate = "1.2.840.113549.1.9.22.1";
constexpr std::string_view kLocalKeyId = "1.2.840.113549.1.9.21";
constexpr std::string_view kPbes2 = "1.2.840.113549.1.5.13";
constexpr std::string_view kPbeSha3Des = "1.2.840.113549.1.12.1.3";
constexpr std::string_view kPbeSha2Des = "1.2.840.113549.1.12.1.4";
constexpr std::string_view kPbeSha128Rc2 = "1.2.840.113549.1.12.1.5";
constexpr std::string_view kPbeSha40Rc2 = "1.2.840.113549.1.12.1.6";
constexpr std::string_view kPbmac1 = "1.2.840.113549.1.5.14";

// Bags hold at most this many items, and a file this many bags (hostile input).
constexpr std::size_t kMaxItems = 256;
// PBKDF/PKCS#12 iterations above this are refused (a file could make reading it take hours).
constexpr std::uint64_t kMaxIterations = 10'000'000;

// Wipes what it holds when it goes (a decrypted key).
struct Secret {
  Bytes bytes;
  Secret() = default;
  explicit Secret(Bytes b) noexcept : bytes(std::move(b)) {}
  Secret(const Secret&) = delete;
  Secret& operator=(const Secret&) = delete;
  Secret(Secret&& other) noexcept : bytes(std::move(other.bytes)) {}
  Secret& operator=(Secret&& other) noexcept {
    wipe();
    bytes = std::move(other.bytes);
    return *this;
  }
  ~Secret() { wipe(); }
  void wipe() noexcept {
    if (!bytes.empty()) {
      mbedtls_platform_zeroize(bytes.data(), bytes.size());
    }
  }
};

std::unexpected<Error> damaged(std::string_view what) { return make_error(ErrorCode::kSyntax, std::format("PKCS#12: {}", what)); }
std::unexpected<Error> wrong_password() { return make_error(ErrorCode::kPasswordRequired, "PKCS#12: wrong password"); }

std::string oid_of(const Element& e) { return e.tag == der::kOid ? der::oid_text(e.content) : std::string(); }

// The password as BMPString with its final 0 (§B.1); none for an empty password, as some tools write.
Secret bmp_password(std::string_view utf8, bool terminated) {
  Secret out;
  std::size_t i = 0;
  while (i < utf8.size()) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    std::uint32_t cp = c;
    std::size_t n = 1;
    if (c >= 0xF0 && i + 3 < utf8.size()) {
      cp = ((c & 0x07U) << 18U) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3FU) << 12U) | ((static_cast<unsigned char>(utf8[i + 2]) & 0x3FU) << 6U) |
           (static_cast<unsigned char>(utf8[i + 3]) & 0x3FU);
      n = 4;
    } else if (c >= 0xE0 && i + 2 < utf8.size()) {
      cp = ((c & 0x0FU) << 12U) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3FU) << 6U) | (static_cast<unsigned char>(utf8[i + 2]) & 0x3FU);
      n = 3;
    } else if (c >= 0xC0 && i + 1 < utf8.size()) {
      cp = ((c & 0x1FU) << 6U) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3FU);
      n = 2;
    }
    i += n;
    const auto put = [&](std::uint32_t unit) {
      out.bytes.push_back(static_cast<std::byte>(unit >> 8U));
      out.bytes.push_back(static_cast<std::byte>(unit & 0xFFU));
    };
    if (cp >= 0x10000) {
      cp -= 0x10000;
      put(0xD800 + (cp >> 10U));
      put(0xDC00 + (cp & 0x3FFU));
    } else {
      put(cp);
    }
  }
  if (terminated) {
    out.bytes.push_back(std::byte{0});
    out.bytes.push_back(std::byte{0});
  }
  return out;
}

// The PKCS#12 key derivation (§B.2): id 1 key, 2 IV, 3 MAC key.
Result<Secret> derive(const Secret& password, std::span<const std::byte> salt, DigestAlgorithm hash, int id, std::uint64_t iterations, std::size_t size) {
  if (iterations == 0 || iterations > kMaxIterations) {
    return make_error(ErrorCode::kLimitExceeded, "PKCS#12: too many iterations");
  }
  Secret out{Bytes(size)};
  if (mbedtls_pkcs12_derivation(mbed::uchars(std::span(out.bytes)), size, mbed::uchars(password.bytes), password.bytes.size(), mbed::uchars(salt), salt.size(),
                                mbed::md_of(hash), id, static_cast<int>(iterations)) != 0) {
    return make_error(ErrorCode::kUnsupported, "PKCS#12: key derivation failed");
  }
  return out;
}

struct PbeParameters {
  Bytes salt;
  std::uint64_t iterations = 1;
};

Result<PbeParameters> pbe_parameters(const Element& params) {
  auto r = Reader::into(params);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  auto salt = r->expect(der::kOctetString);
  auto iterations = salt ? r->expect(der::kInteger) : Result<Element>(std::unexpected(salt.error()));
  if (!iterations) {
    return std::unexpected(std::move(iterations.error()));
  }
  auto count = der::small_integer(*iterations);
  if (!count) {
    return std::unexpected(std::move(count.error()));
  }
  return PbeParameters{.salt = Bytes(salt->content.begin(), salt->content.end()), .iterations = *count};
}

Result<Secret> cipher_decrypt(mbedtls_cipher_type_t type, std::span<const std::byte> key, std::span<const std::byte> iv, std::span<const std::byte> data) {
  mbedtls_cipher_context_t ctx;
  mbedtls_cipher_init(&ctx);
  Secret out{Bytes(data.size() + 32)};
  std::size_t written = 0;
  std::size_t last = 0;
  const mbedtls_cipher_info_t* info = mbedtls_cipher_info_from_type(type);
  const bool ok = info != nullptr && mbedtls_cipher_setup(&ctx, info) == 0 &&
                  mbedtls_cipher_setkey(&ctx, mbed::uchars(key), static_cast<int>(key.size() * 8), MBEDTLS_DECRYPT) == 0 &&
                  mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_PKCS7) == 0 && mbedtls_cipher_set_iv(&ctx, mbed::uchars(iv), iv.size()) == 0 &&
                  mbedtls_cipher_reset(&ctx) == 0 &&
                  mbedtls_cipher_update(&ctx, mbed::uchars(data), data.size(), mbed::uchars(std::span(out.bytes)), &written) == 0 &&
                  mbedtls_cipher_finish(&ctx, mbed::uchars(std::span(out.bytes).subspan(written)), &last) == 0;
  mbedtls_cipher_free(&ctx);
  if (!ok) {
    return wrong_password();  // a bad padding: what a wrong key gives
  }
  out.bytes.resize(written + last);
  return out;
}

// Decrypts what an AlgorithmIdentifier of password-based encryption protects.
Result<Secret> decrypt(const Element& algorithm, std::span<const std::byte> data, std::string_view password) {
  // Nothing encrypted is a damaged file (and Mbed TLS must not be handed an empty buffer: it copies from
  // its pointer, which may be null — found by fuzz_pkcs12).
  if (data.empty()) {
    return damaged("nothing encrypted");
  }
  auto r = Reader::into(algorithm);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  auto id = r->expect(der::kOid);
  if (!id) {
    return std::unexpected(std::move(id.error()));
  }
  const std::string oid = oid_of(*id);
  auto params = r->next();
  if (!params) {
    return std::unexpected(std::move(params.error()));
  }
  if (oid == kPbes2) {
    // RFC 8018 §6.2: PBKDF2 and a cipher, the password as its bytes.
    if (params->tag != der::kSequence) {
      return damaged("bad PBES2 parameters");
    }
    const mbedtls_asn1_buf buf{
        .tag = params->tag,
        .len = params->content.size(),
        .p = const_cast<unsigned char*>(mbed::uchars(params->content))};  // NOLINT(cppcoreguidelines-pro-type-const-cast): Mbed TLS reads it only
    Secret out{Bytes(data.size() + 32)};
    std::size_t written = 0;
    const int rc = mbedtls_pkcs5_pbes2_ext(&buf, MBEDTLS_PKCS5_DECRYPT, mbed::uchars(der::bytes_of(password)), password.size(), mbed::uchars(data), data.size(),
                                           mbed::uchars(std::span(out.bytes)), out.bytes.size(), &written);
    if (rc == MBEDTLS_ERR_PKCS5_FEATURE_UNAVAILABLE) {
      return make_error(ErrorCode::kUnsupported, "PKCS#12: a PBES2 cipher or hash not supported");
    }
    if (rc == MBEDTLS_ERR_PKCS5_BAD_INPUT_DATA || rc == MBEDTLS_ERR_PKCS5_INVALID_FORMAT) {
      return damaged("bad PBES2 parameters");
    }
    if (rc != 0) {
      return wrong_password();
    }
    out.bytes.resize(written);
    return out;
  }
  // The PKCS#12 PBEs (§C): SHA-1, the key and IV derived from the password as BMPString.
  std::size_t key_size = 0;
  bool rc2 = false;
  if (oid == kPbeSha3Des) {
    key_size = 24;
  } else if (oid == kPbeSha2Des) {
    key_size = 16;
  } else if (oid == kPbeSha128Rc2) {
    key_size = 16;
    rc2 = true;
  } else if (oid == kPbeSha40Rc2) {
    key_size = 5;
    rc2 = true;
  } else {
    return make_error(ErrorCode::kUnsupported, std::format("PKCS#12: encryption {} not supported", oid));
  }
  auto pbe = pbe_parameters(*params);
  if (!pbe) {
    return std::unexpected(std::move(pbe.error()));
  }
  // An empty password is taken without its final 0 by some tools: tried both ways.
  std::optional<Error> last;
  for (const bool terminated : {true, false}) {
    if (!terminated && !password.empty()) {
      break;
    }
    const Secret bmp = bmp_password(password, terminated);
    auto key = derive(bmp, pbe->salt, DigestAlgorithm::kSha1, 1, pbe->iterations, key_size);
    auto iv = key ? derive(bmp, pbe->salt, DigestAlgorithm::kSha1, 2, pbe->iterations, 8) : Result<Secret>(std::unexpected(key.error()));
    if (!iv) {
      return std::unexpected(std::move(iv.error()));
    }
    Result<Secret> plain = std::unexpected(Error{});
    if (rc2) {
      auto cipher = Rc2::create(key->bytes, static_cast<unsigned>(key_size * 8));
      if (!cipher) {
        return std::unexpected(std::move(cipher.error()));
      }
      Rc2::Block block{};
      std::ranges::copy(iv->bytes, block.begin());
      auto out = rc2_cbc_decrypt(*cipher, block, data);
      plain = out ? Result<Secret>(Secret(std::move(*out))) : Result<Secret>(wrong_password());
    } else {
      Bytes k = key->bytes;
      if (key_size == 16) {
        k.insert(k.end(), key->bytes.begin(), key->bytes.begin() + 8);  // two-key 3DES: K1 K2 K1
      }
      plain = cipher_decrypt(MBEDTLS_CIPHER_DES_EDE3_CBC, k, iv->bytes, data);
      mbedtls_platform_zeroize(k.data(), k.size());
    }
    if (plain) {
      return plain;
    }
    last = plain.error();
  }
  return std::unexpected(last.value_or(Error{.code = ErrorCode::kPasswordRequired, .message = "PKCS#12: wrong password"}));
}

// The MacData (§4): HMAC over the authenticated safe, its key from the password.
Result<void> check_mac(const Element& mac_data, std::span<const std::byte> auth_safe, std::string_view password) {
  auto r = Reader::into(mac_data);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  auto digest_info = r->expect(der::kSequence);
  auto salt = digest_info ? r->expect(der::kOctetString) : Result<Element>(std::unexpected(digest_info.error()));
  if (!salt) {
    return std::unexpected(std::move(salt.error()));
  }
  std::uint64_t iterations = 1;
  if (auto count = r->next_if(der::kInteger); count && *count) {
    auto n = der::small_integer(**count);
    if (!n) {
      return std::unexpected(std::move(n.error()));
    }
    iterations = *n;
  }
  auto di = Reader::into(*digest_info);
  auto algorithm = di ? di->expect(der::kSequence) : Result<Element>(std::unexpected(di.error()));
  auto expected = algorithm ? di->expect(der::kOctetString) : Result<Element>(std::unexpected(algorithm.error()));
  if (!expected) {
    return std::unexpected(std::move(expected.error()));
  }
  auto ar = Reader::into(*algorithm);
  auto id = ar ? ar->expect(der::kOid) : Result<Element>(std::unexpected(ar.error()));
  if (!id) {
    return std::unexpected(std::move(id.error()));
  }
  if (oid_of(*id) == kPbmac1) {
    return make_error(ErrorCode::kUnsupported, "PKCS#12: PBMAC1 not supported");
  }
  const auto hash = digest_of_oid(oid_of(*id));
  if (!hash) {
    return make_error(ErrorCode::kUnsupported, std::format("PKCS#12: MAC hash {} not supported", oid_of(*id)));
  }
  const std::size_t size = Digest::of(*hash, {}).size();
  for (const bool terminated : {true, false}) {
    if (!terminated && !password.empty()) {
      break;
    }
    auto key = derive(bmp_password(password, terminated), salt->content, *hash, 3, iterations, size);
    if (!key) {
      return std::unexpected(std::move(key.error()));
    }
    auto mac = mbed::hmac(*hash, key->bytes, auth_safe);
    if (!mac) {
      return std::unexpected(std::move(mac.error()));
    }
    if (std::ranges::equal(*mac, expected->content)) {
      return {};
    }
  }
  return wrong_password();
}

struct Bag {
  Bytes local_key_id;
  Secret key;  // PrivateKeyInfo
  Bytes certificate;
};

Bytes local_key_id_of(Reader& bag) {
  auto attributes = bag.next_if(der::kSet);
  if (!attributes || !*attributes) {
    return {};
  }
  auto r = Reader::into(**attributes);
  while (r && !r->empty()) {
    auto attribute = r->next();
    if (!attribute) {
      break;
    }
    auto ar = Reader::into(*attribute);
    auto id = ar ? ar->expect(der::kOid) : Result<Element>(std::unexpected(Error{}));
    auto values = id ? ar->expect(der::kSet) : Result<Element>(std::unexpected(Error{}));
    if (values && oid_of(*id) == kLocalKeyId) {
      auto vr = Reader::into(*values);
      auto value = vr ? vr->expect(der::kOctetString) : Result<Element>(std::unexpected(Error{}));
      if (value) {
        return {value->content.begin(), value->content.end()};
      }
    }
  }
  return {};
}

Result<void> read_safe_contents(std::span<const std::byte> contents, std::string_view password, std::vector<Bag>& bags) {
  Reader top(contents);
  auto seq = top.expect(der::kSequence);
  if (!seq) {
    return std::unexpected(std::move(seq.error()));
  }
  auto r = Reader::into(*seq);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  while (!r->empty()) {
    if (bags.size() >= kMaxItems) {
      return make_error(ErrorCode::kLimitExceeded, "PKCS#12: too many bags");
    }
    auto bag = r->expect(der::kSequence);
    auto br = bag ? Reader::into(*bag) : Result<Reader>(std::unexpected(bag.error()));
    auto id = br ? br->expect(der::kOid) : Result<Element>(std::unexpected(br.error()));
    auto wrapped = id ? br->expect(der::context_constructed(0)) : Result<Element>(std::unexpected(id.error()));
    auto vr = wrapped ? Reader::into(*wrapped) : Result<Reader>(std::unexpected(wrapped.error()));
    auto value = vr ? vr->next() : Result<Element>(std::unexpected(vr.error()));
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    const std::string kind = oid_of(*id);
    Bag item;
    if (kind == kKeyBag) {
      item.key = Secret(Bytes(value->whole.begin(), value->whole.end()));
    } else if (kind == kShroudedKeyBag) {
      // EncryptedPrivateKeyInfo
      auto er = Reader::into(*value);
      auto algorithm = er ? er->expect(der::kSequence) : Result<Element>(std::unexpected(er.error()));
      auto data = algorithm ? er->expect(der::kOctetString) : Result<Element>(std::unexpected(algorithm.error()));
      if (!data) {
        return std::unexpected(std::move(data.error()));
      }
      auto plain = decrypt(*algorithm, data->content, password);
      if (!plain) {
        return std::unexpected(std::move(plain.error()));
      }
      item.key = std::move(*plain);
    } else if (kind == kCertBag) {
      auto cr = Reader::into(*value);
      auto type = cr ? cr->expect(der::kOid) : Result<Element>(std::unexpected(cr.error()));
      if (!type || oid_of(*type) != kX509Certificate) {
        continue;  // an SDSI certificate, a CRL: not used
      }
      auto inner = cr->expect(der::context_constructed(0));
      auto ir = inner ? Reader::into(*inner) : Result<Reader>(std::unexpected(inner.error()));
      auto octets = ir ? ir->expect(der::kOctetString) : Result<Element>(std::unexpected(ir.error()));
      if (!octets) {
        return std::unexpected(std::move(octets.error()));
      }
      item.certificate = Bytes(octets->content.begin(), octets->content.end());
    } else {
      continue;  // CRL, secret and nested bags: not used
    }
    item.local_key_id = local_key_id_of(*br);
    bags.push_back(std::move(item));
  }
  return {};
}

// The content of a ContentInfo of type data or encryptedData (§5.1).
Result<Secret> safe_content(const Element& content_info, std::string_view password) {
  auto r = Reader::into(content_info);
  auto type = r ? r->expect(der::kOid) : Result<Element>(std::unexpected(r.error()));
  auto wrapped = type ? r->expect(der::context_constructed(0)) : Result<Element>(std::unexpected(type.error()));
  auto wr = wrapped ? Reader::into(*wrapped) : Result<Reader>(std::unexpected(wrapped.error()));
  if (!wr) {
    return std::unexpected(std::move(wr.error()));
  }
  const std::string kind = oid_of(*type);
  if (kind == kData) {
    auto octets = wr->next();
    if (!octets || (octets->tag & ~der::kConstructed) != der::kOctetString) {
      return damaged("data is not an OCTET STRING");
    }
    auto bytes = der::string_bytes(*octets, wr->depth());
    if (!bytes) {
      return std::unexpected(std::move(bytes.error()));
    }
    return Secret(std::move(*bytes));
  }
  if (kind != kEncryptedData) {
    return make_error(ErrorCode::kUnsupported, std::format("PKCS#12: content {} not supported (public-key privacy?)", kind));
  }
  // EncryptedData ::= SEQUENCE { version, EncryptedContentInfo { type, algorithm, [0] IMPLICIT content } }
  auto encrypted = wr->expect(der::kSequence);
  auto er = encrypted ? Reader::into(*encrypted) : Result<Reader>(std::unexpected(encrypted.error()));
  auto version = er ? er->expect(der::kInteger) : Result<Element>(std::unexpected(er.error()));
  auto info = version ? er->expect(der::kSequence) : Result<Element>(std::unexpected(version.error()));
  auto ir = info ? Reader::into(*info) : Result<Reader>(std::unexpected(info.error()));
  auto content_type = ir ? ir->expect(der::kOid) : Result<Element>(std::unexpected(ir.error()));
  auto algorithm = content_type ? ir->expect(der::kSequence) : Result<Element>(std::unexpected(content_type.error()));
  auto data = algorithm ? ir->next() : Result<Element>(std::unexpected(algorithm.error()));
  if (!data) {
    return std::unexpected(std::move(data.error()));
  }
  if ((data->tag & ~der::kConstructed) != der::context(0)) {
    return damaged("encrypted content missing");
  }
  auto bytes = der::string_bytes(*data, ir->depth());
  if (!bytes) {
    return std::unexpected(std::move(bytes.error()));
  }
  return decrypt(*algorithm, *bytes, password);
}

}  // namespace

Credentials::~Credentials() {
  if (!key_.empty()) {
    mbedtls_platform_zeroize(key_.data(), key_.size());
  }
}

Result<Credentials> read_pkcs12(std::span<const std::byte> file, std::string_view password) {
  // PFX ::= SEQUENCE { version (3), authSafe ContentInfo (data), macData MacData OPTIONAL }
  Reader top(file);
  auto pfx = top.expect(der::kSequence);
  auto r = pfx ? Reader::into(*pfx) : Result<Reader>(std::unexpected(pfx.error()));
  auto version = r ? r->expect(der::kInteger) : Result<Element>(std::unexpected(r.error()));
  auto auth_safe = version ? r->expect(der::kSequence) : Result<Element>(std::unexpected(version.error()));
  if (!auth_safe) {
    return damaged("not a PKCS#12 file");
  }
  if (der::small_integer(*version) != std::uint64_t{3}) {
    return make_error(ErrorCode::kUnsupported, "PKCS#12: not version 3");
  }
  // The authenticated safe: a ContentInfo of type data holding a SEQUENCE of ContentInfo.
  auto safe = safe_content(*auth_safe, password);
  if (!safe) {
    return std::unexpected(std::move(safe.error()));
  }
  if (auto mac = r->next_if(der::kSequence); mac && *mac) {
    if (auto checked = check_mac(**mac, safe->bytes, password); !checked) {
      return std::unexpected(std::move(checked.error()));
    }
  } else if (!mac) {
    return std::unexpected(std::move(mac.error()));
  }
  Reader safe_reader(safe->bytes);
  auto contents = safe_reader.expect(der::kSequence);
  auto cr = contents ? Reader::into(*contents) : Result<Reader>(std::unexpected(contents.error()));
  if (!cr) {
    return std::unexpected(std::move(cr.error()));
  }
  std::vector<Bag> bags;
  std::size_t count = 0;
  while (!cr->empty()) {
    if (++count > kMaxItems) {
      return make_error(ErrorCode::kLimitExceeded, "PKCS#12: too many parts");
    }
    auto info = cr->expect(der::kSequence);
    if (!info) {
      return std::unexpected(std::move(info.error()));
    }
    auto content = safe_content(*info, password);
    if (!content) {
      return std::unexpected(std::move(content.error()));
    }
    if (auto read = read_safe_contents(content->bytes, password, bags); !read) {
      return std::unexpected(std::move(read.error()));
    }
  }

  // The key, and the certificate that goes with it: the same localKeyId, else the same public key.
  const auto key_bag = std::ranges::find_if(bags, [](const Bag& b) { return !b.key.bytes.empty(); });
  if (key_bag == bags.end()) {
    return make_error(ErrorCode::kOutOfRange, "PKCS#12: no private key in the file");
  }
  mbed::Pk pk;
  if (auto parsed = mbed::parse_private_key(pk, key_bag->key.bytes); !parsed) {
    return std::unexpected(std::move(parsed.error()));
  }
  const auto public_key = mbed::public_key(*pk.get());
  std::optional<std::size_t> mine;
  for (std::size_t i = 0; i < bags.size(); ++i) {
    if (bags[i].certificate.empty()) {
      continue;
    }
    if (!key_bag->local_key_id.empty() && bags[i].local_key_id == key_bag->local_key_id) {
      mine = i;
      break;
    }
    mbed::Certificates certificate;
    if (certificate.add(bags[i].certificate) && public_key && mbed::public_key(certificate.get()->pk) == public_key) {
      mine = i;
      break;
    }
  }
  if (!mine) {
    return make_error(ErrorCode::kOutOfRange, "PKCS#12: no certificate for the private key");
  }
  std::vector<Bytes> chain;
  for (std::size_t i = 0; i < bags.size(); ++i) {
    if (i != *mine && !bags[i].certificate.empty()) {
      chain.push_back(bags[i].certificate);
    }
  }
  Bytes key = std::move(key_bag->key.bytes);
  return Credentials(std::move(key), std::move(bags[*mine].certificate), std::move(chain));
}

}  // namespace capriole::crypto
