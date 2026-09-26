#pragma once

// Object serialisation (ISO 32000-2 §7.3): model::Object back to PDF syntax.
//
// The contract is a round trip: parsing what serialize() writes gives back an equal Object (same
// kinds, same bytes, same string forms, same key order). Output is compact but always separates
// tokens that would otherwise merge, and is deterministic.
//
// Streams: only the dictionary is written here; the writer adds "stream", the data and "endstream".

#include <string>

#include "capriole/model/object.hpp"

namespace capriole::writer {

// Appends the object's PDF syntax to out.
void serialize(const model::Object& object, std::string& out);
[[nodiscard]] std::string serialize(const model::Object& object);

// A real number without exponent (PDF has no exponent notation), with the fewest digits that read
// back to the same double, and always with a decimal point so that it reads back as a real.
// Non-finite values (which PDF cannot represent) are written as 0.0.
[[nodiscard]] std::string format_real(double value);

}  // namespace capriole::writer
