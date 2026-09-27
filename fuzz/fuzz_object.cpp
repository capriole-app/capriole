// Fuzz target for the object parser. The seed corpus is fuzz/corpus/object/.
//
// The input is parsed in both modes — strict, and tolerant (the repair layer's) — each time twice: as
// a sequence of indirect objects (streams included, with an indirect
// /Length resolver that also moves the reader, as a real document would) and as a sequence of direct
// objects. Invariants checked beyond "no crash, no UB":
//   - parsing always terminates and every span lies inside the input;
//   - stream data lies inside the input and inside its object.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/parser/object_parser.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

void parse_indirect_objects(std::span<const std::byte> bytes, const capriole::ParseLimits& limits, capriole::parser::ParseMode mode) {
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  capriole::SourceReader reader(source);
  capriole::parser::ObjectParser parser(reader, limits, mode);
  const capriole::parser::LengthResolver resolver = [&reader](capriole::model::ObjectRef ref) -> capriole::Result<std::int64_t> {
    (void)reader.seek(0);  // move the shared reader: the parser must not depend on its position
    return static_cast<std::int64_t>(ref.number % 64);
  };

  std::uint64_t previous_end = 0;
  for (;;) {
    auto object = parser.parse_indirect_object(resolver);
    if (!object) {
      return;
    }
    check(object->offset >= previous_end && object->end_offset > object->offset && object->end_offset <= bytes.size());
    if (const auto* stream = object->object.as_stream()) {
      check(stream->data_offset <= bytes.size() && stream->data_length <= bytes.size() - stream->data_offset);
      // Strict: "endstream" follows the data inside the object. Tolerant: a stream without "endstream"
      // runs to the end of the input (a truncated file), so the data may end where the object does.
      const std::uint64_t data_end = stream->data_offset + stream->data_length;
      check(stream->data_offset > object->offset &&
            (mode == capriole::parser::ParseMode::kStrict ? data_end < object->end_offset : data_end <= object->end_offset));
    }
    previous_end = object->end_offset;
  }
}

void parse_direct_objects(std::span<const std::byte> bytes, const capriole::ParseLimits& limits, capriole::parser::ParseMode mode) {
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  capriole::SourceReader reader(source);
  capriole::parser::ObjectParser parser(reader, limits, mode);
  // Every object consumes at least one token and every token at least one byte, so the number of
  // objects is bounded by the input size. (The reader position is not a progress measure: an object
  // can be served entirely from tokens the parser has already looked ahead at.)
  for (std::size_t count = 0;; ++count) {
    check(count <= bytes.size());
    auto object = parser.parse_object();
    if (!object) {
      return;
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::ParseLimits limits;
  limits.max_nesting_depth = 32;  // small enough for the fuzzer to reach it
  for (const auto mode : {capriole::parser::ParseMode::kStrict, capriole::parser::ParseMode::kTolerant}) {
    parse_indirect_objects(bytes, limits, mode);
    parse_direct_objects(bytes, limits, mode);
  }
  return 0;
}
