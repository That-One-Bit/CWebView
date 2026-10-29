/* Desktop stub of the libogc PAD surface (see stubs/wii/grrlib.h). */
#ifndef STUB_WII_PAD_H
#define STUB_WII_PAD_H
#include <stdint.h>
typedef uint32_t u32;
#define PAD_BUTTON_LEFT  0x0010
#define PAD_BUTTON_RIGHT 0x0020
#define PAD_BUTTON_DOWN  0x0004
#define PAD_BUTTON_UP    0x0008
#define PAD_BUTTON_A     0x0100
#define PAD_BUTTON_B     0x0200
#define PAD_BUTTON_START 0x1000
#define PAD_BUTTON_UP   0x0008
#define PAD_BUTTON_DOWN 0x0004
void PAD_Init(void);
void PAD_ScanPads(void);
u32 PAD_ButtonsDown(int chan);
u32 PAD_ButtonsHeld(int chan);
u32 PAD_ButtonsUp(int chan);
#endif
