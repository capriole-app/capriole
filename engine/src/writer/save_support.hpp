#pragma once

// Internal to the writer: pieces shared by the full and the incremental save.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "capriole/core/error.hpp"
#include "capriole/crypto/hash.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"
#include "capriole/writer/byte_sink.hpp"

namespace capriole::writer::detail {

// Forwards to another sink and hashes everything that passes through (for the file identifier).
class HashingSink final : public ByteSink {
 public:
  explicit HashingSink(ByteSink& inner) noexcept : inner_(&inner) {}

  [[nodiscard]] Result<void> write(std::span<const std::byte> data) override {
    md5_.update(data);
    return inner_->write(data);
  }
  using ByteSink::write;
  [[nodiscard]] std::uint64_t position() const noexcept override { return inner_->position(); }
  // Hash of everything written so far (the running hash is left untouched).
  [[nodiscard]] crypto::Md5::Digest digest() const {
    crypto::Md5 copy = md5_;
    return copy.finish();
  }

 private:
  ByteSink* inner_;
  crypto::Md5 md5_;
};

// Writes "n g obj ... endobj" for an object of the document. A stream's data is written as the
// document holds it encoded (never re-compressed), with /Length set to its real size.
[[nodiscard]] Result<void> write_indirect(Document& document, ByteSink& sink, model::ObjectRef ref, const model::Object& object);

// The /ID pair: the first string kept from the trailer when present, the second a hash of the file.
[[nodiscard]] model::Object file_identifier(const model::Dict& trailer, const crypto::Md5::Digest& digest);

}  // namespace capriole::writer::detail
