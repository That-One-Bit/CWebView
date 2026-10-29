/* Desktop test stub for <psp2/ime_dialog.h> (field names match the real SDK) */
#ifndef STUB_PSP2_IME_H
#define STUB_PSP2_IME_H
typedef unsigned short SceWChar16;
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH 2048
#define SCE_IME_TYPE_DEFAULT 0
#define SCE_IME_OPTION_MULTILINE 1
#define SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL 1
#define SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT 0
#define SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD 1
#define SCE_IME_DIALOG_BUTTON_ENTER 0
#define SCE_COMMON_DIALOG_STATUS_FINISHED 1
typedef struct SceImeDialogParam {
    unsigned supportedLanguages;
    unsigned languagesForced;
    unsigned type;
    unsigned option;
    unsigned textBoxMode;
    unsigned dialogMode;
    const SceWChar16 *title;
    const SceWChar16 *initialText;
    SceWChar16 *inputTextBuffer;
    unsigned maxTextLength;
    int enterButtonLabel;
    int cancelButtonLabel;
    const SceWChar16 *reserved;
} SceImeDialogParam;
typedef struct SceImeDialogResult {
    int button;
    SceWChar16 *text;
    char *reserved;
} SceImeDialogResult;
void sceImeDialogParamInit(SceImeDialogParam *param);
int sceImeDialogInit(SceImeDialogParam *param);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(SceImeDialogResult *result);
int sceImeDialogTerm(void);
#endif
