#include "capriole/model/object.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace capriole::model {

// ── Dict ────────────────────────────────────────────────────────────────────────────────────

Dict::Dict() = default;
Dict::~Dict() = default;
Dict::Dict(const Dict& other) = default;
Dict::Dict(Dict&& other) noexcept = default;
Dict& Dict::operator=(const Dict& other) = default;
Dict& Dict::operator=(Dict&& other) noexcept = default;

std::size_t Dict::size() const noexcept { return entries_.size(); }

bool Dict::empty() const noexcept { return entries_.empty(); }

const Object* Dict::find(std::string_view key) const noexcept {
  const auto it = std::ranges::find(entries_, key, &DictEntry::key);
  return it == entries_.end() ? nullptr : &it->value;
}

Object* Dict::find(std::string_view key) noexcept {
  const auto it = std::ranges::find(entries_, key, &DictEntry::key);
  return it == entries_.end() ? nullptr : &it->value;
}

void Dict::set(std::string key, Object value) {
  if (Object* existing = find(key)) {
    *existing = std::move(value);
    return;
  }
  entries_.push_back(DictEntry{.key = std::move(key), .value = std::move(value)});
}

bool Dict::erase(std::string_view key) {
  const auto it = std::ranges::find(entries_, key, &DictEntry::key);
  if (it == entries_.end()) {
    return false;
  }
  entries_.erase(it);
  return true;
}

bool operator==(const Dict& a, const Dict& b) { return a.entries_ == b.entries_; }

// ── Object ──────────────────────────────────────────────────────────────────────────────────

std::string_view to_string(ObjectKind kind) noexcept {
  switch (kind) {
    case ObjectKind::kNull:
      return "null";
    case ObjectKind::kBoolean:
      return "boolean";
    case ObjectKind::kInteger:
      return "integer";
    case ObjectKind::kReal:
      return "real";
    case ObjectKind::kString:
      return "string";
    case ObjectKind::kName:
      return "name";
    case ObjectKind::kArray:
      return "array";
    case ObjectKind::kDictionary:
      return "dictionary";
    case ObjectKind::kStream:
      return "stream";
    case ObjectKind::kReference:
      return "reference";
  }
  return "unknown";
}

std::optional<bool> Object::as_bool() const noexcept {
  if (const auto* value = std::get_if<bool>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

std::optional<std::int64_t> Object::as_integer() const noexcept {
  if (const auto* value = std::get_if<std::int64_t>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

std::optional<double> Object::as_number() const noexcept {
  if (const auto* value = std::get_if<std::int64_t>(&value_)) {
    return static_cast<double>(*value);
  }
  if (const auto* value = std::get_if<double>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

std::optional<ObjectRef> Object::as_ref() const noexcept {
  if (const auto* value = std::get_if<ObjectRef>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

bool Object::is_name(std::string_view text) const noexcept {
  const Name* name = as_name();
  return name != nullptr && name->bytes == text;
}

// ── debug_string ────────────────────────────────────────────────────────────────────────────

namespace {

void append_escaped(std::string& out, std::string_view bytes) {
  for (const char ch : bytes) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x21 && byte < 0x7F && ch != '(' && ch != ')' && ch != '\\' && ch != '#' && ch != '/') {
      out.push_back(ch);
    } else {
      out += std::format("\\x{:02X}", byte);
    }
  }
}

void append_debug(std::string& out, const Object& object);

struct DebugVisitor {
  std::string* out;

  void operator()(Null /*unused*/) const { *out += "null"; }
  void operator()(bool value) const { *out += value ? "true" : "false"; }
  void operator()(std::int64_t value) const { *out += std::to_string(value); }
  void operator()(double value) const { *out += std::format("{}", value); }
  void operator()(const String& value) const {
    *out += value.form == StringForm::kHex ? "<" : "(";
    append_escaped(*out, value.bytes);
    *out += value.form == StringForm::kHex ? ">" : ")";
  }
  void operator()(const Name& value) const {
    *out += '/';
    append_escaped(*out, value.bytes);
  }
  void operator()(const Array& value) const {
    *out += '[';
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i > 0) {
        *out += ' ';
      }
      append_debug(*out, value[i]);
    }
    *out += ']';
  }
  void operator()(const Dict& value) const {
    *out += "<<";
    for (const DictEntry& entry : value.entries()) {
      *out += '/';
      append_escaped(*out, entry.key);
      *out += ' ';
      append_debug(*out, entry.value);
    }
    *out += ">>";
  }
  void operator()(const Stream& value) const {
    (*this)(value.dict);
    *out += std::format(" stream[{}+{}]", value.data_offset, value.data_length);
  }
  void operator()(ObjectRef value) const { *out += std::format("{} {} R", value.number, value.generation); }
};

void append_debug(std::string& out, const Object& object) { std::visit(DebugVisitor{&out}, object.value()); }

}  // namespace

std::string debug_string(const Object& object) {
  std::string out;
  append_debug(out, object);
  return out;
}

}  // namespace capriole::model
