#include "jpeg_shim.h"

#include <setjmp.h>
#include <stdio.h> /* jpeglib.h needs FILE: before it */
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include <jpeglib.h>
#include <jerror.h>
/* clang-format on */

/* A progressive file can hold any number of scans, each a pass over the whole image: bounded. */
#define CAPRIOLE_JPEG_MAX_SCANS 1000

typedef struct error_manager {
  struct jpeg_error_mgr base;
  jmp_buf jump;
  char message[JMSG_LENGTH_MAX];
} error_manager;

static void on_error(j_common_ptr cinfo) {
  error_manager* errors = (error_manager*)cinfo->err;
  (*cinfo->err->format_message)(cinfo, errors->message);
  longjmp(errors->jump, 1);
}

/* Warnings (corrupt or truncated data) are not fatal: viewers show what decodes. */
static void on_message(j_common_ptr cinfo, int level) {
  (void)cinfo;
  (void)level;
}

static void on_progress(j_common_ptr cinfo) {
  if (cinfo->is_decompressor) {
    const j_decompress_ptr d = (j_decompress_ptr)cinfo;
    if (d->input_scan_number > CAPRIOLE_JPEG_MAX_SCANS) {
      error_manager* errors = (error_manager*)cinfo->err;
      snprintf(errors->message, sizeof errors->message, "more than %d scans", CAPRIOLE_JPEG_MAX_SCANS);
      longjmp(errors->jump, 1);
    }
  }
}

static void set_message(char* message, size_t message_size, const char* text) {
  if (message != NULL && message_size > 0) {
    snprintf(message, message_size, "%s", text);
  }
}

int capriole_jpeg_decode(const unsigned char* data, size_t size, int color_transform, size_t max_samples, capriole_jpeg_image* out, char* message,
                         size_t message_size) {
  /* volatile: modified between setjmp and a longjmp back to it. */
  unsigned char* volatile samples = NULL;
  struct jpeg_decompress_struct cinfo;
  error_manager errors;
  struct jpeg_progress_mgr progress;

  memset(out, 0, sizeof *out);
  set_message(message, message_size, "");
  if (size == 0 || size > (size_t)(unsigned long)-1) {
    set_message(message, message_size, "empty or oversized JPEG data");
    return 1;
  }
  memset(&cinfo, 0, sizeof cinfo);
  cinfo.err = jpeg_std_error(&errors.base);
  errors.base.error_exit = on_error;
  errors.base.emit_message = on_message;
  errors.message[0] = '\0';
  if (setjmp(errors.jump) != 0) {
    free(samples);
    jpeg_destroy_decompress(&cinfo);
    set_message(message, message_size, errors.message);
    return 1;
  }
  jpeg_create_decompress(&cinfo);
  /* libjpeg allocates no more than this (no temporary files: past it, an error). */
  cinfo.mem->max_memory_to_use = (long)(max_samples > 0x10000000u ? 0x40000000u : max_samples * 4u + 0x1000000u);
  progress.progress_monitor = on_progress;
  cinfo.progress = &progress;
  jpeg_mem_src(&cinfo, data, (unsigned long)size);
  jpeg_read_header(&cinfo, TRUE);

  const int components = cinfo.num_components;
  if (components != 1 && components != 3 && components != 4) {
    set_message(message, message_size, "JPEG with an unsupported number of components");
    jpeg_destroy_decompress(&cinfo);
    return 1;
  }
  if (cinfo.image_width == 0 || cinfo.image_height == 0 || (size_t)cinfo.image_width > max_samples / cinfo.image_height / (size_t)components) {
    set_message(message, message_size, "JPEG image too large");
    jpeg_destroy_decompress(&cinfo);
    return 1;
  }
  if (components == 3 && color_transform >= 0) {
    cinfo.jpeg_color_space = color_transform != 0 ? JCS_YCbCr : JCS_RGB;
  } else if (components == 4 && color_transform >= 0) {
    cinfo.jpeg_color_space = color_transform != 0 ? JCS_YCCK : JCS_CMYK;
  }
  cinfo.out_color_space = JCS_CMYK;
  if (components == 1) {
    cinfo.out_color_space = JCS_GRAYSCALE;
  } else if (components == 3) {
    cinfo.out_color_space = JCS_RGB;
  }
  jpeg_start_decompress(&cinfo);

  const size_t stride = (size_t)cinfo.output_width * (size_t)cinfo.output_components;
  const size_t total = stride * (size_t)cinfo.output_height;
  samples = (unsigned char*)malloc(total > 0 ? total : 1);
  if (samples == NULL) {
    set_message(message, message_size, "out of memory for the JPEG image");
    jpeg_destroy_decompress(&cinfo);
    return 1;
  }
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row = samples + ((size_t)cinfo.output_scanline * stride);
    if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
      break;
    }
  }
  /* Rows never produced (a broken file libjpeg gave up on) read as 0. */
  if (cinfo.output_scanline < cinfo.output_height) {
    memset(samples + ((size_t)cinfo.output_scanline * stride), 0, total - ((size_t)cinfo.output_scanline * stride));
  }
  jpeg_abort_decompress(&cinfo);
  out->width = (int)cinfo.output_width;
  out->height = (int)cinfo.output_height;
  out->components = cinfo.output_components;
  out->samples = samples;
  out->size = total;
  jpeg_destroy_decompress(&cinfo);
  return 0;
}

void capriole_jpeg_free(capriole_jpeg_image* image) {
  if (image != NULL) {
    free(image->samples);
    image->samples = NULL;
    image->size = 0;
  }
}
