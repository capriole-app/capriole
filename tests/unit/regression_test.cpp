// Regression suite (DOCS/CAPRIOLE.md §7.4): every input that once crashed or misbehaved is kept under
// tests/regression/<area>/ and replayed here through the same code path that found it.
//
//   *.pdf           opened as a Document; every object loaded, every stream decoded, page tree walked
//   security/*.bin  an /Encrypt dictionary followed by data, fed to the standard security handler
//   render/*        a document whose first pages are interpreted and drawn, as fuzz_render does
//
// A case passes when it does not crash: under ASan/UBSan any memory error aborts the test binary.
// Errors are expected and fine — these inputs are hostile by construction.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/content/interpreter.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/document/document.hpp"
#include "capriole/parser/object_parser.hpp"
#include "capriole/render/raster.hpp"

namespace capriole {
namespace {

const std::filesystem::path kRegressionDir = std::filesystem::path(CAPRIOLE_TEST_DATA_DIR).parent_path() / "regression";

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    out[i] = static_cast<std::byte>(chars[i]);
  }
  return out;
}

void replay_document(const std::vector<std::byte>& bytes) {
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes));
  if (!document) {
    return;
  }
  for (const auto& [number, entry] : document->xref().entries) {
    auto object = document->get(number);
    if (object && (*object)->as_stream() != nullptr) {
      (void)document->decode_stream(*(*object)->as_stream());
    }
  }
  (void)document->page_refs();
}

// The path of fuzz/fuzz_render.cpp, with its limits: interpret the first pages and draw them small.
void replay_render(const std::vector<std::byte>& bytes) {
  OpenOptions options;
  options.limits.max_nesting_depth = 32;
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes), options);
  if (!document) {
    return;
  }
  auto pages = document->page_refs();
  if (!pages) {
    return;
  }
  content::InterpreterLimits limits;
  limits.parse = options.limits;
  limits.max_operations = 100'000;
  limits.max_commands = 20'000;
  limits.max_form_depth = 8;
  limits.max_image_pixels = 1'000'000;
  limits.max_page_image_pixels = 4'000'000;
  render::RenderLimits render_limits;
  render_limits.max_pixels = 250'000;
  render_limits.max_side = 4'000;
  render_limits.max_layers = 8;
  render_limits.max_dash_segments = 20'000;
  for (std::size_t i = 0; i < pages->size() && i < 3; ++i) {
    if (auto list = content::interpret_page(*document, (*pages)[i], limits)) {
      (void)render::rasterize(*list, 0.25, render_limits);
    }
  }
}

void replay_security(const std::vector<std::byte>& bytes) {
  MemoryByteSource source(bytes);
  SourceReader reader(source);
  parser::ObjectParser parser(reader);
  auto object = parser.parse_object();
  if (!object || object->as_dict() == nullptr) {
    return;
  }
  const auto rest = std::span(bytes).subspan(static_cast<std::size_t>(reader.position()));
  for (const char* password : {"", "user", "owner"}) {
    auto handler = crypto::StandardSecurityHandler::create(*object->as_dict(), {}, password);
    if (handler) {
      (void)handler->decrypt(rest, model::ObjectRef{.number = 7, .generation = 0}, handler->stream_method());
    }
  }
}

TEST(Regression, ReplayAllCases) {
  ASSERT_TRUE(std::filesystem::is_directory(kRegressionDir)) << kRegressionDir;
  std::size_t cases = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(kRegressionDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    SCOPED_TRACE(entry.path().string());
    const auto bytes = read_file(entry.path());
    if (entry.path().parent_path().filename() == "render") {
      replay_render(bytes);
      ++cases;
    } else if (entry.path().extension() == ".pdf") {
      replay_document(bytes);
      ++cases;
    } else if (entry.path().parent_path().filename() == "security" && entry.path().extension() == ".bin") {
      replay_security(bytes);
      ++cases;
    }
  }
  EXPECT_GT(cases, 0U);
}

// The first real bug found by fuzzing (2026-09-26): /V 5 with /R 2-4 asked the MD5 key derivation
// for a 32-byte key, reading past the 16-byte digest. Now a syntax error.
TEST(Regression, EncryptVersionAndRevisionMustMatch) {
  const auto bytes = read_file(kRegressionDir / "security" / "v5_r3_via_document.pdf");
  const auto document = Document::open(std::make_unique<MemoryByteSource>(bytes));
  ASSERT_FALSE(document.has_value());
  EXPECT_EQ(document.error().code, ErrorCode::kSyntax);
}

// fuzz_render, 2026-09-28: CTMs scaled by 1e16 and more (forms drawing themselves scaled, with dashed
// strokes) made Blend2D's stroker read outside the bitmap. Geometry out of range is now clipped in
// double precision before Blend2D sees it (render/raster.cpp, kSafeCoordinate).
TEST(Regression, AstronomicCoordinatesDoNotReachBlend2D) { replay_render(read_file(kRegressionDir / "render" / "astronomic_ctm.pdf")); }

}  // namespace
}  // namespace capriole
