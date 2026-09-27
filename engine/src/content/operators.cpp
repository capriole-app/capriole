#include "capriole/content/operators.hpp"

#include <algorithm>
#include <array>

namespace capriole::content {

namespace {

using enum Operator;
using C = OperatorCategory;

// Colour operators take one operand per colour component: up to 32 for DeviceN (§8.6.6.5, and an
// implementation limit of Annex C), plus a pattern name for SCN/scn.
constexpr std::uint8_t kMaxComponents = 32;

constexpr OperatorInfo entry(std::string_view name, Operator op, OperatorCategory category, std::uint8_t min_operands, std::uint8_t max_operands) {
  return OperatorInfo{.name = name, .op = op, .category = category, .min_operands = min_operands, .max_operands = max_operands};
}

// Sorted by name (byte order), for the binary search in find_operator.
constexpr std::array kOperators = {
    entry("\"", kNextLineSpacingShowText, C::kTextShowing, 3, 3),
    entry("'", kNextLineShowText, C::kTextShowing, 1, 1),
    entry("B", kFillStroke, C::kPathPainting, 0, 0),
    entry("B*", kFillStrokeEvenOdd, C::kPathPainting, 0, 0),
    entry("BDC", kBeginMarkedContentProperties, C::kMarkedContent, 2, 2),
    entry("BI", kInlineImage, C::kInlineImage, 0, 0),
    entry("BMC", kBeginMarkedContent, C::kMarkedContent, 1, 1),
    entry("BT", kBeginText, C::kTextObject, 0, 0),
    entry("BX", kBeginCompatibility, C::kCompatibility, 0, 0),
    entry("CS", kSetStrokeColorSpace, C::kColor, 1, 1),
    entry("DP", kMarkPointProperties, C::kMarkedContent, 2, 2),
    entry("Do", kDoXObject, C::kXObject, 1, 1),
    entry("EMC", kEndMarkedContent, C::kMarkedContent, 0, 0),
    entry("ET", kEndText, C::kTextObject, 0, 0),
    entry("EX", kEndCompatibility, C::kCompatibility, 0, 0),
    entry("F", kFillObsolete, C::kPathPainting, 0, 0),
    entry("G", kSetStrokeGray, C::kColor, 1, 1),
    entry("J", kSetLineCap, C::kGeneralGraphicsState, 1, 1),
    entry("K", kSetStrokeCMYK, C::kColor, 4, 4),
    entry("M", kSetMiterLimit, C::kGeneralGraphicsState, 1, 1),
    entry("MP", kMarkPoint, C::kMarkedContent, 1, 1),
    entry("Q", kRestore, C::kSpecialGraphicsState, 0, 0),
    entry("RG", kSetStrokeRGB, C::kColor, 3, 3),
    entry("S", kStroke, C::kPathPainting, 0, 0),
    entry("SC", kSetStrokeColor, C::kColor, 1, kMaxComponents),
    entry("SCN", kSetStrokeColorN, C::kColor, 1, kMaxComponents + 1),
    entry("T*", kNextLine, C::kTextPositioning, 0, 0),
    entry("TD", kMoveTextSetLeading, C::kTextPositioning, 2, 2),
    entry("TJ", kShowTextArray, C::kTextShowing, 1, 1),
    entry("TL", kSetLeading, C::kTextState, 1, 1),
    entry("Tc", kSetCharSpacing, C::kTextState, 1, 1),
    entry("Td", kMoveText, C::kTextPositioning, 2, 2),
    entry("Tf", kSetFont, C::kTextState, 2, 2),
    entry("Tj", kShowText, C::kTextShowing, 1, 1),
    entry("Tm", kSetTextMatrix, C::kTextPositioning, 6, 6),
    entry("Tr", kSetTextRender, C::kTextState, 1, 1),
    entry("Ts", kSetTextRise, C::kTextState, 1, 1),
    entry("Tw", kSetWordSpacing, C::kTextState, 1, 1),
    entry("Tz", kSetHorizontalScaling, C::kTextState, 1, 1),
    entry("W", kClip, C::kClipping, 0, 0),
    entry("W*", kClipEvenOdd, C::kClipping, 0, 0),
    entry("b", kCloseFillStroke, C::kPathPainting, 0, 0),
    entry("b*", kCloseFillStrokeEvenOdd, C::kPathPainting, 0, 0),
    entry("c", kCurveTo, C::kPathConstruction, 6, 6),
    entry("cm", kConcatMatrix, C::kSpecialGraphicsState, 6, 6),
    entry("cs", kSetFillColorSpace, C::kColor, 1, 1),
    entry("d", kSetDash, C::kGeneralGraphicsState, 2, 2),
    entry("d0", kSetCharWidth, C::kType3Font, 2, 2),
    entry("d1", kSetCacheDevice, C::kType3Font, 6, 6),
    entry("f", kFill, C::kPathPainting, 0, 0),
    entry("f*", kFillEvenOdd, C::kPathPainting, 0, 0),
    entry("g", kSetFillGray, C::kColor, 1, 1),
    entry("gs", kSetExtGState, C::kGeneralGraphicsState, 1, 1),
    entry("h", kClosePath, C::kPathConstruction, 0, 0),
    entry("i", kSetFlatness, C::kGeneralGraphicsState, 1, 1),
    entry("j", kSetLineJoin, C::kGeneralGraphicsState, 1, 1),
    entry("k", kSetFillCMYK, C::kColor, 4, 4),
    entry("l", kLineTo, C::kPathConstruction, 2, 2),
    entry("m", kMoveTo, C::kPathConstruction, 2, 2),
    entry("n", kEndPath, C::kPathPainting, 0, 0),
    entry("q", kSave, C::kSpecialGraphicsState, 0, 0),
    entry("re", kRectangle, C::kPathConstruction, 4, 4),
    entry("rg", kSetFillRGB, C::kColor, 3, 3),
    entry("ri", kSetRenderingIntent, C::kGeneralGraphicsState, 1, 1),
    entry("s", kCloseStroke, C::kPathPainting, 0, 0),
    entry("sc", kSetFillColor, C::kColor, 1, kMaxComponents),
    entry("scn", kSetFillColorN, C::kColor, 1, kMaxComponents + 1),
    entry("sh", kShadingFill, C::kShading, 1, 1),
    entry("v", kCurveToV, C::kPathConstruction, 4, 4),
    entry("w", kSetLineWidth, C::kGeneralGraphicsState, 1, 1),
    entry("y", kCurveToY, C::kPathConstruction, 4, 4),
};

static_assert(std::ranges::is_sorted(kOperators, {}, &OperatorInfo::name), "kOperators must stay sorted by name");

constexpr OperatorInfo kUnknownInfo = entry("", kUnknown, C::kUnknown, 0, 0);

}  // namespace

// By index rather than iterator: std::array's iterator is a pointer in libstdc++ but not in libc++
// (the WebAssembly build), and the code must read the same with both.
const OperatorInfo* find_operator(std::string_view name) noexcept {
  const auto index = static_cast<std::size_t>(std::ranges::lower_bound(kOperators, name, {}, &OperatorInfo::name) - kOperators.begin());
  return index < kOperators.size() && kOperators[index].name == name ? &kOperators[index] : nullptr;
}

const OperatorInfo& info(Operator op) noexcept {
  const auto index = static_cast<std::size_t>(std::ranges::find(kOperators, op, &OperatorInfo::op) - kOperators.begin());
  return index < kOperators.size() ? kOperators[index] : kUnknownInfo;
}

std::string_view to_string(OperatorCategory category) noexcept {
  switch (category) {
    case C::kUnknown:
      return "unknown";
    case C::kGeneralGraphicsState:
      return "general_graphics_state";
    case C::kSpecialGraphicsState:
      return "special_graphics_state";
    case C::kPathConstruction:
      return "path_construction";
    case C::kPathPainting:
      return "path_painting";
    case C::kClipping:
      return "clipping";
    case C::kTextObject:
      return "text_object";
    case C::kTextState:
      return "text_state";
    case C::kTextPositioning:
      return "text_positioning";
    case C::kTextShowing:
      return "text_showing";
    case C::kType3Font:
      return "type3_font";
    case C::kColor:
      return "color";
    case C::kShading:
      return "shading";
    case C::kInlineImage:
      return "inline_image";
    case C::kXObject:
      return "xobject";
    case C::kMarkedContent:
      return "marked_content";
    case C::kCompatibility:
      return "compatibility";
  }
  return "unknown";
}

}  // namespace capriole::content
