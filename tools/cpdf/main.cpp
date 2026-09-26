// cpdf — developer command-line tool for the Capriole engine.
//
// It grows with the engine: every capability gets a subcommand here first, so it can be exercised
// on real files and by the corpus runner before any UI exists.

#include <charconv>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/core/version.hpp"
#include "capriole/parser/header.hpp"
#include "capriole/parser/lexer.hpp"
#include "capriole/parser/object_parser.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

void print_usage() {
  std::cerr << "usage: cpdf <command> [args]\n"
               "\n"
               "commands:\n"
               "  version         print the engine version\n"
               "  header <file>   print the PDF version declared in the file header\n"
               "  tokens <file> [offset] [count]\n"
               "                  print up to count tokens (default 100) starting at offset (default 0)\n"
               "  object <file> <offset>\n"
               "                  parse the indirect object (n g obj ... endobj) that starts at offset\n";
}

std::optional<std::uint64_t> parse_uint(std::string_view text) {
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Printable rendering of token bytes: ASCII as is, everything else as \xHH, long values cut.
std::string printable(std::string_view bytes) {
  constexpr std::size_t kMaxShown = 60;
  std::string out;
  for (const char ch : bytes.substr(0, kMaxShown)) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x20 && byte < 0x7F && byte != '\\') {
      out.push_back(ch);
    } else {
      out += std::format("\\x{:02X}", byte);
    }
  }
  if (bytes.size() > kMaxShown) {
    out += std::format("... ({} bytes)", bytes.size());
  }
  return out;
}

std::string describe(const capriole::parser::Token& token) {
  using capriole::parser::TokenKind;
  switch (token.kind) {
    case TokenKind::kInteger:
      return std::to_string(token.integer);
    case TokenKind::kReal:
      return std::format("{}", token.real);
    case TokenKind::kString:
      return token.string_form == capriole::parser::StringForm::kHex ? "<" + printable(token.bytes) + ">" : "(" + printable(token.bytes) + ")";
    case TokenKind::kName:
      return "/" + printable(token.bytes);
    case TokenKind::kKeyword:
      return printable(token.bytes);
    default:
      return "";
  }
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

int cmd_tokens(std::string_view path, std::uint64_t offset, std::uint64_t count) {
  auto source = capriole::FileByteSource::open(path);
  if (!source) {
    std::cerr << "cpdf: " << source.error().message << '\n';
    return kExitFailure;
  }
  capriole::SourceReader reader(**source);
  if (auto seek = reader.seek(offset); !seek) {
    std::cerr << std::format("cpdf: {}\n", seek.error().message);
    return kExitFailure;
  }
  capriole::parser::Lexer lexer(reader);
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto token = lexer.next();
    if (!token) {
      std::cerr << std::format("cpdf: {} at offset {} ({})\n", token.error().message, token.error().offset, capriole::to_string(token.error().code));
      return kExitFailure;
    }
    if (token->kind == capriole::parser::TokenKind::kEnd) {
      break;
    }
    std::cout << std::format("{:>10}  {:<11}  {}\n", token->offset, capriole::parser::to_string(token->kind), describe(*token));
  }
  return kExitOk;
}

int cmd_object(std::string_view path, std::uint64_t offset) {
  auto source = capriole::FileByteSource::open(path);
  if (!source) {
    std::cerr << "cpdf: " << source.error().message << '\n';
    return kExitFailure;
  }
  capriole::SourceReader reader(**source);
  capriole::parser::ObjectParser parser(reader);
  if (auto seek = parser.seek(offset); !seek) {
    std::cerr << std::format("cpdf: {}\n", seek.error().message);
    return kExitFailure;
  }
  // Without a document there is no xref: an indirect /Length cannot be resolved yet (phase 2c).
  const auto object = parser.parse_indirect_object();
  if (!object) {
    std::cerr << std::format("cpdf: {} at offset {} ({})\n", object.error().message, object.error().offset, capriole::to_string(object.error().code));
    return kExitFailure;
  }
  std::cout << std::format("{} {} obj  [{}..{})  {}\n", object->ref.number, object->ref.generation, object->offset, object->end_offset,
                           capriole::model::to_string(object->object.kind()));
  std::cout << capriole::model::debug_string(object->object) << '\n';
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
  if (command == "object" && args.size() == 4) {
    if (const auto offset = parse_uint(args[3])) {
      return cmd_object(args[2], *offset);
    }
  }
  if (command == "tokens" && args.size() >= 3 && args.size() <= 5) {
    const auto offset = args.size() >= 4 ? parse_uint(args[3]) : std::optional<std::uint64_t>(0);
    const auto count = args.size() == 5 ? parse_uint(args[4]) : std::optional<std::uint64_t>(100);
    if (offset && count) {
      return cmd_tokens(args[2], *offset, *count);
    }
  }

  print_usage();
  return kExitUsage;
}
