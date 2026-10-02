#pragma once

// Internal to the C API: reading the JSON a host passes in (annotation specifications). Strict RFC 8259
// with bounds against hostile input — nesting, length, number of values —; strings come out as UTF-8
// (\u escapes, surrogate pairs joined; an unpaired one is an error).

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::capi {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue {
  std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<JsonArray>, std::shared_ptr<JsonObject>> value;

  [[nodiscard]] const JsonObject* object() const;
  [[nodiscard]] const JsonArray* array() const;
  [[nodiscard]] std::optional<double> number() const;
  [[nodiscard]] const std::string* string() const;
  [[nodiscard]] std::optional<bool> boolean() const;
  // The member of an object (nullptr when this is not an object or has no such member).
  [[nodiscard]] const JsonValue* get(std::string_view key) const;
};

struct JsonLimits {
  std::size_t max_bytes = std::size_t{16} * 1024 * 1024;
  std::size_t max_depth = 32;
  std::size_t max_values = 1'000'000;
};

// The whole text must be one value (white space around it allowed).
[[nodiscard]] Result<JsonValue> parse_json(std::string_view text, const JsonLimits& limits = {});

}  // namespace capriole::capi
