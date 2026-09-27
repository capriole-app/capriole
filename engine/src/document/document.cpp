#include "capriole/document/document.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "capriole/filters/decode.hpp"
#include "capriole/repair/reconstruct.hpp"
#include "capriole/repair/repair.hpp"

namespace capriole {

namespace {

using model::Object;

const Object& null_object() {
  static const Object kNull;
  return kNull;
}

// Keys that describe a cross-reference section rather than the document: when the newest trailer
// is a cross-reference stream dictionary, these belong to that stream, not to the document trailer.
constexpr std::array<std::string_view, 12> kSectionKeys = {"Type", "W",       "Index",        "Length", "Filter", "DecodeParms",
                                                           "F",    "FFilter", "FDecodeParms", "DL",     "Prev",   "XRefStm"};

// The /Kids array of a page tree node. With repair, a misspelt key counts too — "Kid", or "Kids" in
// another case — as long as its value is an array; misspelt tells the caller to note the repair.
const model::Array* kids_of(const model::Dict& node, bool repair, bool* misspelt = nullptr) {
  if (const Object* kids = node.find("Kids"); kids != nullptr) {
    return kids->as_array();
  }
  if (!repair) {
    return nullptr;
  }
  for (const model::DictEntry& entry : node.entries()) {
    std::string key = entry.key;
    std::ranges::transform(key, key.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    if ((key == "kids" || key == "kid") && entry.value.as_array() != nullptr) {
      if (misspelt != nullptr) {
        *misspelt = true;
      }
      return entry.value.as_array();
    }
  }
  return nullptr;
}

// Table trust check at open (repair mode): how many entries are sampled, and the fewest that make
// the check meaningful.
constexpr std::size_t kTableSample = 32;
constexpr std::size_t kMinTableSample = 8;

}  // namespace

Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;
Document::~Document() = default;

Result<Document> Document::open(std::unique_ptr<ByteSource> source, const OpenOptions& options) {
  if (source == nullptr) {
    return make_error(ErrorCode::kIo, "no source to open");
  }
  // Strict reading first (§7.1 of the project specification); repairs only if it fails.
  auto strict = build(source, options);
  std::string distrust;
  if (strict && options.repair) {
    if (!strict->root_is_dictionary()) {
      distrust = "the table's /Root is not a dictionary";
    } else if (const auto [misplaced, sampled] = strict->sample_table(); sampled >= kMinTableSample && misplaced * 4 >= sampled) {
      distrust = std::to_string(misplaced) + " of " + std::to_string(sampled) + " sampled entries do not lead to their object";
    }
  }
  if (!distrust.empty()) {
    // The table reads, but cannot be trusted (/Root marked free, entries shifted by a few objects, ...).
    // Rebuilt by scanning, if that leads to a catalog.
    std::unique_ptr<ByteSource> own = std::move(strict->source_);
    if (auto table = repair::reconstruct_xref(*own, options.limits)) {
      const std::size_t objects = table->entries.size() - 1;
      auto rebuilt = build(own, options, &*table);
      if (rebuilt && rebuilt->root_is_dictionary()) {
        rebuilt->note(repair::RepairKind::kXrefReconstructed,
                      "cross-reference table rebuilt by scanning the file (" + std::to_string(objects) + " objects; " + distrust + ")");
        return rebuilt;
      }
      if (rebuilt) {
        own = std::move(rebuilt->source_);  // build() took the source: take it back before discarding
      }
    }
    strict->source_ = std::move(own);
  }
  if (strict || !options.repair || !repair::may_help(strict.error())) {
    return strict;
  }

  // Repair: junk before %PDF-, and offsets that count from the header instead of from byte 0.
  // The file is read through a view that starts at the header, which makes every offset consistent.
  if (const auto junk = repair::junk_before_header(*source)) {
    auto view = std::make_unique<OffsetByteSource>(std::move(source), *junk);
    OffsetByteSource* view_ptr = view.get();
    std::unique_ptr<ByteSource> shifted = std::move(view);
    auto repaired = build(shifted, options);
    if (repaired) {
      repaired->repairs_.push_back(repair::RepairNote{.kind = repair::RepairKind::kHeaderOffset,
                                                      .description = std::to_string(*junk) + " bytes before %PDF-; offsets read relative to the header"});
      return repaired;
    }
    source = view_ptr->release_base();
  }

  // Repair of last resort: rebuild the cross-reference table by scanning the file.
  if (auto table = repair::reconstruct_xref(*source, options.limits)) {
    const std::size_t objects = table->entries.size() - 1;
    auto rebuilt = build(source, options, &*table);
    if (rebuilt) {
      rebuilt->note(repair::RepairKind::kXrefReconstructed, "cross-reference table rebuilt by scanning the file (" + std::to_string(objects) +
                                                                " objects; strict reading failed: " + strict.error().message + ")");
      return rebuilt;
    }
  }
  return strict;
}

Result<Document> Document::build(std::unique_ptr<ByteSource>& source, const OpenOptions& options, const parser::XrefTable* rebuilt) {
  const ParseLimits& limits = options.limits;
  auto header = parser::find_header(*source);
  if (!header) {
    return std::unexpected(std::move(header.error()));
  }
  auto xref = rebuilt != nullptr ? Result<parser::XrefTable>(*rebuilt) : parser::read_xref(*source, limits);
  if (!xref) {
    return std::unexpected(std::move(xref.error()));
  }

  Document document;
  document.limits_ = limits;
  document.repair_enabled_ = options.repair;
  document.header_ = *header;
  document.trailer_ = xref->trailer;
  for (const std::string_view key : kSectionKeys) {
    document.trailer_.erase(key);
  }

  // Next number: past both /Size and the highest number the xref actually uses.
  std::uint64_t next = xref->entries.empty() ? 1 : static_cast<std::uint64_t>(xref->entries.rbegin()->first) + 1;
  if (const auto size = document.trailer_.find("Size") != nullptr ? document.trailer_.find("Size")->as_integer() : std::nullopt; size && *size > 0) {
    next = std::max(next, static_cast<std::uint64_t>(*size));
  }
  document.next_number_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(next, std::numeric_limits<std::uint32_t>::max()));

  // The loader refers to the source object, which stays where it is when the owning pointer moves:
  // ownership is taken only once everything succeeded, so a failed attempt leaves the caller able to
  // try a repair with the same source.
  document.loader_ = std::make_unique<parser::ObjectLoader>(*source, std::move(*xref), limits);

  if (const Object* encrypt = document.trailer_.find("Encrypt")) {
    // The /Encrypt dictionary itself is never encrypted: load it before enabling decryption.
    std::optional<std::uint32_t> encrypt_object;
    Object encrypt_dict = *encrypt;
    if (const auto ref = encrypt->as_ref()) {
      encrypt_object = ref->number;
      auto loaded = document.loader_->resolve(*ref);
      if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
      }
      encrypt_dict = std::move(*loaded);
    }
    if (encrypt_dict.as_dict() == nullptr) {
      return make_error(ErrorCode::kSyntax, "/Encrypt is not a dictionary");
    }
    // The first /ID string enters the key derivation of revisions 2-4.
    std::span<const std::byte> first_id;
    const Object* id = document.trailer_.find("ID");
    if (id != nullptr && id->as_array() != nullptr && !id->as_array()->empty() && id->as_array()->front().as_string() != nullptr) {
      const std::string& bytes = id->as_array()->front().as_string()->bytes;
      first_id = std::as_bytes(std::span(bytes.data(), bytes.size()));
    }
    auto handler = crypto::StandardSecurityHandler::create(*encrypt_dict.as_dict(), first_id, options.password);
    if (!handler) {
      return std::unexpected(std::move(handler.error()));
    }
    document.security_ = std::make_shared<const crypto::StandardSecurityHandler>(std::move(*handler));
    document.loader_->set_security_handler(document.security_, encrypt_object);
    document.encrypt_object_ = encrypt_object;
  }
  document.source_ = std::move(source);
  return document;
}

// ── Reading ─────────────────────────────────────────────────────────────────────────────────

Result<Document::Slot*> Document::slot(std::uint32_t number) {
  if (auto it = slots_.find(number); it != slots_.end()) {
    return &it->second;
  }
  const parser::XrefEntry* entry = loader_->xref().find(number);
  if (entry == nullptr || entry->type == parser::XrefEntryType::kFree) {
    return nullptr;
  }
  auto object = loader_->load(number);
  if (!object) {
    if (!repair_enabled_ || !repair::may_help(object.error())) {
      return std::unexpected(std::move(object.error()));
    }
    object = repair_object(number, object.error());
    if (!object) {
      return std::unexpected(std::move(object.error()));
    }
  }
  const std::uint16_t generation = entry->type == parser::XrefEntryType::kCompressed ? 0 : entry->generation;
  auto [it, inserted] = slots_.emplace(number, Slot{.state = ObjectState::kUnmodified, .generation = generation, .object = std::move(*object)});
  return &it->second;
}

void Document::note(repair::RepairKind kind, std::string description) {
  constexpr std::size_t kMaxNotes = 1000;
  if (repairs_.size() < kMaxNotes) {
    repairs_.push_back(repair::RepairNote{.kind = kind, .description = std::move(description)});
  } else if (repairs_.size() == kMaxNotes) {
    repairs_.push_back(repair::RepairNote{.kind = kind, .description = "further repairs are not listed"});
  }
}

std::unique_ptr<parser::ObjectLoader> Document::make_loader(parser::XrefTable table, parser::ParseMode mode) const {
  auto loader = std::make_unique<parser::ObjectLoader>(*source_, std::move(table), limits_, mode);
  if (security_ != nullptr) {
    loader->set_security_handler(security_, encrypt_object_);
  }
  return loader;
}

Result<Object> Document::repair_object(std::uint32_t number, const Error& strict_error) {
  // 1. Same place, tolerant parser.
  if (tolerant_loader_ == nullptr) {
    tolerant_loader_ = make_loader(loader_->xref(), parser::ParseMode::kTolerant);
  }
  if (auto tolerant = tolerant_loader_->load(number)) {
    note(repair::RepairKind::kTolerantParsing, "object " + std::to_string(number) + " read tolerantly (" + strict_error.message + ")");
    return tolerant;
  }

  // 2. Where scanning the file finds its definition.
  if (!scan_attempted_) {
    scan_attempted_ = true;
    if (auto table = repair::reconstruct_xref(*source_, limits_)) {
      scan_loader_ = make_loader(std::move(*table), parser::ParseMode::kTolerant);
    }
  }
  if (scan_loader_ != nullptr) {
    const parser::XrefEntry* found = scan_loader_->xref().find(number);
    if (found == nullptr || found->type == parser::XrefEntryType::kFree) {
      note(repair::RepairKind::kMissingObject,
           "object " + std::to_string(number) + " is not defined anywhere in the file; read as null (" + strict_error.message + ")");
      return Object(model::Null{});
    }
    if (auto relocated = scan_loader_->load(number)) {
      note(repair::RepairKind::kObjectRelocated, "object " + std::to_string(number) + " found by scanning the file (" + strict_error.message + ")");
      return relocated;
    }
  }
  return std::unexpected(strict_error);
}

Result<const Object*> Document::get(std::uint32_t number) {
  auto found = slot(number);
  if (!found) {
    return std::unexpected(std::move(found.error()));
  }
  if (*found == nullptr || (*found)->state == ObjectState::kDeleted) {
    return &null_object();
  }
  return &(*found)->object;
}

Result<const Object*> Document::get(model::ObjectRef ref) {
  auto found = slot(ref.number);
  if (!found) {
    return std::unexpected(std::move(found.error()));
  }
  if (*found == nullptr || (*found)->state == ObjectState::kDeleted || (*found)->generation != ref.generation) {
    return &null_object();
  }
  return &(*found)->object;
}

Result<Object> Document::resolve(const Object& object) {
  const auto ref = object.as_ref();
  if (!ref) {
    return object;
  }
  auto target = get(*ref);
  if (!target) {
    return std::unexpected(std::move(target.error()));
  }
  return **target;
}

Result<std::vector<std::byte>> Document::decode_stream(const model::Stream& stream) {
  // /Filter and /DecodeParms may be (or contain) references: resolve them through the document, so
  // that edited objects are seen too.
  model::Dict dict = stream.dict;
  for (const char* key : {"Filter", "DecodeParms"}) {
    Object* value = dict.find(key);
    if (value == nullptr) {
      continue;
    }
    auto resolved = resolve(*value);
    if (!resolved) {
      return std::unexpected(std::move(resolved.error()));
    }
    *value = std::move(*resolved);
    if (model::Array* array = value->as_array()) {
      for (Object& element : *array) {
        auto element_resolved = resolve(element);
        if (!element_resolved) {
          return std::unexpected(std::move(element_resolved.error()));
        }
        element = std::move(*element_resolved);
      }
    }
  }

  // Reading (and decrypting) is the loader's job; the filter chain uses the resolved dictionary.
  auto encoded = loader_->read_stream_data(stream);
  if (!encoded) {
    return std::unexpected(std::move(encoded.error()));
  }
  auto decoded = filters::decode(*encoded, dict, limits_);
  if (decoded || !repair_enabled_ || decoded.error().code != ErrorCode::kCorruptData) {
    return decoded;
  }
  // Repair: keep what decodes before the fault, as every reader does with damaged streams.
  auto partial = filters::decode(std::move(*encoded), dict, limits_, true);
  if (partial) {
    note(repair::RepairKind::kPartialDecode, (stream.origin ? "stream of object " + std::to_string(stream.origin->number) : std::string("stream")) + ": " +
                                                 decoded.error().message + "; kept the " + std::to_string(partial->size()) + " bytes before the fault");
  }
  return partial;
}

std::pair<std::size_t, std::size_t> Document::sample_table() {
  // Up to kTableSample in-use entries spread evenly over the table, loaded tolerantly (not cached):
  // what counts is whether the entry leads to its object, not whether the object is well formed.
  std::vector<std::uint32_t> in_use;
  for (const auto& [number, entry] : loader_->xref().entries) {
    if (entry.type == parser::XrefEntryType::kInUse) {
      in_use.push_back(number);
    }
  }
  const std::size_t step = std::max<std::size_t>(1, in_use.size() / kTableSample);
  std::size_t misplaced = 0;
  std::size_t sampled = 0;
  for (std::size_t i = 0; i < in_use.size() && sampled < kTableSample; i += step, ++sampled) {
    if (tolerant_loader_ == nullptr) {
      tolerant_loader_ = make_loader(loader_->xref(), parser::ParseMode::kTolerant);
    }
    if (!tolerant_loader_->load(in_use[i])) {
      ++misplaced;
    }
  }
  return {misplaced, sampled};
}

bool Document::root_is_dictionary() {
  const Object* root = trailer_.find("Root");
  const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
  auto object = root_ref ? get(*root_ref) : Result<const Object*>(nullptr);
  return object && *object != nullptr && (*object)->as_dict() != nullptr;
}

Result<const model::Dict*> Document::catalog() {
  const Object* root = trailer_.find("Root");
  const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
  if (!root_ref) {
    return make_error(ErrorCode::kSyntax, "the trailer has no /Root reference");
  }
  auto object = get(*root_ref);
  if (object && (*object)->as_dict() != nullptr) {
    return (*object)->as_dict();
  }
  if (repair_enabled_ && !catalog_search_done_) {
    // Repair: the object /Root names is missing or is something else. The catalog is the object with
    // /Type /Catalog and /Pages — the highest-numbered when there are several (the newest, usually).
    catalog_search_done_ = true;
    std::optional<model::ObjectRef> found;
    for (const auto& [number, entry] : loader_->xref().entries) {
      if (entry.type == parser::XrefEntryType::kFree || number == root_ref->number) {
        continue;
      }
      auto candidate = get(number);
      const model::Dict* dict = candidate ? (*candidate)->as_dict() : nullptr;
      const Object* type = dict != nullptr ? dict->find("Type") : nullptr;
      if (type != nullptr && type->is_name("Catalog") && dict->contains("Pages")) {
        found = model::ObjectRef{.number = number, .generation = generation(number)};
      }
    }
    if (found) {
      note(repair::RepairKind::kCatalogFound,
           "/Root " + std::to_string(root_ref->number) + " is not a dictionary; the catalog is object " + std::to_string(found->number));
      trailer_.set("Root", *found);  // a repair of what the file says, not an edit: trailer_modified_ stays
      return catalog();
    }
  }
  if (!object) {
    return std::unexpected(std::move(object.error()));
  }
  return make_error(ErrorCode::kSyntax, "the document catalog (/Root) is not a dictionary");
}

Result<std::vector<model::ObjectRef>> Document::page_refs() {
  auto root = catalog();
  if (!root) {
    return std::unexpected(std::move(root.error()));
  }
  const Object* pages = (*root)->find("Pages");
  const auto pages_ref = pages != nullptr ? pages->as_ref() : std::nullopt;
  if (!pages_ref) {
    return make_error(ErrorCode::kSyntax, "the catalog has no /Pages reference");
  }

  auto walked = walk_page_tree(*pages_ref, repair_enabled_);
  if (!repair_enabled_) {
    return walked;
  }
  // The tree adds up when the walk succeeds and finds as many pages as the root declares.
  std::optional<std::int64_t> declared;
  if (auto root_node = get(*pages_ref); root_node && (*root_node)->as_dict() != nullptr) {
    if (const Object* count = (*root_node)->as_dict()->find("Count")) {
      declared = count->as_integer();
    }
  }
  if (walked && (!declared || std::cmp_equal(walked->size(), *declared))) {
    return walked;
  }
  // Candidates: the walk, the rebuild, and the rebuild that keeps unreadable /Kids entries as
  // placeholder pages. One that matches the declared /Count exactly wins (in that order); otherwise
  // the plain rebuild, if it recovers pages without exceeding /Count (more pages than the document
  // claims means leftovers, e.g. pages of older revisions); otherwise the walk as it is.
  auto rebuilt = rebuild_page_tree(*pages_ref, false);
  auto with_placeholders = rebuild_page_tree(*pages_ref, true);
  const auto adopt = [&](Result<std::vector<model::ObjectRef>>& chosen, std::string_view how) -> Result<std::vector<model::ObjectRef>> {
    note(repair::RepairKind::kPageTreeRebuilt,
         "page tree rebuilt from /Kids and /Parent links" + std::string(how) + ": " + (walked ? std::to_string(walked->size()) : std::string("unreadable")) +
             " -> " + std::to_string(chosen->size()) + " pages" + (declared ? " (root /Count " + std::to_string(*declared) + ")" : std::string()));
    return std::move(chosen);
  };
  const auto matches = [&](const Result<std::vector<model::ObjectRef>>& candidate) {
    return candidate && !candidate->empty() && declared && std::cmp_equal(candidate->size(), *declared);
  };
  if (matches(walked)) {
    return walked;
  }
  if (matches(rebuilt)) {
    return adopt(rebuilt, "");
  }
  if (matches(with_placeholders)) {
    return adopt(with_placeholders, ", unreadable entries kept as pages");
  }
  const bool within_count = !declared || (rebuilt && std::cmp_less_equal(rebuilt->size(), *declared));
  if (rebuilt && !rebuilt->empty() && within_count && (!walked || rebuilt->size() > walked->size())) {
    return adopt(rebuilt, "");
  }
  return walked;
}

Result<std::vector<model::ObjectRef>> Document::walk_page_tree(model::ObjectRef root, bool infer_types) {
  // Depth-first, children pushed in reverse so that pages come out in document order.
  std::vector<model::ObjectRef> result;
  std::vector<std::pair<model::ObjectRef, std::size_t>> stack{{root, 0}};
  std::set<std::uint32_t> visited;
  while (!stack.empty()) {
    const auto [ref, depth] = stack.back();
    stack.pop_back();
    if (!visited.insert(ref.number).second) {
      return make_error(ErrorCode::kSyntax, "page tree visits object " + std::to_string(ref.number) + " twice");
    }
    if (depth > limits_.max_nesting_depth) {
      return make_error(ErrorCode::kLimitExceeded, "page tree deeper than max_nesting_depth");
    }
    auto node_object = get(ref);
    if (!node_object) {
      return std::unexpected(std::move(node_object.error()));
    }
    const model::Dict* node = (*node_object)->as_dict();
    const Object* type = node != nullptr ? node->find("Type") : nullptr;
    bool is_page = type != nullptr && type->is_name("Page");
    bool is_pages = type != nullptr && type->is_name("Pages");
    // A node typed /Page that has /Kids is an intermediate node with a wrong /Type (pages have no /Kids).
    bool misspelt_kids = false;
    const model::Array* array = node != nullptr ? kids_of(*node, infer_types, &misspelt_kids) : nullptr;
    const bool page_with_kids = is_page && array != nullptr;
    if (node != nullptr && !is_pages && (!is_page || page_with_kids) && infer_types) {
      // Repair: a node without a usable /Type (absent, e.g. /pagE, or /Page with /Kids) is a /Pages
      // node if it has /Kids, a page otherwise — as the reference readers do.
      is_pages = array != nullptr || node->find("Kids") != nullptr;
      is_page = !is_pages;
      note(repair::RepairKind::kPageTreeType, "page tree node " + std::to_string(ref.number) + (type == nullptr ? " has no /Type" : " has a wrong /Type") +
                                                  "; taken as /" + (is_pages ? "Pages" : "Page"));
    }
    if (is_page && ref.number == root.number && infer_types && type != nullptr && !type->is_name("Page")) {
      // A root without /Kids typed as something else (e.g. a font: the catalog points elsewhere) is
      // not a page tree. Without any /Type it is taken as the one page, as the reference readers do.
      return make_error(ErrorCode::kSyntax, "page tree root " + std::to_string(ref.number) + " is not a /Pages node");
    }
    if (is_page) {
      result.push_back(ref);
      continue;
    }
    if (!is_pages) {
      return make_error(ErrorCode::kSyntax, "page tree node " + std::to_string(ref.number) + " is neither /Type /Pages nor /Type /Page");
    }
    if (array == nullptr) {
      return make_error(ErrorCode::kSyntax, "page tree node " + std::to_string(ref.number) + " has no /Kids array");
    }
    if (misspelt_kids) {
      note(repair::RepairKind::kPageTreeType, "page tree node " + std::to_string(ref.number) + " has its /Kids under a misspelt key");
    }
    for (const Object& entry : std::views::reverse(*array)) {
      const auto kid = entry.as_ref();
      if (!kid) {
        return make_error(ErrorCode::kSyntax, "page tree node " + std::to_string(ref.number) + " has a /Kids entry that is not a reference");
      }
      stack.emplace_back(*kid, depth + 1);
    }
  }
  return result;
}

Result<std::vector<model::ObjectRef>> Document::rebuild_page_tree(model::ObjectRef root, bool keep_unreadable) {
  // Children of every node by /Parent, in object-number order: the links that survive when a
  // parent's /Kids has lost them.
  std::map<std::uint32_t, std::vector<model::ObjectRef>> children;
  for (const auto& [number, entry] : loader_->xref().entries) {
    if (entry.type == parser::XrefEntryType::kFree) {
      continue;
    }
    auto object = get(number);
    const model::Dict* dict = object ? (*object)->as_dict() : nullptr;
    if (dict == nullptr) {
      continue;
    }
    const Object* parent = dict->find("Parent");
    const auto parent_ref = parent != nullptr ? parent->as_ref() : std::nullopt;
    const Object* type = dict->find("Type");
    const bool node_like = (type != nullptr && looks_like_page(*dict)) || kids_of(*dict, true) != nullptr;
    if (parent_ref && node_like) {
      const std::uint16_t generation = entry.type == parser::XrefEntryType::kCompressed ? 0 : entry.generation;
      children[parent_ref->number].push_back(model::ObjectRef{.number = number, .generation = generation});
    }
  }

  // Walk again: each node's valid /Kids first, then the nodes that name it as /Parent.
  std::vector<model::ObjectRef> result;
  std::vector<std::pair<model::ObjectRef, std::size_t>> stack{{root, 0}};
  std::set<std::uint32_t> visited;
  while (!stack.empty()) {
    const auto [ref, depth] = stack.back();
    stack.pop_back();
    if (!visited.insert(ref.number).second || depth > limits_.max_nesting_depth) {
      continue;
    }
    auto object = get(ref);
    const model::Dict* node = object ? (*object)->as_dict() : nullptr;
    const auto linked_children = children.find(ref.number);
    const bool lost_node = node == nullptr || (kids_of(*node, true) == nullptr && !looks_like_page(*node));
    if (lost_node && linked_children != children.end()) {
      // Not a node (unreadable, or another object under this number), yet pages name it as their
      // /Parent: the node was lost, its pages were not — they take its place.
      for (const model::ObjectRef& child : std::views::reverse(linked_children->second)) {
        if (!visited.contains(child.number)) {
          stack.emplace_back(child, depth + 1);
        }
      }
      continue;
    }
    if (node == nullptr) {
      // With keep_unreadable, a /Kids entry that cannot be read keeps its place as a page (as the
      // reference readers do): it will not display, but the numbering of the others stays right.
      if (keep_unreadable && ref.number != root.number) {
        result.push_back(ref);
      }
      continue;
    }
    const Object* type = node->find("Type");
    std::vector<model::ObjectRef> kids;
    if (const model::Array* kids_array = kids_of(*node, true); kids_array != nullptr) {
      for (const Object& kid : *kids_array) {
        if (const auto kid_ref = kid.as_ref(); kid_ref && !visited.contains(kid_ref->number)) {
          kids.push_back(*kid_ref);
        }
      }
    }
    if (const auto linked = children.find(ref.number); linked != children.end()) {
      for (const model::ObjectRef& child : linked->second) {
        if (std::ranges::none_of(kids, [&](const model::ObjectRef& k) { return k.number == child.number; })) {
          kids.push_back(child);
        }
      }
    }
    const bool has_kids = kids_of(*node, true) != nullptr;
    // A dictionary with another /Type (e.g. a font where a page should be) is not a page; with
    // keep_unreadable it keeps its place, like an entry that cannot be read.
    if (ref.number != root.number && !has_kids && !looks_like_page(*node)) {
      if (keep_unreadable) {
        result.push_back(ref);
      }
      continue;
    }
    const bool is_page = ref.number != root.number && !has_kids && ((type != nullptr && type->is_name("Page")) || kids.empty());
    if (is_page) {
      result.push_back(ref);
      continue;
    }
    for (const model::ObjectRef& kid : std::views::reverse(kids)) {
      stack.emplace_back(kid, depth + 1);
    }
  }
  return result;
}

// ── Editing ─────────────────────────────────────────────────────────────────────────────────

Result<void> Document::set(std::uint32_t number, Object value) {
  auto found = slot(number);
  if (!found) {
    return std::unexpected(std::move(found.error()));
  }
  if (*found == nullptr || (*found)->state == ObjectState::kDeleted) {
    return make_error(ErrorCode::kOutOfRange, "object " + std::to_string(number) + " does not exist");
  }
  (*found)->object = std::move(value);
  if ((*found)->state == ObjectState::kUnmodified) {
    (*found)->state = ObjectState::kModified;
  }
  return {};
}

Result<model::ObjectRef> Document::add(Object value) {
  if (next_number_ == std::numeric_limits<std::uint32_t>::max()) {
    return make_error(ErrorCode::kLimitExceeded, "no object numbers left");
  }
  const std::uint32_t number = next_number_++;
  slots_.insert_or_assign(number, Slot{.state = ObjectState::kNew, .generation = 0, .object = std::move(value)});
  return model::ObjectRef{.number = number, .generation = 0};
}

Result<void> Document::remove(std::uint32_t number) {
  auto found = slot(number);
  if (!found) {
    return std::unexpected(std::move(found.error()));
  }
  if (*found == nullptr || (*found)->state == ObjectState::kDeleted) {
    return make_error(ErrorCode::kOutOfRange, "object " + std::to_string(number) + " does not exist");
  }
  if ((*found)->state == ObjectState::kNew) {
    slots_.erase(number);
    return {};
  }
  (*found)->state = ObjectState::kDeleted;
  (*found)->object = Object();
  return {};
}

void Document::set_trailer_entry(std::string key, Object value) {
  if (value.is_null()) {
    trailer_.erase(key);
  } else {
    trailer_.set(std::move(key), std::move(value));
  }
  trailer_modified_ = true;
}

ObjectState Document::state(std::uint32_t number) const {
  const auto it = slots_.find(number);
  return it == slots_.end() ? ObjectState::kUnmodified : it->second.state;
}

std::vector<model::ObjectRef> Document::live_objects() const {
  std::vector<model::ObjectRef> result;
  const auto& entries = loader_->xref().entries;
  auto slot_it = slots_.begin();
  const auto add = [&](std::uint32_t number) {
    if (state(number) != ObjectState::kDeleted) {
      result.push_back(model::ObjectRef{.number = number, .generation = generation(number)});
    }
  };
  // Merge the file's objects with the new ones, both already sorted by number.
  for (const auto& [number, entry] : entries) {
    for (; slot_it != slots_.end() && slot_it->first < number; ++slot_it) {
      if (slot_it->second.state == ObjectState::kNew) {
        add(slot_it->first);
      }
    }
    if (entry.type != parser::XrefEntryType::kFree && number != 0) {
      add(number);
    }
  }
  for (; slot_it != slots_.end(); ++slot_it) {
    if (slot_it->second.state == ObjectState::kNew && !entries.contains(slot_it->first)) {
      add(slot_it->first);
    }
  }
  return result;
}

bool looks_like_page(const model::Dict& dict) {
  const Object* type = dict.find("Type");
  if (type == nullptr || type->is_name("Page") || type->is_name("Pages")) {
    return true;  // /Pages here: a node without /Kids, which the readers take as a page
  }
  // A misspelling of /Page, or a /MediaBox (only pages have one: annotations, fonts and the other
  // objects with /Parent, /Contents or /Resources do not).
  const model::Name* name = type->as_name();
  const bool page_in_other_case = name != nullptr && std::ranges::equal(name->bytes, std::string_view("page"),
                                                                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
  return page_in_other_case || dict.contains("MediaBox");
}

bool Document::page_tree_conforms(const std::vector<model::ObjectRef>& pages) {
  auto root = catalog();
  const Object* pages_object = root ? (*root)->find("Pages") : nullptr;
  const auto pages_ref = pages_object != nullptr ? pages_object->as_ref() : std::nullopt;
  if (!pages_ref) {
    return false;
  }
  auto root_node = get(*pages_ref);
  const model::Dict* node = root_node ? (*root_node)->as_dict() : nullptr;
  const Object* count = node != nullptr ? node->find("Count") : nullptr;
  if (count == nullptr || !count->as_integer() || !std::cmp_equal(*count->as_integer(), pages.size()) || node->contains("Parent")) {
    return false;  // the root has no /Parent (§7.7.3.2)
  }
  const auto walked = walk_page_tree(*pages_ref, false);
  return walked && std::ranges::equal(*walked, pages, [](model::ObjectRef a, model::ObjectRef b) { return a.number == b.number; });
}

std::uint16_t Document::generation(std::uint32_t number) const {
  if (const auto it = slots_.find(number); it != slots_.end()) {
    return it->second.generation;
  }
  const parser::XrefEntry* entry = loader_->xref().find(number);
  return entry != nullptr && entry->type == parser::XrefEntryType::kInUse ? entry->generation : 0;
}

Result<std::vector<std::byte>> Document::encoded_stream_data(const model::Stream& stream) { return loader_->read_stream_data(stream); }

std::vector<std::uint32_t> Document::changed_objects() const {
  std::vector<std::uint32_t> changed;
  for (const auto& [number, slot] : slots_) {
    if (slot.state != ObjectState::kUnmodified) {
      changed.push_back(number);
    }
  }
  return changed;
}

}  // namespace capriole
