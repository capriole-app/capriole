#include "capriole/core/source_reader.hpp"

#include <algorithm>
#include <utility>

namespace capriole {

SourceReader::SourceReader(ByteSource& source, std::size_t window) : source_(&source), window_(std::max(window, kMinWindow)), size_(source.size()) {}

Result<void> SourceReader::seek(std::uint64_t offset) {
  if (offset > size_) {
    return make_error(ErrorCode::kOutOfRange, "seek beyond end of source", offset);
  }
  pos_ = offset;
  return {};
}

bool SourceReader::fill() {
  auto read = source_->read(pos_, window_);
  if (!read) {
    io_error_ = std::move(read.error());
    return false;
  }
  if (*read == 0) {
    // size() promised bytes that are not there: the source shrank underneath us.
    io_error_ = Error{.code = ErrorCode::kIo, .message = "source ended before its declared size", .offset = pos_};
    return false;
  }
  window_start_ = pos_;
  window_len_ = *read;
  return true;
}

}  // namespace capriole
