/* Minimal psp2/common_dialog.h stub for the desktop main.c syntax gate.
 * Only what main.c uses at boot: the config param types + setters.
 * (The IME dialog types live in the ime_dialog.h stub.) */
#ifndef VW_STUB_PSP2_COMMON_DIALOG_H
#define VW_STUB_PSP2_COMMON_DIALOG_H

#include <psp2/types.h>
#include <psp2/system_param.h>

typedef struct SceCommonDialogConfigParam {
    unsigned int sdkVersion;
    SceSystemParamLang language;
    SceSystemParamEnterButtonAssign enterButtonAssign;
    unsigned char reserved[32];
} SceCommonDialogConfigParam;

int sceCommonDialogConfigParamInit(SceCommonDialogConfigParam *param);
int sceCommonDialogSetConfigParam(const SceCommonDialogConfigParam *configParam);

#endif
