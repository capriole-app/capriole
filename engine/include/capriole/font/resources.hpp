#pragma once

// Data too large to build into the engine, supplied by the host when it has it (ADR 0005): Adobe's
// predefined CMaps (ISO 32000-2 §9.7.5.2), the CJK fonts that stand in for Chinese, Japanese and Korean
// fonts that are not embedded, and the Liberation fonts. The resource pack (scripts/fetch_resources.sh)
// lays them out as the names used here:
//
//     cmap/UniJIS-UCS2-H             a predefined CMap, by its PDF name
//     font/NotoSansJP-Regular.otf    fonts, by file name
//
// Without them the engine still draws everything else; what a page would have needed is reported
// (graphics::DisplayList::missing_resources), so that an application can fetch it and draw again.

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/core/error.hpp"

namespace capriole::font {

class Resources {
 public:
  using Data = std::shared_ptr<const std::vector<std::byte>>;

  void add(std::string name, std::vector<std::byte> data);
  // The data of a resource; null when the host has not supplied it.
  [[nodiscard]] Data find(std::string_view name) const;
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

  // Every file under a directory laid out like the pack (cmap/…, font/…). Files larger than
  // max_file_bytes are skipped.
  [[nodiscard]] static Result<Resources> load_directory(const std::filesystem::path& directory, std::size_t max_file_bytes = std::size_t{64} << 20U);

 private:
  std::map<std::string, Data, std::less<>> data_;
};

}  // namespace capriole::font
