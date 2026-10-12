# Build options. Presets in CMakePresets.json set these; defaults suit a plain native debug build.

option(CAPRIOLE_BUILD_TESTS "Build unit and regression tests" ON)
option(CAPRIOLE_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(CAPRIOLE_RASTER_PROTO "Build the phase 6c rasterizer prototype (experiments/raster; needs third-party sources)" OFF)
option(CAPRIOLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(CAPRIOLE_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers, e.g. address;undefined")
# Fuzz builds for OSS-Fuzz (ci/oss-fuzz/build.sh): its flags bring the sanitizer and the coverage
# instrumentation (CFLAGS/CXXFLAGS), its engine's runtime comes from $LIB_FUZZING_ENGINE.
set(CAPRIOLE_FUZZING_ENGINE "-fsanitize=fuzzer" CACHE STRING "How fuzz targets link their fuzzing engine")
option(CAPRIOLE_FUZZ_INSTRUMENT "Fuzz builds: add libFuzzer's coverage instrumentation (off when the flags bring it)" ON)
set(CAPRIOLE_DEPS_DIR "" CACHE PATH "The dependencies' archives, already fetched (builds without network); empty: download them")

if(CAPRIOLE_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "CAPRIOLE_BUILD_FUZZERS requires Clang (libFuzzer). Use the 'fuzz' preset.")
endif()

if(CAPRIOLE_SANITIZERS AND EMSCRIPTEN)
  message(FATAL_ERROR "Sanitizers are not supported in the WebAssembly build")
endif()
