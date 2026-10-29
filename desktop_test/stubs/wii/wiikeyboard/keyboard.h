/*-------------------------------------------------------------

usbkeyboard.h / keyboard.h stub -- desktop gate only

Mirrors the REAL libogc gc/wiikeyboard headers (devkitPro/libogc
upstream, copied 1:1) so the Wii port's USB keyboard support can be
syntax-checked on the desktop. build-wii.sh compiles against the real
devkitPPC headers.

-------------------------------------------------------------*/

#ifndef STUB_WIIKEYBOARD_H
#define STUB_WIIKEYBOARD_H

#include <stdint.h>
#include "wsksymdef.h"
typedef int32_t s32;
typedef uint16_t u16;
typedef uint8_t u8;

#define MOD_SHIFT_L             (1 << 0)
#define MOD_SHIFT_R             (1 << 1)
#define MOD_SHIFTLOCK           (1 << 2)
#define MOD_CAPSLOCK            (1 << 3)
#define MOD_CONTROL_L           (1 << 4)
#define MOD_CONTROL_R           (1 << 5)
#define MOD_META_L              (1 << 6)
#define MOD_META_R              (1 << 7)
#define MOD_MODESHIFT           (1 << 8)
#define MOD_NUMLOCK             (1 << 9)
#define MOD_COMPOSE             (1 << 10)
#define MOD_HOLDSCREEN          (1 << 11)
#define MOD_COMMAND             (1 << 12)
#define MOD_COMMAND1            (1 << 13)
#define MOD_COMMAND2            (1 << 14)
#define MOD_MODELOCK            (1 << 15)

#define MOD_ANYSHIFT            (MOD_SHIFT_L | MOD_SHIFT_R | MOD_SHIFTLOCK)
#define MOD_ANYCONTROL          (MOD_CONTROL_L | MOD_CONTROL_R)
#define MOD_ANYMETA             (MOD_META_L | MOD_META_R)

typedef enum {
    KEYBOARD_CONNECTED,
    KEYBOARD_DISCONNECTED,
    KEYBOARD_PRESSED,
    KEYBOARD_RELEASED
} keyboard_event_type;

typedef struct {
    keyboard_event_type type;
    u16 modifiers;
    u8 keycode;
    u16 symbol;
} keyboard_event;

typedef void (*keyPressCallback)(char symbol);

s32 KEYBOARD_Init(keyPressCallback keypress_cb);
s32 KEYBOARD_Deinit(void);

s32 KEYBOARD_GetEvent(keyboard_event *event);
s32 KEYBOARD_FlushEvents(void);

#endif
