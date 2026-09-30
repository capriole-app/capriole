// cpdf — developer command-line tool for the Capriole engine.
//
// It grows with the engine: every capability gets a subcommand here first, so it can be exercised
// on real files and by the corpus runner before any UI exists.

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/content/interpreter.hpp"
#include "capriole/content/page_content.hpp"
#include "capriole/core/byte_source.hpp"
#include "capriole/core/source_reader.hpp"
#include "capriole/core/version.hpp"
#include "capriole/document/document.hpp"
#include "capriole/font/resources.hpp"
#include "capriole/graphics/svg.hpp"
#include "capriole/model/text_string.hpp"
#include "capriole/parser/header.hpp"
#include "capriole/parser/lexer.hpp"
#include "capriole/parser/object_parser.hpp"
#include "capriole/parser/xref.hpp"
#include "capriole/render/png.hpp"
#include "capriole/render/raster.hpp"
#include "capriole/text/text.hpp"
#include "capriole/writer/byte_sink.hpp"
#include "capriole/writer/save.hpp"
#include "capriole/writer/serializer.hpp"

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
               "  xref <file>     print the cross-reference sections, trailer and entry counts\n"
               "  object <file> <number> [generation]\n"
               "                  load an object through the xref and print it\n"
               "  object-at <file> <offset>\n"
               "                  parse the indirect object (n g obj ... endobj) that starts at offset\n"
               "  stream <file> <number>\n"
               "                  write the decoded data of a stream object to stdout\n"
               "  pages <file>    print the page count and the page object references\n"
               "  render [--dpi N] [--resources DIR] <file> <page> <output.svg|output.png>\n"
               "                  interpret a page and write its vector preview (.svg) or draw it (.png,\n"
               "                  at N dpi, default 150); DIR: the resource pack (CMaps, CJK fonts,\n"
               "                  scripts/fetch_resources.sh); prints what was drawn, the warnings and\n"
               "                  the resources the page wanted and did not find\n"
               "  text [--words] [--order content|layout] [--resources DIR] <file> <page>\n"
               "                  extract the text of a page in reading order (blocks separated by an empty\n"
               "                  line): the content's order (default) or by columns; --words: one word per\n"
               "                  line with its box (points, top-left origin) and where its first character\n"
               "                  comes from\n"
               "  content <file> <page>\n"
               "                  print the operations of a page's content stream (page from 1)\n"
               "  save [--incremental] [--moddate D:...] <file> <output>\n"
               "                  write the document anew (full save; repairs applied are fixed in the output),\n"
               "                  or append an update to the original bytes (--incremental); --moddate sets\n"
               "                  /Info /ModDate (with --incremental it is the change that gets appended)\n"
               "  check <file>    load every object, decode every stream, walk the page tree;\n"
               "                  exit 1 on any error\n"
               "\n"
               "Encrypted files: set CAPRIOLE_PASSWORD (user or owner password; empty by default).\n"
               "Strict reading only (no repairs): set CAPRIOLE_STRICT=1.\n";
}

int fail(std::string_view context, const capriole::Error& error) {
  std::cerr << std::format("cpdf: {}: {}", context, error.message);
  if (error.offset != capriole::Error::kNoOffset) {
    std::cerr << std::format(" at offset {}", error.offset);
  }
  std::cerr << std::format(" ({})\n", capriole::to_string(error.code));
  return kExitFailure;
}

capriole::Result<capriole::Document> open_document(std::string_view path) {
  auto source = capriole::FileByteSource::open(path);
  if (!source) {
    return std::unexpected(std::move(source.error()));
  }
  // Encrypted files: the password comes from the environment, so it does not end up in shell history.
  capriole::OpenOptions options;
  if (const char* password = std::getenv("CAPRIOLE_PASSWORD")) {  // NOLINT(concurrency-mt-unsafe): single-threaded tool
    options.password = password;
  }
  // CAPRIOLE_STRICT=1: no repairs, to see what strict reading alone makes of a file.
  if (const char* strict = std::getenv("CAPRIOLE_STRICT"); strict != nullptr && std::string_view(strict) == "1") {  // NOLINT(concurrency-mt-unsafe)
    options.repair = false;
  }
  return capriole::Document::open(std::move(*source), options);
}

std::string_view form_name(capriole::parser::XrefForm form) {
  switch (form) {
    case capriole::parser::XrefForm::kTable:
      return "table";
    case capriole::parser::XrefForm::kStream:
      return "stream";
    case capriole::parser::XrefForm::kHybrid:
      return "hybrid";
    case capriole::parser::XrefForm::kReconstructed:
      return "rebuilt";
  }
  return "?";
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

int cmd_xref(std::string_view path) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  const auto& xref = document->xref();
  std::cout << std::format("startxref {}\n", xref.startxref);
  for (const auto& section : xref.sections) {
    std::cout << std::format("section at {:>10}  {:<6}  {} entries\n", section.offset, form_name(section.form), section.entry_count);
  }
  std::map<capriole::parser::XrefEntryType, std::size_t> counts;
  for (const auto& [number, entry] : xref.entries) {
    ++counts[entry.type];
  }
  std::cout << std::format("objects: {} in use, {} compressed, {} free\n", counts[capriole::parser::XrefEntryType::kInUse],
                           counts[capriole::parser::XrefEntryType::kCompressed], counts[capriole::parser::XrefEntryType::kFree]);
  std::cout << "trailer " << capriole::model::debug_string(xref.trailer) << '\n';
  return kExitOk;
}

int cmd_object(std::string_view path, std::uint32_t number, std::optional<std::uint16_t> generation) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto object = generation ? document->get(capriole::model::ObjectRef{.number = number, .generation = *generation}) : document->get(number);
  if (!object) {
    return fail(path, object.error());
  }
  std::cout << std::format("{}  {}\n", number, capriole::model::to_string((*object)->kind()));
  std::cout << capriole::model::debug_string(**object) << '\n';
  return kExitOk;
}

int cmd_stream(std::string_view path, std::uint32_t number) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto object = document->get(number);
  if (!object) {
    return fail(path, object.error());
  }
  const capriole::model::Stream* stream = (*object)->as_stream();
  if (stream == nullptr) {
    std::cerr << std::format("cpdf: object {} is a {}, not a stream\n", number, capriole::model::to_string((*object)->kind()));
    return kExitFailure;
  }
  auto data = document->decode_stream(*stream);
  if (!data) {
    return fail(path, data.error());
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): ostream::write takes char*.
  std::cout.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
  return kExitOk;
}

int cmd_check(std::string_view path) {
  constexpr std::size_t kMaxReported = 20;
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  capriole::Document& doc = *document;
  std::size_t objects = 0;
  std::size_t streams = 0;
  std::size_t errors = 0;
  std::map<std::string, std::size_t> unsupported;
  const auto report = [&](std::uint32_t number, const capriole::Error& error) {
    if (++errors <= kMaxReported) {
      std::cerr << std::format("object {}: {} ({})\n", number, error.message, capriole::to_string(error.code));
    }
  };

  for (const auto& [number, entry] : doc.xref().entries) {
    if (entry.type == capriole::parser::XrefEntryType::kFree) {
      continue;
    }
    auto object = doc.get(number);
    if (!object) {
      // e.g. objects inside the object streams of an encrypted file (decryption is phase 2e)
      if (object.error().code == capriole::ErrorCode::kUnsupported) {
        ++unsupported[object.error().message];
      } else {
        report(number, object.error());
      }
      continue;
    }
    ++objects;
    if (const auto* stream = (*object)->as_stream()) {
      auto data = doc.decode_stream(*stream);
      if (data) {
        ++streams;
      } else if (data.error().code == capriole::ErrorCode::kUnsupported) {
        ++unsupported[data.error().message];
      } else {
        report(number, data.error());
      }
    }
  }

  auto pages = doc.page_refs();
  if (pages) {
    std::cout << std::format("{}: {} pages, {} objects loaded, {} streams decoded, {} errors\n", path, pages->size(), objects, streams, errors);
  } else {
    if (pages.error().code == capriole::ErrorCode::kUnsupported) {
      ++unsupported[pages.error().message];
    } else {
      report(0, pages.error());
    }
    std::cout << std::format("{}: page tree unreadable, {} objects loaded, {} streams decoded, {} errors\n", path, objects, streams, errors);
  }
  for (const auto& [reason, count] : unsupported) {
    std::cout << std::format("  not supported yet ({} objects/streams): {}\n", count, reason);
  }
  for (const auto& note : doc.repairs()) {
    std::cout << std::format("  repaired ({}): {}\n", capriole::repair::to_string(note.kind), note.description);
  }
  return errors == 0 ? kExitOk : kExitFailure;
}

int cmd_content(std::string_view path, std::uint64_t page_number) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto pages = document->page_refs();
  if (!pages) {
    return fail(path, pages.error());
  }
  if (page_number == 0 || page_number > pages->size()) {
    std::cerr << std::format("cpdf: {}: page {} out of 1..{}\n", path, page_number, pages->size());
    return kExitUsage;
  }
  auto content = capriole::content::page_content(*document, (*pages)[page_number - 1]);
  if (!content) {
    return fail(path, content.error());
  }
  // Tolerant, as a viewer reads content: the errors skipped are reported at the end.
  capriole::content::ContentParser parser(*content, capriole::kDefaultParseLimits, capriole::parser::ParseMode::kTolerant);
  std::size_t count = 0;
  for (;;) {
    auto operation = parser.next();
    if (!operation) {
      return fail(path, operation.error());
    }
    if (!*operation) {
      break;
    }
    ++count;
    std::string line = std::format("{:>8}  ", (*operation)->offset);
    for (const auto& operand : (*operation)->operands) {
      line += capriole::writer::serialize(operand) + " ";
    }
    line += (*operation)->name;
    if (const auto& image = (*operation)->image) {
      line += std::format(" {} ID <{} bytes> EI", capriole::writer::serialize(capriole::model::Object(image->dict)), image->data.size());
    } else if ((*operation)->op == capriole::content::Operator::kUnknown) {
      line += "  (unknown operator)";
    }
    std::cout << line << '\n';
  }
  std::cout << std::format("{} operations, {} bytes of content", count, content->size());
  if (parser.recovered_errors() > 0) {
    std::cout << std::format(", {} syntax errors skipped", parser.recovered_errors());
  }
  std::cout << '\n';
  return kExitOk;
}

int cmd_render(std::string_view path, std::uint64_t page_number, std::string_view output, double dpi, std::string_view resources_dir) {
  capriole::font::Resources resources;
  if (!resources_dir.empty()) {
    auto loaded = capriole::font::Resources::load_directory(std::filesystem::path(std::string(resources_dir)));
    if (!loaded) {
      return fail(resources_dir, loaded.error());
    }
    resources = std::move(*loaded);
  }
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto pages = document->page_refs();
  if (!pages) {
    return fail(path, pages.error());
  }
  if (page_number == 0 || page_number > pages->size()) {
    std::cerr << std::format("cpdf: {}: page {} out of 1..{}\n", path, page_number, pages->size());
    return kExitUsage;
  }
  auto list = capriole::content::interpret_page(*document, (*pages)[page_number - 1], {}, &resources);
  if (!list) {
    return fail(path, list.error());
  }
  std::map<std::string_view, std::size_t> counts;
  for (const auto& command : list->commands) {
    ++counts[capriole::graphics::command_kind(command)];
  }
  std::ofstream file{std::string(output), std::ios::binary};
  if (output.ends_with(".png")) {
    auto bitmap = capriole::render::rasterize(*list, dpi / 72);
    if (!bitmap) {
      return fail(path, bitmap.error());
    }
    auto png = capriole::render::encode_png(*bitmap);
    if (!png) {
      return fail(path, png.error());
    }
    file.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    std::cout << std::format("{} x {} pixels at {} dpi\n", bitmap->width, bitmap->height, dpi);
  } else {
    file << capriole::graphics::to_svg(*list);
  }
  if (!file) {
    std::cerr << std::format("cpdf: cannot write {}\n", output);
    return kExitFailure;
  }
  std::cout << std::format("{}: page {}, {} commands:", output, page_number, list->commands.size());
  for (const auto& [name, count] : counts) {
    std::cout << std::format(" {} {}", name, count);
  }
  std::cout << '\n';
  for (const auto& warning : list->warnings) {
    std::cout << "  warning: " << warning << '\n';
  }
  for (const auto& missing : list->missing_resources) {
    std::cout << "  missing resource: " << missing << '\n';
  }
  return kExitOk;
}

int cmd_text(std::string_view path, std::uint64_t page_number, bool words, capriole::text::Order order, std::string_view resources_dir) {
  capriole::font::Resources resources;
  if (!resources_dir.empty()) {
    auto loaded = capriole::font::Resources::load_directory(std::filesystem::path(std::string(resources_dir)));
    if (!loaded) {
      return fail(resources_dir, loaded.error());
    }
    resources = std::move(*loaded);
  }
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto pages = document->page_refs();
  if (!pages) {
    return fail(path, pages.error());
  }
  if (page_number == 0 || page_number > pages->size()) {
    std::cerr << std::format("cpdf: {}: page {} out of 1..{}\n", path, page_number, pages->size());
    return kExitUsage;
  }
  auto list = capriole::content::interpret_page(*document, (*pages)[page_number - 1], {}, &resources, capriole::content::Purpose::kText);
  if (!list) {
    return fail(path, list.error());
  }
  const capriole::text::PageText text = capriole::text::extract(*list, order);
  if (!words) {
    std::cout << text.plain();
    return kExitOk;
  }
  // word, box, provenance of its first character: stream (page = the page's /Contents), operator offset
  // and ordinal, byte of the code.
  for (const auto& word : text.words) {
    const auto& p = text.chars.at(word.first).provenance;
    const std::string stream = p.stream ? std::format("{} {} R", p.stream->number, p.stream->generation) : std::string("page");
    std::cout << std::format("{}\t{:.2f} {:.2f} {:.2f} {:.2f}\t{} @{} #{} +{}\n", capriole::model::to_utf8(word.text), word.box.x0, word.box.y0, word.box.x1,
                             word.box.y1, stream, p.offset, p.operation, p.byte);
  }
  return kExitOk;
}

int cmd_pages(std::string_view path) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto pages = document->page_refs();
  if (!pages) {
    return fail(path, pages.error());
  }
  std::cout << pages->size() << '\n';
  for (const auto& ref : *pages) {
    std::cout << std::format("  {} {} R\n", ref.number, ref.generation);
  }
  return kExitOk;
}

int cmd_save(std::string_view path, std::string_view output, bool incremental, const capriole::writer::SaveOptions& options) {
  auto document = open_document(path);
  if (!document) {
    return fail(path, document.error());
  }
  auto sink = capriole::writer::FileSink::create(output);
  if (!sink) {
    return fail(output, sink.error());
  }
  auto report = incremental ? capriole::writer::save_incremental(*document, **sink, options) : capriole::writer::save_full(*document, **sink, options);
  if (!report) {
    return fail(path, report.error());
  }
  if (auto closed = (*sink)->close(); !closed) {
    return fail(output, closed.error());
  }
  std::cout << std::format("{}: {} objects written, {} bytes", output, report->objects_written, (*sink)->position());
  if (!report->unreadable_objects.empty()) {
    std::cout << std::format(", {} unreadable objects written as null", report->unreadable_objects.size());
  }
  if (report->page_tree_rewritten) {
    std::cout << ", page tree rewritten";
  }
  std::cout << '\n';
  for (const auto& note : document->repairs()) {
    std::cout << std::format("  repaired ({}): {}\n", capriole::repair::to_string(note.kind), note.description);
  }
  return kExitOk;
}

int cmd_object_at(std::string_view path, std::uint64_t offset) {
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
  // No xref here: an indirect /Length cannot be resolved (use `object <file> <number>`).
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

int run(std::span<char*> args) {
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
  if (command == "xref" && args.size() == 3) {
    return cmd_xref(args[2]);
  }
  if (command == "save" && args.size() >= 4) {
    bool incremental = false;
    capriole::writer::SaveOptions options;
    std::size_t i = 2;
    for (; i + 2 < args.size(); ++i) {
      if (std::string_view(args[i]) == "--incremental") {
        incremental = true;
      } else if (std::string_view(args[i]) == "--moddate" && i + 3 < args.size()) {
        options.modification_date = std::string(args[++i]);
      } else {
        break;
      }
    }
    if (i + 2 == args.size()) {
      return cmd_save(args[i], args[i + 1], incremental, options);
    }
  }
  if (command == "pages" && args.size() == 3) {
    return cmd_pages(args[2]);
  }
  if (command == "render") {
    std::optional<std::uint64_t> dpi = 150;
    std::string_view resources_dir;
    std::size_t i = 2;
    for (; i + 1 < args.size(); i += 2) {
      if (std::string_view(args[i]) == "--dpi") {
        dpi = parse_uint(args[i + 1]);
      } else if (std::string_view(args[i]) == "--resources") {
        resources_dir = args[i + 1];
      } else {
        break;
      }
    }
    if (i + 3 == args.size()) {
      if (const auto page = parse_uint(args[i + 1]); page && dpi && *dpi > 0 && *dpi <= 4800) {
        return cmd_render(args[i], *page, args[i + 2], static_cast<double>(*dpi), resources_dir);
      }
    }
  }
  if (command == "text") {
    bool words = false;
    auto order = capriole::text::Order::kContent;
    std::string_view resources_dir;
    std::size_t i = 2;
    while (i < args.size()) {
      if (std::string_view(args[i]) == "--words") {
        words = true;
        ++i;
      } else if (std::string_view(args[i]) == "--order" && i + 1 < args.size()) {
        const std::string_view name = args[i + 1];
        order = name == "layout" ? capriole::text::Order::kLayout : capriole::text::Order::kContent;
        i += 2;
      } else if (std::string_view(args[i]) == "--resources" && i + 1 < args.size()) {
        resources_dir = args[i + 1];
        i += 2;
      } else {
        break;
      }
    }
    if (i + 2 == args.size()) {
      if (const auto page = parse_uint(args[i + 1])) {
        return cmd_text(args[i], *page, words, order, resources_dir);
      }
    }
  }
  if (command == "content" && args.size() == 4) {
    if (const auto page = parse_uint(args[3])) {
      return cmd_content(args[2], *page);
    }
  }
  if (command == "check" && args.size() == 3) {
    return cmd_check(args[2]);
  }
  if ((command == "object" || command == "stream") && (args.size() == 4 || (command == "object" && args.size() == 5))) {
    const auto number = parse_uint(args[3]);
    const auto generation = args.size() == 5 ? parse_uint(args[4]) : std::optional<std::uint64_t>();
    const bool valid = number && *number <= UINT32_MAX && (args.size() == 4 || (generation && *generation <= UINT16_MAX));
    if (valid && command == "stream") {
      return cmd_stream(args[2], static_cast<std::uint32_t>(*number));
    }
    if (valid) {
      return cmd_object(args[2], static_cast<std::uint32_t>(*number),
                        generation ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(*generation)) : std::nullopt);
    }
  }
  if (command == "object-at" && args.size() == 4) {
    if (const auto offset = parse_uint(args[3])) {
      return cmd_object_at(args[2], *offset);
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

}  // namespace

int main(int argc, char** argv) {
  // Errors of the engine are values; what can still throw is the standard library (out of memory).
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& error) {
    std::cerr << "cpdf: internal error: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "cpdf: internal error\n";
  }
  return kExitFailure;
}
