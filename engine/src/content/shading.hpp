#pragma once

// Internal to the interpreter: shading dictionaries and streams (ISO 32000-2 §8.7.4.5, the seven types)
// prepared for drawing as graphics::Shading — colours through the shading's function and colour space
// into sRGB, sampled along t (axial, radial, meshes with a function) or on a grid (function-based);
// free-form and lattice meshes as triangles, Coons and tensor-product patches subdivided into them.

#include <cstddef>
#include <memory>

#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "color_space.hpp"

namespace capriole::content::detail {

struct ShadingLimits {
  std::size_t ramp_samples = 1024;        // colours along t
  int grid_samples = 256;                 // function shadings: a grid of grid × grid colours
  std::size_t max_vertices = 4'000'000;   // read from a mesh stream
  std::size_t max_triangles = 2'000'000;  // after patches are subdivided
};

// The shading of `sh` or of a shading pattern. scale is the length of a unit of shading space in points
// (the CTM's), for splitting patches finely enough; nullptr, with a warning, when the shading is unusable.
[[nodiscard]] std::shared_ptr<const graphics::Shading> load_shading(Document& document, const model::Object& object, const model::Dict* resources, double scale,
                                                                    Warnings& warnings, const ShadingLimits& limits = {}, ColorContext* colors = nullptr);

}  // namespace capriole::content::detail
