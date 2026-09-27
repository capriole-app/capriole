#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "capriole/writer/save.hpp"
#include "capriole/writer/serializer.hpp"
#include "save_support.hpp"

namespace capriole::writer {

namespace {

using model::Object;

// Objects not written by a full save: containers of other objects (object streams, cross-reference
// streams) and linearization dictionaries, which would describe a layout the new file does not have.
bool is_structural(const Object& object) {
  const model::Dict* dict = object.as_dict();
  if (const model::Stream* stream = object.as_stream()) {
    dict = &stream->dict;
  }
  if (dict == nullptr) {
    return false;
  }
  const Object* type = dict->find("Type");
  const bool container = object.as_stream() != nullptr && type != nullptr && (type->is_name("ObjStm") || type->is_name("XRef"));
  return container || dict->contains("Linearized");
}

Result<void> write_all(ByteSink& sink, std::string_view text) { return sink.write(text); }

// Page attributes a page inherits from its ancestors in the page tree (§7.7.3.4).
constexpr std::array<std::string_view, 4> kInheritable{"Resources", "MediaBox", "CropBox", "Rotate"};
// Bound on the /Parent chain followed for inherited attributes (a hostile file can make it a loop).
constexpr std::size_t kMaxParentChain = 256;

// When the pages were found by page tree repairs, those repairs exist only in memory: the tree is
// written anew, flat (the root /Pages node lists every page), so that the output reads without them.
// Pages take the attributes they inherited from the nodes left out, and a page that cannot be read
// (kept by the repair so that the numbering of the others stays right) becomes a blank page — at its
// own number when the object is unreadable anyway, at a new one otherwise. Returns the objects to
// write in place of the document's, by number (empty when the tree conforms or has no pages). A page
// without a /MediaBox anywhere gets the default one.
Result<std::map<std::uint32_t, Object>> flattened_page_tree(Document& document, const std::set<std::uint32_t>& live, std::uint32_t& next_number) {
  std::map<std::uint32_t, Object> overrides;
  auto pages = document.page_refs();
  if (!pages || pages->empty() || document.page_tree_conforms(*pages)) {
    return overrides;
  }
  auto catalog = document.catalog();
  const Object* pages_object = catalog ? (*catalog)->find("Pages") : nullptr;
  const auto root = pages_object != nullptr ? pages_object->as_ref() : std::nullopt;
  if (!root || !live.contains(root->number)) {
    return overrides;
  }
  model::ObjectRef tree_root{.number = root->number, .generation = document.generation(root->number)};
  model::Dict root_node;
  const auto lists_root = [&](model::ObjectRef page) { return page.number == root->number; };
  if (std::ranges::any_of(*pages, lists_root)) {
    // The root was taken as the one page (a lone node without /Type): it stays the page, under a new
    // /Pages root that the catalog now points to. More pages than that would make a loop.
    const Object* catalog_object = document.trailer().find("Root");
    const auto catalog_ref = catalog_object != nullptr ? catalog_object->as_ref() : std::nullopt;
    if (pages->size() != 1 || !catalog_ref || !live.contains(catalog_ref->number)) {
      return overrides;
    }
    tree_root = model::ObjectRef{.number = next_number++, .generation = 0};
    model::Dict new_catalog = **catalog;
    new_catalog.set("Pages", tree_root);
    overrides.insert_or_assign(catalog_ref->number, Object(std::move(new_catalog)));
  } else if (auto loaded = document.get(root->number); loaded && (*loaded)->as_dict() != nullptr) {
    root_node = *(*loaded)->as_dict();
  }

  model::Array kids;
  for (const model::ObjectRef ref : *pages) {
    auto loaded = document.get(ref);
    const model::Dict* original = loaded ? (*loaded)->as_dict() : nullptr;
    if (original != nullptr && !looks_like_page(*original)) {
      original = nullptr;  // another object in a page's place (e.g. a font): kept as it is, the page is blank
    }
    model::Dict page;
    std::uint32_t number = ref.number;
    if (original != nullptr) {
      page = *original;
      // Inherited attributes, nearest ancestor first; the root keeps its own, so it is not copied.
      const model::Dict* node = original;
      std::set<std::uint32_t> seen;
      for (std::size_t depth = 0; depth < kMaxParentChain; ++depth) {
        const Object* parent = node->find("Parent");
        const auto parent_ref = parent != nullptr ? parent->as_ref() : std::nullopt;
        if (!parent_ref || parent_ref->number == root->number || !seen.insert(parent_ref->number).second) {
          break;
        }
        auto parent_object = document.get(*parent_ref);
        node = parent_object ? (*parent_object)->as_dict() : nullptr;
        if (node == nullptr) {
          break;
        }
        for (const std::string_view key : kInheritable) {
          if (const Object* value = node->find(key); value != nullptr && !page.contains(key)) {
            page.set(std::string(key), *value);
          }
        }
      }
      if (ref.number == root->number) {
        page.erase("Count");  // the former root: now a page
        page.erase("Kids");
      }
    } else if (!live.contains(number) || document.get(number)) {
      number = next_number++;  // the object exists and is something else: keep it, use a new number
    }
    if (!page.contains("MediaBox") && !root_node.contains("MediaBox")) {
      // Required (§7.7.3.3) and lost with the damaged nodes (or a blank page): US Letter, as the
      // reference readers assume.
      page.set("MediaBox", model::Array{0, 0, 612, 792});
    }
    page.set("Type", model::Name{.bytes = "Page"});
    page.set("Parent", tree_root);
    const std::uint16_t generation = live.contains(number) ? document.generation(number) : 0;
    kids.emplace_back(model::ObjectRef{.number = number, .generation = generation});
    overrides.insert_or_assign(number, Object(std::move(page)));
  }
  root_node.set("Type", model::Name{.bytes = "Pages"});
  root_node.set("Count", static_cast<std::int64_t>(kids.size()));
  root_node.set("Kids", std::move(kids));
  root_node.erase("Parent");
  overrides.insert_or_assign(tree_root.number, Object(std::move(root_node)));
  return overrides;
}

}  // namespace

Result<SaveReport> save_full(Document& document, ByteSink& output, const SaveOptions& options) {
  if (document.security() != nullptr) {
    return make_error(ErrorCode::kUnsupported, "saving encrypted documents is not supported yet (re-encryption is phase 3d)");
  }
  detail::HashingSink sink(output);
  SaveReport report;

  // Header, with a comment of high bytes so that transfer tools treat the file as binary (§7.5.2).
  const auto& header = document.header();
  if (auto ok = write_all(sink, std::format("%PDF-{}.{}\n%\xE2\xE3\xCF\xD3\n", header.major, header.minor)); !ok) {
    return std::unexpected(std::move(ok.error()));
  }

  model::Dict trailer = document.trailer();
  std::optional<model::ObjectRef> info;
  if (const Object* info_object = trailer.find("Info")) {
    info = info_object->as_ref();
  }

  std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> written;  // number -> (offset, generation)
  const auto write_object = [&](model::ObjectRef ref, const Object& object) -> Result<void> {
    const std::uint64_t offset = sink.position();
    if (auto ok = detail::write_indirect(document, sink, ref, object); !ok) {
      return ok;
    }
    written[ref.number] = {offset, ref.generation};
    ++report.objects_written;
    return {};
  };

  const std::vector<model::ObjectRef> live_objects = document.live_objects();
  std::set<std::uint32_t> live;
  for (const model::ObjectRef ref : live_objects) {
    live.insert(ref.number);
  }
  std::uint32_t size = document.next_object_number();  // grows with the objects the save adds
  auto overrides = flattened_page_tree(document, live, size);
  if (!overrides) {
    return std::unexpected(std::move(overrides.error()));
  }
  report.page_tree_rewritten = !overrides->empty();

  for (const model::ObjectRef ref : live_objects) {
    auto loaded = document.get(ref.number);
    Object object;
    if (loaded) {
      object = **loaded;
    } else {
      report.unreadable_objects.push_back(ref.number);  // written as null: references stay valid
    }
    if (auto it = overrides->find(ref.number); it != overrides->end()) {
      object = std::move(it->second);
      overrides->erase(it);
    }
    if (is_structural(object)) {
      continue;
    }
    if (options.modification_date && info && ref.number == info->number) {
      if (model::Dict* dict = object.as_dict()) {
        dict->set("ModDate", model::String{.bytes = *options.modification_date});
      }
    }
    auto ok = write_object(ref, object);
    if (!ok && ok.error().code != ErrorCode::kIo) {
      // Stream data that cannot be read: the object becomes null rather than failing the save.
      report.unreadable_objects.push_back(ref.number);
      ok = write_object(ref, Object(model::Null{}));
    }
    if (!ok) {
      return std::unexpected(std::move(ok.error()));
    }
  }

  // Blank pages added in place of unreadable ones (at new numbers).
  for (const auto& [number, object] : *overrides) {
    if (auto ok = write_object(model::ObjectRef{.number = number, .generation = 0}, object); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
  }

  // A document information dictionary for the modification date, when the file has none.
  if (options.modification_date && !info) {
    model::Dict dict;
    dict.set("ModDate", model::String{.bytes = *options.modification_date});
    const model::ObjectRef ref{.number = size, .generation = 0};
    if (auto ok = write_object(ref, Object(std::move(dict))); !ok) {
      return std::unexpected(std::move(ok.error()));
    }
    trailer.set("Info", ref);
    ++size;
  }
  if (!written.empty()) {
    size = std::max(size, written.rbegin()->first + 1);
  }

  // Cross-reference table: one subsection, free entries chained in ascending order (§7.5.4).
  const std::uint64_t xref_offset = sink.position();
  std::string xref = std::format("xref\n0 {}\n", size);
  std::vector<std::uint32_t> free_numbers;  // ascending; each free entry points to the next one
  for (std::uint32_t number = 0; number < size; ++number) {
    if (!written.contains(number)) {
      free_numbers.push_back(number);
    }
  }
  std::size_t free_index = 0;
  for (std::uint32_t number = 0; number < size; ++number) {
    if (const auto it = written.find(number); it != written.end()) {
      xref += std::format("{:010} {:05} n \n", it->second.first, it->second.second);
      continue;
    }
    // A deleted object's generation goes up by one, so old references to it stay dead (§7.5.4).
    std::uint32_t generation = 0;
    if (number == 0) {
      generation = 65535;
    } else if (document.state(number) == ObjectState::kDeleted) {
      generation = std::min<std::uint32_t>(document.generation(number) + 1U, 65535);
    }
    ++free_index;
    const std::uint32_t next = free_index < free_numbers.size() ? free_numbers[free_index] : 0;
    xref += std::format("{:010} {:05} f \n", next, generation);
  }
  if (auto ok = write_all(sink, xref); !ok) {
    return std::unexpected(std::move(ok.error()));
  }

  // Trailer: the document's entries, a fresh /Size, and /ID [original-or-new, hash of this file].
  trailer.set("Size", static_cast<std::int64_t>(size));
  trailer.set("ID", detail::file_identifier(trailer, sink.digest()));
  trailer.erase("Encrypt");
  std::string tail = "trailer\n";
  serialize(Object(std::move(trailer)), tail);
  tail += std::format("\nstartxref\n{}\n%%EOF\n", xref_offset);
  if (auto ok = write_all(sink, tail); !ok) {
    return std::unexpected(std::move(ok.error()));
  }
  return report;
}

}  // namespace capriole::writer
