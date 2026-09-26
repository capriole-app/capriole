#pragma once

// Ciphers needed by the PDF standard security handler (ISO 32000-2 §7.6.3, §7.6.4):
// RC4 (revisions 2-4) and AES-128/256 in CBC mode (AESV2, AESV3, and the R6 key derivation).
//
// Written in-house (DOCS/CAPRIOLE.md §4.1) and verified against FIPS 197, NIST SP 800-38A and
// RFC 6229 vectors. The AES implementation is table-based and not constant-time: fine for opening
// documents, NOT suitable where timing side channels matter (e.g. a server decrypting on demand).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto {

class Rc4 {
 public:
  // key: 1..256 bytes (PDF uses 5..16).
  explicit Rc4(std::span<const std::byte> key) noexcept;
  // Encrypts or decrypts in place (RC4 is symmetric).
  void apply(std::span<std::byte> data) noexcept;

 private:
  std::array<std::uint8_t, 256> s_{};
  std::uint8_t i_ = 0;
  std::uint8_t j_ = 0;
};

class Aes {
 public:
  static constexpr std::size_t kBlockSize = 16;
  using Block = std::array<std::byte, kBlockSize>;

  // key: 16 bytes (AES-128) or 32 bytes (AES-256); other sizes are rejected.
  [[nodiscard]] static Result<Aes> create(std::span<const std::byte> key);

  [[nodiscard]] Block encrypt_block(const Block& in) const noexcept;
  [[nodiscard]] Block decrypt_block(const Block& in) const noexcept;

 private:
  Aes() = default;
  // Expanded key: 16 bytes per round key, up to 15 round keys (AES-256).
  std::array<std::uint8_t, 240> round_keys_{};
  std::size_t rounds_ = 0;
};

// CBC decryption. With pkcs7_padding the final block's padding is checked and removed
// (kCorruptData when invalid). data must be a whole number of blocks.
[[nodiscard]] Result<std::vector<std::byte>> aes_cbc_decrypt(const Aes& aes, const Aes::Block& iv, std::span<const std::byte> data, bool pkcs7_padding);

// CBC encryption without padding; data must be a whole number of blocks.
[[nodiscard]] Result<std::vector<std::byte>> aes_cbc_encrypt(const Aes& aes, const Aes::Block& iv, std::span<const std::byte> data);

}  // namespace capriole::crypto
