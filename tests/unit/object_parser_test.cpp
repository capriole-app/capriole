#include "capriole/parser/object_parser.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace capriole::parser {
namespace {

using model::Array;
using model::Dict;
using model::Name;
using model::Object;
using model::ObjectRef;

// Owns the whole parsing stack for one input.
class Parse {
 public:
  explicit Parse(const std::string& text, const ParseLimits& limits = kDefaultParseLimits)
      : source_(MemoryByteSource::from_string(text)), reader_(source_), parser_(reader_, limits) {}

  ObjectParser& parser() { return parser_; }
  SourceReader& reader() { return reader_; }

 private:
  MemoryByteSource source_;
  SourceReader reader_;
  ObjectParser parser_;
};

Object parse_ok(const std::string& text) {
  Parse p(text);
  auto object = p.parser().parse_object();
  EXPECT_TRUE(object.has_value()) << text << ": " << (object ? "" : object.error().message);
  return object.has_value() ? *object : Object();
}

Error parse_error(const std::string& text, const ParseLimits& limits = kDefaultParseLimits) {
  Parse p(text, limits);
  auto object = p.parser().parse_object();
  EXPECT_FALSE(object.has_value()) << "expected an error for: " << text;
  return object.has_value() ? Error{.code = ErrorCode::kIo, .message = "no error"} : object.error();
}

Error indirect_error(const std::string& text, const LengthResolver& resolver = {}) {
  Parse p(text);
  auto object = p.parser().parse_indirect_object(resolver);
  EXPECT_FALSE(object.has_value()) << "expected an error for: " << text;
  return object.has_value() ? Error{.code = ErrorCode::kIo, .message = "no error"} : object.error();
}

// The encoded data of a stream, cut from the input text (offsets are 64-bit; size_t may be 32).
std::string stream_data(const std::string& text, const model::Stream& stream) {
  return text.substr(static_cast<std::size_t>(stream.data_offset), static_cast<std::size_t>(stream.data_length));
}

// ── Direct objects ──────────────────────────────────────────────────────────────────────────

TEST(ObjectParser, Scalars) {
  EXPECT_EQ(parse_ok("null"), Object(model::Null{}));
  EXPECT_EQ(parse_ok("true"), Object(true));
  EXPECT_EQ(parse_ok("false"), Object(false));
  EXPECT_EQ(parse_ok("-17"), Object(-17));
  EXPECT_EQ(parse_ok("3.25"), Object(3.25));
  EXPECT_EQ(parse_ok("/Type"), Object(Name{"Type"}));
  EXPECT_EQ(parse_ok("(text)"), Object(model::String{.bytes = "text", .form = model::StringForm::kLiteral}));
  EXPECT_EQ(parse_ok("<74>"), Object(model::String{.bytes = "t", .form = model::StringForm::kHex}));
}

TEST(ObjectParser, Arrays) {
  EXPECT_EQ(parse_ok("[]"), Object(Array{}));
  EXPECT_EQ(parse_ok("[549 3.14 false (Ralph) /SomeName]"), Object(Array{549, 3.14, false, model::String{.bytes = "Ralph"}, Name{"SomeName"}}));
  EXPECT_EQ(parse_ok("[[1 2][3 [4]]]"), Object(Array{Array{1, 2}, Array{3, Array{4}}}));
}

TEST(ObjectParser, Dictionaries) {
  const Object object = parse_ok(
      "<</Type /Example /Subtype /DictionaryExample /Version 0.01 /IntegerItem 12 /StringItem (a string)"
      " /Subdictionary <</Item1 0.4 /Item2 true /LastItem (not!) /VeryLastItem (OK)>>>>");
  const Dict* dict = object.as_dict();
  ASSERT_NE(dict, nullptr);
  ASSERT_EQ(dict->size(), 6U);
  EXPECT_EQ(dict->entries()[0].key, "Type");
  EXPECT_TRUE(dict->find("Subtype")->is_name("DictionaryExample"));
  EXPECT_EQ(dict->find("IntegerItem")->as_integer(), 12);
  const Dict* sub = dict->find("Subdictionary")->as_dict();
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->size(), 4U);
  EXPECT_EQ(sub->find("Item2")->as_bool(), true);
}

TEST(ObjectParser, NullValuesAreKept) {
  // §7.3.7 treats a null value as an absent entry; the parser keeps it so the file can be reproduced.
  const Object object = parse_ok("<</A null>>");
  ASSERT_NE(object.as_dict(), nullptr);
  EXPECT_TRUE(object.as_dict()->find("A")->is_null());
}

// ── References ──────────────────────────────────────────────────────────────────────────────

TEST(ObjectParser, References) {
  EXPECT_EQ(parse_ok("12 0 R"), Object(ObjectRef{.number = 12, .generation = 0}));
  EXPECT_EQ(parse_ok("[1 0 R 2 5 R]"), Object(Array{ObjectRef{.number = 1, .generation = 0}, ObjectRef{.number = 2, .generation = 5}}));
  // Only an integer followed by an integer and R is a reference.
  EXPECT_EQ(parse_ok("[1 2 3 R]"), Object(Array{1, ObjectRef{.number = 2, .generation = 3}}));
  EXPECT_EQ(parse_ok("[1 2 3]"), Object(Array{1, 2, 3}));
  EXPECT_EQ(parse_ok("<</Parent 3 0 R /Count 2>>").as_dict()->find("Parent")->as_ref(), (ObjectRef{.number = 3, .generation = 0}));
}

TEST(ObjectParser, InvalidReferences) {
  EXPECT_EQ(parse_error("0 0 R").code, ErrorCode::kSyntax);  // object 0 is never a target
  EXPECT_EQ(parse_error("-1 0 R").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("1 -1 R").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("1 65536 R").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("4294967296 0 R").code, ErrorCode::kSyntax);
}

TEST(ObjectParser, IntegerFollowedByUnlexableBytesStillParses) {
  // Lookahead errors are deferred: the integer is complete even if what follows is garbage.
  Parse p("7 )");
  const auto object = p.parser().parse_object();
  ASSERT_TRUE(object.has_value());
  EXPECT_EQ(*object, Object(7));
  EXPECT_FALSE(p.parser().parse_object().has_value());  // the garbage surfaces on the next call
}

TEST(ObjectParser, SequentialObjects) {
  Parse p("1 2 R /A [3] 4");
  EXPECT_EQ(*p.parser().parse_object(), Object(ObjectRef{.number = 1, .generation = 2}));
  EXPECT_EQ(*p.parser().parse_object(), Object(Name{"A"}));
  EXPECT_EQ(*p.parser().parse_object(), Object(Array{3}));
  EXPECT_EQ(*p.parser().parse_object(), Object(4));
  EXPECT_EQ(p.parser().parse_object().error().code, ErrorCode::kUnexpectedEof);
}

// ── Syntax errors ───────────────────────────────────────────────────────────────────────────

TEST(ObjectParser, SyntaxErrors) {
  EXPECT_EQ(parse_error("]").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error(">>").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("{").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("obj").code, ErrorCode::kSyntax);
  EXPECT_EQ(parse_error("<</A 1 /B>>").code, ErrorCode::kSyntax);  // missing value
  EXPECT_EQ(parse_error("<<1 2>>").code, ErrorCode::kSyntax);      // non-name key
  EXPECT_EQ(parse_error("[1 >>").code, ErrorCode::kSyntax);
}

TEST(ObjectParser, DuplicateKeysAreErrors) {
  const Error error = parse_error("<</A 1 /A 2>>");
  EXPECT_EQ(error.code, ErrorCode::kSyntax);
  EXPECT_EQ(error.offset, 7U);  // the second /A
}

TEST(ObjectParser, TruncatedContainers) {
  EXPECT_EQ(parse_error("").code, ErrorCode::kUnexpectedEof);
  EXPECT_EQ(parse_error("[1 2").code, ErrorCode::kUnexpectedEof);
  EXPECT_EQ(parse_error("<</A 1").code, ErrorCode::kUnexpectedEof);
  EXPECT_EQ(parse_error("<</A").code, ErrorCode::kUnexpectedEof);
}

TEST(ObjectParser, NestingLimit) {
  ParseLimits limits;
  limits.max_nesting_depth = 3;
  EXPECT_TRUE(Parse("[[[1]]]", limits).parser().parse_object().has_value());
  EXPECT_EQ(parse_error("[[[[1]]]]", limits).code, ErrorCode::kLimitExceeded);
  EXPECT_EQ(parse_error("<</A<</B<</C<<>>>>>>>>", limits).code, ErrorCode::kLimitExceeded);
  // The default limit stops a hostile nesting depth long before the stack is at risk.
  EXPECT_EQ(parse_error(std::string(100000, '[')).code, ErrorCode::kLimitExceeded);
}

// ── Indirect objects and streams ────────────────────────────────────────────────────────────

TEST(ObjectParser, IndirectObject) {
  Parse p("  12 0 obj\n(Brillig)\nendobj\n");
  const auto object = p.parser().parse_indirect_object();
  ASSERT_TRUE(object.has_value()) << object.error().message;
  EXPECT_EQ(object->ref, (ObjectRef{.number = 12, .generation = 0}));
  EXPECT_EQ(object->object, Object(model::String{.bytes = "Brillig"}));
  EXPECT_EQ(object->offset, 2U);
  EXPECT_EQ(object->end_offset, 27U);
}

TEST(ObjectParser, IndirectObjectWithDictionaryAndIntegerValue) {
  Parse p("3 0 obj <</Count 2>> endobj 4 1 obj 42 endobj");
  const auto first = p.parser().parse_indirect_object();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->object.as_dict()->find("Count")->as_integer(), 2);
  const auto second = p.parser().parse_indirect_object();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->ref, (ObjectRef{.number = 4, .generation = 1}));
  EXPECT_EQ(second->object, Object(42));
}

TEST(ObjectParser, IndirectObjectErrors) {
  EXPECT_EQ(indirect_error("1 0 (x) endobj").code, ErrorCode::kSyntax);  // no obj keyword
  EXPECT_EQ(indirect_error("0 0 obj 1 endobj").code, ErrorCode::kSyntax);
  EXPECT_EQ(indirect_error("1 0 obj 1 2 endobj").code, ErrorCode::kSyntax);
  EXPECT_EQ(indirect_error("1 0 obj 1").code, ErrorCode::kUnexpectedEof);
  EXPECT_EQ(indirect_error("1 0 obj 1 obj").code, ErrorCode::kSyntax);
}

TEST(ObjectParser, StreamWithLf) {
  const std::string text = "5 0 obj\n<</Length 5>>\nstream\nHELLO\nendstream\nendobj";
  Parse p(text);
  const auto object = p.parser().parse_indirect_object();
  ASSERT_TRUE(object.has_value()) << object.error().message;
  const model::Stream* stream = object->object.as_stream();
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->data_length, 5U);
  EXPECT_EQ(stream_data(text, *stream), "HELLO");
  EXPECT_EQ(stream->dict.find("Length")->as_integer(), 5);
}

TEST(ObjectParser, StreamWithCrLfAndBinaryData) {
  // The data contains bytes that would not lex, including a fake "endstream": /Length decides.
  const std::string data = std::string("\x00)endstream>>\xFF", 14);
  const std::string text = "5 0 obj <</Length 14>> stream\r\n" + data + "endstream endobj";
  Parse p(text);
  const auto object = p.parser().parse_indirect_object();
  ASSERT_TRUE(object.has_value()) << object.error().message;
  const model::Stream* stream = object->object.as_stream();
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream_data(text, *stream), data);
}

TEST(ObjectParser, EmptyStream) {
  Parse p("5 0 obj <</Length 0>> stream\nendstream endobj");
  const auto object = p.parser().parse_indirect_object();
  ASSERT_TRUE(object.has_value()) << object.error().message;
  EXPECT_EQ(object->object.as_stream()->data_length, 0U);
}

TEST(ObjectParser, StreamWithIndirectLength) {
  const std::string text = "5 0 obj <</Length 6 0 R>> stream\nABC\nendstream endobj";
  Parse p(text);
  int calls = 0;
  const LengthResolver resolver = [&](ObjectRef ref) -> Result<std::int64_t> {
    ++calls;
    EXPECT_EQ(ref, (ObjectRef{.number = 6, .generation = 0}));
    return 3;
  };
  const auto object = p.parser().parse_indirect_object(resolver);
  ASSERT_TRUE(object.has_value()) << object.error().message;
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(object->object.as_stream()->data_length, 3U);
}

TEST(ObjectParser, ResolverMayMoveTheReader) {
  const std::string text = "5 0 obj <</Length 6 0 R>> stream\nABC\nendstream endobj 6 0 obj 3 endobj";
  Parse p(text);
  const LengthResolver resolver = [&](ObjectRef /*ref*/) -> Result<std::int64_t> {
    // Jump elsewhere with the same reader, as a document resolving through the xref would.
    EXPECT_TRUE(p.reader().seek(text.find("6 0 obj")).has_value());
    return 3;
  };
  const auto object = p.parser().parse_indirect_object(resolver);
  ASSERT_TRUE(object.has_value()) << object.error().message;
}

TEST(ObjectParser, StreamErrors) {
  EXPECT_EQ(indirect_error("5 0 obj <</Length 3>> stream\rABC\nendstream endobj").code, ErrorCode::kSyntax);  // lone CR
  EXPECT_EQ(indirect_error("5 0 obj <</Length 3>> stream ABC\nendstream endobj").code, ErrorCode::kSyntax);   // no EOL
  EXPECT_EQ(indirect_error("5 0 obj <<>> stream\nABC\nendstream endobj").code, ErrorCode::kSyntax);           // no /Length
  EXPECT_EQ(indirect_error("5 0 obj <</Length (3)>> stream\nABC\nendstream endobj").code, ErrorCode::kSyntax);
  EXPECT_EQ(indirect_error("5 0 obj <</Length -1>> stream\nABC\nendstream endobj").code, ErrorCode::kSyntax);
  EXPECT_EQ(indirect_error("5 0 obj <</Length 2>> stream\nABC\nendstream endobj").code, ErrorCode::kSyntax);  // wrong length
  EXPECT_EQ(indirect_error("5 0 obj <</Length 99>> stream\nABC\nendstream endobj").code, ErrorCode::kOutOfRange);
  EXPECT_EQ(indirect_error("5 0 obj <</Length 6 0 R>> stream\nABC\nendstream endobj").code, ErrorCode::kUnsupported);  // no resolver
  const LengthResolver failing = [](ObjectRef ref) -> Result<std::int64_t> { return make_error(ErrorCode::kIo, "cannot resolve", ref.number); };
  EXPECT_EQ(indirect_error("5 0 obj <</Length 6 0 R>> stream\nABC\nendstream endobj", failing).code, ErrorCode::kIo);
}

TEST(ObjectParser, SeekDiscardsLookahead) {
  Parse p("1 2 /X");
  EXPECT_EQ(*p.parser().parse_object(), Object(1));  // peeked "2" and "/X"
  ASSERT_TRUE(p.parser().seek(4).has_value());
  EXPECT_EQ(*p.parser().parse_object(), Object(Name{"X"}));
}

}  // namespace
}  // namespace capriole::parser
