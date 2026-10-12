#pragma once

// Internal to the JBIG2 decoder (filters/jbig2.cpp): Huffman tables (ITU-T T.88 Annex B) — the
// fifteen standard tables and the ones a table segment defines — and a big-endian bit reader.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::filters::jbig2 {

class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) : data_(data) {}

  // Past the end, 0 bits (and at_end() says so).
  unsigned bit();
  std::uint32_t bits(unsigned count);  // count <= 32
  void align() { at_ = (at_ + 7) / 8 * 8; }
  [[nodiscard]] bool at_end() const noexcept { return at_ >= data_.size() * 8; }
  // Far past the end (reading zeros that are not there): see ArithmeticDecoder::exhausted.
  [[nodiscard]] bool exhausted() const noexcept { return at_ > (data_.size() + 64) * 8; }
  // The byte the reader is in (or the next one, aligned).
  [[nodiscard]] std::size_t byte_position() const noexcept { return (at_ + 7) / 8; }
  void skip_bytes(std::size_t count) { at_ = (byte_position() + count) * 8; }

 private:
  std::span<const std::byte> data_;
  std::size_t at_ = 0;
};

struct HuffmanLine {
  enum class Kind : std::uint8_t { kNormal, kLower, kUpper, kOob };
  std::int64_t range_low = 0;
  std::uint32_t prefix_length = 0;  // 0: the line has no code
  std::uint32_t range_length = 0;
  Kind kind = Kind::kNormal;
};

struct HuffmanValue {
  bool ok = false;   // false: no code matched (damaged data)
  bool oob = false;  // the out-of-band value
  std::int64_t value = 0;
};

class HuffmanTable {
 public:
  HuffmanTable() = default;
  // Assigns the prefix codes (B.3, canonical order: by length, then by line order).
  explicit HuffmanTable(std::vector<HuffmanLine> lines);

  [[nodiscard]] HuffmanValue decode(BitReader& in) const;
  [[nodiscard]] const std::vector<HuffmanLine>& lines() const noexcept { return lines_; }
  [[nodiscard]] const std::vector<std::uint32_t>& codes() const noexcept { return codes_; }

 private:
  std::vector<HuffmanLine> lines_;
  std::vector<std::uint32_t> codes_;  // per line
  std::uint32_t max_length_ = 0;
};

// Standard table B.n, n from 1 to 15.
[[nodiscard]] const HuffmanTable& standard_table(int table);

// A table segment's data (7.4.13, B.2).
[[nodiscard]] Result<HuffmanTable> parse_table_segment(std::span<const std::byte> data);

// Lengths → codes for a symbol ID table (the run-coded lengths of 7.4.3.1.7): lines of their own.
[[nodiscard]] HuffmanTable table_from_lengths(std::span<const std::uint32_t> lengths);

}  // namespace capriole::filters::jbig2
