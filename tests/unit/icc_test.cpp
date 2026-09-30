// ICC colour management (content/icc, ADR 0006): profiles applied to colours and images, calibrated
// spaces, the default CMYK (output intent, the press profile, the built-in table). Profiles are made
// here with Little CMS.

#include "content/icc.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include <lcms2.h>

#include "capriole/content/interpreter.hpp"
#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "support/pdf_builder.hpp"

namespace capriole::content {
namespace {

using graphics::DisplayList;
using graphics::Rgb;

std::string saved(cmsHPROFILE profile) {
  cmsUInt32Number size = 0;
  cmsSaveProfileToMem(profile, nullptr, &size);
  std::string out(size, '\0');
  cmsSaveProfileToMem(profile, out.data(), &size);
  cmsCloseProfile(profile);
  return out;
}

std::string srgb_profile() { return saved(cmsCreate_sRGBProfile()); }

// An RGB profile whose red and green primaries are sRGB's swapped: applying it must swap them.
std::string swapped_profile() {
  const cmsCIExyY d65{.x = 0.3127, .y = 0.3290, .Y = 1};
  const cmsCIExyYTRIPLE primaries{.Red = {.x = 0.30, .y = 0.60, .Y = 1}, .Green = {.x = 0.64, .y = 0.33, .Y = 1}, .Blue = {.x = 0.15, .y = 0.06, .Y = 1}};
  cmsToneCurve* curve = cmsBuildGamma(nullptr, 2.2);
  std::array<cmsToneCurve*, 3> curves = {curve, curve, curve};
  cmsHPROFILE profile = cmsCreateRGBProfile(&d65, &primaries, curves.data());
  cmsFreeToneCurve(curve);
  return saved(profile);
}

// A CMYK output profile whose device-to-Lab table is the naive formula: cyan is sRGB cyan, and so on.
std::string naive_cmyk_profile() {
  cmsHPROFILE profile = cmsCreateProfilePlaceholder(nullptr);
  cmsSetProfileVersion(profile, 2.1);
  cmsSetDeviceClass(profile, cmsSigOutputClass);
  cmsSetColorSpace(profile, cmsSigCmykData);
  cmsSetPCS(profile, cmsSigLabData);
  cmsHPROFILE srgb = cmsCreate_sRGBProfile();
  cmsHPROFILE lab = cmsCreateLab4Profile(nullptr);
  cmsHTRANSFORM to_lab = cmsCreateTransform(srgb, TYPE_RGB_DBL, lab, TYPE_Lab_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
  cmsPipeline* pipeline = cmsPipelineAlloc(nullptr, 4, 3);
  cmsStage* clut = cmsStageAllocCLut16bit(nullptr, 5, 4, 3, nullptr);
  cmsStageSampleCLut16bit(
      clut,
      [](const cmsUInt16Number in[], cmsUInt16Number out[], void* cargo) -> cmsInt32Number {
        const double k = in[3] / 65535.0;
        const std::array<double, 3> rgb = {(1 - (in[0] / 65535.0)) * (1 - k), (1 - (in[1] / 65535.0)) * (1 - k), (1 - (in[2] / 65535.0)) * (1 - k)};
        cmsCIELab l{};
        cmsDoTransform(static_cast<cmsHTRANSFORM>(cargo), rgb.data(), &l, 1);
        cmsFloat2LabEncoded(out, &l);
        return 1;
      },
      to_lab, 0);
  cmsPipelineInsertStage(pipeline, cmsAT_BEGIN, clut);
  cmsWriteTag(profile, cmsSigAToB0Tag, pipeline);
  cmsWriteTag(profile, cmsSigMediaWhitePointTag, cmsD50_XYZ());
  cmsPipelineFree(pipeline);
  cmsDeleteTransform(to_lab);
  cmsCloseProfile(srgb);
  cmsCloseProfile(lab);
  return saved(profile);
}

std::vector<std::byte> bytes(const std::string& text) {
  std::vector<std::byte> out(text.size());
  std::ranges::transform(text, out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

std::string stream_object(const std::string& dict, const std::string& data) {
  return "<<" + dict + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream";
}

DisplayList interpret(const std::string& content, const std::string& resources, const std::vector<std::pair<std::uint32_t, std::string>>& objects = {},
                      const std::string& catalog_extra = "", const font::Resources* pack = nullptr) {
  test::PdfBuilder b;
  b.add_object(1, "<</Type/Catalog/Pages 2 0 R" + catalog_extra + ">>");
  b.add_object(2, "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 100 100]>>");
  b.add_object(3, "<</Type/Page/Parent 2 0 R/Contents 4 0 R/Resources<<" + resources + ">>>>");
  b.add_stream(4, content);
  for (const auto& [number, body] : objects) {
    b.add_object(number, body);
  }
  b.finish_table("/Size 40/Root 1 0 R");
  auto document = Document::open(std::make_unique<MemoryByteSource>(bytes(b.str())));
  EXPECT_TRUE(document.has_value());
  auto list = interpret_page(*document, model::ObjectRef{.number = 3, .generation = 0}, {}, pack);
  EXPECT_TRUE(list.has_value());
  return std::move(*list);
}

template <typename T>
std::vector<T> all(const DisplayList& list) {
  std::vector<T> out;
  for (const auto& command : list.commands) {
    if (const T* c = graphics::command_as<T>(command)) {
      out.push_back(*c);
    }
  }
  return out;
}

Rgb fill_colour(const DisplayList& list, std::size_t i) { return list.colors[all<graphics::FillPath>(list).at(i).color].rgb; }

TEST(Icc, TransformsOfEveryKind) {
  const std::string srgb = srgb_profile();
  auto rgb = detail::IccTransform::open(std::as_bytes(std::span(srgb)), 3);
  ASSERT_TRUE(rgb.has_value()) << rgb.error().message;
  const std::array<double, 3> red = {1, 0, 0};
  const Rgb out = (*rgb)->convert(red);
  EXPECT_NEAR(out.r, 1, 0.01);
  EXPECT_NEAR(out.g, 0, 0.01);
  // The wrong number of components, garbage, nothing: refused.
  EXPECT_FALSE(detail::IccTransform::open(std::as_bytes(std::span(srgb)), 4).has_value());
  const std::string junk(500, 'x');
  EXPECT_FALSE(detail::IccTransform::open(std::as_bytes(std::span(junk)), 3).has_value());
  EXPECT_FALSE(detail::IccTransform::open({}, 3).has_value());
  // A budget too small for the profile: refused, not a crash.
  EXPECT_FALSE(detail::IccTransform::open(std::as_bytes(std::span(srgb)), 3, {.max_memory = 2048, .max_profile_bytes = 1 << 20}).has_value());
  // Calibrated spaces.
  auto gray = detail::IccTransform::calibrated({0.9505, 1, 1.089}, {1, 1, 1}, nullptr);
  ASSERT_TRUE(gray.has_value()) << gray.error().message;
  // 8-bit rows agree with single colours.
  const std::string cmyk = naive_cmyk_profile();
  ASSERT_FALSE(cmyk.empty());
  auto press = detail::IccTransform::open(std::as_bytes(std::span(cmyk)), 4);
  ASSERT_TRUE(press.has_value()) << press.error().message;
  const std::array<std::uint8_t, 8> row = {255, 0, 0, 0, 0, 0, 0, 255};
  std::array<std::uint8_t, 6> pixels{};
  (*press)->convert8(row.data(), pixels.data(), 2);
  EXPECT_LT(pixels[0], 40);  // cyan (the test profile's coarse table: not exactly 0)
  EXPECT_NEAR(pixels[1], 255, 6);
  EXPECT_NEAR(pixels[3], 0, 6);  // black
}

TEST(Icc, IccBasedSpacesApplyTheirProfile) {
  const auto list = interpret("/P cs 1 0 0 sc 0 0 1 1 re f /Bad cs 0.5 sc 0 0 1 1 re f", "/ColorSpace<</P[/ICCBased 10 0 R]/Bad[/ICCBased 11 0 R]>>",
                              {{10, stream_object("/N 3", swapped_profile())}, {11, stream_object("/N 1/Alternate/DeviceGray", "not a profile")}});
  const Rgb swapped = fill_colour(list, 0);
  EXPECT_LT(swapped.r, 0.5);  // "red" in this profile is sRGB's green
  EXPECT_GT(swapped.g, 0.8);
  EXPECT_NEAR(fill_colour(list, 1).r, 0.5, 1e-9);  // the alternate space
  EXPECT_TRUE(std::ranges::any_of(list.warnings, [](const std::string& w) { return w.find("ICC profile unusable") != std::string::npos; }));
}

TEST(Icc, CalibratedSpaces) {
  // CalGray with gamma 1 is linear light: 0.5 is lighter in sRGB (≈ 0.735).
  const auto list = interpret("/G cs 0.5 sc 0 0 1 1 re f /R cs 1 1 1 sc 0 0 1 1 re f",
                              "/ColorSpace<</G[/CalGray<</WhitePoint[0.9505 1 1.089]/Gamma 1>>]"
                              "/R[/CalRGB<</WhitePoint[0.9505 1 1.089]/Matrix[0.4124 0.2126 0.0193 0.3576 0.7152 0.1192 0.1805 0.0722 0.9505]>>]>>");
  EXPECT_NEAR(fill_colour(list, 0).r, 0.735, 0.01);
  const Rgb white = fill_colour(list, 1);
  EXPECT_NEAR(white.r, 1, 0.01);
  EXPECT_NEAR(white.b, 1, 0.01);
}

TEST(Icc, DefaultCmykFromTheOutputIntentThePackOrTheTable) {
  const std::string content = "0 1 0 0 k 0 0 1 1 re f";
  // No output intent, no pack: the built-in table (SWOP's magenta is not sRGB magenta), reported missing.
  const auto table = interpret(content, "");
  EXPECT_GT(fill_colour(table, 0).b, 0.4);
  EXPECT_LT(fill_colour(table, 0).b, 0.6);
  EXPECT_EQ(table.missing_resources, (std::vector<std::string>{"icc/SWOP2006_Coated3v2.icc"}));
  // An output intent: its profile, and nothing missing.
  const auto intent =
      interpret(content, "", {{10, "<</Type/OutputIntent/S/GTS_PDFX/DestOutputProfile 11 0 R>>"}, {11, stream_object("/N 4", naive_cmyk_profile())}},
                "/OutputIntents[10 0 R]");
  EXPECT_NEAR(fill_colour(intent, 0).b, 1, 0.03);  // the naive profile: magenta is sRGB magenta
  EXPECT_TRUE(intent.missing_resources.empty());
  // The pack's press profile (here the naive one under its name).
  font::Resources pack;
  pack.add("icc/SWOP2006_Coated3v2.icc", bytes(naive_cmyk_profile()));
  const auto packed = interpret(content, "", {}, "", &pack);
  EXPECT_NEAR(fill_colour(packed, 0).b, 1, 0.03);
  EXPECT_TRUE(packed.missing_resources.empty());
}

TEST(Icc, ImagesGoThroughTheProfileByRows) {
  // A 2 × 1 CMYK image in an ICCBased space: cyan, then black.
  const std::string samples = std::string("\xFF\x00\x00\x00\x00\x00\x00\xFF", 8);
  const auto list = interpret("100 0 0 100 0 0 cm /Im Do", "/XObject<</Im 10 0 R>>",
                              {{10, stream_object("/Type/XObject/Subtype/Image/Width 2/Height 1/BitsPerComponent 8/ColorSpace[/ICCBased 11 0 R]", samples)},
                               {11, stream_object("/N 4", naive_cmyk_profile())}});
  const auto images = all<graphics::DrawImage>(list);
  ASSERT_EQ(images.size(), 1U);
  ASSERT_NE(images[0].pixels, nullptr);
  const auto& rgba = images[0].pixels->rgba;
  EXPECT_LT(rgba[0], 40);
  EXPECT_NEAR(rgba[1], 255, 6);  // through the image's profile: SWOP would give 170
  EXPECT_NEAR(rgba[4], 0, 6);
  EXPECT_EQ(rgba[7], 255);
}

}  // namespace
}  // namespace capriole::content
