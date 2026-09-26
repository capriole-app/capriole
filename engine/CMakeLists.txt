add_library(capriole_engine STATIC
  src/core/byte_source.cpp
  src/core/error.cpp
  src/core/version.cpp
  src/parser/header.cpp)
add_library(capriole::engine ALIAS capriole_engine)

target_include_directories(capriole_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
target_compile_features(capriole_engine PUBLIC cxx_std_23)
capriole_set_warnings(capriole_engine)

configure_file(src/core/version_config.hpp.in include/capriole/core/version_config.hpp @ONLY)
