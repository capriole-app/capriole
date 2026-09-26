/*
 * Capriole C API — the stable boundary of the engine.
 *
 * Everything outside the engine (WASM bindings, the desktop shell via FFI, third-party embedders)
 * talks to it through this header only. Rules:
 *   - plain C, no C++ types, no exceptions crossing the boundary;
 *   - opaque handles, created and destroyed by paired functions;
 *   - every fallible call returns capriole_status; details via capriole_last_error_message();
 *   - additions only: removing or changing a function bumps CAPRIOLE_ABI_VERSION.
 */
#ifndef CAPRIOLE_CAPRIOLE_H
#define CAPRIOLE_CAPRIOLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPRIOLE_ABI_VERSION 0

typedef enum capriole_status {
  CAPRIOLE_OK = 0,
  CAPRIOLE_ERROR_IO = 1,
  CAPRIOLE_ERROR_OUT_OF_RANGE = 2,
  CAPRIOLE_ERROR_UNEXPECTED_EOF = 3,
  CAPRIOLE_ERROR_SYNTAX = 4,
  CAPRIOLE_ERROR_UNSUPPORTED = 5,
  CAPRIOLE_ERROR_LIMIT_EXCEEDED = 6,
  CAPRIOLE_ERROR_CORRUPT_DATA = 7,
  CAPRIOLE_ERROR_PASSWORD_REQUIRED = 8,
  CAPRIOLE_ERROR_INVALID_ARGUMENT = 100,
  CAPRIOLE_ERROR_INTERNAL = 101
} capriole_status;

/* Engine version as "major.minor.patch". The string is static; do not free it. */
const char* capriole_version(void);

/* ABI version this library was built with; compare with CAPRIOLE_ABI_VERSION. */
int capriole_abi_version(void);

/*
 * Message describing the last failed call on this thread, or "" if none. Valid until the next
 * API call on the same thread; do not free it.
 */
const char* capriole_last_error_message(void);

/*
 * Reads the PDF header of an in-memory file. On success writes the version to *major / *minor.
 * data may be NULL only when size is 0.
 */
capriole_status capriole_probe_header(const uint8_t* data, size_t size, int* major, int* minor);

#ifdef __cplusplus
}
#endif

#endif /* CAPRIOLE_CAPRIOLE_H */
