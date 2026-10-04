#pragma once

// Internal to edit/: the XMP of a Factur-X invoice (facturx.cpp), for the conversion to PDF/A (pdfa.cpp),
// which writes the packet anew.

#include <optional>
#include <string>
#include <string_view>

namespace capriole::edit::detail {

// The packet with the Factur-X properties (level: the invoice's conformance level) and the extension
// schema declaring them; nullopt for a packet without </rdf:RDF>.
[[nodiscard]] std::optional<std::string> with_facturx_properties(std::string xmp, std::string_view level);

}  // namespace capriole::edit::detail
