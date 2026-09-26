#pragma once

// File structure (ISO 32000-2 §7.5): startxref, cross-reference sections and trailers.
//
// A file is a chain of revisions: the last xref section (found through startxref) points to the
// previous one through /Prev, and so on back to the original file. Sections come in three forms —
// the classic table (§7.5.4), the cross-reference stream (§7.5.8) and the hybrid of the two (a table
// whose trailer has /XRefStm, §7.5.8.4). The newest definition of each object wins.
//
// The section list is kept, not just the merged result: incremental saving must append a section of
// the same form, and signature validation needs to know which revision defined what.

#include <cstdint>
#include <map>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/model/object.hpp"

namespace capriole::parser {

enum class XrefEntryType : std::uint8_t {
  kFree,        // free (or type >= 3 in a stream: a reference to null)
  kInUse,       // at a byte offset in the file
  kCompressed,  // inside an object stream
};

struct XrefEntry {
  XrefEntryType type = XrefEntryType::kFree;
  std::uint16_t generation = 0;     // kFree, kInUse
  std::uint64_t offset = 0;         // kInUse: offset of "n g obj"
  std::uint32_t stream_number = 0;  // kCompressed: the object stream that holds the object
  std::uint32_t stream_index = 0;   // kCompressed: index of the object within that stream
  friend bool operator==(const XrefEntry&, const XrefEntry&) = default;
};

enum class XrefForm : std::uint8_t { kTable, kStream, kHybrid };

struct XrefSection {
  std::uint64_t offset = 0;  // where the section starts ("xref" keyword or "n g obj" of the stream)
  XrefForm form = XrefForm::kTable;
  std::size_t entry_count = 0;
};

struct XrefTable {
  std::uint64_t startxref = 0;
  // The newest trailer (for a cross-reference stream: the stream dictionary).
  model::Dict trailer;
  // Newest first.
  std::vector<XrefSection> sections;
  // Merged view: for each object number, the newest entry.
  std::map<std::uint32_t, XrefEntry> entries;

  [[nodiscard]] const XrefEntry* find(std::uint32_t number) const;
};

// Offset stored after the last "startxref" in the final 1024 bytes, which must be followed by %%EOF.
[[nodiscard]] Result<std::uint64_t> find_startxref(ByteSource& source);

// Reads every cross-reference section from startxref back through /Prev (and /XRefStm).
[[nodiscard]] Result<XrefTable> read_xref(ByteSource& source, const ParseLimits& limits = kDefaultParseLimits);

}  // namespace capriole::parser
