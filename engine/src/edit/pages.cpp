#include "capriole/edit/pages.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "sound.hpp"

namespace capriole::edit {

namespace {

using model::Array;
using model::Dict;
using model::Name;
using model::Object;
using model::ObjectRef;

// Page attributes inherited from ancestors in the page tree (§7.7.3.4).
constexpr std::array<std::string_view, 4> kInheritable{"Resources", "MediaBox", "CropBox", "Rotate"};
// Bounds against hostile trees: the /Parent chain followed, the page tree nodes walked, nesting copied.
constexpr std::size_t kMaxParentChain = 256;
constexpr std::size_t kMaxTreeNodes = 1'000'000;
constexpr std::size_t kMaxCopyDepth = 256;

Object letter_box() { return Array{0, 0, 612, 792}; }

// The root of the page tree: the catalog's /Pages, as written there — and it must read as a dictionary
// through that reference, or a tree written under it would not be found again (a damaged file whose
// pages were found by repairs: a full save writes their tree anew first). Checked before any change.
Result<ObjectRef> tree_root(Document& document) {
  auto catalog = document.catalog();
  if (!catalog) {
    return std::unexpected(std::move(catalog.error()));
  }
  const Object* pages = (*catalog)->find("Pages");
  const auto ref = pages != nullptr ? pages->as_ref() : std::nullopt;
  if (!ref) {
    return make_error(ErrorCode::kSyntax, "the catalog has no /Pages reference");
  }
  auto root = document.get(*ref);
  if (!root || (*root)->as_dict() == nullptr || !(*root)->as_dict()->contains("Kids")) {
    return make_error(ErrorCode::kUnsupported,
                      "the root of the page tree cannot be read as it is referenced: save the document fully first, which writes the tree anew");
  }
  return *ref;
}

// The page with the attributes it inherits from its ancestors written on it, nearest ancestor first,
// stopping before the node numbered stop (whose attributes still apply under it); every ancestor
// without stop. changed tells whether anything was added.
Dict with_inherited(Document& document, const Dict& page, std::optional<std::uint32_t> stop, bool& changed) {
  Dict out = page;
  const Dict* node = &page;
  std::set<std::uint32_t> seen;
  for (std::size_t depth = 0; depth < kMaxParentChain; ++depth) {
    const Object* parent = node->find("Parent");
    const auto parent_ref = parent != nullptr ? parent->as_ref() : std::nullopt;
    if (!parent_ref || (stop && parent_ref->number == *stop) || !seen.insert(parent_ref->number).second) {
      break;
    }
    auto parent_object = document.get(*parent_ref);
    node = parent_object ? (*parent_object)->as_dict() : nullptr;
    if (node == nullptr) {
      break;
    }
    for (const std::string_view key : kInheritable) {
      if (const Object* value = node->find(key); value != nullptr && !out.contains(key)) {
        out.set(std::string(key), *value);
        changed = true;
      }
    }
  }
  return out;
}

// The /Pages nodes under the root (not the root itself), walking /Kids.
std::vector<std::uint32_t> intermediate_nodes(Document& document, ObjectRef root) {
  std::vector<std::uint32_t> nodes;
  std::set<std::uint32_t> seen{root.number};
  std::deque<std::uint32_t> pending{root.number};
  while (!pending.empty() && seen.size() < kMaxTreeNodes) {
    const std::uint32_t number = pending.front();
    pending.pop_front();
    auto object = document.get(number);
    const Dict* dict = object ? (*object)->as_dict() : nullptr;
    const Object* kids = dict != nullptr ? dict->find("Kids") : nullptr;
    auto resolved = kids != nullptr ? document.resolve(*kids) : Result<Object>(Object());
    const Array* array = resolved ? resolved->as_array() : nullptr;
    if (array == nullptr) {
      continue;
    }
    for (const Object& kid : *array) {
      const auto ref = kid.as_ref();
      if (!ref || !seen.insert(ref->number).second) {
        continue;
      }
      auto kid_object = document.get(*ref);
      const Dict* kid_dict = kid_object ? (*kid_object)->as_dict() : nullptr;
      if (kid_dict != nullptr && kid_dict->contains("Kids")) {
        nodes.push_back(ref->number);
        pending.push_back(ref->number);
      }
    }
  }
  return nodes;
}

// A page ready to stand directly under the root: inherited attributes from the nodes in between,
// /Parent the root, a /MediaBox (required, §7.7.3.3). Written only when something changes.
Result<ObjectRef> settle_page(Document& document, ObjectRef page, ObjectRef root, const Dict& root_dict) {
  auto loaded = document.get(page.number);
  const Dict* dict = loaded ? (*loaded)->as_dict() : nullptr;
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "a page cannot be read");
  }
  bool changed = false;
  Dict settled = with_inherited(document, *dict, root.number, changed);
  const Object* parent = settled.find("Parent");
  if (parent == nullptr || parent->as_ref() != std::optional<ObjectRef>(root)) {
    settled.set("Parent", root);
    changed = true;
  }
  if (const Object* type = settled.find("Type"); type == nullptr || !type->is_name("Page")) {
    settled.set("Type", Name{.bytes = "Page"});
    changed = true;
  }
  if (!settled.contains("MediaBox") && !root_dict.contains("MediaBox")) {
    settled.set("MediaBox", letter_box());
    changed = true;
  }
  if (changed) {
    if (auto set = document.set(page.number, Object(std::move(settled))); !set) {
      return std::unexpected(std::move(set.error()));
    }
  }
  return page;
}

// The page tree becomes the root listing these pages; the intermediate nodes are deleted.
Result<void> install_pages(Document& document, const std::vector<ObjectRef>& pages) {
  auto root = tree_root(document);
  if (!root) {
    return std::unexpected(std::move(root.error()));
  }
  auto root_object = document.get(*root);
  if (!root_object || (*root_object)->as_dict() == nullptr) {
    return make_error(ErrorCode::kSyntax, "the root of the page tree is not a dictionary");
  }
  Dict root_dict = *(*root_object)->as_dict();
  std::set<std::uint32_t> listed;
  for (const ObjectRef page : pages) {
    if (page.number == root->number) {
      return make_error(ErrorCode::kUnsupported, "the root of the page tree is itself a page: save the document fully first");
    }
    listed.insert(page.number);
  }
  const std::vector<std::uint32_t> nodes = intermediate_nodes(document, *root);
  Array kids;
  for (const ObjectRef page : pages) {
    auto settled = settle_page(document, page, *root, root_dict);
    if (!settled) {
      return std::unexpected(std::move(settled.error()));
    }
    kids.emplace_back(*settled);
  }
  root_dict.set("Type", Name{.bytes = "Pages"});
  root_dict.set("Count", static_cast<std::int64_t>(kids.size()));
  root_dict.set("Kids", std::move(kids));
  root_dict.erase("Parent");
  if (auto set = document.set(root->number, Object(std::move(root_dict))); !set) {
    return set;
  }
  for (const std::uint32_t node : nodes) {
    // A node that cannot be removed is gone already: nothing to do.
    if (!listed.contains(node) && !document.remove(node)) {
      continue;
    }
  }
  return {};
}

// A second page object showing the same page: its own copy of the dictionary (inherited attributes
// written on it) and of its annotations — but not form widgets (a field's widgets are the field's) and
// popups (paired with their parent) — and no place in the structure tree.
Result<ObjectRef> duplicate_page(Document& document, ObjectRef page) {
  auto loaded = document.get(page.number);
  const Dict* dict = loaded ? (*loaded)->as_dict() : nullptr;
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "a page to duplicate cannot be read");
  }
  bool changed = false;
  Dict copy = with_inherited(document, *dict, std::nullopt, changed);
  copy.erase("StructParents");
  auto added = document.add(Object());
  if (!added) {
    return added;
  }
  if (const Object* annots = copy.find("Annots")) {
    auto resolved = document.resolve(*annots);
    Array copied;
    if (const Array* array = resolved ? resolved->as_array() : nullptr) {
      for (const Object& entry : *array) {
        auto annotation = document.resolve(entry);
        const Dict* annotation_dict = annotation ? annotation->as_dict() : nullptr;
        if (annotation_dict == nullptr) {
          continue;
        }
        const Object* subtype = annotation_dict->find("Subtype");
        if (subtype != nullptr && (subtype->is_name("Widget") || subtype->is_name("Popup"))) {
          continue;
        }
        Dict annotation_copy = *annotation_dict;
        annotation_copy.set("P", *added);
        annotation_copy.erase("Popup");
        annotation_copy.erase("StructParent");
        auto annotation_ref = document.add(Object(std::move(annotation_copy)));
        if (!annotation_ref) {
          return annotation_ref;
        }
        copied.emplace_back(*annotation_ref);
      }
    }
    if (copied.empty()) {
      copy.erase("Annots");
    } else {
      copy.set("Annots", std::move(copied));
    }
  }
  if (auto set = document.set(added->number, Object(std::move(copy))); !set) {
    return std::unexpected(std::move(set.error()));
  }
  return *added;
}

Result<std::vector<ObjectRef>> pages_of(Document& document) {
  auto pages = document.page_refs();
  if (!pages) {
    return pages;
  }
  return std::move(*pages);
}

Result<void> require_sound(Document& document) { return detail::require_sound_structure(document, "pages"); }

// Copies objects of one document into another: each object once, at a new number, references
// rewritten; the source's pages map to the imported ones, or to null.
class Copier {
 public:
  Copier(Document& target, Document& source, const ImportLimits& limits) : target_(&target), source_(&source), limits_(&limits) {}

  // Pages of the source: those imported (with their number in the target) and all of them.
  void set_pages(std::map<std::uint32_t, ObjectRef> imported, std::set<std::uint32_t> all) {
    imported_ = std::move(imported);
    source_pages_ = std::move(all);
  }

  Result<Object> copy(const Object& object, std::size_t depth = 0) {
    if (depth > kMaxCopyDepth) {
      return make_error(ErrorCode::kLimitExceeded, "objects nested too deeply to copy");
    }
    if (const auto ref = object.as_ref()) {
      return map(*ref);
    }
    if (const Array* array = object.as_array()) {
      Array out;
      out.reserve(array->size());
      for (const Object& element : *array) {
        auto copied = copy(element, depth + 1);
        if (!copied) {
          return copied;
        }
        out.push_back(std::move(*copied));
      }
      return Object(std::move(out));
    }
    if (const Dict* dict = object.as_dict()) {
      auto copied = copy_dict(*dict, depth);
      if (!copied) {
        return std::unexpected(std::move(copied.error()));
      }
      return Object(std::move(*copied));
    }
    if (const model::Stream* stream = object.as_stream()) {
      auto dict = copy_dict(stream->dict, depth);
      if (!dict) {
        return std::unexpected(std::move(dict.error()));
      }
      auto data = source_->encoded_stream_data(*stream);
      if (!data) {
        return std::unexpected(std::move(data.error()));
      }
      bytes_ += data->size();
      if (bytes_ > limits_->max_stream_bytes) {
        return make_error(ErrorCode::kLimitExceeded, "the pages' streams are too large to import");
      }
      model::Stream out;
      out.dict = std::move(*dict);
      out.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*data));
      return Object(std::move(out));
    }
    return object;
  }

  Result<Dict> copy_dict(const Dict& dict, std::size_t depth) {
    Dict out;
    const Object* action = dict.find("S");
    const bool go_to = action != nullptr && action->is_name("GoTo");
    for (const model::DictEntry& entry : dict.entries()) {
      // No structure tree in the target: what ties content to it goes.
      if (entry.key == "StructParent" || entry.key == "StructParents") {
        continue;
      }
      // A named destination would be looked up in the target's names, a different document's.
      const bool named = entry.value.as_string() != nullptr || entry.value.as_name() != nullptr;
      if (named && (entry.key == "Dest" || (go_to && entry.key == "D"))) {
        continue;
      }
      auto copied = copy(entry.value, depth + 1);
      if (!copied) {
        return std::unexpected(std::move(copied.error()));
      }
      out.set(entry.key, std::move(*copied));
    }
    return out;
  }

  // Copies the objects reached so far, and those they reach.
  Result<void> run() {
    while (!pending_.empty()) {
      const auto [from, to] = pending_.front();
      pending_.pop_front();
      auto object = source_->get(from);
      if (!object) {
        continue;  // unreadable: it stays null in the target, as it reads in the source
      }
      auto copied = copy(**object);
      if (!copied) {
        return std::unexpected(std::move(copied.error()));
      }
      if (auto set = target_->set(to.number, std::move(*copied)); !set) {
        return set;
      }
    }
    return {};
  }

  // The copies made: source number → target reference.
  [[nodiscard]] const std::map<std::uint32_t, ObjectRef>& copies() const { return copies_; }

 private:
  Result<Object> map(ObjectRef ref) {
    if (source_pages_.contains(ref.number)) {
      const auto found = imported_.find(ref.number);
      return found != imported_.end() ? Object(found->second) : Object();
    }
    if (const auto found = copies_.find(ref.number); found != copies_.end()) {
      return Object(found->second);
    }
    if (copies_.size() >= limits_->max_objects) {
      return make_error(ErrorCode::kLimitExceeded, "too many objects to import");
    }
    auto placeholder = target_->add(Object());
    if (!placeholder) {
      return std::unexpected(std::move(placeholder.error()));
    }
    copies_.emplace(ref.number, *placeholder);
    pending_.emplace_back(ref.number, *placeholder);
    return Object(*placeholder);
  }

  Document* target_;
  Document* source_;
  const ImportLimits* limits_;
  std::map<std::uint32_t, ObjectRef> imported_;
  std::set<std::uint32_t> source_pages_;
  std::map<std::uint32_t, ObjectRef> copies_;
  std::deque<std::pair<std::uint32_t, ObjectRef>> pending_;
  std::uint64_t bytes_ = 0;
};

// The catalog's /AcroForm as a dictionary, and where to write it back.
struct Form {
  Dict dict;
  std::optional<ObjectRef> ref;  // an indirect /AcroForm; otherwise it lives in the catalog
};

Result<std::optional<Form>> form_of(Document& document) {
  auto catalog = document.catalog();
  if (!catalog) {
    return std::unexpected(std::move(catalog.error()));
  }
  const Object* acro = (*catalog)->find("AcroForm");
  if (acro == nullptr) {
    return std::optional<Form>();
  }
  auto resolved = document.resolve(*acro);
  if (!resolved || resolved->as_dict() == nullptr) {
    return std::optional<Form>();
  }
  return std::optional<Form>(Form{.dict = *resolved->as_dict(), .ref = acro->as_ref()});
}

// Form fields among the copies with no parent: the top of each field tree.
std::vector<ObjectRef> top_fields(Document& document, const std::map<std::uint32_t, ObjectRef>& copies) {
  std::vector<ObjectRef> fields;
  for (const auto& [from, to] : copies) {
    auto object = document.get(to);
    const Dict* dict = object ? (*object)->as_dict() : nullptr;
    if (dict != nullptr && (dict->contains("FT") || dict->contains("T")) && !dict->contains("Parent")) {
      fields.push_back(to);
    }
  }
  return fields;
}

// The imported fields join the document's form, renamed when their name is taken; the source form's
// default resources (fonts) the document's lacks come with them.
Result<void> merge_fields(Document& document, Document& source, Copier& copier) {
  const std::vector<ObjectRef> fields = top_fields(document, copier.copies());
  if (fields.empty()) {
    return {};
  }
  auto existing = form_of(document);
  if (!existing) {
    return std::unexpected(std::move(existing.error()));
  }
  Form form = existing->value_or(Form{});
  Array list;
  std::set<std::string> names;
  if (const Object* current = form.dict.find("Fields")) {
    auto resolved = document.resolve(*current);
    if (const Array* array = resolved ? resolved->as_array() : nullptr) {
      list = *array;
      for (const Object& field : *array) {
        auto field_object = document.resolve(field);
        const Dict* field_dict = field_object ? field_object->as_dict() : nullptr;
        const Object* name = field_dict != nullptr ? field_dict->find("T") : nullptr;
        if (name != nullptr && name->as_string() != nullptr) {
          names.insert(name->as_string()->bytes);
        }
      }
    }
  }
  for (const ObjectRef field : fields) {
    auto object = document.get(field);
    if (!object || (*object)->as_dict() == nullptr) {
      continue;
    }
    Dict dict = *(*object)->as_dict();
    if (const Object* name = dict.find("T"); name != nullptr && name->as_string() != nullptr) {
      const std::string base = name->as_string()->bytes;
      std::string unique = base;
      for (std::size_t n = 2; names.contains(unique); ++n) {
        unique = base + "_" + std::to_string(n);
      }
      names.insert(unique);
      if (unique != base) {
        dict.set("T", model::String{.bytes = unique});
        if (auto set = document.set(field.number, Object(std::move(dict))); !set) {
          return set;
        }
      }
    }
    list.emplace_back(field);
  }
  form.dict.set("Fields", std::move(list));
  // Default resources and appearance of the source form, where the document has none.
  if (auto source_form = form_of(source); source_form && *source_form) {
    const Dict& from = (*source_form)->dict;
    if (const Object* da = from.find("DA"); da != nullptr && !form.dict.contains("DA")) {
      form.dict.set("DA", *da);
    }
    if (const Object* dr = from.find("DR")) {
      auto source_dr = source.resolve(*dr);
      auto copied = source_dr ? copier.copy(*source_dr) : Result<Object>(Object());
      if (!copied) {
        return std::unexpected(std::move(copied.error()));
      }
      if (auto ran = copier.run(); !ran) {
        return ran;
      }
      Dict target_dr;
      if (const Object* current = form.dict.find("DR")) {
        auto resolved = document.resolve(*current);
        if (resolved && resolved->as_dict() != nullptr) {
          target_dr = *resolved->as_dict();
        }
      }
      if (const Dict* copied_dr = copied->as_dict()) {
        for (const model::DictEntry& category : copied_dr->entries()) {
          auto from_category = document.resolve(category.value);
          const Dict* additions = from_category ? from_category->as_dict() : nullptr;
          if (additions == nullptr) {
            continue;
          }
          Dict merged;
          if (const Object* current = target_dr.find(category.key)) {
            auto resolved = document.resolve(*current);
            if (resolved && resolved->as_dict() != nullptr) {
              merged = *resolved->as_dict();
            }
          }
          for (const model::DictEntry& entry : additions->entries()) {
            if (!merged.contains(entry.key)) {
              merged.set(entry.key, entry.value);
            }
          }
          target_dr.set(category.key, std::move(merged));
        }
      }
      form.dict.set("DR", std::move(target_dr));
    }
  }
  if (form.ref) {
    return document.set(form.ref->number, Object(std::move(form.dict)));
  }
  auto catalog = document.catalog();
  const Object* root = document.trailer().find("Root");
  const auto catalog_ref = root != nullptr ? root->as_ref() : std::nullopt;
  if (!catalog || !catalog_ref) {
    return make_error(ErrorCode::kSyntax, "the document has no catalog to hold its form");
  }
  Dict updated = **catalog;
  updated.set("AcroForm", std::move(form.dict));
  return document.set(catalog_ref->number, Object(std::move(updated)));
}

}  // namespace

Result<void> rotate_pages(Document& document, std::span<const std::uint32_t> pages, std::int32_t degrees) {
  constexpr std::int32_t kQuarter = 90;
  if (degrees % kQuarter != 0) {
    return make_error(ErrorCode::kUnsupported, "pages rotate by multiples of 90 degrees");
  }
  auto all = pages_of(document);
  if (!all) {
    return std::unexpected(std::move(all.error()));
  }
  if (auto sound = require_sound(document); !sound) {
    return sound;
  }
  for (const std::uint32_t index : pages) {
    if (index >= all->size()) {
      return make_error(ErrorCode::kOutOfRange, "page index " + std::to_string(index) + " out of 0.." + std::to_string(all->size()));
    }
  }
  std::set<std::uint32_t> done;
  for (const std::uint32_t index : pages) {
    const ObjectRef page = (*all)[index];
    if (!done.insert(page.number).second) {
      continue;
    }
    auto loaded = document.get(page);
    const Dict* dict = loaded ? (*loaded)->as_dict() : nullptr;
    if (dict == nullptr) {
      return make_error(ErrorCode::kSyntax, "page " + std::to_string(index + 1) + " cannot be read");
    }
    bool inherited = false;
    const Dict full = with_inherited(document, *dict, std::nullopt, inherited);
    const Object* current = full.find("Rotate");
    const std::int64_t now = current != nullptr ? current->as_integer().value_or(0) : 0;
    constexpr std::int64_t kTurn = 360;
    const std::int64_t rotated = (((now + degrees) % kTurn) + kTurn) % kTurn;
    Dict updated = *dict;
    updated.set("Rotate", rotated);
    if (auto set = document.set(page.number, Object(std::move(updated))); !set) {
      return set;
    }
  }
  return {};
}

Result<void> arrange_pages(Document& document, std::span<const std::uint32_t> order) {
  auto all = pages_of(document);
  if (!all) {
    return std::unexpected(std::move(all.error()));
  }
  if (auto root = tree_root(document); !root) {
    return std::unexpected(std::move(root.error()));
  }
  if (auto sound = require_sound(document); !sound) {
    return sound;
  }
  if (order.empty()) {
    return make_error(ErrorCode::kOutOfRange, "a document keeps at least one page");
  }
  for (const std::uint32_t index : order) {
    if (index >= all->size()) {
      return make_error(ErrorCode::kOutOfRange, "page index " + std::to_string(index) + " out of 0.." + std::to_string(all->size()));
    }
  }
  std::set<std::uint32_t> kept;
  std::vector<ObjectRef> pages;
  pages.reserve(order.size());
  for (const std::uint32_t index : order) {
    const ObjectRef page = (*all)[index];
    if (kept.insert(page.number).second) {
      pages.push_back(page);
      continue;
    }
    auto duplicate = duplicate_page(document, page);
    if (!duplicate) {
      return std::unexpected(std::move(duplicate.error()));
    }
    pages.push_back(*duplicate);
  }
  if (auto installed = install_pages(document, pages); !installed) {
    return installed;
  }
  for (const ObjectRef page : *all) {
    // A page that cannot be removed is gone already (listed twice by a damaged tree).
    if (!kept.contains(page.number) && !document.remove(page.number)) {
      continue;
    }
  }
  return {};
}

Result<std::vector<ObjectRef>> import_pages(Document& document, Document& source, std::span<const std::uint32_t> pages, std::uint32_t at,
                                            const ImportLimits& limits) {
  if (&document == &source) {
    return make_error(ErrorCode::kUnsupported, "pages are imported from another document (arrange_pages duplicates within one)");
  }
  auto target_pages = pages_of(document);
  if (!target_pages) {
    return std::unexpected(std::move(target_pages.error()));
  }
  if (auto root = tree_root(document); !root) {
    return std::unexpected(std::move(root.error()));
  }
  if (auto sound = require_sound(document); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  if (at > target_pages->size()) {
    return make_error(ErrorCode::kOutOfRange, "insertion point " + std::to_string(at) + " out of 0.." + std::to_string(target_pages->size()));
  }
  auto source_pages = pages_of(source);
  if (!source_pages) {
    return std::unexpected(std::move(source_pages.error()));
  }
  std::set<std::uint32_t> all_source;
  for (const ObjectRef page : *source_pages) {
    all_source.insert(page.number);
  }
  // Each imported page gets its number first, so that references between the pages (an annotation's
  // /P, a link to another imported page) find it.
  std::map<std::uint32_t, ObjectRef> imported;
  std::vector<std::pair<ObjectRef, ObjectRef>> order;  // (source page, target page)
  for (const std::uint32_t index : pages) {
    if (index >= source_pages->size()) {
      return make_error(ErrorCode::kOutOfRange, "source page index " + std::to_string(index) + " out of 0.." + std::to_string(source_pages->size()));
    }
    const ObjectRef page = (*source_pages)[index];
    if (imported.contains(page.number)) {
      return make_error(ErrorCode::kUnsupported, "a source page is imported once per call");
    }
    auto placeholder = document.add(Object());
    if (!placeholder) {
      return std::unexpected(std::move(placeholder.error()));
    }
    imported.emplace(page.number, *placeholder);
    order.emplace_back(page, *placeholder);
  }
  Copier copier(document, source, limits);
  copier.set_pages(imported, all_source);
  std::vector<ObjectRef> added;
  for (const auto& [from, to] : order) {
    auto loaded = source.get(from);
    const Dict* dict = loaded ? (*loaded)->as_dict() : nullptr;
    Dict page;
    if (dict != nullptr && looks_like_page(*dict)) {
      bool changed = false;
      Dict full = with_inherited(source, *dict, std::nullopt, changed);
      full.erase("Parent");
      full.erase("B");  // article beads: threads are not carried over
      auto copied = copier.copy_dict(full, 0);
      if (!copied) {
        return std::unexpected(std::move(copied.error()));
      }
      page = std::move(*copied);
    }
    page.set("Type", Name{.bytes = "Page"});
    if (!page.contains("MediaBox")) {
      page.set("MediaBox", letter_box());
    }
    if (auto set = document.set(to.number, Object(std::move(page))); !set) {
      return std::unexpected(std::move(set.error()));
    }
    added.push_back(to);
  }
  if (auto ran = copier.run(); !ran) {
    return std::unexpected(std::move(ran.error()));
  }
  if (auto merged = merge_fields(document, source, copier); !merged) {
    return std::unexpected(std::move(merged.error()));
  }
  const auto split = target_pages->begin() + static_cast<std::ptrdiff_t>(at);
  std::vector<ObjectRef> list(target_pages->begin(), split);
  list.insert(list.end(), added.begin(), added.end());
  list.insert(list.end(), split, target_pages->end());
  if (auto installed = install_pages(document, list); !installed) {
    return std::unexpected(std::move(installed.error()));
  }
  return added;
}

}  // namespace capriole::edit
