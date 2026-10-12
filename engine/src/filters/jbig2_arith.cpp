#include "jbig2_arith.hpp"

#include <array>

namespace capriole::filters::jbig2 {

namespace {

// Table E.1: Qe, the next index after an MPS and after an LPS, and whether an LPS switches the MPS.
struct QeEntry {
  std::uint16_t qe;
  std::uint8_t nmps;
  std::uint8_t nlps;
  std::uint8_t swap;
  constexpr QeEntry(std::uint16_t q, std::uint8_t mps, std::uint8_t lps, std::uint8_t sw) noexcept : qe(q), nmps(mps), nlps(lps), swap(sw) {}
};

constexpr std::array<QeEntry, 47> kQe = {{
    {0x5601, 1, 1, 1},   {0x3401, 2, 6, 0},   {0x1801, 3, 9, 0},   {0x0AC1, 4, 12, 0},  {0x0521, 5, 29, 0},  {0x0221, 38, 33, 0}, {0x5601, 7, 6, 1},
    {0x5401, 8, 14, 0},  {0x4801, 9, 14, 0},  {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0}, {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0},
    {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0}, {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0}, {0x3001, 21, 19, 0},
    {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0}, {0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
    {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0}, {0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0},
    {0x02A1, 36, 33, 0}, {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0}, {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0},
    {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0}, {0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0},
}};

}  // namespace

// INITDEC (E.3.5), in the software convention of E.3 (C split into its high and low halves).
ArithmeticDecoder::ArithmeticDecoder(std::span<const std::byte> data) : data_(data), c_high_(byte(0)), a_(0x8000) {
  byte_in();
  c_high_ = ((c_high_ << 7U) & 0xFFFFU) | ((c_low_ >> 9U) & 0x7FU);
  c_low_ = (c_low_ << 7U) & 0xFFFFU;
  ct_ -= 7;
}

unsigned ArithmeticDecoder::byte(std::size_t i) const noexcept { return i < data_.size() ? std::to_integer<unsigned>(data_[i]) : 0xFFU; }

// BYTEIN (E.3.4): a 0xFF followed by a byte above 0x8F is a marker — the end of the data: feed 1s.
void ArithmeticDecoder::byte_in() {
  if (at_ + 1 >= data_.size()) {
    ++past_end_;
  }
  if (byte(at_) == 0xFF) {
    if (byte(at_ + 1) > 0x8F) {
      c_low_ += 0xFF00;
      ct_ = 8;
    } else {
      ++at_;
      c_low_ += byte(at_) << 9U;
      ct_ = 7;
    }
  } else {
    ++at_;
    c_low_ += byte(at_) << 8U;
    ct_ = 8;
  }
  if (c_low_ > 0xFFFF) {
    c_high_ += c_low_ >> 16U;
    c_low_ &= 0xFFFFU;
  }
}

unsigned ArithmeticDecoder::decode(Contexts& contexts, std::size_t cx) {
  std::uint8_t& state = contexts.at(cx);
  std::size_t index = state >> 1U;
  unsigned mps = state & 1U;
  const QeEntry& e = kQe.at(index);
  const std::uint32_t qe = e.qe;
  unsigned d = 0;
  a_ -= qe;
  if (c_high_ < qe) {
    // LPS_EXCHANGE
    if (a_ < qe) {
      a_ = qe;
      d = mps;
      index = e.nmps;
    } else {
      a_ = qe;
      d = 1 - mps;
      if (e.swap != 0) {
        mps = d;
      }
      index = e.nlps;
    }
  } else {
    c_high_ -= qe;
    if ((a_ & 0x8000U) != 0) {
      return mps;
    }
    // MPS_EXCHANGE
    if (a_ < qe) {
      d = 1 - mps;
      if (e.swap != 0) {
        mps = d;
      }
      index = e.nlps;
    } else {
      d = mps;
      index = e.nmps;
    }
  }
  // RENORMD: shift until A is at least 0x8000 again (it always starts below it here).
  while ((a_ & 0x8000U) == 0) {
    if (ct_ == 0) {
      byte_in();
    }
    a_ <<= 1U;
    c_high_ = ((c_high_ << 1U) & 0xFFFFU) | ((c_low_ >> 15U) & 1U);
    c_low_ = (c_low_ << 1U) & 0xFFFFU;
    --ct_;
  }
  state = static_cast<std::uint8_t>((index << 1U) | mps);
  return d;
}

std::optional<std::int32_t> decode_integer(ArithmeticDecoder& decoder, Contexts& contexts) {
  // PREV keeps the last bits decoded (A.2): 9 bits, with bit 8 set once more than 8 were read.
  std::uint32_t prev = 1;
  const auto bits = [&](int count) {
    std::uint32_t v = 0;
    for (int i = 0; i < count; ++i) {
      const unsigned bit = decoder.decode(contexts, prev);
      prev = prev < 256 ? ((prev << 1U) | bit) : ((((prev << 1U) | bit) & 511U) | 256U);
      v = (v << 1U) | bit;
    }
    return v;
  };
  const std::uint32_t sign = bits(1);
  std::int64_t value = 0;
  if (bits(1) == 0) {
    value = bits(2);
  } else if (bits(1) == 0) {
    value = bits(4) + 4;
  } else if (bits(1) == 0) {
    value = bits(6) + 20;
  } else if (bits(1) == 0) {
    value = bits(8) + 84;
  } else if (bits(1) == 0) {
    value = bits(12) + 340;
  } else {
    value = static_cast<std::int64_t>(bits(32)) + 4436;
  }
  if (sign == 0) {
    return value > INT32_MAX ? INT32_MAX : static_cast<std::int32_t>(value);
  }
  if (value == 0) {
    return std::nullopt;  // OOB
  }
  return value > INT32_MAX ? -INT32_MAX : static_cast<std::int32_t>(-value);
}

std::uint32_t decode_id(ArithmeticDecoder& decoder, Contexts& contexts, unsigned code_length) {
  std::uint32_t prev = 1;
  for (unsigned i = 0; i < code_length; ++i) {
    prev = (prev << 1U) | decoder.decode(contexts, prev);
  }
  return prev - (std::uint32_t{1} << code_length);
}

}  // namespace capriole::filters::jbig2
