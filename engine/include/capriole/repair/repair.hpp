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
