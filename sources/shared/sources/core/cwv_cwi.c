/* CWI decoder (see cwv_cwi.h for the format).  The whole decoder is
 * ~90 lines of plain C89/C99: the consoles gain fast lossless loads
 * with zero platform code.  The unpack round trip lives in the Python
 * tools; this is the runtime fast path. */
#include "core/cwv_cwi.h"
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int cwv_cwi_decode(const unsigned char *buf, size_t len,
                   unsigned char **rgba_out, int *w_out, int *h_out) {
    const unsigned char *p, *end;
    unsigned width, height, hdrlen, version, fmt;
    size_t need, total_px, got_px;
    unsigned char *out;

    if (!buf || len < 16 || !rgba_out || !w_out || !h_out) return 0;
    if (buf[0] != 'C' || buf[1] != 'W' || buf[2] != 'I' || buf[3] != 'F')
        return 0;
    version = rd16(buf + 4);
    fmt     = rd16(buf + 6);
    width   = rd16(buf + 8);
    height  = rd16(buf + 10);
    hdrlen  = rd32(buf + 12);
    if (version != 1 || fmt != 0 || hdrlen < 16 || hdrlen > len)
        return 0;
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
        return 0;
    /* 4 bytes per pixel, guarded against overflow in size_t */
    need = (size_t)width * height;
    if (need > (size_t)-1 / 4) return 0;
    out = (unsigned char *)malloc(need * 4);
    if (!out) return 0;

    total_px = need;
    got_px = 0;
    p = buf + hdrlen;
    end = buf + len;
    while (p + 8 <= end) {
        unsigned fourcc = rd32(p);
        unsigned clen = rd32(p + 4);
        const unsigned char *cdata = p + 8;
        if ((size_t)(end - cdata) < clen) { free(out); return 0; }
        if (fourcc == 0x4C525850u) {                 /* "PXRL" LE */
            const unsigned char *q = cdata, *qend = cdata + clen;
            while (q < qend && got_px < total_px) {
                unsigned token = *q++;
                unsigned count = (token & 0x7Fu) + 1u;
                if (token & 0x80u) {                 /* raw: count pixels */
                    if (got_px + count > total_px ||
                        (size_t)(qend - q) < (size_t)count * 4u) {
                        free(out); return 0;
                    }
                    memcpy(out + got_px * 4, q, (size_t)count * 4u);
                    q += count * 4u;
                    got_px += count;
                } else {                             /* run: 1 pixel, count times */
                    unsigned i;
                    if (q + 4 > qend || got_px + count > total_px) {
                        free(out); return 0;
                    }
                    for (i = 0; i < count; i++)
                        memcpy(out + (got_px + i) * 4u, q, 4u);
                    q += 4;
                    got_px += count;
                }
            }
            if (got_px != total_px) { free(out); return 0; }
        }
        /* ORIG / META / unknown chunks: skipped (ORIG is only for the
           unpack tool's byte-exact round trip). */
        p = cdata + clen;
    }
    /* Strict integrity (fuzz-found 2026-10-05 (71)): the chunk list must
       end EXACTLY at the end of the buffer.  A file cut inside a trailing
       chunk is corruption even when the pixels happened to decode -
       "zero data loss" means rejecting it, not rendering it. */
    if (p != end) { free(out); return 0; }
    if (got_px != total_px) { free(out); return 0; }
    *rgba_out = out;
    *w_out = (int)width;
    *h_out = (int)height;
    return 1;
}
