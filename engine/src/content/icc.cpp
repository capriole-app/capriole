#include "icc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

// clang-format off
#include <lcms2.h>
#include <lcms2_plugin.h>
// clang-format on

namespace capriole::content::detail {

namespace {

// Little CMS's memory with a budget. Its free function is not told the size of a block: every block
// starts with a header that records it.
struct Budget {
  std::size_t used = 0;
  std::size_t limit = 0;
};

constexpr std::size_t kHeader = alignof(std::max_align_t);

void* budget_malloc(cmsContext context, cmsUInt32Number size) {
  auto* budget = static_cast<Budget*>(cmsGetContextUserData(context));
  if (budget == nullptr || size == 0 || size > budget->limit - budget->used) {
    return nullptr;
  }
  // Little CMS frees what it allocates, through budget_free.
  auto* block = static_cast<unsigned char*>(std::malloc(size + kHeader));  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  if (block == nullptr) {
    return nullptr;
  }
  const std::size_t bytes = size;
  std::memcpy(block, &bytes, sizeof(bytes));
  budget->used += bytes;
  return block + kHeader;
}

void budget_free(cmsContext context, void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  auto* block = static_cast<unsigned char*>(pointer) - kHeader;
  std::size_t bytes = 0;
  std::memcpy(&bytes, block, sizeof(bytes));
  if (auto* budget = static_cast<Budget*>(cmsGetContextUserData(context))) {
    budget->used -= std::min(bytes, budget->used);
  }
  std::free(block);  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

void* budget_realloc(cmsContext context, void* pointer, cmsUInt32Number size) {
  if (pointer == nullptr) {
    return budget_malloc(context, size);
  }
  std::size_t old_bytes = 0;
  std::memcpy(&old_bytes, static_cast<unsigned char*>(pointer) - kHeader, sizeof(old_bytes));
  void* grown = budget_malloc(context, size);
  if (grown == nullptr) {
    return nullptr;
  }
  std::memcpy(grown, pointer, std::min<std::size_t>(old_bytes, size));
  budget_free(context, pointer);
  return grown;
}

cmsPluginMemHandler memory_plugin() {
  cmsPluginMemHandler plugin{};
  plugin.base.Magic = cmsPluginMagicNumber;
  plugin.base.ExpectedVersion = LCMS_VERSION;
  plugin.base.Type = cmsPluginMemHandlerSig;
  plugin.base.Next = nullptr;
  plugin.MallocPtr = budget_malloc;
  plugin.FreePtr = budget_free;
  plugin.ReallocPtr = budget_realloc;
  return plugin;
}

void quiet(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

cmsUInt32Number format16(std::size_t components) {
  switch (components) {
    case 1:
      return TYPE_GRAY_16;
    case 4:
      return TYPE_CMYK_16;
    default:
      return TYPE_RGB_16;
  }
}

cmsUInt32Number format8(std::size_t components) {
  switch (components) {
    case 1:
      return TYPE_GRAY_8;
    case 4:
      return TYPE_CMYK_8;
    default:
      return TYPE_RGB_8;
  }
}

cmsColorSpaceSignature space_of(std::size_t components) {
  switch (components) {
    case 1:
      return cmsSigGrayData;
    case 4:
      return cmsSigCmykData;
    default:
      return cmsSigRgbData;
  }
}

}  // namespace

struct IccTransform::Impl {
  Budget budget;  // outlives the context, which frees through it
  cmsContext context = nullptr;
  cmsHTRANSFORM exact = nullptr;  // 16 bits: single colours
  cmsHTRANSFORM fast = nullptr;   // 8 bits: image rows

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl() {
    if (exact != nullptr) {
      cmsDeleteTransform(exact);
    }
    if (fast != nullptr) {
      cmsDeleteTransform(fast);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }

  bool start(std::size_t limit) {
    budget.limit = limit;
    cmsPluginMemHandler plugin = memory_plugin();
    context = cmsCreateContext(&plugin, &budget);
    if (context == nullptr) {
      return false;
    }
    cmsSetLogErrorHandlerTHR(context, quiet);
    return true;
  }

  // Both transforms from `profile` (owned here, closed after) to sRGB.
  bool build(cmsHPROFILE profile, std::size_t components) {
    cmsHPROFILE srgb = cmsCreate_sRGBProfileTHR(context);
    if (srgb == nullptr) {
      cmsCloseProfile(profile);
      return false;
    }
    // No cache: transforms are then safe to use from several threads.
    const cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE;
    exact = cmsCreateTransformTHR(context, profile, format16(components), srgb, TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC, flags);
    fast = cmsCreateTransformTHR(context, profile, format8(components), srgb, TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC, flags);
    cmsCloseProfile(profile);
    cmsCloseProfile(srgb);
    return exact != nullptr && fast != nullptr;
  }
};

IccTransform::IccTransform(std::unique_ptr<Impl> impl, std::size_t components) : impl_(std::move(impl)), components_(components) {}
IccTransform::~IccTransform() = default;

Result<std::shared_ptr<const IccTransform>> IccTransform::open(std::span<const std::byte> profile, std::size_t components, const IccLimits& limits) {
  if (components != 1 && components != 3 && components != 4) {
    return make_error(ErrorCode::kUnsupported, "ICC profile: " + std::to_string(components) + " components");
  }
  if (profile.empty() || profile.size() > limits.max_profile_bytes) {
    return make_error(ErrorCode::kLimitExceeded, "ICC profile: empty or too large");
  }
  auto impl = std::make_unique<Impl>();
  if (!impl->start(limits.max_memory)) {
    return make_error(ErrorCode::kLimitExceeded, "ICC profile: cannot start Little CMS");
  }
  cmsHPROFILE handle = cmsOpenProfileFromMemTHR(impl->context, profile.data(), static_cast<cmsUInt32Number>(profile.size()));
  if (handle == nullptr) {
    return make_error(ErrorCode::kCorruptData, "ICC profile unreadable");
  }
  const cmsProfileClassSignature device_class = cmsGetDeviceClass(handle);
  if (cmsGetColorSpace(handle) != space_of(components) || device_class == cmsSigLinkClass || device_class == cmsSigAbstractClass ||
      device_class == cmsSigNamedColorClass) {
    cmsCloseProfile(handle);
    return make_error(ErrorCode::kCorruptData, "ICC profile: not a profile of " + std::to_string(components) + "-component colours");
  }
  if (!impl->build(handle, components)) {
    return make_error(ErrorCode::kCorruptData, "ICC profile: no transform to sRGB");
  }
  return std::shared_ptr<const IccTransform>(new IccTransform(std::move(impl), components));
}

Result<std::shared_ptr<const IccTransform>> IccTransform::calibrated(std::array<double, 3> white, std::array<double, 3> gamma,
                                                                     const std::array<double, 9>* matrix) {
  const auto xy = [](double x, double y, double z) {
    const double sum = x + y + z;
    return sum > 0 ? cmsCIExyY{.x = x / sum, .y = y / sum, .Y = 1} : cmsCIExyY{.x = 0.3457, .y = 0.3585, .Y = 1};
  };
  if (!(white[1] > 0) || !std::ranges::all_of(gamma, [](double g) { return g > 0 && std::isfinite(g); })) {
    return make_error(ErrorCode::kCorruptData, "calibrated colour space: invalid /WhitePoint or /Gamma");
  }
  auto impl = std::make_unique<Impl>();
  if (!impl->start(IccLimits{}.max_memory)) {
    return make_error(ErrorCode::kLimitExceeded, "calibrated colour space: cannot start Little CMS");
  }
  const cmsCIExyY white_point = xy(white[0], white[1], white[2]);
  std::array<cmsToneCurve*, 3> curves{};
  for (std::size_t i = 0; i < 3; ++i) {
    curves.at(i) = cmsBuildGamma(impl->context, gamma.at(i));
  }
  cmsHPROFILE profile = nullptr;
  if (matrix == nullptr) {
    profile = curves[0] != nullptr ? cmsCreateGrayProfileTHR(impl->context, &white_point, curves[0]) : nullptr;
  } else {
    const std::array<double, 9>& m = *matrix;
    const cmsCIExyYTRIPLE primaries{.Red = xy(m[0], m[1], m[2]), .Green = xy(m[3], m[4], m[5]), .Blue = xy(m[6], m[7], m[8])};
    if (std::ranges::all_of(curves, [](cmsToneCurve* c) { return c != nullptr; })) {
      profile = cmsCreateRGBProfileTHR(impl->context, &white_point, &primaries, curves.data());
    }
  }
  for (cmsToneCurve* c : curves) {
    if (c != nullptr) {
      cmsFreeToneCurve(c);
    }
  }
  const std::size_t components = matrix == nullptr ? 1 : 3;
  if (profile == nullptr || !impl->build(profile, components)) {
    return make_error(ErrorCode::kCorruptData, "calibrated colour space: no transform to sRGB");
  }
  return std::shared_ptr<const IccTransform>(new IccTransform(std::move(impl), components));
}

graphics::Rgb IccTransform::convert(std::span<const double> in) const {
  std::array<cmsUInt16Number, 4> input{};
  for (std::size_t i = 0; i < components_ && i < input.size(); ++i) {
    const double v = i < in.size() && std::isfinite(in[i]) ? std::clamp(in[i], 0.0, 1.0) : 0.0;
    input.at(i) = static_cast<cmsUInt16Number>(std::lround(v * 65535));
  }
  std::array<cmsUInt16Number, 3> output{};
  cmsDoTransform(impl_->exact, input.data(), output.data(), 1);
  return {output[0] / 65535.0, output[1] / 65535.0, output[2] / 65535.0};
}

void IccTransform::convert8(const std::uint8_t* in, std::uint8_t* out, std::size_t count) const {
  // Little CMS counts pixels in 32 bits: long rows go in parts.
  constexpr std::size_t kPart = std::size_t{1} << 24U;
  while (count > 0) {
    const std::size_t part = std::min(count, kPart);
    cmsDoTransform(impl_->fast, in, out, static_cast<cmsUInt32Number>(part));
    in += part * components_;
    out += part * 3;
    count -= part;
  }
}

}  // namespace capriole::content::detail
