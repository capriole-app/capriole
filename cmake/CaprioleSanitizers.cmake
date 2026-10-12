# Sanitizers apply to the whole build (including GoogleTest), otherwise ASan container checks
# and UBSan vptr checks produce false positives across the boundary.

if(CAPRIOLE_SANITIZERS)
  list(JOIN CAPRIOLE_SANITIZERS "," _capriole_sanitizers)
  add_compile_options(-fsanitize=${_capriole_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${_capriole_sanitizers})
  message(STATUS "Capriole: sanitizers enabled: ${_capriole_sanitizers}")
endif()

# Fuzz builds: every library must carry libFuzzer's coverage instrumentation, otherwise the fuzzer
# sees no edges in the code under test and degrades to blind random input. Only the fuzz targets
# themselves link the libFuzzer runtime (-fsanitize=fuzzer, in fuzz/CMakeLists.txt).
if(CAPRIOLE_BUILD_FUZZERS AND CAPRIOLE_FUZZ_INSTRUMENT)
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()
