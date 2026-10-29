/* Desktop stub of wut's padscore/wpad.h - ONLY the surface used by
 * sources/wiiu/main.c (Wiimote IR cursor).  Mirrors wut 1.9.1; the real
 * build uses devkitPPC's padscore headers. */
#ifndef STUB_WUT_PADSCORE_WPAD_H
#define STUB_WUT_PADSCORE_WPAD_H
#include <stdint.h>

typedef enum WPADChan {
    WPAD_CHAN_0 = 0,
    WPAD_CHAN_1 = 1,
    WPAD_CHAN_2 = 2,
    WPAD_CHAN_3 = 3,
    WPAD_CHAN_MAX = 4
} WPADChan;

typedef enum WPADExtensionType {
    WPAD_EXT_NONE = 0,
    WPAD_EXT_CORE = 2,
    WPAD_EXT_NUNCHUK = 3,
    WPAD_EXT_CLASSIC = 4,
    WPAD_EXT_MPLUS = 5,
    WPAD_EXT_MPLUS_NUNCHUK = 6,
    WPAD_EXT_MPLUS_CLASSIC = 7,
    WPAD_EXT_PRO_CONTROLLER = 31
} WPADExtensionType;

#endif
