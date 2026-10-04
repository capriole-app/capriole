#pragma once

// Redaction (PLAN.md phase 8g, ISO 32000-2 §12.5.6.23): what lies in areas of a page is taken out of the
// file — not covered. The page's content (and the form XObjects it draws, copied when they change, so
// that other pages keep theirs) is rewritten without it:
// - text: every glyph in an area (its centre, or a fifth of its box: a glyph of the next line whose box
//   grazes the area stays) leaves its string; the glyphs after it keep their places
//   (a TJ displacement stands for each glyph taken out). Invisible text (a scan's text layer) too; a
//   marked-content sequence that lost glyphs loses its /ActualText, /Alt and /E, and so does the
//   structure element of its /MCID (with its ancestors) in a tagged document.
// - images: one inside an area goes; one an area crosses is written anew (lossless, Flate) with the
//   pixels in the area painted over (the fill colour, else white; an image mask's left unpainted), so its
//   original data — JPEG, JPEG 2000 — is gone from the page: in its own colour space when its samples
//   are 8-bit gray, RGB, CMYK or ICC without /Decode or /Mask (a soft mask is written anew too, opaque in
//   the area); else in RGB as the engine draws it (other
//   readers may show its colours a little otherwise: a limit). An image the engine cannot decode goes.
// - paths: one (nearly) all in an area — or half in one it is not larger than: a glyph drawn as a path,
//   a mark — is not painted (its painting operator becomes n: a clip it sets stays). A larger one an area
//   crosses — a background, a rule, an overlay — is painted clipped to outside the areas, so that the page
//   keeps its look around them; its geometry stays in the file (a limit), and so does one that sets a
//   clip itself (painted as it was).
// - soft masks (the content of a luminosity group set by gs) are not rewritten: a limit.
// - annotations meeting an area go (form widgets with their place in the form).
// Then, optionally, the areas are filled with a colour. The /Redact annotations of a page (marks made
// first, applied later, as Acrobat does) give their areas (/QuadPoints, else /Rect) and fill (/IC).
//
// A redacted document is saved fully, without the objects nothing references any more
// (Document::redacted(): an incremental save is refused, as it would keep the original bytes).

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"

namespace capriole::edit {

struct RedactionOptions {
  // The colour the areas are filled with afterwards (RGB, 0..1); none: not filled.
  std::optional<std::array<double, 3>> fill = std::array<double, 3>{0, 0, 0};
};

struct RedactionReport {
  std::uint32_t glyphs = 0;          // taken out of strings
  std::uint32_t paths = 0;           // not painted any more
  std::uint32_t images_removed = 0;  // gone
  std::uint32_t images_cleared = 0;  // written anew with the areas painted over
  std::uint32_t annotations = 0;     // removed
};

// Redacts areas of page page_index (from 0), in the page's coordinates as shown (edit/annotations.hpp).
// kUnsupported for no area, or an area that is not finite and non-empty; as for every edit, a document
// whose structure needed repairs is refused (edit/pages.hpp).
[[nodiscard]] Result<RedactionReport> redact(Document& document, std::uint32_t page_index, std::span<const graphics::Rect> areas,
                                             const RedactionOptions& options = {});

// Applies the /Redact annotations of page page_index (their areas, their /IC fill, none when they have
// none) and removes them. kOutOfRange when the page has none.
[[nodiscard]] Result<RedactionReport> apply_redactions(Document& document, std::uint32_t page_index);

}  // namespace capriole::edit
