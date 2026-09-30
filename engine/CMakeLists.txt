# The substitutes of the standard 14 fonts (font/standard_fonts.hpp), built in: in StandardFont order.
include(${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake)
set(_foxit ${CAPRIOLE_FOXIT_FONTS_DIR})
capriole_embed_files(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/standard_fonts_data.cpp FUNCTION embedded_standard_font FILES
  ${_foxit}/FoxitSans.pfb ${_foxit}/FoxitSansBold.pfb ${_foxit}/FoxitSansItalic.pfb ${_foxit}/FoxitSansBoldItalic.pfb
  ${_foxit}/FoxitSerif.pfb ${_foxit}/FoxitSerifBold.pfb ${_foxit}/FoxitSerifItalic.pfb ${_foxit}/FoxitSerifBoldItalic.pfb
  ${_foxit}/FoxitFixed.pfb ${_foxit}/FoxitFixedBold.pfb ${_foxit}/FoxitFixedItalic.pfb ${_foxit}/FoxitFixedBoldItalic.pfb
  ${_foxit}/FoxitSymbol.pfb ${_foxit}/FoxitDingbats.pfb)

add_library(capriole_engine STATIC
  src/core/byte_source.cpp
  src/core/error.cpp
  src/core/source_reader.cpp
  src/core/version.cpp
  src/crypto/cipher.cpp
  src/crypto/hash.cpp
  src/crypto/security_handler.cpp
  src/document/document.cpp
  src/graphics/dash.cpp
  src/graphics/display_list.cpp
  src/graphics/shading.cpp
  src/graphics/svg.cpp
  src/render/blend.cpp
  src/render/png.cpp
  src/render/raster.cpp
  src/filters/basic.cpp
  src/filters/ccitt.cpp
  src/filters/dct.cpp
  src/filters/decode.cpp
  src/filters/flate.cpp
  src/filters/jbig2.cpp
  src/filters/jbig2_arith.cpp
  src/filters/jbig2_huffman.cpp
  src/filters/jpx.cpp
  src/font/cmap.cpp
  src/font/font.cpp
  src/font/glyph_names.cpp
  src/font/glyph_tables.cpp
  src/font/program.cpp
  src/font/resources.cpp
  src/font/standard_fonts.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/generated/standard_fonts_data.cpp
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
  src/content/cmyk_table.cpp
  src/content/color_space.cpp
  src/content/content_parser.cpp
  src/content/function.cpp
  src/content/icc.cpp
  src/content/image.cpp
  src/content/interpreter.cpp
  src/content/operators.cpp
  src/content/page_content.cpp
  src/content/shading.cpp
  src/writer/full_save.cpp
  src/writer/incremental_save.cpp
  src/writer/save_support.cpp
  src/writer/serializer.cpp)
add_library(capriole::engine ALIAS capriole_engine)

target_include_directories(capriole_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(capriole_engine PUBLIC cxx_std_23)
# The C bridge to libjpeg (its errors longjmp, which must stay out of C++ frames): C, with C warnings.
add_library(capriole_jpeg_shim STATIC src/filters/jpeg_shim.c)
set_target_properties(capriole_jpeg_shim PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(capriole_jpeg_shim PRIVATE capriole::jpeg)
if(NOT MSVC)
  target_compile_options(capriole_jpeg_shim PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow $<$<BOOL:${CAPRIOLE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

target_link_libraries(capriole_engine PRIVATE capriole::zlib blend2d::blend2d capriole_jpeg_shim capriole::openjpeg capriole::freetype capriole::lcms2)
capriole_set_warnings(capriole_engine)

configure_file(src/core/version_config.hpp.in include/capriole/core/version_config.hpp @ONLY)
