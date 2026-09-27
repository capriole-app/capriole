# Phase 6c prototype (PLAN.md): Skia and Blend2D behind one interface. Not part of the engine or of the
# gate: configure with -DCAPRIOLE_RASTER_PROTO=ON -DCAPRIOLE_THIRD_PARTY=<dir with blend2d, asmjit,
# skia-linux> (fetched outside the repository; see docs/adr/0002-rasterizzatore.md).

set(CAPRIOLE_THIRD_PARTY "${CMAKE_SOURCE_DIR}/../THIRD_PARTY" CACHE PATH "Third-party sources for the rasterizer prototype")

# Blend2D (zlib licence), static, with its bundled AsmJit (JIT pipelines) found next to it.
set(BLEND2D_STATIC ON CACHE BOOL "" FORCE)
set(BLEND2D_NO_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(${CAPRIOLE_THIRD_PARTY}/blend2d ${CMAKE_BINARY_DIR}/third_party/blend2d EXCLUDE_FROM_ALL)

# Skia (BSD-3-Clause), prebuilt by JetBrains skia-pack m144 (Linux x64, or WebAssembly).
if(EMSCRIPTEN)
  set(_skia ${CAPRIOLE_THIRD_PARTY}/skia-wasm)
  set(_skia_out ${_skia}/out/Release-wasm-wasm)
  set(_skia_system "")
else()
  set(_skia ${CAPRIOLE_THIRD_PARTY}/skia-linux)
  set(_skia_out ${_skia}/out/Release-linux-x64)
  set(_skia_system pthread dl)
endif()
add_library(skia_prebuilt INTERFACE)
target_include_directories(skia_prebuilt SYSTEM INTERFACE ${_skia})
target_link_libraries(skia_prebuilt INTERFACE ${_skia_out}/libskia.a ${_skia_out}/libskcms.a ${_skia_system})

# Both backends (native comparisons), and one backend each (the size each one adds).
add_executable(raster_bench raster_bench.cpp skia_backend.cpp blend2d_backend.cpp)
target_link_libraries(raster_bench PRIVATE capriole::engine capriole::zlib blend2d::blend2d skia_prebuilt)
add_executable(raster_bench_skia raster_bench.cpp skia_backend.cpp)
target_compile_definitions(raster_bench_skia PRIVATE RASTER_WITHOUT_BLEND2D)
target_link_libraries(raster_bench_skia PRIVATE capriole::engine capriole::zlib skia_prebuilt)
add_executable(raster_bench_blend2d raster_bench.cpp blend2d_backend.cpp)
target_compile_definitions(raster_bench_blend2d PRIVATE RASTER_WITHOUT_SKIA)
target_link_libraries(raster_bench_blend2d PRIVATE capriole::engine capriole::zlib blend2d::blend2d)
add_executable(raster_bench_none raster_bench.cpp)
target_compile_definitions(raster_bench_none PRIVATE RASTER_WITHOUT_SKIA RASTER_WITHOUT_BLEND2D)
target_link_libraries(raster_bench_none PRIVATE capriole::engine capriole::zlib)
if(EMSCRIPTEN)
  # Node reads the PDFs straight from disk; memory grows with the page.
  foreach(target raster_bench raster_bench_skia raster_bench_blend2d raster_bench_none)
    target_link_options(${target} PRIVATE "-sNODERAWFS=1" "-sALLOW_MEMORY_GROWTH=1" "-sMAXIMUM_MEMORY=4GB" "-sENVIRONMENT=node" "-sSTACK_SIZE=4MB" "-O3")
  endforeach()
endif()
