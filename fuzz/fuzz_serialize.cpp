// Fuzz target for object serialisation. The seed corpus is fuzz/corpus/serialize/.
//
// Every object the parser accepts from the input must survive a round trip: serialize, parse
// again, get an equal object; serialising that object again gives the same text.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/parser/object_parser.hpp"
#include "capriole/writer/serializer.hpp"

namespace {

capriole::Result<capriole::model::Object> parse(const std::string& text) {
  auto source = capriole::MemoryByteSource::from_string(text);
  capriole::SourceReader reader(source);
  capriole::parser::ObjectParser parser(reader);
  return parser.parse_object();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  capriole::MemoryByteSource source(std::vector<std::byte>(bytes.begin(), bytes.end()));
  capriole::SourceReader reader(source);
  capriole::parser::ObjectParser parser(reader);
  for (std::size_t count = 0; count <= size; ++count) {
    auto object = parser.parse_object();
    if (!object) {
      return 0;
    }
    const std::string text = capriole::writer::serialize(*object);
    auto again = parse(text);
    if (!again || *again != *object || capriole::writer::serialize(*again) != text) {
      __builtin_trap();
    }
  }
  return 0;
}
