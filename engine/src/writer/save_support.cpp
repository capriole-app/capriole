#include "save_support.hpp"

#include <format>
#include <utility>
#include <vector>

#include "capriole/writer/serializer.hpp"

namespace capriole::writer::detail {

Result<void> write_indirect(Document& document, ByteSink& sink, model::ObjectRef ref, const model::Object& object) {
  // Everything that can fail on the input (the stream data) comes before the first byte is written,
  // so that a failed object leaves nothing behind and the caller can write a replacement.
  std::string text = std::format("{} {} obj\n", ref.number, ref.generation);
  std::vector<std::byte> data;
  const model::Stream* stream = object.as_stream();
  if (stream != nullptr) {
    auto encoded = document.encoded_stream_data(*stream);
    if (!encoded) {
      return std::unexpected(std::move(encoded.error()));
    }
    data = std::move(*encoded);
    model::Dict dict = stream->dict;
    dict.set("Length", static_cast<std::int64_t>(data.size()));
    serialize(model::Object(std::move(dict)), text);
    text += "\nstream\n";
  } else {
    serialize(object, text);
  }
  if (auto ok = sink.write(text); !ok) {
    return ok;
  }
  if (stream != nullptr) {
    if (auto ok = sink.write(data); !ok) {
      return ok;
    }
    if (auto ok = sink.write(std::string_view("\nendstream")); !ok) {
      return ok;
    }
  }
  return sink.write(std::string_view("\nendobj\n"));
}

model::Object file_identifier(const model::Dict& trailer, const crypto::Md5::Digest& digest) {
  const std::string fresh(reinterpret_cast<const char*>(digest.data()), digest.size());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  std::string first = fresh;
  if (const model::Object* id = trailer.find("ID");
      id != nullptr && id->as_array() != nullptr && !id->as_array()->empty() && id->as_array()->front().as_string() != nullptr) {
    first = id->as_array()->front().as_string()->bytes;
  }
  return model::Array{model::String{.bytes = first, .form = model::StringForm::kHex}, model::String{.bytes = fresh, .form = model::StringForm::kHex}};
}

}  // namespace capriole::writer::detail
