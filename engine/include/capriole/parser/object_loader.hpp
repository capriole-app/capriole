#pragma once

// Loads objects by number through the cross-reference table: from their byte offset, or out of an
// object stream. Read-only; the modifiable document model (phase 2d) is built on top of this.
//
// Following §7.3.10, a reference to an object that is free, absent from the xref, or whose generation
// does not match, is a reference to the null object — it loads as null, not as an error. Everything
// else that does not match (the xref points at the wrong object, an object stream lies) is an error.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/model/object.hpp"
#include "capriole/parser/object_stream.hpp"
#include "capriole/parser/xref.hpp"

namespace capriole::parser {

class ObjectLoader {
 public:
  // The source must outlive the loader.
  ObjectLoader(ByteSource& source, XrefTable xref, const ParseLimits& limits = kDefaultParseLimits);

  [[nodiscard]] const XrefTable& xref() const noexcept { return xref_; }

  // True when the trailer has /Encrypt. Without a security handler (set_security_handler), decoding
  // streams of an encrypted file — object streams included — is ErrorCode::kUnsupported.
  [[nodiscard]] bool encrypted() const noexcept { return xref_.trailer.contains("Encrypt"); }

  // Enables decryption: strings of objects loaded from then on, and stream data, are decrypted.
  // encrypt_object is the number of the /Encrypt dictionary when it is indirect (it is never
  // encrypted itself).
  void set_security_handler(std::shared_ptr<const crypto::StandardSecurityHandler> handler, std::optional<std::uint32_t> encrypt_object);

  // The object as the xref defines it; null when free or absent.
  [[nodiscard]] Result<model::Object> load(std::uint32_t number);

  // The referenced object; null when the generation does not match the xref.
  [[nodiscard]] Result<model::Object> resolve(model::ObjectRef ref);

  // If object is a reference, the object it refers to; otherwise object itself.
  [[nodiscard]] Result<model::Object> resolve(const model::Object& object);

  // Decoded data of a stream of this file (indirect /Filter and /DecodeParms are resolved).
  [[nodiscard]] Result<std::vector<std::byte>> decode_stream(const model::Stream& stream);

  // The stream's encoded data, decrypted when the file is encrypted, before its /Filter chain.
  [[nodiscard]] Result<std::vector<std::byte>> read_stream_data(const model::Stream& stream);

 private:
  Result<model::Object> load_entry(std::uint32_t number, const XrefEntry& entry);
  Result<model::Object> load_at_offset(std::uint32_t number, const XrefEntry& entry);
  Result<model::Object> load_compressed(std::uint32_t number, const XrefEntry& entry);
  Result<const ObjectStream*> object_stream(std::uint32_t stream_number);
  Result<void> decrypt_strings(model::Object& object, model::ObjectRef ref) const;

  ByteSource* source_;
  XrefTable xref_;
  ParseLimits limits_;
  SourceReader reader_;
  std::map<std::uint32_t, ObjectStream> object_streams_;
  std::size_t depth_ = 0;
  std::shared_ptr<const crypto::StandardSecurityHandler> security_;
  std::optional<std::uint32_t> encrypt_object_;
};

}  // namespace capriole::parser
