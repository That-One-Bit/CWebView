/* Implementations for the desktop stubs: deterministic measurements and a
 * recording draw log. The harness diffs the log between renderer versions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <vita2d.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <curl/curl.h>
#include <psp2/ime_dialog.h>

/* --- recording draw log ------------------------------------------------ */
static FILE *g_log;
void stub_set_log(FILE *f) { g_log = f; }

static void f2(float v) { fprintf(g_log, "%.2f", v); }

void vita2d_clear_screen(void) { fprintf(g_log, "CLEAR\n"); }
/* Scroll-region clip trace: only meaningful changes hit the log, so the
 * golden references of pages without regions never move. */
static int g_clip_active;
void vita2d_stub_set_clip(float x, float y, float x2, float y2) {
    if (x2 <= x || y2 <= y) {
        if (g_clip_active) { fprintf(g_log, "CLIPOFF\n"); g_clip_active = 0; }
        return;
    }
    fprintf(g_log, "CLIP "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(x2 - x); fputc(' ', g_log); f2(y2 - y); fputc('\n', g_log);
    g_clip_active = 1;
}
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned c) {
    fprintf(g_log, "RECT "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(w); fputc(' ', g_log); f2(h); fprintf(g_log, " %08X\n", c);
}
void vita2d_draw_texture_part_scale(vita2d_texture *t, float x, float y,
                                    float tex_x, float tex_y,
                                    float tex_w, float tex_h,
                                    float x_scale, float y_scale) {
    (void)t;
    fprintf(g_log, "TEX "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(tex_w * x_scale); fputc(' ', g_log);
    f2(tex_h * y_scale); fputc(' ', g_log);
    fprintf(g_log, " part="); f2(tex_x); fputc(' ', g_log); f2(tex_y); fputc(' ', g_log);
    f2(tex_w); fputc(' ', g_log); f2(tex_h); fprintf(g_log, "\n");
}

/* Texture sub-region draw (background-image tiles, clipped at the box
   edges).  Traced so the golden reference pins every tile position and
   its visible size. */
void vita2d_draw_texture_part(vita2d_texture *t, float x, float y,
                              float tex_x, float tex_y,
                              float tex_w, float tex_h) {
    (void)t;
    fprintf(g_log, "TEXP "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(tex_w); fputc(' ', g_log); f2(tex_h); fputc(' ', g_log);
    fprintf(g_log, " part="); f2(tex_x); fputc(' ', g_log); f2(tex_y); fputc(' ', g_log);
    fprintf(g_log, "\n");
}
void vita2d_font_draw_text(vita2d_font *f, float x, float y, unsigned c,
                           unsigned size, const char *text) {
    fprintf(g_log, "FONT "); f2(x); fputc(' ', g_log); f2(y);
    fprintf(g_log, " %u %08X [%s]\n", size, c, text ? text : "");
}
void vita2d_pgf_draw_text(vita2d_pgf *p, float x, float y, unsigned c,
                          float size, const char *text) {
    fprintf(g_log, "PGF "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(size); fprintf(g_log, " %08X [%s]\n", c, text ? text : "");
}

/* Deterministic metrics: width = strlen * size * 12 (like the renderer's own
 * fallback when no font is available). */
int vita2d_font_text_width(vita2d_font *f, unsigned size, const char *text) {
    (void)f; return text ? (int)(strlen(text) * size * 12u) : 0;
}
int vita2d_pgf_text_width(vita2d_pgf *p, float size, const char *text) {
    (void)p; return text ? (int)(strlen(text) * size * 12.0f) : 0;
}

/* --- loaders ----------------------------------------------------------- */
static vita2d_pgf g_pgf = { 1 };
vita2d_pgf *vita2d_load_default_pgf(void) { return &g_pgf; }
vita2d_font *vita2d_load_font_file(const char *path) { (void)path; return NULL; }

static vita2d_texture *fake_texture(void) {
    vita2d_texture *t = malloc(sizeof(*t));
    if (t) { t->width = 64; t->height = 64; }
    return t;
}
/* cursor_test.c needs one tunable texture for the CSS url() cursor
   (2026-09-26 (45)); the stub file IO layer only hands out 64x64.
   The accessor lazily allocates so the test can resize it. */
vita2d_texture *cwv_stub_cursor_test_texture(void) {
    static vita2d_texture *t;
    if (!t) t = fake_texture();
    return t;
}
/* CSS image-rendering: trace when the filter is pushed so the golden
   reference pins pixelated vs auto per image (state caching in backend.c
   means one line per image, not one per frame). */
void vita2d_texture_set_filters(vita2d_texture *t,
                                 SceGxmTextureFilter min_filter,
                                 SceGxmTextureFilter mag_filter) {
    (void)t;
    fprintf(g_log, "TEXFILTER %s\n",
            min_filter == SCE_GXM_TEXTURE_FILTER_POINT ? "point" : "linear");
    (void)mag_filter;
}
/* Filled circle - used by <li> disc/circle markers.  Traced so the
   golden reference pins every marker kind and position. */
void vita2d_draw_fill_circle(float x, float y, float radius, unsigned color) {
    fprintf(g_log, "CIRC "); f2(x); fputc(' ', g_log); f2(y); fputc(' ', g_log);
    f2(radius); fprintf(g_log, " %08X\n", color);
}
vita2d_texture *vita2d_load_PNG_file(const char *path) { (void)path; return NULL; }
vita2d_texture *vita2d_load_JPEG_file(const char *path) { (void)path; return NULL; }
void vita2d_draw_texture(const vita2d_texture *t, float x, float y) { (void)t; (void)x; (void)y; }
/* texture lifecycle counters: img_cache_test.c asserts the renderer's
   page-scoped image cache frees everything it allocates (no GXM-style
   accumulation across navigations). */
int g_stub_tex_allocs = 0, g_stub_tex_frees = 0;
vita2d_texture *vita2d_load_PNG_buffer(const void *b) { (void)b; g_stub_tex_allocs++; return fake_texture(); }
vita2d_texture *vita2d_load_JPEG_buffer(void *b, unsigned long s) { (void)b; (void)s; return fake_texture(); }
vita2d_texture *vita2d_load_BMP_buffer(const void *b) { (void)b; return fake_texture(); }
int vita2d_texture_get_width(const vita2d_texture *t) { return t ? t->width : 0; }
int vita2d_texture_get_height(const vita2d_texture *t) { return t ? t->height : 0; }
void vita2d_free_texture(vita2d_texture *t) { g_stub_tex_frees++; free(t); }

/* Draw a whole texture at its native size times a uniform scale - used
   for the bundled broken-image glyph.  Traced so the golden reference
   pins its position and scale on every failed image. */
void vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y,
                               float x_scale, float y_scale) {
    fprintf(g_log, "TEXSCALE "); f2(x); fputc(' ', g_log); f2(y);
    fputc(' ', g_log); f2(x_scale); fputc(' ', g_log); f2(y_scale);
    fprintf(g_log, " %dx%d\n", t ? t->width : 0, t ? t->height : 0);
}
void vita2d_free_font(vita2d_font *f) { (void)f; }
void vita2d_free_pgf(vita2d_pgf *p) { (void)p; }

/* --- file IO: map app0:/ and ux0: paths onto ./testdata and ./tmp ------- */
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static void map_path(const char *in, char *out, size_t cap) {
    if (strncmp(in, "app0:/", 6) == 0)
        snprintf(out, cap, "testdata/%s", in + 6);
    else if (strncmp(in, "sd:/apps/cwebview/", 18) == 0)
        /* the Wii/Wii U package-folder root (approot_test): the same
         * fixture files, reached through a non-Vita path root */
        snprintf(out, cap, "testdata/%s", in + 18);
    else if (strncmp(in, "ux0:", 4) == 0)
        snprintf(out, cap, "tmpux0%s", in + 4); /* ux0:data/x -> tmpux0/data/x */
    else
        snprintf(out, cap, "%s", in);
}

int sceIoOpen(const char *file, int flags, int mode) {
    char p[1024]; map_path(file, p, sizeof(p));
    int o = 0;
    if (flags & SCE_O_RDONLY) o |= O_RDONLY;
    if (flags & SCE_O_WRONLY) o |= O_WRONLY;
    if (flags & SCE_O_CREAT) o |= O_CREAT;
    if (flags & SCE_O_TRUNC) o |= O_TRUNC;
    int fd = open(p, o, 0644);
    return fd < 0 ? fd : fd + 1; /* Vita fds are positive */
}
int sceIoClose(int fd) { return close(fd - 1); }
int sceIoRead(int fd, void *buf, int len) { return (int)read(fd - 1, buf, len); }
int sceIoWrite(int fd, const void *buf, int len) { return (int)write(fd - 1, buf, len); }
int sceIoGetstat(const char *file, SceIoStat *st) {
    char p[1024]; map_path(file, p, sizeof(p));
    struct stat s;
    if (stat(p, &s) != 0) return -1;
    memset(st, 0, sizeof(*st));
    st->st_size = (unsigned long long)s.st_size;
    return 0;
}
int sceIoMkdir(const char *dir, int mode) {
    char p[1024]; map_path(dir, p, sizeof(p));
    return mkdir(p, (mode_t)mode);
}

/* --- curl: SIMULATED NETWORK --------------------------------------------
 * Desktop tests never touch the real internet: every http(s) fetch
 * "downloads" the same tiny red 8x8 PNG, which pins the remote-image
 * path (and its extensionless URL decoding) deterministically. --------- */
static size_t (*g_curl_write)(void *, size_t, size_t, void *) = NULL;
static void *g_curl_write_data = NULL;
static const unsigned char g_curl_png[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x08,0x00,0x00,0x00,0x00,0xE1,0x64,0xE1,0x57,0x00,0x00,0x00,0x11,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0x68,0x60,0x60,0xC0,0x8A,0x18,0x86,0x96,0x04,0x00,0xA1,0x7C,0x20,0x01,0xD6,0x95,0xEB,0xAD,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82
};
/* The fake network payload, shared by the old curl stub below and the
 * seam-based cwv_console_fetch_bin() in cwv_platform_stub.c (both
 * "download" the same bytes so old-vs-new comparisons stay honest). */
const unsigned char *stub_fake_net_png(size_t *out_len) {
    if (out_len) *out_len = sizeof(g_curl_png);
    return g_curl_png;
}
CURLcode curl_global_init(long f) { (void)f; return 0; }
void curl_global_cleanup(void) {}
CURL *curl_easy_init(void) {
    static char fake_handle = 0;   /* CURL is "typedef void CURL" */
    g_curl_write = NULL;
    g_curl_write_data = NULL;
    return (CURL *)&fake_handle;
}
CURLcode curl_easy_setopt(CURL *h, int o, ...) {
    (void)h;
    va_list ap;
    va_start(ap, o);
    if (o == (int)CURLOPT_WRITEFUNCTION)
        g_curl_write = va_arg(ap, size_t (*)(void *, size_t, size_t, void *));
    else if (o == (int)CURLOPT_WRITEDATA)
        g_curl_write_data = va_arg(ap, void *);
    va_end(ap);
    return 0;
}
CURLcode curl_easy_perform(CURL *h) {
    (void)h;
    if (g_curl_write)
        g_curl_write((void *)g_curl_png, 1, sizeof(g_curl_png),
                     g_curl_write_data);
    return 0;
}
void curl_easy_cleanup(CURL *h) { (void)h; }

void sceImeDialogParamInit(SceImeDialogParam *param) {
    memset(param, 0, sizeof(*param));
    param->maxTextLength = 2048;
}
int sceImeDialogInit(SceImeDialogParam *param) { (void)param; return 0; }
int sceImeDialogGetStatus(void) { return SCE_COMMON_DIALOG_STATUS_FINISHED; }
int sceImeDialogGetResult(SceImeDialogResult *result) {
    memset(result, 0, sizeof(*result));
    result->button = SCE_IME_DIALOG_BUTTON_ENTER;
    return 0;
}
int sceImeDialogTerm(void) { return 0; }
