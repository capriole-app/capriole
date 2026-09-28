#pragma once

// CCITTFaxDecode (ISO 32000-2 §7.4.6): fax-encoded bilevel images, ITU-T T.4 (Group 3, one- and
// two-dimensional) and T.6 (Group 4). Our own decoder: no library with a compatible licence does
// only this.
//
// The output is the image's rows, 1 bit per pixel, each row padded to a whole byte, 0 for black
// unless /BlackIs1 — the samples a /DeviceGray 1-bit image (or an image mask) reads.
//
// Fax data is often damaged: like the reference readers, decoding stops at the first invalid code and
// keeps the rows decoded so far (the missing rows of a known height read as white).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/model/object.hpp"

namespace capriole::filters {

struct CcittParams {
  std::int64_t k = 0;        // < 0: Group 4; 0: Group 3 1-D; > 0: Group 3 mixed 1-D / 2-D
  bool end_of_line = false;  // EOL codes are required before each row
  bool encoded_byte_align = false;
  std::int64_t columns = 1728;
  std::int64_t rows = 0;     // 0: as many as the data holds
  bool end_of_block = true;  // the data ends with EOFB (Group 4) or RTC (Group 3)
  bool black_is_1 = false;
};

// The parameters of a /DecodeParms dictionary (nullptr: all defaults), validated.
[[nodiscard]] Result<CcittParams> ccitt_params(const model::Dict* decode_parms);

// kLimitExceeded when the image would exceed max_output bytes.
[[nodiscard]] Result<std::vector<std::byte>> ccitt_decode(std::span<const std::byte> input, const CcittParams& params, std::size_t max_output);

}  // namespace capriole::filters
