#include "rc2.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto {

namespace {

// RFC 2268 §2: the permutation of 0..255 from the digits of pi.
constexpr std::array<std::uint8_t, 256> kPiTable = {
    0xd9, 0x78, 0xf9, 0xc4, 0x19, 0xdd, 0xb5, 0xed, 0x28, 0xe9, 0xfd, 0x79, 0x4a, 0xa0, 0xd8, 0x9d, 0xc6, 0x7e, 0x37, 0x83, 0x2b, 0x76, 0x53, 0x8e, 0x62, 0x4c,
    0x64, 0x88, 0x44, 0x8b, 0xfb, 0xa2, 0x17, 0x9a, 0x59, 0xf5, 0x87, 0xb3, 0x4f, 0x13, 0x61, 0x45, 0x6d, 0x8d, 0x09, 0x81, 0x7d, 0x32, 0xbd, 0x8f, 0x40, 0xeb,
    0x86, 0xb7, 0x7b, 0x0b, 0xf0, 0x95, 0x21, 0x22, 0x5c, 0x6b, 0x4e, 0x82, 0x54, 0xd6, 0x65, 0x93, 0xce, 0x60, 0xb2, 0x1c, 0x73, 0x56, 0xc0, 0x14, 0xa7, 0x8c,
    0xf1, 0xdc, 0x12, 0x75, 0xca, 0x1f, 0x3b, 0xbe, 0xe4, 0xd1, 0x42, 0x3d, 0xd4, 0x30, 0xa3, 0x3c, 0xb6, 0x26, 0x6f, 0xbf, 0x0e, 0xda, 0x46, 0x69, 0x07, 0x57,
    0x27, 0xf2, 0x1d, 0x9b, 0xbc, 0x94, 0x43, 0x03, 0xf8, 0x11, 0xc7, 0xf6, 0x90, 0xef, 0x3e, 0xe7, 0x06, 0xc3, 0xd5, 0x2f, 0xc8, 0x66, 0x1e, 0xd7, 0x08, 0xe8,
    0xea, 0xde, 0x80, 0x52, 0xee, 0xf7, 0x84, 0xaa, 0x72, 0xac, 0x35, 0x4d, 0x6a, 0x2a, 0x96, 0x1a, 0xd2, 0x71, 0x5a, 0x15, 0x49, 0x74, 0x4b, 0x9f, 0xd0, 0x5e,
    0x04, 0x18, 0xa4, 0xec, 0xc2, 0xe0, 0x41, 0x6e, 0x0f, 0x51, 0xcb, 0xcc, 0x24, 0x91, 0xaf, 0x50, 0xa1, 0xf4, 0x70, 0x39, 0x99, 0x7c, 0x3a, 0x85, 0x23, 0xb8,
    0xb4, 0x7a, 0xfc, 0x02, 0x36, 0x5b, 0x25, 0x55, 0x97, 0x31, 0x2d, 0x5d, 0xfa, 0x98, 0xe3, 0x8a, 0x92, 0xae, 0x05, 0xdf, 0x29, 0x10, 0x67, 0x6c, 0xba, 0xc9,
    0xd3, 0x00, 0xe6, 0xcf, 0xe1, 0x9e, 0xa8, 0x2c, 0x63, 0x16, 0x01, 0x3f, 0x58, 0xe2, 0x89, 0xa9, 0x0d, 0x38, 0x34, 0x1b, 0xab, 0x33, 0xff, 0xb0, 0xbb, 0x48,
    0x0c, 0x5f, 0xb9, 0xb1, 0xcd, 0x2e, 0xc5, 0xf3, 0xdb, 0x47, 0xe5, 0xa5, 0x9c, 0x77, 0x0a, 0xa6, 0x20, 0x68, 0xfe, 0x7f, 0xc1, 0xad};

constexpr std::array<unsigned, 4> kShifts = {1, 2, 3, 5};

std::uint16_t ror(std::uint16_t x, unsigned s) { return static_cast<std::uint16_t>((x >> s) | (x << (16U - s))); }

}  // namespace

Result<Rc2> Rc2::create(std::span<const std::byte> key, unsigned effective_bits) {
  if (key.empty() || key.size() > 128 || effective_bits == 0 || effective_bits > 1024) {
    return make_error(ErrorCode::kUnsupported, "RC2: bad key length");
  }
  // §2: the key expanded to 128 bytes, then cut to its effective bits.
  std::array<std::uint8_t, 128> l{};
  for (std::size_t i = 0; i < key.size(); ++i) {
    l.at(i) = std::to_integer<std::uint8_t>(key[i]);
  }
  const std::size_t t = key.size();
  for (std::size_t i = t; i < 128; ++i) {
    l.at(i) = kPiTable.at(static_cast<std::uint8_t>(l.at(i - 1) + l.at(i - t)));
  }
  const std::size_t t8 = (effective_bits + 7) / 8;
  const unsigned tm = 255U % (1U << (8 + effective_bits - (8 * t8)));
  l.at(128 - t8) = kPiTable.at(l.at(128 - t8) & tm);
  for (std::size_t i = 128 - t8; i-- > 0;) {
    l.at(i) = kPiTable.at(l.at(i + 1) ^ l.at(i + t8));
  }
  Rc2 rc2;
  for (std::size_t i = 0; i < 64; ++i) {
    rc2.k_.at(i) = static_cast<std::uint16_t>(l.at(2 * i) | (l.at((2 * i) + 1) << 8U));
  }
  return rc2;
}

Rc2::Block Rc2::decrypt(const Block& block) const noexcept {
  std::array<std::uint16_t, 4> r{};
  for (std::size_t i = 0; i < 4; ++i) {
    r.at(i) = static_cast<std::uint16_t>(std::to_integer<unsigned>(block.at(2 * i)) | (std::to_integer<unsigned>(block.at((2 * i) + 1)) << 8U));
  }
  const auto at = [&](std::size_t i) { return r.at(i % 4); };
  std::size_t j = 63;
  // §4: the rounds of encryption undone, last first (a mash after the 5th and 11th mixing rounds).
  for (int round = 15; round >= 0; --round) {
    if (round == 4 || round == 10) {
      for (std::size_t i = 4; i-- > 0;) {
        r.at(i) = static_cast<std::uint16_t>(r.at(i) - k_.at(at(i + 3) & 63U));
      }
    }
    for (std::size_t i = 4; i-- > 0;) {
      r.at(i) = ror(r.at(i), kShifts.at(i));
      r.at(i) = static_cast<std::uint16_t>(r.at(i) - k_.at(j) - (at(i + 3) & at(i + 2)) - (static_cast<std::uint16_t>(~at(i + 3)) & at(i + 1)));
      --j;
    }
  }
  Block out{};
  for (std::size_t i = 0; i < 4; ++i) {
    out.at(2 * i) = static_cast<std::byte>(r.at(i) & 0xFFU);
    out.at((2 * i) + 1) = static_cast<std::byte>(r.at(i) >> 8U);
  }
  return out;
}

Result<std::vector<std::byte>> rc2_cbc_decrypt(const Rc2& rc2, const Rc2::Block& iv, std::span<const std::byte> data) {
  if (data.empty() || data.size() % 8 != 0) {
    return make_error(ErrorCode::kSyntax, "RC2: data is not whole blocks");
  }
  std::vector<std::byte> out;
  out.reserve(data.size());
  Rc2::Block previous = iv;
  for (std::size_t at = 0; at < data.size(); at += 8) {
    Rc2::Block block{};
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), 8, block.begin());
    const Rc2::Block plain = rc2.decrypt(block);
    for (std::size_t i = 0; i < 8; ++i) {
      out.push_back(plain.at(i) ^ previous.at(i));
    }
    previous = block;
  }
  const auto pad = std::to_integer<std::size_t>(out.back());
  if (pad == 0 || pad > 8) {
    return make_error(ErrorCode::kSyntax, "RC2: bad padding (wrong password?)");
  }
  for (std::size_t i = 0; i < pad; ++i) {
    if (std::to_integer<std::size_t>(out[out.size() - 1 - i]) != pad) {
      return make_error(ErrorCode::kSyntax, "RC2: bad padding (wrong password?)");
    }
  }
  out.resize(out.size() - pad);
  return out;
}

}  // namespace capriole::crypto
