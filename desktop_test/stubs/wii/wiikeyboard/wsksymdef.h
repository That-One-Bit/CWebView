/* Desktop-gate stub mirroring the REAL libogc gc/wiikeyboard/wsksymdef.h
 * subset used by the Wii USB keyboard support (values copied 1:1 from
 * devkitPro/libogc upstream so the desktop gate sees the same symbols
 * the real devkitPPC build sees). */
#ifndef STUB_WSKSYMDEF_H
#define STUB_WSKSYMDEF_H

#define KS_BackSpace        0x08
#define KS_Tab              0x09
#define KS_Return           0x0d
#define KS_Escape           0x1b
#define KS_Delete           0x7f
#define KS_Shift_L          0xf101
#define KS_Shift_R          0xf102
#define KS_F1               0xf340
#define KS_F5               0xf344
#define KS_Home             0xf381
#define KS_Prior            0xf382   /* PageUp */
#define KS_Next             0xf383   /* PageDown */
#define KS_Up               0xf384
#define KS_Down             0xf385
#define KS_Left             0xf386
#define KS_Right            0xf387
#define KS_End              0xf388

#endif
