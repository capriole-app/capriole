# Sanitizers apply to the whole build (including GoogleTest), otherwise ASan container checks
# and UBSan vptr checks produce false positives across the boundary.

if(CAPRIOLE_SANITIZERS)
  list(JOIN CAPRIOLE_SANITIZERS "," _capriole_sanitizers)
  add_compile_options(-fsanitize=${_capriole_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${_capriole_sanitizers})
  message(STATUS "Capriole: sanitizers enabled: ${_capriole_sanitizers}")
endif()
