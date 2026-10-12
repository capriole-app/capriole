#pragma once

// Internal to the JBIG2 decoder (filters/jbig2.cpp): the MQ arithmetic decoder (ITU-T T.88 Annex E)
// and the integer and symbol-ID decoding procedures built on it (Annex A).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace capriole::filters::jbig2 {

// Adaptive contexts: per context, the index into the probability table and the more probable symbol.
class Contexts {
 public:
  explicit Contexts(std::size_t count = 0) : state_(count, 0) {}
  [[nodiscard]] std::uint8_t& at(std::size_t i) { return state_.at(i); }
  [[nodiscard]] std::size_t size() const noexcept { return state_.size(); }
  void reset() { std::ranges::fill(state_, std::uint8_t{0}); }

 private:
  std::vector<std::uint8_t> state_;  // index << 1 | mps
};

class ArithmeticDecoder {
 public:
  // Decodes from data; past its end the decoder reads 0xFF bytes, as the standard's end of data does.
  explicit ArithmeticDecoder(std::span<const std::byte> data);

  // One bit in context cx of contexts (E.3.2, DECODE).
  unsigned decode(Contexts& contexts, std::size_t cx);

  // How far the decoder has read (the next byte it would take).
  [[nodiscard]] std::size_t position() const noexcept { return at_; }

  // Past its end the data reads as an endless run of 0xFF; a real stream needs at most two or three of
  // those bytes (its final ones are the flush of E.2.9).
  // Far beyond, the data is exhausted: what is decoded there is invented, and a hostile stream could
  // have any amount of it decoded.
  [[nodiscard]] bool exhausted() const noexcept { return past_end_ > 16; }

 private:
  [[nodiscard]] unsigned byte(std::size_t i) const noexcept;
  void byte_in();

  std::span<const std::byte> data_;
  std::size_t at_ = 0;
  std::uint32_t c_high_ = 0;
  std::uint32_t c_low_ = 0;
  std::uint32_t a_ = 0;
  int ct_ = 0;
  std::size_t past_end_ = 0;
};

// The integer arithmetic decoding procedure (A.2): a value, or nullopt for OOB. Each IAx procedure
// has its own 512 contexts.
std::optional<std::int32_t> decode_integer(ArithmeticDecoder& decoder, Contexts& contexts);

// The IAID procedure (A.3): a symbol ID of code_length bits (contexts: 2^code_length).
std::uint32_t decode_id(ArithmeticDecoder& decoder, Contexts& contexts, unsigned code_length);

}  // namespace capriole::filters::jbig2
