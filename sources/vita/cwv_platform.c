/* cwv_platform.c - the Vita implementation of the shared engine's
 * platform seam (see sources/shared/sources/core/cwv_platform.h).  Every function
 * here is a thin forward onto the real vita2d / sceIo / sceImeDialog
 * calls; the engine itself never touches a console SDK.
 *
 * A port for another console (Wii U, Wii) provides the same set of
 * functions from its own sources/{console}/cwv_platform.c and the
 * shared engine links and runs unmodified.
 */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <string.h>
#include <stdint.h>
#include <vita2d.h>
#include <curl/curl.h>

#include "core/cwv_platform.h"
#include "core/cwv_cwi.h"
#include <stdlib.h>   /* (75): free() was an implicit declaration on the Vita build */
#include <stdio.h>    /* (80): snprintf was only reaching us transitively */

/* ---- files ---------------------------------------------------------- */
int cwv_file_open_read(const char *path) {
    return sceIoOpen(path, SCE_O_RDONLY, 0);
}
void cwv_file_close(int fd) { sceIoClose((SceUID)fd); }
int cwv_file_read(int fd, void *buf, int len) {
    return sceIoRead((SceUID)fd, buf, len);
}
long cwv_file_size(const char *path) {
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    return (long)st.st_size;
}
int cwv_file_mkdir(const char *path) {
    return sceIoMkdir(path, 0777) < 0 ? -1 : 0;
}
int cwv_file_write_all(const char *path, const void *buf, size_t len) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return -1;
    int wr = sceIoWrite(fd, buf, (int)len);
    sceIoClose(fd);
    return (wr == (int)len) ? 0 : -1;
}
int cwv_file_append(const char *path, const void *buf, size_t len) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0) return -1;
    int wr = sceIoWrite(fd, buf, (int)len);
    sceIoClose(fd);
    return (wr == (int)len) ? 0 : -1;
}

/* ---- textures ------------------------------------------------------- */
void *cwv_tex_load_png_file(const char *path)  { return vita2d_load_PNG_file(path); }
void *cwv_tex_load_jpeg_file(const char *path) { return vita2d_load_JPEG_file(path); }
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len) {
    (void)len;   /* libvita2d's libpng reader pulls only what it needs */
    return vita2d_load_PNG_buffer(buf);
}
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len) {
    return vita2d_load_JPEG_buffer(buf, len);
}
/* CWI (2026-10-05 (71)): shared lossless decode -> vita2d RGBA8888
 * texture.  Width is rounded up to the 8-pixel alignment vita2d
 * requires (the same reformat its PNG loader does); rows are copied
 * per-line because the padded texture is wider than the image. */
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len) {
    unsigned char *rgba = NULL;
    int w = 0, h = 0; unsigned w8, y;
    vita2d_texture *t;
    if (!cwv_cwi_decode((const unsigned char *)buf, (size_t)len, &rgba, &w, &h))
        return NULL;
    w8 = (w + 7u) & ~7u;
    t = vita2d_create_empty_texture(w8, h);
    if (t) {
        unsigned char *dst = (unsigned char *)vita2d_texture_get_datap(t);
        for (y = 0; (int)y < h; y++)   /* (75): unsigned/int sign compare */
            memcpy(dst + (size_t)y * w8 * 4u,
                   rgba + (size_t)y * w * 4u, (size_t)w * 4u);
    }
    free(rgba);
    return t;
}
int cwv_tex_update_rgba_buffer(void *tex, const unsigned char *rgba, int w, int h) {
    vita2d_texture *t = (vita2d_texture *)tex;
    unsigned char *dst;
    int tw, th, y;
    if (!t || !rgba || w <= 0 || h <= 0) return 0;
    tw = (int)vita2d_texture_get_width(t);
    th = vita2d_texture_get_height(t);
    if (w > tw || h > th) return 0;
    dst = (unsigned char *)vita2d_texture_get_datap(t);
    if (!dst) return 0;
    for (y = 0; y < h; ++y)
        memcpy(dst + (size_t)y * tw * 4u,
               rgba + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    return 1;
}
void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len) {
    (void)len;
    return vita2d_load_BMP_buffer(buf);
}
void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int w, int h) {
    unsigned w8, y;
    vita2d_texture *t;
    unsigned char *dst;
    if (!rgba || w <= 0 || h <= 0 || w > 1024 || h > 768) return NULL;
    w8 = ((unsigned)w + 7u) & ~7u;
    t = vita2d_create_empty_texture(w8, (unsigned)h);
    if (!t) return NULL;
    dst = (unsigned char *)vita2d_texture_get_datap(t);
    memset(dst, 0, (size_t)w8 * (size_t)h * 4u);
    for (y = 0; y < (unsigned)h; y++)
        memcpy(dst + (size_t)y * w8 * 4u,
               rgba + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    return t;
}
int cwv_tex_width(void *tex)  { return vita2d_texture_get_width((vita2d_texture *)tex); }
int cwv_tex_height(void *tex){ return vita2d_texture_get_height((vita2d_texture *)tex); }
void cwv_tex_set_filter(void *tex, int min_f, int mag_f) {
    vita2d_texture_set_filters((vita2d_texture *)tex,
                               (SceGxmTextureFilter)min_f,
                               (SceGxmTextureFilter)mag_f);
}
void cwv_tex_free(void *tex) { vita2d_free_texture((vita2d_texture *)tex); }

/* ---- fonts ---------------------------------------------------------- */
void *cwv_pgf_load_default(void) { return vita2d_load_default_pgf(); }
void *cwv_font_load_file(const char *path) { return vita2d_load_font_file(path); }
void cwv_pgf_free(void *pgf)   { vita2d_free_pgf((vita2d_pgf *)pgf); }
void cwv_font_free(void *font) { vita2d_free_font((vita2d_font *)font); }
int cwv_font_text_width(void *font, unsigned size, const char *utf8) {
    return vita2d_font_text_width((vita2d_font *)font, size, utf8);
}
int cwv_pgf_text_width(void *pgf, float size, const char *utf8) {
    return vita2d_pgf_text_width((vita2d_pgf *)pgf, size, utf8);
}
int cwv_font_draw_text(void *font, float x, float y, unsigned color,
                       unsigned size, const char *utf8) {
    vita2d_font_draw_text((vita2d_font *)font, x, y, color, size, utf8);
    return 0;
}
int cwv_pgf_draw_text(void *pgf, float x, float y, unsigned color,
                      float size, const char *utf8) {
    vita2d_pgf_draw_text((vita2d_pgf *)pgf, x, y, color, size, utf8);
    return 0;
}

/* ---- primitives ------------------------------------------------------ */
/* Per-element scroll clip (2026-10-06 (75)): vita2d's GPU scissor
 * clips rects, circles, textures AND text in one call. */
void cwv_set_clip(float x, float y, float w, float h) {
    vita2d_set_clip_rectangle((int)x, (int)y, (int)(x + w), (int)(y + h));
    vita2d_enable_clipping();
}
void cwv_clip_disable(void) {
    vita2d_disable_clipping();
}

void cwv_clear_screen(void) { vita2d_clear_screen(); }
void cwv_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    vita2d_draw_rectangle(x, y, w, h, color);
}
void cwv_draw_fill_circle(float x, float y, float radius, unsigned color) {
    vita2d_draw_fill_circle(x, y, radius, color);
}
void cwv_draw_texture(void *tex, float x, float y) {
    vita2d_draw_texture((vita2d_texture *)tex, x, y);
}
void cwv_draw_texture_scale(void *tex, float x, float y, float xs, float ys) {
    vita2d_draw_texture_scale((const vita2d_texture *)tex, x, y, xs, ys);
}
void cwv_draw_texture_part(void *tex, float x, float y,
                           float tx, float ty, float tw, float th) {
    vita2d_draw_texture_part((vita2d_texture *)tex, x, y, tx, ty, tw, th);
}
void cwv_draw_texture_part_scale(void *tex, float x, float y,
                                 float tx, float ty, float tw, float th,
                                 float xs, float ys) {
    vita2d_draw_texture_part_scale((vita2d_texture *)tex, x, y,
                                   tx, ty, tw, th, xs, ys);
}

/* ---- on-screen keyboard ---------------------------------------------- */
int cwv_ime_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                 int password, int multiline,
                 cwv_utf16 *buffer, int buffer_len) {
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.type = SCE_IME_TYPE_DEFAULT;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
                                 : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = title;
    param.maxTextLength = buffer_len - 1;
    /* The SDK declares initialText without const even though the dialog
     * only ever READS it; the cast documents that, it changes nothing. */
    param.initialText = (cwv_utf16 *)(uintptr_t)initial;
    param.inputTextBuffer = buffer;
    if (multiline) param.option |= SCE_IME_OPTION_MULTILINE;
    return sceImeDialogInit(&param) < 0 ? -1 : 0;
}
int cwv_ime_poll(cwv_utf16 *buffer, int buffer_len) {
    (void)buffer; (void)buffer_len;   /* the console types into the
                                       * buffer registered at open time */
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return 0;
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    int entered = (result.button == SCE_IME_DIALOG_BUTTON_ENTER) ? 1 : 2;
    sceImeDialogTerm();
    return entered;
}

/* ---- console services -------------------------------------------------
 * Moved verbatim from the (now shared) host: sysmodule loads, the
 * AppUtil browser hand-off, and the blocking sceHttp GET behind the
 * page fetch() hook.  Same behavior, new home.
 * ---------------------------------------------------------------------- */
#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
/* sceHttp lives in psp2/net/http.h in the VitaSDK (very old SDK snapshots
 * named it psp2/http.h).  If this SDK has neither header, fetch() support
 * is compiled OUT: the app still builds and runs, and page fetch() calls
 * just report failure through the renderer's graceful (-1, "") path. */
#if defined(__has_include)
#  if __has_include(<psp2/net/http.h>)
#    include <psp2/net/http.h>
#    define CWV_HAVE_SCE_HTTP 1
#  elif __has_include(<psp2/http.h>)
#    include <psp2/http.h>
#    define CWV_HAVE_SCE_HTTP 1
#  else
#    define CWV_HAVE_SCE_HTTP 0
#  endif
#else
#  include <psp2/net/http.h>
#  define CWV_HAVE_SCE_HTTP 1
#endif

static int s_ime_module = 0;
static int s_http_module = 0;
static int s_apputil_module = 0;
static int s_apputil_initialized = 0;
static int s_http_initialized = 0;

void cwv_console_init(void) {
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_IME) >= 0) s_ime_module = 1;
#if CWV_HAVE_SCE_HTTP
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP) >= 0) s_http_module = 1;
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_SSL) >= 0) s_http_module = 1; /* https:// */
#endif
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL) >= 0) {
        s_apputil_module = 1;
        SceAppUtilInitParam ip; SceAppUtilBootParam bp;
        memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp));
        if (sceAppUtilInit(&ip, &bp) >= 0) s_apputil_initialized = 1;
    }
    /* libcurl (used by cwv_console_fetch_bin below, for remote images/
     * fonts/CSS) needs one-time global init, same as the sceHttp/apputil
     * modules above. */
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void cwv_console_shutdown(void) {
    if (s_apputil_initialized) { sceAppUtilShutdown(); s_apputil_initialized = 0; }
    if (s_ime_module) { sceSysmoduleUnloadModule(SCE_SYSMODULE_IME); s_ime_module = 0; }
    curl_global_cleanup();
}

/* Remote @font-face fonts get cached here before cwv_font_load_file
 * (vita2d/SDL_ttf-style font loaders only open real files) - this is
 * the same ux0:data/CWebView/<TitleID>/cache path load_font() used to
 * build directly before this moved behind the seam. CWV_APP_DATA_DIR
 * is a build-time -D (see CMakeLists.txt), set to the app's Title ID. */
int cwv_console_cache_dir(char *out, size_t out_cap) {
    char dir[256];
    snprintf(dir, sizeof(dir), "ux0:data/CWebView/%s/cache", CWV_APP_DATA_DIR);
    cwv_file_mkdir("ux0:data/CWebView");
    cwv_file_mkdir("ux0:data/CWebView/" CWV_APP_DATA_DIR);
    cwv_file_mkdir(dir);
    if (out_cap == 0) return -1;
    snprintf(out, out_cap, "%s", dir);
    return 0;
}

int cwv_console_open_browser(const char *url) {
    if (!url || !*url) return -1;
    if (!s_apputil_initialized) {
        if (s_apputil_module == 0 &&
            sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL) < 0) return -1;
        SceAppUtilInitParam ip; SceAppUtilBootParam bp;
        memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp));
        if (sceAppUtilInit(&ip, &bp) < 0) return -1;
        s_apputil_initialized = 1;
    }
    SceAppUtilWebBrowserParam wp;
    memset(&wp, 0, sizeof(wp));
    wp.str = url; wp.strlen = strlen(url); wp.launchMode = 0;
    return sceAppUtilLaunchWebBrowser(&wp) < 0 ? -1 : 0;
}

/* fetch(): synchronous http/https GET.  Design rule (project spec): a
 * failed fetch must NEVER crash the app - every step is checked and
 * every failure returns -1, the only observable effect being that the
 * page's callback gets (-1, "").  v1: one GET, response body capped by
 * the caller (256 KiB), no headers/POST.  Blocking on the render
 * thread; timeouts are 10 s send / 15 s receive. */
#define CWV_FETCH_SEND_TIMEOUT_US  (10 * 1000 * 1000)
#define CWV_FETCH_RECV_TIMEOUT_US  (15 * 1000 * 1000)

int cwv_console_fetch(const char *url, char *body, size_t cap) {
    if (!url || !body || cap == 0) return -1;
    body[0] = 0;
    /* only plain http/https GET; everything else fails, never crashes */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
        return -1;
    if (!s_http_module) return -1;            /* sysmodule unavailable */
#if !CWV_HAVE_SCE_HTTP
    return -1;   /* this SDK has no sceHttp header: no network backend */
#else
    {
    SceUID tmpl = -1, conn = -1, req = -1;
    int status = -1;
    int n;
    long total = 0;
    if (!s_http_initialized) {
        if (sceHttpInit(0) < 0) return -1;    /* no network stack: fail */
        s_http_initialized = 1;
    }
    tmpl = sceHttpCreateTemplate("CWebView/1.0",
                                SCE_HTTP_VERSION_1_1, SCE_HTTP_PROXY_AUTO);
    if (tmpl < 0) goto fail;
    sceHttpSetSendTimeOut(tmpl, CWV_FETCH_SEND_TIMEOUT_US);
    sceHttpSetRecvTimeOut(tmpl, CWV_FETCH_RECV_TIMEOUT_US);
    conn = sceHttpCreateConnectionWithURL(tmpl, url, 0);
    if (conn < 0) goto fail;
    req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
    if (req < 0) goto fail;
    if (sceHttpSendRequest(req, NULL, 0) < 0) goto fail;
    for (;;) {
        n = sceHttpReadData(req, body + total, (unsigned int)(cap - 1 - total));
        if (n < 0) goto fail;
        if (n == 0) break;
        total += n;
        if ((size_t)total + 1 >= cap) break;
    }
    body[total] = '\0';
    if (sceHttpGetStatusCode(req, &status) < 0) status = -1;
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    return (int)total;
fail:
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    return -1;
    }
#endif /* CWV_HAVE_SCE_HTTP */
}

/* Byte-exact GET for remote resources (images/@font-face fonts/CSS)
 * loaded by the shared renderer's resource loader - see cwv_platform.h.
 * Uses libcurl (this project's Vita build already links VDPM's
 * libcurl.a) rather than sceHttp above: same options load_resource()
 * used to set directly before this moved behind the seam. */
typedef struct { unsigned char *buf; size_t cap; size_t used; } CwvFetchBinBuf;
static size_t fetch_bin_write(void *ptr, size_t sz, size_t nm, void *ud) {
    CwvFetchBinBuf *b = ud;
    size_t n = sz * nm;
    size_t room = (b->used < b->cap) ? (b->cap - b->used) : 0;
    size_t take = (n < room) ? n : room;
    if (take) { memcpy(b->buf + b->used, ptr, take); b->used += take; }
    return n;   /* report the full size handled so curl doesn't abort */
}
int cwv_console_fetch_bin(const char *url, unsigned char *buf, size_t cap,
                          size_t *out_len) {
    if (!url || !buf || cap == 0) return -1;
    CURL *c = curl_easy_init();
    if (!c) return -1;
    CwvFetchBinBuf b = { buf, cap, 0 };
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_bin_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "CWebView/0.2.0");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    CURLcode ec = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (ec != CURLE_OK) return -1;
    if (out_len) *out_len = b.used;
    return 0;
}

/* Platform identity exposed to the limited DOM navigator object. */
const char *cwv_platform_name(void) { return "PlayStation Vita"; }
