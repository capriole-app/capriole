#include "capriole/crypto/cipher.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace capriole::crypto {

namespace {

// ── GF(2^8) arithmetic and the AES S-box, derived rather than typed in (FIPS 197 §4, §5.1.1) ──

constexpr std::uint8_t xtime(std::uint8_t a) noexcept { return static_cast<std::uint8_t>((static_cast<unsigned>(a) << 1U) ^ ((a & 0x80U) != 0 ? 0x1BU : 0U)); }

constexpr std::uint8_t gmul(std::uint8_t a, std::uint8_t b) noexcept {
  std::uint8_t product = 0;
  for (int i = 0; i < 8; ++i) {
    if ((b & 1U) != 0) {
      product ^= a;
    }
    a = xtime(a);
    b >>= 1U;
  }
  return product;
}

struct SBoxes {
  std::array<std::uint8_t, 256> forward{};
  std::array<std::uint8_t, 256> inverse{};
};

constexpr SBoxes make_sboxes() {
  // Powers of the generator 3 give exp/log tables; the multiplicative inverse of x is exp[255 - log x].
  std::array<std::uint8_t, 256> exp{};
  std::array<std::uint8_t, 256> log{};
  std::uint8_t x = 1;
  for (int i = 0; i < 255; ++i) {
    exp[static_cast<std::size_t>(i)] = x;
    log[x] = static_cast<std::uint8_t>(i);
    x = static_cast<std::uint8_t>(x ^ xtime(x));  // x * 3
  }
  SBoxes boxes;
  for (int v = 0; v < 256; ++v) {
    const std::uint8_t inv = v == 0 ? 0 : exp[static_cast<std::size_t>((255 - log[static_cast<std::size_t>(v)]) % 255)];
    const auto s = static_cast<std::uint8_t>(inv ^ std::rotl(inv, 1) ^ std::rotl(inv, 2) ^ std::rotl(inv, 3) ^ std::rotl(inv, 4) ^ 0x63U);
    boxes.forward[static_cast<std::size_t>(v)] = s;
    boxes.inverse[s] = static_cast<std::uint8_t>(v);
  }
  return boxes;
}

constexpr SBoxes kBoxes = make_sboxes();
static_assert(kBoxes.forward[0x00] == 0x63 && kBoxes.forward[0x53] == 0xED, "AES S-box (FIPS 197 Figure 7)");

using State = std::array<std::uint8_t, 16>;

void add_round_key(State& s, const std::uint8_t* key) noexcept {
  for (std::size_t i = 0; i < 16; ++i) {
    s[i] ^= key[i];
  }
}

void shift_rows(State& s, bool inverse) noexcept {
  const State old = s;
  for (std::size_t c = 0; c < 4; ++c) {
    for (std::size_t r = 1; r < 4; ++r) {
      if (inverse) {
        s[(((c + r) % 4) * 4) + r] = old[(c * 4) + r];
      } else {
        s[(c * 4) + r] = old[(((c + r) % 4) * 4) + r];
      }
    }
  }
}

void mix_columns(State& s, bool inverse) noexcept {
  for (std::size_t c = 0; c < 4; ++c) {
    const std::uint8_t a0 = s[(c * 4) + 0];
    const std::uint8_t a1 = s[(c * 4) + 1];
    const std::uint8_t a2 = s[(c * 4) + 2];
    const std::uint8_t a3 = s[(c * 4) + 3];
    if (inverse) {
      s[(c * 4) + 0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
      s[(c * 4) + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
      s[(c * 4) + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
      s[(c * 4) + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
    } else {
      s[(c * 4) + 0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
      s[(c * 4) + 1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
      s[(c * 4) + 2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
      s[(c * 4) + 3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
    }
  }
}

State to_state(const Aes::Block& block) noexcept {
  State s{};
  for (std::size_t i = 0; i < 16; ++i) {
    s[i] = std::to_integer<std::uint8_t>(block[i]);
  }
  return s;
}

Aes::Block to_block(const State& s) noexcept {
  Aes::Block block{};
  for (std::size_t i = 0; i < 16; ++i) {
    block[i] = static_cast<std::byte>(s[i]);
  }
  return block;
}

}  // namespace

// ── RC4 ─────────────────────────────────────────────────────────────────────────────────────

Rc4::Rc4(std::span<const std::byte> key) noexcept {
  for (std::size_t i = 0; i < 256; ++i) {
    s_[i] = static_cast<std::uint8_t>(i);
  }
  if (key.empty()) {
    return;
  }
  std::uint8_t j = 0;
  for (std::size_t i = 0; i < 256; ++i) {
    j = static_cast<std::uint8_t>(j + s_[i] + std::to_integer<std::uint8_t>(key[i % key.size()]));
    std::swap(s_[i], s_[j]);
  }
}

void Rc4::apply(std::span<std::byte> data) noexcept {
  for (std::byte& b : data) {
    i_ = static_cast<std::uint8_t>(i_ + 1);
    j_ = static_cast<std::uint8_t>(j_ + s_[i_]);
    std::swap(s_[i_], s_[j_]);
    b ^= static_cast<std::byte>(s_[static_cast<std::uint8_t>(s_[i_] + s_[j_])]);
  }
}

// ── AES ─────────────────────────────────────────────────────────────────────────────────────

Result<Aes> Aes::create(std::span<const std::byte> key) {
  if (key.size() != 16 && key.size() != 32) {
    return make_error(ErrorCode::kCorruptData, "AES key must be 16 or 32 bytes");
  }
  Aes aes;
  const std::size_t nk = key.size() / 4;  // key length in 32-bit words
  aes.rounds_ = nk + 6;
  const std::size_t total_words = 4 * (aes.rounds_ + 1);
  std::uint8_t rcon = 1;
  for (std::size_t i = 0; i < key.size(); ++i) {
    aes.round_keys_[i] = std::to_integer<std::uint8_t>(key[i]);
  }
  for (std::size_t w = nk; w < total_words; ++w) {
    std::array<std::uint8_t, 4> t{};
    std::copy_n(aes.round_keys_.begin() + static_cast<std::ptrdiff_t>((w - 1) * 4), 4, t.begin());
    if (w % nk == 0) {
      t = {static_cast<std::uint8_t>(kBoxes.forward[t[1]] ^ rcon), kBoxes.forward[t[2]], kBoxes.forward[t[3]], kBoxes.forward[t[0]]};
      rcon = xtime(rcon);
    } else if (nk > 6 && w % nk == 4) {
      for (std::uint8_t& b : t) {
        b = kBoxes.forward[b];
      }
    }
    for (std::size_t k = 0; k < 4; ++k) {
      aes.round_keys_[(w * 4) + k] = aes.round_keys_[((w - nk) * 4) + k] ^ t[k];
    }
  }
  return aes;
}

Aes::Block Aes::encrypt_block(const Block& in) const noexcept {
  State s = to_state(in);
  add_round_key(s, round_keys_.data());
  for (std::size_t round = 1; round <= rounds_; ++round) {
    for (std::uint8_t& b : s) {
      b = kBoxes.forward[b];
    }
    shift_rows(s, false);
    if (round != rounds_) {
      mix_columns(s, false);
    }
    add_round_key(s, round_keys_.data() + (round * 16));
  }
  return to_block(s);
}

Aes::Block Aes::decrypt_block(const Block& in) const noexcept {
  State s = to_state(in);
  add_round_key(s, round_keys_.data() + (rounds_ * 16));
  for (std::size_t round = rounds_; round-- > 0;) {
    shift_rows(s, true);
    for (std::uint8_t& b : s) {
      b = kBoxes.inverse[b];
    }
    add_round_key(s, round_keys_.data() + (round * 16));
    if (round != 0) {
      mix_columns(s, true);
    }
  }
  return to_block(s);
}

Result<std::vector<std::byte>> aes_cbc_decrypt(const Aes& aes, const Aes::Block& iv, std::span<const std::byte> data, bool pkcs7_padding) {
  if (data.size() % Aes::kBlockSize != 0 || (pkcs7_padding && data.empty())) {
    return make_error(ErrorCode::kCorruptData, "AES-CBC data is not a whole number of blocks");
  }
  std::vector<std::byte> out(data.size());
  Aes::Block previous = iv;
  for (std::size_t pos = 0; pos < data.size(); pos += Aes::kBlockSize) {
    Aes::Block block{};
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(pos), Aes::kBlockSize, block.begin());
    const Aes::Block plain = aes.decrypt_block(block);
    for (std::size_t i = 0; i < Aes::kBlockSize; ++i) {
      out[pos + i] = plain[i] ^ previous[i];
    }
    previous = block;
  }
  if (pkcs7_padding) {
    const auto pad = std::to_integer<std::size_t>(out.back());
    if (pad == 0 || pad > Aes::kBlockSize) {
      return make_error(ErrorCode::kCorruptData, "invalid AES-CBC padding");
    }
    for (std::size_t i = out.size() - pad; i < out.size(); ++i) {
      if (std::to_integer<std::size_t>(out[i]) != pad) {
        return make_error(ErrorCode::kCorruptData, "invalid AES-CBC padding");
      }
    }
    out.resize(out.size() - pad);
  }
  return out;
}

Result<std::vector<std::byte>> aes_cbc_encrypt(const Aes& aes, const Aes::Block& iv, std::span<const std::byte> data) {
  if (data.size() % Aes::kBlockSize != 0) {
    return make_error(ErrorCode::kCorruptData, "AES-CBC data is not a whole number of blocks");
  }
  std::vector<std::byte> out(data.size());
  Aes::Block previous = iv;
  for (std::size_t pos = 0; pos < data.size(); pos += Aes::kBlockSize) {
    Aes::Block block{};
    for (std::size_t i = 0; i < Aes::kBlockSize; ++i) {
      block[i] = data[pos + i] ^ previous[i];
    }
    previous = aes.encrypt_block(block);
    std::ranges::copy(previous, out.begin() + static_cast<std::ptrdiff_t>(pos));
  }
  return out;
}

}  // namespace capriole::crypto
