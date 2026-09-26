#include "capriole/core/byte_source.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace capriole {
namespace {

std::string to_string(std::span<const std::byte> bytes) {
  std::string out;
  for (const std::byte b : bytes) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

TEST(MemoryByteSource, ReadsWithinBounds) {
  auto source = MemoryByteSource::from_string("0123456789");
  std::array<std::byte, 4> buffer{};
  const auto read = source.read(3, buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 4U);
  EXPECT_EQ(to_string(buffer), "3456");
}

TEST(MemoryByteSource, ShortReadAtEnd) {
  auto source = MemoryByteSource::from_string("0123456789");
  std::array<std::byte, 8> buffer{};
  const auto read = source.read(7, buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 3U);
  EXPECT_EQ(to_string(std::span(buffer).first(*read)), "789");
}

TEST(MemoryByteSource, ReadAtSizeReturnsZero) {
  auto source = MemoryByteSource::from_string("abc");
  std::array<std::byte, 1> buffer{};
  const auto read = source.read(3, buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 0U);
}

TEST(MemoryByteSource, ReadPastEndIsOutOfRange) {
  auto source = MemoryByteSource::from_string("abc");
  std::array<std::byte, 1> buffer{};
  const auto read = source.read(4, buffer);
  ASSERT_FALSE(read.has_value());
  EXPECT_EQ(read.error().code, ErrorCode::kOutOfRange);
  EXPECT_EQ(read.error().offset, 4U);
}

TEST(MemoryByteSource, EmptySource) {
  auto source = MemoryByteSource::from_string("");
  EXPECT_EQ(source.size(), 0U);
  std::array<std::byte, 1> buffer{};
  const auto read = source.read(0, buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 0U);
}

class FileByteSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() / ("capriole_byte_source_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".bin");
    std::ofstream(path_, std::ios::binary) << "%PDF-1.7\nhello";
  }
  void TearDown() override { std::filesystem::remove(path_); }

  std::filesystem::path path_;
};

TEST_F(FileByteSourceTest, ReadsFile) {
  auto source = FileByteSource::open(path_);
  ASSERT_TRUE(source.has_value()) << source.error().message;
  EXPECT_EQ((*source)->size(), 14U);

  std::array<std::byte, 5> buffer{};
  const auto read = (*source)->read(9, buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 5U);
  EXPECT_EQ(to_string(buffer), "hello");
}

TEST_F(FileByteSourceTest, RandomAccessInAnyOrder) {
  auto source = FileByteSource::open(path_);
  ASSERT_TRUE(source.has_value());
  std::array<std::byte, 3> buffer{};
  ASSERT_TRUE((*source)->read(11, buffer).has_value());
  EXPECT_EQ(to_string(buffer), "llo");
  ASSERT_TRUE((*source)->read(0, buffer).has_value());
  EXPECT_EQ(to_string(buffer), "%PD");
}

TEST(FileByteSource, MissingFileIsIoError) {
  const auto source = FileByteSource::open(std::filesystem::temp_directory_path() / "capriole_does_not_exist.pdf");
  ASSERT_FALSE(source.has_value());
  EXPECT_EQ(source.error().code, ErrorCode::kIo);
}

}  // namespace
}  // namespace capriole
