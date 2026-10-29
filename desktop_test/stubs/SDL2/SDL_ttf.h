/* Minimal desktop stub of SDL2_ttf surface used by the Wii U port
 * (see stubs/SDL2/SDL.h).  Real build uses devkitPro's SDL2_ttf. */
#ifndef STUB_SDL2_SDL_TTF_H
#define STUB_SDL2_SDL_TTF_H
#include "SDL.h"
typedef struct _TTF_Font TTF_Font;
int  TTF_Init(void);
void TTF_Quit(void);
TTF_Font *TTF_OpenFont(const char *path, int ptsize);
void TTF_CloseFont(TTF_Font *f);
void TTF_SetFontStyle(TTF_Font *f, int style);
int  TTF_SizeUTF8(TTF_Font *f, const char *text, int *w, int *h);
int  TTF_FontAscent(TTF_Font *f);
int  TTF_FontDescent(TTF_Font *f);
int  TTF_FontHeight(TTF_Font *f);
SDL_Texture *TTF_RenderUTF8_Solid(TTF_Font *f, const char *text, uint32_t fg);
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *f, const char *text, SDL_Color fg);
#endif
