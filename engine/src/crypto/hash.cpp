#include "capriole/crypto/hash.hpp"

#include <algorithm>
#include <bit>

namespace capriole::crypto {

namespace {

std::uint32_t load_le32(const std::byte* p) noexcept {
  return std::to_integer<std::uint32_t>(p[0]) | (std::to_integer<std::uint32_t>(p[1]) << 8U) | (std::to_integer<std::uint32_t>(p[2]) << 16U) |
         (std::to_integer<std::uint32_t>(p[3]) << 24U);
}

std::uint32_t load_be32(const std::byte* p) noexcept {
  return (std::to_integer<std::uint32_t>(p[0]) << 24U) | (std::to_integer<std::uint32_t>(p[1]) << 16U) | (std::to_integer<std::uint32_t>(p[2]) << 8U) |
         std::to_integer<std::uint32_t>(p[3]);
}

std::uint64_t load_be64(const std::byte* p) noexcept { return (std::uint64_t{load_be32(p)} << 32U) | load_be32(p + 4); }

// Shared buffering of 64/128-byte block hashes: fills the buffer, hands complete blocks over.
template <typename ProcessBlock, std::size_t kBlock>
void buffered_update(std::array<std::byte, kBlock>& buffer, std::size_t& buffered, std::uint64_t& total, std::span<const std::byte> data,
                     ProcessBlock process) noexcept {
  total += data.size();
  std::size_t pos = 0;
  if (buffered > 0) {
    const std::size_t take = std::min(kBlock - buffered, data.size());
    std::copy_n(data.begin(), take, buffer.begin() + static_cast<std::ptrdiff_t>(buffered));
    buffered += take;
    pos = take;
    if (buffered == kBlock) {
      process(buffer.data());
      buffered = 0;
    }
  }
  for (; pos + kBlock <= data.size(); pos += kBlock) {
    process(data.data() + pos);
  }
  const std::size_t rest = data.size() - pos;
  std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(pos), rest, buffer.begin() + static_cast<std::ptrdiff_t>(buffered));
  buffered += rest;
}

// ── MD5 constants (RFC 1321 §3.4) ───────────────────────────────────────────────────────────

constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122,
    0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6,
    0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60,
    0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr std::array<int, 64> kMd5Shift = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                                           14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                                           4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};

// ── SHA-2 constants (FIPS 180-4 §4.2) ───────────────────────────────────────────────────────

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
    0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
    0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
    0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::array<std::uint64_t, 80> kSha512K = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b,
    0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1,
    0x9bdc06a725c71235, 0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
    0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
    0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
    0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8,
    0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
    0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec, 0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
    0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c, 0x4cc5d4becb3e42b6,
    0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817};

}  // namespace

// ── MD5 ─────────────────────────────────────────────────────────────────────────────────────

Md5::Md5() noexcept : state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::process_block(const std::byte* block) noexcept {
  std::array<std::uint32_t, 16> m{};
  for (std::size_t i = 0; i < 16; ++i) {
    m[i] = load_le32(block + (4 * i));
  }
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  for (std::size_t i = 0; i < 64; ++i) {
    std::uint32_t f = 0;
    std::size_t g = 0;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = ((5 * i) + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = ((3 * i) + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    f += a + kMd5K[i] + m[g];
    a = d;
    d = c;
    c = b;
    b += std::rotl(f, kMd5Shift[i]);
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
}

void Md5::update(std::span<const std::byte> data) noexcept {
  buffered_update(buffer_, buffered_, total_bytes_, data, [this](const std::byte* block) { process_block(block); });
}

Md5::Digest Md5::finish() noexcept {
  const std::uint64_t bits = total_bytes_ * 8;
  const std::array<std::byte, 1> one = {std::byte{0x80}};
  update(one);
  const std::array<std::byte, 64> zeros{};
  update(std::span(zeros).first((buffered_ <= 56 ? 56 : 120) - buffered_));
  std::array<std::byte, 8> length{};
  for (std::size_t i = 0; i < 8; ++i) {
    length[i] = static_cast<std::byte>((bits >> (8 * i)) & 0xFFU);
  }
  update(length);
  Digest digest{};
  for (std::size_t i = 0; i < 16; ++i) {
    digest[i] = static_cast<std::byte>((state_[i / 4] >> (8 * (i % 4))) & 0xFFU);
  }
  return digest;
}

// ── SHA-256 ─────────────────────────────────────────────────────────────────────────────────

Sha256::Sha256() noexcept : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::process_block(const std::byte* block) noexcept {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = load_be32(block + (4 * i));
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
    const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::array<std::uint32_t, 8> v = state_;
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
    const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const std::uint32_t t1 = v[7] + s1 + ch + kSha256K[i] + w[i];
    const std::uint32_t s0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
    const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const std::uint32_t t2 = s0 + maj;
    v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
  }
  for (std::size_t i = 0; i < 8; ++i) {
    state_[i] += v[i];
  }
}

void Sha256::update(std::span<const std::byte> data) noexcept {
  buffered_update(buffer_, buffered_, total_bytes_, data, [this](const std::byte* block) { process_block(block); });
}

Sha256::Digest Sha256::finish() noexcept {
  const std::uint64_t bits = total_bytes_ * 8;
  const std::array<std::byte, 1> one = {std::byte{0x80}};
  update(one);
  const std::array<std::byte, 64> zeros{};
  update(std::span(zeros).first((buffered_ <= 56 ? 56 : 120) - buffered_));
  std::array<std::byte, 8> length{};
  for (std::size_t i = 0; i < 8; ++i) {
    length[i] = static_cast<std::byte>((bits >> (56 - (8 * i))) & 0xFFU);
  }
  update(length);
  Digest digest{};
  for (std::size_t i = 0; i < 32; ++i) {
    digest[i] = static_cast<std::byte>((state_[i / 4] >> (24 - (8 * (i % 4)))) & 0xFFU);
  }
  return digest;
}

// ── SHA-512 / SHA-384 ───────────────────────────────────────────────────────────────────────

Sha512::Sha512(bool sha384) noexcept {
  if (sha384) {
    state_ = {0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939,
              0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4};
  } else {
    state_ = {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
              0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
  }
}

void Sha512::process_block(const std::byte* block) noexcept {
  std::array<std::uint64_t, 80> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = load_be64(block + (8 * i));
  }
  for (std::size_t i = 16; i < 80; ++i) {
    const std::uint64_t s0 = std::rotr(w[i - 15], 1) ^ std::rotr(w[i - 15], 8) ^ (w[i - 15] >> 7U);
    const std::uint64_t s1 = std::rotr(w[i - 2], 19) ^ std::rotr(w[i - 2], 61) ^ (w[i - 2] >> 6U);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::array<std::uint64_t, 8> v = state_;
  for (std::size_t i = 0; i < 80; ++i) {
    const std::uint64_t s1 = std::rotr(v[4], 14) ^ std::rotr(v[4], 18) ^ std::rotr(v[4], 41);
    const std::uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const std::uint64_t t1 = v[7] + s1 + ch + kSha512K[i] + w[i];
    const std::uint64_t s0 = std::rotr(v[0], 28) ^ std::rotr(v[0], 34) ^ std::rotr(v[0], 39);
    const std::uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const std::uint64_t t2 = s0 + maj;
    v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
  }
  for (std::size_t i = 0; i < 8; ++i) {
    state_[i] += v[i];
  }
}

void Sha512::update(std::span<const std::byte> data) noexcept {
  buffered_update(buffer_, buffered_, total_bytes_, data, [this](const std::byte* block) { process_block(block); });
}

Sha512::Digest Sha512::finish() noexcept {
  const std::uint64_t bits = total_bytes_ * 8;
  const std::array<std::byte, 1> one = {std::byte{0x80}};
  update(one);
  const std::array<std::byte, 128> zeros{};
  update(std::span(zeros).first((buffered_ <= 112 ? 112 : 240) - buffered_));
  std::array<std::byte, 16> length{};  // 128-bit length; the high 64 bits are zero here
  for (std::size_t i = 0; i < 8; ++i) {
    length[8 + i] = static_cast<std::byte>((bits >> (56 - (8 * i))) & 0xFFU);
  }
  update(length);
  Digest digest{};
  for (std::size_t i = 0; i < 64; ++i) {
    digest[i] = static_cast<std::byte>((state_[i / 8] >> (56 - (8 * (i % 8)))) & 0xFFU);
  }
  return digest;
}

// ── One-shot helpers ────────────────────────────────────────────────────────────────────────

Md5::Digest md5(std::span<const std::byte> data) noexcept {
  Md5 hash;
  hash.update(data);
  return hash.finish();
}

Sha256::Digest sha256(std::span<const std::byte> data) noexcept {
  Sha256 hash;
  hash.update(data);
  return hash.finish();
}

std::array<std::byte, 48> sha384(std::span<const std::byte> data) noexcept {
  Sha512 hash(true);
  hash.update(data);
  const auto full = hash.finish();
  std::array<std::byte, 48> digest{};
  std::copy_n(full.begin(), 48, digest.begin());
  return digest;
}

Sha512::Digest sha512(std::span<const std::byte> data) noexcept {
  Sha512 hash;
  hash.update(data);
  return hash.finish();
}

}  // namespace capriole::crypto
