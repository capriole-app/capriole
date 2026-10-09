// Fuzz target for CMS SignedData and RFC 3161 time stamps (crypto/cms.hpp): the input is what a PDF
// signature's /Contents holds. It is verified against a fixed detached content and as encapsulated
// content, read as a time stamp token and as a time stamp response, and each certificate in it is read
// and signs anew without its key (unsigned_detached), the input itself given as the value (set_value: an
// ECDSA value as DER or r‖s).
// The seed corpus is fuzz/corpus/cms/ (signatures made by the engine, a time stamp response).
//
// Invariants beyond "no crash, no UB, no hang": nothing.

#include <cstddef>
#include <cstdint>
#include <span>

#include "capriole/crypto/cms.hpp"
#include "capriole/crypto/der.hpp"
#include "capriole/crypto/digest.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace capriole::crypto;
  const auto input = std::as_bytes(std::span(data, size));
  const auto content = der::bytes_of("content");
  if (auto detached = verify_cms(input, [&](DigestAlgorithm a) { return Digest::of(a, content); })) {
    for (const auto& certificate : detached->certificates) {
      (void)certificate_info(certificate);
      (void)longest_value(certificate);
      if (auto unsigned_ = unsigned_detached(certificate, {}, Digest::of(DigestAlgorithm::kSha256, content))) {
        (void)digest_info(*unsigned_);
        (void)set_value(*unsigned_, certificate, input);
        (void)set_value(*unsigned_, certificate, detached->signature);
      }
    }
    if (detached->signer) {
      (void)check_chain(detached->certificates, *detached->signer, input);
    }
    if (detached->timestamp_token) {
      if (auto token = verify_cms(*detached->timestamp_token)) {
        (void)timestamp_info(*token);
      }
    }
  }
  if (auto encapsulated = verify_cms(input)) {
    (void)timestamp_info(*encapsulated);
  }
  (void)timestamp_token(input, content);
  return 0;
}
