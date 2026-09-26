#include "capriole/filters/flate.hpp"

// next_in becomes a pointer to const: zlib never writes to its input.
#define ZLIB_CONST
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <zlib.h>

namespace capriole::filters {

namespace {

constexpr std::size_t kOutputChunk = std::size_t{64} * 1024;
// Largest single row accepted by the predictors (a 2^20-column, 32-bit RGBA row is 4 MiB).
constexpr std::uint64_t kMaxRowBytes = std::uint64_t{64} * 1024 * 1024;

// zlib uses unsigned char; std::byte has the same representation.
const Bytef* as_zlib(const std::byte* p) noexcept { return reinterpret_cast<const Bytef*>(p); }  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
Bytef* as_zlib(std::byte* p) noexcept { return reinterpret_cast<Bytef*>(p); }                    // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

class InflateStream {
 public:
  InflateStream() = default;
  InflateStream(const InflateStream&) = delete;
  InflateStream& operator=(const InflateStream&) = delete;
  InflateStream(InflateStream&&) = delete;
  InflateStream& operator=(InflateStream&&) = delete;
  ~InflateStream() {
    if (initialised_) {
      inflateEnd(&stream_);
    }
  }

  bool init() {
    initialised_ = inflateInit(&stream_) == Z_OK;
    return initialised_;
  }
  z_stream& get() noexcept { return stream_; }

 private:
  z_stream stream_{};
  bool initialised_ = false;
};

std::string zlib_message(const z_stream& stream, const char* fallback) { return stream.msg != nullptr ? stream.msg : fallback; }

}  // namespace

Result<std::vector<std::byte>> flate_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient) {
  InflateStream inflater;
  if (!inflater.init()) {
    return make_error(ErrorCode::kLimitExceeded, "cannot initialise the inflater (out of memory)");
  }
  z_stream& zs = inflater.get();

  std::vector<std::byte> out;
  std::size_t produced = 0;
  std::size_t consumed = 0;
  for (;;) {
    if (zs.avail_in == 0 && consumed < input.size()) {
      const std::size_t feed = std::min<std::size_t>(input.size() - consumed, UINT_MAX);
      zs.next_in = as_zlib(input.data() + consumed);
      zs.avail_in = static_cast<uInt>(feed);
      consumed += feed;
    }
    if (produced == out.size()) {
      if (out.size() >= max_output) {
        return make_error(ErrorCode::kLimitExceeded, "decoded stream exceeds max_decoded_stream_bytes");
      }
      const std::size_t grow = std::min({std::max(out.size(), kOutputChunk), max_output - out.size(), std::size_t{UINT_MAX}});
      out.resize(out.size() + grow);
    }
    const auto room = static_cast<uInt>(std::min<std::size_t>(out.size() - produced, UINT_MAX));
    zs.next_out = as_zlib(out.data() + produced);
    zs.avail_out = room;

    const int rc = inflate(&zs, Z_NO_FLUSH);
    produced += room - zs.avail_out;

    switch (rc) {
      case Z_STREAM_END:
        out.resize(produced);
        return out;
      case Z_OK:
        break;
      case Z_BUF_ERROR:
        // No progress possible: fine if only the output was full (it grows next round), fatal if the
        // input is exhausted.
        if (zs.avail_out != 0 && zs.avail_in == 0 && consumed == input.size()) {
          if (lenient) {
            out.resize(produced);
            return out;
          }
          return make_error(ErrorCode::kCorruptData, "truncated flate data");
        }
        break;
      default:
        if (lenient) {
          out.resize(produced);
          return out;
        }
        return make_error(ErrorCode::kCorruptData, "invalid flate data: " + zlib_message(zs, "unknown zlib error"));
    }
  }
}

Result<PredictorParams> predictor_params(const model::Dict* decode_parms) {
  PredictorParams params;
  if (decode_parms == nullptr) {
    return params;
  }
  const auto read = [&](std::string_view key, int& field) -> Result<void> {
    const model::Object* value = decode_parms->find(key);
    if (value == nullptr) {
      return {};
    }
    const auto integer = value->as_integer();
    if (!integer || !std::in_range<int>(*integer)) {
      return make_error(ErrorCode::kSyntax, "/" + std::string(key) + " in /DecodeParms is not a valid integer");
    }
    field = static_cast<int>(*integer);
    return {};
  };
  for (const auto& [key, field] : {std::pair<std::string_view, int*>{"Predictor", &params.predictor},
                                   {"Colors", &params.colors},
                                   {"BitsPerComponent", &params.bits_per_component},
                                   {"Columns", &params.columns}}) {
    if (auto ok = read(key, *field); !ok) {
      return std::unexpected(ok.error());
    }
  }

  const bool predictor_ok = params.predictor == 1 || params.predictor == 2 || (params.predictor >= 10 && params.predictor <= 15);
  const int bpc = params.bits_per_component;
  if (!predictor_ok) {
    return make_error(ErrorCode::kSyntax, "invalid /Predictor " + std::to_string(params.predictor));
  }
  if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) {
    return make_error(ErrorCode::kSyntax, "invalid /BitsPerComponent " + std::to_string(bpc));
  }
  if (params.colors < 1 || params.columns < 1) {
    return make_error(ErrorCode::kSyntax, "/Colors and /Columns must be positive");
  }
  return params;
}

Result<std::vector<std::byte>> apply_predictor(std::vector<std::byte> data, const PredictorParams& params, bool lenient) {
  if (params.predictor == 1) {
    return data;
  }
  const std::uint64_t row_bits =
      static_cast<std::uint64_t>(params.colors) * static_cast<std::uint64_t>(params.bits_per_component) * static_cast<std::uint64_t>(params.columns);
  const std::uint64_t row_bytes64 = (row_bits + 7) / 8;
  if (row_bytes64 > kMaxRowBytes) {
    return make_error(ErrorCode::kLimitExceeded, "predictor row too large");
  }
  const auto row_bytes = static_cast<std::size_t>(row_bytes64);
  // Bytes per complete pixel, at least 1: the distance to the "left" byte.
  const auto bpp = static_cast<std::size_t>(
      std::max<std::uint64_t>(1, static_cast<std::uint64_t>(params.colors) * static_cast<std::uint64_t>(params.bits_per_component) / 8));

  if (params.predictor == 2) {
    // TIFF predictor 2 (§7.4.4.4 / TIFF 6.0 §14): each sample adds the same sample of the previous pixel.
    if (params.bits_per_component != 8 && params.bits_per_component != 16) {
      return make_error(ErrorCode::kUnsupported, "TIFF predictor with /BitsPerComponent below 8");
    }
    if (data.size() % row_bytes != 0) {
      if (!lenient) {
        return make_error(ErrorCode::kCorruptData, "TIFF predictor data is not a whole number of rows");
      }
      data.resize(data.size() - (data.size() % row_bytes));
    }
    for (std::size_t row = 0; row < data.size(); row += row_bytes) {
      if (params.bits_per_component == 8) {
        for (std::size_t i = bpp; i < row_bytes; ++i) {
          data[row + i] = static_cast<std::byte>(std::to_integer<unsigned>(data[row + i]) + std::to_integer<unsigned>(data[row + i - bpp]));
        }
      } else {
        for (std::size_t i = bpp; i + 1 < row_bytes; i += 2) {
          const unsigned left = (std::to_integer<unsigned>(data[row + i - bpp]) << 8U) | std::to_integer<unsigned>(data[row + i - bpp + 1]);
          const unsigned cur = (std::to_integer<unsigned>(data[row + i]) << 8U) | std::to_integer<unsigned>(data[row + i + 1]);
          const unsigned sum = (cur + left) & 0xFFFFU;
          data[row + i] = static_cast<std::byte>(sum >> 8U);
          data[row + i + 1] = static_cast<std::byte>(sum & 0xFFU);
        }
      }
    }
    return data;
  }

  // PNG predictors (RFC 2083 §6): every row is prefixed by its filter type.
  const std::size_t stride = row_bytes + 1;
  if (data.size() % stride != 0 && !lenient) {
    return make_error(ErrorCode::kCorruptData, "PNG predictor data is not a whole number of rows");
  }
  const std::size_t rows = data.size() / stride;
  std::vector<std::byte> out(rows * row_bytes);
  for (std::size_t r = 0; r < rows; ++r) {
    const std::byte* in = data.data() + (r * stride) + 1;
    std::byte* cur = out.data() + (r * row_bytes);
    const std::byte* prev = r > 0 ? cur - row_bytes : nullptr;
    const auto filter = std::to_integer<unsigned>(data[r * stride]);
    for (std::size_t i = 0; i < row_bytes; ++i) {
      const auto x = std::to_integer<unsigned>(in[i]);
      const unsigned a = i >= bpp ? std::to_integer<unsigned>(cur[i - bpp]) : 0U;
      const unsigned b = prev != nullptr ? std::to_integer<unsigned>(prev[i]) : 0U;
      const unsigned c = (prev != nullptr && i >= bpp) ? std::to_integer<unsigned>(prev[i - bpp]) : 0U;
      unsigned value = 0;
      switch (filter) {
        case 0:
          value = x;
          break;
        case 1:
          value = x + a;
          break;
        case 2:
          value = x + b;
          break;
        case 3:
          value = x + ((a + b) / 2);
          break;
        case 4: {
          const int p = static_cast<int>(a + b) - static_cast<int>(c);
          const int pa = std::abs(p - static_cast<int>(a));
          const int pb = std::abs(p - static_cast<int>(b));
          const int pc = std::abs(p - static_cast<int>(c));
          // Paeth predictor: the neighbour closest to a + b - c, ties broken in the order a, b, c.
          unsigned predictor = c;
          if (pa <= pb && pa <= pc) {
            predictor = a;
          } else if (pb <= pc) {
            predictor = b;
          }
          value = x + predictor;
          break;
        }
        default:
          if (!lenient) {
            return make_error(ErrorCode::kCorruptData, "invalid PNG predictor filter type " + std::to_string(filter));
          }
          value = x;  // unknown filter type: read the row as unfiltered
          break;
      }
      cur[i] = static_cast<std::byte>(value & 0xFFU);
    }
  }
  return out;
}

}  // namespace capriole::filters
