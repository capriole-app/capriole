#pragma once

// FlateDecode (ISO 32000-2 §7.4.4): zlib-wrapped deflate data (RFC 1950/1951), and the PNG / TIFF
// predictors that may follow it (§7.4.4.4). The same predictors apply to LZWDecode.

#include <cstddef>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/model/object.hpp"

namespace capriole::filters {

// Inflates zlib data. Bytes after the end of the compressed stream are ignored (writers often count
// a trailing EOL in /Length). Fails with kCorruptData on invalid or truncated data and with
// kLimitExceeded when the output would exceed max_output bytes.
[[nodiscard]] Result<std::vector<std::byte>> flate_decode(std::span<const std::byte> input, std::size_t max_output);

struct PredictorParams {
  int predictor = 1;  // 1 none, 2 TIFF, 10..15 PNG
  int colors = 1;
  int bits_per_component = 8;
  int columns = 1;
};

// Reads /Predictor, /Colors, /BitsPerComponent, /Columns from a /DecodeParms dictionary (nullptr:
// all defaults) and validates them.
[[nodiscard]] Result<PredictorParams> predictor_params(const model::Dict* decode_parms);

// Undoes the predictor. PNG predictors carry a per-row filter byte, so the row type in the data wins
// over the specific value 10..15 (as §7.4.4.4 requires).
[[nodiscard]] Result<std::vector<std::byte>> apply_predictor(std::vector<std::byte> data, const PredictorParams& params);

}  // namespace capriole::filters
