cmake_minimum_required(VERSION 3.28)

project(capriole
  VERSION 0.0.1
  DESCRIPTION "Open source PDF engine and editor"
  LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset debug")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules yet: skip the dependency scan on every file.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CaprioleOptions)
include(CaprioleWarnings)
include(CaprioleSanitizers)
include(Dependencies)

if(EMSCRIPTEN)
  # The WebAssembly stack is fixed (64 KB by default) and has no guard page: overflowing it silently
  # corrupts memory. Parsing recurses up to ParseLimits::max_nesting_depth levels, so the stack must
  # hold that depth with margin. tests/ links with STACK_OVERFLOW_CHECK=2 to prove it does.
  add_link_options("-sSTACK_SIZE=1MB")
  # Binaryen's constraint-analysis pass (emsdk 6.0.10) aborts on an assertion
  # (constraint.cpp: "!constraints.provesEverything()") while optimizing edit/text_edit.cpp's code after
  # inlining; which function trips it moves with the code. The pass is skipped until a fixed Binaryen.
  add_link_options("-sBINARYEN_EXTRA_PASSES=--skip-pass=constraint-analysis")
endif()

if(CAPRIOLE_BUILD_TESTS)
  # Before any add_subdirectory: add_test() calls made earlier are silently ignored.
  enable_testing()
endif()

add_subdirectory(engine)
add_subdirectory(capi)

if(EMSCRIPTEN)
  add_subdirectory(bindings/wasm)
else()
  add_subdirectory(tools/cpdf)
endif()

if(CAPRIOLE_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(CAPRIOLE_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()

if(CAPRIOLE_RASTER_PROTO)
  add_subdirectory(experiments/raster)
endif()
