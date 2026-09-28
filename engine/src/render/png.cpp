#include "capriole/render/png.hpp"

// next_in becomes a pointer to const: zlib never writes to its input.
#define ZLIB_CONST
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <span>
#include <string_view>

#include <zlib.h>

namespace capriole::render {

namespace {

constexpr std::size_t kOutputChunk = std::size_t{64} * 1024;

void put_u32(std::vector<std::byte>& out, std::uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::byte>((v >> shift) & 0xFFU));
  }
}

// A chunk: length, type, data, CRC over type and data.
void put_chunk(std::vector<std::byte>& out, std::string_view type, std::span<const std::byte> data) {
  put_u32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t type_at = out.size();
  for (const char c : type) {
    out.push_back(static_cast<std::byte>(c));
  }
  out.insert(out.end(), data.begin(), data.end());
  const std::span<const std::byte> covered = std::span(out).subspan(type_at);
  uLong crc = crc32(0, nullptr, 0);
  // zlib's crc32 takes at most UINT_MAX bytes per call.
  for (std::size_t at = 0; at < covered.size(); at += UINT_MAX) {
    const std::span<const std::byte> part = covered.subspan(at, std::min<std::size_t>(covered.size() - at, UINT_MAX));
    crc = crc32(crc, reinterpret_cast<const Bytef*>(part.data()), static_cast<uInt>(part.size()));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  }
  put_u32(out, static_cast<std::uint32_t>(crc));
}

class Deflater {
 public:
  Deflater() = default;
  Deflater(const Deflater&) = delete;
  Deflater& operator=(const Deflater&) = delete;
  Deflater(Deflater&&) = delete;
  Deflater& operator=(Deflater&&) = delete;
  ~Deflater() {
    if (initialised_) {
      deflateEnd(&stream_);
    }
  }

  bool init() {
    initialised_ = deflateInit(&stream_, 6) == Z_OK;
    return initialised_;
  }

  // Compresses input (at most one row: well below UINT_MAX) into out; finish flushes the stream.
  bool feed(std::span<const std::uint8_t> input, bool finish, std::vector<std::byte>& out) {
    stream_.next_in = input.data();
    stream_.avail_in = static_cast<uInt>(input.size());
    const int flush = finish ? Z_FINISH : Z_NO_FLUSH;
    while (true) {
      const std::size_t at = out.size();
      out.resize(at + kOutputChunk);
      stream_.next_out = reinterpret_cast<Bytef*>(out.data() + at);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
      stream_.avail_out = static_cast<uInt>(kOutputChunk);
      const int status = deflate(&stream_, flush);
      out.resize(at + kOutputChunk - stream_.avail_out);
      if (status == Z_STREAM_END) {
        return true;
      }
      if (status != Z_OK && status != Z_BUF_ERROR) {
        return false;
      }
      if (!finish && stream_.avail_in == 0 && stream_.avail_out != 0) {
        return true;
      }
    }
  }

 private:
  z_stream stream_{};
  bool initialised_ = false;
};

}  // namespace

Result<std::vector<std::byte>> encode_png(const Bitmap& bitmap) {
  if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.rgba.size() != static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height) * 4) {
    return make_error(ErrorCode::kOutOfRange, "bitmap size does not match its pixels");
  }
  const auto width = static_cast<std::size_t>(bitmap.width);
  const auto height = static_cast<std::size_t>(bitmap.height);
  const std::size_t stride = width * 4;

  constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<std::byte> out(kSignature.size());
  std::ranges::transform(kSignature, out.begin(), [](std::uint8_t b) { return static_cast<std::byte>(b); });

  std::vector<std::byte> header;
  put_u32(header, static_cast<std::uint32_t>(width));
  put_u32(header, static_cast<std::uint32_t>(height));
  // Bit depth 8, colour type 6 (RGBA), deflate, adaptive filtering, not interlaced.
  constexpr std::array<std::uint8_t, 5> kHeaderTail = {8, 6, 0, 0, 0};
  for (const std::uint8_t b : kHeaderTail) {
    header.push_back(static_cast<std::byte>(b));
  }
  put_chunk(out, "IHDR", header);

  Deflater deflater;
  if (!deflater.init()) {
    return make_error(ErrorCode::kLimitExceeded, "cannot start the PNG compressor");
  }
  std::vector<std::byte> data;
  std::vector<std::uint8_t> row(1 + stride);
  const std::span<const std::uint8_t> pixels(bitmap.rgba);
  for (std::size_t y = 0; y < height; ++y) {
    const std::span<const std::uint8_t> current = pixels.subspan(y * stride, stride);
    row[0] = 2;  // Up: the difference from the row above (the first row's "above" is zeros)
    for (std::size_t i = 0; i < stride; ++i) {
      const std::uint8_t above = y == 0 ? 0 : pixels[((y - 1) * stride) + i];
      row[1 + i] = static_cast<std::uint8_t>(current[i] - above);
    }
    if (!deflater.feed(row, y + 1 == height, data)) {
      return make_error(ErrorCode::kLimitExceeded, "PNG compression failed");
    }
  }
  // IDAT chunks of at most 1 MiB: a chunk's length is 31 bits, and readers stream them anyway.
  constexpr std::size_t kIdatSize = std::size_t{1} << 20;
  const std::span<const std::byte> compressed(data);
  for (std::size_t at = 0; at < compressed.size(); at += kIdatSize) {
    put_chunk(out, "IDAT", compressed.subspan(at, std::min(kIdatSize, compressed.size() - at)));
  }
  put_chunk(out, "IEND", {});
  return out;
}

}  // namespace capriole::render
