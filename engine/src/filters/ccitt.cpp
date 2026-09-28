#include "capriole/filters/ccitt.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace capriole::filters {

namespace {

// ── Code tables (ITU-T T.4, Tables 2 and 3; T.6 Table 1) ───────────────────────────────────────

struct Code {
  std::string_view bits;
  std::int32_t value;
  constexpr Code(std::string_view code, std::int32_t v) noexcept : bits(code), value(v) {}
};

// Special values in the run tables.
constexpr std::int32_t kEol = -2;
constexpr std::int32_t kNone = -1;

constexpr std::array<Code, 91> kWhiteCodes = {{
    {"00110101", 0},     {"000111", 1},       {"0111", 2},         {"1000", 3},         {"1011", 4},         {"1100", 5},         {"1110", 6},
    {"1111", 7},         {"10011", 8},        {"10100", 9},        {"00111", 10},       {"01000", 11},       {"001000", 12},      {"000011", 13},
    {"110100", 14},      {"110101", 15},      {"101010", 16},      {"101011", 17},      {"0100111", 18},     {"0001100", 19},     {"0001000", 20},
    {"0010111", 21},     {"0000011", 22},     {"0000100", 23},     {"0101000", 24},     {"0101011", 25},     {"0010011", 26},     {"0100100", 27},
    {"0011000", 28},     {"00000010", 29},    {"00000011", 30},    {"00011010", 31},    {"00011011", 32},    {"00010010", 33},    {"00010011", 34},
    {"00010100", 35},    {"00010101", 36},    {"00010110", 37},    {"00010111", 38},    {"00101000", 39},    {"00101001", 40},    {"00101010", 41},
    {"00101011", 42},    {"00101100", 43},    {"00101101", 44},    {"00000100", 45},    {"00000101", 46},    {"00001010", 47},    {"00001011", 48},
    {"01010010", 49},    {"01010011", 50},    {"01010100", 51},    {"01010101", 52},    {"00100100", 53},    {"00100101", 54},    {"01011000", 55},
    {"01011001", 56},    {"01011010", 57},    {"01011011", 58},    {"01001010", 59},    {"01001011", 60},    {"00110010", 61},    {"00110011", 62},
    {"00110100", 63},    {"11011", 64},       {"10010", 128},      {"010111", 192},     {"0110111", 256},    {"00110110", 320},   {"00110111", 384},
    {"01100100", 448},   {"01100101", 512},   {"01101000", 576},   {"01100111", 640},   {"011001100", 704},  {"011001101", 768},  {"011010010", 832},
    {"011010011", 896},  {"011010100", 960},  {"011010101", 1024}, {"011010110", 1088}, {"011010111", 1152}, {"011011000", 1216}, {"011011001", 1280},
    {"011011010", 1344}, {"011011011", 1408}, {"010011000", 1472}, {"010011001", 1536}, {"010011010", 1600}, {"011000", 1664},    {"010011011", 1728},
}};

constexpr std::array<Code, 91> kBlackCodes = {{
    {"0000110111", 0},
    {"010", 1},
    {"11", 2},
    {"10", 3},
    {"011", 4},
    {"0011", 5},
    {"0010", 6},
    {"00011", 7},
    {"000101", 8},
    {"000100", 9},
    {"0000100", 10},
    {"0000101", 11},
    {"0000111", 12},
    {"00000100", 13},
    {"00000111", 14},
    {"000011000", 15},
    {"0000010111", 16},
    {"0000011000", 17},
    {"0000001000", 18},
    {"00001100111", 19},
    {"00001101000", 20},
    {"00001101100", 21},
    {"00000110111", 22},
    {"00000101000", 23},
    {"00000010111", 24},
    {"00000011000", 25},
    {"000011001010", 26},
    {"000011001011", 27},
    {"000011001100", 28},
    {"000011001101", 29},
    {"000001101000", 30},
    {"000001101001", 31},
    {"000001101010", 32},
    {"000001101011", 33},
    {"000011010010", 34},
    {"000011010011", 35},
    {"000011010100", 36},
    {"000011010101", 37},
    {"000011010110", 38},
    {"000011010111", 39},
    {"000001101100", 40},
    {"000001101101", 41},
    {"000011011010", 42},
    {"000011011011", 43},
    {"000001010100", 44},
    {"000001010101", 45},
    {"000001010110", 46},
    {"000001010111", 47},
    {"000001100100", 48},
    {"000001100101", 49},
    {"000001010010", 50},
    {"000001010011", 51},
    {"000000100100", 52},
    {"000000110111", 53},
    {"000000111000", 54},
    {"000000100111", 55},
    {"000000101000", 56},
    {"000001011000", 57},
    {"000001011001", 58},
    {"000000101011", 59},
    {"000000101100", 60},
    {"000001011010", 61},
    {"000001100110", 62},
    {"000001100111", 63},
    {"0000001111", 64},
    {"000011001000", 128},
    {"000011001001", 192},
    {"000001011011", 256},
    {"000000110011", 320},
    {"000000110100", 384},
    {"000000110101", 448},
    {"0000001101100", 512},
    {"0000001101101", 576},
    {"0000001001010", 640},
    {"0000001001011", 704},
    {"0000001001100", 768},
    {"0000001001101", 832},
    {"0000001110010", 896},
    {"0000001110011", 960},
    {"0000001110100", 1024},
    {"0000001110101", 1088},
    {"0000001110110", 1152},
    {"0000001110111", 1216},
    {"0000001010010", 1280},
    {"0000001010011", 1344},
    {"0000001010100", 1408},
    {"0000001010101", 1472},
    {"0000001011010", 1536},
    {"0000001011011", 1600},
    {"0000001100100", 1664},
    {"0000001100101", 1728},
}};

// Extended make-up codes, the same for both colours (T.4 Table 3/T.4).
constexpr std::array<Code, 13> kExtendedCodes = {{
    {"00000001000", 1792},
    {"00000001100", 1856},
    {"00000001101", 1920},
    {"000000010010", 1984},
    {"000000010011", 2048},
    {"000000010100", 2112},
    {"000000010101", 2176},
    {"000000010110", 2240},
    {"000000010111", 2304},
    {"000000011100", 2368},
    {"000000011101", 2432},
    {"000000011110", 2496},
    {"000000011111", 2560},
}};

// Two-dimensional mode codes (T.4 Table 4).
enum Mode : std::uint8_t { kPass, kHorizontal, kV0, kVR1, kVR2, kVR3, kVL1, kVL2, kVL3, kExtension };
constexpr std::array<Code, 10> kModeCodes = {{
    {"0001", kPass},
    {"001", kHorizontal},
    {"1", kV0},
    {"011", kVR1},
    {"000011", kVR2},
    {"0000011", kVR3},
    {"010", kVL1},
    {"000010", kVL2},
    {"0000010", kVL3},
    {"0000001", kExtension},
}};

constexpr std::size_t kMaxCodeLength = 13;

// Codes by length: table[length][code] = value (kNone when no code of that length has those bits).
class CodeTable {
 public:
  template <std::size_t... N>
  explicit CodeTable(const std::array<Code, N>&... groups) {
    for (std::size_t length = 1; length <= kMaxCodeLength; ++length) {
      by_length_.at(length).assign(std::size_t{1} << length, kNone);
    }
    (add(groups), ...);
    by_length_.at(12).at(1) = kEol;  // 000000000001
  }

  [[nodiscard]] std::int32_t find(std::size_t length, std::uint32_t code) const { return by_length_.at(length).at(code); }

 private:
  template <std::size_t N>
  void add(const std::array<Code, N>& codes) {
    for (const Code& c : codes) {
      std::uint32_t value = 0;
      for (const char bit : c.bits) {
        value = (value << 1U) | (bit == '1' ? 1U : 0U);
      }
      by_length_.at(c.bits.size()).at(value) = c.value;
    }
  }

  std::array<std::vector<std::int32_t>, kMaxCodeLength + 1> by_length_;
};

const CodeTable& white_table() {
  static const CodeTable kTable(kWhiteCodes, kExtendedCodes);
  return kTable;
}
const CodeTable& black_table() {
  static const CodeTable kTable(kBlackCodes, kExtendedCodes);
  return kTable;
}
const CodeTable& mode_table() {
  static const CodeTable kTable(kModeCodes);
  return kTable;
}

// ── Decoding ───────────────────────────────────────────────────────────────────────────────────

class BitReader {
 public:
  explicit BitReader(std::span<const std::byte> data) : data_(data) {}

  // The next bit; past the end, 0 (and at_end() says so).
  unsigned bit() {
    if (at_ >= data_.size() * 8) {
      return 0;
    }
    const unsigned b = (std::to_integer<unsigned>(data_[at_ / 8]) >> (7 - (at_ % 8))) & 1U;
    ++at_;
    return b;
  }

  [[nodiscard]] bool at_end() const { return at_ >= data_.size() * 8; }
  [[nodiscard]] std::size_t position() const { return at_; }
  void seek(std::size_t position) { at_ = position; }
  void align() { at_ = (at_ + 7) / 8 * 8; }

 private:
  std::span<const std::byte> data_;
  std::size_t at_ = 0;
};

// One code of a table: its value, kEol, or kNone for bits that are no code.
std::int32_t read_code(BitReader& in, const CodeTable& table) {
  std::uint32_t code = 0;
  for (std::size_t length = 1; length <= kMaxCodeLength; ++length) {
    code = (code << 1U) | in.bit();
    const std::int32_t value = table.find(length, code);
    if (value != kNone) {
      return value;
    }
  }
  return kNone;
}

// A run: make-up codes, then a terminating code (< 64). kNone on an invalid code, kEol on an EOL.
std::int32_t read_run(BitReader& in, bool white) {
  const CodeTable& table = white ? white_table() : black_table();
  std::int32_t total = 0;
  for (;;) {
    const std::int32_t value = read_code(in, table);
    if (value < 0) {
      return value;
    }
    total += value;
    if (value < 64) {
      return total;
    }
    if (total > (1 << 24)) {
      return kNone;  // a run longer than any row: garbage
    }
  }
}

// Consumes an EOL (fill zeros, then 000000000001) if one comes next.
bool skip_eol(BitReader& in) {
  const std::size_t start = in.position();
  std::size_t zeros = 0;
  constexpr std::size_t kMaxFill = 64;
  while (!in.at_end() && zeros < 11 + kMaxFill) {
    if (in.bit() == 1) {
      if (zeros >= 11) {
        return true;
      }
      break;
    }
    ++zeros;
  }
  in.seek(start);
  return false;
}

class Decoder {
 public:
  Decoder(std::span<const std::byte> input, const CcittParams& p) : in_(input), p_(p), columns_(static_cast<std::int32_t>(p.columns)) {
    reference_ = {columns_, columns_};
  }

  // Decodes rows into out (1 bit per pixel, rows byte-aligned) until the data, the rows or the limit end.
  Result<void> run(std::vector<std::byte>& out, std::size_t max_output) {
    const std::size_t row_bytes = (static_cast<std::size_t>(columns_) + 7) / 8;
    std::int64_t rows = 0;
    bool two_d = p_.k < 0;
    while (p_.rows == 0 || rows < p_.rows) {
      if (p_.encoded_byte_align && p_.k < 0) {
        in_.align();
      }
      // EOLs: required before Group 3 rows when /EndOfLine, tolerated everywhere. Two in a row end the
      // block (EOFB for Group 4, RTC for Group 3).
      if (p_.k >= 0) {
        if (p_.encoded_byte_align && !p_.end_of_line) {
          in_.align();
        }
        const bool eol = skip_eol(in_);
        if (eol && skip_eol(in_)) {
          break;
        }
        if (p_.k > 0) {
          two_d = in_.bit() == 0;  // the tag bit: 1 for a one-dimensional row
        }
      }
      if (in_.at_end()) {
        break;
      }
      const std::size_t start = in_.position();
      const Status status = two_d ? row_2d() : row_1d();
      if (status == Status::kEnd) {
        break;
      }
      if (status == Status::kError) {
        in_.seek(start);
        break;  // keep what decoded (the reference readers do the same)
      }
      if (out.size() + row_bytes > max_output) {
        return make_error(ErrorCode::kLimitExceeded, "CCITTFaxDecode output exceeds the limit");
      }
      emit(out, row_bytes);
      reference_ = current_;
      reference_.push_back(columns_);
      reference_.push_back(columns_);
      ++rows;
    }
    // A known height: the rows the data did not give read as white.
    const std::uint8_t white = p_.black_is_1 ? 0x00 : 0xFF;
    while (p_.rows > 0 && rows < p_.rows) {
      if (out.size() + row_bytes > max_output) {
        return make_error(ErrorCode::kLimitExceeded, "CCITTFaxDecode output exceeds the limit");
      }
      out.insert(out.end(), row_bytes, static_cast<std::byte>(white));
      ++rows;
    }
    return {};
  }

 private:
  enum class Status : std::uint8_t { kRow, kEnd, kError };

  // Adds a changing element (the colour changes at x), keeping the list well-formed.
  void change(std::int32_t x) {
    x = std::clamp(x, 0, columns_);
    if (!current_.empty() && x < current_.back()) {
      x = current_.back();
    }
    current_.push_back(x);
  }

  Status row_1d() {
    current_.clear();
    std::int32_t a0 = 0;
    bool white = true;
    while (a0 < columns_) {
      const std::int32_t run = read_run(in_, white);
      if (run == kEol) {
        return current_.empty() && a0 == 0 ? Status::kEnd : Status::kError;
      }
      if (run < 0) {
        return Status::kError;
      }
      a0 = std::min(columns_, a0 + run);
      change(a0);
      white = !white;
    }
    return Status::kRow;
  }

  Status row_2d() {
    current_.clear();
    std::int32_t a0 = -1;
    bool white = true;
    std::size_t j = 0;  // the reference changes before j are behind a0
    while (a0 < columns_) {
      // b1: the first change on the reference line after a0 that goes to the colour opposite to a0's
      // (even indices go white → black), b2 the change after it.
      while (j < reference_.size() && (reference_[j] <= a0 || ((j % 2 == 0) != white))) {
        ++j;
      }
      const std::int32_t b1 = j < reference_.size() ? reference_[j] : columns_;
      const std::int32_t b2 = j + 1 < reference_.size() ? reference_[j + 1] : columns_;
      const std::int32_t mode = read_code(in_, mode_table());
      const std::int32_t start = std::max(a0, 0);
      switch (mode) {
        case kPass:
          a0 = b2;
          break;
        case kHorizontal: {
          const std::int32_t first = read_run(in_, white);
          const std::int32_t second = first >= 0 ? read_run(in_, !white) : kNone;
          if (first < 0 || second < 0) {
            return Status::kError;
          }
          const std::int32_t a1 = std::min(columns_, start + first);
          change(a1);
          const std::int32_t a2 = std::min(columns_, a1 + second);
          change(a2);
          a0 = a2;
          break;
        }
        case kV0:
        case kVR1:
        case kVR2:
        case kVR3:
        case kVL1:
        case kVL2:
        case kVL3: {
          constexpr std::array<std::int32_t, 7> kOffsets = {0, 1, 2, 3, -1, -2, -3};
          const std::int32_t a1 = b1 + kOffsets.at(static_cast<std::size_t>(mode - kV0));
          if (a1 < start) {
            return Status::kError;  // a change behind a0: damaged data
          }
          change(a1);
          a0 = a1;
          white = !white;
          if (j > 0) {
            --j;  // b1 of the next step may be the change just before this one
          }
          break;
        }
        case kEol:
          return current_.empty() && a0 < 0 ? Status::kEnd : Status::kError;
        default:  // an extension (uncompressed mode) or bits that are no code
          return Status::kError;
      }
    }
    return Status::kRow;
  }

  // The current row as bits: white is 1 unless /BlackIs1.
  void emit(std::vector<std::byte>& out, std::size_t row_bytes) const {
    const std::size_t at = out.size();
    out.resize(at + row_bytes, static_cast<std::byte>(p_.black_is_1 ? 0x00 : 0xFF));
    std::int32_t from = 0;
    bool white = true;
    for (std::size_t i = 0; i <= current_.size(); ++i) {
      const std::int32_t to = i < current_.size() ? current_[i] : columns_;
      if (!white) {
        for (std::int32_t x = from; x < to; ++x) {
          auto& byte = out[at + (static_cast<std::size_t>(x) / 8)];
          const auto mask = static_cast<std::byte>(0x80U >> (static_cast<unsigned>(x) % 8));
          byte = p_.black_is_1 ? (byte | mask) : (byte & ~mask);
        }
      }
      from = to;
      white = !white;
    }
  }

  BitReader in_;
  CcittParams p_;
  std::int32_t columns_;
  std::vector<std::int32_t> reference_;  // changing elements of the previous row, then two sentinels
  std::vector<std::int32_t> current_;
};

}  // namespace

Result<CcittParams> ccitt_params(const model::Dict* decode_parms) {
  CcittParams p;
  if (decode_parms == nullptr) {
    return p;
  }
  const auto integer = [&](std::string_view key, std::int64_t fallback) {
    const model::Object* value = decode_parms->find(key);
    return value != nullptr ? value->as_integer().value_or(fallback) : fallback;
  };
  const auto flag = [&](std::string_view key, bool fallback) {
    const model::Object* value = decode_parms->find(key);
    return value != nullptr ? value->as_bool().value_or(fallback) : fallback;
  };
  p.k = integer("K", 0);
  p.end_of_line = flag("EndOfLine", false);
  p.encoded_byte_align = flag("EncodedByteAlign", false);
  p.columns = integer("Columns", 1728);
  p.rows = integer("Rows", 0);
  p.end_of_block = flag("EndOfBlock", true);
  p.black_is_1 = flag("BlackIs1", false);
  constexpr std::int64_t kMaxColumns = std::int64_t{1} << 20;
  if (p.columns < 1 || p.columns > kMaxColumns || p.rows < 0 || p.rows > (std::int64_t{1} << 30)) {
    return make_error(ErrorCode::kSyntax, "CCITTFaxDecode with invalid /Columns or /Rows");
  }
  return p;
}

Result<std::vector<std::byte>> ccitt_decode(std::span<const std::byte> input, const CcittParams& params, std::size_t max_output) {
  std::vector<std::byte> out;
  Decoder decoder(input, params);
  if (auto done = decoder.run(out, max_output); !done) {
    return std::unexpected(std::move(done.error()));
  }
  return out;
}

}  // namespace capriole::filters
