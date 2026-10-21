# Third-party dependencies.
#
# Rule (docs/CAPRIOLE.md §4.2): only permissive licenses may be linked into the engine. Every new
# dependency is added here with a pinned version + SHA-256 and recorded in docs/DEPENDENCIES.md
# with its license, before any code uses it.

include(FetchContent)

# Where each archive comes from: its URL, then any mirrors after it (CMake tries them in order) — or,
# with CAPRIOLE_DEPS_DIR set (builds without network, as OSS-Fuzz's: ci/oss-fuzz/fetch_deps.py fills
# it), the file of the same name there. The SHA-256 is checked either way.
function(capriole_dependency_url out url)
  if(CAPRIOLE_DEPS_DIR)
    get_filename_component(_name ${url} NAME)
    if(NOT EXISTS ${CAPRIOLE_DEPS_DIR}/${_name})
      message(FATAL_ERROR "CAPRIOLE_DEPS_DIR: ${CAPRIOLE_DEPS_DIR}/${_name} is missing (ci/oss-fuzz/fetch_deps.py)")
    endif()
    set(${out} ${CAPRIOLE_DEPS_DIR}/${_name} PARENT_SCOPE)
  else()
    set(${out} ${url} ${ARGN} PARENT_SCOPE)
  endif()
endfunction()

# zlib 1.3.1 — Zlib license. FlateDecode (and, later, compression in the writer).
# zlib's own CMakeLists is not used (global include_directories, a shared library, a CMake minimum
# that newer CMake rejects): SOURCE_SUBDIR points nowhere so nothing is added, and the library is
# built below from its sources. One pinned version on every platform, WASM included.
capriole_dependency_url(_url https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz)
FetchContent_Declare(zlib
  URL ${_url}
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
capriole_dependency_url(_url https://github.com/asmjit/asmjit/archive/0d7f01052f2d3f6d6b9bb6714ef32bd7b80177eb.tar.gz)
FetchContent_Declare(asmjit
  URL ${_url}
  URL_HASH SHA256=955a378fac217f0e9a57e61d55d869fffa21f3217e361d9402c5251bf91e1faa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-asmjit-cmake)
FetchContent_MakeAvailable(asmjit)
capriole_dependency_url(_url https://github.com/blend2d/blend2d/archive/58ca9460b4138af6e793184682e9324e0a8053b2.tar.gz)
FetchContent_Declare(blend2d
  URL ${_url}
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
# Blend2D builds without RTTI: the vptr check cannot apply. Implied by -fsanitize=undefined it is dropped
# quietly, but OSS-Fuzz's flags name it (ci/oss-fuzz/build.sh), which is an error with -fno-rtti.
if(CMAKE_CXX_FLAGS MATCHES "vptr")
  target_compile_options(blend2d PRIVATE -fno-sanitize=vptr)
endif()

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
if(CAPRIOLE_BUILD_FUZZERS AND CAPRIOLE_FUZZ_INSTRUMENT)
  string(APPEND _jpeg_c_flags " -fsanitize=fuzzer-no-link")
endif()
set(_jpeg_build_type "${CMAKE_BUILD_TYPE}")
if(NOT _jpeg_build_type)
  set(_jpeg_build_type Release)
endif()
set(_jpeg_library ${_jpeg_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
capriole_dependency_url(_url https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz)
ExternalProject_Add(libjpeg_turbo
  URL ${_url}
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
# (Under OSS-Fuzz the sanitizers come in the flags.)
if((CAPRIOLE_SANITIZERS OR CMAKE_C_FLAGS MATCHES "-fsanitize=") AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  string(APPEND _opj_c_flags " -fno-sanitize=function")
endif()
set(_opj_library ${_opj_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}openjp2${CMAKE_STATIC_LIBRARY_SUFFIX})
capriole_dependency_url(_url https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.4.tar.gz)
ExternalProject_Add(openjpeg
  URL ${_url}
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

# FreeType 2.14.3 — FreeType License (FTL, BSD-style; chosen over its GPLv2 alternative) (ADR 0005).
# Glyph outlines of the fonts in PDFs: TrueType/OpenType, Type 1, CFF, CID-keyed. Built from its sources
# like zlib (the tarball's GPG signature checked at the upgrade, the hash pinned here): only the font
# drivers PDF needs, no rasterizers or hinters, our options and module list in cmake/freetype/.
# The same tarball from FreeType's SourceForge release area first: savannah answered 504 to two release builds
# in a row (21/10), and the tag could not be built.
capriole_dependency_url(_url https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz
                        https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz)
FetchContent_Declare(freetype
  URL ${_url}
  URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-freetype-cmake)
FetchContent_MakeAvailable(freetype)
add_library(capriole_freetype STATIC
  ${freetype_SOURCE_DIR}/src/base/ftsystem.c
  ${freetype_SOURCE_DIR}/src/base/ftinit.c
  ${freetype_SOURCE_DIR}/src/base/ftdebug.c
  ${freetype_SOURCE_DIR}/src/base/ftbase.c
  ${freetype_SOURCE_DIR}/src/base/ftcid.c
  ${freetype_SOURCE_DIR}/src/base/fttype1.c
  ${freetype_SOURCE_DIR}/src/sfnt/sfnt.c
  ${freetype_SOURCE_DIR}/src/truetype/truetype.c
  ${freetype_SOURCE_DIR}/src/type1/type1.c
  ${freetype_SOURCE_DIR}/src/cff/cff.c
  ${freetype_SOURCE_DIR}/src/cid/type1cid.c
  ${freetype_SOURCE_DIR}/src/psaux/psaux.c
  ${freetype_SOURCE_DIR}/src/psnames/psnames.c)
add_library(capriole::freetype ALIAS capriole_freetype)
# Our options and modules first; SYSTEM: third-party headers must not trip our warning flags.
target_include_directories(capriole_freetype SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/freetype ${freetype_SOURCE_DIR}/include)
target_compile_definitions(capriole_freetype
  PUBLIC "FT_CONFIG_OPTIONS_H=<capriole_ftoption.h>" "FT_CONFIG_MODULES_H=<capriole_ftmodule.h>"
  PRIVATE FT2_BUILD_LIBRARY)
set_target_properties(capriole_freetype PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(CAPRIOLE_SANITIZERS)
  # FreeType copies zero bytes from a null pointer (memcpy(p, NULL, 0), an empty CFF index entry: found
  # by fuzz_font): undefined in C before C2y, harmless on every real libc, and its code. That one check
  # is off for FreeType alone; every other sanitizer stays on.
  target_compile_options(capriole_freetype PRIVATE -fno-sanitize=nonnull-attribute)
endif()

# Little CMS 2.19.1 — MIT (ADR 0006). ICC colour management: the profiles in PDFs (ICCBased, output
# intents), CalRGB/CalGray, and DeviceCMYK through a press profile. Built from its sources like zlib;
# its own mutexes stay on (pthreads; stubs in WebAssembly): hosts may render on several threads.
capriole_dependency_url(_url https://github.com/mm2/Little-CMS/releases/download/lcms2.19.1/lcms2-2.19.1.tar.gz)
FetchContent_Declare(lcms2
  URL ${_url}
  URL_HASH SHA256=bfc54f7bab59fbc921012014a8032e4cba4abd46db47d46b76416a8c0b2815c8
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-lcms2-build)
FetchContent_MakeAvailable(lcms2)
file(GLOB _lcms2_sources ${lcms2_SOURCE_DIR}/src/*.c)
add_library(capriole_lcms2 STATIC ${_lcms2_sources})
add_library(capriole::lcms2 ALIAS capriole_lcms2)
target_include_directories(capriole_lcms2 SYSTEM PUBLIC ${lcms2_SOURCE_DIR}/include PRIVATE ${lcms2_SOURCE_DIR}/src)
set_target_properties(capriole_lcms2 PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(capriole_lcms2 PUBLIC Threads::Threads)
endif()
# Like OpenJPEG, Little CMS calls its interpolation routines through pointers cast to a common signature
# (found by fuzz_icc): harmless on every real ABI, its design. Clang's -fsanitize=function is off for it
# alone; every other sanitizer stays on.
if((CAPRIOLE_SANITIZERS OR CMAKE_C_FLAGS MATCHES "-fsanitize=") AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(capriole_lcms2 PRIVATE -fno-sanitize=function)
endif()

# Mbed TLS 3.6.7 (LTS) — Apache-2.0 (dual-licensed Apache-2.0 OR GPL-2.0-or-later: taken under
# Apache-2.0). Digital signatures (PLAN.md phase 8h): X.509, RSA, ECDSA, hashes, the key encryption of
# PKCS#12 files. Built from its sources like zlib, with our configuration (cmake/mbedtls/): no TLS, no
# PSA, no entropy or clock of the platform. The CMS and PKCS#12 structures are the engine's own.
capriole_dependency_url(_url https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2)
FetchContent_Declare(mbedtls
  URL ${_url}
  URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-mbedtls-build)
FetchContent_MakeAvailable(mbedtls)
# Every module is guarded by its MBEDTLS_*_C option: those our configuration leaves out compile to nothing.
file(GLOB _mbedtls_sources ${mbedtls_SOURCE_DIR}/library/*.c)
add_library(capriole_mbedtls STATIC ${_mbedtls_sources})
add_library(capriole::mbedtls ALIAS capriole_mbedtls)
target_include_directories(capriole_mbedtls SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/mbedtls ${mbedtls_SOURCE_DIR}/include
  PRIVATE ${mbedtls_SOURCE_DIR}/library)
target_compile_definitions(capriole_mbedtls PUBLIC "MBEDTLS_CONFIG_FILE=<capriole_mbedtls_config.h>")
set_target_properties(capriole_mbedtls PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# The standard 14 fonts' substitutes (ADR 0005): the Foxit fonts — BSD-3-Clause (the PDFium authors),
# CFF programs with the metrics of Adobe's core fonts — taken from the pdfjs-dist 2.16.105 package on
# npm, the last one that carries all fourteen (with its LICENSE_FOXIT). Only data: built into the engine
# by engine/CMakeLists.txt (cmake/EmbedFiles.cmake); nothing of pdf.js is used.
capriole_dependency_url(_url https://registry.npmjs.org/pdfjs-dist/-/pdfjs-dist-2.16.105.tgz)
FetchContent_Declare(foxit_fonts
  URL ${_url}
  URL_HASH SHA256=b14d705aa185c548af0381d6835fc4f917451985a6358c2dae0ba8dd01355141
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add-pdfjs)
FetchContent_MakeAvailable(foxit_fonts)
set(CAPRIOLE_FOXIT_FONTS_DIR ${foxit_fonts_SOURCE_DIR}/standard_fonts CACHE INTERNAL "")

if(CAPRIOLE_BUILD_TESTS)
  # GoogleTest 1.17.0 — BSD-3-Clause. Test-only, never linked into the engine.
  capriole_dependency_url(_url https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz)
  FetchContent_Declare(googletest
    URL ${_url}
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
