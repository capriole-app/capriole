#pragma once

// PKCS#12 files (.p12, .pfx; RFC 7292): a private key and its certificate, with the certificates of its
// chain, protected by a password — what users sign with (PLAN.md phase 8h). Read: hostile input, like
// every file. The integrity MAC (HMAC with SHA-1 or SHA-2, its key derived from the password) is checked
// first: a wrong password is told apart from a damaged file. Encryption: PBES2 (PBKDF2 with AES or 3DES,
// what current tools write), and the PKCS#12 PBEs with 3DES or RC2 (older tools, Windows). Not read:
// PBMAC1, public-key privacy (certificates in an EnvelopedData) — rare.
//
// The password is UTF-8; the PKCS#12 PBEs and the MAC take it as BMPString (UTF-16BE with a final 0),
// PBES2 as the bytes themselves (§B.1, RFC 8018).

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto {

// A private key and the certificates that go with it. The key's bytes are wiped when it goes.
class Credentials {
 public:
  Credentials() = default;
  Credentials(std::vector<std::byte> key, std::vector<std::byte> certificate, std::vector<std::vector<std::byte>> chain) noexcept
      : key_(std::move(key)), certificate_(std::move(certificate)), chain_(std::move(chain)) {}
  Credentials(const Credentials&) = delete;
  Credentials& operator=(const Credentials&) = delete;
  Credentials(Credentials&&) noexcept = default;
  Credentials& operator=(Credentials&&) noexcept = default;
  ~Credentials();

  // PKCS#8 PrivateKeyInfo, DER, unencrypted.
  [[nodiscard]] std::span<const std::byte> key() const noexcept { return key_; }
  // The key's certificate (DER).
  [[nodiscard]] std::span<const std::byte> certificate() const noexcept { return certificate_; }
  // The other certificates of the file (DER), in its order: the chain up to a root, when it has it.
  [[nodiscard]] const std::vector<std::vector<std::byte>>& chain() const noexcept { return chain_; }

 private:
  std::vector<std::byte> key_;
  std::vector<std::byte> certificate_;
  std::vector<std::vector<std::byte>> chain_;
};

// kPasswordRequired: the password is wrong (the MAC does not match, or a part does not decrypt).
// kUnsupported: what the file uses is not read here. kSyntax: not a PKCS#12 file, or damaged.
// kOutOfRange: no private key with its certificate in it.
[[nodiscard]] Result<Credentials> read_pkcs12(std::span<const std::byte> file, std::string_view password);

}  // namespace capriole::crypto
