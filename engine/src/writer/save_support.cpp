#include "save_support.hpp"

#include <format>
#include <utility>
#include <vector>

#include "capriole/crypto/security_handler.hpp"
#include "capriole/writer/serializer.hpp"

namespace capriole::writer::detail {

namespace {

// Encrypts the strings of an object written as ref: the inverse of ObjectLoader::decrypt_strings,
// with the same exceptions — the dictionary of a cross-reference stream, and the /Contents of a
// signature dictionary (§7.6.2).
Result<void> encrypt_strings(const crypto::StandardSecurityHandler& security, model::Object& object, model::ObjectRef ref) {
  if (const model::String* string = object.as_string()) {
    auto encrypted = security.encrypt(std::as_bytes(std::span(string->bytes.data(), string->bytes.size())), ref, security.string_method());
    if (!encrypted) {
      return std::unexpected(std::move(encrypted.error()));
    }
    // Written in hexadecimal: encrypted bytes are binary, and hex is what writers use for them.
    object = model::Object(model::String{
        .bytes = std::string(reinterpret_cast<const char*>(encrypted->data()), encrypted->size()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        .form = model::StringForm::kHex});
    return {};
  }
  if (model::Array* array = object.as_array()) {
    for (model::Object& element : *array) {
      if (auto done = encrypt_strings(security, element, ref); !done) {
        return done;
      }
    }
    return {};
  }
  model::Dict* dict = object.as_dict();
  if (model::Stream* stream = object.as_stream()) {
    const model::Object* type = stream->dict.find("Type");
    if (type != nullptr && type->is_name("XRef")) {
      return {};
    }
    dict = &stream->dict;
  }
  if (dict == nullptr) {
    return {};
  }
  const bool signature = dict->contains("ByteRange") && dict->contains("Contents");
  for (const auto& entry : dict->entries()) {
    if (signature && entry.key == "Contents") {
      continue;
    }
    if (auto done = encrypt_strings(security, *dict->find(entry.key), ref); !done) {
      return done;
    }
  }
  return {};
}

}  // namespace

Result<void> write_indirect(Document& document, ByteSink& sink, model::ObjectRef ref, const model::Object& original) {
  // Everything that can fail on the input (the stream data) comes before the first byte is written,
  // so that a failed object leaves nothing behind and the caller can write a replacement.
  //
  // An encrypted document is written encrypted with its own key and methods (PLAN.md 3d): the
  // document holds everything decrypted, so each object is encrypted again for the number it is
  // written as — except the /Encrypt dictionary itself.
  const crypto::StandardSecurityHandler* security = document.security();
  if (security != nullptr && document.encrypt_object() == ref.number) {
    security = nullptr;
  }
  model::Object object = original;
  std::string text = std::format("{} {} obj\n", ref.number, ref.generation);
  std::vector<std::byte> data;
  model::Stream* stream = object.as_stream();
  if (stream != nullptr) {
    auto encoded = document.encoded_stream_data(*stream);
    if (!encoded) {
      return std::unexpected(std::move(encoded.error()));
    }
    data = std::move(*encoded);
    if (security != nullptr) {
      auto method = security->method_for_stream(stream->dict);
      if (!method) {
        return std::unexpected(std::move(method.error()));
      }
      auto encrypted = security->encrypt(data, ref, *method);
      if (!encrypted) {
        return std::unexpected(std::move(encrypted.error()));
      }
      data = std::move(*encrypted);
    }
  }
  if (security != nullptr) {
    if (auto done = encrypt_strings(*security, object, ref); !done) {
      return done;
    }
  }
  if (stream != nullptr) {
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

model::Object file_identifier(const model::Dict& trailer, const crypto::Md5::Digest& digest, bool encrypted) {
  const std::string fresh(reinterpret_cast<const char*>(digest.data()), digest.size());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  // The first string enters the key derivation of an encrypted file (Algorithms 2 and 6): it must stay
  // what it was — empty when the file had none.
  std::string first = encrypted ? std::string() : fresh;
  if (const model::Object* id = trailer.find("ID");
      id != nullptr && id->as_array() != nullptr && !id->as_array()->empty() && id->as_array()->front().as_string() != nullptr) {
    first = id->as_array()->front().as_string()->bytes;
  }
  return model::Array{model::String{.bytes = first, .form = model::StringForm::kHex}, model::String{.bytes = fresh, .form = model::StringForm::kHex}};
}

}  // namespace capriole::writer::detail
