// cpdf — developer command-line tool for the Capriole engine.
//
// It grows with the engine: every capability gets a subcommand here first, so it can be exercised
// on real files and by the corpus runner before any UI exists.

#include <format>
#include <iostream>
#include <span>
#include <string_view>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/version.hpp"
#include "capriole/parser/header.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

void print_usage() {
  std::cerr << "usage: cpdf <command> [args]\n"
               "\n"
               "commands:\n"
               "  version         print the engine version\n"
               "  header <file>   print the PDF version declared in the file header\n";
}

int cmd_header(std::string_view path) {
  auto source = capriole::FileByteSource::open(path);
  if (!source) {
    std::cerr << "cpdf: " << source.error().message << '\n';
    return kExitFailure;
  }
  const auto header = capriole::parser::find_header(**source);
  if (!header) {
    std::cerr << std::format("cpdf: {}: {} ({})\n", path, header.error().message, capriole::to_string(header.error().code));
    return kExitFailure;
  }
  std::cout << std::format("PDF {}.{} (header at offset {})\n", header->major, header->minor, header->offset);
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (args.size() < 2) {
    print_usage();
    return kExitUsage;
  }

  const std::string_view command = args[1];
  if (command == "version" || command == "--version") {
    std::cout << "cpdf " << capriole::version() << '\n';
    return kExitOk;
  }
  if (command == "header" && args.size() == 3) {
    return cmd_header(args[2]);
  }

  print_usage();
  return kExitUsage;
}
