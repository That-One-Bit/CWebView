/* Minimal desktop stub of SDL2_image surface used by the Wii U port
 * (see stubs/SDL2/SDL.h).  Real build uses devkitPro's SDL2_image. */
#ifndef STUB_SDL2_SDL_IMAGE_H
#define STUB_SDL2_SDL_IMAGE_H
#include "SDL.h"

SDL_Texture *IMG_LoadTexture(SDL_Renderer *r, const char *file);
SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_LoadPNG_RW(SDL_RWops *rw);
SDL_Surface *IMG_LoadJPG_RW(SDL_RWops *rw);
SDL_Surface *IMG_LoadBMP_RW(SDL_RWops *rw);
void SDL_FreeSurface(SDL_Surface *s);
#endif
