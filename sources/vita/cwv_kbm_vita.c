/* ------------------------------------------------------------------ */
/* cwv_kbm_vita.c - Bluetooth HID keyboard + mouse via SceHid          */
/* ------------------------------------------------------------------ */
/*
 * Key translation policy (2026-10-05, user direction: KBM on every
 * platform that can use it; the Vita's BT HID stack is the seam):
 *
 *   arrows          -> SCE_CTRL_UP/DOWN/LEFT/RIGHT   (scroll / focus)
 *   Enter           -> SCE_CTRL_CROSS               (activate)
 *   Esc, Backspace  -> SCE_CTRL_CIRCLE              (cancel / back)
 *   PgUp / PgDn     -> SCE_CTRL_LTRIGGER / RTRIGGER  (page-at-a-time)
 *
 * Everything else (menus, settings, devtools customize, JS button-wait
 * pages) reacts to those bits through the existing controller paths, so
 * keyboard behavior can never drift from controller behavior.
 *
 * The mouse is RELATIVE (rel_x/rel_y); we keep an absolute virtual
 * cursor (centered at boot, clamped to the screen) and report edges so
 * the caller can run the same tap/cancel flow the rear touchpad uses.
 */
#include <string.h>
#include "cwv_kbm_vita.h"
#include <psp2/hid.h>

/* ---- connection state ------------------------------------------------ */

#define KBM_MAX_KBD   4
#define KBM_ENUM_EVERY 300   /* frames between re-enumeration attempts */

static int  s_kbd[KBM_MAX_KBD];        /* keyboard handles               */
static int  s_nkbd;
static unsigned char s_kbd_keys[KBM_MAX_KBD][6];  /* last report keys  */
static int  s_mouse = -1;              /* mouse handle, -1 = none        */
static int  s_enum_frame;              /* frames since last enumeration  */
static int  s_have_enumerated;

/* Virtual mouse cursor */
static int  s_mx, s_my;                /* absolute position             */
static int  s_mleft, s_mright;         /* held buttons                  */
static int  s_wheel_acc;               /* wheel steps not yet consumed  */

/* Screen size for clamping - set from main via cwv_kbm_vita_set_screen() */
static int s_screen_w = 960, s_screen_h = 544;

/* Boot-protocol HID usage IDs (subset; anything else is ignored) */
#define HIDK_ENTER      0x28
#define HIDK_ESCAPE     0x29
#define HIDK_BACKSPACE  0x2A
#define HIDK_RIGHT      0x4F
#define HIDK_LEFT       0x50
#define HIDK_DOWN       0x51
#define HIDK_UP         0x52
#define HIDK_PGUP       0x4B
#define HIDK_PGDN       0x4E

void cwv_kbm_vita_set_screen(int w, int h)
{
    if (w > 0 && h > 0) {
        s_screen_w = w;
        s_screen_h = h;
    }
    /* keep the cursor inside the (possibly new) screen */
    if (s_mx >= s_screen_w) s_mx = s_screen_w - 1;
    if (s_my >= s_screen_h) s_my = s_screen_h - 1;
    if (s_mx < 0) s_mx = 0;
    if (s_my < 0) s_my = 0;
}

void cwv_kbm_vita_init(void)
{
    int handles[SCE_HID_MAX_DEVICE_COUNT];
    int n, i;

    s_nkbd = 0;
    memset(s_kbd, 0, sizeof(s_kbd));
    memset(s_kbd_keys, 0, sizeof(s_kbd_keys));

    n = sceHidKeyboardEnumerate(handles, SCE_HID_MAX_DEVICE_COUNT);
    for (i = 0; i < n && s_nkbd < KBM_MAX_KBD; i++)
        s_kbd[s_nkbd++] = handles[i];

    s_mouse = -1;
    n = sceHidMouseEnumerate(handles, SCE_HID_MAX_DEVICE_COUNT);
    if (n > 0)
        s_mouse = handles[0];

    /* first boot: cursor starts centered */
    if (!s_have_enumerated) {
        s_mx = s_screen_w / 2;
        s_my = s_screen_h / 2;
        s_have_enumerated = 1;
    }
    s_enum_frame = 0;
}

/* ---- keyboard: synthesized controller bits -------------------------- */

static unsigned kbd_bits_for(int k)
{
    switch (k) {
    case HIDK_UP:        return SCE_CTRL_UP;
    case HIDK_DOWN:      return SCE_CTRL_DOWN;
    case HIDK_LEFT:      return SCE_CTRL_LEFT;
    case HIDK_RIGHT:     return SCE_CTRL_RIGHT;
    case HIDK_ENTER:     return SCE_CTRL_CROSS;
    case HIDK_ESCAPE:
    case HIDK_BACKSPACE: return SCE_CTRL_CIRCLE;
    case HIDK_PGUP:      return SCE_CTRL_LTRIGGER;
    case HIDK_PGDN:      return SCE_CTRL_RTRIGGER;
    default:             return 0;
    }
}

unsigned cwv_kbm_vita_ctrl_bits(void)
{
    unsigned bits = 0;
    int k, j;

    for (k = 0; k < s_nkbd; k++) {
        /* Peek is non-blocking: returns whatever new reports are ready.
         * reports[] is an array of POINTERS per the SceHid API. */
        SceHidKeyboardReport rep[SCE_HID_MAX_REPORT];
        SceHidKeyboardReport *ptr[SCE_HID_MAX_REPORT];
        int n, r;
        memset(rep, 0, sizeof(rep));
        for (r = 0; r < SCE_HID_MAX_REPORT; r++)
            ptr[r] = &rep[r];
        n = sceHidKeyboardPeek((SceUInt32)s_kbd[k], ptr,
                               SCE_HID_MAX_REPORT);
        if (n > 0) {
            /* keep the NEWEST report's held keys */
            int last = (n - 1 < SCE_HID_MAX_REPORT) ? n - 1
                                                    : SCE_HID_MAX_REPORT - 1;
            memcpy(s_kbd_keys[k], rep[last].keycodes, 6);
        }
        /* keys still in the last report count as held (boot protocol
         * only sends a report when something changes) */
        for (j = 0; j < 6; j++)
            bits |= kbd_bits_for(s_kbd_keys[k][j]);
    }
    return bits;
}

/* ---- mouse: virtual relative cursor --------------------------------- */

void cwv_kbm_vita_mouse_poll(CwvKbmVitaMouse *m)
{
    SceHidMouseReport rep[SCE_HID_MAX_REPORT];
    SceHidMouseReport *ptr[SCE_HID_MAX_REPORT];
    int n, r, i;
    int left_now = s_mleft, right_now = s_mright;

    if (!m) { s_wheel_acc = 0; return; }

    m->present = (s_mouse >= 0);
    m->x = s_mx;
    m->y = s_my;
    m->left = s_mleft;
    m->right = s_mright;
    m->left_pressed = 0;
    m->left_released = 0;
    m->right_pressed = 0;
    m->wheel = 0;

    /* periodic re-enumeration so a keyboard/mouse paired WHILE the app
     * runs (or woken from sleep) is picked up without a restart */
    if (++s_enum_frame >= KBM_ENUM_EVERY) {
        s_enum_frame = 0;
        if (s_nkbd == 0 && s_mouse < 0)
            cwv_kbm_vita_init();
    }

    if (s_mouse < 0) { s_wheel_acc = 0; return; }

    memset(rep, 0, sizeof(rep));
    for (r = 0; r < SCE_HID_MAX_REPORT; r++)
        ptr[r] = &rep[r];
    n = sceHidMouseRead((SceUInt32)s_mouse, ptr, SCE_HID_MAX_REPORT);
    for (i = 0; i < n && i < SCE_HID_MAX_REPORT; i++) {
        s_mx += rep[i].rel_x;
        s_my += rep[i].rel_y;
        /* buttons: bit0 = left, bit1 = right, bit2 = middle (boot) */
        left_now  = rep[i].buttons & 0x01;
        right_now = (rep[i].buttons >> 1) & 0x01;
        s_wheel_acc += rep[i].wheel;
    }

    /* clamp the virtual cursor to the screen */
    if (s_mx < 0) s_mx = 0;
    if (s_my < 0) s_my = 0;
    if (s_mx >= s_screen_w) s_mx = s_screen_w - 1;
    if (s_my >= s_screen_h) s_my = s_screen_h - 1;

    m->left_pressed  = left_now && !s_mleft;
    m->left_released = !left_now && s_mleft;
    m->right_pressed = right_now && !s_mright;
    s_mleft = left_now;
    s_mright = right_now;

    m->x = s_mx;
    m->y = s_my;
    m->left = s_mleft;
    m->right = s_mright;
    m->wheel = s_wheel_acc;
    s_wheel_acc = 0;
}
