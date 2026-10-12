#include "capriole/crypto/digest.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <mbedtls/md.h>

#include "mbed.hpp"

namespace capriole::crypto {

std::string_view name(DigestAlgorithm algorithm) noexcept {
  switch (algorithm) {
    case DigestAlgorithm::kMd5:
      return "MD5";
    case DigestAlgorithm::kSha1:
      return "SHA-1";
    case DigestAlgorithm::kSha224:
      return "SHA-224";
    case DigestAlgorithm::kSha256:
      return "SHA-256";
    case DigestAlgorithm::kSha384:
      return "SHA-384";
    case DigestAlgorithm::kSha512:
      return "SHA-512";
  }
  return "";
}

std::optional<DigestAlgorithm> digest_of_oid(std::string_view oid) noexcept {
  struct Entry {
    std::string_view oid;
    DigestAlgorithm algorithm;
  };
  static constexpr std::array<Entry, 17> kTable = {{
      {.oid = "1.2.840.113549.2.5", .algorithm = DigestAlgorithm::kMd5},
      {.oid = "1.3.14.3.2.26", .algorithm = DigestAlgorithm::kSha1},
      {.oid = "2.16.840.1.101.3.4.2.4", .algorithm = DigestAlgorithm::kSha224},
      {.oid = "2.16.840.1.101.3.4.2.1", .algorithm = DigestAlgorithm::kSha256},
      {.oid = "2.16.840.1.101.3.4.2.2", .algorithm = DigestAlgorithm::kSha384},
      {.oid = "2.16.840.1.101.3.4.2.3", .algorithm = DigestAlgorithm::kSha512},
      // RSA with a hash (RFC 8017 A.2.4), ECDSA with a hash (RFC 5758 §3.2)
      {.oid = "1.2.840.113549.1.1.4", .algorithm = DigestAlgorithm::kMd5},
      {.oid = "1.2.840.113549.1.1.5", .algorithm = DigestAlgorithm::kSha1},
      {.oid = "1.2.840.113549.1.1.14", .algorithm = DigestAlgorithm::kSha224},
      {.oid = "1.2.840.113549.1.1.11", .algorithm = DigestAlgorithm::kSha256},
      {.oid = "1.2.840.113549.1.1.12", .algorithm = DigestAlgorithm::kSha384},
      {.oid = "1.2.840.113549.1.1.13", .algorithm = DigestAlgorithm::kSha512},
      {.oid = "1.2.840.10045.4.1", .algorithm = DigestAlgorithm::kSha1},
      {.oid = "1.2.840.10045.4.3.1", .algorithm = DigestAlgorithm::kSha224},
      {.oid = "1.2.840.10045.4.3.2", .algorithm = DigestAlgorithm::kSha256},
      {.oid = "1.2.840.10045.4.3.3", .algorithm = DigestAlgorithm::kSha384},
      {.oid = "1.2.840.10045.4.3.4", .algorithm = DigestAlgorithm::kSha512},
  }};
  for (const Entry& e : kTable) {
    if (e.oid == oid) {
      return e.algorithm;
    }
  }
  return std::nullopt;
}

std::string_view oid_of(DigestAlgorithm algorithm) noexcept {
  switch (algorithm) {
    case DigestAlgorithm::kMd5:
      return "1.2.840.113549.2.5";
    case DigestAlgorithm::kSha1:
      return "1.3.14.3.2.26";
    case DigestAlgorithm::kSha224:
      return "2.16.840.1.101.3.4.2.4";
    case DigestAlgorithm::kSha256:
      return "2.16.840.1.101.3.4.2.1";
    case DigestAlgorithm::kSha384:
      return "2.16.840.1.101.3.4.2.2";
    case DigestAlgorithm::kSha512:
      return "2.16.840.1.101.3.4.2.3";
  }
  return "";
}

struct Digest::State {
  mbedtls_md_context_t ctx{};
};

Digest::Digest(DigestAlgorithm algorithm) : state_(std::make_unique<State>()) {
  mbedtls_md_init(&state_->ctx);
  // Every algorithm of the enum is in the build's configuration (cmake/mbedtls/).
  (void)mbedtls_md_setup(&state_->ctx, mbedtls_md_info_from_type(mbed::md_of(algorithm)), 0);
  (void)mbedtls_md_starts(&state_->ctx);
}

Digest::Digest(Digest&&) noexcept = default;
Digest& Digest::operator=(Digest&&) noexcept = default;

Digest::~Digest() {
  if (state_) {
    mbedtls_md_free(&state_->ctx);
  }
}

void Digest::update(std::span<const std::byte> data) { (void)mbedtls_md_update(&state_->ctx, mbed::uchars(data), data.size()); }

std::vector<std::byte> Digest::finish() {
  std::vector<std::byte> out(mbedtls_md_get_size(mbedtls_md_info_from_ctx(&state_->ctx)));
  (void)mbedtls_md_finish(&state_->ctx, mbed::uchars(std::span(out)));
  return out;
}

std::vector<std::byte> Digest::of(DigestAlgorithm algorithm, std::span<const std::byte> data) {
  Digest d(algorithm);
  d.update(data);
  return d.finish();
}

}  // namespace capriole::crypto
