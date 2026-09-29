include(GoogleTest)

add_executable(capriole_unit_tests
  unit/byte_source_test.cpp
  unit/header_test.cpp
  unit/lexer_test.cpp
  unit/object_parser_test.cpp
  unit/object_test.cpp
  unit/regression_test.cpp
  unit/repair_test.cpp
  unit/save_test.cpp
  unit/serializer_test.cpp
  unit/source_reader_test.cpp
  unit/capi_test.cpp
  unit/content_parser_test.cpp
  unit/font_test.cpp
  unit/shading_test.cpp
  unit/interpreter_test.cpp
  unit/blend_test.cpp
  unit/ccitt_test.cpp
  unit/dct_test.cpp
  unit/function_test.cpp
  unit/jbig2_test.cpp
  unit/jpx_test.cpp
  unit/png_test.cpp
  unit/render_test.cpp
  unit/basic_filters_test.cpp
  unit/crypto_test.cpp
  unit/decode_test.cpp
  unit/document_test.cpp
  unit/encryption_test.cpp
  unit/flate_test.cpp
  unit/xref_test.cpp)
# support/ holds test helpers (PdfBuilder); zlib is linked to build compressed test data.
# engine/src too: white-box tests of internal pieces (the JBIG2 Huffman tables).
target_include_directories(capriole_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/engine/src)
target_link_libraries(capriole_unit_tests PRIVATE capriole::engine capriole::capi capriole::zlib GTest::gtest_main)
capriole_set_warnings(capriole_unit_tests)
# Test data files (fixtures) are read from the source tree.
target_compile_definitions(capriole_unit_tests PRIVATE CAPRIOLE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(EMSCRIPTEN)
  # NODERAWFS: the tests run under Node and read the fixtures from the real file system.
  target_link_options(capriole_unit_tests PRIVATE "-sNODERAWFS=1")
  # Trap on stack overflow instead of corrupting memory: the nesting-limit tests then prove that the
  # deepest recursion allowed by ParseLimits fits the WebAssembly stack (see the root CMakeLists).
  target_link_options(capriole_unit_tests PRIVATE "-sSTACK_OVERFLOW_CHECK=2")
  # Memory grows as in the real module (bindings/wasm): images decode to tens of megabytes.
  target_link_options(capriole_unit_tests PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sMAXIMUM_MEMORY=4GB")
endif()

# PRE_TEST: discover at ctest time, so cross-compiled (WASM) test binaries are not run during the build.
gtest_discover_tests(capriole_unit_tests DISCOVERY_MODE PRE_TEST)

# Regression suite (tests/regression/<area>/): one input per fixed bug, replayed by unit/regression_test.cpp.
# Differential suite (tests/differential/): runs against external reference engines; not part of ctest.
