#pragma once

// What the engine's signature code takes from Mbed TLS, wrapped: owners for its contexts, and a
// deterministic random generator (principle 6: the same input, the same signature).

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <mbedtls/hmac_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "capriole/core/error.hpp"
#include "capriole/crypto/digest.hpp"

namespace capriole::crypto::mbed {

using Bytes = std::vector<std::byte>;

[[nodiscard]] mbedtls_md_type_t md_of(DigestAlgorithm algorithm) noexcept;
[[nodiscard]] const unsigned char* uchars(std::span<const std::byte> data) noexcept;
[[nodiscard]] unsigned char* uchars(std::span<std::byte> data) noexcept;

// HMAC-DRBG seeded from what is given (key blinding, ECDSA's nonce mixing: never from the platform).
class Drbg {
 public:
  explicit Drbg(std::span<const std::byte> seed);
  Drbg(const Drbg&) = delete;
  Drbg& operator=(const Drbg&) = delete;
  Drbg(Drbg&&) = delete;
  Drbg& operator=(Drbg&&) = delete;
  ~Drbg();
  static int random(void* self, unsigned char* out, std::size_t size);

 private:
  mbedtls_hmac_drbg_context ctx_{};
};

class Pk {
 public:
  Pk();
  Pk(const Pk&) = delete;
  Pk& operator=(const Pk&) = delete;
  Pk(Pk&&) = delete;
  Pk& operator=(Pk&&) = delete;
  ~Pk();
  [[nodiscard]] mbedtls_pk_context* get() noexcept { return &ctx_; }

 private:
  mbedtls_pk_context ctx_{};
};

class Certificates {
 public:
  Certificates();
  Certificates(const Certificates&) = delete;
  Certificates& operator=(const Certificates&) = delete;
  Certificates(Certificates&&) = delete;
  Certificates& operator=(Certificates&&) = delete;
  ~Certificates();
  // Adds one (DER); false when it does not parse.
  bool add(std::span<const std::byte> der);
  // Adds a PEM bundle, or DER certificates one after the other; how many were added.
  std::size_t add_all(std::span<const std::byte> data);
  [[nodiscard]] mbedtls_x509_crt* get() noexcept { return &chain_; }
  [[nodiscard]] bool empty() const noexcept { return chain_.raw.p == nullptr; }

 private:
  mbedtls_x509_crt chain_{};
};

// A private key (PKCS#8 DER, unencrypted).
[[nodiscard]] Result<void> parse_private_key(Pk& pk, std::span<const std::byte> der);
// The SubjectPublicKeyInfo of a key, DER.
[[nodiscard]] std::optional<Bytes> public_key(mbedtls_pk_context& pk);
[[nodiscard]] Result<Bytes> hmac(DigestAlgorithm algorithm, std::span<const std::byte> key, std::span<const std::byte> data);

}  // namespace capriole::crypto::mbed
