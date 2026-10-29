/* ------------------------------------------------------------------ */
/* cwv_kbm_vita.h - Bluetooth HID keyboard + mouse (SceHid)           */
/* ------------------------------------------------------------------ */
/*
 * The Vita natively pairs Bluetooth keyboards and mice; SceHid exposes
 * them to applications.  This module:
 *
 *   - enumerates paired HID keyboards and mice (re-enumerates from time
 *     to time so a keyboard paired while CWebView runs still works);
 *   - translates held keyboard keys into SYNTHESIZED SceCtrl bits so
 *     the whole app - page scrolling, focus, CWMenu, Settings, DevTools
 *     customize mode - responds to the keyboard exactly as it does to
 *     the D-pad/sticks.  Edge detection keeps working because the main
 *     loop computes "pressed" from consecutive controller reads.
 *   - integrates a Bluetooth MOUSE as a relative-pointer virtual
 *     cursor (absolute screen coordinates maintained here), the same
 *     role the rear touchpad plays when the pointer setting is on.
 *
 * The module is self-contained on purpose: main.c only calls
 * cwv_kbm_vita_init(), cwv_kbm_vita_ctrl_bits() (from the controller
 * read helper) and cwv_kbm_vita_mouse_poll() once per frame.
 */
#ifndef CWV_KBM_VITA_H
#define CWV_KBM_VITA_H

#include <psp2/ctrl.h>

typedef struct {
    int present;          /* a BT mouse is currently connected            */
    int x, y;             /* virtual cursor, absolute screen pixels      */
    int left, right;      /* current button hold state                   */
    int left_pressed;     /* press edge this frame                       */
    int left_released;    /* release edge this frame                    */
    int right_pressed;    /* press edge this frame                      */
    int wheel;            /* accumulated wheel steps this frame         */
} CwvKbmVitaMouse;

/* Enumerate paired HID devices.  Call once at boot; safe to call again
 * (it refreshes handles for devices paired/powered on later). */
void cwv_kbm_vita_init(void);

/* Screen bounds for the virtual mouse cursor (clamps rel movement). */
void cwv_kbm_vita_set_screen(int w, int h);

/* Held-key translation: OR these bits into SceCtrlData.buttons after the
 * physical read.  Keys are held while the boot report contains them. */
unsigned cwv_kbm_vita_ctrl_bits(void);

/* Poll keyboards (refresh ctrl bits) and mice; fills the mouse state for
 * this frame.  Never blocks: uses sceHidKeyboardPeek / sceHidMouseRead
 * with whatever reports are ready. */
void cwv_kbm_vita_mouse_poll(CwvKbmVitaMouse *m);

#endif /* CWV_KBM_VITA_H */
