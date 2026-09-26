include(GoogleTest)

add_executable(capriole_unit_tests
  unit/byte_source_test.cpp
  unit/header_test.cpp
  unit/capi_test.cpp)
target_link_libraries(capriole_unit_tests PRIVATE capriole::engine capriole::capi GTest::gtest_main)
capriole_set_warnings(capriole_unit_tests)

# PRE_TEST: discover at ctest time, so cross-compiled (WASM) test binaries are not run during the build.
gtest_discover_tests(capriole_unit_tests DISCOVERY_MODE PRE_TEST)

# Regression suite (tests/regression/): one minimized file per fixed bug. Wired in with the parser (phase 2).
# Differential suite (tests/differential/): runs against external reference engines; not part of ctest.
