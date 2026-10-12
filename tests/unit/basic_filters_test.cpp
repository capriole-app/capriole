#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "capriole/filters/basic.hpp"

namespace capriole::filters {
namespace {

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    out[i] = static_cast<std::byte>(text[i]);
  }
  return out;
}

std::string text(const Result<std::vector<std::byte>>& data) {
  EXPECT_TRUE(data.has_value()) << (data ? "" : data.error().message);
  std::string out;
  if (data) {
    for (const std::byte b : *data) {
      out.push_back(static_cast<char>(b));
    }
  }
  return out;
}

constexpr std::size_t kNoLimit = 1U << 30U;

// ── ASCIIHexDecode ──────────────────────────────────────────────────────────────────────────

TEST(AsciiHexDecode, Basic) {
  EXPECT_EQ(text(ascii_hex_decode(bytes("48656C6C6F>"), kNoLimit)), "Hello");
  EXPECT_EQ(text(ascii_hex_decode(bytes("48 65\n6c 6C\r6f >"), kNoLimit)), "Hello");
  EXPECT_EQ(text(ascii_hex_decode(bytes("414>"), kNoLimit)), "A@");  // odd digit: implied 0
  EXPECT_EQ(text(ascii_hex_decode(bytes(">"), kNoLimit)), "");
}

TEST(AsciiHexDecode, Errors) {
  EXPECT_EQ(ascii_hex_decode(bytes("4G>"), kNoLimit).error().code, ErrorCode::kCorruptData);
  EXPECT_EQ(ascii_hex_decode(bytes("4142"), kNoLimit).error().code, ErrorCode::kCorruptData);  // no '>'
  EXPECT_EQ(ascii_hex_decode(bytes("414243>"), 2).error().code, ErrorCode::kLimitExceeded);
}

// ── ASCII85Decode ───────────────────────────────────────────────────────────────────────────

TEST(Ascii85Decode, Basic) {
  // "Man is" in Ascii85 (a classic example), plus partial groups of every length.
  EXPECT_EQ(text(ascii85_decode(bytes("9jqo^BlbD-BleB1DJ+*+F(f,q~>"), kNoLimit)), "Man is distinguished");
  EXPECT_EQ(text(ascii85_decode(bytes("5l~>"), kNoLimit)), "A");
  EXPECT_EQ(text(ascii85_decode(bytes("5sb~>"), kNoLimit)), "AB");
  EXPECT_EQ(text(ascii85_decode(bytes("5sdp~>"), kNoLimit)), "ABC");
  EXPECT_EQ(text(ascii85_decode(bytes("5sdq,~>"), kNoLimit)), "ABCD");
  EXPECT_EQ(text(ascii85_decode(bytes("z~>"), kNoLimit)), std::string(4, '\0'));
  EXPECT_EQ(text(ascii85_decode(bytes("5s dq\n,~>"), kNoLimit)), "ABCD");  // whitespace anywhere
  EXPECT_EQ(text(ascii85_decode(bytes("~>"), kNoLimit)), "");
}

TEST(Ascii85Decode, Errors) {
  EXPECT_EQ(ascii85_decode(bytes("5sdq,"), kNoLimit).error().code, ErrorCode::kCorruptData);     // no ~>
  EXPECT_EQ(ascii85_decode(bytes("5~>"), kNoLimit).error().code, ErrorCode::kCorruptData);       // 1-char group
  EXPECT_EQ(ascii85_decode(bytes("5szq,~>"), kNoLimit).error().code, ErrorCode::kCorruptData);   // z inside a group
  EXPECT_EQ(ascii85_decode(bytes("s8W-\"~>"), kNoLimit).error().code, ErrorCode::kCorruptData);  // group > 2^32-1
  EXPECT_EQ(ascii85_decode(bytes("5sdq,v~>"), kNoLimit).error().code, ErrorCode::kCorruptData);  // 'v' out of range
  EXPECT_EQ(ascii85_decode(bytes("5s~x"), kNoLimit).error().code, ErrorCode::kCorruptData);      // '~' without '>'
  EXPECT_EQ(ascii85_decode(bytes("zz~>"), 4).error().code, ErrorCode::kLimitExceeded);
}

// ── LZWDecode ───────────────────────────────────────────────────────────────────────────────

// Packs codes MSB-first at the widths an EarlyChange=1 encoder uses (all 9 bits here).
std::vector<std::byte> pack9(const std::vector<unsigned>& codes) {
  std::vector<std::byte> out;
  unsigned buffer = 0;
  unsigned bits = 0;
  for (const unsigned code : codes) {
    buffer = (buffer << 9U) | code;
    bits += 9;
    while (bits >= 8) {
      out.push_back(static_cast<std::byte>((buffer >> (bits - 8)) & 0xFFU));
      bits -= 8;
    }
  }
  if (bits > 0) {
    out.push_back(static_cast<std::byte>((buffer << (8 - bits)) & 0xFFU));
  }
  return out;
}

TEST(LzwDecode, SpecificationExample) {
  // ISO 32000-2 §7.4.4.2: "-----A---B" encodes as 256 45 258 258 65 259 66 257.
  EXPECT_EQ(text(lzw_decode(pack9({256, 45, 258, 258, 65, 259, 66, 257}), true, kNoLimit)), "-----A---B");
}

TEST(LzwDecode, SpecificationExampleBytes) {
  // The same example as the byte sequence printed in the standard.
  const std::vector<std::byte> data = {std::byte{0x80}, std::byte{0x0B}, std::byte{0x60}, std::byte{0x50}, std::byte{0x22},
                                       std::byte{0x0C}, std::byte{0x0C}, std::byte{0x85}, std::byte{0x01}};
  EXPECT_EQ(text(lzw_decode(data, true, kNoLimit)), "-----A---B");
}

TEST(LzwDecode, KwKwKCase) {
  // Code 258 used right after it is defined: "aaa" = 97, 258.
  EXPECT_EQ(text(lzw_decode(pack9({97, 258, 257}), true, kNoLimit)), "aaa");
}

TEST(LzwDecode, WidthGrowsToTenBits) {
  // 256 literals fill the table up to entry 512, so the decoder must switch to 10-bit codes midway.
  std::string expected;
  std::vector<unsigned> codes = {256};
  for (unsigned i = 0; i < 256; ++i) {
    codes.push_back(i);
    expected.push_back(static_cast<char>(i));
  }
  // Codes 258..511 are now "(i)(i+1)"; re-emit a few of them, then the end code.
  const std::vector<unsigned> tail = {258, 300, 400, 257};
  // Emit the first 256 literals at 9 bits, then the tail at the width the decoder expects.
  std::vector<std::byte> packed;
  unsigned long long buffer = 0;
  unsigned bits = 0;
  unsigned next = 258;
  unsigned width = 9;
  const auto put = [&](unsigned code) {
    buffer = (buffer << width) | code;
    bits += width;
    while (bits >= 8) {
      packed.push_back(static_cast<std::byte>((buffer >> (bits - 8)) & 0xFFU));
      bits -= 8;
    }
  };
  bool first = true;
  for (const unsigned code : codes) {
    put(code);
    if (code == 256) {
      continue;
    }
    if (!first) {
      ++next;
    }
    first = false;
    if (next + 1 >= (1U << width)) {
      ++width;
    }
  }
  for (const unsigned code : tail) {
    put(code);
    if (code == 257) {
      break;
    }
    ++next;
    if (next + 1 >= (1U << width) && width < 12) {
      ++width;
    }
  }
  if (bits > 0) {
    packed.push_back(static_cast<std::byte>((buffer << (8 - bits)) & 0xFFU));
  }
  // Entry 258 = "\x00\x01", 300 = "\x2A\x2B", 400 = "\x8E\x8F".
  expected += std::string("\x00\x01", 2) + "\x2A\x2B" + "\x8E\x8F";
  EXPECT_EQ(text(lzw_decode(packed, true, kNoLimit)), expected);
}

TEST(LzwDecode, Errors) {
  EXPECT_EQ(lzw_decode(pack9({65, 66}), true, kNoLimit).error().code, ErrorCode::kCorruptData);        // no end code
  EXPECT_EQ(lzw_decode(pack9({300, 257}), true, kNoLimit).error().code, ErrorCode::kCorruptData);      // undefined first code
  EXPECT_EQ(lzw_decode(pack9({65, 400, 257}), true, kNoLimit).error().code, ErrorCode::kCorruptData);  // code beyond the table
  EXPECT_EQ(lzw_decode(pack9({65, 258, 259, 260, 257}), true, 3).error().code, ErrorCode::kLimitExceeded);
}

// ── RunLengthDecode ─────────────────────────────────────────────────────────────────────────

TEST(RunLengthDecode, Basic) {
  EXPECT_EQ(text(run_length_decode(bytes(std::string("\x02"
                                                     "abc"
                                                     "\xFE"
                                                     "x"
                                                     "\x80",
                                                     7)),
                                   kNoLimit)),
            "abcxxx");
  EXPECT_EQ(text(run_length_decode(bytes("\x80"), kNoLimit)), "");
  EXPECT_EQ(text(run_length_decode(bytes(std::string("\x81"
                                                     "z"
                                                     "\x80",
                                                     3)),
                                   kNoLimit)),
            std::string(128, 'z'));
}

TEST(RunLengthDecode, Errors) {
  EXPECT_EQ(run_length_decode(bytes("\x02"
                                    "ab"),
                              kNoLimit)
                .error()
                .code,
            ErrorCode::kCorruptData);                                                           // literal run cut short
  EXPECT_EQ(run_length_decode(bytes("\xFE"), kNoLimit).error().code, ErrorCode::kCorruptData);  // repeat byte missing
  EXPECT_EQ(run_length_decode(bytes(std::string("\x00"
                                                "a",
                                                2)),
                              kNoLimit)
                .error()
                .code,
            ErrorCode::kCorruptData);  // no end byte
  EXPECT_EQ(run_length_decode(bytes(std::string("\x81"
                                                "z"
                                                "\x80",
                                                3)),
                              100)
                .error()
                .code,
            ErrorCode::kLimitExceeded);
}

}  // namespace
}  // namespace capriole::filters
