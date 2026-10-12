// Fuzz target for the standard security handler. The input starts with an /Encrypt dictionary in
// PDF syntax; the bytes after it are used as data to decrypt with every crypt method. The seed
// corpus is fuzz/corpus/security/.
//
// Checks "no crash, no UB, no hang" across parameter validation, key derivation (all revisions,
// including the iterated R6 hash) and RC4/AES decryption of arbitrary data.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/parser/object_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  capriole::SourceReader reader(source);
  capriole::parser::ObjectParser parser(reader);
  auto object = parser.parse_object();
  if (!object || object->as_dict() == nullptr) {
    return 0;
  }
  const std::uint64_t consumed = reader.position();
  const auto rest = bytes.subspan(static_cast<std::size_t>(consumed));

  constexpr std::array<std::byte, 16> kId = {std::byte{0x01}, std::byte{0x23}, std::byte{0x45}, std::byte{0x67}};
  for (const char* password : {"", "user", "owner"}) {
    auto handler = capriole::crypto::StandardSecurityHandler::create(*object->as_dict(), kId, password);
    if (!handler) {
      continue;
    }
    for (const auto method : {capriole::crypto::CryptMethod::kIdentity, capriole::crypto::CryptMethod::kRc4, capriole::crypto::CryptMethod::kAesV2,
                              capriole::crypto::CryptMethod::kAesV3}) {
      (void)handler->decrypt(rest, capriole::model::ObjectRef{.number = 7, .generation = 0}, method);
    }
  }
  return 0;
}
