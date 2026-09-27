#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "capriole/crypto/security_handler.hpp"
#include "capriole/document/document.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"

namespace capriole {
namespace {

// Fixtures made by tests/data/encrypted/make_fixtures.sh (qpdf): the same one-page document
// encrypted with every revision of the standard security handler.
const std::string kDir = std::string(CAPRIOLE_TEST_DATA_DIR) + "/encrypted/";

Result<Document> open(const std::string& name, const std::string& password) {
  auto source = FileByteSource::open(kDir + name);
  if (!source) {
    return std::unexpected(source.error());
  }
  return Document::open(std::move(*source), OpenOptions{.limits = kDefaultParseLimits, .password = password});
}

std::string as_text(const std::vector<std::byte>& data) {
  std::string out;
  for (const std::byte b : data) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

// Everything the fixture document holds, read through decryption.
void expect_decrypted_content(Document& document) {
  const auto pages = document.page_refs();
  ASSERT_TRUE(pages.has_value()) << pages.error().message;
  ASSERT_EQ(pages->size(), 1U);

  const auto page = document.get(pages->front());
  ASSERT_TRUE(page.has_value());
  const auto contents = document.resolve(*(*page)->as_dict()->find("Contents"));
  ASSERT_TRUE(contents.has_value());
  ASSERT_NE(contents->as_stream(), nullptr);
  const auto data = document.decode_stream(*contents->as_stream());
  ASSERT_TRUE(data.has_value()) << data.error().message;
  EXPECT_EQ(as_text(*data), "BT /F1 12 Tf 72 720 Td (Hello encrypted) Tj ET");

  // A string: /Info /Title is encrypted in the file.
  const auto info = document.resolve(*document.trailer().find("Info"));
  ASSERT_TRUE(info.has_value());
  ASSERT_NE(info->as_dict(), nullptr);
  EXPECT_EQ(info->as_dict()->find("Title")->as_string()->bytes, "Capriole test");

  const auto catalog = document.catalog();
  ASSERT_TRUE(catalog.has_value());
  const auto metadata = document.resolve(*(*catalog)->find("Metadata"));
  ASSERT_TRUE(metadata.has_value());
  const auto xmp = document.decode_stream(*metadata->as_stream());
  ASSERT_TRUE(xmp.has_value()) << xmp.error().message;
  EXPECT_NE(as_text(*xmp).find("x:xmpmeta"), std::string::npos);
}

struct Fixture {
  const char* file;
  int revision;
};

class EncryptedFixture : public ::testing::TestWithParam<Fixture> {};

TEST_P(EncryptedFixture, OpensWithUserPassword) {
  auto document = open(GetParam().file, "user");
  ASSERT_TRUE(document.has_value()) << document.error().message;
  ASSERT_NE(document->security(), nullptr);
  EXPECT_EQ(document->security()->revision(), GetParam().revision);
  EXPECT_FALSE(document->security()->owner_authenticated());
  expect_decrypted_content(*document);
}

TEST_P(EncryptedFixture, OpensWithOwnerPassword) {
  auto document = open(GetParam().file, "owner");
  ASSERT_TRUE(document.has_value()) << document.error().message;
  EXPECT_TRUE(document->security()->owner_authenticated());
  expect_decrypted_content(*document);
}

TEST_P(EncryptedFixture, WrongOrMissingPasswordIsRejected) {
  EXPECT_EQ(open(GetParam().file, "wrong").error().code, ErrorCode::kPasswordRequired);
  EXPECT_EQ(open(GetParam().file, "").error().code, ErrorCode::kPasswordRequired);
}

TEST_P(EncryptedFixture, EncryptIsTheInverseOfDecrypt) {
  auto document = open(GetParam().file, "user");
  ASSERT_TRUE(document.has_value()) << document.error().message;
  const crypto::StandardSecurityHandler& handler = *document->security();
  const model::ObjectRef ref{.number = 7, .generation = 0};
  for (const crypto::CryptMethod method : {handler.string_method(), handler.stream_method()}) {
    for (const std::size_t size : {0U, 1U, 15U, 16U, 17U, 100U}) {
      std::vector<std::byte> plain(size);
      for (std::size_t i = 0; i < size; ++i) {
        plain[i] = static_cast<std::byte>(i * 7 + 3);
      }
      const auto encrypted = handler.encrypt(plain, ref, method);
      ASSERT_TRUE(encrypted.has_value()) << encrypted.error().message;
      EXPECT_EQ(*handler.decrypt(*encrypted, ref, method), plain) << size;
      EXPECT_EQ(*handler.encrypt(plain, ref, method), *encrypted);  // deterministic
      if (method == crypto::CryptMethod::kAesV2 || method == crypto::CryptMethod::kAesV3) {
        EXPECT_EQ(encrypted->size(), 16 + (size / 16 + 1) * 16);  // IV + padded data
      }
      if (size > 0) {
        EXPECT_NE(*handler.encrypt(plain, model::ObjectRef{.number = 8, .generation = 0}, method), *encrypted);  // per object
      }
    }
  }
}

std::string save_bytes(Document& document, bool incremental) {
  writer::MemorySink sink;
  const auto report = incremental ? writer::save_incremental(document, sink) : writer::save_full(document, sink);
  EXPECT_TRUE(report.has_value()) << (report ? "" : report.error().message);
  return as_text(sink.data());
}

Result<Document> open_bytes(const std::string& pdf, const std::string& password) {
  std::vector<std::byte> data;
  for (const char c : pdf) {
    data.push_back(static_cast<std::byte>(c));
  }
  return Document::open(std::make_unique<MemoryByteSource>(std::move(data)), OpenOptions{.limits = kDefaultParseLimits, .password = password, .repair = false});
}

TEST_P(EncryptedFixture, FullSaveKeepsTheEncryption) {
  auto document = open(GetParam().file, "user");
  ASSERT_TRUE(document.has_value()) << document.error().message;
  const std::string out = save_bytes(*document, false);
  EXPECT_EQ(out.find("Capriole test"), std::string::npos);  // the /Title is not written in clear
  EXPECT_EQ(open_bytes(out, "wrong").error().code, ErrorCode::kPasswordRequired);
  for (const char* password : {"user", "owner"}) {
    auto saved = open_bytes(out, password);  // strict reading: the output conforms
    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_EQ(saved->security()->revision(), GetParam().revision);
    expect_decrypted_content(*saved);
  }
}

TEST_P(EncryptedFixture, IncrementalSaveEncryptsWhatItAppends) {
  auto document = open(GetParam().file, "user");
  ASSERT_TRUE(document.has_value()) << document.error().message;
  const auto info_ref = document->trailer().find("Info")->as_ref();
  model::Dict info = *(*document->get(*info_ref))->as_dict();
  info.set("Subject", model::String{.bytes = "Appended secret"});
  ASSERT_TRUE(document->set(info_ref->number, info).has_value());
  const std::string out = save_bytes(*document, true);
  EXPECT_EQ(out.find("Appended secret"), std::string::npos);
  auto saved = open_bytes(out, "user");
  ASSERT_TRUE(saved.has_value()) << saved.error().message;
  EXPECT_EQ(saved->xref().sections.size(), document->xref().sections.size() + 1);
  const auto saved_info = saved->resolve(*saved->trailer().find("Info"));
  EXPECT_EQ(saved_info->as_dict()->find("Subject")->as_string()->bytes, "Appended secret");
  expect_decrypted_content(*saved);
}

INSTANTIATE_TEST_SUITE_P(AllRevisions, EncryptedFixture,
                         ::testing::Values(Fixture{"r2_rc4_40.pdf", 2}, Fixture{"r3_rc4_128.pdf", 3}, Fixture{"r4_aes_128.pdf", 4},
                                           Fixture{"r4_aes_128_cleartext_metadata.pdf", 4}, Fixture{"r4_aes_128_object_streams.pdf", 4},
                                           Fixture{"r5_aes_256.pdf", 5}, Fixture{"r6_aes_256.pdf", 6}, Fixture{"r6_aes_256_object_streams.pdf", 6}),
                         [](const auto& test_info) {
                           std::string name = test_info.param.file;
                           return name.substr(0, name.find('.'));
                         });

TEST(Encryption, EmptyUserPasswordOpensWithoutAPassword) {
  for (const char* file : {"r4_aes_128_empty_user.pdf", "r6_aes_256_empty_user.pdf"}) {
    auto document = open(file, "");
    ASSERT_TRUE(document.has_value()) << file << ": " << document.error().message;
    EXPECT_FALSE(document->security()->owner_authenticated());
    expect_decrypted_content(*document);
    EXPECT_TRUE(open(file, "owner")->security()->owner_authenticated()) << file;
  }
}

TEST(Encryption, CleartextMetadataIsNotEncrypted) {
  auto document = open("r4_aes_128_cleartext_metadata.pdf", "user");
  ASSERT_TRUE(document.has_value());
  EXPECT_FALSE(document->security()->encrypt_metadata());
}

// ── Security handler parameter validation ───────────────────────────────────────────────────

model::Dict standard_dict(int v, int r) {
  model::Dict dict;
  dict.set("Filter", model::Name{"Standard"});
  dict.set("V", v);
  dict.set("R", r);
  dict.set("P", -4);
  dict.set("O", model::String{.bytes = std::string(32, 'o')});
  dict.set("U", model::String{.bytes = std::string(32, 'u')});
  return dict;
}

TEST(SecurityHandler, RejectsWhatItDoesNotImplement) {
  model::Dict public_key = standard_dict(2, 3);
  public_key.set("Filter", model::Name{"Adobe.PubSec"});
  EXPECT_EQ(crypto::StandardSecurityHandler::create(public_key, {}, "").error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(crypto::StandardSecurityHandler::create(standard_dict(3, 3), {}, "").error().code, ErrorCode::kUnsupported);
  EXPECT_EQ(crypto::StandardSecurityHandler::create(standard_dict(2, 7), {}, "").error().code, ErrorCode::kUnsupported);
}

TEST(SecurityHandler, RejectsMalformedDictionaries) {
  model::Dict no_r = standard_dict(2, 3);
  no_r.erase("R");
  EXPECT_EQ(crypto::StandardSecurityHandler::create(no_r, {}, "").error().code, ErrorCode::kSyntax);
  model::Dict short_o = standard_dict(2, 3);
  short_o.set("O", model::String{.bytes = "short"});
  EXPECT_EQ(crypto::StandardSecurityHandler::create(short_o, {}, "").error().code, ErrorCode::kSyntax);
  model::Dict bad_length = standard_dict(2, 3);
  bad_length.set("Length", 44);
  EXPECT_EQ(crypto::StandardSecurityHandler::create(bad_length, {}, "").error().code, ErrorCode::kSyntax);
  // Well-formed but with random /O and /U: no password can match.
  EXPECT_EQ(crypto::StandardSecurityHandler::create(standard_dict(2, 3), {}, "").error().code, ErrorCode::kPasswordRequired);
}

}  // namespace
}  // namespace capriole
