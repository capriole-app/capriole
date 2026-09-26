#pragma once

// Sequential byte access with a sliding window over a ByteSource.
//
// Lexers and scanners read one byte at a time with a little lookahead; going to the ByteSource for
// each byte would be far too slow, and loading the whole file is not an option (see byte_source.hpp).
// SourceReader keeps a window of the file in memory and refills it as the position moves.
//
// Bytes are returned as int in 0..255, or kEof. An I/O error while refilling is recorded in io_error()
// and from then on the reader behaves as if it were at end of input, so a lexer loop needs a single
// kEof check and can tell "the file ended" from "the file could not be read" afterwards.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"

namespace capriole {

class SourceReader {
 public:
  static constexpr int kEof = -1;
  static constexpr std::size_t kDefaultWindow = 64 * 1024;
  // peek() lookahead must stay below the window size; lexers need a couple of bytes at most.
  static constexpr std::size_t kMinWindow = 16;

  // The source must outlive the reader. window is clamped to at least kMinWindow.
  explicit SourceReader(ByteSource& source, std::size_t window = kDefaultWindow);

  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] std::uint64_t position() const noexcept { return pos_; }
  [[nodiscard]] bool at_end() const noexcept { return pos_ >= size_; }

  // Moves to an absolute offset. offset == size() is valid (at end); beyond it is kOutOfRange.
  [[nodiscard]] Result<void> seek(std::uint64_t offset);

  // Byte at position() + ahead, or kEof past the end or after an I/O error. ahead < kMinWindow.
  [[nodiscard]] int peek(std::size_t ahead = 0) {
    const std::uint64_t at = pos_ + ahead;
    if (at >= size_ || io_error_) {
      return kEof;
    }
    if (pos_ < window_start_ || at >= window_start_ + window_len_) {
      if (!fill()) {
        return kEof;
      }
    }
    return std::to_integer<int>(window_[static_cast<std::size_t>(at - window_start_)]);
  }

  // Returns peek() and advances by one byte (no-op at end).
  int next() {
    const int c = peek();
    if (c != kEof) {
      ++pos_;
    }
    return c;
  }

  // Advances by count bytes, stopping at end of input.
  void advance(std::size_t count = 1) noexcept { pos_ = (count >= size_ - pos_) ? size_ : pos_ + count; }

  // The first I/O error met while refilling the window, if any.
  [[nodiscard]] const std::optional<Error>& io_error() const noexcept { return io_error_; }

 private:
  // Loads the window starting at pos_. Returns false (and sets io_error_) on failure.
  bool fill();

  ByteSource* source_;
  std::vector<std::byte> window_;
  std::uint64_t window_start_ = 0;
  std::size_t window_len_ = 0;
  std::uint64_t pos_ = 0;
  std::uint64_t size_;
  std::optional<Error> io_error_;
};

}  // namespace capriole
