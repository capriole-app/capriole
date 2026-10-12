#include "capriole/font/resources.hpp"

#include <fstream>
#include <system_error>
#include <utility>

namespace capriole::font {

void Resources::add(std::string name, std::vector<std::byte> data) {
  data_.insert_or_assign(std::move(name), std::make_shared<const std::vector<std::byte>>(std::move(data)));
}

Resources::Data Resources::find(std::string_view name) const {
  const auto found = data_.find(name);
  return found != data_.end() ? found->second : nullptr;
}

Result<Resources> Resources::load_directory(const std::filesystem::path& directory, std::size_t max_file_bytes) {
  std::error_code error;
  std::filesystem::recursive_directory_iterator it(directory, error);
  if (error) {
    return make_error(ErrorCode::kIo, "resources: cannot read " + directory.string() + ": " + error.message());
  }
  Resources resources;
  for (const std::filesystem::recursive_directory_iterator end; it != end; it.increment(error)) {
    if (error) {
      return make_error(ErrorCode::kIo, "resources: " + error.message());
    }
    if (it->path().filename().string().starts_with('.')) {
      if (it->is_directory(error)) {
        it.disable_recursion_pending();  // hidden: a download cache, a VCS directory
      }
      continue;
    }
    if (!it->is_regular_file(error) || it->file_size(error) > max_file_bytes) {
      continue;
    }
    std::ifstream in(it->path(), std::ios::binary);
    std::vector<std::byte> data(static_cast<std::size_t>(it->file_size(error)));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes from a stream
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
      continue;
    }
    resources.add(it->path().lexically_relative(directory).generic_string(), std::move(data));
  }
  return resources;
}

}  // namespace capriole::font
