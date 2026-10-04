#include "capriole/edit/attachments.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/crypto/hash.hpp"
#include "capriole/filters/flate.hpp"
#include "capriole/model/text_string.hpp"
#include "common.hpp"
#include "sound.hpp"

namespace capriole::edit {

namespace {

using detail::name;
using model::Array;
using model::Dict;
using model::Object;
using model::ObjectRef;

// Bounds against hostile name trees.
constexpr std::size_t kMaxTreeDepth = 32;
constexpr std::size_t kMaxEntries = 100'000;

constexpr std::array<std::string_view, 8> kRelationships = {"Source",           "Data",     "Alternative", "Supplement",
                                                            "EncryptedPayload", "FormData", "Schema",      "Unspecified"};

std::string utf8_text(const Object* value) {
  return value != nullptr && value->as_string() != nullptr ? model::to_utf8(model::decode_text_string(value->as_string()->bytes)) : std::string();
}

std::string bytes_of(const Object* value) { return value != nullptr && value->as_string() != nullptr ? value->as_string()->bytes : std::string(); }

std::optional<Dict> dict_at(Document& document, const Object* entry) {
  if (entry == nullptr) {
    return std::nullopt;
  }
  auto resolved = document.resolve(*entry);
  return resolved && resolved->as_dict() != nullptr ? std::optional<Dict>(*resolved->as_dict()) : std::nullopt;
}

// A name tree's entries (§7.9.6), in tree order: key bytes and value.
struct Entry {
  std::string key;
  Object value;
};

void collect(Document& document, const Object& node, std::size_t depth, std::set<std::uint32_t>& seen, std::vector<Entry>& out) {
  if (depth > kMaxTreeDepth || out.size() >= kMaxEntries) {
    return;
  }
  if (const auto ref = node.as_ref(); ref && !seen.insert(ref->number).second) {
    return;  // a cycle
  }
  const auto dict = dict_at(document, &node);
  if (!dict) {
    return;
  }
  if (const auto names = [&]() -> std::optional<Object> {
        const Object* entry = dict->find("Names");
        auto resolved = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
        return resolved && resolved->as_array() != nullptr ? std::optional<Object>(std::move(*resolved)) : std::nullopt;
      }()) {
    const Array& pairs = *names->as_array();
    for (std::size_t i = 0; i + 1 < pairs.size() && out.size() < kMaxEntries; i += 2) {
      auto key = document.resolve(pairs[i]);
      if (key && key->as_string() != nullptr) {
        out.push_back({.key = key->as_string()->bytes, .value = pairs[i + 1]});
      }
    }
  }
  const Object* kids_entry = dict->find("Kids");
  auto kids = kids_entry != nullptr ? document.resolve(*kids_entry) : Result<Object>(Object());
  if (kids && kids->as_array() != nullptr) {
    for (const Object& kid : *kids->as_array()) {
      collect(document, kid, depth + 1, seen, out);
    }
  }
}

// Where the document's attachments hang: the catalog (and its reference), /Names, /EmbeddedFiles.
struct Place {
  ObjectRef catalog_ref;
  Dict catalog;
  std::vector<Entry> entries;  // /EmbeddedFiles
};

Result<Place> place_of(Document& document) {
  const Object* root = document.trailer().find("Root");
  const auto root_ref = root != nullptr ? root->as_ref() : std::nullopt;
  auto catalog = document.catalog();
  if (!catalog || !root_ref) {
    return make_error(ErrorCode::kSyntax, "the document catalog cannot be read");
  }
  Place place{.catalog_ref = *root_ref, .catalog = **catalog, .entries = {}};
  if (const auto names = dict_at(document, place.catalog.find("Names"))) {
    if (const Object* tree = names->find("EmbeddedFiles")) {
      std::set<std::uint32_t> seen;
      collect(document, *tree, 0, seen, place.entries);
    }
  }
  return place;
}

// The catalog's /AF, as references.
std::vector<ObjectRef> associated(Document& document, const Dict& catalog) {
  std::vector<ObjectRef> out;
  const Object* af = catalog.find("AF");
  auto resolved = af != nullptr ? document.resolve(*af) : Result<Object>(Object());
  if (resolved && resolved->as_array() != nullptr) {
    for (const Object& o : *resolved->as_array()) {
      if (const auto ref = o.as_ref()) {
        out.push_back(*ref);
      }
    }
  }
  return out;
}

// The embedded file of an /EF dictionary: /UF, else /F.
const Object* embedded_entry(const std::optional<Dict>& ef) {
  if (!ef) {
    return nullptr;
  }
  const Object* uf = ef->find("UF");
  return uf != nullptr ? uf : ef->find("F");
}

// What a file specification says of its embedded file; nullopt when it has none.
std::optional<AttachmentInfo> describe(Document& document, ObjectRef ref, const Dict& spec) {
  const auto ef = dict_at(document, spec.find("EF"));
  const Object* stream_entry = embedded_entry(ef);
  auto stream = stream_entry != nullptr ? document.resolve(*stream_entry) : Result<Object>(Object());
  if (!stream || stream->as_stream() == nullptr) {
    return std::nullopt;
  }
  AttachmentInfo info;
  info.spec = ref;
  info.file_name = utf8_text(spec.find("UF"));
  if (info.file_name.empty()) {
    info.file_name = utf8_text(spec.find("F"));
  }
  info.description = utf8_text(spec.find("Desc"));
  if (const Object* rel = spec.find("AFRelationship"); rel != nullptr && rel->as_name() != nullptr) {
    info.relationship = rel->as_name()->bytes;
  }
  const Dict& sd = stream->as_stream()->dict;
  if (const Object* subtype = sd.find("Subtype"); subtype != nullptr && subtype->as_name() != nullptr) {
    info.mime_type = subtype->as_name()->bytes;
  }
  if (const auto params = dict_at(document, sd.find("Params"))) {
    if (const Object* size = params->find("Size"); size != nullptr && size->as_integer().value_or(-1) >= 0) {
      info.size = static_cast<std::uint64_t>(size->as_integer().value_or(0));
    }
    info.creation_date = bytes_of(params->find("CreationDate"));
    info.modification_date = bytes_of(params->find("ModDate"));
  }
  return info;
}

// The embedded file stream of a specification (or of a file attachment annotation's).
Result<model::Stream> embedded_stream(Document& document, ObjectRef ref) {
  auto object = document.get(ref);
  const Dict* dict = object && *object != nullptr ? (*object)->as_dict() : nullptr;
  if (dict == nullptr) {
    return make_error(ErrorCode::kOutOfRange, "not an attachment");
  }
  std::optional<Dict> spec = *dict;
  if (detail::subtype_of(*dict) == "FileAttachment") {
    spec = dict_at(document, dict->find("FS"));
  }
  const auto ef = spec ? dict_at(document, spec->find("EF")) : std::nullopt;
  const Object* entry = embedded_entry(ef);
  auto stream = entry != nullptr ? document.resolve(*entry) : Result<Object>(Object());
  if (!stream || stream->as_stream() == nullptr) {
    return make_error(ErrorCode::kOutOfRange, "not an attachment");
  }
  return *stream->as_stream();
}

// Writes /EmbeddedFiles anew as one sorted /Names array, and /AF, into the catalog (or the objects
// they live in).
Result<void> write_place(Document& document, Place& place, std::vector<Entry> entries, const std::vector<ObjectRef>& af) {
  std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) { return a.key < b.key; });
  Array names;
  for (Entry& e : entries) {
    names.emplace_back(model::String{.bytes = e.key});
    names.push_back(std::move(e.value));
  }
  bool catalog_changed = false;
  // An entry is written into the object it refers to only when the reference reaches what it should
  // (a reference whose generation does not match reads as null, §7.3.10: written there, the change
  // would be out of reach); otherwise the entry is replaced.
  const Object* names_entry = place.catalog.find("Names");
  const std::optional<Dict> names_found = dict_at(document, names_entry);
  const std::optional<ObjectRef> names_ref = names_entry != nullptr && names_found ? names_entry->as_ref() : std::nullopt;
  Dict names_dict = names_found.value_or(Dict{});
  const Object* tree_entry = names_dict.find("EmbeddedFiles");
  const std::optional<ObjectRef> tree_ref = tree_entry != nullptr && dict_at(document, tree_entry) ? tree_entry->as_ref() : std::nullopt;
  Dict tree;
  tree.set("Names", std::move(names));
  const bool empty = entries.empty();
  if (tree_ref && !empty) {
    if (auto set = document.set(tree_ref->number, Object(std::move(tree))); !set) {
      return set;
    }
  } else {
    if (empty) {
      names_dict.erase("EmbeddedFiles");
    } else {
      names_dict.set("EmbeddedFiles", std::move(tree));
    }
    if (names_ref) {
      if (auto set = document.set(names_ref->number, Object(std::move(names_dict))); !set) {
        return set;
      }
    } else if (names_dict.entries().empty()) {
      catalog_changed = place.catalog.find("Names") != nullptr;
      place.catalog.erase("Names");
    } else {
      place.catalog.set("Names", std::move(names_dict));
      catalog_changed = true;
    }
  }
  // /AF: written where it is (its own array object, or the catalog).
  const std::vector<ObjectRef> before = associated(document, place.catalog);
  if (before != af) {
    Array list;
    for (const ObjectRef r : af) {
      list.emplace_back(r);
    }
    const Object* af_entry = place.catalog.find("AF");
    auto af_found = af_entry != nullptr ? document.resolve(*af_entry) : Result<Object>(Object());
    const std::optional<ObjectRef> af_ref = af_entry != nullptr && af_found && af_found->as_array() != nullptr ? af_entry->as_ref() : std::nullopt;
    if (af_ref && !af.empty()) {
      if (auto set = document.set(af_ref->number, Object(std::move(list))); !set) {
        return set;
      }
    } else {
      if (af.empty()) {
        place.catalog.erase("AF");
      } else {
        place.catalog.set("AF", std::move(list));
      }
      catalog_changed = true;
    }
  }
  return catalog_changed ? document.set(place.catalog_ref.number, Object(place.catalog)) : Result<void>();
}

}  // namespace

Result<std::vector<AttachmentInfo>> list_attachments(Document& document) {
  auto place = place_of(document);
  if (!place) {
    return std::unexpected(std::move(place.error()));
  }
  const std::vector<ObjectRef> af = associated(document, place->catalog);
  std::vector<AttachmentInfo> out;
  std::set<std::uint32_t> listed;
  for (const Entry& e : place->entries) {
    const auto ref = e.value.as_ref();
    const auto spec = ref ? dict_at(document, &e.value) : std::nullopt;
    if (!spec || !listed.insert(ref->number).second) {
      continue;
    }
    if (auto info = describe(document, *ref, *spec)) {
      info->name = model::to_utf8(model::decode_text_string(e.key));
      info->associated = std::ranges::find(af, *ref) != af.end();
      out.push_back(std::move(*info));
    }
  }
  // File attachment annotations.
  auto pages = document.page_refs();
  if (pages) {
    for (std::uint32_t i = 0; i < pages->size() && out.size() < kMaxEntries; ++i) {
      auto page = document.get((*pages)[i]);
      const Dict* page_dict = page && *page != nullptr ? (*page)->as_dict() : nullptr;
      const Object* annots_entry = page_dict != nullptr ? page_dict->find("Annots") : nullptr;
      auto annots = annots_entry != nullptr ? document.resolve(*annots_entry) : Result<Object>(Object());
      if (!annots || annots->as_array() == nullptr) {
        continue;
      }
      for (const Object& a : *annots->as_array()) {
        const auto annotation_ref = a.as_ref();
        const auto annotation = annotation_ref ? dict_at(document, &a) : std::nullopt;
        if (!annotation || detail::subtype_of(*annotation) != "FileAttachment") {
          continue;
        }
        const Object* fs = annotation->find("FS");
        const auto spec = dict_at(document, fs);
        // The specification is the identity when it is its own object; else the annotation is.
        const ObjectRef identity = fs != nullptr && fs->as_ref() ? *fs->as_ref() : *annotation_ref;
        if (!spec || !listed.insert(identity.number).second) {
          continue;
        }
        if (auto info = describe(document, identity, *spec)) {
          info->page = i;
          info->associated = std::ranges::find(af, identity) != af.end();
          out.push_back(std::move(*info));
        }
      }
    }
  }
  return out;
}

Result<AttachmentData> attachment_data(Document& document, ObjectRef spec) {
  auto list = list_attachments(document);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  if (std::ranges::none_of(*list, [&](const AttachmentInfo& a) { return a.spec == spec; })) {
    return make_error(ErrorCode::kOutOfRange, std::format("{} {} R is not an attachment", spec.number, spec.generation));
  }
  auto stream = embedded_stream(document, spec);
  if (!stream) {
    return std::unexpected(std::move(stream.error()));
  }
  auto bytes = document.decode_stream(*stream);
  if (!bytes) {
    return std::unexpected(std::move(bytes.error()));
  }
  AttachmentData out{.bytes = std::move(*bytes), .checksum_matches = std::nullopt};
  if (const auto params = dict_at(document, stream->dict.find("Params"))) {
    const std::string checksum = bytes_of(params->find("CheckSum"));
    if (checksum.size() == crypto::Md5::kDigestSize) {
      crypto::Md5 md5;
      md5.update(out.bytes);
      const auto digest = md5.finish();
      out.checksum_matches = std::ranges::equal(digest, checksum, [](std::byte d, char c) { return d == static_cast<std::byte>(c); });
    }
  }
  return out;
}

Result<ObjectRef> add_attachment(Document& document, const AttachmentSpec& spec, std::span<const std::byte> data) {
  if (auto sound = detail::require_sound_structure(document, "attachments"); !sound) {
    return std::unexpected(std::move(sound.error()));
  }
  if (spec.file_name.empty()) {
    return make_error(ErrorCode::kUnsupported, "an attachment needs a file name");
  }
  if (std::ranges::find(kRelationships, spec.relationship) == kRelationships.end()) {
    return make_error(ErrorCode::kUnsupported, "unknown relationship /" + spec.relationship);
  }
  auto place = place_of(document);
  if (!place) {
    return std::unexpected(std::move(place.error()));
  }
  auto compressed = filters::flate_encode(data);
  if (!compressed) {
    return std::unexpected(std::move(compressed.error()));
  }
  // A key not yet taken: "name", "name (2)", ...
  std::set<std::string> taken;
  for (const Entry& e : place->entries) {
    taken.insert(model::to_utf8(model::decode_text_string(e.key)));
  }
  std::string key = spec.file_name;
  for (int n = 2; taken.contains(key); ++n) {
    key = std::format("{} ({})", spec.file_name, n);
  }

  crypto::Md5 md5;
  md5.update(data);
  const auto digest = md5.finish();
  Dict params;
  params.set("Size", static_cast<std::int64_t>(data.size()));
  std::string checksum(digest.size(), '\0');
  std::ranges::transform(digest, checksum.begin(), [](std::byte b) { return static_cast<char>(b); });
  params.set("CheckSum", model::String{.bytes = std::move(checksum)});
  if (!spec.creation_date.empty()) {
    params.set("CreationDate", model::String{.bytes = spec.creation_date});
  }
  if (!spec.modification_date.empty()) {
    params.set("ModDate", model::String{.bytes = spec.modification_date});
  }
  model::Stream file;
  file.dict.set("Type", name("EmbeddedFile"));
  if (!spec.mime_type.empty()) {
    file.dict.set("Subtype", name(spec.mime_type));
  }
  file.dict.set("Params", std::move(params));
  file.dict.set("Filter", name("FlateDecode"));
  file.memory_data = std::make_shared<const std::vector<std::byte>>(std::move(*compressed));
  auto file_ref = document.add(Object(std::move(file)));
  if (!file_ref) {
    return file_ref;
  }
  const model::String text_name{.bytes = model::encode_text_string(model::from_utf8(spec.file_name))};
  Dict ef;
  ef.set("F", *file_ref);
  ef.set("UF", *file_ref);
  Dict filespec;
  filespec.set("Type", name("Filespec"));
  filespec.set("F", text_name);
  filespec.set("UF", text_name);
  if (!spec.description.empty()) {
    filespec.set("Desc", model::String{.bytes = model::encode_text_string(model::from_utf8(spec.description))});
  }
  filespec.set("EF", std::move(ef));
  filespec.set("AFRelationship", name(spec.relationship));
  auto spec_ref = document.add(Object(std::move(filespec)));
  if (!spec_ref) {
    return spec_ref;
  }
  std::vector<Entry> entries = place->entries;
  entries.push_back({.key = model::encode_text_string(model::from_utf8(key)), .value = Object(*spec_ref)});
  std::vector<ObjectRef> af = associated(document, place->catalog);
  af.push_back(*spec_ref);
  if (auto written = write_place(document, *place, std::move(entries), af); !written) {
    return std::unexpected(std::move(written.error()));
  }
  return *spec_ref;
}

Result<void> remove_attachment(Document& document, ObjectRef spec) {
  if (auto sound = detail::require_sound_structure(document, "attachments"); !sound) {
    return sound;
  }
  auto list = list_attachments(document);
  if (!list) {
    return std::unexpected(std::move(list.error()));
  }
  const auto found = std::ranges::find_if(*list, [&](const AttachmentInfo& a) { return a.spec == spec; });
  if (found == list->end()) {
    return make_error(ErrorCode::kOutOfRange, std::format("{} {} R is not an attachment", spec.number, spec.generation));
  }
  if (found->page) {
    return make_error(ErrorCode::kUnsupported, "the file of an annotation goes with its annotation");
  }
  auto place = place_of(document);
  if (!place) {
    return std::unexpected(std::move(place.error()));
  }
  std::vector<Entry> entries = place->entries;
  std::erase_if(entries, [&](const Entry& e) { return e.value.as_ref() == std::optional<ObjectRef>(spec); });
  std::vector<ObjectRef> af = associated(document, place->catalog);
  std::erase(af, spec);
  return write_place(document, *place, std::move(entries), af);
}

}  // namespace capriole::edit
