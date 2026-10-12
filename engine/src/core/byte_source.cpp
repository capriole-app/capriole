#include "capriole/core/byte_source.hpp"

#include <algorithm>
#include <cstring>
#include <ios>
#include <limits>
#include <string>
#include <utility>

namespace capriole {

namespace {

// Clamps a read request to what is left in a source of the given size.
Result<std::size_t> clamp_read(std::uint64_t source_size, std::uint64_t offset, std::size_t requested) {
  if (offset > source_size) {
    return make_error(ErrorCode::kOutOfRange, "read offset beyond end of source", offset);
  }
  const std::uint64_t available = source_size - offset;
  return static_cast<std::size_t>(std::min<std::uint64_t>(available, requested));
}

}  // namespace

// ── MemoryByteSource ─────────────────────────────────────────────────────────────────────────

MemoryByteSource::MemoryByteSource(std::vector<std::byte> data) noexcept : data_(std::move(data)) {}

MemoryByteSource MemoryByteSource::from_string(std::string_view text) {
  std::vector<std::byte> data(text.size());
  if (!text.empty()) {
    std::memcpy(data.data(), text.data(), text.size());
  }
  return MemoryByteSource(std::move(data));
}

std::uint64_t MemoryByteSource::size() const noexcept { return data_.size(); }

Result<std::size_t> MemoryByteSource::read(std::uint64_t offset, std::span<std::byte> dest) {
  auto count = clamp_read(data_.size(), offset, dest.size());
  if (!count) {
    return count;
  }
  // clamp_read guarantees offset <= data_.size(), so it fits in size_t.
  const auto source = std::span<const std::byte>(data_).subspan(static_cast<std::size_t>(offset), *count);
  std::ranges::copy(source, dest.begin());
  return count;
}

// ── SpanByteSource ───────────────────────────────────────────────────────────────────────────

Result<std::size_t> SpanByteSource::read(std::uint64_t offset, std::span<std::byte> dest) {
  auto count = clamp_read(data_.size(), offset, dest.size());
  if (!count) {
    return count;
  }
  std::ranges::copy(data_.subspan(static_cast<std::size_t>(offset), *count), dest.begin());
  return count;
}

// ── FileByteSource ───────────────────────────────────────────────────────────────────────────

FileByteSource::FileByteSource(std::ifstream stream, std::uint64_t size) noexcept : stream_(std::move(stream)), size_(size) {}

Result<std::unique_ptr<FileByteSource>> FileByteSource::open(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return make_error(ErrorCode::kIo, "cannot open file: " + path.string());
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff end = stream.tellg();
  if (!stream || end < 0) {
    return make_error(ErrorCode::kIo, "cannot determine file size: " + path.string());
  }
  // The constructor is private, so std::make_unique cannot be used.
  return std::unique_ptr<FileByteSource>(new FileByteSource(std::move(stream), static_cast<std::uint64_t>(end)));
}

std::uint64_t FileByteSource::size() const noexcept { return size_; }

Result<std::size_t> FileByteSource::read(std::uint64_t offset, std::span<std::byte> dest) {
  auto count = clamp_read(size_, offset, dest.size());
  if (!count || *count == 0) {
    return count;
  }
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return make_error(ErrorCode::kOutOfRange, "offset not representable by the stream", offset);
  }

  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(offset));
  // Reading bytes through char* is the sanctioned way to fill a std::byte buffer from a stream.
  stream_.read(reinterpret_cast<char*>(dest.data()), static_cast<std::streamsize>(*count));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto got = static_cast<std::size_t>(stream_.gcount());
  if (got != *count) {
    // The file shrank after open, or the device failed.
    return make_error(ErrorCode::kIo, "short read (" + std::to_string(got) + " of " + std::to_string(*count) + " bytes)", offset);
  }
  return got;
}

// ── OffsetByteSource ─────────────────────────────────────────────────────────────────────────

OffsetByteSource::OffsetByteSource(std::unique_ptr<ByteSource> base, std::uint64_t offset) noexcept
    : base_(std::move(base)), offset_(base_ != nullptr ? std::min(offset, base_->size()) : 0) {}

std::uint64_t OffsetByteSource::size() const noexcept { return base_ != nullptr ? base_->size() - offset_ : 0; }

Result<std::size_t> OffsetByteSource::read(std::uint64_t offset, std::span<std::byte> dest) {
  if (base_ == nullptr) {
    return make_error(ErrorCode::kIo, "source released");
  }
  auto count = clamp_read(size(), offset, dest.size());
  if (!count || *count == 0) {
    return count;
  }
  return base_->read(offset_ + offset, dest.first(*count));
}

}  // namespace capriole
