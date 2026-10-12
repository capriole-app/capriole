// Fuzz target for shadings (content/shading and graphics/shading): the input's first four bytes choose a
// shading type (1 to 7) and the bits per coordinate, component and flag; the rest is the mesh data (or,
// for types 1 to 3, the /Coords and /Domain numbers). It is put in a one-page document whose content
// paints the shading, which is interpreted and drawn at a low resolution. Seed corpus: fuzz/corpus/shading/.
//
// Invariants beyond "no crash, no UB, no hang": a drawn page is an opaque bitmap of its size.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/document/document.hpp"
#include "capriole/render/raster.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    __builtin_trap();
  }
}

constexpr std::array<int, 8> kCoordinateBits = {1, 2, 4, 8, 12, 16, 24, 32};
constexpr std::array<int, 6> kComponentBits = {1, 2, 4, 8, 12, 16};
constexpr std::array<int, 3> kFlagBits = {2, 4, 8};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) {
    return 0;
  }
  const int type = 1 + (data[0] % 7);
  const int coordinate_bits = kCoordinateBits.at(data[1] % kCoordinateBits.size());
  const int component_bits = kComponentBits.at(data[2] % kComponentBits.size());
  const int flag_bits = kFlagBits.at(data[3] % kFlagBits.size());
  const std::string payload(reinterpret_cast<const char*>(data + 4), size - 4);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

  std::string shading;
  const std::string function = "/Function<</FunctionType 2/Domain[0 1]/C0[1 0 0]/C1[0 0 1]/N 1.5>>";
  if (type <= 3) {
    // The payload as the numbers of the geometry (whatever they parse as).
    const char* key = type == 1 ? "/Domain[" : "/Coords[";
    shading = "<</ShadingType " + std::to_string(type) + "/ColorSpace/DeviceRGB" + key + payload + "]/Extend[true true]" +
              (type == 1 ? "/Function<</FunctionType 4/Domain[0 1 0 1]/Range[0 1 0 1 0 1]>>" : function) + ">>";
  } else {
    shading = "<</ShadingType " + std::to_string(type) + "/ColorSpace/DeviceRGB/BitsPerCoordinate " + std::to_string(coordinate_bits) + "/BitsPerComponent " +
              std::to_string(component_bits) + "/BitsPerFlag " + std::to_string(flag_bits) + "/VerticesPerRow 3" +
              "/Decode[-10 110 -10 110 0 1 0 1 0 1]/Length " + std::to_string(payload.size()) + ">>\nstream\n" + payload + "\nendstream";
  }
  const std::string pdf =
      "%PDF-1.7\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 100 100]>>endobj\n"
      "3 0 obj<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<</Shading<</S 5 0 R>>>>>>endobj\n"
      "4 0 obj<</Length 21>>stream\n0 0 50 50 re W n /S sh\nendstream endobj\n5 0 obj" +
      shading + "endobj\ntrailer<</Root 1 0 R>>\n%%EOF\n";
  std::vector<std::byte> bytes(pdf.size());
  for (std::size_t i = 0; i < pdf.size(); ++i) {
    bytes[i] = static_cast<std::byte>(pdf[i]);
  }
  capriole::OpenOptions options;
  options.repair = true;
  auto document = capriole::Document::open(std::make_unique<capriole::MemoryByteSource>(std::move(bytes)), options);
  if (!document) {
    return 0;
  }
  auto pages = document->page_refs();
  if (!pages || pages->empty()) {
    return 0;
  }
  auto list = capriole::content::interpret_page(*document, pages->front());
  if (!list) {
    return 0;
  }
  if (auto bitmap = capriole::render::rasterize(*list, 0.5)) {
    check(bitmap->rgba.size() == static_cast<std::size_t>(bitmap->width) * static_cast<std::size_t>(bitmap->height) * 4);
  }
  return 0;
}
