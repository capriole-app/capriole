// Fuzz target for revocation (crypto/revocation.hpp): OCSP responses, CRLs and the URLs of certificates.
// The input is cut at each "\xff\xfe\xfd" into an answer, a certificate and its issuer (missing parts
// are the whole input): the answer is checked as an OCSP response and as a CRL of the certificate, the
// certificate's URLs read, the OCSP request made. The seed corpus is fuzz/corpus/revocation/ (the
// fixtures of tests/data/signing/make_ltv_fixtures.sh, joined so).
//
// Invariants beyond "no crash, no UB, no hang": nothing.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/crypto/revocation.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace capriole::crypto;
  const auto input = std::as_bytes(std::span(data, size));
  constexpr std::array kCut{std::byte{0xFF}, std::byte{0xFE}, std::byte{0xFD}};
  std::vector<std::span<const std::byte>> parts;
  auto rest = input;
  while (parts.size() < 2) {
    const auto at = std::ranges::search(rest, kCut);
    if (at.empty()) {
      break;
    }
    parts.push_back(rest.first(static_cast<std::size_t>(at.begin() - rest.begin())));
    rest = rest.subspan(static_cast<std::size_t>(at.end() - rest.begin()));
  }
  parts.push_back(rest);
  const auto answer = parts[0];
  const auto certificate = parts.size() > 1 ? parts[1] : input;
  const auto issuer = parts.size() > 2 ? parts[2] : input;
  (void)certificate_urls(certificate);
  (void)ocsp_request(certificate, issuer);
  (void)issued_by(certificate, issuer);
  (void)check_ocsp(answer, certificate, issuer);
  (void)check_crl(answer, certificate, issuer);
  (void)ocsp_produced_at(answer);
  (void)crl_this_update(answer);
  return 0;
}
