/* Minimal psp2/apputil.h stub for the desktop syntax gate ONLY. */
#ifndef VW_STUB_PSP2_APPUTIL_H
#define VW_STUB_PSP2_APPUTIL_H
#include <psp2/types.h>
#include <stddef.h>
typedef struct { char dummy; } SceAppUtilInitParam;
typedef struct { char dummy; } SceAppUtilBootParam;
typedef struct { const char *str; size_t strlen; int launchMode; } SceAppUtilWebBrowserParam;
int sceAppUtilInit(const SceAppUtilInitParam *ip, const SceAppUtilBootParam *bp);
int sceAppUtilLaunchWebBrowser(const SceAppUtilWebBrowserParam *param);
int sceAppUtilShutdown(void);
int sceAppUtilSystemParamGetInt(unsigned int paramId, int *value);
#endif
