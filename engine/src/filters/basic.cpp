#include "capriole/filters/basic.hpp"

#include <cstdint>
#include <optional>

#include "capriole/parser/lexer.hpp"

namespace capriole::filters {

namespace {

std::unexpected<Error> corrupt(const char* what) { return make_error(ErrorCode::kCorruptData, what); }

// The end of decoding at a fault: the output so far when lenient, an error otherwise.
Result<std::vector<std::byte>> fault(std::vector<std::byte>& out, bool lenient, const char* what) {
  if (lenient) {
    return std::move(out);
  }
  return corrupt(what);
}
std::unexpected<Error> too_large() { return make_error(ErrorCode::kLimitExceeded, "decoded stream exceeds max_decoded_stream_bytes"); }

int hex_value(int c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}  // namespace

// ── ASCIIHexDecode (§7.4.2) ─────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>> ascii_hex_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient) {
  std::vector<std::byte> out;
  int high = -1;
  for (const std::byte b : input) {
    const int c = std::to_integer<int>(b);
    if (c == '>') {
      if (high >= 0) {
        if (out.size() >= max_output) {
          return too_large();
        }
        out.push_back(static_cast<std::byte>(high << 4));
      }
      return out;
    }
    if (parser::is_whitespace(c)) {
      continue;
    }
    const int value = hex_value(c);
    if (value < 0) {
      return fault(out, lenient, "invalid character in ASCIIHexDecode data");
    }
    if (high < 0) {
      high = value;
      continue;
    }
    if (out.size() >= max_output) {
      return too_large();
    }
    out.push_back(static_cast<std::byte>((high << 4) | value));
    high = -1;
  }
  return fault(out, lenient, "ASCIIHexDecode data has no '>' end marker");
}

// ── ASCII85Decode (§7.4.3) ──────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>> ascii85_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient) {
  constexpr std::uint64_t kMaxGroup = 0xFFFFFFFFU;
  std::vector<std::byte> out;
  std::uint64_t value = 0;
  int count = 0;

  const auto emit = [&](std::uint64_t group, int bytes) -> bool {
    if (out.size() + static_cast<std::size_t>(bytes) > max_output) {
      return false;
    }
    for (int i = 0; i < bytes; ++i) {
      out.push_back(static_cast<std::byte>((group >> (24U - (8U * static_cast<unsigned>(i)))) & 0xFFU));
    }
    return true;
  };

  for (std::size_t i = 0; i < input.size(); ++i) {
    const int c = std::to_integer<int>(input[i]);
    if (parser::is_whitespace(c)) {
      continue;
    }
    if (c == '~') {
      if (i + 1 >= input.size() || std::to_integer<int>(input[i + 1]) != '>') {
        return fault(out, lenient, "'~' in ASCII85Decode data not followed by '>'");
      }
      if (count == 1) {
        return fault(out, lenient, "ASCII85Decode data ends with a single-character group");
      }
      if (count > 0) {
        // Pad the partial group with 'u' (84) and keep count - 1 bytes.
        for (int pad = count; pad < 5; ++pad) {
          value = (value * 85) + 84;
        }
        if (value > kMaxGroup) {
          return fault(out, lenient, "ASCII85Decode group out of range");
        }
        if (!emit(value, count - 1)) {
          return too_large();
        }
      }
      return out;
    }
    if (c == 'z') {
      if (count != 0) {
        return fault(out, lenient, "'z' inside an ASCII85Decode group");
      }
      if (!emit(0, 4)) {
        return too_large();
      }
      continue;
    }
    if (c < '!' || c > 'u') {
      return fault(out, lenient, "invalid character in ASCII85Decode data");
    }
    value = (value * 85) + static_cast<std::uint64_t>(c - '!');
    if (++count == 5) {
      if (value > kMaxGroup) {
        return fault(out, lenient, "ASCII85Decode group out of range");
      }
      if (!emit(value, 4)) {
        return too_large();
      }
      value = 0;
      count = 0;
    }
  }
  return fault(out, lenient, "ASCII85Decode data has no '~>' end marker");
}

// ── LZWDecode (§7.4.4) ──────────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>> lzw_decode(std::span<const std::byte> input, bool early_change, std::size_t max_output, bool lenient) {
  constexpr unsigned kClear = 256;
  constexpr unsigned kEnd = 257;
  constexpr unsigned kFirstFree = 258;
  constexpr unsigned kTableSize = 4096;
  constexpr unsigned kMinWidth = 9;
  constexpr unsigned kMaxWidth = 12;

  // Each entry is its prefix code plus one byte; strings are rebuilt by walking the prefixes back.
  struct Entry {
    std::uint16_t prefix = 0;
    std::byte last{};
    std::byte first{};
    std::uint16_t length = 0;
  };
  std::vector<Entry> table(kTableSize);
  for (unsigned i = 0; i < 256; ++i) {
    table[i] = Entry{.prefix = 0, .last = static_cast<std::byte>(i), .first = static_cast<std::byte>(i), .length = 1};
  }

  std::size_t bit_pos = 0;
  const auto read_code = [&](unsigned width) -> std::optional<unsigned> {
    if (bit_pos + width > input.size() * 8) {
      return std::nullopt;
    }
    unsigned code = 0;
    for (unsigned i = 0; i < width; ++i, ++bit_pos) {
      const auto byte = std::to_integer<unsigned>(input[bit_pos / 8]);
      code = (code << 1U) | ((byte >> (7U - (bit_pos % 8))) & 1U);
    }
    return code;
  };

  std::vector<std::byte> out;
  const auto emit = [&](unsigned code) -> bool {
    const std::size_t length = table[code].length;
    if (out.size() + length > max_output) {
      return false;
    }
    out.resize(out.size() + length);
    std::size_t pos = out.size();
    for (unsigned c = code;; c = table[c].prefix) {
      out[--pos] = table[c].last;
      if (table[c].length == 1) {
        break;
      }
    }
    return true;
  };

  unsigned width = kMinWidth;
  unsigned next = kFirstFree;
  std::optional<unsigned> previous;
  const unsigned early = early_change ? 1 : 0;
  for (;;) {
    const auto code = read_code(width);
    if (!code) {
      return fault(out, lenient, "LZWDecode data has no end-of-data code");
    }
    if (*code == kClear) {
      width = kMinWidth;
      next = kFirstFree;
      previous.reset();
      continue;
    }
    if (*code == kEnd) {
      return out;
    }
    if (!previous) {
      if (*code > 255) {
        return fault(out, lenient, "LZWDecode data starts with an undefined code");
      }
      if (!emit(*code)) {
        return too_large();
      }
      previous = code;
      continue;
    }

    if (*code > next || (*code == next && next >= kTableSize)) {
      return fault(out, lenient, "undefined code in LZWDecode data");
    }
    if (next < kTableSize) {
      // The new entry is previous + the first byte of the current string; when the code is the one
      // being defined (the "KwKwK" case) that first byte is previous's own first byte.
      const std::byte first = *code == next ? table[*previous].first : table[*code].first;
      table[next] = Entry{.prefix = static_cast<std::uint16_t>(*previous),
                          .last = first,
                          .first = table[*previous].first,
                          .length = static_cast<std::uint16_t>(table[*previous].length + 1)};
      ++next;
    }
    if (!emit(*code)) {
      return too_large();
    }
    previous = code;
    if (next + early >= (1U << width) && width < kMaxWidth) {
      ++width;
    }
  }
}

// ── RunLengthDecode (§7.4.5) ────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>> run_length_decode(std::span<const std::byte> input, std::size_t max_output, bool lenient) {
  std::vector<std::byte> out;
  std::size_t i = 0;
  while (i < input.size()) {
    const auto length = std::to_integer<unsigned>(input[i++]);
    if (length == 128) {
      return out;
    }
    if (length < 128) {
      const std::size_t count = length + 1;
      if (count > input.size() - i) {
        return fault(out, lenient, "RunLengthDecode literal run past the end of the data");
      }
      if (out.size() + count > max_output) {
        return too_large();
      }
      out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(i), input.begin() + static_cast<std::ptrdiff_t>(i + count));
      i += count;
    } else {
      if (i >= input.size()) {
        return fault(out, lenient, "RunLengthDecode repeat run past the end of the data");
      }
      const std::size_t count = 257 - length;
      if (out.size() + count > max_output) {
        return too_large();
      }
      out.insert(out.end(), count, input[i++]);
    }
  }
  return fault(out, lenient, "RunLengthDecode data has no end-of-data byte");
}

}  // namespace capriole::filters
