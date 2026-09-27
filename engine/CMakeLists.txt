add_library(capriole_engine STATIC
  src/core/byte_source.cpp
  src/core/error.cpp
  src/core/source_reader.cpp
  src/core/version.cpp
  src/crypto/cipher.cpp
  src/crypto/hash.cpp
  src/crypto/security_handler.cpp
  src/document/document.cpp
  src/filters/basic.cpp
  src/filters/decode.cpp
  src/filters/flate.cpp
  src/model/object.cpp
  src/parser/header.cpp
  src/parser/lexer.cpp
  src/parser/object_loader.cpp
  src/parser/object_parser.cpp
  src/parser/object_stream.cpp
  src/parser/xref.cpp
  src/repair/reconstruct.cpp
  src/repair/repair.cpp
  src/writer/byte_sink.cpp
  src/content/content_parser.cpp
  src/content/operators.cpp
  src/content/page_content.cpp
  src/writer/full_save.cpp
  src/writer/incremental_save.cpp
  src/writer/save_support.cpp
  src/writer/serializer.cpp)
add_library(capriole::engine ALIAS capriole_engine)

target_include_directories(capriole_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
target_compile_features(capriole_engine PUBLIC cxx_std_23)
target_link_libraries(capriole_engine PRIVATE capriole::zlib)
capriole_set_warnings(capriole_engine)

configure_file(src/core/version_config.hpp.in include/capriole/core/version_config.hpp @ONLY)
