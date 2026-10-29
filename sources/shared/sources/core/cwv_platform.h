/* cwv_platform.h - the console seam for the shared CWebView engine.
 *
 * The shared engine (parser, CSS, JS runtime, resources) calls ONLY the
 * functions in this header when it needs to touch the console: reading
 * files, decoding/uploading textures, drawing primitives/text, or
 * opening the on-screen keyboard.  Each console port provides exactly
 * one implementation and links it in:
 *
 *   sources/vita/cwv_platform.c        (forwards to the console 2D / IO / IME APIs)
 *   desktop_test/cwv_platform_stub.c   (forwards to the test stubs)
 *
 * A port for a new console (Wii U, Wii) implements this header and the
 * engine works unmodified.  Paths stay in the app0:/web/... virtual form
 * used by the engine; the console maps that onto real storage.
 */
#ifndef CWV_PLATFORM_H
#define CWV_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* UTF-16 code unit, as used by the on-screen keyboard buffers. */
typedef unsigned short cwv_utf16;
#define CWV_IME_MAX_TEXT 2048   /* keyboard buffer length (UTF-16 units) */

/* Packed draw color, same layout the Vita tools use (a<<24|b<<16|g<<8|r).
 * Keep this macro guarded so a console 2D library may define its own. */
#ifndef RGBA8
#define RGBA8(r, g, b, a) ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | ((r)&0xFF))
#endif

/* Texture scaling filter (nearest-neighbor vs bilinear). */
#define CWV_TEX_FILTER_POINT  0
#define CWV_TEX_FILTER_LINEAR 1

/* ---- app content root ----------------------------------------------- */
/* One root prefix maps the engine's URL space onto physical storage:
 *   Vita (default): file:///app0  <->  app0:        (the VPK mount)
 *   Wii U:           the folder the rpx sits in (web/ lives next to it)
 *   Wii:             the folder boot.dol sits in (web/ lives next to it)
 * app0: is a REAL Vita device and stays Vita-only.  The Wii and Wii U
 * run from SD cards, so their URLs name the folder the package is in
 * (e.g. file:///sd:/apps/cwebview) - the URL bar stays honest, and the
 * web content is a plain folder next to the package, never packaged
 * into the rpx/.wuhb itself.  Web content lives at <root>/web on every
 * console.  Ports call the setter once at boot; the defaults keep the
 * Vita layout, so Vita and the desktop stubs are unchanged. */
void        cwv_platform_set_app_root(const char *url_root, const char *path_root);
const char *cwv_platform_url_root(void);   /* "file:///app0" (no trailing /) */
const char *cwv_platform_path_root(void);
/* Stable platform label for navigator.platform and platform-aware launchers. */
const char *cwv_platform_name(void);  /* "app0:"        (no trailing /) */

/* ---- files ---------------------------------------------------------- */
/* All return -1 (or fd < 0) on failure, like the raw console calls. */
int  cwv_file_open_read(const char *path);                 /* -> fd      */
void cwv_file_close(int fd);
int  cwv_file_read(int fd, void *buf, int len);
long cwv_file_size(const char *path);                      /* -1 = fail */
int  cwv_file_mkdir(const char *path);
/* Writes the whole buffer to a freshly created/truncated file. 0 = ok. */
int  cwv_file_write_all(const char *path, const void *buf, size_t len);
/* Append `len` bytes to a file, creating it if needed (runtime logs). */
int  cwv_file_append(const char *path, const void *buf, size_t len);
/* A writable, console-native directory (created if needed) where the
 * shared engine may cache downloaded per-URL resources across
 * app0:/web/... loads - currently: remote @font-face TTFs, since some
 * font loaders can only open a real file, not an in-memory buffer.
 * Copies the path (no trailing slash) into `out` (size `out_cap`) and
 * returns 0, or returns -1 if this console has no such location (the
 * caller then just fails that one load, e.g. Wii's stub below). */
int  cwv_console_cache_dir(char *out, size_t out_cap);

/* ---- textures ------------------------------------------------------- */
/* Texture handles are opaque pointers owned by the console layer.
 * The argument shapes mirror the real console 2D libraries so a port
 * is a one-line forward. */
void *cwv_tex_load_png_file(const char *path);
void *cwv_tex_load_jpeg_file(const char *path);
/* Buffer loaders take the data LENGTH: binary image data embeds NUL
 * bytes, so an implementation must never treat the buffer as a string
 * (SDL_RWFromConstMem(buf, -1) truncates at the first NUL; GRRLIB
 * callers need a nonzero length).  Console libs that read only as
 * much as the decoder asks (libvita2d's libpng streaming) simply
 * ignore the length. */
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len);
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len);
void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len);
/* Upload raw top-down RGBA8 pixels. Each platform may optimize alignment,
 * texture format, and transfer strategy for its own graphics hardware. */
void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int width, int height);
/* Update an existing RGBA texture in place when its logical dimensions match. */
int cwv_tex_update_rgba_buffer(void *tex, const unsigned char *rgba, int width, int height);
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len);   /* CWI
   (2026-10-05 (71)): decode the lossless native container (shared
   cwv_cwi.c) and upload the RGBA8 pixels - same seam as the BMP path. */
int   cwv_tex_width(void *tex);
int   cwv_tex_height(void *tex);
void  cwv_tex_set_filter(void *tex, int min_filter, int mag_filter);
void  cwv_tex_free(void *tex);

/* ---- fonts ---------------------------------------------------------- */
/* pgf = the console's built-in bitmap font; font = a loaded TTF/OTF. */
void *cwv_pgf_load_default(void);
void *cwv_font_load_file(const char *path);
void  cwv_pgf_free(void *pgf);
void  cwv_font_free(void *font);
int   cwv_font_text_width(void *font, unsigned size, const char *utf8);
int   cwv_pgf_text_width(void *pgf, float size, const char *utf8);
int   cwv_font_draw_text(void *font, float x, float y, unsigned color,
                         unsigned size, const char *utf8);
int   cwv_pgf_draw_text(void *pgf, float x, float y, unsigned color,
                        float size, const char *utf8);

/* ---- primitives ------------------------------------------------------ */
void cwv_clear_screen(void);
/* Per-element scroll regions (2026-10-06 (75)): restrict every subsequent
 * draw (rects, circles, textures, text) to the screen-space rect until
 * the next call.  cwv_clip_disable() restores full-screen drawing.  The
 * shared engine drives these for CSS overflow:auto/scroll boxes that
 * joined the focus (tab) chain; ports with a native scissor
 * (vita2d, SDL2, GDI) clip pixel-exact, and pure-software ports
 * intersect rect blits and coarse-cull the rest (documented). */
void cwv_set_clip(float x, float y, float w, float h);
void cwv_clip_disable(void);
void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color);
void cwv_draw_fill_circle(float x, float y, float radius, unsigned color);
void cwv_draw_texture(void *tex, float x, float y);
void cwv_draw_texture_scale(void *tex, float x, float y,
                            float xs, float ys);
void cwv_draw_texture_part(void *tex, float x, float y,
                           float tx, float ty, float tw, float th);
void cwv_draw_texture_part_scale(void *tex, float x, float y,
                                 float tx, float ty, float tw, float th,
                                 float xs, float ys);

/* ---- on-screen keyboard (IME) --------------------------------------- */
/* Opens the keyboard pre-filled with `initial` (UTF-16).  The console
 * types into `buffer`.  Returns -1 if it could not be opened. */
int cwv_ime_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                 int password, int multiline,
                 cwv_utf16 *buffer, int buffer_len);
/* Called every frame: 0 = still open, 1 = finished with Enter (buffer
 * holds the text), 2 = finished otherwise.  Closes the dialog itself. */
int cwv_ime_poll(cwv_utf16 *buffer, int buffer_len);


/* ---- console services (host-level; one implementation per console) ----
 * init/shutdown: sysmodule loads, app-util bring-up, network stack setup.
 * open_browser: hand a URL to the console's native/external browser;
 *               returns >=0 launched, -1 unavailable or failed.
 * fetch: blocking http(s) GET for the page fetch() hook.  Returns the
 *        body length (>=0) or -1 on ANY failure; must never crash.
 */
void cwv_console_init(void);
void cwv_console_shutdown(void);
int  cwv_console_open_browser(const char *url);
int  cwv_console_fetch(const char *url, char *body, size_t cap);
/* Byte-exact blocking http(s) GET for remote resources (images,
 * @font-face fonts, linked CSS) loaded by the shared renderer's
 * resource loader.  Unlike cwv_console_fetch() above, the body is
 * NOT NUL-terminated/treated as text - binary data may contain NUL
 * bytes, so the exact byte count is reported via *out_len (never
 * more than cap).  Returns 0 on success, -1 on ANY failure (no
 * network, bad URL, timeout, etc); must never crash. */
int  cwv_console_fetch_bin(const char *url, unsigned char *buf, size_t cap,
                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CWV_PLATFORM_H */
