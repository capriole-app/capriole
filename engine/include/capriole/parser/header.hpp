#pragma once

// The file header: "%PDF-<major>.<minor>" (ISO 32000-2 §7.5.2).

#include <cstdint>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"

namespace capriole::parser {

// How far from the start of the file the header is searched for. The standard puts it at offset 0,
// but real files often carry leading junk (mail headers, BOMs); every major reader tolerates up to 1 KB.
inline constexpr std::uint64_t kHeaderSearchWindow = 1024;

struct PdfHeader {
  int major = 0;
  int minor = 0;
  // Offset of the '%' of "%PDF-". Non-zero means leading junk: byte offsets in the xref may then be
  // relative to this position instead of to the start of the file, which the repair layer handles.
  std::uint64_t offset = 0;
};

// Locates and parses the header. Fails with kSyntax when no well-formed header is found in the window.
[[nodiscard]] Result<PdfHeader> find_header(ByteSource& source);

}  // namespace capriole::parser
