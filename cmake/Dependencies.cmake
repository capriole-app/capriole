# Third-party dependencies.
#
# Rule (docs/CAPRIOLE.md §4.2): only permissive licenses may be linked into the engine. Every new
# dependency is added here with a pinned version + SHA-256 and recorded in docs/DEPENDENCIES.md
# with its license, before any code uses it.

include(FetchContent)

# zlib 1.3.1 — Zlib license. FlateDecode (and, later, compression in the writer).
# zlib's own CMakeLists is not used (global include_directories, a shared library, a CMake minimum
# that newer CMake rejects): SOURCE_SUBDIR points nowhere so nothing is added, and the library is
# built below from its sources. One pinned version on every platform, WASM included.
FetchContent_Declare(zlib
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-zlib-cmake)
FetchContent_MakeAvailable(zlib)

# The gz* file API is not needed (and wants unistd.h): only the in-memory codec is built.
add_library(capriole_zlib STATIC
  ${zlib_SOURCE_DIR}/adler32.c
  ${zlib_SOURCE_DIR}/compress.c
  ${zlib_SOURCE_DIR}/crc32.c
  ${zlib_SOURCE_DIR}/deflate.c
  ${zlib_SOURCE_DIR}/infback.c
  ${zlib_SOURCE_DIR}/inffast.c
  ${zlib_SOURCE_DIR}/inflate.c
  ${zlib_SOURCE_DIR}/inftrees.c
  ${zlib_SOURCE_DIR}/trees.c
  ${zlib_SOURCE_DIR}/uncompr.c
  ${zlib_SOURCE_DIR}/zutil.c)
add_library(capriole::zlib ALIAS capriole_zlib)
# SYSTEM: third-party headers must not trip our warning flags.
target_include_directories(capriole_zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
set_target_properties(capriole_zlib PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

# Blend2D (2D rasterizer, ADR 0002) + AsmJit (its JIT) — both Zlib license, pinned commits.
# AsmJit is only fetched: Blend2D compiles it into itself from ASMJIT_DIR. The JIT runs in native release
# builds; WebAssembly cannot generate code, and in sanitizer and fuzz builds the reference pipelines
# (plain C++) are what the tools can check, so there Blend2D is built without it.
FetchContent_Declare(asmjit
  URL https://github.com/asmjit/asmjit/archive/0d7f01052f2d3f6d6b9bb6714ef32bd7b80177eb.tar.gz
  URL_HASH SHA256=955a378fac217f0e9a57e61d55d869fffa21f3217e361d9402c5251bf91e1faa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-asmjit-cmake)
FetchContent_MakeAvailable(asmjit)
FetchContent_Declare(blend2d
  URL https://github.com/blend2d/blend2d/archive/58ca9460b4138af6e793184682e9324e0a8053b2.tar.gz
  URL_HASH SHA256=b7887f581dc1340ce4c7198a4ccc96c79fd832fe099367db065c3b9ff5dfffd8
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
  SYSTEM)  # its headers must not trip our warning flags
set(ASMJIT_DIR "${asmjit_SOURCE_DIR}" CACHE PATH "" FORCE)
set(BLEND2D_STATIC ON CACHE BOOL "" FORCE)
set(BLEND2D_NO_INSTALL ON CACHE BOOL "" FORCE)
set(BLEND2D_TEST OFF CACHE BOOL "" FORCE)
set(BLEND2D_DEMOS OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN OR CAPRIOLE_SANITIZERS OR CAPRIOLE_BUILD_FUZZERS)
  set(BLEND2D_NO_JIT ON CACHE BOOL "" FORCE)
else()
  set(BLEND2D_NO_JIT OFF CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(blend2d)

if(CAPRIOLE_BUILD_TESTS)
  # GoogleTest 1.17.0 — BSD-3-Clause. Test-only, never linked into the engine.
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
