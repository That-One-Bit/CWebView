/* cwv_bmp.h - portable BMP (Windows/DIB) decoder for the shared engine.
 *
 * Why this exists (2026-10-01): every other platform decodes BMP images
 * natively (libvita2d BMP, SDL2_image BMP on Wii U, WIC on Windows), but
 * GRRLIB on Wii only sniffs PNG/JPEG, so cwv_tex_load_bmp_buffer() there
 * silently failed for real BMP files. load_image() picks the decoder from
 * the file's magic bytes, so a "BM" file on Wii decoded to nothing.
 *
 * This decoder covers what web assets actually use: uncompressed
 * bottom-up or top-down DIBs at 24 or 32 bits per pixel (BI_RGB), plus
 * 32bpp BI_BITFIELDS with the standard {8,8,8,0}/{8,8,8,8} masks.
 * RLE, palettized, and JPEG/PNG-in-BMP (BITMAPV5) files are rejected
 * with a negative code - callers log the reason, exactly like the PNG
 * preflight. Output is tightly packed RGBA8888, row 0 = top row.
 *
 * All of it is plain C99 with no dependencies, so the desktop unit gate
 * (desktop_test/bmp_unit.c) runs the real decoder on both.
 */
#ifndef CWV_BMP_H
#define CWV_BMP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 when the buffer starts with a BMP ("BM") file signature. */
int cwv_bmp_is(const unsigned char *buf, size_t len);

/* Decode `buf` into a freshly malloc'd RGBA8888 image. On success
 * returns 0, stores the pixel array (free() it), width and height.
 * On failure returns a negative code and leaves the outputs untouched:
 *   -1 bad signature / truncated file
 *   -2 unsupported header (planes, bpp, compression, size)
 *   -3 out of memory
 */
int cwv_bmp_decode(const unsigned char *buf, size_t len,
                   unsigned char **rgba_out, int *w_out, int *h_out);

#ifdef __cplusplus
}
#endif

#endif /* CWV_BMP_H */
