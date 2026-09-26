add_executable(cpdf main.cpp)
target_link_libraries(cpdf PRIVATE capriole::engine)
capriole_set_warnings(cpdf)
