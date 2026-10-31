/* cwv_bmp.c - see cwv_bmp.h. Little-endian DIB fields are read byte by
 * byte so the decoder is endian-clean on every console CPU. */
#include <stdlib.h>
#include <string.h>
#include "cwv_bmp.h"

static unsigned bmp_le32(const unsigned char *p) {
    return ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) |
           ((unsigned)p[1] << 8) | (unsigned)p[0];
}
static unsigned bmp_le16(const unsigned char *p) {
    return ((unsigned)p[1] << 8) | (unsigned)p[0];
}

int cwv_bmp_is(const unsigned char *buf, size_t len) {
    return buf && len >= 2 && buf[0] == 'B' && buf[1] == 'M';
}

int cwv_bmp_decode(const unsigned char *buf, size_t len,
                   unsigned char **rgba_out, int *w_out, int *h_out) {
    unsigned data_off, hdr_size, planes, bpp, compression;
    int width, height, top_down, y, x;
    size_t row_bytes;
    unsigned char *out;

    /* Callers may probe with NULL outputs; never dereference them. */
    if (!rgba_out || !w_out || !h_out) return -1;
    if (!cwv_bmp_is(buf, len) || len < 54) return -1;
    data_off = bmp_le32(buf + 10);
    hdr_size = bmp_le32(buf + 14);
    if (data_off < 14 + 12 || data_off > len) return -1;
    /* BITMAPINFOHEADER (40) or the V4/V5 superset: same leading fields. */
    if (hdr_size < 40 || 14 + (size_t)hdr_size > len) return -1;

    width = (int)bmp_le32(buf + 18);
    height = (int)bmp_le32(buf + 22);
    top_down = height < 0;
    if (top_down) height = -height;
    planes = bmp_le16(buf + 26);
    bpp = bmp_le16(buf + 28);
    compression = bmp_le32(buf + 30);

    if (width <= 0 || height <= 0 || planes != 1) return -2;
    if (bpp != 24 && bpp != 32) return -2;              /* no palettes */
    if (compression == 3) {                             /* BI_BITFIELDS */
        /* Standard masks only (see the header comment): the three DWORDs
         * live right after the 40-byte header for BITMAPINFOHEADER. */
        unsigned mr, mg, mb;
        if (hdr_size < 52 || 14 + 52 > len) return -2;
        mr = bmp_le32(buf + 14 + 40);
        mg = bmp_le32(buf + 14 + 44);
        mb = bmp_le32(buf + 14 + 48);
        if (bpp != 32 || mr != 0x00FF0000u || mg != 0x0000FF00u ||
            mb != 0x000000FFu)
            return -2;
        compression = 0;                                /* treat as BI_RGB */
    }
    if (compression != 0) return -2;                   /* no RLE/etc */

    row_bytes = ((size_t)width * (bpp / 8u) + 3u) & ~(size_t)3u;
    if ((size_t)data_off + row_bytes * (size_t)height > len) return -1;

    out = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!out) return -3;

    for (y = 0; y < height; y++) {
        const unsigned char *src = buf + data_off +
            row_bytes * (size_t)(top_down ? y : (height - 1 - y));
        unsigned char *dst = out + 4u * (size_t)y * (size_t)width;
        for (x = 0; x < width; x++) {
            const unsigned char *px = src + (size_t)x * (bpp / 8u);
            dst[0] = px[2];                             /* R */
            dst[1] = px[1];                             /* G */
            dst[2] = px[0];                             /* B */
            dst[3] = (bpp == 32) ? px[3] : 255;         /* A */
            dst += 4;
        }
    }
    *rgba_out = out;
    *w_out = width;
    *h_out = height;
    return 0;
}
