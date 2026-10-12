#include "capriole/writer/serializer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "capriole/core/source_reader.hpp"
#include "capriole/parser/object_parser.hpp"
#include "capriole/writer/byte_sink.hpp"

namespace capriole::writer {
namespace {

using model::Array;
using model::Dict;
using model::Name;
using model::Object;
using model::ObjectRef;
using model::String;
using model::StringForm;

Result<Object> parse(const std::string& text) {
  auto source = MemoryByteSource::from_string(text);
  SourceReader reader(source);
  parser::ObjectParser parser(reader);
  return parser.parse_object();
}

// The contract: what serialize() writes parses back to an equal object, and writing that again
// gives the same text.
void expect_round_trip(const Object& object) {
  const std::string text = serialize(object);
  const auto parsed = parse(text);
  ASSERT_TRUE(parsed.has_value()) << text << ": " << parsed.error().message;
  EXPECT_EQ(*parsed, object) << text;
  EXPECT_EQ(serialize(*parsed), text);
}

// ── Exact output ────────────────────────────────────────────────────────────────────────────

TEST(Serializer, Scalars) {
  EXPECT_EQ(serialize(Object(model::Null{})), "null");
  EXPECT_EQ(serialize(Object(true)), "true");
  EXPECT_EQ(serialize(Object(false)), "false");
  EXPECT_EQ(serialize(Object(-42)), "-42");
  EXPECT_EQ(serialize(Object(ObjectRef{.number = 12, .generation = 3})), "12 3 R");
}

TEST(Serializer, Reals) {
  EXPECT_EQ(format_real(3.25), "3.25");
  EXPECT_EQ(format_real(-0.002), "-0.002");
  EXPECT_EQ(format_real(4.0), "4.0");         // a decimal point, so that it reads back as a real
  EXPECT_EQ(format_real(1e-7), "0.0000001");  // never exponent notation
  EXPECT_EQ(format_real(1e20), "100000000000000000000.0");
  EXPECT_EQ(format_real(0.1), "0.1");  // shortest digits that round-trip
  EXPECT_EQ(format_real(std::numeric_limits<double>::infinity()), "0.0");
  EXPECT_EQ(format_real(std::nan("")), "0.0");
}

TEST(Serializer, Strings) {
  EXPECT_EQ(serialize(Object(String{.bytes = "plain"})), "(plain)");
  EXPECT_EQ(serialize(Object(String{.bytes = "a(b)c\\d"})), "(a\\(b\\)c\\\\d)");
  EXPECT_EQ(serialize(Object(String{.bytes = "cr\rlf\n"})), "(cr\\rlf\n)");
  EXPECT_EQ(serialize(Object(String{.bytes = "\x01\xFF", .form = StringForm::kHex})), "<01FF>");
}

TEST(Serializer, Names) {
  EXPECT_EQ(serialize(Object(Name{"Type"})), "/Type");
  EXPECT_EQ(serialize(Object(Name{"Lime Green"})), "/Lime#20Green");
  EXPECT_EQ(serialize(Object(Name{"a#b/c(d)"})), "/a#23b#2Fc#28d#29");
  EXPECT_EQ(serialize(Object(Name{"\xE9t\xE9"})), "/#E9t#E9");
  EXPECT_EQ(serialize(Object(Name{""})), "/");
}

TEST(Serializer, Containers) {
  Dict dict;
  dict.set("Type", Name{"Page"});
  dict.set("Kids", Array{ObjectRef{.number = 4, .generation = 0}, 5, 2.5});
  dict.set("Empty", Dict{});
  EXPECT_EQ(serialize(Object(dict)), "<</Type /Page /Kids [4 0 R 5 2.5] /Empty <<>>>>");
  EXPECT_EQ(serialize(Object(Array{})), "[]");
}

TEST(Serializer, StreamsWriteTheirDictionaryOnly) {
  model::Stream stream;
  stream.dict.set("Length", 5);
  EXPECT_EQ(serialize(Object(stream)), "<</Length 5>>");
}

// ── Round trips ─────────────────────────────────────────────────────────────────────────────

TEST(Serializer, RoundTripsBuiltObjects) {
  std::string all_bytes;
  for (int i = 0; i < 256; ++i) {
    all_bytes.push_back(static_cast<char>(i));
  }
  std::string all_name_bytes;
  for (int i = 1; i < 256; ++i) {  // a name cannot hold byte 0 (§7.3.5)
    all_name_bytes.push_back(static_cast<char>(i));
  }
  Dict nested;
  nested.set("Z", Array{Array{Array{}}, Dict{}, model::Null{}});
  nested.set("A", String{.bytes = all_bytes, .form = StringForm::kHex});
  const std::vector<Object> objects = {
      Object(0),
      Object(std::numeric_limits<std::int64_t>::min()),
      Object(std::numeric_limits<std::int64_t>::max()),
      Object(0.1),
      Object(-123456.789),
      Object(1e-300),
      Object(5e307),
      Object(String{.bytes = all_bytes}),
      Object(String{.bytes = "((unbalanced"}),
      Object(String{.bytes = "\r\n\r"}),
      Object(Name{all_name_bytes}),
      Object(Array{1, Name{"x"}, String{.bytes = "s"}, ObjectRef{.number = 1, .generation = 0}, true, model::Null{}}),
      Object(nested),
  };
  for (const Object& object : objects) {
    SCOPED_TRACE(model::debug_string(object).substr(0, 80));
    expect_round_trip(object);
  }
}

TEST(Serializer, RoundTripsParsedObjects) {
  for (const char* text : {"<</Type/Example/Subtype/DictionaryExample/Version 0.01/IntegerItem 12/StringItem(a string)"
                           "/Subdictionary<</Item1 0.4/Item2 true/LastItem(not!)/VeryLastItem(OK)>>>>",
                           "[549 3.14 false (Ralph) /SomeName]", "(Strings may contain balanced parentheses ( ) and special characters (*!&}^% and so on).)",
                           "<4E6F762073686D6F7A206B6120706F702E>", "/paired#28#29parentheses", "[1 2 R 3.5 (s) <41> /N [<<>>] true false null]",
                           "(\\053\\777 escaped)", "-.002", "4."}) {
    const auto object = parse(text);
    ASSERT_TRUE(object.has_value()) << text;
    SCOPED_TRACE(text);
    expect_round_trip(*object);
  }
}

// ── Byte sinks ──────────────────────────────────────────────────────────────────────────────

TEST(ByteSink, MemorySinkTracksPosition) {
  MemorySink sink;
  ASSERT_TRUE(sink.write("%PDF-").has_value());
  ASSERT_TRUE(sink.write("2.0\n").has_value());
  EXPECT_EQ(sink.position(), 9U);
  EXPECT_EQ(sink.data().size(), 9U);
}

TEST(ByteSink, FileSinkWritesAFile) {
  const auto path = std::filesystem::temp_directory_path() / "capriole_sink_test.bin";
  {
    auto sink = FileSink::create(path);
    ASSERT_TRUE(sink.has_value()) << sink.error().message;
    ASSERT_TRUE((*sink)->write("hello").has_value());
    EXPECT_EQ((*sink)->position(), 5U);
    ASSERT_TRUE((*sink)->close().has_value());
  }
  EXPECT_EQ(std::filesystem::file_size(path), 5U);
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace capriole::writer
