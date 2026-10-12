#include "capriole/writer/byte_sink.hpp"

#include <utility>

namespace capriole::writer {

Result<void> MemorySink::write(std::span<const std::byte> data) {
  data_.insert(data_.end(), data.begin(), data.end());
  return {};
}

FileSink::FileSink(std::ofstream stream) noexcept : stream_(std::move(stream)) {}

Result<std::unique_ptr<FileSink>> FileSink::create(const std::filesystem::path& path) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return make_error(ErrorCode::kIo, "cannot create file: " + path.string());
  }
  // The constructor is private, so std::make_unique cannot be used.
  return std::unique_ptr<FileSink>(new FileSink(std::move(stream)));
}

Result<void> FileSink::write(std::span<const std::byte> data) {
  // Writing bytes through char* is the sanctioned way to put a std::byte buffer into a stream.
  stream_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!stream_) {
    return make_error(ErrorCode::kIo, "write failed", position_);
  }
  position_ += data.size();
  return {};
}

Result<void> FileSink::close() {
  stream_.close();
  if (stream_.fail()) {
    return make_error(ErrorCode::kIo, "closing the output file failed", position_);
  }
  return {};
}

}  // namespace capriole::writer
