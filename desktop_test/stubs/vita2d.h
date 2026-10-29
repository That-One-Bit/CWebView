/* Desktop test stub for <vita2d.h>.
 * Records every draw call so the harness can diff rendering between
 * versions of the renderer. Never compiled for the real Vita. */
#ifndef STUB_VITA2D_H
#define STUB_VITA2D_H

#include <stddef.h>

#define RGBA8(r,g,b,a) ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | ((r)&0xFF))

typedef struct { int width, height; } vita2d_texture;
typedef struct { int dummy; } vita2d_font;
typedef struct { int dummy; } vita2d_pgf;

void vita2d_clear_screen(void);

/* Recording draw stubs (implemented in stub_impl.c) */
/* Scroll-region clip (2026-10-06 (75)): x2<=x marks "clip off".  Logged
 * on state change only, so pages without scroll regions keep byte-
 * identical goldens. */
void vita2d_stub_set_clip(float x, float y, float x2, float y2);
/* REAL libvita2d clip API (vitasdk/vita2d vita2d.h): the rectangle is
 * min/max corners, NOT x/y/w/h, and the name is ..._clip_rectangle (the
 * Vita build once shipped a nonexistent vita2d_set_clip_rect).  Declared
 * here so build_vita_kbm_gate.sh can syntax-check the REAL
 * sources/vita/cwv_platform.c with implicit declarations as errors. */
void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max);
void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned color);
/* REAL libvita2d signature (texture, screen x/y, part x/y, part w/h,
   x scale, y scale) - the engine must call it exactly this way on the
   Vita, so the stub mirrors the real argument order, not our own. */
void vita2d_draw_texture_part_scale(vita2d_texture *t, float x, float y,
                                    float tex_x, float tex_y,
                                    float tex_w, float tex_h,
                                    float x_scale, float y_scale);
void vita2d_draw_texture_part(vita2d_texture *t, float x, float y,
                              float tex_x, float tex_y,
                              float tex_w, float tex_h);
void vita2d_font_draw_text(vita2d_font *f, float x, float y, unsigned color,
                            unsigned size, const char *text);
void vita2d_pgf_draw_text(vita2d_pgf *p, float x, float y, unsigned color,
                          float size, const char *text);
int vita2d_font_text_width(vita2d_font *f, unsigned size, const char *text);
int vita2d_pgf_text_width(vita2d_pgf *p, float size, const char *text);

vita2d_pgf *vita2d_load_default_pgf(void);
vita2d_font *vita2d_load_font_file(const char *path);
typedef enum SceGxmTextureFilter {
    SCE_GXM_TEXTURE_FILTER_POINT = 0,
    SCE_GXM_TEXTURE_FILTER_LINEAR = 1
} SceGxmTextureFilter;
void vita2d_texture_set_filters(vita2d_texture *texture,
                                SceGxmTextureFilter min_filter,
                                SceGxmTextureFilter mag_filter);
vita2d_texture *vita2d_load_PNG_file(const char *path);
vita2d_texture *vita2d_load_JPEG_file(const char *path);
vita2d_texture *vita2d_load_PNG_buffer(const void *buffer);
vita2d_texture *vita2d_load_JPEG_buffer(void *buffer, unsigned long size);
vita2d_texture *vita2d_load_BMP_buffer(const void *buffer);
int vita2d_texture_get_width(const vita2d_texture *t);
/* REAL libvita2d texture-creation API used by the CWI upload path. */
vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h);
void *vita2d_texture_get_datap(const vita2d_texture *t);
int vita2d_texture_get_height(const vita2d_texture *t);
void vita2d_free_texture(vita2d_texture *t);
void vita2d_free_font(vita2d_font *f);
void vita2d_free_pgf(vita2d_pgf *p);

/* --- extra declarations used ONLY by the main.c syntax gate ---
 * (main.c runs on the Vita; these are just prototypes so -fsyntax-only
 * can compile it on the desktop.  No definitions are needed: the gate
 * never links.) */
void vita2d_start_drawing(void);
int  vita2d_common_dialog_update(void);
void vita2d_end_drawing(void);
void vita2d_swap_buffers(void);
int  vita2d_init(void);
int  vita2d_init_advanced(unsigned int mem_size);
void vita2d_fini(void);
void vita2d_set_clear_color(unsigned int color);
void vita2d_draw_fill_circle(float x, float y, float radius, unsigned int color);
void vita2d_draw_texture(const vita2d_texture *t, float x, float y);
void vita2d_draw_line(float x0, float y0, float x1, float y1, unsigned int color);
void vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y,
                               float x_scale, float y_scale);

#endif
