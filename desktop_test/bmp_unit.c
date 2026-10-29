/* BMP decoder unit gate (2026-10-01): the shared cwv_bmp decoder is what
 * the Wii platform now uses for BMP images (GRRLIB only sniffs PNG/JPEG).
 * Builds real BMP bytes by hand and checks pixels, orientation, padding,
 * alpha and every rejection path. Exit 0 = all pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cwv_bmp.h"

static int fails;
static void check(int cond, const char *what) {
    printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}

/* Hand-build an uncompressed DIB. bpp = 24 or 32; top_down flips the
 * height sign. Pixel bytes arrive in DIB order (B,G,R[,A]). */
static size_t make_bmp(unsigned char **out, int w, int h, int bpp,
                       int top_down, const unsigned char *rows) {
    size_t row = ((size_t)w * (size_t)(bpp / 8) + 3) & ~(size_t)3;
    size_t data = row * (size_t)(h < 0 ? -h : h);
    size_t total = 14 + 40 + data;
    unsigned char *b = (unsigned char *)calloc(1, total);
    b[0] = 'B'; b[1] = 'M';
    unsigned long ih = (unsigned long)(top_down ? -h : h);
    b[10] = (unsigned char)(14 + 40);            /* pixel data offset */
    b[14] = 40;                                  /* BITMAPINFOHEADER */
    b[18] = (unsigned char)w; b[19] = (unsigned char)(w >> 8);
    /* height is a full 4-byte signed field: a 2-byte write turns -1
     * into 65535 and the decoder rejects the file. */
    b[22] = (unsigned char)ih; b[23] = (unsigned char)(ih >> 8);
    b[24] = (unsigned char)(ih >> 16); b[25] = (unsigned char)(ih >> 24);
    b[26] = 1;                                   /* planes */
    b[28] = (unsigned char)bpp;                  /* bits per pixel */
    memcpy(b + 14 + 40, rows, data);
    *out = b;
    return total;
}

int main(void) {
    unsigned char *buf = NULL;
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    size_t n;

    /* 2x1 bottom-up 24bpp: first stored row is the BOTTOM row.
     * Red on top, green on the bottom. One pad byte per row. */
    {
        /* DIB rows pad to 4 bytes: 2px * 3B = 6B + 2 pad per row. */
        unsigned char rows[] = { 0,255,0, 0,0,0,0,0,   /* bottom: green (6B + 2 pad) */
                                 0,0,255, 0,0,0,0,0 }; /* top:    red   */
        n = make_bmp(&buf, 2, 2, 24, 0, rows);
        check(cwv_bmp_is(buf, n), "signature detected");
        check(cwv_bmp_decode(buf, n, &rgba, &w, &h) == 0 && w == 2 && h == 2,
              "24bpp bottom-up decodes");
        if (rgba) {
            check(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255,
                  "top-left pixel is red, opaque");
            check(rgba[8] == 0 && rgba[9] == 255 && rgba[10] == 0,
                  "bottom-left pixel is green (orientation respected)");
            free(rgba); rgba = NULL;
        }
        free(buf); buf = NULL;
    }

    /* 2x1 top-down 32bpp with a real alpha gradient. */
    {
        unsigned char rows[] = { 10,20,30, 64,  50,60,70, 200 };  /* 2*4=8, no pad */
        n = make_bmp(&buf, 2, 1, 32, 1, rows);
        check(cwv_bmp_decode(buf, n, &rgba, &w, &h) == 0 && w == 2 && h == 1,
              "32bpp top-down decodes");
        if (rgba) {
            check(rgba[0] == 30 && rgba[1] == 20 && rgba[2] == 10 && rgba[3] == 64,
                  "32bpp BGRA -> RGBA with alpha");
            check(rgba[4] == 70 && rgba[5] == 60 && rgba[6] == 50 && rgba[7] == 200,
                  "second pixel exact");
            free(rgba); rgba = NULL;
        }
        free(buf); buf = NULL;
    }

    /* Rejections. */
    {
        unsigned char rows[8] = { 0 };   /* 1px * 3B + 1 pad */
        int rc;
        n = make_bmp(&buf, 1, 1, 24, 0, rows);
        rc = cwv_bmp_decode(buf, 5, &rgba, &w, &h);
        check(rc == -1 && rgba == NULL, "truncated file rejected");
        rc = cwv_bmp_decode(buf, n, NULL, NULL, NULL);
        check(rc == -1, "NULL outputs rejected");
        buf[28] = 8;                                   /* 8bpp */
        rc = cwv_bmp_decode(buf, n, &rgba, &w, &h);
        check(rc == -2 && rgba == NULL, "palettized 8bpp rejected");
        buf[28] = 24;
        buf[30] = 4;                                   /* BI_RLE8 */
        rc = cwv_bmp_decode(buf, n, &rgba, &w, &h);
        check(rc == -2, "RLE compression rejected");
        free(buf); buf = NULL;
    }

    printf("%s\n", fails ? "BMP UNIT: FAILURES" : "BMP UNIT: ALL PASS");
    return fails ? 1 : 0;
}
