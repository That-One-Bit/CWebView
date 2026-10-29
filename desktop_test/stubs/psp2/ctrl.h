#ifndef MS_CTRL_H
#define MS_CTRL_H
#define SCE_CTRL_MODE_ANALOG 1
#define SCE_CTRL_MODE_DIGITAL 0
#define SCE_CTRL_SELECT 0x1
#define SCE_CTRL_START 0x8
#define SCE_CTRL_UP 0x10
#define SCE_CTRL_RIGHT 0x20
#define SCE_CTRL_DOWN 0x40
#define SCE_CTRL_LEFT 0x80
#define SCE_CTRL_L1 0x4000
#define SCE_CTRL_R1 0x8000
#define SCE_CTRL_SQUARE 0x8000
#define SCE_CTRL_CROSS 0x10000
#define SCE_CTRL_CIRCLE 0x20000
#define SCE_CTRL_TRIANGLE 0x40000
#define SCE_CTRL_LTRIGGER 0x100
#define SCE_CTRL_RTRIGGER 0x200
#define SCE_CTRL_L2 0x100000
#define SCE_CTRL_R2 0x200000
#define SCE_CTRL_L3 0x40000
#define SCE_CTRL_R3 0x80000
typedef struct SceCtrlData {
    unsigned int timeStamp; unsigned int buttons;
    unsigned char lx,ly,rx,ry;
    unsigned char up,right,down,left;
    unsigned char triangle,circle,cross,square;
    unsigned char lt,rt,l1,r1,l2,r2,lsel,rsel;
    unsigned char rsrv[16];
} SceCtrlData;
int sceCtrlSetSamplingMode(int);
int sceCtrlSetSamplingModeExt(int);
int sceCtrlPeekBufferPositive(int,SceCtrlData*,int);
int sceCtrlPeekBufferPositiveExt(int,SceCtrlData*,int);
int sceCtrlReadBufferPositive(int,SceCtrlData*,int);
#endif
