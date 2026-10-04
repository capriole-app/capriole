#pragma once

// The hashes of digital signatures (PLAN.md phase 8h), streaming: a signature's byte ranges are read
// from the file in pieces.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace capriole::crypto {

enum class DigestAlgorithm : std::uint8_t { kMd5, kSha1, kSha224, kSha256, kSha384, kSha512 };

[[nodiscard]] std::string_view name(DigestAlgorithm algorithm) noexcept;
// From an AlgorithmIdentifier's OID: the hash's own, or a signature algorithm's (sha256WithRSAEncryption,
// ecdsa-with-SHA256…), which some signers put in its place.
[[nodiscard]] std::optional<DigestAlgorithm> digest_of_oid(std::string_view oid) noexcept;
[[nodiscard]] std::string_view oid_of(DigestAlgorithm algorithm) noexcept;

class Digest {
 public:
  explicit Digest(DigestAlgorithm algorithm);
  Digest(const Digest&) = delete;
  Digest& operator=(const Digest&) = delete;
  Digest(Digest&&) noexcept;
  Digest& operator=(Digest&&) noexcept;
  ~Digest();

  void update(std::span<const std::byte> data);
  [[nodiscard]] std::vector<std::byte> finish();

  [[nodiscard]] static std::vector<std::byte> of(DigestAlgorithm algorithm, std::span<const std::byte> data);

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace capriole::crypto
