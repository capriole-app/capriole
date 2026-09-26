#pragma once

// Hash functions needed by the PDF standard security handler (ISO 32000-2 §7.6.4):
// MD5 (RFC 1321) for revisions 2-4, SHA-256/384/512 (FIPS 180-4) for revisions 5-6.
//
// Written in-house on purpose (DOCS/CAPRIOLE.md §4.1): they are small, fully specified, and
// verified against the published test vectors. They are NOT a general-purpose crypto library: MD5
// is broken for security uses and is here only because the PDF format requires it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace capriole::crypto {

class Md5 {
 public:
  static constexpr std::size_t kDigestSize = 16;
  using Digest = std::array<std::byte, kDigestSize>;

  Md5() noexcept;
  void update(std::span<const std::byte> data) noexcept;
  [[nodiscard]] Digest finish() noexcept;

 private:
  void process_block(const std::byte* block) noexcept;

  std::array<std::uint32_t, 4> state_{};
  std::array<std::byte, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

class Sha256 {
 public:
  static constexpr std::size_t kDigestSize = 32;
  using Digest = std::array<std::byte, kDigestSize>;

  Sha256() noexcept;
  void update(std::span<const std::byte> data) noexcept;
  [[nodiscard]] Digest finish() noexcept;

 private:
  void process_block(const std::byte* block) noexcept;

  std::array<std::uint32_t, 8> state_{};
  std::array<std::byte, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

// SHA-512, or SHA-384 (same algorithm, different initial state, truncated output).
class Sha512 {
 public:
  static constexpr std::size_t kDigestSize = 64;
  using Digest = std::array<std::byte, kDigestSize>;

  explicit Sha512(bool sha384 = false) noexcept;
  void update(std::span<const std::byte> data) noexcept;
  // For SHA-384 only the first 48 bytes are meaningful.
  [[nodiscard]] Digest finish() noexcept;

 private:
  void process_block(const std::byte* block) noexcept;

  std::array<std::uint64_t, 8> state_{};
  std::array<std::byte, 128> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

// One-shot helpers.
[[nodiscard]] Md5::Digest md5(std::span<const std::byte> data) noexcept;
[[nodiscard]] Sha256::Digest sha256(std::span<const std::byte> data) noexcept;
[[nodiscard]] std::array<std::byte, 48> sha384(std::span<const std::byte> data) noexcept;
[[nodiscard]] Sha512::Digest sha512(std::span<const std::byte> data) noexcept;

}  // namespace capriole::crypto
