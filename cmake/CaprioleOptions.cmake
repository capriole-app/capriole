# Build options. Presets in CMakePresets.json set these; defaults suit a plain native debug build.

option(CAPRIOLE_BUILD_TESTS "Build unit and regression tests" ON)
option(CAPRIOLE_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(CAPRIOLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(CAPRIOLE_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers, e.g. address;undefined")

if(CAPRIOLE_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "CAPRIOLE_BUILD_FUZZERS requires Clang (libFuzzer). Use the 'fuzz' preset.")
endif()

if(CAPRIOLE_SANITIZERS AND EMSCRIPTEN)
  message(FATAL_ERROR "Sanitizers are not supported in the WebAssembly build")
endif()
