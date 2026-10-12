#include "capriole/filters/jpx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include <openjpeg.h>

namespace capriole::filters {

namespace {

// OpenJPEG reads through callbacks: this is its memory stream.
struct Source {
  std::span<const std::byte> data;
  std::size_t at = 0;
};

OPJ_SIZE_T read(void* buffer, OPJ_SIZE_T size, void* user) {
  auto* s = static_cast<Source*>(user);
  if (s->at >= s->data.size()) {
    return static_cast<OPJ_SIZE_T>(-1);  // end of stream, as OpenJPEG expects it
  }
  const std::size_t count = std::min<std::size_t>(size, s->data.size() - s->at);
  std::memcpy(buffer, s->data.data() + s->at, count);
  s->at += count;
  return count;
}

OPJ_OFF_T skip(OPJ_OFF_T count, void* user) {
  auto* s = static_cast<Source*>(user);
  if (count < 0) {
    return -1;
  }
  const std::size_t moved = std::min<std::size_t>(static_cast<std::size_t>(count), s->data.size() - s->at);
  s->at += moved;
  return static_cast<OPJ_OFF_T>(moved);
}

OPJ_BOOL seek(OPJ_OFF_T to, void* user) {
  auto* s = static_cast<Source*>(user);
  if (to < 0 || static_cast<std::size_t>(to) > s->data.size()) {
    return OPJ_FALSE;
  }
  s->at = static_cast<std::size_t>(to);
  return OPJ_TRUE;
}

// OpenJPEG's messages: the last error is kept for ours; warnings and information are not needed.
void on_error(const char* message, void* user) { *static_cast<std::string*>(user) = message != nullptr ? message : "error"; }
void on_quiet(const char* /*message*/, void* /*user*/) {}

struct CodecDeleter {
  void operator()(opj_codec_t* c) const { opj_destroy_codec(c); }
};
struct StreamDeleter {
  void operator()(opj_stream_t* s) const { opj_stream_destroy(s); }
};
struct ImageDeleter {
  void operator()(opj_image_t* i) const { opj_image_destroy(i); }
};
struct InfoDeleter {
  void operator()(opj_codestream_info_v2_t* i) const {
    opj_codestream_info_v2_t* p = i;
    opj_destroy_cstr_info(&p);
  }
};

// A component's sample as 8 bits.
std::uint8_t to_byte(const opj_image_comp_t& c, OPJ_INT32 v) {
  const auto precision = static_cast<int>(std::clamp<OPJ_UINT32>(c.prec, 1, 31));
  std::int64_t x = v;
  if (c.sgnd != 0) {
    x += std::int64_t{1} << (precision - 1);
  }
  const std::int64_t top = (std::int64_t{1} << precision) - 1;
  x = std::clamp<std::int64_t>(x, 0, top);
  return static_cast<std::uint8_t>(precision >= 8 ? (x >> (precision - 8)) : (x * 255 + top / 2) / top);
}

}  // namespace

Result<JpxImage> jpx_decode(std::span<const std::byte> data, std::int64_t max_pixels) {
  // A JP2 file starts with its signature box; anything else is taken as a codestream (SOC FF 4F).
  constexpr std::array<std::uint8_t, 12> kJp2 = {0, 0, 0, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
  const bool jp2 = data.size() >= kJp2.size() &&
                   std::equal(kJp2.begin(), kJp2.end(), data.begin(), [](std::uint8_t a, std::byte b) { return a == std::to_integer<std::uint8_t>(b); });
  const std::unique_ptr<opj_codec_t, CodecDeleter> codec(opj_create_decompress(jp2 ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K));
  if (!codec) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: cannot create the decoder");
  }
  std::string error;
  opj_set_error_handler(codec.get(), on_error, &error);
  opj_set_warning_handler(codec.get(), on_quiet, nullptr);
  opj_set_info_handler(codec.get(), on_quiet, nullptr);
  opj_dparameters_t parameters;
  opj_set_default_decoder_parameters(&parameters);
  if (opj_setup_decoder(codec.get(), &parameters) == OPJ_FALSE) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: cannot set up the decoder");
  }

  Source source{.data = data, .at = 0};
  const std::unique_ptr<opj_stream_t, StreamDeleter> stream(opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE));
  if (!stream) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: cannot create the stream");
  }
  opj_stream_set_user_data(stream.get(), &source, nullptr);
  opj_stream_set_user_data_length(stream.get(), data.size());
  opj_stream_set_read_function(stream.get(), read);
  opj_stream_set_skip_function(stream.get(), skip);
  opj_stream_set_seek_function(stream.get(), seek);

  opj_image_t* raw_image = nullptr;
  if (opj_read_header(stream.get(), codec.get(), &raw_image) == OPJ_FALSE || raw_image == nullptr) {
    opj_image_destroy(raw_image);
    return make_error(ErrorCode::kCorruptData, "JPXDecode: " + (error.empty() ? std::string("unreadable header") : error));
  }
  std::unique_ptr<opj_image_t, ImageDeleter> image(raw_image);
  constexpr OPJ_UINT32 kMaxComponents = 32;
  if (image->numcomps == 0 || image->numcomps > kMaxComponents || image->x1 <= image->x0 || image->y1 <= image->y0) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: invalid image size or components");
  }
  // Too many pixels: drop resolution levels — JPEG 2000 decodes a reduced image natively.
  std::uint32_t reduction = 0;
  const std::int64_t full_w = static_cast<std::int64_t>(image->x1) - image->x0;
  const std::int64_t full_h = static_cast<std::int64_t>(image->y1) - image->y0;
  std::uint32_t levels = 1;
  if (std::unique_ptr<opj_codestream_info_v2_t, InfoDeleter> info(opj_get_cstr_info(codec.get())); info && info->m_default_tile_info.tccp_info != nullptr) {
    levels = std::max<OPJ_UINT32>(1, info->m_default_tile_info.tccp_info[0].numresolutions);
  }
  while (((full_w + (std::int64_t{1} << reduction) - 1) >> reduction) * ((full_h + (std::int64_t{1} << reduction) - 1) >> reduction) > max_pixels) {
    if (reduction + 1 >= levels) {
      return make_error(ErrorCode::kLimitExceeded, "JPXDecode: image too large even at its smallest resolution");
    }
    ++reduction;
  }
  if (reduction > 0 && opj_set_decoded_resolution_factor(codec.get(), reduction) == OPJ_FALSE) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: cannot reduce the resolution");
  }
  if (opj_decode(codec.get(), stream.get(), image.get()) == OPJ_FALSE || opj_end_decompress(codec.get(), stream.get()) == OPJ_FALSE) {
    return make_error(ErrorCode::kCorruptData, "JPXDecode: " + (error.empty() ? std::string("undecodable data") : error));
  }

  // The output grid: the largest component; subsampled ones are stretched to it (nearest sample).
  JpxImage out;
  out.reduction = reduction;
  const std::size_t n = image->numcomps;
  OPJ_UINT32 w = 0;
  OPJ_UINT32 h = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (image->comps[i].data == nullptr) {
      return make_error(ErrorCode::kCorruptData, "JPXDecode: a component without data");
    }
    w = std::max(w, image->comps[i].w);
    h = std::max(h, image->comps[i].h);
  }
  if (w == 0 || h == 0 || static_cast<std::int64_t>(w) * h > max_pixels) {
    return make_error(ErrorCode::kLimitExceeded, "JPXDecode: decoded image too large");
  }
  out.width = static_cast<std::int32_t>(w);
  out.height = static_cast<std::int32_t>(h);
  out.components = static_cast<std::int32_t>(n);
  out.has_alpha = n > 1 && image->comps[n - 1].alpha != 0;
  out.samples.resize(static_cast<std::size_t>(w) * h * n);
  for (std::size_t i = 0; i < n; ++i) {
    const opj_image_comp_t& c = image->comps[i];
    for (OPJ_UINT32 y = 0; y < h; ++y) {
      const OPJ_UINT32 cy = std::min<OPJ_UINT32>(c.h - 1, static_cast<OPJ_UINT32>((static_cast<std::uint64_t>(y) * c.h) / h));
      for (OPJ_UINT32 x = 0; x < w; ++x) {
        const OPJ_UINT32 cx = std::min<OPJ_UINT32>(c.w - 1, static_cast<OPJ_UINT32>((static_cast<std::uint64_t>(x) * c.w) / w));
        out.samples[(((static_cast<std::size_t>(y) * w) + x) * n) + i] = to_byte(c, c.data[(static_cast<std::size_t>(cy) * c.w) + cx]);
      }
    }
  }
  const std::size_t colour = n - (out.has_alpha ? 1 : 0);
  switch (image->color_space) {
    case OPJ_CLRSPC_GRAY:
      out.space = JpxImage::Space::kGray;
      break;
    case OPJ_CLRSPC_SRGB:
      out.space = JpxImage::Space::kRgb;
      break;
    case OPJ_CLRSPC_CMYK:
      out.space = JpxImage::Space::kCmyk;
      break;
    case OPJ_CLRSPC_SYCC:
    case OPJ_CLRSPC_EYCC:
      // YCC to RGB (ITU-R BT.601, as sYCC defines it).
      if (colour >= 3) {
        for (std::size_t p = 0; p < static_cast<std::size_t>(w) * h; ++p) {
          std::uint8_t* s = &out.samples[p * n];
          const double yy = s[0];
          const double cb = s[1] - 128.0;
          const double cr = s[2] - 128.0;
          const auto clamp = [](double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0))); };
          s[0] = clamp(yy + (1.402 * cr));
          s[1] = clamp(yy - (0.344136 * cb) - (0.714136 * cr));
          s[2] = clamp(yy + (1.772 * cb));
        }
        out.space = JpxImage::Space::kRgb;
      }
      break;
    default:
      // Unspecified: by the number of colour components.
      if (colour == 1) {
        out.space = JpxImage::Space::kGray;
      } else if (colour == 3) {
        out.space = JpxImage::Space::kRgb;
      } else if (colour == 4) {
        out.space = JpxImage::Space::kCmyk;
      }
      break;
  }
  return out;
}

}  // namespace capriole::filters
