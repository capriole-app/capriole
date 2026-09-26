#include "capriole/document/document.hpp"

#include <algorithm>
#include <array>
#include <limits>
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

Result<const model::Dict*> Document::catalog() {
  const Object* root = trailer_.find("Root");
  const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
  if (!root_ref) {
    return make_error(ErrorCode::kSyntax, "the trailer has no /Root reference");
  }
  auto object = get(*root_ref);
  if (!object) {
    return std::unexpected(std::move(object.error()));
  }
  const model::Dict* dict = (*object)->as_dict();
  if (dict == nullptr) {
    return make_error(ErrorCode::kSyntax, "the document catalog (/Root) is not a dictionary");
  }
  return dict;
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

  // Depth-first, children pushed in reverse so that pages come out in document order.
  std::vector<model::ObjectRef> result;
  std::vector<std::pair<model::ObjectRef, std::size_t>> stack{{*pages_ref, 0}};
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
    if (node != nullptr && !is_page && !is_pages && repair_enabled_) {
      // Repair: a node without a usable /Type (absent, or e.g. /pagE) is a /Pages node if it has
      // /Kids, a page otherwise — as the reference readers do.
      is_pages = node->find("Kids") != nullptr;
      is_page = !is_pages;
      note(repair::RepairKind::kPageTreeType, "page tree node " + std::to_string(ref.number) + (type == nullptr ? " has no /Type" : " has a wrong /Type") +
                                                  "; taken as /" + (is_pages ? "Pages" : "Page"));
    }
    if (is_page) {
      result.push_back(ref);
      continue;
    }
    if (!is_pages) {
      return make_error(ErrorCode::kSyntax, "page tree node " + std::to_string(ref.number) + " is neither /Type /Pages nor /Type /Page");
    }
    const Object* kids = node->find("Kids");
    const model::Array* array = kids != nullptr ? kids->as_array() : nullptr;
    if (array == nullptr) {
      return make_error(ErrorCode::kSyntax, "page tree node " + std::to_string(ref.number) + " has no /Kids array");
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
