#include "capriole/core/version.hpp"

#include "capriole/core/version_config.hpp"

namespace capriole {

std::string_view version() noexcept { return build::kVersionString; }

}  // namespace capriole
