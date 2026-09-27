#pragma once

// Content stream parser (ISO 32000-2 §7.8.2): the operations of a page, a form XObject or a Type 3
// glyph, as operands followed by an operator. Operands are objects (as in the rest of the file, but
// without references); operators are keywords (content/operators.hpp).
//
// Inline images (§8.9.7) — BI, key/value pairs, ID, binary data, EI — come out as ONE operation
// (Operator::kInlineImage) carrying the dictionary and the data. The end of the data is not declared
// before PDF 2.0 (/L), so it is found by scanning for "EI" between white-space, followed by content
// that is not binary: the heuristic the reference readers use.
//
// Strict mode fails at the first syntax error. Tolerant mode — what an interpreter drawing a page
// wants, since broken content streams are common — skips the offending byte, drops the operands
// gathered so far, and goes on; recovered_errors() counts how often.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/content/operators.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/model/object.hpp"
#include "capriole/parser/object_parser.hpp"

namespace capriole::content {

struct InlineImage {
  model::Dict dict;  // as written: abbreviated keys (/W /H /BPC /CS /F ...) are not expanded here
  std::vector<std::byte> data;
};

struct Operation {
  Operator op = Operator::kUnknown;
  std::string name;  // the operator as written (the only way to tell unknown operators apart)
  std::vector<model::Object> operands;
  std::uint64_t offset = 0;                  // of the operator in the content
  std::shared_ptr<const InlineImage> image;  // for Operator::kInlineImage
};

// Operands beyond this many before an operator are an error (strict) or dropped (tolerant): real
// operators take at most 33, and a stream of numbers without operators must not grow without bound.
inline constexpr std::size_t kMaxOperands = 1024;

class ContentParser {
 public:
  // content must outlive the parser (it is read in place, not copied).
  explicit ContentParser(std::span<const std::byte> content, const ParseLimits& limits = kDefaultParseLimits,
                         parser::ParseMode mode = parser::ParseMode::kStrict);
  // Not copyable or movable: the reader and the parser point into the parser's own members.
  ContentParser(const ContentParser&) = delete;
  ContentParser& operator=(const ContentParser&) = delete;
  ContentParser(ContentParser&&) = delete;
  ContentParser& operator=(ContentParser&&) = delete;
  ~ContentParser() = default;

  // The next operation, or nullopt at the end (operands left without an operator are dropped).
  [[nodiscard]] Result<std::optional<Operation>> next();

  // Tolerant mode: syntax errors skipped so far.
  [[nodiscard]] std::size_t recovered_errors() const noexcept { return recovered_; }

 private:
  Result<Operation> read_inline_image(std::uint64_t bi_offset);
  // After a syntax error at offset: skip that byte (tolerant) or report it (strict).
  Result<void> recover(const Error& error);

  std::span<const std::byte> content_;
  SpanByteSource source_;
  SourceReader reader_;
  parser::ObjectParser parser_;
  parser::ParseMode mode_;
  std::size_t recovered_ = 0;
};

// Parses a whole content stream (convenience for tests and tools).
[[nodiscard]] Result<std::vector<Operation>> parse_content(std::span<const std::byte> content, const ParseLimits& limits = kDefaultParseLimits,
                                                           parser::ParseMode mode = parser::ParseMode::kStrict);

}  // namespace capriole::content
