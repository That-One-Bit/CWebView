/* cwv_platform.c - Wii implementation of the shared engine's platform
 * seam (sources/shared/sources/core/cwv_platform.h), built on libogc + GRRLIB.
 *
 * STATUS (port started, not yet hardware-verified):
 *   DONE   files (sd:/apps/cwebview/ layout), textures (PNG/JPEG/BMP
 *          decode + upload), draw primitives, screen clear, TTF fonts
 *   DONE   part-texture draws (GRRLIB_DrawPart, real per-UV sub-rects
 *          since GRRLIB 4.4) and the bundled default font
 *          (the package folder's assets/font.ttf, same file the Wii U
 *          ships)
 *   TODO   software keyboard (no native IME on Wii; cwv_ime_open
 *          returns -1 and the engine degrades gracefully)
 *
 * Path mapping (2026-09-23 (33), user direction): app0: is a REAL Vita
 * device and stays Vita-only.  The Wii loads everything from the
 * FOLDER THE PACKAGE SITS IN - the Homebrew Channel passes boot.dol's
 * full path in argv[0], so .../exampledir/boot.dol reads its web
 * content from .../exampledir/web/ - and the engine's URL root is
 * pointed at that folder at boot (cwv_platform_set_app_root).  The
 * compile-time CWV_WII_APP_ROOT default is the canonical sd:/apps
 * layout for when argv[0] is unusable.
 *
 * Modules (.rel, user note): the Wii's relocatable .rel format is the
 * analog of the Vita's suprx - code loaded on demand, memory released
 * when not needed.  ESM/Scroll equivalents can ship as .rel files later;
 * for now the built-in transformer (compiled in) is the plan, same as
 * the Wii U.  See sources/wii/README-WII.md.
 */
#include "core/cwv_bmp.h"
#include "core/cwv_cwi.h"
#include <grrlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "core/cwv_platform.h"

#ifndef CWV_WII_APP_ROOT
#define CWV_WII_APP_ROOT "sd:/apps/cwebview"
#endif

/* GRRLIB colors are 0xRRGGBBAA; the engine speaks ABGR (0xAABBGGRR). */
static unsigned cwv_to_grrlib(unsigned color) {
    return ((color & 0x000000FFUL) << 24) |   /* R */
           ((color & 0x0000FF00UL) <<  8) |  /* G */
           ((color & 0x00FF0000UL) >>  8) |  /* B */
           ((color & 0xFF000000UL) >> 24);   /* A */
}

/* ---- files ---------------------------------------------------------- */
/* The package folder (folder boot.dol sits in), from the HBC's argv[0]
 * when available, else the CWV_WII_APP_ROOT fallback. */
static char g_app_root[256] = "";

/* The Homebrew Channel (and emulators) pass the app's full path as
 * argv[0]; the folder it sits in IS the app root
 * (.../exampledir/boot.dol -> .../exampledir).  "" on unusable input. */
void cwv_wii_app_root_init(const char *argv0)
{
    const char *slash;
    const char *sdcard = NULL;
    const char *p;
    size_t n;
    g_app_root[0] = 0;
    if (!argv0 || !argv0[0]) return;

    /* Dolphin often launches the staged host file directly, so argv[0]
     * can look like C:\...\build\wii\sdcard\apps\cwebview\boot.dol
     * rather than the path visible to the emulated Wii. Translate that
     * staged SD-card suffix back to the Wii's sd:/ namespace. */
    for (p = argv0; *p; ++p) {
        if (strncmp(p, "sdcard/", 7) == 0 || strncmp(p, "sdcard\\", 7) == 0) {
            sdcard = p + 7;
            break;
        }
    }
    if (sdcard) {
        n = strlen(sdcard);
        if (n >= 9 && (strcmp(sdcard + n - 9, "/boot.dol") == 0 ||
                       strcmp(sdcard + n - 9, "\\boot.dol") == 0))
            n -= 9;
        else {
            slash = strrchr(sdcard, '/');
            {
                const char *backslash = strrchr(sdcard, '\\');
                if (!slash || (backslash && backslash > slash)) slash = backslash;
            }
            if (slash) n = (size_t)(slash - sdcard);
        }
        if (n + 4 < sizeof(g_app_root)) {
            memcpy(g_app_root, "sd:/", 4);
            memcpy(g_app_root + 4, sdcard, n);
            g_app_root[n + 4] = 0;
            /* Convert any Windows separators in the copied suffix. */
            for (p = g_app_root + 4; *p; ++p)
                if (*p == '\\') *((char *)p) = '/';
            return;
        }
    }

    /* Real Homebrew Channel paths use sd:/.../boot.dol. */
    slash = strrchr(argv0, '/');
    {
        const char *backslash = strrchr(argv0, '\\');
        if (!slash || (backslash && backslash > slash)) slash = backslash;
    }
    if (!slash || slash == argv0) return;
    n = (size_t)(slash - argv0);
    if (n == 0 || n >= sizeof(g_app_root)) return;
    memcpy(g_app_root, argv0, n);
    g_app_root[n] = 0;
}

const char *cwv_wii_app_root(void)
{
    if (!g_app_root[0])
        snprintf(g_app_root, sizeof(g_app_root), "%s", CWV_WII_APP_ROOT);
    return g_app_root;
}

static void map_path(const char *in, char *out, size_t cap) {
    /* app0: is Vita-only (see the header comment): the engine now works
     * in REAL sd: paths - the folder the package sits in. */
    snprintf(out, cap, "%s", in);
}
int cwv_file_open_read(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    FILE *f = fopen(p, "rb");
    return f ? (int)(long)f : -1;
}
void cwv_file_close(int fd) { if (fd >= 0) fclose((FILE *)(long)fd); }
int cwv_file_read(int fd, void *buf, int len) {
    return (int)fread(buf, 1, (size_t)len, (FILE *)(long)fd);
}
long cwv_file_size(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}
int cwv_file_mkdir(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    return mkdir(p, 0777) == 0 ? 0 : -1;
}
int cwv_file_write_all(const char *path, const void *buf, size_t len) {
    char p[512]; map_path(path, p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t wr = fwrite(buf, 1, len, f);
    fclose(f);
    return (wr == len) ? 0 : -1;
}
int cwv_file_append(const char *path, const void *buf, size_t len) {
    char p[512]; map_path(path, p, sizeof(p));
    FILE *f = fopen(p, "ab");
    if (!f) return -1;
    size_t wr = fwrite(buf, 1, len, f);
    fclose(f);
    return (wr == len) ? 0 : -1;
}

/* ---- textures ------------------------------------------------------- */
/* GRRLIB_LoadTexture sniffs the format itself (PNG/JPEG/BMP), so one
 * loader serves all three content-type entry points. */
static GRRLIB_texImg *tex_from_buffer(const void *buf, unsigned long len) {
    if (!buf || len == 0) return NULL;   /* len is validated by callers; GRRLIB sniffs the format */
    return GRRLIB_LoadTexture(buf);
}
void *cwv_tex_load_png_file(const char *path)  { return cwv_tex_load_jpeg_file(path); }
void *cwv_tex_load_jpeg_file(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    long size = cwv_file_size(path);
    if (size <= 0) return NULL;
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    u8 *data = malloc((size_t)size);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return NULL; }
    fclose(f);
    GRRLIB_texImg *tex = GRRLIB_LoadTexture(data);
    free(data);
    return tex;
}
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len) {
    /* GRRLIB_LoadTexture decodes from the buffer directly; the engine
     * guarantees the buffer outlives the call.  len only needs to be
     * nonzero (GRRLIB sniffs the format itself). */
    return tex_from_buffer(buf, len ? 1 : 0);
}
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len) {
    return tex_from_buffer(buf, len);
}
/* RGBA8 -> GX_TF_RGBA8 upload (shared by the BMP and CWI loaders).
 * BUG 59 (2026-10-02, user report): GRRLIB master removed the old
 * GRRLIB_SetPixel(tex, x, y, c) API (4.4.1 called it
 * GRRLIB_SetPixelTotexImg, master renamed it again). Write the texels
 * ourselves so the build is independent of the installed GRRLIB
 * version.  Layout (defined by GRRLIB's own RawTo4x4RGBA /
 * PNGU_DecodeTo4x4RGBA8): 4x4 blocks of 64 bytes - 32 bytes A/R plane
 * (A,R pairs, row-major), then 32 bytes G/B. */
static void *wii_tex_from_rgba(const unsigned char *rgba, int w, int h) {
    int w4 = (w + 3) & ~3, h4 = (h + 3) & ~3; /* GX RGBA8 is 4-aligned */
    GRRLIB_texImg *tex = GRRLIB_CreateEmptyTexture((u16)w4, (u16)h4);
    if (tex) {
        u8 *data = (u8 *)tex->data;
        u32 blocks_w = (u32)w4 >> 2;
        int y, x;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                const unsigned char *p = rgba + 4u * (size_t)(y * w + x);
                u32 blk = ((u32)y >> 2) * blocks_w + ((u32)x >> 2);
                u8 *b = data + blk * 64u;
                u32 pair = ((u32)(y & 3) * 4u + (u32)(x & 3)) * 2u;
                b[pair] = p[3];          /* A */
                b[pair + 1] = p[0];      /* R */
                b[32u + pair] = p[1];    /* G */
                b[32u + pair + 1] = p[2];/* B */
            }
        }
        GRRLIB_FlushTex(tex);
        tex->w = (u16)w;             /* logical size: draw at real dims */
        tex->h = (u16)h;
    }
    /* BUG 74 (2026-10-06, memory pass): the decoder's RGBA buffer is a
     * one-shot staging allocation - the GX texels above are a COPY, so
     * the source pixels must be freed on BOTH paths (texture built or
     * GRRLIB_CreateEmptyTexture failed), not only on success.  The img
     * element's loaders leaked exactly like this before the per-page
     * texture audit. */
    free((void *)rgba);   /* (75): drop the const qualifier for free() */
    return tex;
}

void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int w, int h) {
    unsigned char *copy;
    size_t bytes;
    if (!rgba || w <= 0 || h <= 0 || w > 1024 || h > 768) return NULL;
    bytes = (size_t)w * (size_t)h * 4u;
    copy = (unsigned char *)malloc(bytes);
    if (!copy) return NULL;
    memcpy(copy, rgba, bytes);
    return wii_tex_from_rgba(copy, w, h);
}

int cwv_tex_update_rgba_buffer(void *texture, const unsigned char *rgba, int w, int h) {
    GRRLIB_texImg *tex = (GRRLIB_texImg *)texture;
    u8 *data;
    u32 blocks_w;
    int x, y;
    if (!tex || !rgba || w <= 0 || h <= 0 ||
        tex->w != (u16)w || tex->h != (u16)h) return 0;
    data = (u8 *)tex->data;
    if (!data) return 0;
    blocks_w = (u32)((w + 3) & ~3) >> 2;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            const unsigned char *p = rgba + 4u * ((size_t)y * (size_t)w + (size_t)x);
            u32 block = ((u32)y >> 2) * blocks_w + ((u32)x >> 2);
            u8 *b = data + (size_t)block * 64u;
            u32 pair = ((u32)(y & 3) * 4u + (u32)(x & 3)) * 2u;
            b[pair] = p[3]; b[pair + 1] = p[0];
            b[32u + pair] = p[1]; b[32u + pair + 1] = p[2];
        }
    }
    /* GRRLIB_FlushTex uses tex->w/h to size its cache flush. The texture
       allocation is 4x4 tiled and may be padded beyond the logical canvas,
       so flush the physical tile dimensions before restoring logical sizing. */
    {
        u16 logical_w = tex->w, logical_h = tex->h;
        tex->w = (u16)((w + 3) & ~3);
        tex->h = (u16)((h + 3) & ~3);
        GRRLIB_FlushTex(tex);
        tex->w = logical_w;
        tex->h = logical_h;
    }
    return 1;
}

void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len) {
    /* GRRLIB's loader sniffs only PNG/JPEG (2026-10-01 audit): routing
     * BMP files through it silently failed, so "images work on all
     * platforms" was false for BMP on Wii. Decode with the shared,
     * unit-gated cwv_bmp decoder and build a real texture. */
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    if (!cwv_bmp_decode(buf, (size_t)len, &rgba, &w, &h))
        return NULL;
    return wii_tex_from_rgba(rgba, w, h);
}

/* CWI (2026-10-05 (71)): the shared lossless decoder produces exactly
 * the RGBA8 layout the BMP path builds, so the same verified GX upload
 * applies - one helper, two decoders, zero duplication. */
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len) {
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    if (!cwv_cwi_decode(buf, (size_t)len, &rgba, &w, &h))
        return NULL;
    return wii_tex_from_rgba(rgba, w, h);
}
int cwv_tex_width(void *tex)  { return tex ? ((GRRLIB_texImg *)tex)->w : 0; }
int cwv_tex_height(void *tex) { return tex ? ((GRRLIB_texImg *)tex)->h : 0; }
void cwv_tex_set_filter(void *tex, int min_f, int mag_f) {
    /* GRRLIB 4.x exposes no per-texture GX filter switch; textures use
     * the global GX settings.  TODO: direct GX calls when needed. */
    (void)tex; (void)min_f; (void)mag_f;
}
void cwv_tex_free(void *tex) { if (tex) GRRLIB_FreeTexture((GRRLIB_texImg *)tex); }

/* ---- fonts ---------------------------------------------------------- */
/* Wii ships no system font either (same as the Wii U): the bundled
 * assets/font.ttf (the libre font described by assets/FONT_LICENSE.txt)
 * IS the default face.  Loaded once, kept for the app lifetime - the
 * engine frees it with cwv_pgf_free, which is a no-op for the singleton. */
static GRRLIB_ttfFont *s_default_font = NULL;
void *cwv_pgf_load_default(void) {
    if (!s_default_font) {
        char font_path[330];
        snprintf(font_path, sizeof(font_path), "%s/assets/font.ttf",
                 cwv_wii_app_root());
        s_default_font = (GRRLIB_ttfFont *)cwv_font_load_file(font_path);
    }
    return s_default_font;
}
void *cwv_font_load_file(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    long size = cwv_file_size(path);
    if (size <= 0) return NULL;
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    u8 *data = malloc((size_t)size);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return NULL; }
    fclose(f);
    /* GRRLIB API (2024, github master): LoadTTF takes the buffer AND
     * its size.  GRRLIB copies the face on load, so free after. */
    GRRLIB_ttfFont *font = GRRLIB_LoadTTF(data, (s32)size);
    free(data);
    return font;
}
void cwv_pgf_free(void *pgf) { (void)pgf; }
void cwv_font_free(void *font) { if (font) GRRLIB_FreeTTF((GRRLIB_ttfFont *)font); }
int cwv_font_text_width(void *font, unsigned size, const char *utf8) {
    /* GRRLIB API (2024): SizeTTF is gone; WidthTTF returns the width. */
    if (!font || !utf8) return 0;
    return (int)GRRLIB_WidthTTF((GRRLIB_ttfFont *)font, utf8, size);
}
int cwv_pgf_text_width(void *pgf, float size, const char *utf8) {
    /* The engine measures default-face text at 32px per unit scale (the
     * Vita PGF contract, see cwv_platform.h) - same factor as the draw. */
    if (!pgf || !utf8) return 0;
    return (int)GRRLIB_WidthTTF((GRRLIB_ttfFont *)pgf, utf8,
                                (unsigned)(size * 32.0f + 0.5f));
}
int cwv_font_draw_text(void *font, float x, float y, unsigned color,
                       unsigned size, const char *utf8) {
    if (!font || !utf8) return 0;
    GRRLIB_PrintfTTF((int)x, (int)y, (GRRLIB_ttfFont *)font, utf8,
                     (unsigned)size, cwv_to_grrlib(color));
    return 1;
}
int cwv_pgf_draw_text(void *pgf, float x, float y, unsigned color,
                      float size, const char *utf8) {
    if (!pgf || !utf8) return 0;
    GRRLIB_PrintfTTF((int)(x + 0.5f), (int)(y + 0.5f), (GRRLIB_ttfFont *)pgf,
                     utf8, (unsigned)(size * 32.0f + 0.5f),
                     cwv_to_grrlib(color));
    return 1;
}

/* ---- primitives ------------------------------------------------------ */
void cwv_clear_screen(void) { GRRLIB_FillScreen(0x000000FF); }

/* Per-element scroll clip (2026-10-06 (75)): GRRLIB is a CPU-side
 * framebuffer renderer with no scissor, so the clip intersects the
 * rect/circle primitives here; the shared engine additionally
 * coarse-culls nodes that fall ENTIRELY outside a scroll region, so
 * only partial text/texture overlap can bleed on this port
 * (documented limitation of the Wii seam). */
static int    s_clip_on;
static float  s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1;
static float  clipf(float a, float b) { return a > b ? a : b; }
static float  clipmin(float a, float b) { return a < b ? a : b; }
void cwv_set_clip(float x, float y, float w, float h) {
    s_clip_on = 1;
    s_clip_x0 = x; s_clip_y0 = y; s_clip_x1 = x + w; s_clip_y1 = y + h;
}
void cwv_clip_disable(void) { s_clip_on = 0; }
static int clip_rect(float *x, float *y, float *w, float *h) {
    if (!s_clip_on) return 1;
    float x2 = *x + *w, y2 = *y + *h;
    float cx2 = clipf(*x, s_clip_x0), cy2 = clipf(*y, s_clip_y0);
    x2 = clipmin(x2, s_clip_x1); y2 = clipmin(y2, s_clip_y1);
    if (x2 <= cx2 || y2 <= cy2) return 0;
    *x = cx2; *y = cy2; *w = x2 - cx2; *h = y2 - cy2;
    return 1;
}
void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    if (!clip_rect(&x, &y, &w, &h)) return;
    GRRLIB_Rectangle(x, y, w, h, cwv_to_grrlib(color), 1);
}
void cwv_draw_fill_circle(float x, float y, float radius, unsigned color) {
    /* GRRLIB API (2024): Circle takes a filled flag - the old implicit
     * fill is an explicit true now. */
    GRRLIB_Circle(x, y, radius, cwv_to_grrlib(color), true);
}
void cwv_draw_texture(void *tex, float x, float y) {
    if (!tex) return;
    GRRLIB_DrawImg(x, y, (GRRLIB_texImg *)tex, 0, 1, 1, 0xFFFFFFFF);
}
void cwv_draw_texture_scale(void *tex, float x, float y, float xs, float ys) {
    if (!tex) return;
    GRRLIB_DrawImg(x, y, (GRRLIB_texImg *)tex, 0, xs, ys, 0xFFFFFFFF);
}
/* Part draws go through GRRLIB_DrawPart (added in GRRLIB 4.4): the
 * source rect (tx,ty,tw,th) is TEXTURE pixels, exactly the semantics the
 * engine passes (same contract as the Wii U SDL srcrect).  Background
 * tiles and object-fit crops are now real sub-UV draws instead of the
 * old whole-texture stand-in. */
void cwv_draw_texture_part(void *tex, float x, float y,
                           float tx, float ty, float tw, float th) {
    if (!tex) return;
    GRRLIB_DrawPart(x, y, tx, ty, tw, th, (GRRLIB_texImg *)tex,
                    0, 1.0f, 1.0f, 0xFFFFFFFF);
}
void cwv_draw_texture_part_scale(void *tex, float x, float y,
                                 float tx, float ty, float tw, float th,
                                 float xs, float ys) {
    if (!tex) return;
    GRRLIB_DrawPart(x, y, tx, ty, tw, th, (GRRLIB_texImg *)tex,
                    0, xs, ys, 0xFFFFFFFF);
}

/* ---- on-screen keyboard ---------------------------------------------- */
/* The software keyboard is its own module (cwv_ime_wii.c): libogc has
 * no IME, so the engine seam is a code-drawn keyboard - the Wii's
 * answer to the Vita OSK and the Wii U swkbd.  main.c routes pad/IR
 * input to it and draws it last; the seam contract is unchanged. */
#include "cwv_ime_wii.h"
int cwv_ime_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                 int password, int multiline,
                 cwv_utf16 *buffer, int buffer_len) {
    return cwv_ime_wii_open(title, initial, password, multiline,
                            buffer, buffer_len);
}
int cwv_ime_poll(cwv_utf16 *buffer, int buffer_len) {
    return cwv_ime_wii_poll(buffer, buffer_len);
}

/* ---- console services ------------------------------------------------- */
/* GRRLIB_Init/video setup happens in the Wii app loop; nothing extra. */
void cwv_console_init(void) {}
void cwv_console_shutdown(void) {}
int cwv_console_open_browser(const char *url) { (void)url; return -1; }  /* TODO: Wii has no external browser hand-off */
/* cwv_console_fetch / cwv_console_fetch_bin live in cwv_fetch_net.c
 * (libogc net stack, HTTP; https is refused - libogc has no TLS). */
/* Remote @font-face fonts get cached next to boot.dol (the classic SD
 * app folder is writable, unlike the read-only disc/WUHB layouts). */
int cwv_console_cache_dir(char *out, size_t out_cap) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/cache", cwv_wii_app_root());
    cwv_file_mkdir(dir);
    if (out_cap == 0) return -1;
    snprintf(out, out_cap, "%s", dir);
    return 0;
}

/* Platform identity exposed to the limited DOM navigator object. */
const char *cwv_platform_name(void) { return "Nintendo Wii"; }
