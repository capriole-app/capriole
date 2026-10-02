# libFuzzer targets. Built only with the 'fuzz' preset (Clang, ASan + UBSan).
#
#   cmake --preset fuzz && cmake --build --preset fuzz
#   ./build/fuzz/fuzz/fuzz_header -max_total_time=60 fuzz/corpus/header
#
# Every target takes the whole input as a file and must never crash, leak or hit UB, whatever the bytes.

function(capriole_add_fuzzer name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE capriole::engine)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
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
capriole_add_fuzzer(fuzz_pages fuzz_pages.cpp)
# Through the C API, as the apps edit annotations.
capriole_add_fuzzer(fuzz_annotations fuzz_annotations.cpp)
target_link_libraries(fuzz_annotations PRIVATE capriole::capi)
capriole_add_fuzzer(fuzz_icc fuzz_icc.cpp)
target_include_directories(fuzz_icc PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
# These two exercise the engine's internals (font/): its sources are their include path.
target_include_directories(fuzz_font PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
target_include_directories(fuzz_cmap PRIVATE ${PROJECT_SOURCE_DIR}/engine/src)
