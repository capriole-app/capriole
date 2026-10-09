# libFuzzer targets. Built only with the 'fuzz' preset (Clang, ASan + UBSan).
#
#   cmake --preset fuzz && cmake --build --preset fuzz
#   ./build/fuzz/fuzz/fuzz_header -max_total_time=60 fuzz/corpus/header
#
# Every target takes the whole input as a file and must never crash, leak or hit UB, whatever the bytes.

# The fuzzing engine's runtime: libFuzzer by default, $LIB_FUZZING_ENGINE under OSS-Fuzz.
separate_arguments(_capriole_fuzzing_engine UNIX_COMMAND "${CAPRIOLE_FUZZING_ENGINE}")

function(capriole_add_fuzzer name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE capriole::engine)
  target_link_options(${name} PRIVATE ${_capriole_fuzzing_engine})
  capriole_set_warnings(${name})
endfunction()

capriole_add_fuzzer(fuzz_header fuzz_header.cpp)
capriole_add_fuzzer(fuzz_lexer fuzz_lexer.cpp)
capriole_add_fuzzer(fuzz_object fuzz_object.cpp)
capriole_add_fuzzer(fuzz_xref fuzz_xref.cpp)
capriole_add_fuzzer(fuzz_filters fuzz_filters.cpp)
capriole_add_fuzzer(fuzz_security fuzz_security.cpp)
capriole_add_fuzzer(fuzz_serialize fuzz_serialize.cpp)
capriole_add_fuzzer(fuzz_content fuzz_content.cpp)
capriole_add_fuzzer(fuzz_render fuzz_render.cpp)
capriole_add_fuzzer(fuzz_function fuzz_function.cpp)
capriole_add_fuzzer(fuzz_dct fuzz_dct.cpp)
capriole_add_fuzzer(fuzz_ccitt fuzz_ccitt.cpp)
capriole_add_fuzzer(fuzz_jbig2 fuzz_jbig2.cpp)
capriole_add_fuzzer(fuzz_jpx fuzz_jpx.cpp)
capriole_add_fuzzer(fuzz_font fuzz_font.cpp)
capriole_add_fuzzer(fuzz_cmap fuzz_cmap.cpp)
capriole_add_fuzzer(fuzz_shading fuzz_shading.cpp)
capriole_add_fuzzer(fuzz_text fuzz_text.cpp)
capriole_add_fuzzer(fuzz_page_model fuzz_page_model.cpp)
capriole_add_fuzzer(fuzz_object_edit fuzz_object_edit.cpp)
capriole_add_fuzzer(fuzz_pages fuzz_pages.cpp)
# Through the C API, as the apps edit annotations.
capriole_add_fuzzer(fuzz_annotations fuzz_annotations.cpp)
target_link_libraries(fuzz_annotations PRIVATE capriole::capi)
# Through the C API, as the apps fill forms.
capriole_add_fuzzer(fuzz_forms fuzz_forms.cpp)
target_link_libraries(fuzz_forms PRIVATE capriole::capi)
# Through the C API, as the apps handle attachments and Factur-X.
capriole_add_fuzzer(fuzz_attachments fuzz_attachments.cpp)
target_link_libraries(fuzz_attachments PRIVATE capriole::capi)
# Through the C API, as the apps redact.
capriole_add_fuzzer(fuzz_redact fuzz_redact.cpp)
target_link_libraries(fuzz_redact PRIVATE capriole::capi)
capriole_add_fuzzer(fuzz_icc fuzz_icc.cpp)
target_include_directories(fuzz_icc PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
# These two exercise the engine's internals (font/): its sources are their include path.
target_include_directories(fuzz_font PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
target_include_directories(fuzz_cmap PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)

# Digital signatures: CMS and time stamps, PKCS#12 files, and a document's signatures through the C API
# (listed, then signed with the test key and listed again).
capriole_add_fuzzer(fuzz_cms fuzz_cms.cpp)
capriole_add_fuzzer(fuzz_pkcs12 fuzz_pkcs12.cpp)
include(${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake)
capriole_embed_files(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/fuzz_signing_files.cpp FUNCTION fuzz_signing_file FILES
  ${PROJECT_SOURCE_DIR}/tests/data/signing/ec.p12 ${PROJECT_SOURCE_DIR}/tests/data/signing/ca.pem)
capriole_add_fuzzer(fuzz_signatures fuzz_signatures.cpp)
target_sources(fuzz_signatures PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/fuzz_signing_files.cpp)
target_link_libraries(fuzz_signatures PRIVATE capriole::capi)

# The conversion to PDF/A through the C API.
capriole_add_fuzzer(fuzz_pdfa fuzz_pdfa.cpp)
target_link_libraries(fuzz_pdfa PRIVATE capriole::capi)

# Editing a page's text through the C API.
capriole_add_fuzzer(fuzz_text_edit fuzz_text_edit.cpp)
target_link_libraries(fuzz_text_edit PRIVATE capriole::capi)

# Editing a paragraph through the C API.
capriole_add_fuzzer(fuzz_paragraph_edit fuzz_paragraph_edit.cpp)
target_link_libraries(fuzz_paragraph_edit PRIVATE capriole::capi)

# Extending TrueType subsets with a full font's glyphs (font/sfnt, edit/font_library); white-box.
capriole_add_fuzzer(fuzz_font_extend fuzz_font_extend.cpp)
target_include_directories(fuzz_font_extend PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
