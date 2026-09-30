#pragma once

// Internal to the interpreter: decoding image XObjects and inline images (ISO 32000-2 §8.9) into
// straight RGBA — samples of 1 to 16 bits in any colour space the interpreter knows, /Decode arrays,
// stencil masks painted with the fill colour, and transparency from /SMask (§11.6.5.3) or /Mask
// (colour key or explicit mask, §8.9.6). The stream's filters are run by the caller (JPEG, CCITT and
// JBIG2 included); JPEG 2000, which describes itself, is decode_jpx_image's.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/graphics/display_list.hpp"
#include "capriole/model/object.hpp"
#include "color_space.hpp"

namespace capriole::content::detail {

struct ImageLimits {
  // Per image (4 bytes each once decoded): a larger image is read at a reduced size, every s-th sample
  // of every s-th row, rather than refused.
  std::int64_t max_pixels = 16'000'000;
};

// The pixels of an image. dict is the image dictionary — inline images with their abbreviated keys
// and names (/W, /CS /RGB, ...) — and data its samples, filters already undone. fill is the colour a
// stencil mask (/ImageMask true) paints with.
[[nodiscard]] Result<std::shared_ptr<const graphics::ImagePixels>> decode_image(Document& document, const model::Dict& dict, const std::vector<std::byte>& data,
                                                                                const model::Dict* resources, graphics::Rgb fill, Warnings& warnings,
                                                                                const ImageLimits& limits = {}, ColorContext* colors = nullptr);

// Whether the image's data is JPEG 2000 (/Filter JPXDecode, alone): such data describes itself, and is
// decoded by decode_jpx_image from its bytes as stored.
[[nodiscard]] bool is_jpx(Document& document, const model::Dict& dict);

// The pixels of a JPEG 2000 image (§7.4.9): its own colour space unless the dictionary gives a fitting
// one, its opacity channel as the soft mask with /SMaskInData, then /SMask or /Mask as for any image.
// Too large, it is decoded at a lower resolution.
[[nodiscard]] Result<std::shared_ptr<const graphics::ImagePixels>> decode_jpx_image(Document& document, const model::Dict& dict,
                                                                                    std::span<const std::byte> data, const model::Dict* resources,
                                                                                    Warnings& warnings, const ImageLimits& limits = {},
                                                                                    ColorContext* colors = nullptr);

// An inline image dictionary with its abbreviations expanded (§8.9.7, Tables 91 and 92), so that the
// filter chain and decode_image read it like an XObject's.
[[nodiscard]] model::Dict expand_inline_dict(const model::Dict& dict);

}  // namespace capriole::content::detail
