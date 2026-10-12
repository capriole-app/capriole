#pragma once

// Output side of the engine: where the writer puts bytes. It tracks the number of bytes written,
// which is what cross-reference offsets are made of.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::writer {

class ByteSink {
 public:
  ByteSink() = default;
  ByteSink(const ByteSink&) = delete;
  ByteSink& operator=(const ByteSink&) = delete;
  ByteSink(ByteSink&&) = delete;
  ByteSink& operator=(ByteSink&&) = delete;
  virtual ~ByteSink() = default;

  [[nodiscard]] virtual Result<void> write(std::span<const std::byte> data) = 0;
  [[nodiscard]] Result<void> write(std::string_view text) { return write(std::as_bytes(std::span(text.data(), text.size()))); }

  // Bytes written so far.
  [[nodiscard]] virtual std::uint64_t position() const noexcept = 0;
};

// Collects the output in memory (the WebAssembly build, tests).
class MemorySink final : public ByteSink {
 public:
  [[nodiscard]] Result<void> write(std::span<const std::byte> data) override;
  using ByteSink::write;
  [[nodiscard]] std::uint64_t position() const noexcept override { return data_.size(); }

  [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return data_; }
  [[nodiscard]] std::vector<std::byte> take() noexcept { return std::move(data_); }

 private:
  std::vector<std::byte> data_;
};

// Writes to a file, replacing it.
class FileSink final : public ByteSink {
 public:
  [[nodiscard]] static Result<std::unique_ptr<FileSink>> create(const std::filesystem::path& path);

  [[nodiscard]] Result<void> write(std::span<const std::byte> data) override;
  using ByteSink::write;
  [[nodiscard]] std::uint64_t position() const noexcept override { return position_; }
  // Flushes and closes; an error here means the file is incomplete.
  [[nodiscard]] Result<void> close();

 private:
  explicit FileSink(std::ofstream stream) noexcept;

  std::ofstream stream_;
  std::uint64_t position_ = 0;
};

}  // namespace capriole::writer
