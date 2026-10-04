#pragma once

// ASN.1 in BER/DER (ITU-T X.690), as far as the structures of digital signatures need it — CMS
// (RFC 5652), PKCS#12 (RFC 7292), time stamps (RFC 3161), ESS (RFC 5035). Read: hostile input, so
// every length is checked against what is left and nesting is limited; BER's indefinite lengths are
// read too (signatures made by some tools use them), and constructed strings are joined. Written:
// DER only (SET OF sorted, §11.6).
//
// Only single-byte tags (tag numbers up to 30): the structures read here use no others.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::crypto::der {

using Bytes = std::vector<std::byte>;

// Tags (universal class, and the context-specific ones the structures use).
inline constexpr std::uint8_t kBoolean = 0x01;
inline constexpr std::uint8_t kInteger = 0x02;
inline constexpr std::uint8_t kBitString = 0x03;
inline constexpr std::uint8_t kOctetString = 0x04;
inline constexpr std::uint8_t kNull = 0x05;
inline constexpr std::uint8_t kOid = 0x06;
inline constexpr std::uint8_t kUtf8String = 0x0C;
inline constexpr std::uint8_t kPrintableString = 0x13;
inline constexpr std::uint8_t kIa5String = 0x16;
inline constexpr std::uint8_t kUtcTime = 0x17;
inline constexpr std::uint8_t kGeneralizedTime = 0x18;
inline constexpr std::uint8_t kBmpString = 0x1E;
inline constexpr std::uint8_t kSequence = 0x30;
inline constexpr std::uint8_t kSet = 0x31;
inline constexpr std::uint8_t kConstructed = 0x20;
// [n] context-specific: primitive (IMPLICIT of a primitive type), constructed (EXPLICIT, or IMPLICIT of
// a constructed type).
[[nodiscard]] constexpr std::uint8_t context(std::uint8_t n) { return static_cast<std::uint8_t>(0x80U | n); }
[[nodiscard]] constexpr std::uint8_t context_constructed(std::uint8_t n) { return static_cast<std::uint8_t>(0xA0U | n); }

// How deep elements may nest (BER's indefinite lengths are found by walking what they hold).
inline constexpr int kMaxDepth = 32;

struct Element {
  std::uint8_t tag = 0;
  std::span<const std::byte> content;  // without the end-of-contents of an indefinite length
  std::span<const std::byte> whole;    // tag and length included

  [[nodiscard]] bool constructed() const noexcept { return (tag & kConstructed) != 0; }
};

// Reads the elements of a content one after the other.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data, int depth = 0) noexcept : data_(data), depth_(depth) {}
  // The content of an element, read at its depth + 1.
  [[nodiscard]] static Result<Reader> into(const Element& element, int depth = 0);

  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
  [[nodiscard]] int depth() const noexcept { return depth_; }
  // The next element, whatever its tag.
  [[nodiscard]] Result<Element> next();
  // The next element, which must have this tag.
  [[nodiscard]] Result<Element> expect(std::uint8_t tag);
  // The next element when it has this tag (else none, and nothing is read).
  [[nodiscard]] Result<std::optional<Element>> next_if(std::uint8_t tag);
  // The tag of the next element, without reading it (none at the end).
  [[nodiscard]] std::optional<std::uint8_t> peek() const noexcept;

 private:
  std::span<const std::byte> data_;
  int depth_ = 0;
};

// The bytes of a string type, its segments joined when constructed (BER).
[[nodiscard]] Result<Bytes> string_bytes(const Element& element, int depth = 0);
// An OBJECT IDENTIFIER's content in dotted form ("1.2.840.113549.1.7.2"); empty when malformed.
[[nodiscard]] std::string oid_text(std::span<const std::byte> content);
// A non-negative INTEGER that fits 64 bits.
[[nodiscard]] Result<std::uint64_t> small_integer(const Element& element);
// UTCTime or GeneralizedTime (in UTC, "Z") as a PDF date ("D:20260704123000Z").
[[nodiscard]] Result<std::string> time_text(const Element& element);

// Writing (DER).
[[nodiscard]] Bytes tlv(std::uint8_t tag, std::span<const std::byte> content);
[[nodiscard]] Bytes tlv(std::uint8_t tag, std::initializer_list<std::span<const std::byte>> parts);
[[nodiscard]] Bytes sequence(std::initializer_list<std::span<const std::byte>> parts);
// SET OF: the elements sorted by their encodings.
[[nodiscard]] Bytes set_of(std::vector<Bytes> elements);
[[nodiscard]] Bytes oid(std::string_view dotted);
// An INTEGER from an unsigned big-endian magnitude (a sign byte added when needed).
[[nodiscard]] Bytes integer(std::span<const std::byte> magnitude);
[[nodiscard]] Bytes integer(std::uint64_t value);
[[nodiscard]] Bytes octet_string(std::span<const std::byte> content);
[[nodiscard]] Bytes null();
[[nodiscard]] Bytes boolean(bool value);
// GeneralizedTime from a PDF date ("D:YYYYMMDDHHmmSS" and an offset, §7.9.4), in UTC.
[[nodiscard]] Result<Bytes> generalized_time(std::string_view pdf_date);

[[nodiscard]] std::span<const std::byte> bytes_of(std::string_view text) noexcept;
[[nodiscard]] std::string_view chars_of(std::span<const std::byte> bytes) noexcept;

}  // namespace capriole::crypto::der
