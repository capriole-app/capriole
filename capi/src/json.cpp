#include "json.hpp"

#include <array>
#include <format>

#include "capriole/writer/serializer.hpp"

namespace capriole::capi {

void append_json_string(std::string& out, std::string_view bytes) {
  out += '"';
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte == '"' || byte == '\\') {
      out += '\\';
      out += c;
    } else if (byte >= 0x20 && byte < 0x7F) {
      out += c;
    } else {
      out += std::format("\\u{:04x}", byte);
    }
  }
  out += '"';
}

namespace {

// Integers and reals as strings: int64 values do not all fit a JavaScript number, and a real keeps
// the exact text the engine would write.
void append_number(std::string& out, std::string_view kind, std::string_view text) {
  out += R"({"t":")";
  out += kind;
  out += R"(","v":")";
  out += text;
  out += "\"}";
}

void append_dict(std::string& out, const model::Dict& dict) {
  // Entries as [key, value] pairs: the file's order is kept (a JSON object would not guarantee it).
  out += '[';
  bool first = true;
  for (const model::DictEntry& entry : dict.entries()) {
    if (!first) {
      out += ',';
    }
    first = false;
    out += '[';
    append_json_string(out, entry.key);
    out += ',';
    append_json_object(out, entry.value);
    out += ']';
  }
  out += ']';
}

}  // namespace

void append_json_object(std::string& out, const model::Object& object) {
  switch (object.kind()) {
    case model::ObjectKind::kNull:
      out += R"({"t":"null"})";
      return;
    case model::ObjectKind::kBoolean:
      out += std::get<bool>(object.value()) ? R"({"t":"bool","v":true})" : R"({"t":"bool","v":false})";
      return;
    case model::ObjectKind::kInteger:
      append_number(out, "int", std::to_string(std::get<std::int64_t>(object.value())));
      return;
    case model::ObjectKind::kReal:
      append_number(out, "real", writer::format_real(std::get<double>(object.value())));
      return;
    case model::ObjectKind::kString: {
      const auto& string = std::get<model::String>(object.value());
      out += R"({"t":"string","form":")";
      out += string.form == model::StringForm::kHex ? "hex" : "literal";
      out += R"(","v":)";
      append_json_string(out, string.bytes);
      out += '}';
      return;
    }
    case model::ObjectKind::kName:
      out += R"({"t":"name","v":)";
      append_json_string(out, std::get<model::Name>(object.value()).bytes);
      out += '}';
      return;
    case model::ObjectKind::kArray: {
      out += R"({"t":"array","v":[)";
      bool first = true;
      for (const model::Object& element : std::get<model::Array>(object.value())) {
        if (!first) {
          out += ',';
        }
        first = false;
        append_json_object(out, element);
      }
      out += "]}";
      return;
    }
    case model::ObjectKind::kDictionary:
      out += R"({"t":"dict","v":)";
      append_dict(out, std::get<model::Dict>(object.value()));
      out += '}';
      return;
    case model::ObjectKind::kStream: {
      const auto& stream = std::get<model::Stream>(object.value());
      const std::uint64_t length = stream.in_memory() ? stream.memory_data->size() : stream.data_length;
      out += R"({"t":"stream","dict":)";
      append_dict(out, stream.dict);
      out += std::format(R"(,"length":{},"offset":{}}})", length, stream.in_memory() ? -1 : static_cast<std::int64_t>(stream.data_offset));
      return;
    }
    case model::ObjectKind::kReference: {
      const auto ref = std::get<model::ObjectRef>(object.value());
      out += std::format(R"({{"t":"ref","n":{},"g":{}}})", ref.number, ref.generation);
      return;
    }
  }
}

}  // namespace capriole::capi
