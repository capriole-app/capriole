#include "capriole/model/object.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace capriole::model {
namespace {

TEST(Object, KindsAndAccessors) {
  EXPECT_EQ(Object().kind(), ObjectKind::kNull);
  EXPECT_TRUE(Object(Null{}).is_null());
  EXPECT_EQ(Object(true).as_bool(), true);
  EXPECT_EQ(Object(42).as_integer(), 42);
  EXPECT_EQ(Object(std::int64_t{-7}).as_integer(), -7);
  EXPECT_EQ(Object(2.5).kind(), ObjectKind::kReal);
  EXPECT_EQ(Object(Name{"Type"}).kind(), ObjectKind::kName);
  EXPECT_EQ(Object(Array{}).kind(), ObjectKind::kArray);
  EXPECT_EQ(Object(Dict{}).kind(), ObjectKind::kDictionary);
  EXPECT_EQ(Object(Stream{}).kind(), ObjectKind::kStream);
  EXPECT_EQ(Object(ObjectRef{.number = 3, .generation = 0}).as_ref(), (ObjectRef{.number = 3, .generation = 0}));

  // Wrong-kind access is empty, never a throw.
  EXPECT_FALSE(Object(42).as_bool().has_value());
  EXPECT_EQ(Object(42).as_dict(), nullptr);
  EXPECT_FALSE(Object(Name{"x"}).as_integer().has_value());
}

TEST(Object, AsNumberAcceptsIntegersAndReals) {
  EXPECT_EQ(Object(3).as_number(), 3.0);
  EXPECT_EQ(Object(0.5).as_number(), 0.5);
  EXPECT_FALSE(Object(true).as_number().has_value());
}

TEST(Object, IsName) {
  EXPECT_TRUE(Object(Name{"Page"}).is_name("Page"));
  EXPECT_FALSE(Object(Name{"Page"}).is_name("Pages"));
  EXPECT_FALSE(Object(String{.bytes = "Page"}).is_name("Page"));
}

TEST(Dict, PreservesInsertionOrder) {
  Dict dict;
  dict.set("Type", Name{"Page"});
  dict.set("Parent", ObjectRef{.number = 2, .generation = 0});
  dict.set("MediaBox", Array{0, 0, 612, 792});
  ASSERT_EQ(dict.size(), 3U);
  EXPECT_EQ(dict.entries()[0].key, "Type");
  EXPECT_EQ(dict.entries()[1].key, "Parent");
  EXPECT_EQ(dict.entries()[2].key, "MediaBox");
}

TEST(Dict, SetReplacesInPlace) {
  Dict dict;
  dict.set("A", 1);
  dict.set("B", 2);
  dict.set("A", 3);
  ASSERT_EQ(dict.size(), 2U);
  EXPECT_EQ(dict.entries()[0].key, "A");
  EXPECT_EQ(dict.find("A")->as_integer(), 3);
}

TEST(Dict, FindAndErase) {
  Dict dict;
  dict.set("A", 1);
  EXPECT_TRUE(dict.contains("A"));
  EXPECT_EQ(dict.find("B"), nullptr);
  EXPECT_TRUE(dict.erase("A"));
  EXPECT_FALSE(dict.erase("A"));
  EXPECT_TRUE(dict.empty());
}

TEST(Object, Equality) {
  Dict a;
  a.set("K", Array{1, 2.5, Name{"N"}});
  Dict b = a;
  EXPECT_EQ(Object(a), Object(b));
  b.set("K", Array{1, 2.5, Name{"M"}});
  EXPECT_NE(Object(a), Object(b));
  // Same bytes, different string form: not equal (the form is part of the file's content).
  EXPECT_NE(Object(String{.bytes = "x", .form = StringForm::kLiteral}), Object(String{.bytes = "x", .form = StringForm::kHex}));
}

TEST(Object, DebugString) {
  Dict dict;
  dict.set("Type", Name{"Page"});
  dict.set("Kids", Array{ObjectRef{.number = 4, .generation = 0}, Null{}, true});
  dict.set("Title", String{.bytes = "A (b)"});
  EXPECT_EQ(debug_string(Object(dict)), "<</Type /Page/Kids [4 0 R null true]/Title (A\\x20\\x28b\\x29)>>");
  EXPECT_EQ(debug_string(Object(Stream{.dict = {}, .data_offset = 10, .data_length = 5, .memory_data = nullptr, .origin = std::nullopt})), "<<>> stream[10+5]");
  EXPECT_EQ(to_string(ObjectKind::kDictionary), "dictionary");
}

}  // namespace
}  // namespace capriole::model
