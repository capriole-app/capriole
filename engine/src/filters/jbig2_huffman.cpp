#include "jbig2_huffman.hpp"

#include <algorithm>
#include <array>

namespace capriole::filters::jbig2 {

unsigned BitReader::bit() {
  if (at_end()) {
    ++at_;
    return 0;
  }
  const unsigned b = (std::to_integer<unsigned>(data_[at_ / 8]) >> (7 - (at_ % 8))) & 1U;
  ++at_;
  return b;
}

std::uint32_t BitReader::bits(unsigned count) {
  std::uint32_t v = 0;
  for (unsigned i = 0; i < count; ++i) {
    v = (v << 1U) | bit();
  }
  return v;
}

HuffmanTable::HuffmanTable(std::vector<HuffmanLine> lines) : lines_(std::move(lines)) {
  codes_.assign(lines_.size(), 0);
  for (const HuffmanLine& line : lines_) {
    max_length_ = std::max(max_length_, line.prefix_length);
  }
  // B.3: LENCOUNT per length, FIRSTCODE per length, codes in line order within a length.
  std::vector<std::uint32_t> count(max_length_ + 1, 0);
  for (const HuffmanLine& line : lines_) {
    ++count.at(line.prefix_length);
  }
  count.at(0) = 0;
  std::uint32_t first = 0;
  for (std::uint32_t length = 1; length <= max_length_; ++length) {
    first = (first + count.at(length - 1)) << 1U;
    std::uint32_t code = first;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
      if (lines_[i].prefix_length == length) {
        codes_[i] = code++;
      }
    }
  }
}

HuffmanValue HuffmanTable::decode(BitReader& in) const {
  std::uint32_t code = 0;
  for (std::uint32_t length = 1; length <= max_length_ && length <= 32; ++length) {
    code = (code << 1U) | in.bit();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
      const HuffmanLine& line = lines_[i];
      if (line.prefix_length != length || codes_[i] != code) {
        continue;
      }
      switch (line.kind) {
        case HuffmanLine::Kind::kOob:
          return {.ok = true, .oob = true, .value = 0};
        case HuffmanLine::Kind::kLower:
          return {.ok = true, .oob = false, .value = line.range_low - static_cast<std::int64_t>(in.bits(32))};
        case HuffmanLine::Kind::kUpper:
          return {.ok = true, .oob = false, .value = line.range_low + static_cast<std::int64_t>(in.bits(32))};
        case HuffmanLine::Kind::kNormal:
          return {.ok = true, .oob = false, .value = line.range_low + static_cast<std::int64_t>(in.bits(std::min(line.range_length, 32U)))};
      }
    }
  }
  return {};
}

namespace {

using K = HuffmanLine::Kind;

HuffmanLine n(std::int64_t low, std::uint32_t prefix, std::uint32_t range) {
  return {.range_low = low, .prefix_length = prefix, .range_length = range, .kind = K::kNormal};
}
HuffmanLine lower(std::int64_t low, std::uint32_t prefix) { return {.range_low = low, .prefix_length = prefix, .range_length = 32, .kind = K::kLower}; }
HuffmanLine upper(std::int64_t low, std::uint32_t prefix) { return {.range_low = low, .prefix_length = prefix, .range_length = 32, .kind = K::kUpper}; }
HuffmanLine oob(std::uint32_t prefix) { return {.range_low = 0, .prefix_length = prefix, .range_length = 0, .kind = K::kOob}; }

// Tables B.1 to B.15, in the order of their lines (which decides the codes within a length).
std::vector<HuffmanLine> standard_lines(int table) {
  switch (table) {
    case 1:
      return {n(0, 1, 4), n(16, 2, 8), n(272, 3, 16), upper(65808, 3)};
    case 2:
      return {n(0, 1, 0), n(1, 2, 0), n(2, 3, 0), n(3, 4, 3), n(11, 5, 6), upper(75, 6), oob(6)};
    case 3:
      return {n(-256, 8, 8), n(0, 1, 0), n(1, 2, 0), n(2, 3, 0), n(3, 4, 3), n(11, 5, 6), lower(-257, 8), upper(75, 7), oob(6)};
    case 4:
      return {n(1, 1, 0), n(2, 2, 0), n(3, 3, 0), n(4, 4, 3), n(12, 5, 6), upper(76, 5)};
    case 5:
      return {n(-255, 7, 8), n(1, 1, 0), n(2, 2, 0), n(3, 3, 0), n(4, 4, 3), n(12, 5, 6), lower(-256, 7), upper(76, 6)};
    case 6:
      return {n(-2048, 5, 10), n(-1024, 4, 9), n(-512, 4, 8), n(-256, 4, 7), n(-128, 5, 6),  n(-64, 5, 5),    n(-32, 4, 5),
              n(0, 2, 7),      n(128, 3, 7),   n(256, 3, 8),  n(512, 4, 9),  n(1024, 4, 10), lower(-2049, 6), upper(2048, 6)};
    case 7:
      return {n(-1024, 4, 9), n(-512, 3, 8), n(-256, 4, 7), n(-128, 5, 6), n(-64, 5, 5),   n(-32, 4, 5),    n(0, 4, 5),    n(32, 5, 5),
              n(64, 5, 6),    n(128, 4, 7),  n(256, 3, 8),  n(512, 3, 9),  n(1024, 3, 10), lower(-1025, 5), upper(2048, 5)};
    case 8:
      return {n(-15, 8, 3), n(-7, 9, 1),  n(-5, 8, 1),  n(-3, 9, 0),   n(-2, 7, 0),   n(-1, 4, 0),    n(0, 2, 1),
              n(2, 5, 0),   n(3, 6, 0),   n(4, 3, 4),   n(20, 6, 1),   n(22, 4, 4),   n(38, 4, 5),    n(70, 5, 6),
              n(134, 5, 7), n(262, 6, 7), n(390, 7, 8), n(646, 6, 10), lower(-16, 9), upper(1670, 9), oob(2)};
    case 9:
      return {n(-31, 8, 4), n(-15, 9, 2), n(-11, 8, 2),   n(-7, 9, 1),   n(-5, 7, 1),    n(-3, 4, 1), n(-1, 3, 1),  n(1, 3, 1),
              n(3, 5, 1),   n(5, 6, 1),   n(7, 3, 5),     n(39, 6, 2),   n(43, 4, 5),    n(75, 4, 6), n(139, 5, 7), n(267, 5, 8),
              n(523, 6, 8), n(779, 7, 9), n(1291, 6, 11), lower(-32, 9), upper(3339, 9), oob(2)};
    case 10:
      return {n(-21, 7, 4), n(-5, 8, 0),  n(-4, 7, 0),    n(-3, 5, 0),    n(-2, 2, 2),   n(2, 5, 0),     n(3, 6, 0),
              n(4, 7, 0),   n(5, 8, 0),   n(6, 2, 6),     n(70, 5, 5),    n(102, 6, 5),  n(134, 6, 6),   n(198, 6, 7),
              n(326, 6, 8), n(582, 6, 9), n(1094, 6, 10), n(2118, 7, 11), lower(-22, 8), upper(4166, 8), oob(2)};
    case 11:
      return {n(1, 1, 0),  n(2, 2, 1),  n(4, 4, 0),  n(5, 4, 1),  n(7, 5, 1),  n(9, 5, 2),   n(13, 6, 2),
              n(17, 7, 2), n(21, 7, 3), n(29, 7, 4), n(45, 7, 5), n(77, 7, 6), upper(141, 7)};
    case 12:
      return {n(1, 1, 0),  n(2, 2, 0),  n(3, 3, 1),  n(5, 5, 0),  n(6, 5, 1),  n(8, 6, 1),  n(10, 7, 0),
              n(11, 7, 1), n(13, 7, 2), n(17, 7, 3), n(25, 7, 4), n(41, 8, 5), upper(73, 8)};
    case 13:
      return {n(1, 1, 0),  n(2, 3, 0),  n(3, 4, 0),  n(4, 5, 0),  n(5, 4, 1),  n(7, 3, 3),   n(15, 6, 1),
              n(17, 6, 2), n(21, 6, 3), n(29, 6, 4), n(45, 6, 5), n(77, 7, 6), upper(141, 7)};
    case 14:
      return {n(-2, 3, 0), n(-1, 3, 0), n(0, 1, 0), n(1, 3, 0), n(2, 3, 0)};
    case 15:
      return {n(-24, 7, 4), n(-8, 6, 2), n(-4, 5, 1), n(-2, 4, 0), n(-1, 3, 0),   n(0, 1, 0),  n(1, 3, 0),
              n(2, 4, 0),   n(3, 5, 1),  n(5, 6, 2),  n(9, 7, 4),  lower(-25, 7), upper(25, 7)};
    default:
      return {};
  }
}

}  // namespace

const HuffmanTable& standard_table(int table) {
  static const std::array<HuffmanTable, 15> kTables = [] {
    std::array<HuffmanTable, 15> tables;
    for (int i = 0; i < 15; ++i) {
      tables.at(static_cast<std::size_t>(i)) = HuffmanTable(standard_lines(i + 1));
    }
    return tables;
  }();
  return kTables.at(static_cast<std::size_t>(std::clamp(table, 1, 15) - 1));
}

Result<HuffmanTable> parse_table_segment(std::span<const std::byte> data) {
  if (data.size() < 9) {
    return make_error(ErrorCode::kCorruptData, "JBIG2 table segment too short");
  }
  const auto flags = std::to_integer<unsigned>(data[0]);
  const bool has_oob = (flags & 1U) != 0;
  const unsigned prefix_bits = ((flags >> 1U) & 7U) + 1;
  const unsigned range_bits = ((flags >> 4U) & 7U) + 1;
  const auto s32 = [&](std::size_t at) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      v = (v << 8U) | std::to_integer<std::uint32_t>(data[at + i]);
    }
    return static_cast<std::int64_t>(static_cast<std::int32_t>(v));
  };
  const std::int64_t low = s32(1);
  const std::int64_t high = s32(5);
  if (high <= low) {
    return make_error(ErrorCode::kCorruptData, "JBIG2 table with an empty range");
  }
  BitReader in(data.subspan(9));
  std::vector<HuffmanLine> lines;
  constexpr std::size_t kMaxLines = 4096;
  for (std::int64_t current = low; current < high;) {
    if (lines.size() >= kMaxLines || in.at_end()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 table with too many lines");
    }
    const std::uint32_t prefix = in.bits(prefix_bits);
    const std::uint32_t range = in.bits(range_bits);
    if (range > 32) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 table with a range over 32 bits");
    }
    lines.push_back(n(current, prefix, range));
    current += std::int64_t{1} << range;
  }
  lines.push_back(lower(low - 1, in.bits(prefix_bits)));
  lines.push_back(upper(high, in.bits(prefix_bits)));
  if (has_oob) {
    lines.push_back(oob(in.bits(prefix_bits)));
  }
  return HuffmanTable(std::move(lines));
}

HuffmanTable table_from_lengths(std::span<const std::uint32_t> lengths) {
  std::vector<HuffmanLine> lines;
  lines.reserve(lengths.size());
  for (std::size_t i = 0; i < lengths.size(); ++i) {
    lines.push_back(n(static_cast<std::int64_t>(i), std::min(lengths[i], 32U), 0));
  }
  return HuffmanTable(std::move(lines));
}

}  // namespace capriole::filters::jbig2
