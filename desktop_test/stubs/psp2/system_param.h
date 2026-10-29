/* Minimal psp2/system_param.h stub for the desktop main.c syntax gate.
 * Mirrors the real vitasdk header (enum names/values match). */
#ifndef VW_STUB_PSP2_SYSTEM_PARAM_H
#define VW_STUB_PSP2_SYSTEM_PARAM_H

typedef enum SceSystemParamId {
    SCE_SYSTEM_PARAM_ID_LANG = 0,
    SCE_SYSTEM_PARAM_ID_ENTER_BUTTON,
    SCE_SYSTEM_PARAM_ID_USERNAME,
    SCE_SYSTEM_PARAM_ID_DATE_FORMAT,
    SCE_SYSTEM_PARAM_ID_TIME_FORMAT,
    SCE_SYSTEM_PARAM_ID_MAX_VALUE = 0xFFFFFFFF
} SceSystemParamId;

typedef enum SceSystemParamLang {
    SCE_SYSTEM_PARAM_LANG_JAPANESE,
    SCE_SYSTEM_PARAM_LANG_ENGLISH_US
} SceSystemParamLang;

typedef enum SceSystemParamEnterButtonAssign {
    SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS = 0,
    SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE = 1
} SceSystemParamEnterButtonAssign;

#endif
