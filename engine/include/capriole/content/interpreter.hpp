#pragma once

// Content stream interpreter (ISO 32000-2 §8–§9): runs the operations of a page (and the form
// XObjects it draws) against a graphics state and records what gets painted as a display list
// (graphics/display_list.hpp), independent of any rasterizer.
//
// Done here: graphics state and its stack (q/Q, cm, line style, ExtGState: line style, alpha, font),
// paths, painting and clipping, colour spaces (Device*, Cal*, ICCBased by component count, Indexed,
// Lab; Separation and DeviceN approximated until PDF functions exist), text state and positioning with
// the glyph widths of the font dictionary (simple fonts, and Type0 fonts with Identity CMaps), form
// XObjects (matrix, bounding box clip, own resources, recursion bounded and cycle-safe), image
// XObjects, images decoded to pixels (JPEG included; JPX, JBIG2, CCITT not yet), shadings as placed
// commands; transparency (§11): blend modes, soft masks
// (their group drawn as commands between BeginSoftMask and EndSoftMask) and transparency groups.
// Shadings, glyph outlines and patterns belong to the renderer phases.
//
// Content is read tolerantly, as viewers do: a malformed operation is skipped with a warning, never a
// failure of the page. Only a page that cannot be reached at all is an error.

#include <cstddef>
#include <cstdint>
#include <string>

#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"

namespace capriole::content {

struct InterpreterLimits {
  ParseLimits parse = kDefaultParseLimits;
  // Operations executed per page, forms included (a form drawn a million times is still bounded).
  std::size_t max_operations = 20'000'000;
  // Commands recorded per page: the display list's memory.
  std::size_t max_commands = 5'000'000;
  // Form XObjects inside form XObjects.
  std::size_t max_form_depth = 32;
  // q without Q (§8.4.2 puts no limit; Acrobat's was 28, real files go deeper).
  std::size_t max_save_depth = 1024;
  // Decoded image pixels (4 bytes each): per image, and for all the images of the page.
  std::int64_t max_image_pixels = 16'000'000;
  std::int64_t max_page_image_pixels = 64'000'000;
};

// The display list of page (a page object of the document). Errors only when the page itself cannot
// be read; everything inside is best effort, with warnings.
[[nodiscard]] Result<graphics::DisplayList> interpret_page(Document& document, model::ObjectRef page, const InterpreterLimits& limits = {});

}  // namespace capriole::content
