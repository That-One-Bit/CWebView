/* cwv_platform_stub.c - the DESKTOP TEST implementation of the engine's
 * platform seam (see sources/shared/sources/core/cwv_platform.h).  It forwards every
 * call to the psp2/vita2d stubs so the golden trace records exactly the
 * same operations the real Vita implementation issues, and linked test
 * binaries behave identically to the console build.
 */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <string.h>
#include "vita2d.h"

#include <stdlib.h>
#include "core/cwv_platform.h"
#include "core/cwv_cwi.h"

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

/* ---- textures ------------------------------------------------------- */
void *cwv_tex_load_png_file(const char *path)  { return vita2d_load_PNG_file(path); }
void *cwv_tex_load_jpeg_file(const char *path) { return vita2d_load_JPEG_file(path); }
void *cwv_tex_load_png_buffer(const void *buf, unsigned long len) {
    (void)len;
    return vita2d_load_PNG_buffer(buf);
}
void *cwv_tex_load_jpeg_buffer(void *buf, unsigned long len) {
    return vita2d_load_JPEG_buffer(buf, len);
}
/* CWI (2026-10-05 (71)): the golden harness records layout, not pixels,
 * so a magic-sniffed container yields the same fixed fake texture the
 * PNG stub returns (64x64).  The REAL decoder is exercised end-to-end
 * by the dedicated CWI unit gate (build_cwi_unit.sh); here only the
 * load/fail decision must match the engine. */
void *cwv_tex_load_cwi_buffer(const void *buf, unsigned long len) {
    vita2d_texture *t;
    if (!cwv_cwi_is((const unsigned char *)buf, (size_t)len))
        return NULL;
    t = (vita2d_texture *)malloc(sizeof(vita2d_texture));
    if (!t) return NULL;
    t->width = 64;
    t->height = 64;
    return t;
}
void *cwv_tex_load_bmp_buffer(const void *buf, unsigned long len) {
    (void)len;
    return vita2d_load_BMP_buffer(buf);
}
void *cwv_tex_load_rgba_buffer(const unsigned char *rgba, int width, int height) {
    vita2d_texture *t;
    if (!rgba || width <= 0 || height <= 0) return NULL;
    t = (vita2d_texture *)malloc(sizeof(vita2d_texture));
    if (!t) return NULL;
    t->width = width; t->height = height;
    return t;
}
int cwv_tex_update_rgba_buffer(void *tex, const unsigned char *rgba, int width, int height) {
    return tex != NULL && rgba != NULL && width > 0 && height > 0;
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
void cwv_clear_screen(void) { vita2d_clear_screen(); }
/* Scroll-region clip (2026-10-06 (75)): forwarded to the vita2d stub
 * so the golden/unit gates can assert the exact clip rects. */
void cwv_set_clip(float x, float y, float w, float h) {
    vita2d_stub_set_clip(x, y, x + w, y + h);
}
void cwv_clip_disable(void) {
    vita2d_stub_set_clip(0.0f, 0.0f, 0.0f, 0.0f);
}

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
    param.initialText = initial;
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

/* ---- console services (test doubles) --------------------------------- */
void cwv_console_init(void) {}
void cwv_console_shutdown(void) {}
int cwv_console_open_browser(const char *url) { (void)url; return -1; }
int cwv_console_fetch(const char *url, char *body, size_t cap) {
    (void)url; (void)cap; if (body) body[0]=0; return -1;
}
/* SIMULATED NETWORK (see stub_impl.c): desktop tests never touch the
 * real internet - every remote binary fetch "downloads" the same tiny
 * red 8x8 PNG, which pins the remote-image path (and its extensionless
 * URL decoding) deterministically, exactly like the old curl stub. */
const unsigned char *stub_fake_net_png(size_t *out_len);   /* stub_impl.c */
int cwv_console_fetch_bin(const char *url, unsigned char *buf, size_t cap,
                          size_t *out_len) {
    size_t len = 0;
    const unsigned char *png = stub_fake_net_png(&len);
    (void)url;   /* every URL "downloads" the same PNG */
    if (!png || len > cap) return -1;
    memcpy(buf, png, len);
    if (out_len) *out_len = len;
    return 0;
}
/* Cache dir: none on the test double (every cached-resource load just
 * fails that one resource, never the page). */
int cwv_console_cache_dir(char *out, size_t out_cap) {
    (void)out; (void)out_cap; return -1;
}

int cwv_file_append(const char *path, const void *buf, size_t len) {
    (void)path; (void)buf; (void)len; return 0;
}

/* Platform identity exposed to the limited DOM navigator object. */
const char *cwv_platform_name(void) { return "PlayStation Vita (test stub)"; }
