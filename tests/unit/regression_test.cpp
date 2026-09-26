// Regression suite (DOCS/CAPRIOLE.md §7.4): every input that once crashed or misbehaved is kept under
// tests/regression/<area>/ and replayed here through the same code path that found it.
//
//   *.pdf           opened as a Document; every object loaded, every stream decoded, page tree walked
//   security/*.bin  an /Encrypt dictionary followed by data, fed to the standard security handler
//
// A case passes when it does not crash: under ASan/UBSan any memory error aborts the test binary.
// Errors are expected and fine — these inputs are hostile by construction.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capriole/core/source_reader.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/document/document.hpp"
#include "capriole/parser/object_parser.hpp"

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
    if (entry.path().extension() == ".pdf") {
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

}  // namespace
}  // namespace capriole
