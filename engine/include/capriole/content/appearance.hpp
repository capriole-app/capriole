#pragma once

// Appearances of markup annotations (§12.5.6.8–12.5.6.13): the content stream that draws an annotation
// from its own entries — /Rect, /QuadPoints, /InkList, /L, /Vertices, its colours /C and /IC, its border
// /BS or /Border. The interpreter draws annotations that have no appearance with it; the editor
// (edit/annotations.hpp) writes it as the /AP of the annotations it makes, so that every reader shows
// them as Capriole does.

#include <string>
#include <string_view>
#include <vector>

#include "capriole/document/document.hpp"
#include "capriole/graphics/geometry.hpp"
#include "capriole/model/object.hpp"

namespace capriole::content {

// The content, in the page's coordinates (the annotation's own space: /Rect is where it goes); empty
// when there is nothing to draw (no colour, no geometry, a type it does not know).
[[nodiscard]] std::string markup_appearance(Document& document, std::string_view subtype, const model::Dict& annotation, const graphics::Rect& rect);

// The numbers of an array entry, references resolved (0 for what is not a number).
[[nodiscard]] std::vector<double> numbers(Document& document, const model::Dict& dict, std::string_view key);

}  // namespace capriole::content
