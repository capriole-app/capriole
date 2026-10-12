#pragma once

// The standard security handler (ISO 32000-2 §7.6.4): authenticates a password against the /Encrypt
// dictionary, derives the file key, and decrypts strings and streams (§7.6.2, §7.6.3) — and encrypts
// them again for saving, with the same key and methods (re-encryption, not a new security setup).
//
// Supported: /V 1, 2, 4, 5 with /R 2, 3, 4, 5 (Adobe's deprecated AES-256 revision) and 6; crypt
// filters /V2 (RC4), /AESV2 (AES-128), /AESV3 (AES-256) and /None or /Identity.
// Not supported: public-key security handlers, /V 3 (never published).
//
// Passwords are taken as given bytes. For R5/R6 they should be UTF-8 (SASLprep normalisation is not
// applied yet); for R2-R4 they should be PDFDocEncoding (non-ASCII UTF-8 input is not converted yet).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/model/object.hpp"

namespace capriole::crypto {

enum class CryptMethod : std::uint8_t { kIdentity, kRc4, kAesV2, kAesV3 };

class StandardSecurityHandler {
 public:
  // encrypt: the /Encrypt dictionary; first_id: the first string of the trailer /ID (may be empty).
  // Tries password as user password, then as owner password. kPasswordRequired when neither matches,
  // kUnsupported for handlers or versions that are not implemented, kSyntax for malformed entries.
  [[nodiscard]] static Result<StandardSecurityHandler> create(const model::Dict& encrypt, std::span<const std::byte> first_id, std::string_view password);

  [[nodiscard]] int revision() const noexcept { return revision_; }
  // /P: the user access permissions (bit 3 print, 4 modify, 5 copy, ...; §7.6.4.2 Table 22).
  [[nodiscard]] std::int32_t permissions() const noexcept { return permissions_; }
  // True when authenticated with the owner password (permissions do not restrict the owner).
  [[nodiscard]] bool owner_authenticated() const noexcept { return owner_; }
  [[nodiscard]] bool encrypt_metadata() const noexcept { return encrypt_metadata_; }
  [[nodiscard]] CryptMethod string_method() const noexcept { return string_method_; }
  [[nodiscard]] CryptMethod stream_method() const noexcept { return stream_method_; }

  // The method of a named crypt filter from /CF (for streams with a /Crypt filter), or /Identity.
  [[nodiscard]] Result<CryptMethod> crypt_filter(std::string_view name) const;

  // Decrypts data belonging to indirect object ref with the given method (Algorithm 1 for RC4 and
  // AESV2 derives a per-object key; AESV3 uses the file key directly).
  [[nodiscard]] Result<std::vector<std::byte>> decrypt(std::span<const std::byte> data, model::ObjectRef ref, CryptMethod method) const;
  // The inverse, for saving (PLAN.md 3d): decrypt(encrypt(x)) == x. AES output is deterministic (the
  // IV is derived from the key, the object and the data; see the .cpp).
  [[nodiscard]] Result<std::vector<std::byte>> encrypt(std::span<const std::byte> data, model::ObjectRef ref, CryptMethod method) const;
  // How the data of a stream with this dictionary is encrypted: /Identity for cross-reference
  // streams and, without /EncryptMetadata, metadata streams; the crypt filter a leading /Crypt
  // filter names; the default stream method otherwise. Shared by reading and writing.
  [[nodiscard]] Result<CryptMethod> method_for_stream(const model::Dict& stream_dict) const;

 private:
  StandardSecurityHandler() = default;
  // The key for an object's data: Algorithm 1 for RC4 and AESV2, the file key for AESV3.
  [[nodiscard]] std::vector<std::byte> object_key(model::ObjectRef ref, CryptMethod method) const;

  int version_ = 0;
  int revision_ = 0;
  std::int32_t permissions_ = 0;
  bool owner_ = false;
  bool encrypt_metadata_ = true;
  CryptMethod string_method_ = CryptMethod::kIdentity;
  CryptMethod stream_method_ = CryptMethod::kIdentity;
  std::vector<std::byte> file_key_;
  model::Dict crypt_filters_;
};

// Algorithm 2.B (ISO 32000-2 §7.6.4.3.4): the iterated hash of revision 6. udata is the 48-byte /U
// value when computing an owner hash, empty otherwise. Exposed for testing.
[[nodiscard]] std::vector<std::byte> hash_r6(std::span<const std::byte> password, std::span<const std::byte> salt, std::span<const std::byte> udata);

}  // namespace capriole::crypto
