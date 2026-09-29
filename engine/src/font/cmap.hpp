#pragma once

// CMaps (ISO 32000-2 §9.7.5 and Adobe Technical Note #5014): how the bytes of a string shown with a
// Type 0 font split into character codes — the codespace ranges, 1 to 4 bytes — and which CID each
// code selects (cidrange, cidchar, notdefrange, notdefchar); for a ToUnicode CMap (§9.10.3), which
// text a code stands for (bfrange, bfchar). The same parser reads embedded CMap streams and Adobe's
// predefined CMaps (the resource pack, ADR 0005), which use the same syntax.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::font::detail {

struct CMapLimits {
  // Mappings of one CMap (ranges count once, whatever their size) and CMaps chained by usecmap.
  std::size_t max_mappings = 1'000'000;
  std::size_t max_depth = 8;
  // Text of one bfchar/bfrange destination, in UTF-16 code units (ligatures are a few; this is a bound).
  std::size_t max_text_units = 256;
};

class CMap {
 public:
  struct Code {
    std::uint32_t code = 0;
    std::uint8_t length = 1;  // bytes
    std::uint32_t cid = 0;
  };

  // The CMap a usecmap operator (or a /UseCMap entry) names, parsed at depth (its place in the chain,
  // which bounds cycles); nullptr when it is unknown.
  using Resolver = std::function<std::shared_ptr<const CMap>(std::string_view name, std::size_t depth)>;

  // Identity-H and Identity-V: two bytes, CID = code.
  [[nodiscard]] static std::shared_ptr<const CMap> identity(bool vertical);

  // A CMap from its text. Tolerant of what does not matter (comments, dictionaries, unknown operators);
  // an error only when nothing usable is in it. depth counts the CMaps above this one in a usecmap chain.
  [[nodiscard]] static Result<CMap> parse(std::string_view text, const Resolver& resolve, const CMapLimits& limits = {}, std::size_t depth = 0);

  // The next code of bytes from `at` (§9.7.6.2), advancing `at` past it. A byte sequence that matches no
  // codespace range takes the length of the shortest range its first byte fits, and CID 0 (§9.7.6.3).
  [[nodiscard]] Code next(std::string_view bytes, std::size_t& at) const;

  // Vertical writing (/WMode 1).
  [[nodiscard]] bool vertical() const noexcept { return vertical_; }
  // A usecmap parent may be given after parsing (/UseCMap of an embedded CMap stream).
  void set_parent(std::shared_ptr<const CMap> parent) { parent_ = std::move(parent); }
  void set_vertical(bool vertical) noexcept { vertical_ = vertical; }

  // The text of a code in a ToUnicode CMap (UTF-32); empty when it has none.
  [[nodiscard]] std::u32string text(std::uint32_t code, std::uint8_t length) const;

  // Every code → CID mapping (ranges expanded), for building the reverse of a Unicode CMap. Stops when
  // f returns false.
  void for_each_cid(const std::function<bool(std::uint32_t code, std::uint8_t length, std::uint32_t cid)>& f) const;

  // The CID of a code, mapped or notdef; nullopt when this CMap and its parents say nothing about it.
  [[nodiscard]] std::optional<std::uint32_t> cid(std::uint32_t code, std::uint8_t length) const;

  struct Codespace {
    std::uint8_t length = 1;
    std::array<std::uint8_t, 4> low{};
    std::array<std::uint8_t, 4> high{};
  };
  struct Range {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint8_t length = 1;
    std::uint32_t value = 0;  // the CID of `low`
  };
  struct TextRange {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint8_t length = 1;
    std::u32string text;  // the text of `low`; its last character increases along the range
  };

 private:
  [[nodiscard]] bool in_codespace(std::uint32_t code, std::uint8_t length) const;
  [[nodiscard]] bool has_codespace() const;
  [[nodiscard]] std::uint8_t fallback_length(std::uint8_t first) const;

  bool identity_ = false;
  bool vertical_ = false;
  std::vector<Codespace> codespace_;
  std::vector<Range> cids_;      // sorted by (length, low)
  std::vector<Range> notdefs_;   // sorted by (length, low)
  std::vector<TextRange> text_;  // sorted by (length, low)
  std::shared_ptr<const CMap> parent_;
};

}  // namespace capriole::font::detail
