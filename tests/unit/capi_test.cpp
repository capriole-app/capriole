#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "capriole/capriole.h"

namespace {

TEST(CApi, VersionMatchesAbi) {
  EXPECT_FALSE(std::string_view(capriole_version()).empty());
  EXPECT_EQ(capriole_abi_version(), CAPRIOLE_ABI_VERSION);
}

TEST(CApi, ProbeHeader) {
  constexpr std::string_view kPdf = "%PDF-1.6\n";
  int major = 0;
  int minor = 0;
  const auto* data = reinterpret_cast<const std::uint8_t*>(kPdf.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  EXPECT_EQ(capriole_probe_header(data, kPdf.size(), &major, &minor), CAPRIOLE_OK);
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 6);
  EXPECT_STREQ(capriole_last_error_message(), "");
}

TEST(CApi, ProbeHeaderReportsSyntaxError) {
  constexpr std::string_view kNotPdf = "hello";
  int major = 0;
  int minor = 0;
  const auto* data = reinterpret_cast<const std::uint8_t*>(kNotPdf.data());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  EXPECT_EQ(capriole_probe_header(data, kNotPdf.size(), &major, &minor), CAPRIOLE_ERROR_SYNTAX);
  EXPECT_STRNE(capriole_last_error_message(), "");
}

TEST(CApi, ProbeHeaderRejectsNullArguments) {
  int major = 0;
  int minor = 0;
  EXPECT_EQ(capriole_probe_header(nullptr, 4, &major, &minor), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(capriole_probe_header(nullptr, 0, nullptr, &minor), CAPRIOLE_ERROR_INVALID_ARGUMENT);
  // An empty buffer is valid input, just not a PDF.
  EXPECT_EQ(capriole_probe_header(nullptr, 0, &major, &minor), CAPRIOLE_ERROR_SYNTAX);
}

}  // namespace
