add_executable(cpdf main.cpp)
# The C API too: `cpdf annot` goes through it, as the apps do.
target_link_libraries(cpdf PRIVATE capriole::engine capriole::capi)
capriole_set_warnings(cpdf)
