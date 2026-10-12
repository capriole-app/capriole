// Fuzz target for PKCS#12 files (crypto/pkcs12.hpp): the input is a .p12 read with the password of the
// seeds ("capriole") and with an empty one. The seed corpus is fuzz/corpus/pkcs12/ (the test fixtures:
// PBES2/AES, the legacy RC2 and 3DES, no password).
//
// Invariants beyond "no crash, no UB, no hang": credentials read have a key and a certificate.

#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/crypto/pkcs12.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto input = std::as_bytes(std::span(data, size));
  for (const char* password : {"capriole", ""}) {
    if (auto credentials = capriole::crypto::read_pkcs12(input, password)) {
      if (credentials->key().empty() || credentials->certificate().empty()) {
        __builtin_trap();
      }
    }
  }
  return 0;
}
