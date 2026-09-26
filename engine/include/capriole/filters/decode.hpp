#pragma once

// Stream decoding: reading a stream's encoded bytes and running its /Filter chain (§7.4).

#include <cstddef>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/model/object.hpp"

namespace capriole::filters {

// The stream's encoded data, exactly as stored in the file.
[[nodiscard]] Result<std::vector<std::byte>> read_encoded(ByteSource& source, const model::Stream& stream);

// Runs the /Filter chain of stream_dict (with /DecodeParms) over encoded. /Filter and /DecodeParms
// must be direct objects: the caller resolves indirect references first. Filters not implemented yet
// are ErrorCode::kUnsupported, naming the filter.
[[nodiscard]] Result<std::vector<std::byte>> decode(std::vector<std::byte> encoded, const model::Dict& stream_dict, const ParseLimits& limits);

}  // namespace capriole::filters
