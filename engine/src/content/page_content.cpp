#include "capriole/content/page_content.hpp"

#include <string>
#include <utility>

namespace capriole::content {

Result<std::vector<std::byte>> page_content(Document& document, model::ObjectRef page) {
  auto node = document.get(page);
  if (!node) {
    return std::unexpected(std::move(node.error()));
  }
  const model::Dict* dict = (*node)->as_dict();
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "page " + std::to_string(page.number) + " is not a dictionary");
  }
  const model::Object* contents = dict->find("Contents");
  if (contents == nullptr) {
    return std::vector<std::byte>{};
  }
  auto resolved = document.resolve(*contents);
  if (!resolved) {
    return std::unexpected(std::move(resolved.error()));
  }

  // One stream, or an array of (references to) streams.
  std::vector<model::Object> parts;
  if (const model::Array* array = resolved->as_array()) {
    for (const model::Object& element : *array) {
      auto part = document.resolve(element);
      if (!part) {
        return std::unexpected(std::move(part.error()));
      }
      parts.push_back(std::move(*part));
    }
  } else {
    parts.push_back(std::move(*resolved));
  }

  std::vector<std::byte> content;
  for (const model::Object& part : parts) {
    const model::Stream* stream = part.as_stream();
    if (stream == nullptr) {
      continue;
    }
    auto data = document.decode_stream(*stream);
    if (!data) {
      return std::unexpected(std::move(data.error()));
    }
    if (!content.empty()) {
      content.push_back(std::byte{'\n'});
    }
    content.insert(content.end(), data->begin(), data->end());
  }
  return content;
}

}  // namespace capriole::content
