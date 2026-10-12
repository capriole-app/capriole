#pragma once

// The content stream operators (ISO 32000-2 §8–§9, §14; Annex A, Table A.1), with the category and
// the number of operands each takes. The parser only recognises them; checking operand counts and
// types is the interpreter's job.

#include <cstdint>
#include <string_view>

namespace capriole::content {

enum class Operator : std::uint8_t {
  kUnknown,  // not an operator of the standard (ignored inside BX/EX, an error elsewhere: §7.8.2)
  // General graphics state (§8.4.4)
  kSetLineWidth,        // w
  kSetLineCap,          // J
  kSetLineJoin,         // j
  kSetMiterLimit,       // M
  kSetDash,             // d
  kSetRenderingIntent,  // ri
  kSetFlatness,         // i
  kSetExtGState,        // gs
  // Special graphics state (§8.4.4)
  kSave,          // q
  kRestore,       // Q
  kConcatMatrix,  // cm
  // Path construction (§8.5.2)
  kMoveTo,     // m
  kLineTo,     // l
  kCurveTo,    // c
  kCurveToV,   // v
  kCurveToY,   // y
  kClosePath,  // h
  kRectangle,  // re
  // Path painting (§8.5.3)
  kStroke,                  // S
  kCloseStroke,             // s
  kFill,                    // f
  kFillObsolete,            // F
  kFillEvenOdd,             // f*
  kFillStroke,              // B
  kFillStrokeEvenOdd,       // B*
  kCloseFillStroke,         // b
  kCloseFillStrokeEvenOdd,  // b*
  kEndPath,                 // n
  // Clipping (§8.5.4)
  kClip,         // W
  kClipEvenOdd,  // W*
  // Text objects (§9.4)
  kBeginText,  // BT
  kEndText,    // ET
  // Text state (§9.3)
  kSetCharSpacing,        // Tc
  kSetWordSpacing,        // Tw
  kSetHorizontalScaling,  // Tz
  kSetLeading,            // TL
  kSetFont,               // Tf
  kSetTextRender,         // Tr
  kSetTextRise,           // Ts
  // Text positioning (§9.4.2)
  kMoveText,            // Td
  kMoveTextSetLeading,  // TD
  kSetTextMatrix,       // Tm
  kNextLine,            // T*
  // Text showing (§9.4.3)
  kShowText,                 // Tj
  kShowTextArray,            // TJ
  kNextLineShowText,         // '
  kNextLineSpacingShowText,  // "
  // Type 3 fonts (§9.6.4)
  kSetCharWidth,    // d0
  kSetCacheDevice,  // d1
  // Colour (§8.6.8)
  kSetStrokeColorSpace,  // CS
  kSetFillColorSpace,    // cs
  kSetStrokeColor,       // SC
  kSetStrokeColorN,      // SCN
  kSetFillColor,         // sc
  kSetFillColorN,        // scn
  kSetStrokeGray,        // G
  kSetFillGray,          // g
  kSetStrokeRGB,         // RG
  kSetFillRGB,           // rg
  kSetStrokeCMYK,        // K
  kSetFillCMYK,          // k
  // Shading (§8.7.4.2)
  kShadingFill,  // sh
  // Inline images (§8.9.7): BI ... ID data EI, returned by the parser as one operation.
  kInlineImage,  // BI
  // XObjects (§8.8)
  kDoXObject,  // Do
  // Marked content (§14.6)
  kMarkPoint,                     // MP
  kMarkPointProperties,           // DP
  kBeginMarkedContent,            // BMC
  kBeginMarkedContentProperties,  // BDC
  kEndMarkedContent,              // EMC
  // Compatibility (§7.8.2)
  kBeginCompatibility,  // BX
  kEndCompatibility,    // EX
};

enum class OperatorCategory : std::uint8_t {
  kUnknown,
  kGeneralGraphicsState,
  kSpecialGraphicsState,
  kPathConstruction,
  kPathPainting,
  kClipping,
  kTextObject,
  kTextState,
  kTextPositioning,
  kTextShowing,
  kType3Font,
  kColor,
  kShading,
  kInlineImage,
  kXObject,
  kMarkedContent,
  kCompatibility,
};

struct OperatorInfo {
  std::string_view name;  // as written in content streams
  Operator op;
  OperatorCategory category;
  std::uint8_t min_operands;
  std::uint8_t max_operands;  // equal to min for all but SC/sc/SCN/scn (colour components)
};

// The operator written as name, or nullptr for a name the standard does not define. "ID" and "EI" are
// not listed: they are part of the inline image syntax, which the parser reads as a whole.
[[nodiscard]] const OperatorInfo* find_operator(std::string_view name) noexcept;

// The information about op (the kUnknown entry for kUnknown).
[[nodiscard]] const OperatorInfo& info(Operator op) noexcept;

// A stable snake_case name for the category ("path_painting", ...), for tools and the C API.
[[nodiscard]] std::string_view to_string(OperatorCategory category) noexcept;

}  // namespace capriole::content
