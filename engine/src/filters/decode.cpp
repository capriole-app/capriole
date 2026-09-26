#include "capriole/filters/decode.hpp"

#include <string>
#include <utility>

#include "capriole/filters/basic.hpp"
#include "capriole/filters/flate.hpp"

namespace capriole::filters {

namespace {

using model::Dict;
using model::Object;

Result<std::vector<std::byte>> apply_filter(std::vector<std::byte> data, const std::string& name, const Dict* parms, const ParseLimits& limits, bool lenient) {
  const std::size_t max = limits.max_decoded_stream_bytes;
  if (name == "FlateDecode" || name == "LZWDecode") {
    auto params = predictor_params(parms);
    if (!params) {
      return std::unexpected(std::move(params.error()));
    }
    Result<std::vector<std::byte>> expanded;
    if (name == "FlateDecode") {
      expanded = flate_decode(data, max, lenient);
    } else {
      // /EarlyChange defaults to 1 (§7.4.4.3, Table 8).
      bool early_change = true;
      if (const model::Object* value = parms != nullptr ? parms->find("EarlyChange") : nullptr) {
        const auto integer = value->as_integer();
        if (!integer || (*integer != 0 && *integer != 1)) {
          return make_error(ErrorCode::kSyntax, "/EarlyChange must be 0 or 1");
        }
        early_change = *integer == 1;
      }
      expanded = lzw_decode(data, early_change, max, lenient);
    }
    if (!expanded) {
      return expanded;
    }
    return apply_predictor(std::move(*expanded), *params, lenient);
  }
  if (name == "ASCIIHexDecode") {
    return ascii_hex_decode(data, max, lenient);
  }
  if (name == "ASCII85Decode") {
    return ascii85_decode(data, max, lenient);
  }
  if (name == "RunLengthDecode") {
    return run_length_decode(data, max, lenient);
  }
  if (name == "Crypt") {
    // Decryption happens before the filter chain (ObjectLoader::read_stream_data), which also reads
    // this filter's /Name to choose the crypt filter: here it is a pass-through.
    return data;
  }
  return make_error(ErrorCode::kUnsupported, "filter /" + name + " is not supported yet");
}

// /DecodeParms entry i: a dictionary, or nullptr for null / absent.
Result<const Dict*> parms_at(const Object* decode_parms, std::size_t index, std::size_t filter_count) {
  if (decode_parms == nullptr || decode_parms->is_null()) {
    return nullptr;
  }
  if (const Dict* dict = decode_parms->as_dict()) {
    if (filter_count != 1) {
      return make_error(ErrorCode::kSyntax, "/DecodeParms must be an array when /Filter is an array");
    }
    return dict;
  }
  const model::Array* array = decode_parms->as_array();
  if (array == nullptr || array->size() != filter_count) {
    return make_error(ErrorCode::kSyntax, "/DecodeParms must be a dictionary or an array matching /Filter");
  }
  const Object& entry = (*array)[index];
  if (entry.is_null()) {
    return nullptr;
  }
  if (const Dict* dict = entry.as_dict()) {
    return dict;
  }
  return make_error(ErrorCode::kSyntax, "/DecodeParms entries must be dictionaries or null");
}

}  // namespace

Result<std::vector<std::byte>> read_encoded(ByteSource& source, const model::Stream& stream) {
  if (stream.data_length > source.size() || stream.data_offset > source.size() - stream.data_length) {
    return make_error(ErrorCode::kOutOfRange, "stream data outside the source", stream.data_offset);
  }
  std::vector<std::byte> data(static_cast<std::size_t>(stream.data_length));
  auto read = source.read(stream.data_offset, data);
  if (!read) {
    return std::unexpected(std::move(read.error()));
  }
  if (*read != data.size()) {
    return make_error(ErrorCode::kIo, "short read of stream data", stream.data_offset);
  }
  return data;
}

Result<std::vector<std::byte>> decode(std::vector<std::byte> encoded, const Dict& stream_dict, const ParseLimits& limits, bool lenient) {
  if (stream_dict.contains("F")) {
    return make_error(ErrorCode::kUnsupported, "streams with external data (/F) are not supported");
  }
  const Object* filter = stream_dict.find("Filter");
  if (filter == nullptr || filter->is_null()) {
    return encoded;
  }

  std::vector<const std::string*> names;
  if (const model::Name* name = filter->as_name()) {
    names.push_back(&name->bytes);
  } else if (const model::Array* array = filter->as_array()) {
    for (const Object& entry : *array) {
      const model::Name* entry_name = entry.as_name();
      if (entry_name == nullptr) {
        return make_error(ErrorCode::kSyntax, "/Filter array entries must be names");
      }
      names.push_back(&entry_name->bytes);
    }
  } else {
    return make_error(ErrorCode::kSyntax, "/Filter must be a name or an array of names");
  }

  const Object* decode_parms = stream_dict.find("DecodeParms");
  std::vector<std::byte> data = std::move(encoded);
  for (std::size_t i = 0; i < names.size(); ++i) {
    auto parms = parms_at(decode_parms, i, names.size());
    if (!parms) {
      return std::unexpected(std::move(parms.error()));
    }
    auto next = apply_filter(std::move(data), *names[i], *parms, limits, lenient);
    if (!next) {
      return next;
    }
    data = std::move(*next);
  }
  return data;
}

}  // namespace capriole::filters
