#pragma once

// Builds small PDF files for tests, with correct byte offsets: classic xref tables, cross-reference
// streams (optionally compressed), object streams and incremental updates.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <zlib.h>

namespace capriole::test {

// zlib-compresses bytes (FlateDecode data).
inline std::string zlib_compress(const std::string& data) {
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  std::string out(size, '\0');
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const int rc = compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()), 9);
  if (rc != Z_OK) {
    return {};
  }
  out.resize(size);
  return out;
}

// Big-endian field of width bytes.
inline std::string be(std::uint64_t value, int width) {
  std::string out(static_cast<std::size_t>(width), '\0');
  for (int i = width - 1; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = static_cast<char>(value & 0xFFU);
    value >>= 8U;
  }
  return out;
}

class PdfBuilder {
 public:
  explicit PdfBuilder(const std::string& header = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n") : data_(header) {}

  [[nodiscard]] std::uint64_t size() const { return data_.size(); }
  [[nodiscard]] const std::string& str() const { return data_; }

  void raw(const std::string& text) { data_ += text; }

  // "n g obj\n<body>\nendobj\n"; returns its offset and records it for the next xref section.
  std::uint64_t add_object(std::uint32_t number, const std::string& body, std::uint16_t generation = 0) {
    const std::uint64_t offset = data_.size();
    data_ += std::to_string(number) + " " + std::to_string(generation) + " obj\n" + body + "\nendobj\n";
    pending_[number] = {offset, generation};
    return offset;
  }

  // A stream object with the right /Length. extra_dict is inserted in the dictionary.
  std::uint64_t add_stream(std::uint32_t number, const std::string& data, const std::string& extra_dict = "") {
    return add_object(number, "<</Length " + std::to_string(data.size()) + extra_dict + ">>\nstream\n" + data + "\nendstream");
  }

  // Classic xref for the objects added since the last section, trailer, startxref, %%EOF.
  // Object 0 is written as the free-list head in the first section. Returns the xref offset.
  std::uint64_t finish_table(const std::string& trailer_extra, std::optional<std::uint64_t> prev = std::nullopt) {
    const std::uint64_t xref_offset = data_.size();
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> entries = pending_;
    data_ += "xref\n";
    if (first_section_) {
      data_ += "0 1\n0000000000 65535 f \n";
    }
    // One subsection per run of consecutive numbers.
    for (auto it = entries.begin(); it != entries.end();) {
      auto end = it;
      std::uint32_t expected = it->first;
      while (end != entries.end() && end->first == expected) {
        ++end;
        ++expected;
      }
      data_ += std::to_string(it->first) + " " + std::to_string(expected - it->first) + "\n";
      for (; it != end; ++it) {
        data_ += pad(it->second.first, 10) + " " + pad(it->second.second, 5) + " n \n";
      }
    }
    data_ += "trailer\n<<" + trailer_extra + (prev ? "/Prev " + std::to_string(*prev) : "") + ">>\n";
    finish(xref_offset);
    return xref_offset;
  }

  struct StreamEntry {
    std::uint32_t number;
    int type;  // 0 free, 1 in use, 2 compressed
    std::uint64_t field2;
    std::uint64_t field3;
  };

  // Cross-reference stream object `number` with W [1 4 2], covering the pending objects (type 1)
  // plus extra entries (e.g. compressed objects), then startxref. Returns its offset.
  std::uint64_t finish_xref_stream(std::uint32_t number, const std::string& dict_extra, const std::vector<StreamEntry>& extra = {}, bool compress = true,
                                   std::optional<std::uint64_t> prev = std::nullopt) {
    const std::uint64_t offset = write_xref_stream(number, dict_extra, extra, compress, prev, true);
    finish(offset);
    return offset;
  }

  // Writes only the cross-reference stream object (no startxref): the /XRefStm of a hybrid file.
  std::uint64_t write_xref_stream(std::uint32_t number, const std::string& dict_extra, const std::vector<StreamEntry>& extra, bool compress,
                                  std::optional<std::uint64_t> prev, bool include_pending) {
    const std::uint64_t offset = data_.size();
    std::map<std::uint32_t, StreamEntry> all;
    if (include_pending) {
      for (const auto& [n, e] : pending_) {
        all[n] = StreamEntry{n, 1, e.first, e.second};
      }
    }
    for (const StreamEntry& e : extra) {
      all[e.number] = e;
    }
    all[number] = StreamEntry{number, 1, offset, 0};
    std::string index;
    std::string rows;
    for (const auto& [n, e] : all) {
      index += std::to_string(n) + " 1 ";
      rows += be(static_cast<std::uint64_t>(e.type), 1) + be(e.field2, 4) + be(e.field3, 2);
    }
    const std::string data = compress ? zlib_compress(rows) : rows;
    data_ += std::to_string(number) + " 0 obj\n<</Type/XRef/W[1 4 2]/Index[" + index + "]/Length " + std::to_string(data.size()) +
             (compress ? "/Filter/FlateDecode" : "") + (prev ? "/Prev " + std::to_string(*prev) : "") + dict_extra + ">>\nstream\n" + data +
             "\nendstream\nendobj\n";
    return offset;
  }

 private:
  static std::string pad(std::uint64_t value, std::size_t width) {
    std::string digits = std::to_string(value);
    return std::string(width > digits.size() ? width - digits.size() : 0, '0') + digits;
  }

  void finish(std::uint64_t xref_offset) {
    data_ += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
    pending_.clear();
    first_section_ = false;
  }

  std::string data_;
  std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> pending_;
  bool first_section_ = true;
};

}  // namespace capriole::test
