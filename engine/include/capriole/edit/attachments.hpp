#pragma once

// Attachments (PLAN.md phase 8f, ISO 32000-2 §7.11): the files embedded in a document — those of the
// catalog's /EmbeddedFiles name tree (§7.7.4), and those of file attachment annotations (§12.5.6.15) on
// its pages —; their bytes; adding and removing the document's own. An added file is associated with
// the document (§14.13: the catalog's /AF, with its /AFRelationship), as PDF/A-3 wants, compressed, with
// its size, dates, MIME type and MD5 checksum (/Params).
//
// The name tree, when changed, is written flat (one /Names array, sorted by key); nodes it no longer
// uses stay in the file until a compact save. Edits mark only the objects they touch; a document whose
// structure needed repairs is refused for edits (edit/pages.hpp). Listing and reading work on any
// document.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit {

struct AttachmentInfo {
  model::ObjectRef spec;              // the file specification: the attachment's identity
  std::string name;                   // its key in /EmbeddedFiles (UTF-8); empty for an annotation's
  std::string file_name;              // /UF, else /F (UTF-8)
  std::string description;            // /Desc (UTF-8)
  std::string mime_type;              // the embedded file's /Subtype ("text/xml")
  std::optional<std::uint64_t> size;  // /Params /Size
  std::string creation_date;          // /Params, as written (a PDF date)
  std::string modification_date;
  std::string relationship;           // /AFRelationship ("Data", "Source", "Alternative", ...); empty when none
  std::optional<std::uint32_t> page;  // a file attachment annotation's page (from 0); none for the document's
  bool associated = false;            // in the catalog's /AF
};

// The document's attachments: /EmbeddedFiles in key order, then the file attachment annotations in page
// order. Specifications without an embedded file (a reference to a file elsewhere) are left out.
[[nodiscard]] Result<std::vector<AttachmentInfo>> list_attachments(Document& document);

struct AttachmentData {
  std::vector<std::byte> bytes;
  // /Params /CheckSum: none when absent, else whether the bytes match it (MD5).
  std::optional<bool> checksum_matches;
};

// The bytes of an attachment (spec as listed). kOutOfRange for what is not a listed attachment.
[[nodiscard]] Result<AttachmentData> attachment_data(Document& document, model::ObjectRef spec);

// Relationship of an associated file to the document (§14.13.2, Table 43).
struct AttachmentSpec {
  std::string file_name;  // UTF-8; also its key in /EmbeddedFiles (made unique: "name (2)")
  std::string description;
  std::string mime_type;                     // e.g. "text/xml"; none when empty
  std::string relationship = "Unspecified";  // Source, Data, Alternative, Supplement, EncryptedPayload, FormData, Schema, Unspecified
  std::string creation_date;                 // PDF dates; none when empty
  std::string modification_date;
};

// Embeds a file in the document; returns its file specification. kUnsupported for an empty file name or
// an unknown relationship.
[[nodiscard]] Result<model::ObjectRef> add_attachment(Document& document, const AttachmentSpec& spec, std::span<const std::byte> data);

// Removes one of the document's attachments (from /EmbeddedFiles and /AF). An annotation's file goes
// with its annotation (edit/annotations.hpp): kUnsupported. kOutOfRange for what is not listed.
[[nodiscard]] Result<void> remove_attachment(Document& document, model::ObjectRef spec);

}  // namespace capriole::edit
