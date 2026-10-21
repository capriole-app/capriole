#include "capriole/filters/jbig2.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "capriole/filters/ccitt.hpp"
#include "jbig2_arith.hpp"
#include "jbig2_generic.hpp"
#include "jbig2_huffman.hpp"

namespace capriole::filters {

namespace {

using jbig2::ArithmeticDecoder;
using jbig2::Bitmap;
using jbig2::BitReader;
using jbig2::Combine;
using jbig2::Contexts;
using jbig2::decode_generic;
using jbig2::generic_contexts;
using jbig2::GenericParams;
using jbig2::HuffmanTable;
using jbig2::HuffmanValue;

// ── Bitmaps (jbig2_generic.hpp) ────────────────────────────────────────────────────────────

Combine combine_of(unsigned bits) {
  switch (bits) {
    case 1:
      return Combine::kAnd;
    case 2:
      return Combine::kXor;
    case 3:
      return Combine::kXnor;
    case 4:
      return Combine::kReplace;
    default:
      return Combine::kOr;
  }
}

// Every bitmap allocated counts against a pixel budget: the data is hostile, and symbol dictionaries or
// halftone grids can ask for any number of bitmaps.
class Budget {
 public:
  Budget(std::int64_t per_bitmap, std::int64_t total) : per_bitmap_(per_bitmap), left_(total) {}

  Result<Bitmap> bitmap(std::int64_t width, std::int64_t height, std::uint8_t fill = 0) {
    if (width == 0 || height == 0) {
      return Bitmap{};  // no pixels: 0 x 0, so that nothing loops over the other side (it may be huge)
    }
    if (width < 0 || height < 0 || width > per_bitmap_ / height) {
      return make_error(ErrorCode::kLimitExceeded, "JBIG2 bitmap too large");
    }
    const std::int64_t pixels = width * height;
    if (pixels > left_) {
      return make_error(ErrorCode::kLimitExceeded, "JBIG2 bitmaps exceed the limit");
    }
    left_ -= pixels;
    return Bitmap{.width = static_cast<std::int32_t>(width),
                  .height = static_cast<std::int32_t>(height),
                  .px = std::vector<std::uint8_t>(static_cast<std::size_t>(pixels), fill)};
  }

 private:
  std::int64_t per_bitmap_;
  std::int64_t left_;
};

// ── Byte fields of segment data ─────────────────────────────────────────────────────────────

class Fields {
 public:
  explicit Fields(std::span<const std::byte> data) : data_(data) {}

  [[nodiscard]] bool has(std::size_t count) const noexcept { return at_ + count <= data_.size(); }
  std::uint32_t u8() { return take(1); }
  std::uint32_t u16() { return take(2); }
  std::uint32_t u32() { return take(4); }
  std::int32_t i8() { return static_cast<std::int8_t>(static_cast<std::uint8_t>(take(1))); }
  std::int32_t i32() { return static_cast<std::int32_t>(take(4)); }
  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] std::size_t position() const noexcept { return at_; }
  [[nodiscard]] std::span<const std::byte> rest() const { return data_.subspan(std::min(at_, data_.size())); }

 private:
  std::uint32_t take(std::size_t count) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (at_ < data_.size()) {
        v = (v << 8U) | std::to_integer<std::uint32_t>(data_[at_]);
      } else {
        v <<= 8U;
        ok_ = false;
      }
      ++at_;
    }
    return v;
  }

  std::span<const std::byte> data_;
  std::size_t at_ = 0;
  bool ok_ = true;
};

// ── Generic region decoding (6.2): jbig2_generic.cpp; MMR here ─────────────────────────────

// MMR (T.6) coded bitmap: consumed receives the bytes used.
Result<void> decode_mmr(std::span<const std::byte> data, Bitmap& b, std::size_t* consumed) {
  CcittParams p;
  p.k = -1;
  p.columns = std::max(b.width, 1);
  p.rows = b.height;
  p.black_is_1 = true;
  const std::size_t row_bytes = (static_cast<std::size_t>(p.columns) + 7) / 8;
  auto rows = ccitt_decode(data, p, (row_bytes * static_cast<std::size_t>(std::max(b.height, 0))) + 1, consumed);
  if (!rows) {
    return std::unexpected(std::move(rows.error()));
  }
  for (std::int64_t y = 0; y < b.height; ++y) {
    for (std::int64_t x = 0; x < b.width; ++x) {
      const std::byte packed = (*rows)[(static_cast<std::size_t>(y) * row_bytes) + (static_cast<std::size_t>(x) / 8)];
      b.set(x, y, static_cast<std::uint8_t>((std::to_integer<unsigned>(packed) >> (7 - (static_cast<unsigned>(x) % 8))) & 1U));
    }
  }
  return {};
}

// ── Generic refinement region decoding (6.3) ───────────────────────────────────────────────

struct RefinementParams {
  unsigned tmpl = 0;
  bool tpgron = false;
  std::array<std::int32_t, 4> at{};  // (x, y) of the AT pixel in the new bitmap, then in the reference
  const Bitmap* reference = nullptr;
  std::int64_t dx = 0;
  std::int64_t dy = 0;
};

std::size_t refinement_contexts(unsigned tmpl) { return tmpl == 0 ? std::size_t{1} << 13U : std::size_t{1} << 10U; }

void decode_refinement(ArithmeticDecoder& decoder, Contexts& contexts, Bitmap& b, const RefinementParams& p) {
  const Bitmap& ref = *p.reference;
  bool ltp = false;
  for (std::int64_t y = 0; y < b.height && !decoder.exhausted(); ++y) {
    if (p.tpgron) {
      ltp = ltp != (decoder.decode(contexts, p.tmpl == 0 ? 0x0010 : 0x0008) != 0);
    }
    for (std::int64_t x = 0; x < b.width; ++x) {
      const auto r = [&](std::int64_t i, std::int64_t j) { return static_cast<std::uint32_t>(ref.get(x - p.dx + i, y - p.dy + j)); };
      const auto c = [&](std::int64_t i, std::int64_t j) { return static_cast<std::uint32_t>(b.get(x + i, y + j)); };
      if (ltp) {
        // TPGRPIX: where the reference is uniform around the pixel, the pixel is that value.
        const std::uint32_t v = r(-1, -1);
        bool uniform = true;
        for (std::int64_t j = -1; j <= 1 && uniform; ++j) {
          for (std::int64_t i = -1; i <= 1 && uniform; ++i) {
            uniform = r(i, j) == v;
          }
        }
        if (uniform) {
          b.set(x, y, static_cast<std::uint8_t>(v));
          continue;
        }
      }
      std::uint32_t cx = 0;
      if (p.tmpl == 0) {
        cx = r(1, 1) | (r(0, 1) << 1U) | (r(-1, 1) << 2U) | (r(1, 0) << 3U) | (r(0, 0) << 4U) | (r(-1, 0) << 5U) | (r(1, -1) << 6U) | (r(0, -1) << 7U) |
             (r(p.at[2], p.at[3]) << 8U) | (c(-1, 0) << 9U) | (c(1, -1) << 10U) | (c(0, -1) << 11U) | (c(p.at[0], p.at[1]) << 12U);
      } else {
        cx = r(1, 1) | (r(0, 1) << 1U) | (r(1, 0) << 2U) | (r(0, 0) << 3U) | (r(-1, 0) << 4U) | (r(0, -1) << 5U) | (c(-1, 0) << 6U) | (c(1, -1) << 7U) |
             (c(0, -1) << 8U) | (c(-1, -1) << 9U);
      }
      b.set(x, y, static_cast<std::uint8_t>(decoder.decode(contexts, cx)));
    }
  }
}

// ── Integer sources: the arithmetic IAx procedures or Huffman tables ───────────────────────

// The contexts of every IAx procedure a symbol dictionary or a text region uses.
struct IntegerContexts {
  Contexts dh{512}, dw{512}, ex{512}, ai{512}, dt{512}, fs{512}, ds{512}, it{512}, ri{512}, rdw{512}, rdh{512}, rdx{512}, rdy{512};
  Contexts id;
};

// A value, OOB, or a failure (damaged data).
struct Integer {
  bool ok = false;
  bool oob = false;
  std::int64_t value = 0;
};

Integer from(std::optional<std::int32_t> v) { return v ? Integer{.ok = true, .oob = false, .value = *v} : Integer{.ok = true, .oob = true, .value = 0}; }
Integer from(const HuffmanValue& v) { return {.ok = v.ok, .oob = v.oob, .value = v.value}; }

unsigned code_length_for(std::size_t symbols) {
  unsigned length = 0;
  while ((std::size_t{1} << length) < symbols && length < 31) {
    ++length;
  }
  return length;
}

using Symbols = std::vector<std::shared_ptr<const Bitmap>>;

// ── Text region decoding (6.4) ─────────────────────────────────────────────────────────────

struct TextParams {
  bool huffman = false;
  bool refine = false;
  std::uint32_t log_strips = 0;
  unsigned corner = 1;  // 0 bottom-left, 1 top-left, 2 bottom-right, 3 top-right
  bool transposed = false;
  Combine combine = Combine::kOr;
  std::uint8_t default_pixel = 0;
  std::int32_t ds_offset = 0;
  unsigned rtemplate = 0;
  std::array<std::int32_t, 4> rat{};
  std::uint32_t instances = 0;
  std::int64_t width = 0;
  std::int64_t height = 0;
  const Symbols* symbols = nullptr;
  unsigned code_length = 0;
  // Huffman: the tables (and the symbol ID table: null reads code_length raw bits).
  const HuffmanTable* fs = nullptr;
  const HuffmanTable* ds = nullptr;
  const HuffmanTable* dt = nullptr;
  const HuffmanTable* rdw = nullptr;
  const HuffmanTable* rdh = nullptr;
  const HuffmanTable* rdx = nullptr;
  const HuffmanTable* rdy = nullptr;
  const HuffmanTable* rsize = nullptr;
  const HuffmanTable* ids = nullptr;
};

// Data of a text region, arithmetic or Huffman coded. The arithmetic decoder (and its contexts) are
// shared with the caller: a symbol dictionary's refinement/aggregate text regions read on from its own.
class TextDecoder {
 public:
  TextDecoder(ArithmeticDecoder* arithmetic, BitReader* bits, std::span<const std::byte> data, IntegerContexts& ix, Contexts& gr, Budget& budget)
      : arithmetic_(arithmetic), bits_(bits), data_(data), ix_(&ix), gr_(&gr), budget_(&budget) {}

  Result<Bitmap> decode(const TextParams& p) {
    auto region = budget_->bitmap(p.width, p.height, p.default_pixel);
    if (!region) {
      return region;
    }
    const std::int64_t strips = std::int64_t{1} << std::min<std::uint32_t>(p.log_strips, 3);
    const Integer first_t = number(p, ix_->dt, p.dt);
    if (!first_t.ok || first_t.oob) {
      return region;
    }
    std::int64_t strip_t = -first_t.value * strips;
    std::int64_t first_s = 0;
    std::uint32_t done = 0;
    while (done < p.instances) {
      const Integer dt = number(p, ix_->dt, p.dt);
      if (!dt.ok || dt.oob) {
        break;
      }
      strip_t += dt.value * strips;
      const Integer dfs = number(p, ix_->fs, p.fs);
      if (!dfs.ok || dfs.oob) {
        break;
      }
      first_s += dfs.value;
      std::int64_t current_s = first_s;
      for (;;) {
        if (p.huffman ? bits_->exhausted() : arithmetic_->exhausted()) {
          return region;  // the data ran out: what was placed
        }
        std::int64_t current_t = 0;
        if (strips > 1) {
          current_t = p.huffman ? static_cast<std::int64_t>(bits_->bits(p.log_strips)) : value_or(decode_ia(ix_->it), 0);
        }
        const std::int64_t t = strip_t + current_t;
        std::uint32_t id = 0;
        if (p.huffman) {
          if (p.ids != nullptr) {
            const HuffmanValue v = p.ids->decode(*bits_);
            if (!v.ok || v.oob) {
              return region;
            }
            id = static_cast<std::uint32_t>(v.value);
          } else {
            id = bits_->bits(p.code_length);
          }
        } else {
          id = jbig2::decode_id(*arithmetic_, ix_->id, p.code_length);
        }
        if (id >= p.symbols->size() || !(*p.symbols)[id]) {
          return region;  // an ID past the symbols: damaged data, keep what is placed
        }
        std::shared_ptr<const Bitmap> symbol = (*p.symbols)[id];
        bool refine = false;
        if (p.refine) {
          refine = p.huffman ? bits_->bit() != 0 : value_or(decode_ia(ix_->ri), 0) != 0;
        }
        if (refine) {
          auto refined = refined_symbol(p, *symbol);
          if (!refined) {
            return region;
          }
          symbol = std::make_shared<const Bitmap>(std::move(*refined));
        }
        const std::int64_t w = symbol->width;
        const std::int64_t h = symbol->height;
        // 6.4.5 (3 c x): the reference corner is at (CURS, T), or (T, CURS) when transposed.
        const bool right = (p.corner & 2U) != 0;
        const bool top = (p.corner & 1U) != 0;
        if (!p.transposed && right) {
          current_s += w - 1;
        } else if (p.transposed && !top) {
          current_s += h - 1;
        }
        const std::int64_t rx = p.transposed ? t : current_s;
        const std::int64_t ry = p.transposed ? current_s : t;
        region->compose(*symbol, rx - (right ? w - 1 : 0), ry - (top ? 0 : h - 1), p.combine);
        if (!p.transposed && !right) {
          current_s += w - 1;
        } else if (p.transposed && top) {
          current_s += h - 1;
        }
        ++done;
        const Integer ds = number(p, ix_->ds, p.ds);
        if (!ds.ok || ds.oob) {
          break;
        }
        if (done >= p.instances) {
          return region;  // more instances than announced: damaged data
        }
        current_s += ds.value + p.ds_offset;
      }
    }
    return region;
  }

 private:
  std::optional<std::int32_t> decode_ia(Contexts& contexts) { return jbig2::decode_integer(*arithmetic_, contexts); }
  static std::int64_t value_or(std::optional<std::int32_t> v, std::int64_t fallback) { return v ? *v : fallback; }

  Integer number(const TextParams& p, Contexts& contexts, const HuffmanTable* table) {
    if (p.huffman) {
      return table != nullptr ? from(table->decode(*bits_)) : Integer{};
    }
    return from(decode_ia(contexts));
  }

  // 6.4.11: the symbol refined, RDW/RDH changing its size, RDX/RDY moving the reference.
  Result<Bitmap> refined_symbol(const TextParams& p, const Bitmap& symbol) {
    const Integer rdw = number(p, ix_->rdw, p.rdw);
    const Integer rdh = number(p, ix_->rdh, p.rdh);
    const Integer rdx = number(p, ix_->rdx, p.rdx);
    const Integer rdy = number(p, ix_->rdy, p.rdy);
    if (!rdw.ok || !rdh.ok || !rdx.ok || !rdy.ok || rdw.oob || rdh.oob || rdx.oob || rdy.oob) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 refinement without its offsets");
    }
    auto bitmap = budget_->bitmap(symbol.width + rdw.value, symbol.height + rdh.value);
    if (!bitmap) {
      return bitmap;
    }
    RefinementParams rp;
    rp.tmpl = p.rtemplate;
    rp.at = p.rat;
    rp.reference = &symbol;
    rp.dx = (rdw.value >> 1) + rdx.value;  // floor(RDW / 2) + RDX
    rp.dy = (rdh.value >> 1) + rdy.value;
    if (p.huffman) {
      // 6.4.11.1: its data is arithmetic coded, BMSIZE bytes from the next byte boundary.
      const Integer size = p.rsize != nullptr ? from(p.rsize->decode(*bits_)) : Integer{};
      if (!size.ok || size.oob || size.value < 0) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 refinement without its size");
      }
      bits_->align();
      const std::size_t start = std::min(bits_->byte_position(), data_.size());
      const std::size_t length = std::min(static_cast<std::size_t>(size.value), data_.size() - start);
      ArithmeticDecoder local(data_.subspan(start, length));
      decode_refinement(local, *gr_, *bitmap, rp);
      bits_->skip_bytes(length);
    } else {
      decode_refinement(*arithmetic_, *gr_, *bitmap, rp);
    }
    return bitmap;
  }

  ArithmeticDecoder* arithmetic_;
  BitReader* bits_;
  std::span<const std::byte> data_;
  IntegerContexts* ix_;
  Contexts* gr_;
  Budget* budget_;
};

// ── Segments ───────────────────────────────────────────────────────────────────────────────

struct SymbolDictionary {
  Symbols exported;
  // Retained contexts (flag "bitmap coding context retained"), for a later dictionary to use.
  std::shared_ptr<Contexts> gb;
  std::shared_ptr<Contexts> gr;
};

struct PatternDictionary {
  std::vector<Bitmap> patterns;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct Table {
  HuffmanTable table;
};

struct RegionResult {
  Bitmap bitmap;
  std::int64_t x = 0;
  std::int64_t y = 0;
  Combine combine = Combine::kOr;
};

constexpr std::int64_t kMaxBitmapPixels = 80'000'000;
constexpr std::int64_t kUnknownSize = 0xFFFFFFFF;  // a height the data will tell (striped pages, 7.4.8)
constexpr std::int64_t kMaxTotalPixels = 200'000'000;

using Decoded = std::variant<std::monostate, SymbolDictionary, PatternDictionary, Table, RegionResult>;

struct Segment {
  std::uint32_t number = 0;
  unsigned type = 0;
  std::vector<std::uint32_t> referred;
  std::uint32_t page = 0;
  std::span<const std::byte> data;
};

struct RegionInfo {
  std::int64_t width = 0;
  std::int64_t height = 0;
  std::int64_t x = 0;
  std::int64_t y = 0;
  Combine combine = Combine::kOr;
};

RegionInfo region_info(Fields& f) {
  RegionInfo r;
  r.width = f.u32();
  r.height = f.u32();
  r.x = f.u32();
  r.y = f.u32();
  r.combine = combine_of(f.u8() & 7U);
  return r;
}

class Decoder {
 public:
  // Bitmaps take a byte per pixel: at most 80 Mpixel each (an A3 page at 600 dpi) and 200 Mpixel in all,
  // and never more than the output allows.
  explicit Decoder(std::size_t max_output)
      : budget_(std::min<std::int64_t>(kMaxBitmapPixels, pixels_for(max_output)), std::min<std::int64_t>(kMaxTotalPixels, 4 * pixels_for(max_output))) {}

  // The pixels max_output bytes hold at 1 bit each (in 64 bits: size_t is 32 bits in WebAssembly).
  static std::int64_t pixels_for(std::size_t max_output) { return static_cast<std::int64_t>(std::min<std::uint64_t>(max_output, std::uint64_t{1} << 40U)) * 8; }

  // Reads the segments of data (headers followed by their data: the embedded organisation).
  Result<void> read(std::span<const std::byte> data) {
    Fields f(data);
    while (f.has(11)) {
      Segment s;
      s.number = f.u32();
      const std::uint32_t flags = f.u8();
      s.type = flags & 63U;
      const bool long_page = (flags & 64U) != 0;
      const std::uint32_t head = f.u8();
      std::uint32_t count = head >> 5U;
      if (count == 7) {
        // Long form: 29 bits of count, then the retention bits (7.2.4).
        const std::uint32_t rest = f.u16() << 8U | f.u8();
        count = ((head & 0x1FU) << 24U) | rest;
        if (count > 65536) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 segment referring to too many segments");
        }
        for (std::uint32_t i = 0; i < (count + 8) / 8; ++i) {
          (void)f.u8();
        }
      }
      // Referred segment numbers take 1, 2 or 4 bytes depending on this segment's number (7.2.5).
      for (std::uint32_t i = 0; i < count; ++i) {
        if (s.number <= 256) {
          s.referred.push_back(f.u8());
        } else if (s.number <= 65536) {
          s.referred.push_back(f.u16());
        } else {
          s.referred.push_back(f.u32());
        }
      }
      s.page = long_page ? f.u32() : f.u8();
      std::uint32_t length = f.u32();
      if (!f.ok()) {
        break;
      }
      const std::span<const std::byte> rest = f.rest();
      if (length == 0xFFFFFFFF) {
        // Only an immediate generic region may leave its length unknown (7.2.7): find its end marker.
        auto found = unknown_length(s.type, rest);
        if (!found) {
          return std::unexpected(std::move(found.error()));
        }
        length = static_cast<std::uint32_t>(*found);
      }
      if (length > rest.size()) {
        length = static_cast<std::uint32_t>(rest.size());  // truncated: what there is
      }
      s.data = rest.subspan(0, length);
      for (std::uint32_t i = 0; i < length; ++i) {
        (void)f.u8();
      }
      if (auto r = segment(s); !r) {
        return r;
      }
      if (s.type == 51 || (s.type == 49 && page_)) {
        break;  // end of file, end of page
      }
    }
    return {};
  }

  // The page, 1 bit per pixel, 0 for black.
  Result<std::vector<std::byte>> output(std::size_t max_output) {
    if (!page_) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 data without a page");
    }
    const auto row_bytes = (static_cast<std::size_t>(page_->width) + 7) / 8;
    const auto rows = static_cast<std::size_t>(page_->height);
    if (row_bytes * rows > max_output) {
      return make_error(ErrorCode::kLimitExceeded, "JBIG2 page exceeds the limit");
    }
    std::vector<std::byte> out(row_bytes * rows, std::byte{0xFF});
    const auto width = static_cast<std::size_t>(page_->width);
    for (std::size_t y = 0; y < rows; ++y) {
      for (std::size_t x = 0; x < width; ++x) {
        if (page_->px[(y * width) + x] != 0) {
          out[(y * row_bytes) + (x / 8)] &= static_cast<std::byte>(~(0x80U >> (x % 8)));
        }
      }
    }
    return out;
  }

 private:
  // The data length of a generic region left unknown: up to its end marker (0xFF 0xAC arithmetic,
  // 0x00 0x00 MMR) and the row count after it.
  static Result<std::size_t> unknown_length(unsigned type, std::span<const std::byte> rest) {
    if ((type != 38 && type != 39) || rest.size() < 18) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 segment of unknown length");
    }
    const bool mmr = (std::to_integer<unsigned>(rest[17]) & 1U) != 0;
    const std::byte a = mmr ? std::byte{0x00} : std::byte{0xFF};
    const std::byte b = mmr ? std::byte{0x00} : std::byte{0xAC};
    for (std::size_t i = 18; i + 5 < rest.size(); ++i) {
      if (rest[i] == a && rest[i + 1] == b) {
        return i + 6;
      }
    }
    return rest.size();
  }

  Result<void> segment(const Segment& s) {
    // Segments of another page than the first one are not ours (PDF allows one page per stream).
    if (s.page != 0 && page_number_ != 0 && s.page != page_number_) {
      return {};
    }
    Fields f(s.data);
    switch (s.type) {
      case 0:
        return symbol_dictionary(s, f);
      case 4:
      case 6:
      case 7:
        return text_region(s, f);
      case 16:
        return pattern_dictionary(s, f);
      case 20:
      case 22:
      case 23:
        return halftone_region(s, f);
      case 36:
      case 38:
      case 39:
        return generic_region(s, f);
      case 40:
      case 42:
      case 43:
        return refinement_region(s, f);
      case 48:
        return page_information(s, f);
      case 50:
        return end_of_stripe(f);
      case 53: {
        auto table = jbig2::parse_table_segment(s.data);
        if (!table) {
          return std::unexpected(std::move(table.error()));
        }
        results_[s.number] = Table{.table = std::move(*table)};
        return {};
      }
      default:
        return {};  // end of page/file, profiles, extensions: nothing to draw
    }
  }

  // ── Page ───────────────────────────────────────────────────────────────────────────────

  Result<void> page_information(const Segment& s, Fields& f) {
    if (page_) {
      return {};  // a second page: not ours
    }
    const std::int64_t width = f.u32();
    const std::int64_t height = f.u32();
    (void)f.u32();
    (void)f.u32();
    const std::uint32_t flags = f.u8();
    (void)f.u16();
    if (!f.ok()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 page information too short");
    }
    default_pixel_ = static_cast<std::uint8_t>((flags >> 2U) & 1U);
    page_combine_ = combine_of((flags >> 3U) & 3U);
    combine_override_ = (flags & 64U) != 0;
    unknown_height_ = height == kUnknownSize;
    page_number_ = s.page;
    auto page = budget_.bitmap(width, unknown_height_ ? 0 : height, default_pixel_);
    if (!page) {
      return std::unexpected(std::move(page.error()));
    }
    page_ = std::move(*page);
    return {};
  }

  Result<void> end_of_stripe(Fields& f) {
    const std::int64_t end = f.u32();
    if (page_ && unknown_height_ && f.ok()) {
      return grow_page(*page_, end + 1);
    }
    return {};
  }

  Result<void> grow_page(Bitmap& page, std::int64_t height) {
    if (height <= page.height) {
      return {};
    }
    auto extra = budget_.bitmap(page.width, height - page.height, default_pixel_);
    if (!extra) {
      return std::unexpected(std::move(extra.error()));
    }
    page.px.insert(page.px.end(), extra->px.begin(), extra->px.end());
    page.height = static_cast<std::int32_t>(height);
    return {};
  }

  // An immediate region onto the page (7.4.x.x, with the page's combination operator unless regions
  // may override it); an intermediate one kept for a refinement to use.
  Result<void> place(const Segment& s, Bitmap bitmap, const RegionInfo& info, bool intermediate) {
    if (intermediate) {
      results_[s.number] = RegionResult{.bitmap = std::move(bitmap), .x = info.x, .y = info.y, .combine = info.combine};
      return {};
    }
    if (!page_) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 region before the page information");
    }
    Bitmap& page = *page_;
    if (unknown_height_) {
      if (auto grown = grow_page(page, info.y + bitmap.height); !grown) {
        return grown;
      }
    }
    page.compose(bitmap, info.x, info.y, combine_override_ ? info.combine : page_combine_);
    return {};
  }

  // ── Referred segments ──────────────────────────────────────────────────────────────────

  template <typename T>
  std::vector<const T*> referred(const Segment& s) {
    std::vector<const T*> out;
    for (const std::uint32_t n : s.referred) {
      if (const auto found = results_.find(n); found != results_.end()) {
        if (const T* v = std::get_if<T>(&found->second)) {
          out.push_back(v);
        }
      }
    }
    return out;
  }

  // Custom tables are taken in order from the referred table segments (7.4.2.1.6, 7.4.3.1.6).
  class TablePicker {
   public:
    explicit TablePicker(std::vector<const Table*> custom) : custom_(std::move(custom)) {}
    const HuffmanTable* pick(unsigned selector, std::initializer_list<int> standard) {
      if (selector < standard.size()) {
        return &jbig2::standard_table(*(standard.begin() + selector));
      }
      if (next_ < custom_.size()) {
        return &custom_[next_++]->table;
      }
      return nullptr;
    }

   private:
    std::vector<const Table*> custom_;
    std::size_t next_ = 0;
  };

  // ── Generic and refinement regions ─────────────────────────────────────────────────────

  Result<void> generic_region(const Segment& s, Fields& f) {
    RegionInfo info = region_info(f);
    const std::uint32_t flags = f.u8();
    const bool mmr = (flags & 1U) != 0;
    GenericParams p;
    p.tmpl = (flags >> 1U) & 3U;
    p.tpgdon = (flags & 8U) != 0;
    if (!mmr) {
      const std::size_t pairs = p.tmpl == 0 ? 4 : 1;
      for (std::size_t i = 0; i < 2 * pairs; ++i) {
        p.at.at(i) = f.i8();
      }
    }
    if (!f.ok()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 generic region header too short");
    }
    const std::span<const std::byte> data = f.rest();
    // A length left unknown: the row count after the end marker may be the real height (7.4.6.4).
    if (s.data.size() >= 4 && info.height == kUnknownSize) {
      const auto& d = s.data;
      std::uint32_t rows = 0;
      for (std::size_t i = d.size() - 4; i < d.size(); ++i) {
        rows = (rows << 8U) | std::to_integer<std::uint32_t>(d[i]);
      }
      info.height = rows;
    }
    auto bitmap = budget_.bitmap(info.width, info.height);
    if (!bitmap) {
      return std::unexpected(std::move(bitmap.error()));
    }
    if (mmr) {
      if (auto r = decode_mmr(data, *bitmap, nullptr); !r) {
        return r;
      }
    } else {
      Contexts contexts(generic_contexts(p.tmpl));
      ArithmeticDecoder decoder(data);
      decode_generic(decoder, contexts, *bitmap, p);
    }
    return place(s, std::move(*bitmap), info, s.type == 36);
  }

  Result<void> refinement_region(const Segment& s, Fields& f) {
    const RegionInfo info = region_info(f);
    const std::uint32_t flags = f.u8();
    RefinementParams p;
    p.tmpl = flags & 1U;
    p.tpgron = (flags & 2U) != 0;
    if (p.tmpl == 0) {
      for (std::size_t i = 0; i < 4; ++i) {
        p.at.at(i) = f.i8();
      }
    }
    if (!f.ok()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 refinement region header too short");
    }
    // The reference: the intermediate region referred to, or the page under this region (7.4.7.5).
    Bitmap from_page;
    const auto regions = referred<RegionResult>(s);
    if (!regions.empty()) {
      p.reference = &regions.front()->bitmap;
    } else if (page_) {
      auto area = budget_.bitmap(info.width, info.height);
      if (!area) {
        return std::unexpected(std::move(area.error()));
      }
      from_page = std::move(*area);
      for (std::int64_t y = 0; y < info.height; ++y) {
        for (std::int64_t x = 0; x < info.width; ++x) {
          from_page.set(x, y, page_->get(info.x + x, info.y + y));
        }
      }
      p.reference = &from_page;
    } else {
      return make_error(ErrorCode::kCorruptData, "JBIG2 refinement without a reference");
    }
    auto bitmap = budget_.bitmap(info.width, info.height);
    if (!bitmap) {
      return std::unexpected(std::move(bitmap.error()));
    }
    Contexts contexts(refinement_contexts(p.tmpl));
    ArithmeticDecoder decoder(f.rest());
    decode_refinement(decoder, contexts, *bitmap, p);
    // Refining the page itself or an intermediate region, the result is placed as any region is — with
    // the region's combination operator (as jbig2dec and pdfium do).
    return place(s, std::move(*bitmap), info, s.type == 40);
  }

  // ── Symbol dictionaries (6.5, 7.4.2) ───────────────────────────────────────────────────

  Result<void> symbol_dictionary(const Segment& s, Fields& f) {
    const std::uint32_t flags = f.u16();
    const bool huffman = (flags & 1U) != 0;
    const bool refagg = (flags & 2U) != 0;
    const unsigned sel_dh = (flags >> 2U) & 3U;
    const unsigned sel_dw = (flags >> 4U) & 3U;
    const unsigned sel_bmsize = (flags >> 6U) & 1U;
    const unsigned sel_agginst = (flags >> 7U) & 1U;
    const bool context_used = (flags & 0x100U) != 0;
    const bool context_retained = (flags & 0x200U) != 0;
    const unsigned tmpl = (flags >> 10U) & 3U;
    const unsigned rtemplate = (flags >> 12U) & 1U;
    GenericParams gp;
    gp.tmpl = tmpl;
    if (!huffman) {
      const std::size_t pairs = tmpl == 0 ? 4 : 1;
      for (std::size_t i = 0; i < 2 * pairs; ++i) {
        gp.at.at(i) = f.i8();
      }
    }
    std::array<std::int32_t, 4> rat{};
    if (refagg && rtemplate == 0) {
      for (auto& v : rat) {
        v = f.i8();
      }
    }
    const std::uint32_t exported_count = f.u32();
    const std::uint32_t new_count = f.u32();
    if (!f.ok()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary header too short");
    }
    constexpr std::uint32_t kMaxSymbols = 1'000'000;
    if (new_count > kMaxSymbols || exported_count > kMaxSymbols) {
      return make_error(ErrorCode::kLimitExceeded, "JBIG2 symbol dictionary with too many symbols");
    }
    // The input symbols: those exported by the dictionaries referred to, in order.
    Symbols symbols;
    const auto dictionaries = referred<SymbolDictionary>(s);
    for (const SymbolDictionary* d : dictionaries) {
      symbols.insert(symbols.end(), d->exported.begin(), d->exported.end());
    }
    const std::size_t input_count = symbols.size();
    TablePicker tables(referred<Table>(s));
    const HuffmanTable* table_dh = huffman ? tables.pick(sel_dh, {4, 5}) : nullptr;
    const HuffmanTable* table_dw = huffman ? tables.pick(sel_dw, {2, 3}) : nullptr;
    const HuffmanTable* table_bmsize = huffman ? tables.pick(sel_bmsize, {1}) : nullptr;
    const HuffmanTable* table_agginst = huffman ? tables.pick(sel_agginst, {1}) : nullptr;
    if (huffman && (table_dh == nullptr || table_dw == nullptr || table_bmsize == nullptr || table_agginst == nullptr)) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary without its Huffman tables");
    }

    const std::span<const std::byte> data = f.rest();
    ArithmeticDecoder arithmetic(data);
    BitReader bits(data);
    IntegerContexts ix;
    const unsigned code_length = code_length_for(input_count + new_count);
    ix.id = Contexts(std::size_t{1} << code_length);
    auto gb = std::make_shared<Contexts>(generic_contexts(tmpl));
    auto gr = std::make_shared<Contexts>(refinement_contexts(rtemplate));
    if (context_used && !dictionaries.empty() && dictionaries.back()->gb && dictionaries.back()->gb->size() == gb->size()) {
      *gb = *dictionaries.back()->gb;
      if (dictionaries.back()->gr && dictionaries.back()->gr->size() == gr->size()) {
        *gr = *dictionaries.back()->gr;
      }
    }
    const auto number = [&](Contexts& contexts, const HuffmanTable* table) {
      return huffman ? from(table->decode(bits)) : from(jbig2::decode_integer(arithmetic, contexts));
    };

    // 6.5.5: height classes of symbols.
    std::int64_t height = 0;
    std::uint32_t decoded = 0;
    const auto exhausted = [&] { return huffman ? bits.exhausted() : arithmetic.exhausted(); };
    while (decoded < new_count) {
      if (exhausted()) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: the data ran out");
      }
      const Integer dh = number(ix.dh, table_dh);
      if (!dh.ok || dh.oob) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: bad height class");
      }
      height += dh.value;
      if (height < 0 || height > (1 << 24)) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 symbol height out of range");
      }
      std::int64_t width = 0;
      std::int64_t total_width = 0;
      const std::size_t class_first = symbols.size();
      std::vector<std::int64_t> widths;
      for (;;) {
        if (exhausted()) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: the data ran out");
        }
        const Integer dw = number(ix.dw, table_dw);
        if (!dw.ok) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: bad symbol width");
        }
        if (dw.oob) {
          break;
        }
        if (decoded >= new_count) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: more symbols than announced");
        }
        width += dw.value;
        if (width < 0 || width > (1 << 24)) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 symbol width out of range");
        }
        total_width += width;
        if (!huffman || refagg) {
          auto symbol = budget_.bitmap(width, height);
          if (!symbol) {
            return std::unexpected(std::move(symbol.error()));
          }
          if (!refagg) {
            decode_generic(arithmetic, *gb, *symbol, gp);
          } else {
            auto made =
                aggregate(*symbol, huffman, arithmetic, bits, data, ix, *gr, symbols, input_count + new_count, code_length, rtemplate, rat, table_agginst);
            if (!made) {
              return made;
            }
          }
          symbols.push_back(std::make_shared<const Bitmap>(std::move(*symbol)));
        } else {
          widths.push_back(width);
        }
        ++decoded;
      }
      // 6.5.9: Huffman without refinement: the class is one collective bitmap, cut by the widths.
      if (huffman && !refagg) {
        const Integer size = from(table_bmsize->decode(bits));
        if (!size.ok || size.oob || size.value < 0) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 symbol dictionary: bad collective bitmap size");
        }
        bits.align();
        auto collective = budget_.bitmap(total_width, height);
        if (!collective) {
          return std::unexpected(std::move(collective.error()));
        }
        const std::size_t start = std::min(bits.byte_position(), data.size());
        if (size.value == 0) {
          // Uncompressed, rows padded to bytes.
          const std::size_t row_bytes = (static_cast<std::size_t>(total_width) + 7) / 8;
          for (std::int64_t y = 0; y < height; ++y) {
            for (std::int64_t x = 0; x < total_width; ++x) {
              const std::size_t at = start + (static_cast<std::size_t>(y) * row_bytes) + (static_cast<std::size_t>(x) / 8);
              const unsigned byte = at < data.size() ? std::to_integer<unsigned>(data[at]) : 0U;
              collective->set(x, y, static_cast<std::uint8_t>((byte >> (7 - (static_cast<unsigned>(x) % 8))) & 1U));
            }
          }
          bits.skip_bytes(row_bytes * static_cast<std::size_t>(height));
        } else {
          const std::size_t length = std::min(static_cast<std::size_t>(size.value), data.size() - start);
          if (auto r = decode_mmr(data.subspan(start, length), *collective, nullptr); !r) {
            return r;
          }
          bits.skip_bytes(length);
        }
        std::int64_t x = 0;
        for (const std::int64_t w : widths) {
          auto symbol = budget_.bitmap(w, height);
          if (!symbol) {
            return std::unexpected(std::move(symbol.error()));
          }
          symbol->compose(*collective, -x, 0, Combine::kReplace);
          symbols.push_back(std::make_shared<const Bitmap>(std::move(*symbol)));
          x += w;
        }
      }
      (void)class_first;
    }

    // 6.5.10: which symbols are exported, as runs of alternating flags.
    SymbolDictionary out;
    const std::size_t total = symbols.size();
    std::size_t index = 0;
    bool exporting = false;
    std::size_t runs = 0;
    while (index < total && runs++ < 2 * total + 2) {
      const Integer run = huffman ? from(jbig2::standard_table(1).decode(bits)) : from(jbig2::decode_integer(arithmetic, ix.ex));
      if (!run.ok || run.oob || run.value < 0) {
        break;
      }
      const std::size_t end = std::min(total, index + static_cast<std::size_t>(std::min<std::int64_t>(run.value, static_cast<std::int64_t>(total))));
      if (exporting) {
        out.exported.insert(out.exported.end(), symbols.begin() + static_cast<std::ptrdiff_t>(index), symbols.begin() + static_cast<std::ptrdiff_t>(end));
      }
      index = end;
      exporting = !exporting;
    }
    if (context_retained) {
      out.gb = gb;
      out.gr = gr;
    }
    results_[s.number] = std::move(out);
    return {};
  }

  // 6.5.8.2: a symbol made of other symbols — one refined, or several placed as a text region.
  Result<void> aggregate(Bitmap& symbol, bool huffman, ArithmeticDecoder& arithmetic, BitReader& bits, std::span<const std::byte> data, IntegerContexts& ix,
                         Contexts& gr, const Symbols& symbols, std::size_t all_count, unsigned code_length, unsigned rtemplate,
                         const std::array<std::int32_t, 4>& rat, const HuffmanTable* table_agginst) {
    const Integer instances = huffman ? from(table_agginst->decode(bits)) : from(jbig2::decode_integer(arithmetic, ix.ai));
    if (!instances.ok || instances.oob || instances.value < 1 || instances.value > 1'000'000) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 aggregate symbol with a bad instance count");
    }
    (void)all_count;
    if (instances.value == 1) {
      std::uint32_t id = 0;
      Integer rdx;
      Integer rdy;
      if (huffman) {
        id = bits.bits(code_length);
        rdx = from(jbig2::standard_table(15).decode(bits));
        rdy = from(jbig2::standard_table(15).decode(bits));
      } else {
        id = jbig2::decode_id(arithmetic, ix.id, code_length);
        rdx = from(jbig2::decode_integer(arithmetic, ix.rdx));
        rdy = from(jbig2::decode_integer(arithmetic, ix.rdy));
      }
      if (id >= symbols.size() || !rdx.ok || !rdy.ok || rdx.oob || rdy.oob) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 refined symbol with a bad reference");
      }
      RefinementParams p;
      p.tmpl = rtemplate;
      p.at = rat;
      p.reference = symbols[id].get();
      p.dx = rdx.value;
      p.dy = rdy.value;
      if (huffman) {
        const Integer size = from(jbig2::standard_table(1).decode(bits));
        if (!size.ok || size.oob || size.value < 0) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 refined symbol without its size");
        }
        bits.align();
        const std::size_t start = std::min(bits.byte_position(), data.size());
        const std::size_t length = std::min(static_cast<std::size_t>(size.value), data.size() - start);
        ArithmeticDecoder local(data.subspan(start, length));
        decode_refinement(local, gr, symbol, p);
        bits.skip_bytes(length);
      } else {
        decode_refinement(arithmetic, gr, symbol, p);
      }
      return {};
    }
    // Several instances: a text region the size of the symbol (6.5.8.2.1 with the fixed parameters).
    TextParams tp;
    tp.huffman = huffman;
    tp.refine = true;
    tp.corner = 1;
    tp.rtemplate = rtemplate;
    tp.rat = rat;
    tp.instances = static_cast<std::uint32_t>(instances.value);
    tp.width = symbol.width;
    tp.height = symbol.height;
    tp.symbols = &symbols;
    tp.code_length = code_length;
    if (huffman) {
      tp.fs = &jbig2::standard_table(6);
      tp.ds = &jbig2::standard_table(8);
      tp.dt = &jbig2::standard_table(11);
      tp.rdw = &jbig2::standard_table(15);
      tp.rdh = &jbig2::standard_table(15);
      tp.rdx = &jbig2::standard_table(15);
      tp.rdy = &jbig2::standard_table(15);
      tp.rsize = &jbig2::standard_table(1);
    }
    TextDecoder text(&arithmetic, &bits, data, ix, gr, budget_);
    auto made = text.decode(tp);
    if (!made) {
      return std::unexpected(std::move(made.error()));
    }
    symbol = std::move(*made);
    return {};
  }

  // ── Text regions (6.4, 7.4.3) ──────────────────────────────────────────────────────────

  Result<void> text_region(const Segment& s, Fields& f) {
    const RegionInfo info = region_info(f);
    const std::uint32_t flags = f.u16();
    TextParams p;
    p.huffman = (flags & 1U) != 0;
    p.refine = (flags & 2U) != 0;
    p.log_strips = (flags >> 2U) & 3U;
    p.corner = (flags >> 4U) & 3U;
    p.transposed = (flags & 0x40U) != 0;
    p.combine = combine_of((flags >> 7U) & 3U);
    p.default_pixel = static_cast<std::uint8_t>((flags >> 9U) & 1U);
    const std::uint32_t offset = (flags >> 10U) & 0x1FU;
    p.ds_offset = offset >= 16 ? static_cast<std::int32_t>(offset) - 32 : static_cast<std::int32_t>(offset);
    p.rtemplate = (flags >> 15U) & 1U;
    std::uint32_t huffman_flags = 0;
    if (p.huffman) {
      huffman_flags = f.u16();
    }
    if (p.refine && p.rtemplate == 0) {
      for (auto& v : p.rat) {
        v = f.i8();
      }
    }
    p.instances = f.u32();
    if (!f.ok()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 text region header too short");
    }
    Symbols symbols;
    for (const SymbolDictionary* d : referred<SymbolDictionary>(s)) {
      symbols.insert(symbols.end(), d->exported.begin(), d->exported.end());
    }
    p.symbols = &symbols;
    p.width = info.width;
    p.height = info.height;
    p.code_length = code_length_for(symbols.size());
    const std::span<const std::byte> data = f.rest();
    BitReader bits(data);
    HuffmanTable ids;
    if (p.huffman) {
      TablePicker tables(referred<Table>(s));
      p.fs = tables.pick(huffman_flags & 3U, {6, 7});
      p.ds = tables.pick((huffman_flags >> 2U) & 3U, {8, 9, 10});
      p.dt = tables.pick((huffman_flags >> 4U) & 3U, {11, 12, 13});
      p.rdw = tables.pick((huffman_flags >> 6U) & 3U, {14, 15});
      p.rdh = tables.pick((huffman_flags >> 8U) & 3U, {14, 15});
      p.rdx = tables.pick((huffman_flags >> 10U) & 3U, {14, 15});
      p.rdy = tables.pick((huffman_flags >> 12U) & 3U, {14, 15});
      p.rsize = tables.pick((huffman_flags >> 14U) & 1U, {1});
      if (p.fs == nullptr || p.ds == nullptr || p.dt == nullptr || p.rdw == nullptr || p.rdh == nullptr || p.rdx == nullptr || p.rdy == nullptr ||
          p.rsize == nullptr) {
        return make_error(ErrorCode::kCorruptData, "JBIG2 text region without its Huffman tables");
      }
      // 7.4.3.1.7: the symbol ID code lengths, run coded with a table of their own.
      std::array<std::uint32_t, 35> run_lengths{};
      for (auto& length : run_lengths) {
        length = bits.bits(4);
      }
      const HuffmanTable runs = jbig2::table_from_lengths(run_lengths);
      std::vector<std::uint32_t> lengths;
      lengths.reserve(symbols.size());
      while (lengths.size() < symbols.size()) {
        const HuffmanValue code = runs.decode(bits);
        if (!code.ok || code.oob) {
          return make_error(ErrorCode::kCorruptData, "JBIG2 text region: bad symbol ID table");
        }
        std::size_t repeat = 0;
        std::uint32_t value = 0;
        if (code.value < 32) {
          repeat = 1;
          value = static_cast<std::uint32_t>(code.value);
        } else if (code.value == 32) {
          if (lengths.empty()) {
            return make_error(ErrorCode::kCorruptData, "JBIG2 text region: repeat without a length");
          }
          repeat = 3 + bits.bits(2);
          value = lengths.back();
        } else if (code.value == 33) {
          repeat = 3 + bits.bits(3);
        } else {
          repeat = 11 + bits.bits(7);
        }
        for (std::size_t i = 0; i < repeat && lengths.size() < symbols.size(); ++i) {
          lengths.push_back(value);
        }
      }
      bits.align();
      ids = jbig2::table_from_lengths(lengths);
      p.ids = &ids;
    }
    ArithmeticDecoder arithmetic(data);
    IntegerContexts ix;
    ix.id = Contexts(std::size_t{1} << p.code_length);
    Contexts gr(refinement_contexts(p.rtemplate));
    TextDecoder text(&arithmetic, &bits, data, ix, gr, budget_);
    auto region = text.decode(p);
    if (!region) {
      return std::unexpected(std::move(region.error()));
    }
    return place(s, std::move(*region), info, s.type == 4);
  }

  // ── Pattern dictionaries and halftone regions (6.6, 6.7) ───────────────────────────────

  Result<void> pattern_dictionary(const Segment& s, Fields& f) {
    const std::uint32_t flags = f.u8();
    const std::int64_t width = f.u8();
    const std::int64_t height = f.u8();
    const std::int64_t gray_max = f.u32();
    if (!f.ok() || width == 0 || height == 0 || gray_max > 65535) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 pattern dictionary header invalid");
    }
    auto collective = budget_.bitmap((gray_max + 1) * width, height);
    if (!collective) {
      return std::unexpected(std::move(collective.error()));
    }
    if ((flags & 1U) != 0) {
      if (auto r = decode_mmr(f.rest(), *collective, nullptr); !r) {
        return r;
      }
    } else {
      GenericParams p;
      p.tmpl = (flags >> 1U) & 3U;
      p.at = {static_cast<std::int32_t>(-width), 0, -3, -1, 2, -2, -2, -2};
      Contexts contexts(generic_contexts(p.tmpl));
      ArithmeticDecoder decoder(f.rest());
      decode_generic(decoder, contexts, *collective, p);
    }
    PatternDictionary out;
    out.width = static_cast<std::int32_t>(width);
    out.height = static_cast<std::int32_t>(height);
    for (std::int64_t i = 0; i <= gray_max; ++i) {
      auto pattern = budget_.bitmap(width, height);
      if (!pattern) {
        return std::unexpected(std::move(pattern.error()));
      }
      pattern->compose(*collective, -i * width, 0, Combine::kReplace);
      out.patterns.push_back(std::move(*pattern));
    }
    results_[s.number] = std::move(out);
    return {};
  }

  Result<void> halftone_region(const Segment& s, Fields& f) {
    const RegionInfo info = region_info(f);
    const std::uint32_t flags = f.u8();
    const bool mmr = (flags & 1U) != 0;
    const unsigned tmpl = (flags >> 1U) & 3U;
    const bool skip_enabled = (flags & 8U) != 0;
    const Combine combine = combine_of((flags >> 4U) & 7U);
    const auto default_pixel = static_cast<std::uint8_t>((flags >> 7U) & 1U);
    const std::int64_t grid_w = f.u32();
    const std::int64_t grid_h = f.u32();
    const std::int64_t grid_x = f.i32();
    const std::int64_t grid_y = f.i32();
    const std::int64_t step_x = f.u16();
    const std::int64_t step_y = f.u16();
    const auto dictionaries = referred<PatternDictionary>(s);
    if (!f.ok() || dictionaries.empty() || dictionaries.front()->patterns.empty()) {
      return make_error(ErrorCode::kCorruptData, "JBIG2 halftone region without its patterns");
    }
    const PatternDictionary& patterns = *dictionaries.front();
    auto region = budget_.bitmap(info.width, info.height, default_pixel);
    if (!region) {
      return std::unexpected(std::move(region.error()));
    }
    // The skip bitmap: grid cells whose pattern falls entirely outside the region (6.6.5.1).
    auto skip = budget_.bitmap(grid_w, grid_h);
    if (!skip) {
      return std::unexpected(std::move(skip.error()));
    }
    const auto cell = [&](std::int64_t m, std::int64_t n) {
      return std::pair{(grid_x + (m * step_y) + (n * step_x)) >> 8, (grid_y + (m * step_x) - (n * step_y)) >> 8};
    };
    // The grid as allocated: an empty one is 0 x 0 whatever its declared other side.
    const std::int64_t rows = skip->height;
    const std::int64_t columns = skip->width;
    if (skip_enabled) {
      for (std::int64_t m = 0; m < rows; ++m) {
        for (std::int64_t n = 0; n < columns; ++n) {
          const auto [x, y] = cell(m, n);
          if (x + patterns.width <= 0 || x >= info.width || y + patterns.height <= 0 || y >= info.height) {
            skip->set(n, m, 1);
          }
        }
      }
    }
    // Annex C.5: the gray-scale values as bitplanes, most significant first, Gray coded.
    const unsigned planes = code_length_for(patterns.patterns.size());
    std::vector<Bitmap> plane;
    const std::span<const std::byte> data = f.rest();
    GenericParams gp;
    gp.tmpl = tmpl;
    gp.at = {tmpl <= 1 ? 3 : 2, -1, -3, -1, 2, -2, -2, -2};
    gp.skip = skip_enabled ? &*skip : nullptr;
    Contexts contexts(generic_contexts(tmpl));
    ArithmeticDecoder arithmetic(data);
    std::size_t offset = 0;
    for (unsigned j = 0; j < planes; ++j) {
      auto bitmap = budget_.bitmap(grid_w, grid_h);
      if (!bitmap) {
        return std::unexpected(std::move(bitmap.error()));
      }
      if (mmr) {
        std::size_t used = 0;
        if (auto r = decode_mmr(data.subspan(std::min(offset, data.size())), *bitmap, &used); !r) {
          return r;
        }
        offset += used;
      } else {
        decode_generic(arithmetic, contexts, *bitmap, gp);
      }
      plane.push_back(std::move(*bitmap));
    }
    // plane[0] is the most significant: undo the Gray code downwards.
    for (std::size_t j = 1; j < plane.size(); ++j) {
      for (std::size_t i = 0; i < plane[j].px.size(); ++i) {
        plane[j].px[i] = plane[j].px[i] ^ plane[j - 1].px[i];
      }
    }
    for (std::int64_t m = 0; m < rows; ++m) {
      for (std::int64_t n = 0; n < columns; ++n) {
        if (skip_enabled && skip->get(n, m) != 0) {
          continue;
        }
        std::size_t gray = 0;
        for (const Bitmap& b : plane) {
          gray = (gray << 1U) | b.get(n, m);
        }
        const auto [x, y] = cell(m, n);
        region->compose(patterns.patterns[std::min(gray, patterns.patterns.size() - 1)], x, y, combine);
      }
    }
    return place(s, std::move(*region), info, s.type == 20);
  }

  Budget budget_;
  std::map<std::uint32_t, Decoded> results_;
  std::optional<Bitmap> page_;
  std::uint32_t page_number_ = 0;
  std::uint8_t default_pixel_ = 0;
  Combine page_combine_ = Combine::kOr;
  bool combine_override_ = false;
  bool unknown_height_ = false;
};

}  // namespace

Result<std::vector<std::byte>> jbig2_decode(std::span<const std::byte> data, std::span<const std::byte> globals, std::size_t max_output) {
  Decoder decoder(max_output);
  // Damaged data: keep the page as far as it was drawn (the reference readers do), unless there is none.
  // A limit reached is reported, not hidden in a partial page.
  for (const std::span<const std::byte> part : {globals, data}) {
    if (auto read = decoder.read(part); !read && read.error().code == ErrorCode::kLimitExceeded) {
      return std::unexpected(std::move(read.error()));
    }
  }
  return decoder.output(max_output);
}

}  // namespace capriole::filters
