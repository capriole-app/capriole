#pragma once

// PDF objects (ISO 32000-2 §7.3): the building blocks of the low-level model.
//
// The representation stays faithful to the file rather than convenient: dictionaries keep their key
// order, strings remember whether they were literal or hexadecimal, streams remember where their data
// lies instead of loading it. The writer relies on this to reproduce unmodified content exactly.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace capriole::model {

class Object;

// null
struct Null {
  friend bool operator==(Null, Null) noexcept = default;
};

// Indirect reference "n g R" (§7.3.10).
struct ObjectRef {
  std::uint32_t number = 0;
  std::uint16_t generation = 0;
  friend bool operator==(ObjectRef, ObjectRef) noexcept = default;
};

enum class StringForm : std::uint8_t { kLiteral, kHex };

// Byte string (§7.3.4). bytes is binary data; form records how it was written.
struct String {
  std::string bytes;
  StringForm form = StringForm::kLiteral;
  friend bool operator==(const String&, const String&) = default;
};

// Name (§7.3.5), decoded, without the leading '/'.
struct Name {
  std::string bytes;
  friend bool operator==(const Name&, const Name&) = default;
};

using Array = std::vector<Object>;

struct DictEntry;

// Dictionary (§7.3.7). An ordered list of entries, not a hash map: file order is preserved, and
// real dictionaries are small enough that a linear lookup is as fast as hashing.
class Dict {
 public:
  // Special members are defined out of line, where DictEntry is complete: Object's inline
  // constructors would otherwise instantiate ~vector<DictEntry> on an incomplete type.
  Dict();
  ~Dict();
  Dict(const Dict& other);
  Dict(Dict&& other) noexcept;
  Dict& operator=(const Dict& other);
  Dict& operator=(Dict&& other) noexcept;

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const std::vector<DictEntry>& entries() const noexcept { return entries_; }

  // The value for key, or nullptr when absent.
  [[nodiscard]] const Object* find(std::string_view key) const noexcept;
  [[nodiscard]] Object* find(std::string_view key) noexcept;
  [[nodiscard]] bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

  // Sets key to value: replaces in place if present (order kept), appends otherwise.
  void set(std::string key, Object value);
  // Removes key. Returns whether it was present.
  bool erase(std::string_view key);

  friend bool operator==(const Dict&, const Dict&);

 private:
  std::vector<DictEntry> entries_;
};

// Stream (§7.3.8): a dictionary plus the location of its (still encoded) data in the source.
// The data is not loaded: files can hold gigabytes of images.
struct Stream {
  Dict dict;
  std::uint64_t data_offset = 0;
  std::uint64_t data_length = 0;
  friend bool operator==(const Stream&, const Stream&) = default;
};

enum class ObjectKind : std::uint8_t { kNull, kBoolean, kInteger, kReal, kString, kName, kArray, kDictionary, kStream, kReference };

std::string_view to_string(ObjectKind kind) noexcept;

class Object {
 public:
  using Value = std::variant<Null, bool, std::int64_t, double, String, Name, Array, Dict, Stream, ObjectRef>;

  Object() = default;
  // Implicit on purpose: Object{42}, Object{Name{"Type"}}, Object{Array{...}} read naturally.
  Object(Null value) : value_(value) {}
  Object(bool value) : value_(value) {}
  Object(int value) : value_(std::int64_t{value}) {}
  Object(std::int64_t value) : value_(value) {}
  Object(double value) : value_(value) {}
  Object(String value) : value_(std::move(value)) {}
  Object(Name value) : value_(std::move(value)) {}
  Object(Array value) : value_(std::move(value)) {}
  Object(Dict value) : value_(std::move(value)) {}
  Object(Stream value) : value_(std::move(value)) {}
  Object(ObjectRef value) : value_(value) {}
  // A string literal would otherwise convert to bool. Use String{...} or Name{...}.
  Object(const char*) = delete;

  [[nodiscard]] ObjectKind kind() const noexcept { return static_cast<ObjectKind>(value_.index()); }
  [[nodiscard]] const Value& value() const noexcept { return value_; }

  [[nodiscard]] bool is_null() const noexcept { return kind() == ObjectKind::kNull; }

  // Typed access: nullptr / nullopt when the object is of another kind.
  [[nodiscard]] std::optional<bool> as_bool() const noexcept;
  [[nodiscard]] std::optional<std::int64_t> as_integer() const noexcept;
  // Integer or real, as a double (§7.3.3: where a real is expected an integer is acceptable).
  [[nodiscard]] std::optional<double> as_number() const noexcept;
  [[nodiscard]] std::optional<ObjectRef> as_ref() const noexcept;
  [[nodiscard]] const String* as_string() const noexcept { return std::get_if<String>(&value_); }
  [[nodiscard]] const Name* as_name() const noexcept { return std::get_if<Name>(&value_); }
  [[nodiscard]] const Array* as_array() const noexcept { return std::get_if<Array>(&value_); }
  [[nodiscard]] Array* as_array() noexcept { return std::get_if<Array>(&value_); }
  [[nodiscard]] const Dict* as_dict() const noexcept { return std::get_if<Dict>(&value_); }
  [[nodiscard]] Dict* as_dict() noexcept { return std::get_if<Dict>(&value_); }
  [[nodiscard]] const Stream* as_stream() const noexcept { return std::get_if<Stream>(&value_); }
  [[nodiscard]] Stream* as_stream() noexcept { return std::get_if<Stream>(&value_); }

  // True when this is a name equal to text.
  [[nodiscard]] bool is_name(std::string_view text) const noexcept;

  friend bool operator==(const Object&, const Object&) = default;

 private:
  Value value_;
};

struct DictEntry {
  std::string key;
  Object value;
  friend bool operator==(const DictEntry&, const DictEntry&) = default;
};

// PDF-like rendering for diagnostics and tests (not the writer: no escaping guarantees for output).
std::string debug_string(const Object& object);

}  // namespace capriole::model
