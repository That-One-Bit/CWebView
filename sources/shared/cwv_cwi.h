/* CWI - CWebView Image (2026-10-05 (71)).
 *
 * A LOSSLESS native image format for consoles: no inflate, no PNG
 * filters, one tiny shared decoder, and every decoded RGBA byte is
 * identical to the source image's pixels (David's zero-data-loss
 * requirement, 2026-10-05).  Files are produced by tools/cwi_pack.py,
 * which also embeds the ORIGINAL source file (deflate-compressed) in an
 * ORIG chunk so tools/cwi_unpack.py can restore it byte-for-byte.
 *
 * Container layout (all little-endian):
 *   Header (16 bytes):
 *     0  "CWIF"           magic
 *     4  u16 version (1)
 *     6  u16 pixel format (0 = RGBA8 interleaved)
 *     8  u16 width
 *    10  u16 height
 *    12  u32 header size (16)
 *   Chunks, concatenated until end of file:
 *     u32 fourcc ("PXRL", "ORIG", "META"), u32 data length, data bytes
 *   PXRL (mandatory): RLE pixel stream, decoded until w*h pixels:
 *     u8 token: bit7 = mode (0 run / 1 raw), bits6..0 = count - 1
 *     run:  4 bytes RGBA, repeated `count` times
 *     raw:  `count` * 4 bytes, distinct pixels
 *   ORIG (optional): zlib-deflated ORIGINAL source file, exact bytes
 *   META (optional): ASCII "src=<name>" text
 *
 * The ENGINE decodes only the header + PXRL (fast path, no inflate);
 * ORIG/META are skipped.  The unpack tool uses ORIG for the lossless
 * round trip.  Accepted everywhere an image source is: <img>, CSS
 * url(), cursor url(), and data: URIs (data:image/x-cwi;base64,...).
 */
#ifndef CWV_CWI_H
#define CWV_CWI_H
#include <stddef.h>
#include "cwv_platform.h"

/* Is this buffer a CWI container? (bounds-checked magic sniff) */
static inline int cwv_cwi_is(const unsigned char *buf, size_t len) {
    return buf && len >= 16 && buf[0] == 'C' && buf[1] == 'W' &&
           buf[2] == 'I' && buf[3] == 'F';
}

/* Decode a CWI buffer to a fresh malloc'd RGBA8 buffer (w*h*4 bytes,
 * R,G,B,A interleaved, top-down - the same layout cwv_bmp_decode
 * produces, matching cwv_bmp_decode's out-parameter types).  Returns 1
 * on success, 0 on any malformed input; *rgba_out
 * must be free()d by the caller.  Never reads out of bounds, and the
 * decoded pixel count must cover the image exactly - a truncated or
 * over-long stream is rejected, not truncated silently. */
int cwv_cwi_decode(const unsigned char *buf, size_t len,
                   unsigned char **rgba_out, int *w_out, int *h_out);

#endif /* CWV_CWI_H */
