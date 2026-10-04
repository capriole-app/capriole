#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
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
  if (!root) {
    return overrides;
  }
  model::ObjectRef tree_root{.number = root->number, .generation = document.generation(root->number)};
  model::Dict root_node;
  const auto lists_root = [&](model::ObjectRef page) { return page.number == root->number; };
  const Object* catalog_object = document.trailer().find("Root");
  const auto catalog_ref = catalog_object != nullptr ? catalog_object->as_ref() : std::nullopt;
  const bool root_is_page = std::ranges::any_of(*pages, lists_root);
  if (root_is_page || !live.contains(root->number)) {
    // The root was taken as the one page (a lone node without /Type: it stays the page — more pages
    // than that would make a loop), or it does not exist at all (the pages were found through their
    // /Parent links): a new /Pages root, which the catalog now points to.
    if ((root_is_page && pages->size() != 1) || !catalog_ref || !live.contains(catalog_ref->number)) {
      return overrides;
    }
    tree_root = model::ObjectRef{.number = next_number++, .generation = 0};
    model::Dict new_catalog = **catalog;
    new_catalog.set("Pages", tree_root);
    overrides.insert_or_assign(catalog_ref->number, Object(std::move(new_catalog)));
  } else {
    if (auto loaded = document.get(root->number); loaded && (*loaded)->as_dict() != nullptr) {
      root_node = *(*loaded)->as_dict();
    }
    // The catalog may name the root with another generation than the object has (a damaged file read
    // through repairs): the reference is written to match, or the new tree would not be found.
    if (root->generation != tree_root.generation && catalog_ref && live.contains(catalog_ref->number)) {
      model::Dict new_catalog = **catalog;
      new_catalog.set("Pages", tree_root);
      overrides.insert_or_assign(catalog_ref->number, Object(std::move(new_catalog)));
    }
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
      if (catalog_ref && ref.number == catalog_ref->number) {
        // The catalog itself taken as the page (a damaged object holding both): the page is written at
        // a new number, the catalog stays the catalog.
        page.erase("Pages");
        number = next_number++;
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

// The objects reachable from the trailer, through the objects as they will be written (the page tree
// overrides first). Iterative: references can chain as deep as the file likes.
std::set<std::uint32_t> reachable(Document& document, const model::Dict& trailer, const std::map<std::uint32_t, Object>& overrides) {
  std::set<std::uint32_t> seen;
  std::vector<const Object*> pending;
  for (const model::DictEntry& entry : trailer.entries()) {
    pending.push_back(&entry.value);
  }
  while (!pending.empty()) {
    const Object* object = pending.back();
    pending.pop_back();
    if (const auto ref = object->as_ref()) {
      if (!seen.insert(ref->number).second) {
        continue;
      }
      if (const auto it = overrides.find(ref->number); it != overrides.end()) {
        pending.push_back(&it->second);
      } else if (auto loaded = document.get(ref->number)) {
        pending.push_back(*loaded);
      }
    } else if (const model::Array* array = object->as_array()) {
      for (const Object& element : *array) {
        pending.push_back(&element);
      }
    } else if (const model::Dict* dict = object->as_dict()) {
      for (const model::DictEntry& entry : dict->entries()) {
        pending.push_back(&entry.value);
      }
    } else if (const model::Stream* stream = object->as_stream()) {
      for (const model::DictEntry& entry : stream->dict.entries()) {
        pending.push_back(&entry.value);
      }
    }
  }
  return seen;
}

// References to objects that the output does not have become null — which is what they read as
// (§7.3.10) — so that no object added later can take their number and give them a meaning.
void null_dangling(Object& object, const std::set<std::uint32_t>& present) {
  if (const auto ref = object.as_ref()) {
    if (!present.contains(ref->number)) {
      object = Object(model::Null{});
    }
  } else if (model::Array* array = object.as_array()) {
    for (Object& element : *array) {
      null_dangling(element, present);
    }
  } else if (model::Dict* dict = object.as_dict()) {
    for (const model::DictEntry& entry : dict->entries()) {
      null_dangling(*dict->find(entry.key), present);
    }
  } else if (model::Stream* stream = object.as_stream()) {
    for (const model::DictEntry& entry : stream->dict.entries()) {
      null_dangling(*stream->dict.find(entry.key), present);
    }
  }
}

}  // namespace

Result<SaveReport> save_full(Document& document, ByteSink& output, const SaveOptions& options) {
  detail::HashingSink sink(output);
  SaveReport report;

  // Header, with a comment of high bytes so that transfer tools treat the file as binary (§7.5.2).
  const auto& header = document.header();
  const auto [major, minor] = options.version.value_or(std::pair{header.major, header.minor});
  if (auto ok = write_all(sink, std::format("%PDF-{}.{}\n%\xE2\xE3\xCF\xD3\n", major, minor)); !ok) {
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
  std::optional<std::set<std::uint32_t>> kept;
  // After a redaction, what it removed must not stay in the file (Document::redacted()).
  if (options.drop_unreachable || document.redacted()) {
    kept = reachable(document, trailer, *overrides);
  }
  // What the output has: the live objects (all, or those kept) and the ones the page tree adds.
  std::set<std::uint32_t> present;
  for (const std::uint32_t number : live) {
    if (!kept || kept->contains(number)) {
      present.insert(number);  // (kept also holds numbers referenced but missing)
    }
  }
  for (const auto& [number, object] : *overrides) {
    present.insert(number);
  }

  for (const model::ObjectRef ref : live_objects) {
    if (kept && !kept->contains(ref.number)) {
      ++report.objects_dropped;
      continue;
    }
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
    null_dangling(object, present);
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
  for (auto& [number, object] : *overrides) {
    null_dangling(object, present);
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

  // Cross-reference table (§7.5.4): the numbers written, object 0, and the deleted objects (listed as
  // free with their generation up by one, so old references to them stay dead), in subsections of
  // consecutive numbers — small gaps are listed as free entries rather than starting a subsection.
  // Numbers far apart (an object numbered 1666666666 in a small file) cost one subsection, not a line
  // for every number in between. Free entries are chained in ascending order from entry 0.
  constexpr std::uint32_t kGapListed = 64;
  std::map<std::uint32_t, std::uint32_t> free_generation{{0, 65535}};  // number -> generation of a free entry
  for (const std::uint32_t number : document.changed_objects()) {
    if (document.state(number) == ObjectState::kDeleted && !written.contains(number)) {
      free_generation[number] = std::min<std::uint32_t>(document.generation(number) + 1U, 65535);
    }
  }
  std::set<std::uint32_t> listed;
  for (const auto& [number, entry] : written) {
    listed.insert(number);
  }
  for (const auto& [number, generation] : free_generation) {
    listed.insert(number);
  }
  {
    std::vector<std::uint32_t> gaps;
    std::uint32_t previous = 0;
    for (const std::uint32_t number : listed) {
      if (number > previous + 1 && number - previous - 1 <= kGapListed) {
        for (std::uint32_t gap = previous + 1; gap < number; ++gap) {
          gaps.push_back(gap);
        }
      }
      previous = number;
    }
    for (const std::uint32_t gap : gaps) {
      listed.insert(gap);
      free_generation.emplace(gap, 0);
    }
  }
  size = *listed.rbegin() + 1;  // one past the highest number defined (§7.5.5), as the table now ends there
  const std::uint64_t xref_offset = sink.position();
  std::string xref = "xref\n";
  for (auto run = listed.begin(); run != listed.end();) {
    auto end = run;
    std::uint32_t count = 0;
    for (std::uint32_t expected = *run; end != listed.end() && *end == expected; ++end, ++expected) {
      ++count;
    }
    xref += std::format("{} {}\n", *run, count);
    for (auto it = run; it != end; ++it) {
      if (const auto found = written.find(*it); found != written.end()) {
        xref += std::format("{:010} {:05} n \n", found->second.first, found->second.second);
        continue;
      }
      const auto next_free = free_generation.upper_bound(*it);
      xref += std::format("{:010} {:05} f \n", next_free == free_generation.end() ? 0U : next_free->first, free_generation.at(*it));
    }
    run = end;
  }
  if (auto ok = write_all(sink, xref); !ok) {
    return std::unexpected(std::move(ok.error()));
  }

  // Trailer: the document's entries, a fresh /Size, and /ID [original-or-new, hash of this file].
  trailer.set("Size", static_cast<std::int64_t>(size));
  // /Encrypt stays: the document is written encrypted as it was (its dictionary is one of the objects).
  trailer.set("ID", detail::file_identifier(trailer, sink.digest(), document.security() != nullptr));
  std::string tail = "trailer\n";
  serialize(Object(std::move(trailer)), tail);
  tail += std::format("\nstartxref\n{}\n%%EOF\n", xref_offset);
  if (auto ok = write_all(sink, tail); !ok) {
    return std::unexpected(std::move(ok.error()));
  }
  return report;
}

}  // namespace capriole::writer
