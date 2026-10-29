/* Minimal psp2/sysmodule.h stub for the desktop syntax gate ONLY. */
#ifndef VW_STUB_PSP2_SYSMODULE_H
#define VW_STUB_PSP2_SYSMODULE_H

#define SCE_SYSMODULE_IME    0x11
#define SCE_SYSMODULE_HTTP   0x02
#define SCE_SYSMODULE_SSL    0x03
#define SCE_SYSMODULE_APPUTIL 0x0F

int sceSysmoduleLoadModule(unsigned int id);
int sceSysmoduleUnloadModule(unsigned int id);

#endif
