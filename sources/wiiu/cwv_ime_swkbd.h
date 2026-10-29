#ifndef CWV_IME_SWKBD_H
#define CWV_IME_SWKBD_H

/* This header is included by C (main.c) AND C++ (cwv_ime_swkbd.cpp).
 * WITHOUT the extern "C" guard the .cpp definition of cwv_ime_draw()
 * gets C++-mangled and the C app loop fails to link:
 *   undefined reference to `cwv_ime_draw'
 * (cwv_ime_open/poll are safe only because they are also declared in
 * cwv_platform.h, which already has the guard.) */
#ifdef __cplusplus
extern "C" {
#endif

/* cwv_ime_swkbd.h - Wii U IME glue (wut's nn::swkbd library).
 *
 * The seam functions (cwv_ime_open / cwv_ime_poll, declared in
 * sources/shared/sources/core/cwv_platform.h) drive the keyboard: open brings it up,
 * the engine calls poll every frame.  THIS header exists for the one
 * function main.c must call directly: the draw hook.  wut's swkbd is an
 * IN-APP keyboard library, not a blocking system dialog - the app must
 * draw it LAST inside its own frame (wut: "after rendering all other
 * TV graphics") so the keyboard lands on top of the page, and that
 * render order belongs to the app loop, not the engine seam. */

/* Draw the keyboard on top of the frame the app just rendered.  Call
 * this every frame AFTER the page draws and BEFORE SDL_RenderPresent();
 * it is a no-op while no keyboard is up. */
void cwv_ime_draw(void);

#ifdef __cplusplus
}
#endif

#endif
