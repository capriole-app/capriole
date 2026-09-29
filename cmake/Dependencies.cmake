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

# libjpeg-turbo 3.2.0 — IJG + BSD-3-Clause (ADR 0003). DCTDecode: JPEG images.
# Its build refuses add_subdirectory() and asks for ExternalProject_Add(): it is built on its own, with
# our compiler, toolchain (Emscripten) and sanitizer/fuzzer flags, and imported as capriole::jpeg.
# Without SIMD everywhere (no NASM needed, and the same pixels natively and in WebAssembly); 8-bit
# samples, arithmetic decoding on, no TurboJPEG API, tools or tests.
include(ExternalProject)
set(_jpeg_prefix ${CMAKE_BINARY_DIR}/_deps/libjpeg-turbo)
set(_jpeg_c_flags "${CMAKE_C_FLAGS}")
if(CAPRIOLE_SANITIZERS)
  list(JOIN CAPRIOLE_SANITIZERS "," _jpeg_sanitizers)
  string(APPEND _jpeg_c_flags " -fsanitize=${_jpeg_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all")
endif()
if(CAPRIOLE_BUILD_FUZZERS)
  string(APPEND _jpeg_c_flags " -fsanitize=fuzzer-no-link")
endif()
set(_jpeg_build_type "${CMAKE_BUILD_TYPE}")
if(NOT _jpeg_build_type)
  set(_jpeg_build_type Release)
endif()
set(_jpeg_library ${_jpeg_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
ExternalProject_Add(libjpeg_turbo
  URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz
  URL_HASH SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  PREFIX ${_jpeg_prefix}/build
  INSTALL_DIR ${_jpeg_prefix}
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_INSTALL_INCLUDEDIR=include
    -DCMAKE_BUILD_TYPE=${_jpeg_build_type}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_C_FLAGS=${_jpeg_c_flags}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_SIMD=OFF
    -DWITH_ARITH_DEC=ON
    -DWITH_ARITH_ENC=OFF
    -DWITH_TURBOJPEG=OFF
    -DWITH_TOOLS=OFF
    -DWITH_TESTS=OFF
    -DWITH_FUZZ=OFF
  BUILD_BYPRODUCTS ${_jpeg_library})
file(MAKE_DIRECTORY ${_jpeg_prefix}/include)
add_library(capriole_jpeg STATIC IMPORTED GLOBAL)
set_target_properties(capriole_jpeg PROPERTIES IMPORTED_LOCATION ${_jpeg_library})
# SYSTEM: third-party headers must not trip our warning flags.
target_include_directories(capriole_jpeg SYSTEM INTERFACE ${_jpeg_prefix}/include)
add_dependencies(capriole_jpeg libjpeg_turbo)
add_library(capriole::jpeg ALIAS capriole_jpeg)

# OpenJPEG 2.5.4 — BSD-2-Clause (ADR 0004). JPXDecode: JPEG 2000 images.
# Built like libjpeg-turbo (ExternalProject, the same compiler, toolchain and flags), static library
# only, one thread (deterministic, and the same in WebAssembly).
set(_opj_prefix ${CMAKE_BINARY_DIR}/_deps/openjpeg)
# OpenJPEG calls its codec functions through pointers cast to a common signature (a void* first
# parameter): undefined in C, harmless on every real ABI, and its design. Clang's -fsanitize=function
# flags each call, so it is off for OpenJPEG alone; every other sanitizer stays on.
set(_opj_c_flags "${_jpeg_c_flags}")
if(CAPRIOLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  string(APPEND _opj_c_flags " -fno-sanitize=function")
endif()
set(_opj_library ${_opj_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}openjp2${CMAKE_STATIC_LIBRARY_SUFFIX})
ExternalProject_Add(openjpeg
  URL https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.4.tar.gz
  URL_HASH SHA256=a695fbe19c0165f295a8531b1e4e855cd94d0875d2f88ec4b61080677e27188a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  PREFIX ${_opj_prefix}/build
  INSTALL_DIR ${_opj_prefix}
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_INSTALL_INCLUDEDIR=include
    -DCMAKE_BUILD_TYPE=${_jpeg_build_type}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_C_FLAGS=${_opj_c_flags}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_CODEC=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_DOC=OFF
    -DBUILD_THIRDPARTY=OFF
    -DBUILD_PKGCONFIG_FILES=OFF
    -DOPJ_USE_THREAD=OFF
  BUILD_BYPRODUCTS ${_opj_library})
file(MAKE_DIRECTORY ${_opj_prefix}/include/openjpeg-2.5)
add_library(capriole_openjpeg STATIC IMPORTED GLOBAL)
set_target_properties(capriole_openjpeg PROPERTIES IMPORTED_LOCATION ${_opj_library})
target_include_directories(capriole_openjpeg SYSTEM INTERFACE ${_opj_prefix}/include/openjpeg-2.5)
target_compile_definitions(capriole_openjpeg INTERFACE OPJ_STATIC)
add_dependencies(capriole_openjpeg openjpeg)
add_library(capriole::openjpeg ALIAS capriole_openjpeg)

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
