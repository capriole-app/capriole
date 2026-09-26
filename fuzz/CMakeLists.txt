# libFuzzer targets. Built only with the 'fuzz' preset (Clang, ASan + UBSan).
#
#   cmake --preset fuzz && cmake --build --preset fuzz
#   ./build/fuzz/fuzz/fuzz_header -max_total_time=60 fuzz/corpus/header
#
# Every target takes the whole input as a file and must never crash, leak or hit UB, whatever the bytes.

function(capriole_add_fuzzer name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE capriole::engine)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  capriole_set_warnings(${name})
endfunction()

capriole_add_fuzzer(fuzz_header fuzz_header.cpp)
capriole_add_fuzzer(fuzz_lexer fuzz_lexer.cpp)
capriole_add_fuzzer(fuzz_object fuzz_object.cpp)
capriole_add_fuzzer(fuzz_xref fuzz_xref.cpp)
capriole_add_fuzzer(fuzz_filters fuzz_filters.cpp)
