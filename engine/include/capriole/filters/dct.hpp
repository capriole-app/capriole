#pragma once

// DCTDecode (ISO 32000-2 §7.4.8): JPEG data, decoded with libjpeg-turbo (ADR 0003). The output is the
// samples as the image dictionary describes them: 8 bits, 1, 3 or 4 components interleaved, rows top to
// bottom — gray, RGB, or CMYK as stored (no conversion between colour spaces: that is the image
// dictionary's /ColorSpace and /Decode job).

#include <cstddef>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/model/object.hpp"

namespace capriole::filters {

// parms: the filter's /DecodeParms (nullptr when absent), for /ColorTransform. kLimitExceeded when the
// image would exceed max_output bytes; kCorruptData when libjpeg cannot decode it at all (truncated or
// damaged data that still decodes in part gives what decodes, as viewers show it).
[[nodiscard]] Result<std::vector<std::byte>> dct_decode(std::span<const std::byte> input, const model::Dict* parms, std::size_t max_output);

}  // namespace capriole::filters
