include(GoogleTest)

add_executable(capriole_unit_tests
  unit/byte_source_test.cpp
  unit/header_test.cpp
  unit/lexer_test.cpp
  unit/object_parser_test.cpp
  unit/object_test.cpp
  unit/source_reader_test.cpp
  unit/capi_test.cpp
  unit/decode_test.cpp
  unit/flate_test.cpp
  unit/xref_test.cpp)
# support/ holds test helpers (PdfBuilder); zlib is linked to build compressed test data.
target_include_directories(capriole_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(capriole_unit_tests PRIVATE capriole::engine capriole::capi capriole::zlib GTest::gtest_main)
capriole_set_warnings(capriole_unit_tests)
if(EMSCRIPTEN)
  # Trap on stack overflow instead of corrupting memory: the nesting-limit tests then prove that the
  # deepest recursion allowed by ParseLimits fits the WebAssembly stack (see the root CMakeLists).
  target_link_options(capriole_unit_tests PRIVATE "-sSTACK_OVERFLOW_CHECK=2")
endif()

# PRE_TEST: discover at ctest time, so cross-compiled (WASM) test binaries are not run during the build.
gtest_discover_tests(capriole_unit_tests DISCOVERY_MODE PRE_TEST)

# Regression suite (tests/regression/): one minimized file per fixed bug. Wired in with the parser (phase 2).
# Differential suite (tests/differential/): runs against external reference engines; not part of ctest.
