/* Minimal desktop stub of the SDL2 surface used by the Wii U port.
 * ONLY for syntax-checking on a desktop - the real build uses the
 * actual SDL2 headers from devkitPro (wiiu-sdl2). */
#ifndef STUB_SDL2_SDL_H
#define STUB_SDL2_SDL_H
#include <stdint.h>

#define SDL_INIT_VIDEO 1
#define SDL_INIT_JOYSTICK 2
#define SDL_WINDOWPOS_UNDEFINED 0
#define SDL_RENDERER_ACCELERATED 0x1
#define SDL_RENDERER_PRESENTVSYNC 0x2
#define SDL_BLENDMODE_BLEND 1
#define SDL_HINT_TOUCH_MOUSE_EVENTS "SDL_TOUCH_MOUSE_EVENTS"
#define SDL_ScaleModeNearest 0
#define SDL_ScaleModeLinear  1
#define SDL_HAT_UP    0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN  0x04
#define SDL_HAT_LEFT  0x08

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Texture SDL_Texture;
struct SDL_RWops { int (*close)(struct SDL_RWops *ctx); };
typedef struct SDL_RWops SDL_RWops;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { float x, y; } SDL_PointF;

typedef struct SDL_Vertex {
    SDL_PointF position;
    SDL_Color color;
    SDL_PointF tex_coord;
} SDL_Vertex;
struct SDL_Surface { int w, h; };
typedef struct SDL_Surface SDL_Surface;

enum {
    SDL_QUIT = 0x100,
    SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEMOTION,
    SDL_MOUSEWHEEL,
    SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP, SDL_JOYHATMOTION,
    SDL_KEYDOWN, SDL_KEYUP,
    SDL_FINGERDOWN = 0x700, SDL_FINGERUP, SDL_FINGERMOTION
};

typedef struct { uint32_t type; uint8_t state; int32_t x, y; } SDL_MouseMotionEvent;
typedef struct { uint32_t type; uint8_t button; int32_t x, y; } SDL_MouseButtonEvent;
/* Mouse wheel (SDL_MouseWheelEvent): x/y are step deltas. */
typedef struct { uint32_t type; int32_t x, y; } SDL_MouseWheelEvent;
typedef struct { uint32_t type; int64_t touchId, fingerId; float x, y, dx, dy, pressure; } SDL_TouchFingerEvent;
/* Keyboard: SDL_Keycode is Sint32 in SDL 2.0; keysym.sym holds it.
 * The SDLK_* values below are the real SDL 2.0 keycode constants. */
typedef int32_t SDL_Keycode;
typedef struct { uint32_t scancode; SDL_Keycode sym; uint16_t mod; } SDL_Keysym;
typedef struct { uint32_t type; SDL_Keysym keysym; } SDL_KeyboardEvent;
enum {
    SDLK_TAB      = 0x00000009,
    SDLK_RETURN   = 0x0000000D,
    SDLK_ESCAPE   = 0x0000001B,
    SDLK_F1       = 0x4000003A,
    SDLK_F5       = 0x4000003E,
    SDLK_HOME     = 0x4000004A,
    SDLK_END      = 0x4000004B,
    SDLK_PAGEUP   = 0x4000004C,
    SDLK_PAGEDOWN = 0x4000004D,
    SDLK_LEFT     = 0x40000050,
    SDLK_RIGHT    = 0x40000051,
    SDLK_UP       = 0x40000052,
    SDLK_DOWN     = 0x40000054
};
/* `which` mirrors SDL 2.0 SDL_JoystickID (the opened instance id). */
typedef int32_t SDL_JoystickID;
typedef struct { uint32_t type; SDL_JoystickID which; uint8_t button; } SDL_JoyButtonEvent;
typedef struct { uint32_t type; uint8_t hat, value; } SDL_JoyHatEvent;
typedef union SDL_Event {
    uint32_t type;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyHatEvent jhat;
    SDL_TouchFingerEvent tfinger;
    SDL_KeyboardEvent key;
} SDL_Event;

int SDL_Init(unsigned flags);
int SDL_SetHint(const char *name, const char *value);
const char *SDL_GetError(void);
void SDL_Quit(void);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, unsigned flags);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int idx, unsigned flags);
void SDL_DestroyWindow(SDL_Window *w);
void SDL_DestroyRenderer(SDL_Renderer *r);
int SDL_PollEvent(SDL_Event *ev);
uint32_t SDL_GetTicks(void);
void SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t a, uint8_t b, uint8_t c, uint8_t d);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode);
int SDL_RenderDrawLineF(SDL_Renderer *r, float x1, float y1, float x2, float y2);
void SDL_RenderPresent(SDL_Renderer *r);
int SDL_NumJoysticks(void);
int SDL_InitSubSystem(int flags);
short SDL_JoystickGetAxis(SDL_Joystick *j, int axis);
SDL_Joystick *SDL_JoystickOpen(int idx);
void SDL_JoystickClose(SDL_Joystick *j);
int16_t SDL_JoystickGetAxis(SDL_Joystick *j, int axis);

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
int SDL_QueryTexture(SDL_Texture *t, uint32_t *fmt, int *access, int *w, int *h);
int SDL_SetTextureScaleMode(SDL_Texture *t, int mode);
/* SDL pixel formats used by the engine (values mirror SDL2's headers;
   the desktop gate is syntax-only, the constants just need to exist). */
#define SDL_PIXELFORMAT_RGBA32 0x16161804u
#define SDL_TEXTUREACCESS_STATIC 1
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, uint32_t fmt, int access, int w, int h);
int SDL_UpdateTexture(SDL_Texture *t, const void *rect, const void *pixels, int pitch);
void SDL_DestroyTexture(SDL_Texture *t);
int SDL_SetTextureBlendMode(SDL_Texture *t, int mode);
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const void *srcrect, const void *dstrect);
int SDL_RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const void *srcrect, const SDL_FRect *dstrect);
int SDL_RenderFillRectF(SDL_Renderer *r, const SDL_FRect *rect);
int SDL_RenderDrawLinesF(SDL_Renderer *r, const SDL_PointF *points, int count);
int SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *verts, int nverts, int *indices, int nindices);
float SDL_cosf(float x);
float SDL_sinf(float x);
void SDL_FreeSurface(SDL_Surface *s);
#endif
