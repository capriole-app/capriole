#pragma once

// Internal to the interpreter: decoding image XObjects and inline images (ISO 32000-2 §8.9) into
// straight RGBA — samples of 1 to 16 bits in any colour space the interpreter knows, /Decode arrays,
// stencil masks painted with the fill colour, and transparency from /SMask (§11.6.5.3) or /Mask
// (colour key or explicit mask, §8.9.6). The stream's filters are run by the caller (JPEG included,
// filters/dct.hpp); the JPX, JBIG2 and CCITT codecs are not implemented yet and give kUnsupported there.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "color_space.hpp"

namespace capriole::content::detail {

struct ImageLimits {
  std::int64_t max_pixels = 16'000'000;  // per image (4 bytes each once decoded), masks included
};

// The pixels of an image. dict is the image dictionary — inline images with their abbreviated keys
// and names (/W, /CS /RGB, ...) — and data its samples, filters already undone. fill is the colour a
// stencil mask (/ImageMask true) paints with.
[[nodiscard]] Result<std::shared_ptr<const graphics::ImagePixels>> decode_image(Document& document, const model::Dict& dict, const std::vector<std::byte>& data,
                                                                                const model::Dict* resources, graphics::Rgb fill, Warnings& warnings,
                                                                                const ImageLimits& limits = {});

// An inline image dictionary with its abbreviations expanded (§8.9.7, Tables 91 and 92), so that the
// filter chain and decode_image read it like an XObject's.
[[nodiscard]] model::Dict expand_inline_dict(const model::Dict& dict);

}  // namespace capriole::content::detail
