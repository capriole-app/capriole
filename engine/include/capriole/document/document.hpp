#pragma once

// The low-level document model (DOCS/CAPRIOLE.md §5.1): the file's objects, exactly as the file
// defines them, plus what the user changed. It is the source of truth that the semantic model (§5.2)
// is built on and that the writer serialises.
//
// Objects are loaded lazily through the cross-reference table and cached. Each object the document
// has touched carries a state; only Modified, New and Deleted objects are written by an incremental
// save, so everything else stays byte-identical and existing signatures remain verifiable (§5.3).
//
// Object pointers returned by get() stay valid until that object is changed (set/remove) or the
// document is destroyed.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capriole/core/byte_source.hpp"
#include "capriole/core/error.hpp"
#include "capriole/core/limits.hpp"
#include "capriole/crypto/security_handler.hpp"
#include "capriole/model/object.hpp"
#include "capriole/parser/header.hpp"
#include "capriole/parser/object_loader.hpp"
#include "capriole/parser/xref.hpp"
#include "capriole/repair/repair.hpp"

namespace capriole {

enum class ObjectState : std::uint8_t {
  kUnmodified,  // as in the file
  kModified,    // exists in the file, value replaced
  kNew,         // added in this session
  kDeleted,     // exists in the file, removed in this session
};

struct OpenOptions {
  ParseLimits limits = kDefaultParseLimits;
  // Tried as the user password, then as the owner password. Most encrypted files open with the
  // empty user password (they only restrict permissions).
  std::string password;
  // Let the repair layer (repair/) fix files that strict reading rejects; every repair applied is
  // listed by Document::repairs(). false = strict only, for diagnostics.
  bool repair = true;
};

// Whether a dictionary in a page's place can be taken as a page: /Type /Page (or /Pages without
// /Kids, as the readers do), no /Type, /Page in another case (/pagE), or another /Type on something
// with a /MediaBox. Another object that ended up there (a font descriptor, say) cannot — the repairs
// and the writer keep it as it is.
[[nodiscard]] bool looks_like_page(const model::Dict& dict);

class Document {
 public:
  // Opens a document from a source it takes ownership of: header, cross-reference sections and,
  // for an encrypted file, the security handler. ErrorCode::kPasswordRequired when the file is
  // encrypted and the password does not open it.
  [[nodiscard]] static Result<Document> open(std::unique_ptr<ByteSource> source, const OpenOptions& options = {});

  Document(Document&&) noexcept;
  Document& operator=(Document&&) noexcept;
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  ~Document();

  [[nodiscard]] const parser::PdfHeader& header() const noexcept { return header_; }
  // The cross-reference sections as read from the file (never changed by edits).
  [[nodiscard]] const parser::XrefTable& xref() const noexcept { return loader_->xref(); }
  [[nodiscard]] ByteSource& source() noexcept { return *source_; }
  // Repairs applied while opening (empty for a file that conforms).
  [[nodiscard]] const std::vector<repair::RepairNote>& repairs() const noexcept { return repairs_; }
  // The security handler of an encrypted document (permissions, revision); nullptr otherwise.
  [[nodiscard]] const crypto::StandardSecurityHandler* security() const noexcept { return security_.get(); }

  // ── Reading ─────────────────────────────────────────────────────────────────────────────

  // The object with this number (any generation); a null object when free, absent or deleted.
  [[nodiscard]] Result<const model::Object*> get(std::uint32_t number);
  // The referenced object; null when the generation does not match (§7.3.10).
  [[nodiscard]] Result<const model::Object*> get(model::ObjectRef ref);
  // Follows a reference (one level); returns a copy of object itself otherwise.
  [[nodiscard]] Result<model::Object> resolve(const model::Object& object);

  // Decoded data of a stream belonging to this document (from the file or held in memory).
  [[nodiscard]] Result<std::vector<std::byte>> decode_stream(const model::Stream& stream);

  [[nodiscard]] const model::Dict& trailer() const noexcept { return trailer_; }
  // The document catalog (trailer /Root).
  [[nodiscard]] Result<const model::Dict*> catalog();
  // References of the page objects in order, walking the page tree (§7.7.3).
  [[nodiscard]] Result<std::vector<model::ObjectRef>> page_refs();

  // ── Editing ─────────────────────────────────────────────────────────────────────────────

  // Replaces an existing object. Error when the object does not exist or was deleted.
  [[nodiscard]] Result<void> set(std::uint32_t number, model::Object value);
  // Adds a new object with the next free number, generation 0. kLimitExceeded when the object
  // numbers are exhausted (a hostile /Size can start them near the top of the range).
  [[nodiscard]] Result<model::ObjectRef> add(model::Object value);
  // Deletes an object (a new one disappears entirely). Error when it does not exist.
  [[nodiscard]] Result<void> remove(std::uint32_t number);
  // Sets (or, with a null value, removes) a trailer entry, e.g. /Info or /Root.
  void set_trailer_entry(std::string key, model::Object value);

  [[nodiscard]] ObjectState state(std::uint32_t number) const;
  // Numbers of Modified, New and Deleted objects, ascending: what an incremental save writes.
  [[nodiscard]] std::vector<std::uint32_t> changed_objects() const;
  [[nodiscard]] bool trailer_modified() const noexcept { return trailer_modified_; }
  [[nodiscard]] bool has_changes() const { return trailer_modified_ || !changed_objects().empty(); }
  // One past the highest object number in use or ever assigned (the next /Size).
  [[nodiscard]] std::uint32_t next_object_number() const noexcept { return next_number_; }

  // ── For the writer ──────────────────────────────────────────────────────────────────────

  // Every object that exists now (in the file and not deleted, or added), ascending by number.
  [[nodiscard]] std::vector<model::ObjectRef> live_objects() const;
  // Generation of an object as the document knows it (0 for objects in object streams and new ones).
  [[nodiscard]] std::uint16_t generation(std::uint32_t number) const;
  // Whether the page tree as it is in the file, read strictly, gives exactly these pages (the result
  // of page_refs()) and its root /Count agrees. When not, the pages were found by repairs that exist
  // only in memory, and a full save must write the page tree anew.
  [[nodiscard]] bool page_tree_conforms(const std::vector<model::ObjectRef>& pages);
  // A stream's encoded data as it will be written: decrypted if the file is encrypted, not decoded.
  [[nodiscard]] Result<std::vector<std::byte>> encoded_stream_data(const model::Stream& stream);

 private:
  struct Slot {
    ObjectState state = ObjectState::kUnmodified;
    std::uint16_t generation = 0;
    model::Object object;
  };

  Document() = default;
  // One strict attempt. source is moved into the document only on success. With rebuilt, that table is
  // used instead of the file's cross-reference sections (the reconstruction repair).
  static Result<Document> build(std::unique_ptr<ByteSource>& source, const OpenOptions& options, const parser::XrefTable* rebuilt = nullptr);
  // Whether the trailer's /Root leads to a dictionary (without the catalog repair).
  bool root_is_dictionary();
  // (misplaced, sampled): of a sample of in-use entries, how many do not load — a table
  // shifted by a few objects fails most of them, a valid one none.
  std::pair<std::size_t, std::size_t> sample_table();
  // Records a repair (the list is capped: a badly broken file can need one per object).
  void note(repair::RepairKind kind, std::string description);
  // Repairs for an object whose strict load failed (tolerant parsing, then the scanned position).
  Result<model::Object> repair_object(std::uint32_t number, const Error& strict_error);
  // The page tree walked as it is; with infer_types, a node without a usable /Type is taken as a
  // /Pages node or a page (repair).
  Result<std::vector<model::ObjectRef>> walk_page_tree(model::ObjectRef root, bool infer_types);
  // The page tree rebuilt from /Kids and /Parent links (repair); keep_unreadable counts /Kids entries
  // that cannot be read as placeholder pages.
  Result<std::vector<model::ObjectRef>> rebuild_page_tree(model::ObjectRef root, bool keep_unreadable);
  // Loader with a security handler matching the document's.
  [[nodiscard]] std::unique_ptr<parser::ObjectLoader> make_loader(parser::XrefTable table, parser::ParseMode mode) const;
  // The slot for an object that exists (loading it from the file if needed); nullptr otherwise.
  Result<Slot*> slot(std::uint32_t number);

  std::unique_ptr<ByteSource> source_;
  std::unique_ptr<parser::ObjectLoader> loader_;
  std::shared_ptr<const crypto::StandardSecurityHandler> security_;
  std::vector<repair::RepairNote> repairs_;
  bool repair_enabled_ = true;
  std::optional<std::uint32_t> encrypt_object_;
  std::unique_ptr<parser::ObjectLoader> tolerant_loader_;  // same table, tolerant parser (lazy)
  std::unique_ptr<parser::ObjectLoader> scan_loader_;      // table rebuilt by scanning (lazy)
  bool scan_attempted_ = false;
  bool catalog_search_done_ = false;  // the kCatalogFound repair runs once
  ParseLimits limits_;
  parser::PdfHeader header_;
  model::Dict trailer_;
  bool trailer_modified_ = false;
  std::map<std::uint32_t, Slot> slots_;
  std::uint32_t next_number_ = 1;
};

}  // namespace capriole
