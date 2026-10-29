/* cwv_platform.c - Windows implementation of the CWebView console seam.
 *
 * Targets (user direction 2026-10-01): Windows on AMD64 and ARM64; the
 * same C11 source builds for both (the win gate cross-compiles with the
 * x86_64-w64-mingw32 and aarch64-w64-mingw32 toolchains and links a
 * real .exe for each).
 *
 * Native Windows, no third-party dependencies:
 *   window/framebuffer : Win32 + GDI (a top-down 32bpp DIB section; the
 *                         primitives are drawn by hand so the composite
 *                         is identical on both architectures)
 *   image decode       : Windows Imaging Component (WIC) - the native
 *                         Windows PNG/JPEG/BMP/TIFF/GIF decoders
 *   text               : GDI + private TTF loading
 *   on-screen keyboard : a small Win32 dialog with an edit control
 *   network            : WinHTTP (http and https GET)
 *
 * The app root is the folder the .exe sits in; web/ lives next to it,
 * exactly like the Wii/Wii U SD-card layouts.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>   /* WIC: mingw + MSVC both ship this name */
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cwv_platform.h"
#include "core/cwv_cwi.h"
#include "cwv_win_internal.h"

/* cwv_platform_set_app_root/url_root/path_root are implemented by the
 * shared engine (cwv_backend.c holds the URL-space roots); the port only
 * maps physical paths, so nothing to define here. */

/* UTF-8 -> UTF-16 for the CRT file calls (install paths may be non-ASCII). */
static wchar_t *utf8_to_wide(const char *utf8, wchar_t *out, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
    if (n <= 0 && cap > 0) out[0] = 0;
    return out;
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
int cwv_file_open_read(const char *path) {
    wchar_t w[1024];
    if (!path) return -1;
    return _wopen(utf8_to_wide(path, w, 1024), _O_RDONLY | _O_BINARY);
}
void cwv_file_close(int fd) { if (fd >= 0) _close(fd); }
int cwv_file_read(int fd, void *buf, int len) {
    return (fd >= 0 && buf && len > 0) ? (int)_read(fd, buf, (unsigned)len) : -1;
}
long cwv_file_size(const char *path) {
    struct _stat64 st;
    wchar_t w[1024];
    if (!path || _wstat64(utf8_to_wide(path, w, 1024), &st) != 0) return -1;
    if (st.st_size < 0 || st.st_size > 0x7FFFFFFFL) return -1;
    return (long)st.st_size;
}
int cwv_file_mkdir(const char *path) {
    wchar_t w[1024];
    if (!path) return -1;
    return _wmkdir(utf8_to_wide(path, w, 1024)) == 0 ? 0 : -1;
}
int cwv_file_write_all(const char *path, const void *buf, size_t len) {
    wchar_t w[1024];
    int fd, wr;
    if (!path || !buf) return -1;
    fd = _wopen(utf8_to_wide(path, w, 1024),
                _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0666);
    if (fd < 0) return -1;
    wr = (int)_write(fd, buf, (unsigned)len);
    _close(fd);
    return (wr == (int)len) ? 0 : -1;
}
int cwv_file_append(const char *path, const void *buf, size_t len) {
    wchar_t w[1024];
    int fd, wr;
    if (!path || !buf) return -1;
    fd = _wopen(utf8_to_wide(path, w, 1024),
                _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0666);
    if (fd < 0) return -1;
    wr = (int)_write(fd, buf, (unsigned)len);
    _close(fd);
    return (wr == (int)len) ? 0 : -1;
}
int cwv_console_cache_dir(char *out, size_t out_cap) {
    wchar_t w[1024];
    char utf8[1024];
    char base[1100];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, w)))
        return -1;
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, sizeof(utf8),
                            NULL, NULL) <= 0)
        return -1;
    snprintf(base, sizeof(base), "%s/CWebView", utf8);
    cwv_file_mkdir(base);
    snprintf(out, out_cap, "%s/cache", base);
    return cwv_file_mkdir(out) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* textures: WIC -> RGBA8888 memory images                              */
/* ------------------------------------------------------------------ */
typedef struct WinTex {
    int w, h;
    unsigned char *rgba;
    int min_filter, mag_filter;   /* CWV_TEX_FILTER_* */
} WinTex;

static IWICImagingFactory *s_wic;
static int s_com_inited;

static IWICImagingFactory *wic_factory(void) {
    if (!s_wic && !s_com_inited) {
        s_com_inited = 1;
        if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
            CoCreateInstance(&CLSID_WICImagingFactory, NULL,
                CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                (void **)&s_wic);
        }
    }
    return s_wic;
}

static void *wic_decode(const unsigned char *buf, size_t len) {
    IWICImagingFactory *f = wic_factory();
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    IStream *stream = NULL;
    HGLOBAL hmem = NULL;
    void *locked = NULL;
    WinTex *tex = NULL;
    UINT w = 0, h = 0;
    if (!f || !buf || len < 8) return NULL;
    /* CreateDecoderFromMemory only exists on IWICImagingFactory2 (Win8+)
     * and is missing from mingw's vtable; an HGLOBAL-backed IStream plus
     * CreateDecoderFromStream works on every Windows and every
     * toolchain, so use that single path everywhere. */
    hmem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (hmem) locked = GlobalLock(hmem);
    if (!hmem || !locked) { if (hmem) GlobalFree(hmem); return NULL; }
    memcpy(locked, buf, len);
    GlobalUnlock(hmem);
    if (FAILED(CreateStreamOnHGlobal(hmem, TRUE, &stream)) || !stream) {
        if (!stream) GlobalFree(hmem);
        return NULL;
    }
    if (FAILED(f->lpVtbl->CreateDecoderFromStream(
            f, stream, NULL, WICDecodeMetadataCacheOnDemand, &dec)) || !dec) {
        stream->lpVtbl->Release(stream);
        return NULL;
    }
    stream->lpVtbl->Release(stream);
    if (SUCCEEDED(dec->lpVtbl->GetFrame(dec, 0, &frame)) && frame) {
        if (SUCCEEDED(f->lpVtbl->CreateFormatConverter(f, &conv)) && conv) {
            if (SUCCEEDED(conv->lpVtbl->Initialize(
                    conv, (IWICBitmapSource *)frame,
                    &GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone, NULL, 0.0,
                    WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(conv->lpVtbl->GetSize(conv, &w, &h)) &&
                w > 0 && h > 0 && w <= 8192 && h <= 8192) {
                tex = (WinTex *)calloc(1, sizeof(WinTex));
                if (tex) {
                    tex->w = (int)w;
                    tex->h = (int)h;
                    /* LINEAR default = the GPU sampling default on the
                     * other ports (vita2d/SDL/GX); the engine switches to
                     * POINT per-texture for image-rendering: pixelated. */
                    tex->min_filter = tex->mag_filter = CWV_TEX_FILTER_LINEAR;
                    tex->rgba = (unsigned char *)malloc((size_t)w * h * 4u);
                    if (!tex->rgba ||
                        FAILED(conv->lpVtbl->CopyPixels(
                            conv, NULL, w * 4u,
                            (UINT)((size_t)w * h * 4u), tex->rgba))) {
                        free(tex->rgba);
                        free(tex);
                        tex = NULL;
                    }
                }
            }
            conv->lpVtbl->Release(conv);
        }
        frame->lpVtbl->Release(frame);
    }
    dec->lpVtbl->Release(dec);
    return tex; /* the HGLOBAL is owned (and freed) by the IStream */
}

static void *tex_from_file(const char *path) {
    long size = cwv_file_size(path);
    unsigned char *buf;
    void *tex = NULL;
    int fd;
    if (size <= 0) return NULL;
    fd = cwv_file_open_read(path);
    if (fd < 0) return NULL;
    buf = (unsigned char *)malloc((size_t)size);
    if (buf && cwv_file_read(fd, buf, (int)size) == (int)size)
        tex = wic_decode(buf, (size_t)size);
    free(buf);
    cwv_file_close(fd);
    return tex;
}

void *cwv_tex_load_png_file(const char *path)   { return tex_from_file(path); }
void *cwv_tex_load_jpeg_file(const char *path)  { return tex_from_file(path); }
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len) {
    return wic_decode((const unsigned char *)buf, (size_t)len);
}
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len) {
    return wic_decode((const unsigned char *)buf, (size_t)len);
}
/* CWI (2026-10-05 (71)): the Win renderer draws straight from the
 * WinTex rgba buffer (software scaling), so the shared lossless decode
 * is already the upload - one copy, no WIC involved. */
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len) {
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    WinTex *t;
    if (!cwv_cwi_decode((const unsigned char *)buf, (size_t)len, &rgba, &w, &h))
        return NULL;
    t = (WinTex *)calloc(1, sizeof(WinTex));
    if (!t) { free(rgba); return NULL; }
    t->w = (int)w;
    t->h = (int)h;
    t->rgba = rgba;
    t->min_filter = t->mag_filter = CWV_TEX_FILTER_LINEAR;
    return t;
}
int cwv_tex_update_rgba_buffer(void *tex, const unsigned char *rgba, int w, int h) {
    WinTex *t = (WinTex *)tex;
    if (!t || !t->rgba || !rgba || t->w != w || t->h != h || w <= 0 || h <= 0)
        return 0;
    memcpy(t->rgba, rgba, (size_t)w * (size_t)h * 4u);
    return 1;
}
void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len) {
    return wic_decode((const unsigned char *)buf, (size_t)len);
}
void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int w, int h) {
    WinTex *t;
    size_t bytes;
    if (!rgba || w <= 0 || h <= 0 || w > 1024 || h > 768) return NULL;
    bytes = (size_t)w * (size_t)h * 4u;
    t = (WinTex *)calloc(1, sizeof(WinTex));
    if (!t) return NULL;
    t->rgba = (unsigned char *)malloc(bytes);
    if (!t->rgba) { free(t); return NULL; }
    memcpy(t->rgba, rgba, bytes);
    t->w = w; t->h = h;
    t->min_filter = t->mag_filter = CWV_TEX_FILTER_LINEAR;
    return t;
}
int cwv_tex_width(void *tex)  { return tex ? ((WinTex *)tex)->w : 0; }
int cwv_tex_height(void *tex) { return tex ? ((WinTex *)tex)->h : 0; }
void cwv_tex_set_filter(void *tex, int min_f, int mag_f) {
    if (tex) {
        ((WinTex *)tex)->min_filter = min_f;
        ((WinTex *)tex)->mag_filter = mag_f;
    }
}
void cwv_tex_free(void *tex) {
    if (tex) { free(((WinTex *)tex)->rgba); free(tex); }
}

/* ------------------------------------------------------------------ */
/* frame buffer: a top-down 32bpp DIB the CPU draws into                */
/* ------------------------------------------------------------------ */
static unsigned char *s_pixels;   /* BGRA, top-down, stride = W*4 (GDI-owned) */
static int s_w = CWV_WIN_DEFAULT_W, s_h = CWV_WIN_DEFAULT_H;
static HDC s_memdc;
static HBITMAP s_dib, s_oldbm;
static BITMAPINFO s_bmi;

void cwv_win_frame_size(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w == s_w && h == s_h && s_pixels) return;
    if (s_memdc && s_oldbm) {
        SelectObject(s_memdc, s_oldbm);
        s_oldbm = NULL;
    }
    if (s_dib) { DeleteObject(s_dib); s_dib = NULL; }
    if (s_memdc) { DeleteDC(s_memdc); s_memdc = NULL; }
    s_pixels = NULL;
    s_w = w; s_h = h;
    memset(&s_bmi, 0, sizeof(s_bmi));
    s_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth = w;
    s_bmi.bmiHeader.biHeight = -h;   /* top-down rows, BGRA byte order */
    s_bmi.bmiHeader.biPlanes = 1;
    s_bmi.bmiHeader.biBitCount = 32;
    s_bmi.bmiHeader.biCompression = BI_RGB;
}

/* Lazily create the DIB section + memory DC (needed by draw AND text
 * measure calls, which happen during layout, before any frame draws). */
static HDC frame_dc(void) {
    if (!s_pixels) {
        HDC screen = GetDC(NULL);
        if (!s_memdc) s_memdc = CreateCompatibleDC(screen);
        if (!s_dib)
            s_dib = CreateDIBSection(screen, &s_bmi, DIB_RGB_COLORS,
                                     (void **)&s_pixels, NULL, 0);
        ReleaseDC(NULL, screen);
        if (s_memdc && s_dib && !s_oldbm)
            s_oldbm = (HBITMAP)SelectObject(s_memdc, s_dib);
        if (!s_pixels) {                   /* fall back to a plain heap buffer */
            s_pixels = (unsigned char *)calloc((size_t)s_w * s_h, 4u);
        }
    }
    return s_memdc;
}

void cwv_win_frame_begin(void) {
    if (!s_pixels) cwv_win_frame_size(s_w, s_h);
    (void)frame_dc();
}

void cwv_win_frame_end(HWND hwnd) {
    HDC wnd_dc;
    RECT rc;
    if (!s_pixels || !hwnd) return;
    wnd_dc = GetDC(hwnd);
    if (wnd_dc) {
        GetClientRect(hwnd, &rc);
        /* Keep the software framebuffer crisp when the window is scaled.
         * HALFTONE blurred text and thin CSS borders in fullscreen; the
         * current renderer is a fixed logical canvas, so point sampling is
         * the least surprising presentation until a resizable host viewport
         * is implemented. */
        SetStretchBltMode(wnd_dc, COLORONCOLOR);
        SetBrushOrgEx(wnd_dc, 0, 0, NULL);
        StretchDIBits(wnd_dc, 0, 0, rc.right, rc.bottom,
                      0, 0, s_w, s_h, s_pixels, &s_bmi, DIB_RGB_COLORS,
                      SRCCOPY);
        ReleaseDC(hwnd, wnd_dc);
    }
}

/* ---- hand-drawn primitives over the BGRA buffer ------------------ */
static unsigned char *px(int x, int y) {
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) return NULL;
    return s_pixels + 4u * (size_t)(y * s_w + x);
}
/* RGBA8(color) blended over the opaque destination. */
/* Per-element scroll clip (2026-10-06 (75)): this port is a pure
 * software renderer, so the clip lives in the pixel-write path itself.
 * blend_pixel (rects/circles/blits) and the GDI text draw below both
 * intersect with it. */
static int   s_clip_on;
static float s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1;
void cwv_set_clip(float x, float y, float w, float h) {
    s_clip_on = 1;
    s_clip_x0 = x; s_clip_y0 = y;
    s_clip_x1 = x + w; s_clip_y1 = y + h;
}
void cwv_clip_disable(void) { s_clip_on = 0; }

static void blend_pixel(int x, int y, unsigned r, unsigned g, unsigned b, unsigned a) {
    if (s_clip_on && ((float)x < s_clip_x0 || (float)x >= s_clip_x1 ||
                      (float)y < s_clip_y0 || (float)y >= s_clip_y1)) return;
    unsigned char *d = px(x, y);
    if (!d || a == 0) return;
    if (a >= 255) { d[0] = (unsigned char)b; d[1] = (unsigned char)g; d[2] = (unsigned char)r; return; }
    d[0] = (unsigned char)((b * a + d[0] * (255 - a)) >> 8);
    d[1] = (unsigned char)((g * a + d[1] * (255 - a)) >> 8);
    d[2] = (unsigned char)((r * a + d[2] * (255 - a)) >> 8);
}
static unsigned char *tex_px(const WinTex *t, int x, int y) {
    if (!t || !t->rgba || x < 0 || y < 0 || x >= t->w || y >= t->h) return NULL;
    return t->rgba + 4u * (size_t)(y * t->w + x);
}

void cwv_clear_screen(void) {
    if (!s_pixels) return;
    memset(s_pixels, 0, 4u * (size_t)s_w * s_h);
}
void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    int x0 = (int)x, y0 = (int)y, x1 = x0 + (int)w, y1 = y0 + (int)h;
    int ix, iy;
    unsigned r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
    unsigned a = (color >> 24) & 0xFF;
    if (!s_pixels) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_w) x1 = s_w;
    if (y1 > s_h) y1 = s_h;
    for (iy = y0; iy < y1; iy++)
        for (ix = x0; ix < x1; ix++)
            blend_pixel(ix, iy, r, g, b, a);
}
void cwv_draw_fill_circle(float x, float y, float radius, unsigned color) {
    unsigned r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
    unsigned a = (color >> 24) & 0xFF;
    int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
    int rad = (int)(radius + 0.5f);
    int ix, iy;
    if (!s_pixels || rad <= 0) return;
    for (iy = cy - rad; iy <= cy + rad; iy++) {
        if (iy < 0 || iy >= s_h) continue;
        for (ix = cx - rad; ix <= cx + rad; ix++) {
            int dx = ix - cx, dy = iy - cy;
            if (dx * dx + dy * dy <= rad * rad)
                blend_pixel(ix, iy, r, g, b, a);
        }
    }
}

/* Texture blits, alpha-blended into the frame DIB.
 *
 * FILTER PARITY (2026-10-03): the other ports sample textures on GPU
 * hardware whose default filter is LINEAR (vita2d/GPU, Wii U SDL, Wii
 * GX) - only CSS "image-rendering: pixelated" switches to POINT
 * (CWV_TEX_FILTER_POINT).  The engine pushes that per-texture choice
 * through cwv_tex_set_filter, so this software blit now honors the
 * texture's mag filter: bilinear sampling (sample centers, edge-clamped)
 * for LINEAR, hard nearest for POINT.  Before this, every image was
 * nearest-sampled - blocky on Windows, smooth on the consoles. */
static void blit_scaled(const WinTex *t, float x, float y,
                        int sx, int sy, int sw, int sh,
                        float xs, float ys) {
    int dw = (int)(sw * xs), dh = (int)(sh * ys);
    int bilinear = t && t->mag_filter != CWV_TEX_FILTER_POINT;
    int ix, iy;
    if (!s_pixels) return;
    for (iy = 0; iy < dh; iy++) {
        int dx_row = (int)y + iy;
        float fy = bilinear ? ((float)iy + 0.5f) / ys - 0.5f
                           : (float)iy / ys;
        int src_y = (int)fy + sy;          /* floor for both paths (fy>=0) */
        if (src_y < sy) src_y = sy;
        if (src_y > sy + sh - 1) src_y = sy + sh - 1;
        if (src_y < 0) continue;
        for (ix = 0; ix < dw; ix++) {
            int dx = (int)x + ix, dy = dx_row;
            unsigned char *p;
            if (!bilinear) {
                p = tex_px(t, (int)((float)ix / xs) + sx, src_y);
                if (p) blend_pixel(dx, dy, p[0], p[1], p[2], p[3]);
                continue;
            }
            {
                float fx = ((float)ix + 0.5f) / xs - 0.5f;
                int x0 = (int)fx + sx, x1;
                float wx = fx - (float)(int)fx;
                float wy = fy - (float)(int)fy;
                const unsigned char *p00, *p10, *p01, *p11;
                int y1 = src_y < sy + sh - 1 ? src_y + 1 : src_y;
                unsigned c[4];
                int ch;
                if (x0 < sx) x0 = sx;
                if (x0 > sx + sw - 1) x0 = sx + sw - 1;
                x1 = x0 < sx + sw - 1 ? x0 + 1 : x0;
                p00 = tex_px((WinTex *)t, x0, src_y);
                p10 = tex_px((WinTex *)t, x1, src_y);
                p01 = tex_px((WinTex *)t, x0, y1);
                p11 = tex_px((WinTex *)t, x1, y1);
                if (!p00 || !p10 || !p01 || !p11) continue;
                for (ch = 0; ch < 4; ch++) {
                    float top = p00[ch] + (p10[ch] - p00[ch]) * wx;
                    float bot = p01[ch] + (p11[ch] - p01[ch]) * wx;
                    c[ch] = (unsigned)(top + (bot - top) * wy + 0.5f);
                }
                blend_pixel(dx, dy, c[0], c[1], c[2], c[3]);
            }
        }
    }
}
void cwv_draw_texture(void *tex, float x, float y) {
    const WinTex *t = (const WinTex *)tex;
    if (!t) return;
    blit_scaled(t, x, y, 0, 0, t->w, t->h, 1.0f, 1.0f);
}
void cwv_draw_texture_scale(void *tex, float x, float y, float xs, float ys) {
    const WinTex *t = (const WinTex *)tex;
    if (!t) return;
    blit_scaled(t, x, y, 0, 0, t->w, t->h,
                xs > 0.0f ? xs : 1.0f, ys > 0.0f ? ys : 1.0f);
}
void cwv_draw_texture_part(void *tex, float x, float y,
                           float tx, float ty, float tw, float th) {
    const WinTex *t = (const WinTex *)tex;
    if (!t) return;
    blit_scaled(t, x, y, (int)tx, (int)ty, (int)tw, (int)th, 1.0f, 1.0f);
}
void cwv_draw_texture_part_scale(void *tex, float x, float y,
                                 float tx, float ty, float tw, float th,
                                 float xs, float ys) {
    const WinTex *t = (const WinTex *)tex;
    if (!t) return;
    blit_scaled(t, x, y, (int)tx, (int)ty, (int)tw, (int)th,
                xs > 0.0f ? xs : 1.0f, ys > 0.0f ? ys : 1.0f);
}

/* ------------------------------------------------------------------ */
/* fonts: GDI text into the DIB                                        */
/* ------------------------------------------------------------------ */
#define WIN_FONT_CACHE 16
typedef struct WinFont {
    char family[64];                 /* GDI face name (from the TTF) */
    HFONT cache[WIN_FONT_CACHE];     /* per pixel-size handles */
    int   sizes[WIN_FONT_CACHE];
    float fit;                       /* engine px -> GDI em px (see below) */
} WinFont;

/* TEXT FIT (2026-10-03, user report: "text is too large for its element
 * container and overflows outside of it" - render parity round).
 *
 * The shared engine speaks a fixed calibration: CSS font sizes live in
 * renderer units, the @font-face seams are called with pixels
 * pre-multiplied at 32 PX PER UNIT (cwv_html.c measure_text_width,
 * cwv_backend.c draw sites), and the LINE BOX is 28 px per unit
 * (cwv_html.c line_height).  A font whose glyph CELL height is <= the
 * line box fits its container - that is what the Vita PGF and the Wii U
 * TTF (WIIU_PGF_PX_PER_UNIT / WIIU_FONT_FIT) calibrations produce.
 *
 * GDI was drawing the engine's 32-px-per-unit size as the EM size
 * verbatim (CreateFontW(-px)): the CELL (ascent + descent + internal
 * leading, ~1.15x em for a typical UI face) then measured ~36 px per
 * unit against a 28 px line box - every text run overflowed its
 * element.  The fix mirrors the other TTF ports: scale the em so the
 * CELL height lands at 28 px per unit, measured per face via
 * GetTextMetrics (works for any bundled font, not one magic constant):
 *      fit = 28 / (cell_ratio * 32),  cell_ratio = cell_px / em_px
 * Measure and draw both go through the same fit, so wrap/width math
 * stays internally consistent (wrapping measures what is drawn). */
#define WIN_FIT_LINEBOX_PER_UNIT 28.0f   /* engine line_height contract */
#define WIN_FIT_PX_PER_UNIT      32.0f   /* engine font-face contract   */
#define WIN_FIT_DEFAULT_CELL_RATIO 1.15f /* typical UI face, before probe */

static HFONT font_handle(WinFont *f, int px) {
    int i, slot = -1;
    for (i = 0; i < WIN_FONT_CACHE; i++) {
        if (f->cache[i] && f->sizes[i] == px) return f->cache[i];
        if (!f->cache[i] && slot < 0) slot = i;
    }
    if (slot < 0) { DeleteObject(f->cache[0]); slot = 0; f->cache[0] = NULL; }
    f->cache[slot] = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE,
                                 utf8_to_wide(f->family, (wchar_t[64]){0}, 64));
    f->sizes[slot] = px;
    return f->cache[slot];
}

/* Parse the family name out of a TTF/OTF "name" table without any
 * external library: enough for @font-face files, which are exactly the
 * fonts the engine loads. */
static int ttf_family(const char *path, char *out, size_t cap) {
    long size = cwv_file_size(path);
    unsigned char *buf;
    int fd, ok = 0;
    unsigned num_tables, i;
    if (size < 12) return -1;
    fd = cwv_file_open_read(path);
    if (fd < 0) return -1;
    buf = (unsigned char *)malloc((size_t)size);
    if (buf && cwv_file_read(fd, buf, (int)size) == (int)size &&
        buf[0] == 0x00 && buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x00) {
        num_tables = ((unsigned)buf[4] << 8) | buf[5];
        for (i = 0; i < num_tables && !ok; i++) {
            unsigned char *rec = buf + 12 + 16 * i;
            unsigned off, len;
            if (rec + 16 > buf + size) break;
            if (rec[0] == 'n' && rec[1] == 'a' && rec[2] == 'm' && rec[3] == 'e') {
                off = ((unsigned)rec[8] << 24) | ((unsigned)rec[9] << 16) |
                      ((unsigned)rec[10] << 8) | rec[11];
                len = ((unsigned)rec[12] << 24) | ((unsigned)rec[13] << 16) |
                      ((unsigned)rec[14] << 8) | rec[15];
                if (off + len <= (unsigned)size && len >= 6) {
                    unsigned char *nm = buf + off;
                    unsigned count = ((unsigned)nm[4] << 8) | nm[5], r;
                    for (r = 0; r < count && !ok; r++) {
                        unsigned char *rec2 = nm + 6 + 12 * r;
                        unsigned name_id, str_off, str_len;
                        if (rec2 + 12 > buf + size) break;
                        name_id = ((unsigned)rec2[6] << 8) | rec2[7];
                        str_off = ((unsigned)nm[2] << 8) | nm[3];  /* string storage */
                        str_off += ((unsigned)rec2[10] << 8) | rec2[11];
                        str_len = ((unsigned)rec2[8] << 8) | rec2[9];
                        if (name_id == 1 && str_len > 0 &&
                            str_off + str_len <= (unsigned)size) {
                            /* UTF-16BE name -> UTF-8 (ASCII-safe subset) */
                            size_t o = 0;
                            unsigned k;
                            for (k = 0; k + 1 < str_len && o + 3 < cap; k += 2) {
                                unsigned ch = ((unsigned)buf[str_off + k] << 8) |
                                              buf[str_off + k + 1];
                                if (ch >= 32 && ch < 127) out[o++] = (char)ch;
                            }
                            out[o] = 0;
                            ok = o > 0;
                        }
                    }
                }
            }
        }
    }
    free(buf);
    cwv_file_close(fd);
    return ok ? 0 : -1;
}

static void cwv_font_free_impl(void *font);

void *cwv_pgf_load_default(void) {
    WinFont *f = (WinFont *)calloc(1, sizeof(WinFont));
    if (f) snprintf(f->family, sizeof(f->family), "Segoe UI");
    return f;
}
void *cwv_font_load_file(const char *path) {
    WinFont *f = (WinFont *)calloc(1, sizeof(WinFont));
    wchar_t w[1024];
    if (!f) return NULL;
    if (ttf_family(path, f->family, sizeof(f->family)) != 0) {
        free(f);
        return NULL;                 /* not a loadable sfnt: caller logs it */
    }
    /* Private to this process; unloaded when the font object is freed. */
    AddFontResourceExW(utf8_to_wide(path, w, 1024), FR_PRIVATE, 0);
    return f;
}
void cwv_pgf_free(void *pgf) { cwv_font_free_impl(pgf); }
void  cwv_font_free(void *font) { cwv_font_free_impl(font); }
static void cwv_font_free_impl(void *font) {
    WinFont *f = (WinFont *)font;
    int i;
    if (!f) return;
    for (i = 0; i < WIN_FONT_CACHE; i++)
        if (f->cache[i]) DeleteObject(f->cache[i]);
    free(f);
}

/* The per-face fit factor (see the WinFont block above).  Probes the
 * real metrics ONCE per face (100px em) and caches; falls back to the
 * typical cell ratio when no DC is available yet. */
static float font_fit(WinFont *f) {
    HDC dc;
    HFONT old;
    TEXTMETRICW tm;
    if (!f) return WIN_FIT_LINEBOX_PER_UNIT /
                 (WIN_FIT_DEFAULT_CELL_RATIO * WIN_FIT_PX_PER_UNIT);
    if (f->fit > 0.0f) return f->fit;
    f->fit = WIN_FIT_LINEBOX_PER_UNIT /
             (WIN_FIT_DEFAULT_CELL_RATIO * WIN_FIT_PX_PER_UNIT);
    dc = frame_dc();
    if (!dc) return f->fit;
    old = (HFONT)SelectObject(dc, font_handle(f, 100));
    if (GetTextMetricsW(dc, &tm) && tm.tmHeight > 0) {
        float cell_ratio = (float)tm.tmHeight / 100.0f;
        if (cell_ratio > 0.5f && cell_ratio < 3.0f)
            f->fit = WIN_FIT_LINEBOX_PER_UNIT /
                     (cell_ratio * WIN_FIT_PX_PER_UNIT);
    }
    SelectObject(dc, old);
    return f->fit;
}
/* Engine renderer units -> engine pixels (the pgf seam's contract is
 * the same 32 px per unit the @font-face draw sites pre-multiply). */
static int font_px_from_scale(float size) {
    int px = (int)(WIN_FIT_PX_PER_UNIT * (size > 0.0f ? size : 1.0f) + 0.5f);
    return px < 8 ? 8 : px;
}
static int text_width_impl(WinFont *f, int px, const char *utf8) {
    HDC dc = frame_dc();
    wchar_t w[512];
    SIZE sz = {0, 0};
    HFONT old;
    int len;
    if (!dc) return 0;
    len = MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, w, 512) - 1;
    if (len <= 0) return 0;
    /* engine px -> GDI em px, so the CELL height fits the line box
     * (see the WinFont fit block); measured = drawn, wrapping stays
     * consistent */
    px = (int)((float)px * font_fit(f) + 0.5f);
    if (px < 4) px = 4;
    old = (HFONT)SelectObject(dc, font_handle(f, px));
    GetTextExtentPoint32W(dc, w, len, &sz);
    SelectObject(dc, old);
    return (int)sz.cx;
}
int cwv_pgf_text_width(void *pgf, float size, const char *utf8) {
    return text_width_impl((WinFont *)pgf, font_px_from_scale(size), utf8);
}
int cwv_font_text_width(void *font, unsigned size, const char *utf8) {
    /* `size` arrives pre-multiplied at 32 px/unit (engine contract);
     * the fit factor inside text_width_impl scales to what is drawn */
    return text_width_impl((WinFont *)font, (int)size, utf8);
}
static int draw_text_impl(WinFont *f, int px, float x, float y,
                          unsigned color, const char *utf8) {
    HDC dc = frame_dc();
    wchar_t w[512];
    HFONT old;
    int len;
    if (!dc) return 0;
    len = MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, w, 512) - 1;
    if (len <= 0) return 0;
    px = (int)((float)px * font_fit(f) + 0.5f);   /* see fit block */
    if (px < 4) px = 4;
    old = (HFONT)SelectObject(dc, font_handle(f, px));
    SetTextColor(dc, RGB(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF));
    SetBkMode(dc, TRANSPARENT);
    /* BUG 58 (2026-10-02, user report): engine draw sites pass a BASELINE
     * y (the Wii U seam documents "engine sites pass a baseline" and
     * subtracts the ascent itself). TextOutW defaults to TA_TOP, which
     * drew every glyph one ascent BELOW its element. Align on the
     * baseline like the other ports. */
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    if (s_clip_on) {
        /* scroll-region text: clip the GDI output to the same rect the
         * pixel path uses (SaveDC pairs with RestoreDC below). */
        SaveDC(dc);
        IntersectClipRect(dc, (int)s_clip_x0, (int)s_clip_y0,
                          (int)(s_clip_x1 + 0.5f), (int)(s_clip_y1 + 0.5f));
        TextOutW(dc, (int)x, (int)y, w, len);
        RestoreDC(dc, -1);
    } else {
        TextOutW(dc, (int)x, (int)y, w, len);
    }
    SelectObject(dc, old);
    return px;
}
int cwv_pgf_draw_text(void *pgf, float x, float y, unsigned color,
                      float size, const char *utf8) {
    return draw_text_impl((WinFont *)pgf, font_px_from_scale(size),
                          x, y, color, utf8);
}
int cwv_font_draw_text(void *font, float x, float y, unsigned color,
                       unsigned size, const char *utf8) {
    return draw_text_impl((WinFont *)font, (int)size,
                          x, y, color, utf8);
}

/* ------------------------------------------------------------------ */
/* on-screen keyboard: a Win32 dialog with an edit control             */
/* ------------------------------------------------------------------ */
static struct {
    HWND dlg, edit;
    int open, result;                /* result: 0 none, 1 OK, 2 cancel */
} s_ime;

static void ime_on_command(HWND dlg, int id) {
    if (id == 100) { s_ime.result = 1; DestroyWindow(dlg); }
    else if (id == 101) { s_ime.result = 2; DestroyWindow(dlg); }
}
static LRESULT CALLBACK ime_wndproc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        ime_on_command(dlg, LOWORD(wp));
        return 0;
    case WM_CLOSE:
        s_ime.result = 2;
        DestroyWindow(dlg);
        return 0;
    case WM_DESTROY:
        s_ime.dlg = NULL; s_ime.edit = NULL; s_ime.open = 0;
        return 0;
    default:
        return DefWindowProcW(dlg, msg, wp, lp);
    }
}

int cwv_ime_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                 int password, int multiline,
                 cwv_utf16 *buffer, int buffer_len) {
    static int class_reg;
    WNDCLASSW wc;
    HWND dlg, edit, ok_btn, cancel_btn;
    RECT wa;
    int w = 460, h = multiline ? 220 : 160, x, y;
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    (void)buffer; (void)buffer_len;   /* text is read in cwv_ime_poll */
    if (!class_reg) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = ime_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"CWVImeWnd";
        RegisterClassW(&wc);
        class_reg = 1;
    }
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    x = wa.left + ((wa.right - wa.left) - w) / 2;
    y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    dlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"CWVImeWnd",
                          title ? (LPCWSTR)title : L"Input",
                          WS_POPUP | WS_CAPTION | WS_SYSMENU,
                          x, y, w, h, NULL, NULL,
                          GetModuleHandleW(NULL), NULL);
    if (!dlg) return -1;
    if (multiline) style |= ES_MULTILINE;
    if (password) style |= ES_PASSWORD;
    edit = CreateWindowExW(0, L"EDIT", initial ? (LPCWSTR)initial : L"",
                           style, 12, 12, w - 24, h - 84, dlg,
                           (HMENU)200, GetModuleHandleW(NULL), NULL);
    ok_btn = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
                             w - 210, h - 58, 90, 30, dlg,
                             (HMENU)100, GetModuleHandleW(NULL), NULL);
    cancel_btn = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                                 w - 110, h - 58, 90, 30, dlg,
                                 (HMENU)101, GetModuleHandleW(NULL), NULL);
    (void)ok_btn; (void)cancel_btn;
    if (!edit) { DestroyWindow(dlg); return -1; }
    s_ime.dlg = dlg; s_ime.edit = edit; s_ime.open = 1; s_ime.result = 0;
    ShowWindow(dlg, SW_SHOWNORMAL);
    SetFocus(edit);
    return 0;
}

int cwv_ime_poll(cwv_utf16 *buffer, int buffer_len) {
    MSG msg;
    while (s_ime.dlg &&
           PeekMessageW(&msg, s_ime.dlg, 0, 0, PM_REMOVE | PM_QS_INPUT)) {
        if (!IsDialogMessageW(s_ime.dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (s_ime.result == 0) return 0;
    if (s_ime.result == 1 && buffer && buffer_len > 0) {
        int len = GetWindowTextW(s_ime.edit, (LPWSTR)buffer, buffer_len);
        (void)len;
    }
    {
        int r = s_ime.result;
        s_ime.result = 0;   /* dialog is destroyed; next open resets */
        return r;
    }
}

/* ------------------------------------------------------------------ */
/* console services: WinHTTP fetch + ShellExecute                     */
/* ------------------------------------------------------------------ */
void cwv_console_init(void) {
    (void)wic_factory();             /* CoInitialize + WIC factory up front */
}
void cwv_console_shutdown(void) {
    if (s_wic) { s_wic->lpVtbl->Release(s_wic); s_wic = NULL; }
    if (s_com_inited) {
        CoUninitialize();
        s_com_inited = 0;
    }
}
int cwv_console_open_browser(const char *url) {
    wchar_t w[2048];
    if (!url) return -1;
    if ((HINSTANCE)(INT_PTR)ShellExecuteW(NULL, L"open",
            utf8_to_wide(url, w, 2048), NULL, NULL, SW_SHOWNORMAL) > (HINSTANCE)32)
        return 0;
    return -1;
}

static int http_get(const char *url, unsigned char *buf, size_t cap,
                    size_t *out_len, int nul_terminate) {
    URL_COMPONENTSW parts;
    wchar_t wurl[2048], whost[512], wpath[1536];
    wchar_t scheme[32];
    HINTERNET net = NULL, conn = NULL, req = NULL;
    DWORD avail, read_total = 0, chunk;
    int ok = 0, is_https;
    size_t used = 0;

    if (!url || !buf || cap == 0) return -1;
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszScheme = scheme;      parts.dwSchemeLength = 32;
    parts.lpszHostName = whost;     parts.dwHostNameLength = 512;
    parts.lpszUrlPath = wpath;      parts.dwUrlPathLength = 1536;
    if (!WinHttpCrackUrl(utf8_to_wide(url, wurl, 2048), 0, 0, &parts) ||
        parts.dwHostNameLength == 0)
        return -1;
    is_https = parts.nScheme == INTERNET_SCHEME_HTTPS;
    net = WinHttpOpen(L"CWebView/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!net) return -1;
    conn = WinHttpConnect(net, whost,
                          is_https ? INTERNET_DEFAULT_HTTPS_PORT
                                   : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!conn) goto done;
    req = WinHttpOpenRequest(conn, L"GET",
                             parts.dwUrlPathLength ? wpath : L"/",
                             NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto done;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL))
        goto done;
    for (;;) {
        if (!WinHttpQueryDataAvailable(req, &avail)) goto done;
        if (avail == 0) break;
        if (used + avail >= cap) break;            /* cap = truncate */
        chunk = avail;
        if (!WinHttpReadData(req, buf + used, chunk, &chunk)) goto done;
        if (chunk == 0) break;
        used += chunk;
    }
    if (nul_terminate && used + 1 <= cap) buf[used] = 0;
    ok = 1;
    read_total = (DWORD)used;
    (void)read_total;
done:
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (net) WinHttpCloseHandle(net);
    if (ok && out_len) *out_len = used;
    return ok ? 0 : -1;
}

int cwv_console_fetch(const char *url, char *body, size_t cap) {
    size_t n = 0;
    if (http_get(url, (unsigned char *)body, cap, &n, 1) != 0) return -1;
    return (int)n;
}
int cwv_console_fetch_bin(const char *url, unsigned char *buf, size_t cap,
                          size_t *out_len) {
    return http_get(url, buf, cap, out_len, 0);
}

/* Platform identity exposed to the limited DOM navigator object. */
const char *cwv_platform_name(void) { return "Windows"; }
