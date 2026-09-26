#pragma once

#include <string_view>

namespace capriole {

// Engine version as "major.minor.patch".
std::string_view version() noexcept;

}  // namespace capriole
