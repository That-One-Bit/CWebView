/* Desktop-gate stub mirroring the REAL VitaSDK psp2/hid.h (Bluetooth HID
 * keyboards and mice, SceHid).  Structs, constants and prototypes are
 * copied 1:1 from vitasdk/vita-headers include/psp2/hid.h so the desktop
 * gate proves the port compiles against the same API surface the real
 * cross-build sees (build-vita.sh). */
#ifndef STUB_PSP2_HID_H
#define STUB_PSP2_HID_H

#include <psp2/types.h>

typedef struct SceHidKeyboardReport {
    SceUInt8 reserved;
    SceUInt8 modifiers[2]; /* [0] standard Ctrl/Shift/Alt, [1] locks */
    SceUInt8 keycodes[6];
    SceUInt8 reserved2[7];
    SceUInt64 timestamp;   /* microseconds */
} SceHidKeyboardReport;   /* 0x18 bytes upstream */

typedef struct SceHidMouseReport {
    SceUInt8 buttons;      /* bit0 left, bit1 right, bit2 middle */
    SceUInt8 reserved;
    SceInt16 rel_x;
    SceInt16 rel_y;
    SceInt8 wheel;
    SceInt8 tilt;
    SceUInt64 timestamp;   /* microseconds */
} SceHidMouseReport;      /* 0x10 bytes upstream */

#define SCE_HID_MAX_REPORT 16
#define SCE_HID_MAX_DEVICE_COUNT 8

int sceHidKeyboardEnumerate(int *handle, int count);
int sceHidKeyboardRead(SceUInt32 handle, SceHidKeyboardReport *reports[],
                       int nReports);    /* blocking */
int sceHidKeyboardPeek(SceUInt32 handle, SceHidKeyboardReport *reports[],
                       int nReports);   /* non-blocking */

int sceHidMouseEnumerate(int *handle, int count);
int sceHidMouseRead(SceUInt32 handle, SceHidMouseReport *reports[],
                   int nReports);

#endif
