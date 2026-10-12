#pragma once

// Cross-reference reconstruction: the repair of last resort for a file whose cross-reference data is
// missing, truncated or wrong (no startxref, malformed entries, offsets that point elsewhere).
//
// The whole file is scanned once for "n g obj" and "trailer". The last definition of each object
// wins, as later incremental updates do; trailers are merged in file order. When there is no
// trailer keyword (files with cross-reference streams), the dictionaries of cross-reference streams
// found by the scan provide /Root, /Info, /ID and /Encrypt; failing that, the last /Type /Catalog
// object becomes /Root. Object streams are opened to register the objects they hold — only when the
// file is not encrypted (their data cannot be decrypted before the security handler exists). For
// every object the definition that comes later in the file wins, direct or compressed (a compressed
// object is dated by the offset of its object stream), as it does with incremental updates.

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/parser/xref.hpp"

namespace capriole::repair {

[[nodiscard]] Result<parser::XrefTable> reconstruct_xref(ByteSource& source, const ParseLimits& limits = kDefaultParseLimits);

}  // namespace capriole::repair
