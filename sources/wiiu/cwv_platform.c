/* cwv_platform.c - Wii U implementation of the shared engine's platform
 * seam (sources/shared/sources/core/cwv_platform.h), built on wut + SDL2
 * (wiiu-sdl2, wiiu-sdl2_image for texture decode, wiiu-sdl2_ttf for
 * fonts).
 *
 * STATUS (engine-first port, not yet hardware-verified):
 *   DONE   files (romfs mapping), textures (PNG/JPEG/BMP), draw
 *          primitives, scaled/part texture draws, clear, fonts
 *          (bundled assets/font.ttf via SDL2_ttf), app loop
 *          (sources/wiiu/main.c), IME (wut's NATIVE nn::swkbd system
 *          keyboard - sources/wiiu/cwv_ime_swkbd.cpp), HTTP fetch
 *          (sources/wiiu/cwv_fetch_nets2.cpp, nsysnet sockets)
 *   TODO   the hardware check of the swkbd GX2 draw mixing; error dialogs
 *          in that shell should use wut's nn::erreula applet, not a
 *          hand-drawn dialog (see README-WIIU.md's native-first policy).
 *
 * Path mapping (2026-09-23 (33), user direction): app0: is a REAL Vita
 * device and stays Vita-only.  The Wii U loads everything from the
 * FOLDER THE PACKAGE SITS IN - .../exampledir/s.rpx reads its web
 * content from .../exampledir/web/ - so the engine's URL root is set
 * to that folder at boot (cwv_platform_set_app_root) and NO content
 * is packaged inside the .rpx/.wuhb itself.  The root comes from the
 * launcher's argv[0] (HBL, Aroma's EnvironmentLoader and Cemu pass
 * the package's full path), validated against the web/ folder; the
 * fallback probe is the canonical SD layout:
 *   fs:/vol/external01/wiiu/apps/CWebView   (SD / HBL / Cemu; Cemu
 *                        emulates the SD card via <Cemu>/mlc01/sdcard/)
 * The fs:/vol/content probe (BUG 43, 2026-09-26) is a LAST-RESORT
 * Cemu case only, AFTER argv[0]/cwd/canonical/SD-scan all miss: Cemu's
 * "File -> Load executable" mounts the loaded .rpx's OWN FOLDER as the
 * title content dir, so the web/ folder NEXT TO the RPX (the very
 * folder this whole scheme serves) is reachable at fs:/vol/content/.
 * That is still NOT "loading from the RPX" (33 rejected content inside
 * the package) - the folder is external, reached through the launcher
 * mount, and only probed when nothing else matched.
 * The previous "rom:/" mapping was a nonexistent device: wut's only
 * devoptab is "fs" (SD at fs:/vol/external01, per wutdevoptab), so
 * rom:/ paths fell through to FSA as invalid root-relative paths -
 * the initial page could never load on ANY target, and the malformed
 * FSA path is what faulted inside Cemu (2026-09-20 crash dump:
 * Cemu.exe AV on a garbage PPC-derived pointer right after startup).
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <coreinit/debug.h>   /* OSReport: font trace lands in Cemu's log.txt */
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "core/cwv_platform.h"
#include "core/cwv_cwi.h"
#include "cwv_font8x8.h"

static SDL_Renderer *g_rend = NULL;   /* set by the wiiu main loop */
void cwv_wiiu_set_renderer(SDL_Renderer *r) { g_rend = r; }

/* ---- virtual canvas fit (2026-09-21 (29)) -----------------------------
 * The Wii U GamePad is 854x480 while the Vita (the engine's reference
 * screen) is 960x544 - 11-12% fewer pixels in EACH dimension and a
 * slightly different aspect (1.779 vs 1.765).  Laying out directly at
 * 854x480 would wrap every line ~11% earlier and draw everything ~12%
 * larger relative to the screen than the Vita look this engine is
 * calibrated for.  Instead the engine runs on a VIRTUAL 960x544 canvas
 * (main.c creates the host at those dimensions): identical wrapping,
 * identical line metrics, identical relative proportions to the Vita.
 * THIS file then fits the virtual canvas onto the real framebuffer at
 * the seam boundary, which keeps every shared-engine calculation
 * (scroll ranges, culling, hit-testing, wrap widths) consistent because
 * the engine never sees a device pixel:
 *   x and widths scale by WIIU_FIT_X = 854/960 (exactly full width)
 *   y and heights scale by WIIU_FIT_Y = 480/544 (exactly full height)
 * 960*WIIU_FIT_X = 854 and 544*WIIU_FIT_Y = 480 exactly, so there is no
 * letterbox and no crop - the 0.8% non-uniformity between the two axes
 * IS the aspect-ratio correction (imperceptible in text: fonts render
 * and MEASURE at the same fit-scaled size, so wrapping stays exact).
 * Font pixel sizes scale by the vertical factor (WIIU_FONT_FIT), both
 * at measure time and draw time, so the two can never disagree.
 * Pointer input converts the other way (device -> virtual) so the
 * engine's hit-testing still sees virtual canvas coordinates. */
#define WIIU_VIRTUAL_W 960.0f
#define WIIU_VIRTUAL_H 544.0f
#define WIIU_FIT_X   (854.0f / WIIU_VIRTUAL_W)
#define WIIU_FIT_Y   (480.0f / WIIU_VIRTUAL_H)
#define WIIU_FONT_FIT WIIU_FIT_Y

/* Device (GamePad) touch/mouse pixel -> virtual canvas pixel.  The
 * engine hit-tests in virtual coordinates; main.c calls this before
 * feeding cwv_host_pointer_event. */
void cwv_wiiu_fit_pointer(int *x, int *y) {
    if (!x || !y) return;
    *x = (int)((float)*x / WIIU_FIT_X + 0.5f);
    *y = (int)((float)*y / WIIU_FIT_Y + 0.5f);
}

/* ---- files ---------------------------------------------------------- */
/* Content root: the folder the package (rpx/.wuhb) sits in.  argv[0]
 * supplies it when the launcher passes the package path; the probe is
 * the canonical SD fallback (see the header comment). */
static char g_app_root[256] = "";
static int  g_app_root_probed = 0;
static char g_argv_root[256] = "";
static char g_root_source[80] = "";   /* argv[0] / canonical / scan: ... */

#ifndef CWV_WIIU_APP_ROOT
#define CWV_WIIU_APP_ROOT "fs:/vol/external01/wiiu/apps/CWebView"
#endif

/* The launcher passes the package's full path as argv[0]; the folder
 * it sits in IS the app root (.../exampledir/s.rpx -> .../exampledir).
 * Empty on anything unusable - the probe below then applies. */
static char g_argv0[256] = "";   /* raw launcher argument, diagnostics */

void cwv_wiiu_app_root_init(const char *argv0)
{
    const char *slash;
    size_t n;
    g_argv_root[0] = 0;
    if (!argv0) return;
    snprintf(g_argv0, sizeof(g_argv0), "%s", argv0);
    slash = strrchr(argv0, '/');
    if (!slash || slash == argv0) return;
    n = (size_t)(slash - argv0);
    if (n == 0 || n >= sizeof(g_argv_root)) return;
    memcpy(g_argv_root, argv0, n);
    g_argv_root[n] = 0;
}

const char *cwv_wiiu_app_root_argv0(void) { return g_argv0; }

/* A root "exists" for us if the staged web/ folder is there.  Two
 * probes: stat() on the directory, then fopen() on the index page,
 * because devoptab stat coverage can lag open on some backends. */
static int root_matches(const char *root) {
    char p[300];
    struct stat st;
    snprintf(p, sizeof(p), "%s/web", root);
    if (stat(p, &st) == 0) return 1;
    snprintf(p, sizeof(p), "%s/web/index.html", root);
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* defined below (the file seam lives later in this file) */
long cwv_file_size(const char *path);

/* Our own package inside a scanned folder: the rpx/wuhb we build is
 * named CWebView.* - a folder with web/ AND one of those is this
 * app's package, whatever the folder is called. */
static int folder_has_package(const char *dir) {
    char p[350];
    snprintf(p, sizeof(p), "%s/CWebView.rpx", dir);
    if (cwv_file_size(p) > 0) return 1;
    snprintf(p, sizeof(p), "%s/CWebView.wuhb", dir);
    return cwv_file_size(p) > 0;
}

/* Last-resort scan: some launchers (Aroma's homebrew-on-menu,
 * forwarders, Cemu) do not pass the package path in argv[0], but the
 * standard layout every Wii U homebrew uses is sd:/wiiu/apps/<any
 * folder name>/ - so look for OUR package there. */
static void scan_apps_dir(const char *apps_dir) {
    DIR *d;
    struct dirent *e;
    /* 2026-09-26 (46), hardware report: gcc -Wformat-truncation fired on
     * the snprintf join (apps_dir + '/' + d_name can exceed 256).  The
     * join is now a memcpy build into an oversized buffer - it can never
     * truncate - and candidates that do not fit g_app_root are skipped
     * with an explicit length check (a silently truncated root would be
     * a wrong path, not a short one). */
    char cand[514];   /* 255 + '/' + 255 + NUL: provably sufficient */
    size_t alen = strlen(apps_dir);
    /* layout A: a .wuhb file IN the apps dir with web/ beside it
     * (.../wiiu/apps/CWebView.wuhb + .../wiiu/apps/web/) */
    if (root_matches(apps_dir) && folder_has_package(apps_dir)) {
        snprintf(g_app_root, sizeof(g_app_root), "%s", apps_dir);
        snprintf(g_root_source, sizeof(g_root_source),
                 "scan: %s", apps_dir);
        return;
    }
    /* layout B: a package FOLDER (.../wiiu/apps/<any name>/) */
    d = opendir(apps_dir);
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        size_t nlen;
        if (e->d_name[0] == '.') continue;
        nlen = strlen(e->d_name);
        if (alen + 1 + nlen + 1 > sizeof(cand)) continue;   /* can't happen */
        memcpy(cand, apps_dir, alen);
        cand[alen] = '/';
        memcpy(cand + alen + 1, e->d_name, nlen + 1);
        if (root_matches(cand) && folder_has_package(cand)
            && alen + 1 + nlen < sizeof(g_app_root)) {
            memcpy(g_app_root, cand, alen + 1 + nlen + 1);
            /* precision bounds the note so gcc -Wformat-truncation is
             * satisfied on every path (g_root_source is 80) */
            snprintf(g_root_source, sizeof(g_root_source),
                     "scan: %.73s", apps_dir);
            break;
        }
    }
    closedir(d);
}

const char *cwv_wiiu_app_root(void) {
    /* argv[0]-derived folder first, but only if the web/ folder is
     * really there (a launcher passing anything else loses). */
    if (g_argv_root[0] && root_matches(g_argv_root)) {
        if (!g_root_source[0])
            snprintf(g_root_source, sizeof(g_root_source), "argv[0]");
        return g_argv_root;
    }
    if (!g_app_root_probed) {
        g_app_root_probed = 1;
        /* the Homebrew Launcher (and some forwarders) chdir() into the
         * app folder before launching instead of passing argv[0] */
        {
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) && root_matches(cwd)) {
                snprintf(g_app_root, sizeof(g_app_root), "%s", cwd);
                snprintf(g_root_source, sizeof(g_root_source), "cwd");
                return g_app_root;
            }
        }
        if (root_matches(CWV_WIIU_APP_ROOT)) {
            snprintf(g_app_root, sizeof(g_app_root), "%s", CWV_WIIU_APP_ROOT);
            snprintf(g_root_source, sizeof(g_root_source), "canonical");
        } else {
            /* the folder may be named anything the user chose */
            scan_apps_dir("fs:/vol/external01/wiiu/apps");
        }
        /* BUG 43 (Cemu report, 2026-09-26): "File -> Load executable" on
         * a bare .rpx gives the process NO usable argv[0]/cwd and the SD
         * scan finds nothing (nothing was copied to mlc01/sdcard) - the
         * app then booted with the engine's Vita default URL root and
         * showed the "app0:/web/" error even though web/ sat right next
         * to the RPX.  Cemu mounts the loaded RPX's OWN FOLDER as the
         * title's content dir, so that external web/ folder is visible
         * at fs:/vol/content/ (and /vol/code/).  This is NOT "loading
         * from the RPX" (33 rejected content INSIDE the package) - it
         * is the same external next-to-the-package folder, reached
         * through the launcher's own mount. */
        if (!g_app_root[0] && root_matches("fs:/vol/content")) {
            snprintf(g_app_root, sizeof(g_app_root), "fs:/vol/content");
            snprintf(g_root_source, sizeof(g_root_source),
                     "vol/content (Cemu rpx folder)");
        }
        if (!g_app_root[0] && root_matches("fs:/vol/code")) {
            snprintf(g_app_root, sizeof(g_app_root), "fs:/vol/code");
            snprintf(g_root_source, sizeof(g_root_source),
                     "vol/code (Cemu rpx folder)");
        }
        /* A WUHB launched through the Homebrew Launcher mounts its
         * bundled content at app0:/ - probe that too (our WUHB is
         * code-only since (36), so this only matches if web/ was packed
         * INTO a wuhb by hand). */
        if (!g_app_root[0] && root_matches("app0:")) {
            snprintf(g_app_root, sizeof(g_app_root), "app0:");
            snprintf(g_root_source, sizeof(g_root_source),
                     "app0: (wuhb content)");
        }
    }
    return g_app_root;   /* "" = nothing matched (caller shows a hint) */
}

/* Boot diagnostics: what the launcher passed and which root won. */
const char *cwv_wiiu_app_root_source(void) { return g_root_source; }

static void map_path(const char *in, char *out, size_t cap) {
    /* app0: is Vita-only (see the header comment): the engine works in
     * REAL fs: paths - the folder the package sits in.  Defense in depth
     * (BUG 43): when a root WAS found, any app0:/-prefixed path (the
     * engine's Vita-default URL root, or a page that hardcodes one) is
     * rewritten onto that root instead of failing against a device the
     * Wii U never registered.  With no root the path passes through
     * untouched and the file open reports honestly. */
    if (!strncmp(in, "app0:/", 6) && g_app_root[0]) {
        snprintf(out, cap, "%s/%s", g_app_root, in + 6);
        return;
    }
    snprintf(out, cap, "%s", in);
}
int cwv_file_open_read(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    return (int)(long)f;   /* opaque fd: the pointer */
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
void *cwv_tex_load_png_file(const char *path)  { return cwv_tex_load_jpeg_file(path); }
void *cwv_tex_load_jpeg_file(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    SDL_Surface *s = IMG_Load(p);
    return s ? SDL_CreateTextureFromSurface(g_rend, s) : NULL;
}
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len) {
    /* len is REQUIRED here: SDL_RWFromConstMem(buf, -1) would treat the
     * buffer as a C string and truncate at the first NUL byte - PNG
     * chunk headers are almost all 0x00, so the decoder would only ever
     * see the 8-byte signature.  This is why every <img> decoded to
     * "broken" on the Wii U even after the file itself loaded fine. */
    SDL_RWops *rw = len ? SDL_RWFromConstMem(buf, (int)len) : NULL;
    SDL_Surface *s = rw ? IMG_LoadPNG_RW(rw) : NULL;
    if (rw) rw->close(rw);
    return s ? SDL_CreateTextureFromSurface(g_rend, s) : NULL;
}
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len) {
    SDL_RWops *rw = SDL_RWFromMem(buf, (int)len);
    SDL_Surface *s = rw ? IMG_LoadJPG_RW(rw) : NULL;
    if (rw) rw->close(rw);
    return s ? SDL_CreateTextureFromSurface(g_rend, s) : NULL;
}
/* CWI (2026-10-05 (71)): shared lossless decode -> SDL texture.
 * RGBA32 is the byte order the decoder produces (R,G,B,A interleaved). */
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len) {
    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    SDL_Texture *t;
    if (!cwv_cwi_decode((const unsigned char *)buf, (size_t)len, &rgba, &w, &h))
        return NULL;
    t = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
    if (t)
        SDL_UpdateTexture(t, NULL, rgba, (int)w * 4);
    free(rgba);
    return t;
}
void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int w, int h) {
    SDL_Texture *t;
    if (!rgba || w <= 0 || h <= 0 || w > 1024 || h > 768) return NULL;
    t = SDL_CreateTexture(g_rend, SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (t && SDL_UpdateTexture(t, NULL, rgba, w * 4) != 0) {
        SDL_DestroyTexture(t);
        return NULL;
    }
    return t;
}
int cwv_tex_update_rgba_buffer(void *tex, const unsigned char *rgba, int w, int h) {
    SDL_Texture *t = (SDL_Texture *)tex;
    int tw = 0, th = 0;
    if (!t || !rgba || w <= 0 || h <= 0) return 0;
    if (SDL_QueryTexture(t, NULL, NULL, &tw, &th) != 0 || tw != w || th != h) return 0;
    return SDL_UpdateTexture(t, NULL, rgba, w * 4) == 0;
}
void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len) {
    SDL_RWops *rw = len ? SDL_RWFromConstMem(buf, (int)len) : NULL;
    SDL_Surface *s = rw ? IMG_LoadBMP_RW(rw) : NULL;
    if (rw) rw->close(rw);
    return s ? SDL_CreateTextureFromSurface(g_rend, s) : NULL;
}
int cwv_tex_width(void *tex) {
    int w = 0; if (tex) SDL_QueryTexture((SDL_Texture *)tex, NULL, NULL, &w, NULL);
    return w;
}
int cwv_tex_height(void *tex) {
    int h = 0; if (tex) SDL_QueryTexture((SDL_Texture *)tex, NULL, NULL, NULL, &h);
    return h;
}
void cwv_tex_set_filter(void *tex, int min_f, int mag_f) {
    (void)min_f;
    SDL_SetTextureScaleMode((SDL_Texture *)tex,
        mag_f == CWV_TEX_FILTER_POINT ? SDL_ScaleModeNearest
                                       : SDL_ScaleModeLinear);
}
void cwv_tex_free(void *tex) { if (tex) SDL_DestroyTexture((SDL_Texture *)tex); }

/* ---- fonts ---------------------------------------------------------- */
/* The Wii U exposes no default font to homebrew, so the port bundles
 * one: a libre TTF at app_root/assets/font.ttf (packed into the WUHB
 * next to the web/ files).  If the file is absent every handle comes
 * back NULL / every draw returns 0 and the engine draws text as empty
 * boxes, same as a failed font load on any console - never a crash.
 *
 * 2026-09-20 (26) FIX "no text rendered on Wii U": the shared engine
 * draws all ordinary page text through the SYSTEM-FONT seam
 * (cwv_pgf_*: r->system_pgf - cwv_backend.c), while the earlier port
 * only implemented the @font-face seam (cwv_font_*) and stubbed the
 * pgf functions as "no default font yet" - so every page text draw
 * returned 0 and NOTHING rendered (Vita was unaffected because its
 * cwv_pgf_* maps to the real vita2d PGF font).  Both seams now share
 * one TTF implementation below.
 *
 * Size handling: every measure/draw call carries the pixel size the
 * engine wants (node font-size * zoom).  TTF_OpenFont bakes the size
 * into the handle, so each (file, size) pair gets its own cached
 * TTF_Font - opened lazily, ring-evicted past WIIU_FONT_CACHE sizes.
 * Measure and draw round through the same clamp, so layout width
 * always matches the rendered glyph size at the same zoom.
 *
 * TTF_Init: SDL2_ttf needs explicit init before TTF_OpenFont; it was
 * never called, so even the webfont/boot-screen path was dead on the
 * console.  Done lazily once, failure disables all text (NULL/0). */

#define WIIU_FONT_SIZE_MIN  4
#define WIIU_FONT_SIZE_MAX  128
#define WIIU_FONT_CACHE     16    /* distinct sizes kept open per file */

/* 2026-09-20 (27) FIX "text tiny/blurry/off-center on Wii U":
 * font_handle() takes a literal PIXEL size - that IS the contract for
 * the @font-face seam (cwv_font_draw_text/cwv_font_text_width): the
 * shared engine already premultiplies by 32 before calling it
 * (cwv_html.c: "unsigned sz = (unsigned)(n->font_size * 32.0f)").
 * The SYSTEM-FONT seam (cwv_pgf_draw_text/cwv_pgf_text_width) is a
 * DIFFERENT contract: on Vita it is vita2d_pgf's "scale" parameter, a
 * unitless multiplier of vita2d's fixed-size default PGF glyph (NOT
 * pixels) - the shared engine passes it raw (cwv_backend.c:
 * "cwv_pgf_draw_text(r->system_pgf, ..., n->font_size * z, ...)",
 * no *32).  Every CSS font-size in the engine lives in this same
 * "renderer unit" space (BODY_FONT_SIZE=0.75, css_prop_font_size
 * clamps to 0.1..8.0) - css_font_size()'s own comment gives the
 * calibration: "the renderer measures fonts at 32 px per unit" - that
 * constant is only used by the @font-face (cwv_font_*) draw sites, which
 * premultiply BEFORE calling this file, so it stays untouched.
 * The previous Wii U port fed the RAW 0.1-8.0 scale straight into
 * TTF_OpenFont as a point size (clamped up to a 4px floor) - hence a
 * ~4px font: tiny, blurry once anti-aliased, and visually "off
 * center" only because the centering math (line_height() etc.) is
 * calibrated for a real ~21px line, not a 4px one.  WIIU_PGF_PX_PER_UNIT
 * converts the system-font seam's renderer-unit scale to pixels before
 * it reaches TTF_OpenFont.  2026-09-21 (29) RECALIBRATION: the ground
 * truth is the VITA (the reference port).  Screenshot measurement of
 * the Vita PGF output (index.html): h1 (1.5u) inks 21px, h2 (1.125u)
 * inks 16px, body (0.75u) inks 12px - linear at ~16px em per renderer
 * unit (h1 23px em, h2 17.6px em, body 13.2px em), so 16 px/unit is
 * the Vita-parity calibration.  (28) used 28 px/unit to match the
 * engine's 28px-per-unit LINE BOXES; that made every glyph ~75% larger
 * than the Vita look this engine targets - the "text much too large"
 * of the (28) Wii U screenshots.  Line boxes are taller than the em
 * on the Vita too: 0.75u renders ~13px of ink inside a 21px line box.
 * With the virtual canvas (see WIIU_FIT_*), the drawn em per unit is
 * 16 * WIIU_FONT_FIT (~14.1px), i.e. the Vita's 16px scaled by the
 * screen-size ratio - proportions match the Vita exactly.
 * pixels.  (No Wii U system-font substitute exists to use instead: wut
 * exposes NO coreinit font API - grep of the wut include tree confirms
 * coreinit/ has no font header, and the real Cafe system font
 * (FontRasterizer.rpl / nn_font) is Nintendo-private, only reachable
 * by binary patching as unofficial plugins like "System Font Replacer"
 * do, not a stable SDK call - so the bundled TTF remains the
 * system-font stand-in, now scaled correctly.) */
#define WIIU_PGF_PX_PER_UNIT 16.0f

typedef struct wiiu_font {
    char      path[512];                        /* resolved, openable */
    TTF_Font *cached[WIIU_FONT_CACHE];
    int       sizes[WIIU_FONT_CACHE];
    int       next;                             /* ring eviction slot */
    int       dead;                             /* file gone: stop retrying */
    int       bitmap;                           /* 8x8 fallback font */
} wiiu_font;

static int g_ttf_ready = 0;

static int size_clamped(float size) {
    int s = (int)(size + 0.5f);
    if (s < WIIU_FONT_SIZE_MIN)  s = WIIU_FONT_SIZE_MIN;
    if (s > WIIU_FONT_SIZE_MAX)  s = WIIU_FONT_SIZE_MAX;
    return s;
}

/* SDL_ttf rasterization used to create and destroy a GPU texture for EVERY
 * string on EVERY frame.  A busy page repeatedly allocates thousands of
 * textures per second, making color/presentation unstable on the Cafe GPU.
 * Cache short rendered strings by face+color+text.  Fonts can be evicted by
 * the size ring or freed at navigation, so those paths invalidate entries. */
#define WIIU_TEXT_CACHE_COUNT 64
#define WIIU_TEXT_KEY_BYTES 160
typedef struct {
    TTF_Font *font;
    SDL_Texture *tex;
    unsigned color;
    unsigned long stamp;
    int width, height, ascent;
    char text[WIIU_TEXT_KEY_BYTES];
} wiiu_text_cache_entry;
static wiiu_text_cache_entry s_text_cache[WIIU_TEXT_CACHE_COUNT];
static unsigned long s_text_clock;
static void text_cache_invalidate(TTF_Font *font)
{
    for(int i=0;i<WIIU_TEXT_CACHE_COUNT;i++)
        if(s_text_cache[i].tex && (!font || s_text_cache[i].font==font)){
            SDL_DestroyTexture(s_text_cache[i].tex);
            memset(&s_text_cache[i],0,sizeof(s_text_cache[i]));
        }
}
void cwv_wiiu_text_cache_clear(void) { text_cache_invalidate(NULL); }

static TTF_Font *font_handle(wiiu_font *wf, float size) {
    if (!wf || wf->dead || wf->bitmap) return NULL;
    if (!g_ttf_ready) {
        g_ttf_ready = (TTF_Init() == 0);
        if (!g_ttf_ready) return NULL;
    }
    int s = size_clamped(size);
    for (int i = 0; i < WIIU_FONT_CACHE; i++)
        if (wf->cached[i] && wf->sizes[i] == s) return wf->cached[i];
    TTF_Font *f = TTF_OpenFont(wf->path, s);
    if (!f) {
        /* Distinguish "bad size" from "file unreadable": only the
         * latter is permanent - mark dead so a missing font does not
         * retry (and re-fail) on every frame. */
        FILE *t = fopen(wf->path, "rb");
        if (!t) { wf->dead = 1; return NULL; }
        fclose(t);
        return NULL;
    }
    int slot = wf->next;
    wf->next = (wf->next + 1) % WIIU_FONT_CACHE;
    if (wf->cached[slot]) {
        text_cache_invalidate(wf->cached[slot]);
        TTF_CloseFont(wf->cached[slot]);
    }
    wf->cached[slot] = f;
    wf->sizes[slot] = s;
    return f;
}

static wiiu_font *font_new(const char *resolved_path) {
    FILE *t = fopen(resolved_path, "rb");
    if (!t) return NULL;
    fclose(t);
    wiiu_font *wf = (wiiu_font *)calloc(1, sizeof(wiiu_font));
    if (wf) snprintf(wf->path, sizeof(wf->path), "%s", resolved_path);
    return wf;
}

/* Engine handles: webfonts each get their own wiiu_font; the system
 * font is ONE singleton owned for the app's lifetime. */
static wiiu_font *s_system_font = NULL;

void *cwv_font_load_file(const char *path) {
    char p[512]; map_path(path, p, sizeof(p));
    return font_new(p);
}
/* defined later in the fonts section; forward-declared because the
 * default font falls back to it when no file can be opened */
static wiiu_font *g_bitmap_font;
static wiiu_font *bitmap_font(void);

void *cwv_pgf_load_default(void) {
    if (!s_system_font) {
        /* 2026-09-23 (35): this STILL asked for the Vita-only app0:
         * path, so the menu font was ALWAYS NULL on the Wii U since
         * the app-root change - CWMenu drew shapes with no text.
         * Now: the package folder's font, else the built-in 8x8. */
        char p[512];
        snprintf(p, sizeof(p), "%s/assets/font.ttf", cwv_wiiu_app_root());
        s_system_font = font_new(p);
        if (!s_system_font) {
            s_system_font = (wiiu_font *)bitmap_font();
            OSReport("CWV: default font missing (%s) - built-in 8x8 in use\n", p);
        } else {
            OSReport("CWV: default font loaded (%s)\n", p);
        }
    }
    return s_system_font;
}
void cwv_pgf_free(void *pgf) { (void)pgf; }   /* singleton: lives with the app */
void cwv_font_free(void *font) {
    wiiu_font *wf = (wiiu_font *)font;
    if (!wf || wf == s_system_font || wf == g_bitmap_font) return;
    for (int i = 0; i < WIIU_FONT_CACHE; i++)
        if (wf->cached[i]) {
            text_cache_invalidate(wf->cached[i]);
            TTF_CloseFont(wf->cached[i]);
        }
    free(wf);
}
static int draw_utf8(TTF_Font *f, float x, float y, unsigned color, const char *utf8) {
    size_t len;
    int candidate=-1, cacheable;
    wiiu_text_cache_entry *e=NULL;
    SDL_Surface *surface=NULL;
    SDL_Texture *texture=NULL;
    int width=0,height=0,ascent;
    if (!f || !g_rend || !utf8 || !*utf8) return 0;
    len=strlen(utf8);
    cacheable=(len<WIIU_TEXT_KEY_BYTES);
    if(cacheable){
        for(int i=0;i<WIIU_TEXT_CACHE_COUNT;i++){
            wiiu_text_cache_entry *item=&s_text_cache[i];
            if(item->tex && item->font==f && item->color==color &&
               !strcmp(item->text,utf8)){e=item;break;}
            if(candidate<0 || !item->tex ||
               (s_text_cache[candidate].tex && item->stamp<s_text_cache[candidate].stamp))
                candidate=i;
        }
    }
    if(e){
        texture=e->tex;width=e->width;height=e->height;ascent=e->ascent;
        e->stamp=++s_text_clock;
    }else{
        SDL_Color c = { color & 0xFF, (color >> 8) & 0xFF,
                        (color >> 16) & 0xFF, (color >> 24) & 0xFF };
        surface=TTF_RenderUTF8_Blended(f,utf8,c);
        if(!surface)return 0;
        width=surface->w;height=surface->h;ascent=TTF_FontAscent(f);
        texture=SDL_CreateTextureFromSurface(g_rend,surface);
        SDL_FreeSurface(surface);
        if(!texture)return 0;
        /* Blended glyph surfaces are alpha textures; state this explicitly
         * instead of relying on SDL's format inference for Wii U. */
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        if(cacheable && width<=512 && height<=64 && candidate>=0){
            e=&s_text_cache[candidate];
            if(e->tex)SDL_DestroyTexture(e->tex);
            e->tex=texture;e->font=f;e->color=color;
            e->width=width;e->height=height;e->ascent=ascent;
            e->stamp=++s_text_clock;
            memcpy(e->text,utf8,len+1);
        }
    }
    /* SDL_ttf surfaces start at the TOP; engine sites pass a baseline. */
    SDL_FRect dst={x*WIIU_FIT_X,y*WIIU_FIT_Y-(float)ascent,
                   (float)width,(float)height};
    SDL_RenderCopyF(g_rend,texture,NULL,&dst);
    if(!e)SDL_DestroyTexture(texture);
    return 1;
}
/* ---- 8x8 bitmap fallback font --------------------------------------
 * Bug report 2026-09-23 (35): "no text on start" - with no app root the
 * TTF cannot load (and pgf_load_default still asked for the Vita-only
 * app0:), so the boot screen and CWMenu drew shapes with NO text and
 * the failure was undiagnosable on hardware.  This built-in font keeps
 * every diagnostic readable even when no file can be opened at all.
 * Coordinates and the cell size are VIRTUAL px here; cwv_draw_rectangle
 * applies the fit factor, exactly like the TTF blit path.  */
void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color);

static int bitmap_scale(float cell_virtual_px) {
    int sc = (int)(cell_virtual_px / 8.0f + 0.5f);
    return sc < 1 ? 1 : sc;
}

static wiiu_font *bitmap_font(void) {
    if (!g_bitmap_font) {
        g_bitmap_font = (wiiu_font *)calloc(1, sizeof(wiiu_font));
        if (g_bitmap_font) {
            g_bitmap_font->bitmap = 1;
            snprintf(g_bitmap_font->path, sizeof(g_bitmap_font->path),
                    "<builtin 8x8>");
        }
    }
    return g_bitmap_font;
}

static int bitmap_text_width(int sc, const char *utf8) {
    if (!utf8) return 0;
    return (int)strlen(utf8) * 8 * sc;
}

static int bitmap_draw_text(float x, float y, unsigned color, int sc,
                            const char *utf8) {
    const unsigned char *p;
    int n = 0;
    float top = y - 8.0f * sc;   /* engine sites pass a baseline */
    if (!utf8) return 0;
    for (p = (const unsigned char *)utf8; *p && n < 200; p++, n++) {
        int c = *p - 32, row, col;
        if (c < 0 || c >= 95) c = '?' - 32;
        for (row = 0; row < 8; row++) {
            unsigned bits = cwv_font8x8[c][row];
            if (!bits) continue;
            for (col = 0; col < 8; col++)
                if (bits & (1u << col))
                    cwv_draw_rectangle(x + (float)(col * sc),
                                       top + (float)(row * sc),
                                       (float)sc, (float)sc, color);
        }
        x += 8.0f * sc;
    }
    return n;
}

int cwv_font_text_width(void *font, unsigned size, const char *utf8) {
    wiiu_font *wf = (wiiu_font *)font;
    if (wf && wf->bitmap)
        return bitmap_text_width(bitmap_scale((float)size), utf8);
    /* `size` arrives pre-multiplied by 32 px/unit (the @font-face
       contract, see the block above); the fit factor scales it to the
       device the SAME way the draw call below does, so wrapping is
       measured against exactly what is drawn. */
    TTF_Font *f = font_handle((wiiu_font *)font,
                              (float)size * WIIU_FONT_FIT);
    if (!f) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, utf8, &w, &h);
    /* TTF sizes are framebuffer pixels; layout/hit testing use virtual x
       coordinates. The draw path keeps glyphs at their intended physical
       width, so convert the measured width back through the horizontal fit. */
    return (int)((float)w / WIIU_FIT_X + 0.5f);
}
int cwv_pgf_text_width(void *pgf, float size, const char *utf8) {
    wiiu_font *wf = (wiiu_font *)pgf;
    if (wf && wf->bitmap)
        return bitmap_text_width(
            bitmap_scale(size * WIIU_PGF_PX_PER_UNIT), utf8);
    /* Fit-scaled to match the draw call below exactly (wrapping must
       measure what will be drawn on the device). */
    TTF_Font *f = font_handle((wiiu_font *)pgf,
                              size * WIIU_PGF_PX_PER_UNIT * WIIU_FONT_FIT);
    if (!f) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, utf8, &w, &h);
    /* TTF reports physical pixels; the shared layout works in virtual
       canvas units. Compensate for horizontal fit so button/content boxes
       contain the glyphs when they are mapped to the framebuffer. */
    return (int)((float)w / WIIU_FIT_X + 0.5f);
}
int cwv_font_draw_text(void *font, float x, float y, unsigned color,
                       unsigned size, const char *utf8) {
    wiiu_font *wf = (wiiu_font *)font;
    if (wf && wf->bitmap)
        return bitmap_draw_text(x, y, color,
                                bitmap_scale((float)size), utf8);
    TTF_Font *f = font_handle((wiiu_font *)font,
                              (float)size * WIIU_FONT_FIT);
    return f ? draw_utf8(f, x, y, color, utf8) : 0;
}
int cwv_pgf_draw_text(void *pgf, float x, float y, unsigned color,
                      float size, const char *utf8) {
    wiiu_font *wf = (wiiu_font *)pgf;
    if (wf && wf->bitmap)
        return bitmap_draw_text(x, y, color,
            bitmap_scale(size * WIIU_PGF_PX_PER_UNIT), utf8);
    TTF_Font *f = font_handle((wiiu_font *)pgf,
                              size * WIIU_PGF_PX_PER_UNIT * WIIU_FONT_FIT);
    return f ? draw_utf8(f, x, y, color, utf8) : 0;
}

/* ---- primitives ------------------------------------------------------ */
void cwv_clear_screen(void) {
    if (!g_rend) return;
    SDL_SetRenderDrawColor(g_rend, 0, 0, 0, 255);
    SDL_RenderClear(g_rend);
}
static SDL_Color to_sdl(unsigned color) {
    SDL_Color c = { color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF };
    return c;
}
/* All coordinates arriving here are VIRTUAL CANVAS pixels; the fit
 * factors (see the virtual-canvas block above) map them onto the
 * 854x480 framebuffer.  Positions scale per-axis; font sizes scale by
 * WIIU_FONT_FIT at BOTH measure and draw time so text wrapping stays
 * exact.  A 960x544 rectangle maps to exactly 854x480 - no letterbox. */
/* Per-element scroll clip (2026-10-06 (75)): the SDL2 renderer clips
 * rects, geometry AND texture blits - the virtual->framebuffer fit is
 * applied exactly like every other draw here. */
void cwv_set_clip(float x, float y, float w, float h) {
    if (!g_rend) return;
    SDL_Rect r = { (int)(x * WIIU_FIT_X), (int)(y * WIIU_FIT_Y),
                   (int)(w * WIIU_FIT_X), (int)(h * WIIU_FIT_Y) };
    SDL_RenderSetClipRect(g_rend, &r);
}
void cwv_clip_disable(void) {
    if (g_rend) SDL_RenderSetClipRect(g_rend, NULL);
}

void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    if (!g_rend) return;
    SDL_Color c = to_sdl(color);
    SDL_SetRenderDrawColor(g_rend, c.r, c.g, c.b, c.a);
    SDL_FRect r = { x * WIIU_FIT_X, y * WIIU_FIT_Y,
                    w * WIIU_FIT_X, h * WIIU_FIT_Y };
    SDL_RenderFillRectF(g_rend, &r);
}
void cwv_draw_fill_circle(float x, float y, float radius, unsigned color) {
    if (!g_rend) return;
    SDL_Color c = to_sdl(color);
    SDL_SetRenderDrawColor(g_rend, c.r, c.g, c.b, c.a);
    /* triangle fan via SDL_RenderGeometry; radius uses the horizontal
       fit so the circle stays round under the per-axis mapping */
    SDL_Vertex verts[24];
    int n = (int)(sizeof(verts) / sizeof(verts[0])) - 1;
    float cx = x * WIIU_FIT_X, cy = y * WIIU_FIT_Y, cr = radius * WIIU_FIT_X;
    for (int i = 0; i <= n; i++) {
        float a = (float)(i % n) * 6.2831853f / n;
        verts[i].position.x = cx + cr * SDL_cosf(a);
        verts[i].position.y = cy + cr * SDL_sinf(a);
        verts[i].color.r = c.r; verts[i].color.g = c.g;
        verts[i].color.b = c.b; verts[i].color.a = c.a;
    }
    SDL_RenderGeometry(g_rend, NULL, verts, n + 1, NULL, 0);
}
void cwv_draw_texture(void *tex, float x, float y) {
    if (!g_rend || !tex) return;
    SDL_FRect dst = { x * WIIU_FIT_X, y * WIIU_FIT_Y,
                      (float)cwv_tex_width(tex) * WIIU_FIT_X,
                      (float)cwv_tex_height(tex) * WIIU_FIT_Y };
    SDL_RenderCopyF(g_rend, (SDL_Texture *)tex, NULL, &dst);
}
void cwv_draw_texture_scale(void *tex, float x, float y, float xs, float ys) {
    if (!g_rend || !tex) return;
    SDL_FRect dst = { x * WIIU_FIT_X, y * WIIU_FIT_Y,
                      cwv_tex_width(tex) * xs * WIIU_FIT_X,
                      cwv_tex_height(tex) * ys * WIIU_FIT_Y };
    SDL_RenderCopyF(g_rend, (SDL_Texture *)tex, NULL, &dst);
}
void cwv_draw_texture_part(void *tex, float x, float y,
                           float tx, float ty, float tw, float th) {
    if (!g_rend || !tex) return;
    /* SDL_RenderCopyF's srcrect is a plain (int) SDL_Rect - it selects
     * whole texture pixels; only dstrect (screen position) is float.
     * The SOURCE rect is texture space (unscaled by the fit). */
    SDL_FRect dst = { x * WIIU_FIT_X, y * WIIU_FIT_Y,
                      tw * WIIU_FIT_X, th * WIIU_FIT_Y };
    SDL_Rect src = { (int)(tx + 0.5f), (int)(ty + 0.5f),
                      (int)(tw + 0.5f), (int)(th + 0.5f) };
    SDL_RenderCopyF(g_rend, (SDL_Texture *)tex, &src, &dst);
}
void cwv_draw_texture_part_scale(void *tex, float x, float y,
                                 float tx, float ty, float tw, float th,
                                 float xs, float ys) {
    if (!g_rend || !tex) return;
    SDL_FRect dst = { x * WIIU_FIT_X, y * WIIU_FIT_Y,
                      tw * xs * WIIU_FIT_X, th * ys * WIIU_FIT_Y };
    SDL_Rect src = { (int)(tx + 0.5f), (int)(ty + 0.5f),
                      (int)(tw + 0.5f), (int)(th + 0.5f) };
    SDL_RenderCopyF(g_rend, (SDL_Texture *)tex, &src, &dst);
}

/* ---- console services ------------------------------------------------- */
/* SDL_Init happens in the Wii U app loop; nothing extra is needed here. */
void cwv_console_init(void) {}
void cwv_console_shutdown(void) {}
int cwv_console_open_browser(const char *url) { (void)url; return -1; }  /* TODO: Wii U has no external browser hand-off */
/* Remote @font-face fonts get cached here before cwv_font_load_file
 * (SDL_ttf only opens real files) - same SD-card app directory the
 * runtime log already uses (see CWV_WIIU_LOG_PATH in main.c); the
 * WUHB's own rom:/ content is read-only so it can't be used for this. */
int cwv_console_cache_dir(char *out, size_t out_cap) {
    char base[300], dir[330];
    snprintf(base, sizeof(base), "%s", cwv_wiiu_app_root());
    if (base[0] == 0) return -1;
    cwv_file_mkdir(base);
    snprintf(dir, sizeof(dir), "%s/cache", base);
    cwv_file_mkdir(dir);
    if (out_cap == 0) return -1;
    snprintf(out, out_cap, "%s", dir);
    return 0;
}
/* cwv_console_fetch is implemented in sources/wiiu/cwv_fetch_nets2.cpp
 * (nn::ac + the nets2-backed socket surface, per user direction). */

/* Platform identity exposed to the limited DOM navigator object. */
const char *cwv_platform_name(void) { return "Nintendo Wii U"; }
