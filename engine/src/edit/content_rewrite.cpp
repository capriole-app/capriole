#include "content_rewrite.hpp"

#include <algorithm>
#include <format>
#include <memory>

#include "capriole/content/interpreter.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/writer/serializer.hpp"
#include "common.hpp"

namespace capriole::edit::detail {

using model::Dict;
using model::Object;
using model::ObjectRef;

std::vector<content::Operation> operations_of(std::span<const std::byte> content) {
  const content::InterpreterLimits limits;
  std::vector<content::Operation> ops;
  content::ContentParser parser(content, limits.parse, parser::ParseMode::kTolerant);
  for (;;) {
    auto next = parser.next();
    if (!next || !*next) {
      break;
    }
    ops.push_back(std::move(**next));
  }
  return ops;
}

std::string operand_text(const Object& operand) { return writer::serialize(operand); }

std::string op_text(const content::Operation& op) {
  if (op.op == content::Operator::kInlineImage && op.image) {
    std::string out = "BI";
    for (const model::DictEntry& e : op.image->dict.entries()) {
      out += " " + operand_text(Object(name(e.key))) + " " + operand_text(e.value);
    }
    out += " ID ";
    for (const std::byte b : op.image->data) {
      out.push_back(static_cast<char>(b));
    }
    return out + "\nEI";
  }
  std::string out;
  for (const Object& operand : op.operands) {
    out += operand_text(operand) + " ";
  }
  return out + op.name;
}

Result<ObjectRef> add_content(Document& document, Dict dict, const std::string& content) {
  std::vector<std::byte> raw(content.size());
  std::ranges::transform(content, raw.begin(), [](char c) { return static_cast<std::byte>(c); });
  auto packed = filters::flate_encode(raw);
  if (!packed) {
    return std::unexpected(std::move(packed.error()));
  }
  model::Stream stream;
  stream.dict = std::move(dict);
  for (const std::string_view key : {"Length", "Filter", "DecodeParms", "F", "FFilter", "FDecodeParms", "DL"}) {
    stream.dict.erase(key);
  }
  stream.dict.set("Filter", name("FlateDecode"));
  stream.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*packed));
  return document.add(Object(std::move(stream)));
}

Dict with_xobjects(Document& document, Dict resources, const std::vector<std::pair<std::string, ObjectRef>>& added, const std::set<std::string>& removed) {
  if (added.empty() && removed.empty()) {
    return resources;
  }
  Dict xobjects;
  if (const Object* entry = resources.find("XObject")) {
    auto resolved = document.resolve(*entry);
    if (resolved && resolved->as_dict() != nullptr) {
      xobjects = *resolved->as_dict();
    }
  }
  for (const std::string& key : removed) {
    xobjects.erase(key);
  }
  for (const auto& [key, ref] : added) {
    xobjects.set(key, ref);
  }
  resources.set("XObject", std::move(xobjects));
  return resources;
}

std::string fresh_name(Document& document, const Dict& resources, std::set<std::string>& taken, std::string_view prefix) {
  Dict xobjects;
  if (const Object* entry = resources.find("XObject")) {
    auto resolved = document.resolve(*entry);
    if (resolved && resolved->as_dict() != nullptr) {
      xobjects = *resolved->as_dict();
    }
  }
  for (std::size_t n = 1;; ++n) {
    std::string candidate = std::format("{}{}", prefix, n);
    if (!xobjects.contains(candidate) && taken.insert(candidate).second) {
      return candidate;
    }
  }
}

}  // namespace capriole::edit::detail
