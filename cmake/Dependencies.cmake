# Third-party dependencies.
#
# Rule (DOCS/CAPRIOLE.md §4.2): only permissive licenses may be linked into the engine. Every new
# dependency is added here with a pinned version + SHA-256 and recorded in docs/DEPENDENCIES.md
# with its license, before any code uses it.

include(FetchContent)

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
