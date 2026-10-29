/* cwv_ime_wii.h - Wii software on-screen keyboard (libogc has no IME).
 *
 * The engine seam (cwv_ime_open/cwv_ime_poll, sources/shared/
 * cwv_platform.h) is implemented by this module; main.c routes pad and
 * IR-pointer input to it while it is on screen and calls the draw hook
 * after the page/cursor and before GRRLIB_Render (the "draw last"
 * placement, same rule as the Wii U swkbd).
 */
#ifndef CWV_IME_WII_H
#define CWV_IME_WII_H

#include "core/cwv_platform.h"

/* Input events routed from the app loop (D-pad navigation + buttons). */
enum {
    CWV_IME_W_UP = 0,
    CWV_IME_W_DOWN,
    CWV_IME_W_LEFT,
    CWV_IME_W_RIGHT,
    CWV_IME_W_CONFIRM,   /* A without IR pointing: activate selection */
    CWV_IME_W_CANCEL     /* B: close without applying (result 2) */
};

/* Engine seam (cwv_ime_open/cwv_ime_poll delegate here). */
int  cwv_ime_wii_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                      int password, int multiline,
                      cwv_utf16 *buffer, int buffer_len);
int  cwv_ime_wii_poll(cwv_utf16 *buffer, int buffer_len);

/* True while the keyboard is on screen (modal: the app loop routes all
 * input here and skips page/menu handling). */
int  cwv_ime_wii_active(void);

/* D-pad/button navigation (one event per call). */
void cwv_ime_wii_input(int event);

/* IR-pointer tap on a key (screen coordinates, 640x480 space). */
void cwv_ime_wii_pointer(int x, int y);

/* Draws the whole keyboard. `font` is the default TTF face
 * (cwv_pgf_load_default); drawn LAST over page + cursor. */
void cwv_ime_wii_draw(void *font);

#endif /* CWV_IME_WII_H */
