#pragma once

// RC2 (RFC 2268), decryption only: the certificates of PKCS#12 files made by older tools (OpenSSL 1.x,
// Windows) are encrypted with pbeWithSHAAnd40BitRC2-CBC. Mbed TLS no longer has it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto {

class Rc2 {
 public:
  using Block = std::array<std::byte, 8>;

  // A key of 1..128 bytes, with its effective length in bits (1..1024).
  [[nodiscard]] static Result<Rc2> create(std::span<const std::byte> key, unsigned effective_bits);
  [[nodiscard]] Block decrypt(const Block& block) const noexcept;

 private:
  Rc2() = default;
  std::array<std::uint16_t, 64> k_{};
};

// CBC decryption with PKCS#7 padding removed.
[[nodiscard]] Result<std::vector<std::byte>> rc2_cbc_decrypt(const Rc2& rc2, const Rc2::Block& iv, std::span<const std::byte> data);

}  // namespace capriole::crypto
