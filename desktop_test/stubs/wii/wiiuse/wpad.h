/* Desktop stub of the libogc WPAD surface (see stubs/wii/grrlib.h).
 * Button masks, channel enums, IR structs and data formats mirror the
 * REAL libogc include/wiiuse/wpad.h (verified against devkitPro/libogc master)
 * so the desktop gate catches wrong constants, not just syntax. */
#ifndef STUB_WII_WPAD_H
#define STUB_WII_WPAD_H
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint8_t u8;

enum { WPAD_CHAN_ALL = -1, WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2,
       WPAD_CHAN_3, WPAD_BALANCE_BOARD, WPAD_CHAN_UNKNOWN, WPAD_MAX_DEVICES };

/* Real libogc button masks (gc/wiiuse/wpad.h). */
#define WPAD_BUTTON_2     0x0001
#define WPAD_BUTTON_1     0x0002
#define WPAD_BUTTON_B     0x0004
#define WPAD_BUTTON_A     0x0008
#define WPAD_BUTTON_MINUS 0x0010
#define WPAD_BUTTON_HOME  0x0080
#define WPAD_BUTTON_LEFT  0x0100
#define WPAD_BUTTON_RIGHT 0x0200
#define WPAD_BUTTON_DOWN  0x0400
#define WPAD_BUTTON_UP    0x0800
#define WPAD_BUTTON_PLUS  0x1000

enum { WPAD_FMT_BTNS = 0, WPAD_FMT_BTNS_ACC, WPAD_FMT_BTNS_ACC_IR };
enum { WPAD_EXP_NONE = 0, WPAD_EXP_NUNCHUK, WPAD_EXP_CLASSIC,
       WPAD_EXP_GUITARHERO3, WPAD_EXP_WIIBOARD, WPAD_EXP_UNKNOWN = 255 };

/* Subset of libogc ir_t used by this port: x/y are SCREEN pixels
 * (WPAD_SetVRes maps the sensor bar to that resolution), angle in
 * degrees, valid nonzero while the pointer is tracked. */
typedef struct ir_t { int x, y; float angle; u8 valid; } ir_t;

typedef struct _wpad_data {
    s32 err;
    u32 data_present;
    u8 battery_level;
    u32 btns_h, btns_l, btns_d, btns_u;
    struct ir_t ir;
} WPADData;

s32 WPAD_Init(void);
s32 WPAD_ScanPads(void);
s32 WPAD_SetDataFormat(s32 chan, s32 fmt);
s32 WPAD_SetVRes(s32 chan, u32 xres, u32 yres);
WPADData *WPAD_Data(int chan);
u32 WPAD_ButtonsUp(int chan);
u32 WPAD_ButtonsDown(int chan);
u32 WPAD_ButtonsHeld(int chan);
#endif
