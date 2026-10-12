#include "capriole/core/source_reader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace capriole {
namespace {

// A source whose reads fail from a given offset on, to exercise I/O error propagation.
class FailingByteSource final : public ByteSource {
 public:
  FailingByteSource(std::uint64_t size, std::uint64_t fail_from) : size_(size), fail_from_(fail_from) {}

  [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }

  [[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> dest) override {
    if (offset >= fail_from_) {
      return make_error(ErrorCode::kIo, "simulated device failure", offset);
    }
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>({dest.size(), size_ - offset, fail_from_ - offset}));
    for (std::size_t i = 0; i < count; ++i) {
      dest[i] = std::byte{'a'};
    }
    return count;
  }

 private:
  std::uint64_t size_;
  std::uint64_t fail_from_;
};

std::string read_all(SourceReader& reader) {
  std::string out;
  for (int c = reader.next(); c != SourceReader::kEof; c = reader.next()) {
    out.push_back(static_cast<char>(c));
  }
  return out;
}

TEST(SourceReader, ReadsSequentially) {
  auto source = MemoryByteSource::from_string("hello");
  SourceReader reader(source);
  EXPECT_EQ(reader.size(), 5U);
  EXPECT_EQ(reader.peek(), 'h');
  EXPECT_EQ(reader.peek(1), 'e');
  EXPECT_EQ(reader.next(), 'h');
  EXPECT_EQ(reader.position(), 1U);
  EXPECT_EQ(read_all(reader), "ello");
  EXPECT_TRUE(reader.at_end());
  EXPECT_EQ(reader.peek(), SourceReader::kEof);
  EXPECT_EQ(reader.next(), SourceReader::kEof);
  EXPECT_EQ(reader.position(), 5U);
}

TEST(SourceReader, ReturnsBytesAboveAsciiAsUnsigned) {
  auto source = MemoryByteSource::from_string("\xFF\x80");
  SourceReader reader(source);
  EXPECT_EQ(reader.next(), 0xFF);
  EXPECT_EQ(reader.next(), 0x80);
}

TEST(SourceReader, PeekPastEndIsEof) {
  auto source = MemoryByteSource::from_string("ab");
  SourceReader reader(source);
  EXPECT_EQ(reader.peek(1), 'b');
  EXPECT_EQ(reader.peek(2), SourceReader::kEof);
}

TEST(SourceReader, SmallWindowCrossesBoundariesTransparently) {
  std::string text;
  for (int i = 0; i < 1000; ++i) {
    text.push_back(static_cast<char>('a' + i % 26));
  }
  auto source = MemoryByteSource::from_string(text);
  SourceReader reader(source, SourceReader::kMinWindow);
  std::string out;
  while (!reader.at_end()) {
    // Lookahead across the window edge must not disturb the current position.
    const int ahead = reader.peek(SourceReader::kMinWindow - 1);
    const std::uint64_t ahead_pos = reader.position() + SourceReader::kMinWindow - 1;
    EXPECT_EQ(ahead, ahead_pos < text.size() ? static_cast<unsigned char>(text[static_cast<std::size_t>(ahead_pos)]) : SourceReader::kEof);
    out.push_back(static_cast<char>(reader.next()));
  }
  EXPECT_EQ(out, text);
}

TEST(SourceReader, SeekAndAdvance) {
  auto source = MemoryByteSource::from_string("0123456789");
  SourceReader reader(source);
  ASSERT_TRUE(reader.seek(7).has_value());
  EXPECT_EQ(reader.next(), '7');
  ASSERT_TRUE(reader.seek(2).has_value());  // backwards
  EXPECT_EQ(reader.next(), '2');
  reader.advance(3);
  EXPECT_EQ(reader.next(), '6');
  reader.advance(100);  // clamps
  EXPECT_TRUE(reader.at_end());
  ASSERT_TRUE(reader.seek(10).has_value());  // exactly at end is valid
  EXPECT_TRUE(reader.at_end());
}

TEST(SourceReader, SeekBeyondEndIsOutOfRange) {
  auto source = MemoryByteSource::from_string("abc");
  SourceReader reader(source);
  const auto result = reader.seek(4);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(reader.position(), 0U);
}

TEST(SourceReader, IoErrorBehavesAsEndAndIsReported) {
  FailingByteSource source(100, 40);
  SourceReader reader(source, SourceReader::kMinWindow);
  std::size_t count = 0;
  while (reader.next() != SourceReader::kEof) {
    ++count;
  }
  EXPECT_EQ(count, 40U);
  ASSERT_TRUE(reader.io_error().has_value());
  EXPECT_EQ(reader.io_error()->code, ErrorCode::kIo);
  EXPECT_FALSE(reader.at_end());  // the source is longer; we stopped because of the error
}

}  // namespace
}  // namespace capriole
