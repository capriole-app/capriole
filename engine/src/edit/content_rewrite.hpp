#pragma once

// Internal to edit/: what the edits that rewrite a page's content share (redact.cpp, content_move.cpp) —
// its operators parsed and written back, new content streams, and the XObjects of resources.

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "capriole/content/content_parser.hpp"
#include "capriole/core/error.hpp"
#include "capriole/document/document.hpp"
#include "capriole/model/object.hpp"

namespace capriole::edit::detail {

// The operators of a content stream (read tolerantly: what a reader draws).
[[nodiscard]] std::vector<content::Operation> operations_of(std::span<const std::byte> content);

// An operand, and an operator with its operands (an inline image whole), as content.
[[nodiscard]] std::string operand_text(const model::Object& operand);
[[nodiscard]] std::string op_text(const content::Operation& op);

// A new content stream (Flate) with the entries of dict, but those of the old data.
[[nodiscard]] Result<model::ObjectRef> add_content(Document& document, model::Dict dict, const std::string& content);

// Resources with new XObjects added and some removed (a copy: the original may be shared).
[[nodiscard]] model::Dict with_xobjects(Document& document, model::Dict resources, const std::vector<std::pair<std::string, model::ObjectRef>>& added,
                                        const std::set<std::string>& removed);

// An XObject name not in the resources yet, nor in taken (where it goes): prefix and a number.
[[nodiscard]] std::string fresh_name(Document& document, const model::Dict& resources, std::set<std::string>& taken, std::string_view prefix);

}  // namespace capriole::edit::detail
