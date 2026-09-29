/* FreeType's options for Capriole (ADR 0005): its stock ftoption.h, then off what a PDF renderer that
 * only reads outlines does not need — less code in the engine and in WebAssembly, less attack surface.
 * Selected with FT_CONFIG_OPTIONS_H (see cmake/Dependencies.cmake). */
#ifndef CAPRIOLE_FTOPTION_H_
#define CAPRIOLE_FTOPTION_H_

#include <freetype/config/ftoption.h>

/* Same result whatever the environment: no FREETYPE_PROPERTIES. */
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
/* Compressed font files (.gz, .Z, .bz2, WOFF) do not occur in PDFs, nor do Mac resource forks. */
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
/* Outlines only: no embedded bitmaps, colour layers or SVG glyphs. */
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_EMBEDDED_BITMAPS
#undef TT_CONFIG_OPTION_COLOR_LAYERS
#undef TT_SUPPORT_COLRV1
#undef TT_CONFIG_OPTION_BDF
/* Unhinted outlines (glyphs are drawn at any scale and angle by Blend2D): the TrueType bytecode
 * interpreter — the largest part of FreeType exposed to hostile programs — is not compiled. */
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#undef TT_CONFIG_OPTION_SUBPIXEL_HINTING
#undef TT_USE_BYTECODE_INTERPRETER
#undef TT_SUPPORT_SUBPIXEL_HINTING_MINIMAL
/* Variable fonts: PDF embeds a font's instance, not the variations. */
#undef TT_CONFIG_OPTION_GX_VAR_SUPPORT

#endif /* CAPRIOLE_FTOPTION_H_ */
