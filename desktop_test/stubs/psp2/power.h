#ifndef CWV_STUB_PSP2_POWER_H
#define CWV_STUB_PSP2_POWER_H
#include <psp2/types.h>
#define SCE_POWER_CB_AFTER_SYSTEM_RESUME 0x00000080
#define SCE_POWER_CB_SYSTEM_SUSPEND      0x00010000
#define SCE_POWER_CB_SYSTEM_RESUMING     0x00020000
#define SCE_POWER_CB_SYSTEM_RESUME       0x00040000
#define SCE_POWER_CB_APP_RESUME          0x00200000
#define SCE_POWER_CB_APP_SUSPEND         0x00400000
#define SCE_POWER_CB_APP_RESUMING        0x00800000
int scePowerRegisterCallback(SceUID cbid);
int scePowerUnregisterCallback(SceUID cbid);
#endif
