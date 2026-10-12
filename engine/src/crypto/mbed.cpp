#include "mbed.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <mbedtls/hmac_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "capriole/core/error.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"

namespace capriole::crypto::mbed {

mbedtls_md_type_t md_of(DigestAlgorithm algorithm) noexcept {
  switch (algorithm) {
    case DigestAlgorithm::kMd5:
      return MBEDTLS_MD_MD5;
    case DigestAlgorithm::kSha1:
      return MBEDTLS_MD_SHA1;
    case DigestAlgorithm::kSha224:
      return MBEDTLS_MD_SHA224;
    case DigestAlgorithm::kSha256:
      return MBEDTLS_MD_SHA256;
    case DigestAlgorithm::kSha384:
      return MBEDTLS_MD_SHA384;
    case DigestAlgorithm::kSha512:
      return MBEDTLS_MD_SHA512;
  }
  return MBEDTLS_MD_NONE;
}

unsigned char* uchars(std::span<std::byte> data) noexcept {
  return reinterpret_cast<unsigned char*>(data.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mbed TLS writes bytes as unsigned char
}

const unsigned char* uchars(std::span<const std::byte> data) noexcept {
  return reinterpret_cast<const unsigned char*>(data.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mbed TLS takes bytes as unsigned char
}

Drbg::Drbg(std::span<const std::byte> seed) {
  mbedtls_hmac_drbg_init(&ctx_);
  // A seed of no bytes is refused by the DRBG: a fixed byte goes first.
  std::vector<unsigned char> material{0x43};
  material.insert(material.end(), uchars(seed), uchars(seed) + seed.size());
  (void)mbedtls_hmac_drbg_seed_buf(&ctx_, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), material.data(), material.size());
  mbedtls_platform_zeroize(material.data(), material.size());
}

Drbg::~Drbg() { mbedtls_hmac_drbg_free(&ctx_); }

int Drbg::random(void* self, unsigned char* out, std::size_t size) { return mbedtls_hmac_drbg_random(&static_cast<Drbg*>(self)->ctx_, out, size); }

Pk::Pk() { mbedtls_pk_init(&ctx_); }
Pk::~Pk() { mbedtls_pk_free(&ctx_); }

Certificates::Certificates() { mbedtls_x509_crt_init(&chain_); }
Certificates::~Certificates() { mbedtls_x509_crt_free(&chain_); }

bool Certificates::add(std::span<const std::byte> der) { return mbedtls_x509_crt_parse_der(&chain_, uchars(der), der.size()) == 0; }

std::size_t Certificates::add_all(std::span<const std::byte> data) {
  const std::string_view text = der::chars_of(data);
  std::size_t added = 0;
  if (text.find("-----BEGIN") != std::string_view::npos) {
    // PEM: Mbed TLS wants it terminated by a NUL, counted in the size.
    std::vector<unsigned char> pem(uchars(data), uchars(data) + data.size());
    pem.push_back(0);
    const auto before = [&] {
      std::size_t n = 0;
      for (const mbedtls_x509_crt* c = &chain_; c != nullptr && c->raw.p != nullptr; c = c->next) {
        ++n;
      }
      return n;
    };
    const std::size_t had = before();
    (void)mbedtls_x509_crt_parse(&chain_, pem.data(), pem.size());
    return before() - had;
  }
  der::Reader reader(data);
  while (!reader.empty()) {
    auto element = reader.next();
    if (!element) {
      break;
    }
    if (add(element->whole)) {
      ++added;
    }
  }
  return added;
}

Result<void> parse_private_key(Pk& pk, std::span<const std::byte> der) {
  Drbg drbg(der);
  if (const int rc = mbedtls_pk_parse_key(pk.get(), uchars(der), der.size(), nullptr, 0, Drbg::random, &drbg); rc != 0) {
    return make_error(ErrorCode::kUnsupported, std::format("the private key cannot be read (Mbed TLS {:#x})", -rc));
  }
  return {};
}

std::optional<Bytes> public_key(mbedtls_pk_context& pk) {
  // Written from the end of the buffer.
  std::vector<unsigned char> buffer(8192);
  const int n = mbedtls_pk_write_pubkey_der(&pk, buffer.data(), buffer.size());
  if (n <= 0) {
    return std::nullopt;
  }
  const auto* begin = buffer.data() + buffer.size() - static_cast<std::size_t>(n);
  Bytes out(static_cast<std::size_t>(n));
  std::memcpy(out.data(), begin, out.size());
  return out;
}

Result<Bytes> hmac(DigestAlgorithm algorithm, std::span<const std::byte> key, std::span<const std::byte> data) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(md_of(algorithm));
  if (info == nullptr) {
    return make_error(ErrorCode::kUnsupported, "hash not available");
  }
  Bytes out(mbedtls_md_get_size(info));
  if (mbedtls_md_hmac(info, uchars(key), key.size(), uchars(data), data.size(), uchars(std::span(out))) != 0) {
    return make_error(ErrorCode::kUnsupported, "HMAC failed");
  }
  return out;
}

}  // namespace capriole::crypto::mbed
