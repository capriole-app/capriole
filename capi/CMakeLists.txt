add_library(capriole_capi STATIC src/capriole_c.cpp src/json.cpp src/json_read.cpp)
add_library(capriole::capi ALIAS capriole_capi)

target_include_directories(capriole_capi PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(capriole_capi PRIVATE capriole::engine)
capriole_set_warnings(capriole_capi)
