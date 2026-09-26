#include "capriole/parser/object_loader.hpp"

#include <string>
#include <utility>

#include "capriole/filters/decode.hpp"
#include "capriole/parser/object_parser.hpp"

namespace capriole::parser {

namespace {

using model::Object;

// Counts nested loads for the lifetime of a scope (hostile files can chain references in cycles).
class DepthGuard {
 public:
  explicit DepthGuard(std::size_t* depth) noexcept : depth_(depth) { ++*depth_; }
  DepthGuard(const DepthGuard&) = delete;
  DepthGuard& operator=(const DepthGuard&) = delete;
  DepthGuard(DepthGuard&&) = delete;
  DepthGuard& operator=(DepthGuard&&) = delete;
  ~DepthGuard() { --*depth_; }

 private:
  std::size_t* depth_;
};

std::unexpected<Error> with_context(Error error, std::uint32_t number) {
  error.message += " (object " + std::to_string(number) + ")";
  return std::unexpected(std::move(error));
}

}  // namespace

ObjectLoader::ObjectLoader(ByteSource& source, XrefTable xref, const ParseLimits& limits)
    : source_(&source), xref_(std::move(xref)), limits_(limits), reader_(source) {}

Result<Object> ObjectLoader::load(std::uint32_t number) {
  const XrefEntry* entry = xref_.find(number);
  if (entry == nullptr) {
    return Object(model::Null{});
  }
  return load_entry(number, *entry);
}

Result<Object> ObjectLoader::resolve(model::ObjectRef ref) {
  const XrefEntry* entry = xref_.find(ref.number);
  if (entry == nullptr || entry->type == XrefEntryType::kFree) {
    return Object(model::Null{});
  }
  // Objects in object streams always have generation 0 (§7.5.7).
  const std::uint16_t generation = entry->type == XrefEntryType::kCompressed ? 0 : entry->generation;
  if (ref.generation != generation) {
    return Object(model::Null{});
  }
  return load_entry(ref.number, *entry);
}

Result<Object> ObjectLoader::resolve(const Object& object) {
  if (const auto ref = object.as_ref()) {
    return resolve(*ref);
  }
  return object;
}

Result<Object> ObjectLoader::load_entry(std::uint32_t number, const XrefEntry& entry) {
  if (depth_ >= limits_.max_load_depth) {
    return make_error(ErrorCode::kLimitExceeded, "indirect objects nested deeper than max_load_depth (object " + std::to_string(number) + ")");
  }
  const DepthGuard guard(&depth_);
  switch (entry.type) {
    case XrefEntryType::kFree:
      return Object(model::Null{});
    case XrefEntryType::kInUse:
      return load_at_offset(number, entry);
    case XrefEntryType::kCompressed:
      return load_compressed(number, entry);
  }
  return Object(model::Null{});
}

Result<Object> ObjectLoader::load_at_offset(std::uint32_t number, const XrefEntry& entry) {
  ObjectParser parser(reader_, limits_);
  if (auto moved = parser.seek(entry.offset); !moved) {
    return with_context(std::move(moved.error()), number);
  }
  const LengthResolver resolver = [this](model::ObjectRef ref) -> Result<std::int64_t> {
    auto length = resolve(ref);
    if (!length) {
      return std::unexpected(std::move(length.error()));
    }
    const auto value = length->as_integer();
    if (!value) {
      return make_error(ErrorCode::kSyntax, "indirect /Length is not an integer (object " + std::to_string(ref.number) + ")");
    }
    return *value;
  };
  auto object = parser.parse_indirect_object(resolver);
  if (!object) {
    return with_context(std::move(object.error()), number);
  }
  if (object->ref.number != number || object->ref.generation != entry.generation) {
    return make_error(ErrorCode::kSyntax,
                      "xref entry for object " + std::to_string(number) + " " + std::to_string(entry.generation) + " points to object " +
                          std::to_string(object->ref.number) + " " + std::to_string(object->ref.generation),
                      entry.offset);
  }
  // Strings of objects stored directly in the file are encrypted one by one; objects inside object
  // streams are not (the whole object stream is encrypted instead, §7.6.2).
  if (security_ != nullptr && encrypt_object_ != number) {
    if (auto decrypted = decrypt_strings(object->object, object->ref); !decrypted) {
      return with_context(std::move(decrypted.error()), number);
    }
  }
  return std::move(object->object);
}

Result<Object> ObjectLoader::load_compressed(std::uint32_t number, const XrefEntry& entry) {
  auto stream = object_stream(entry.stream_number);
  if (!stream) {
    return with_context(std::move(stream.error()), number);
  }
  auto found = (*stream)->object_at(entry.stream_index);
  if (!found) {
    return with_context(std::move(found.error()), number);
  }
  if (found->first != number) {
    return make_error(ErrorCode::kSyntax, "object stream " + std::to_string(entry.stream_number) + " holds object " + std::to_string(found->first) +
                                              " where the xref expects object " + std::to_string(number));
  }
  return std::move(found->second);
}

Result<const ObjectStream*> ObjectLoader::object_stream(std::uint32_t stream_number) {
  if (const auto cached = object_streams_.find(stream_number); cached != object_streams_.end()) {
    return &cached->second;
  }
  // An object stream is itself a regular object: object streams do not nest (§7.5.7).
  const XrefEntry* entry = xref_.find(stream_number);
  if (entry == nullptr || entry->type != XrefEntryType::kInUse) {
    return make_error(ErrorCode::kSyntax, "object stream " + std::to_string(stream_number) + " is not an in-use object at a file offset");
  }
  auto object = load_entry(stream_number, *entry);
  if (!object) {
    return std::unexpected(std::move(object.error()));
  }
  const model::Stream* stream = object->as_stream();
  if (stream == nullptr || stream->dict.find("Type") == nullptr || !stream->dict.find("Type")->is_name("ObjStm")) {
    return make_error(ErrorCode::kSyntax, "object " + std::to_string(stream_number) + " is not an object stream (/Type /ObjStm)");
  }
  auto decoded = decode_stream(*stream);
  if (!decoded) {
    return std::unexpected(std::move(decoded.error()));
  }
  auto parsed = ObjectStream::parse(std::move(*decoded), stream->dict, limits_);
  if (!parsed) {
    return std::unexpected(std::move(parsed.error()));
  }
  const auto inserted = object_streams_.emplace(stream_number, std::move(*parsed));
  return &inserted.first->second;
}

void ObjectLoader::set_security_handler(std::shared_ptr<const crypto::StandardSecurityHandler> handler, std::optional<std::uint32_t> encrypt_object) {
  security_ = std::move(handler);
  encrypt_object_ = encrypt_object;
}

Result<void> ObjectLoader::decrypt_strings(Object& object, model::ObjectRef ref) const {
  if (const model::String* string = object.as_string()) {
    auto plain = security_->decrypt(std::as_bytes(std::span(string->bytes.data(), string->bytes.size())), ref, security_->string_method());
    if (!plain) {
      return std::unexpected(std::move(plain.error()));
    }
    object = Object(
        model::String{.bytes = std::string(reinterpret_cast<const char*>(plain->data()), plain->size()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                      .form = string->form});
    return {};
  }
  if (model::Array* array = object.as_array()) {
    for (Object& element : *array) {
      if (auto done = decrypt_strings(element, ref); !done) {
        return done;
      }
    }
    return {};
  }
  model::Dict* dict = object.as_dict();
  if (model::Stream* stream = object.as_stream()) {
    // Cross-reference streams are never encrypted (§7.5.8.1).
    const Object* type = stream->dict.find("Type");
    if (type != nullptr && type->is_name("XRef")) {
      return {};
    }
    dict = &stream->dict;
  }
  if (dict == nullptr) {
    return {};
  }
  // The /Contents of a signature dictionary is not encrypted (§7.6.2): the signed byte range must
  // be verifiable without the key.
  const bool signature = dict->contains("ByteRange") && dict->contains("Contents");
  for (const auto& entry : dict->entries()) {
    if (signature && entry.key == "Contents") {
      continue;
    }
    Object* value = dict->find(entry.key);
    if (auto done = decrypt_strings(*value, ref); !done) {
      return done;
    }
  }
  return {};
}

Result<std::vector<std::byte>> ObjectLoader::read_stream_data(const model::Stream& stream) {
  if (stream.in_memory()) {
    return *stream.memory_data;
  }
  auto encoded = filters::read_encoded(*source_, stream);
  if (!encoded || !encrypted()) {
    return encoded;
  }
  if (security_ == nullptr) {
    return make_error(ErrorCode::kUnsupported, "the document is encrypted and no security handler is set");
  }
  const Object* type = stream.dict.find("Type");
  if (type != nullptr && type->is_name("XRef")) {
    return encoded;  // cross-reference streams are never encrypted
  }
  if (type != nullptr && type->is_name("Metadata") && !security_->encrypt_metadata()) {
    return encoded;
  }
  if (!stream.origin) {
    return make_error(ErrorCode::kSyntax, "stream of an encrypted file has no object number to derive its key");
  }

  // A /Crypt filter first in the chain selects the crypt filter for this stream (§7.4.10).
  crypto::CryptMethod method = security_->stream_method();
  // /Filter and /DecodeParms are a single value or an array of them: the first element either way.
  const auto first_of = [&](std::string_view key) -> const Object* {
    const Object* value = stream.dict.find(key);
    if (value == nullptr || value->as_array() == nullptr) {
      return value;
    }
    return value->as_array()->empty() ? nullptr : &value->as_array()->front();
  };
  const Object* first_filter = first_of("Filter");
  if (first_filter != nullptr && first_filter->is_name("Crypt")) {
    const Object* first_parms = first_of("DecodeParms");
    const Object* name = first_parms != nullptr && first_parms->as_dict() != nullptr ? first_parms->as_dict()->find("Name") : nullptr;
    auto selected = security_->crypt_filter(name != nullptr && name->as_name() != nullptr ? name->as_name()->bytes : "Identity");
    if (!selected) {
      return std::unexpected(std::move(selected.error()));
    }
    method = *selected;
  }
  return security_->decrypt(*encoded, *stream.origin, method);
}

Result<std::vector<std::byte>> ObjectLoader::decode_stream(const model::Stream& stream) {
  // /Filter and /DecodeParms may be (or contain) references; the decoder wants direct objects.
  model::Dict dict = stream.dict;
  for (const char* key : {"Filter", "DecodeParms"}) {
    Object* value = dict.find(key);
    if (value == nullptr) {
      continue;
    }
    auto resolved = resolve(*value);
    if (!resolved) {
      return std::unexpected(std::move(resolved.error()));
    }
    *value = std::move(*resolved);
    if (model::Array* array = value->as_array()) {
      for (Object& element : *array) {
        auto element_resolved = resolve(element);
        if (!element_resolved) {
          return std::unexpected(std::move(element_resolved.error()));
        }
        element = std::move(*element_resolved);
      }
    }
  }
  auto encoded = read_stream_data(stream);
  if (!encoded) {
    return std::unexpected(std::move(encoded.error()));
  }
  return filters::decode(std::move(*encoded), dict, limits_);
}

}  // namespace capriole::parser
