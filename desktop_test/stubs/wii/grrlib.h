/* Minimal desktop stub of the GRRLIB/libogc surface used by
 * sources/wii/cwv_platform.c.  ONLY for syntax-checking the Wii port
 * on a desktop - the real build uses devkitPro's grrlib.h. */
#ifndef STUB_WII_GRRLIB_H
#define STUB_WII_GRRLIB_H
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint16_t u16;
typedef int32_t s32;
typedef float f32;

typedef struct { u32 w, h; u8 *data; } GRRLIB_texImg;
typedef struct GRRLIB_ttfFont GRRLIB_ttfFont;

void GRRLIB_FlushTex(const GRRLIB_texImg *tex);
int GRRLIB_Init(void);
void GRRLIB_Exit(void);
void GRRLIB_FillScreen(u32 color);
void GRRLIB_Rectangle(f32 x, f32 y, f32 w, f32 h, u32 color, u8 filled);
void GRRLIB_Circle(f32 x, f32 y, f32 radius, u32 color, bool filled);
GRRLIB_texImg *GRRLIB_LoadTexture(const u8 *data);
/* Real GRRLIB (4.4+): per-UV part draw, source rect in texture pixels. */
void GRRLIB_DrawPart(const f32 xpos, const f32 ypos, const f32 partx,
                     const f32 party, const f32 partw, const f32 parth,
                     const GRRLIB_texImg *tex, const f32 degrees,
                     const f32 scaleX, const f32 scaleY, const u32 color);
void GRRLIB_FreeTexture(GRRLIB_texImg *tex);
void GRRLIB_DrawImg(f32 x, f32 y, GRRLIB_texImg *tex, f32 degrees,
                    f32 scaleX, f32 scaleY, u32 color);
GRRLIB_ttfFont *GRRLIB_LoadTTF(const u8 *data, const s32 size);
void GRRLIB_FreeTTF(GRRLIB_ttfFont *font);
void GRRLIB_PrintfTTF(int x, int y, GRRLIB_ttfFont *font,
                      const char *text, unsigned size, u32 color);
unsigned GRRLIB_SizeTTF(unsigned *w, unsigned *h, GRRLIB_ttfFont *font,
                        const char *text, unsigned size);
int GRRLIB_WidthTTF(GRRLIB_ttfFont *font, const char *text, unsigned size);
/* BMP support (2026-10-01): the shared cwv_bmp decoder produces RGBA
 * bytes; the Wii platform wraps them in a GRRLIB texture. */
GRRLIB_texImg *GRRLIB_CreateEmptyTexture(const u16 w, const u16 h);
void GRRLIB_SetPixel(const GRRLIB_texImg *tex, const int x, const int y,
                     const u32 color);
#endif

/* main-loop surface used by sources/wii/main.c */
int GRRLIB_Render(void);
