#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "capriole/crypto/cipher.hpp"
#include "capriole/crypto/hash.hpp"

namespace capriole::crypto {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out;
  for (const char ch : text) {
    out.push_back(static_cast<std::byte>(ch));
  }
  return out;
}

std::vector<std::byte> from_hex(const std::string& hex) {
  std::vector<std::byte> out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::byte>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

template <typename Container>
std::string to_hex(const Container& data) {
  static constexpr char kDigits[] = "0123456789abcdef";  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  std::string out;
  for (const std::byte b : data) {
    out.push_back(kDigits[std::to_integer<unsigned>(b) >> 4U]);
    out.push_back(kDigits[std::to_integer<unsigned>(b) & 0xFU]);
  }
  return out;
}

// Deterministic test pattern shared with the reference values computed by Python's hashlib.
std::vector<std::byte> pattern(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = static_cast<std::byte>(((i * 7) + 3) & 0xFFU);
  }
  return out;
}

// ── MD5 (RFC 1321 Appendix A.5) ─────────────────────────────────────────────────────────────

TEST(Md5, Rfc1321Vectors) {
  EXPECT_EQ(to_hex(md5(bytes(""))), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(to_hex(md5(bytes("a"))), "0cc175b9c0f1b6a831c399e269772661");
  EXPECT_EQ(to_hex(md5(bytes("abc"))), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(to_hex(md5(bytes("message digest"))), "f96b697d7cb7938d525a2f31aaf161d0");
  EXPECT_EQ(to_hex(md5(bytes("abcdefghijklmnopqrstuvwxyz"))), "c3fcd3d76192e4007dfb496cca67e13b");
  EXPECT_EQ(to_hex(md5(bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"))), "d174ab98d277d9f5a5611c2c9f419d9f");
  EXPECT_EQ(to_hex(md5(bytes("12345678901234567890123456789012345678901234567890123456789012345678901234567890"))), "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(Md5, BlockBoundaries) {
  EXPECT_EQ(to_hex(md5(pattern(55))), "52c0e574e1198de5fe3f8f11440dcb1b");
  EXPECT_EQ(to_hex(md5(pattern(56))), "46c9907fc908ee68b1e7b8e71286a518");
  EXPECT_EQ(to_hex(md5(pattern(63))), "a62f6d59e837867693f042f5b8f5a236");
  EXPECT_EQ(to_hex(md5(pattern(64))), "7160b8fb5e9e4023d549c3971fbaeead");
  EXPECT_EQ(to_hex(md5(pattern(65))), "70bd662e7aefbda85a0f7244167b7897");
  EXPECT_EQ(to_hex(md5(pattern(200))), "4c79b81ac94bad7a875519ce6b964c66");
}

// ── SHA-2 (FIPS 180-4 examples) ─────────────────────────────────────────────────────────────

TEST(Sha256, StandardVectors) {
  EXPECT_EQ(to_hex(sha256(bytes(""))), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(to_hex(sha256(bytes("abc"))), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(to_hex(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  Sha256 million;
  const std::vector<std::byte> chunk(1000, std::byte{'a'});
  for (int i = 0; i < 1000; ++i) {
    million.update(chunk);
  }
  EXPECT_EQ(to_hex(million.finish()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, BlockBoundaries) {
  EXPECT_EQ(to_hex(sha256(pattern(55))), "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b");
  EXPECT_EQ(to_hex(sha256(pattern(56))), "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27");
  EXPECT_EQ(to_hex(sha256(pattern(64))), "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241");
  EXPECT_EQ(to_hex(sha256(pattern(200))), "2c7e18c942ef065b526a2d4e5546283749cd3ddfb51d8fc71f42717363685f46");
}

TEST(Sha512, StandardVectors) {
  EXPECT_EQ(to_hex(sha512(bytes("abc"))),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  EXPECT_EQ(to_hex(sha384(bytes("abc"))), "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
  EXPECT_EQ(to_hex(sha512(bytes(""))),
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
}

TEST(Sha512, BlockBoundaries) {
  for (const auto& [n, sha384_prefix, sha512_prefix] : {std::tuple{111U, "341388a9dc2275074e90cf3943237619", "68cffa6d0d76f309c9ce0d35280939f8"},
                                                        std::tuple{112U, "619cc5d06138526d70659eccf602d197", "d0865c524d1dddf7c23b799c413f5adc"},
                                                        std::tuple{127U, "f8234ab81f5f9641c656680825a9bbb3", "e0b6a20f1c0c88970a9340152cd5a1c1"},
                                                        std::tuple{128U, "e8480e9c4dd90f88104a79cbaccec48e", "99b16f17aa0b969a5b8f08f367719d51"},
                                                        std::tuple{200U, "8deb83535fa35d2f493c6c3695b1057b", "cca3c0276046ef9f2897bdfc3ec330f7"}}) {
    EXPECT_EQ(to_hex(sha384(pattern(n))).substr(0, 32), sha384_prefix) << n;
    EXPECT_EQ(to_hex(sha512(pattern(n))).substr(0, 32), sha512_prefix) << n;
  }
}

TEST(Hash, IncrementalEqualsOneShot) {
  const auto data = pattern(1000);
  for (const std::size_t split : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{64}, std::size_t{127}, std::size_t{500}}) {
    Md5 m;
    Sha256 s;
    Sha512 l;
    for (const auto part : {std::span(data).first(split), std::span(data).subspan(split)}) {
      m.update(part);
      s.update(part);
      l.update(part);
    }
    EXPECT_EQ(m.finish(), md5(data)) << split;
    EXPECT_EQ(s.finish(), sha256(data)) << split;
    EXPECT_EQ(l.finish(), sha512(data)) << split;
  }
}

// ── RC4 ─────────────────────────────────────────────────────────────────────────────────────

std::string rc4_hex(const std::string& key, const std::string& plain) {
  auto data = bytes(plain);
  Rc4 rc4(bytes(key));
  rc4.apply(data);
  return to_hex(data);
}

TEST(Rc4, PublishedVectors) {
  EXPECT_EQ(rc4_hex("Key", "Plaintext"), "bbf316e8d940af0ad3");
  EXPECT_EQ(rc4_hex("Wiki", "pedia"), "1021bf0420");
  EXPECT_EQ(rc4_hex("Secret", "Attack at dawn"), "45a01f645fc35b383552544b9bf5");
  // RFC 6229, 40-bit key 0x0102030405, keystream bytes 0..15.
  std::vector<std::byte> zeros(16);
  Rc4 rc4(from_hex("0102030405"));
  rc4.apply(zeros);
  EXPECT_EQ(to_hex(zeros), "b2396305f03dc027ccc3524a0a1118a8");
}

TEST(Rc4, IsItsOwnInverse) {
  auto data = pattern(300);
  const auto original = data;
  Rc4(bytes("some key")).apply(data);
  EXPECT_NE(data, original);
  Rc4(bytes("some key")).apply(data);
  EXPECT_EQ(data, original);
}

// ── AES (FIPS 197 Appendix C, NIST SP 800-38A F.2) ──────────────────────────────────────────

Aes::Block block(const std::string& hex) {
  Aes::Block b{};
  const auto data = from_hex(hex);
  std::copy(data.begin(), data.end(), b.begin());
  return b;
}

TEST(Aes, Fips197Vectors) {
  const auto aes128 = Aes::create(from_hex("000102030405060708090a0b0c0d0e0f"));
  ASSERT_TRUE(aes128.has_value());
  EXPECT_EQ(to_hex(aes128->encrypt_block(block("00112233445566778899aabbccddeeff"))), "69c4e0d86a7b0430d8cdb78070b4c55a");
  EXPECT_EQ(to_hex(aes128->decrypt_block(block("69c4e0d86a7b0430d8cdb78070b4c55a"))), "00112233445566778899aabbccddeeff");

  const auto aes256 = Aes::create(from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
  ASSERT_TRUE(aes256.has_value());
  EXPECT_EQ(to_hex(aes256->encrypt_block(block("00112233445566778899aabbccddeeff"))), "8ea2b7ca516745bfeafc49904b496089");
  EXPECT_EQ(to_hex(aes256->decrypt_block(block("8ea2b7ca516745bfeafc49904b496089"))), "00112233445566778899aabbccddeeff");

  EXPECT_FALSE(Aes::create(from_hex("0011")).has_value());
}

TEST(Aes, CbcVectors) {
  // SP 800-38A F.2.1 (CBC-AES128), first two blocks.
  const auto aes = Aes::create(from_hex("2b7e151628aed2a6abf7158809cf4f3c"));
  const auto iv = block("000102030405060708090a0b0c0d0e0f");
  const auto plain = from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
  const auto cipher = aes_cbc_encrypt(*aes, iv, plain);
  ASSERT_TRUE(cipher.has_value());
  EXPECT_EQ(to_hex(*cipher), "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
  EXPECT_EQ(*aes_cbc_decrypt(*aes, iv, *cipher, false), plain);

  // AES-256-CBC, checked against Python's `cryptography`.
  const auto aes256 = Aes::create(from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
  std::vector<std::byte> plain48(48);
  for (std::size_t i = 0; i < 48; ++i) {
    plain48[i] = static_cast<std::byte>(((i * 13) + 1) & 0xFFU);
  }
  const auto cipher256 = aes_cbc_encrypt(*aes256, block("101112131415161718191a1b1c1d1e1f"), plain48);
  EXPECT_EQ(to_hex(*cipher256), "fc04b37d515aa2c5d72d02987d43197e0390818e5dcc93ff7ca176d56210707beaf83373b487fdb1ebdd7855e0585de8");
}

TEST(Aes, CbcPadding) {
  const auto aes = Aes::create(from_hex("2b7e151628aed2a6abf7158809cf4f3c"));
  const auto iv = block("000102030405060708090a0b0c0d0e0f");
  // "hello" + 11 bytes of 0x0B padding, encrypted, decrypts back to "hello".
  auto padded = bytes("hello");
  padded.resize(16, std::byte{0x0B});
  const auto cipher = aes_cbc_encrypt(*aes, iv, padded);
  const auto plain = aes_cbc_decrypt(*aes, iv, *cipher, true);
  ASSERT_TRUE(plain.has_value());
  EXPECT_EQ(*plain, bytes("hello"));

  auto bad = bytes("hello");
  bad.resize(16, std::byte{0x11});  // padding value above the block size
  EXPECT_EQ(aes_cbc_decrypt(*aes, iv, *aes_cbc_encrypt(*aes, iv, bad), true).error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(aes_cbc_decrypt(*aes, iv, pattern(15), false).error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(aes_cbc_decrypt(*aes, iv, {}, true).error().code, ErrorCode::kCorruptData);
}

}  // namespace
}  // namespace capriole::crypto
