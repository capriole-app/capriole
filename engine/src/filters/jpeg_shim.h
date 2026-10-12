#ifndef CAPRIOLE_FILTERS_JPEG_SHIM_H
#define CAPRIOLE_FILTERS_JPEG_SHIM_H

/*
 * Internal to the engine: the one place that calls libjpeg (libjpeg-turbo, ADR 0003). libjpeg reports
 * fatal errors by longjmp, which must not cross C++ frames (destructors would be skipped): this C file
 * catches them and returns a status instead. Used by filters/dct.cpp only.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct capriole_jpeg_image { /* NOLINT(modernize-use-using,readability-identifier-naming): a C header, C names */
  unsigned char* samples;            /* rows top to bottom, components interleaved; release with capriole_jpeg_free */
  size_t size;
  int width;
  int height;
  int components; /* 1 (gray), 3 (RGB) or 4 (CMYK), as stored: no conversion between them */
} capriole_jpeg_image;

/*
 * Decodes data. color_transform: -1 to follow the file (JFIF / Adobe markers), 0 to take the components
 * as they are, 1 for YCbCr (3 components) or YCCK (4) to be converted (§7.4.8, /ColorTransform).
 * max_samples bounds width × height × components; the memory libjpeg may use is bounded from it too.
 * Returns 0 on success; otherwise *out is empty and message holds the reason.
 */
int capriole_jpeg_decode(const unsigned char* data, size_t size, int color_transform, size_t max_samples, capriole_jpeg_image* out, char* message,
                         size_t message_size);

void capriole_jpeg_free(capriole_jpeg_image* image);

#ifdef __cplusplus
}
#endif

#endif /* CAPRIOLE_FILTERS_JPEG_SHIM_H */
