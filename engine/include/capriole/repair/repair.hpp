#pragma once

// The repair layer (DOCS/CAPRIOLE.md §7.1): what the engine does when a file violates the standard
// in a way real files commonly do. It is kept apart from the strict parser on purpose: the parser
// reports the violation, Document::open decides whether a repair applies and records it, so the
// parser never grows a jungle of exceptions.
//
// Repairs are tried only after strict reading failed, in a fixed order, and each one applied is
// recorded in the document (Document::repairs()): the writer needs to know, and so does anyone
// investigating a file.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"

namespace capriole::repair {

enum class RepairKind : std::uint8_t {
  // Bytes precede %PDF- (e.g. HTML or a file path written by a web server) and the file's offsets
  // count from the header rather than from byte 0 (§7.5.2 puts the header at byte 0).
  kHeaderOffset,
  // No usable cross-reference data: the table was rebuilt by scanning the file (reconstruct.hpp).
  kXrefReconstructed,
  // An object that strict parsing rejects was read with the tolerant parser (duplicate keys, a
  // wrong /Length, a missing endobj, ...: see parser::ParseMode).
  kTolerantParsing,
  // An object the cross-reference table places wrongly was found elsewhere by scanning the file.
  kObjectRelocated,
  // An object the cross-reference table lists does not exist in the file: it reads as null.
  kMissingObject,
  // A page tree node without a usable /Type (absent or wrong): /Pages if it has /Kids, /Page otherwise.
  kPageTreeType,
  // Encoded data that is truncated or corrupt: the part that decodes is kept.
  kPartialDecode,
  // The page tree does not add up (walk fails, or finds fewer pages than the root's /Count): it is
  // rebuilt from /Kids plus the /Parent links of the nodes, which recovers pages whose parent lost
  // them from its /Kids.
  kPageTreeRebuilt,
  // The trailer's /Root is not a dictionary (a missing or renumbered object): the catalog is the
  // object with /Type /Catalog and /Pages (the highest-numbered, if several).
  kCatalogFound,
};

std::string_view to_string(RepairKind kind) noexcept;

struct RepairNote {
  RepairKind kind;
  std::string description;
};

// Whether a strict-reading failure is the kind a repair may fix: malformed structure, yes; a wrong
// password, an unsupported feature, an I/O failure or an exceeded resource limit, no.
[[nodiscard]] bool may_help(const Error& error) noexcept;

// Offset of a header preceded by junk (> 0), if the file has one.
[[nodiscard]] std::optional<std::uint64_t> junk_before_header(ByteSource& source);

}  // namespace capriole::repair
