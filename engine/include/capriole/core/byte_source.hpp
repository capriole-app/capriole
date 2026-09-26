#pragma once

// Random-access input for the parser.
//
// The parser never assumes the whole file is in memory: wasm32 caps memory at 4 GB and technical or
// scanned PDFs can be larger. In the browser a ByteSource is backed by FileReaderSync slices inside a
// Worker; natively by a file; in tests and fuzzers by a memory buffer.
//
// Implementations are not thread-safe.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole {

class ByteSource {
 public:
  ByteSource() = default;
  ByteSource(const ByteSource&) = delete;
  ByteSource& operator=(const ByteSource&) = delete;
  ByteSource(ByteSource&&) = delete;
  ByteSource& operator=(ByteSource&&) = delete;
  virtual ~ByteSource() = default;

  // Total size in bytes.
  [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;

  // Reads up to dest.size() bytes starting at offset and returns how many were read. A short read
  // happens only at end of source; reading exactly at size() returns 0. offset > size() is kOutOfRange.
  [[nodiscard]] virtual Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> dest) = 0;
};

// Owns a copy of the bytes. Used by tests, fuzzers and the WASM build for small files.
class MemoryByteSource final : public ByteSource {
 public:
  explicit MemoryByteSource(std::vector<std::byte> data) noexcept;
  static MemoryByteSource from_string(std::string_view text);

  [[nodiscard]] std::uint64_t size() const noexcept override;
  [[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> dest) override;

 private:
  std::vector<std::byte> data_;
};

// Reads from a file on disk.
class FileByteSource final : public ByteSource {
 public:
  [[nodiscard]] static Result<std::unique_ptr<FileByteSource>> open(const std::filesystem::path& path);

  [[nodiscard]] std::uint64_t size() const noexcept override;
  [[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> dest) override;

 private:
  FileByteSource(std::ifstream stream, std::uint64_t size) noexcept;

  std::ifstream stream_;
  std::uint64_t size_;
};

// A view of another source that starts `offset` bytes in: byte 0 of the view is byte `offset` of the
// base. Used by the repair layer when a file's offsets are relative to a header preceded by junk.
class OffsetByteSource final : public ByteSource {
 public:
  // offset must not exceed base->size().
  OffsetByteSource(std::unique_ptr<ByteSource> base, std::uint64_t offset) noexcept;

  [[nodiscard]] std::uint64_t size() const noexcept override;
  [[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> dest) override;

  [[nodiscard]] std::uint64_t base_offset() const noexcept { return offset_; }
  // Gives the base source back (the view is empty afterwards).
  [[nodiscard]] std::unique_ptr<ByteSource> release_base() noexcept { return std::move(base_); }

 private:
  std::unique_ptr<ByteSource> base_;
  std::uint64_t offset_;
};

}  // namespace capriole
