/* Minimal desktop stub of wut 1.9.1's nn::swkbd C++ surface.  Only for
 * syntax-checking sources/wiiu/cwv_ime_swkbd.cpp (build_wiiu_gate.sh);
 * the real build uses devkitPro wut.  Mirrors the REAL header's names
 * and field paths exactly (enums, CreateArg/AppearArg/InputFormArg,
 * Create, AppearInputForm, Calc, IsDecide, GetInputFormString, Draw) so a
 * guess at an API that wut does not provide fails HERE, at the gate,
 * instead of in the middle of a devkitPPC build. */
#ifndef STUB_NN_SWKBD_CPP_H
#define STUB_NN_SWKBD_CPP_H
#include <cstdint>

namespace nn { namespace swkbd {

enum class ControllerType { WiiRemote0, WiiRemote1, WiiRemote2,
                             WiiRemote3, DrcGamepad };
enum class LanguageType { Japanese, English, French, German, Italian,
                          Spanish, SimplifiedChinese, Korean, Dutch,
                          Portuguese, Russian, TraditionalChinese };
enum class RegionType { Japan, USA, Europe, China, Korea, Taiwan };
enum class State { Hidden, FadeIn, Visible, FadeOut };
enum class InputFormType { InputForm0, Default };
enum class KeyboardLayout { LanguageDefault };
enum class KeyboardMode { Full, Numpad, Utf8, NNID };
enum class PasswordMode { Clear, Hide, Fade };

struct ConfigArg {
    LanguageType languageType;
    ControllerType controllerType;
    KeyboardMode keyboardMode;
    uint32_t accessFlags;
    KeyboardLayout keyboardLayout;
    int32_t keyboardTab;
    bool disableNewLine;
};
struct ReceiverArg { uint32_t unk; };
struct KeyboardArg { ConfigArg configArg; ReceiverArg receiverArg; };
struct InputFormArg {
    InputFormType type;
    const char16_t *initialText;
    const char16_t *hintText;
    int32_t maxTextLength;
    PasswordMode passwordMode;
};
struct AppearArg { KeyboardArg keyboardArg; InputFormArg inputFormArg; };
struct CreateArg { void *workMemory; RegionType regionType; void *fsClient; };
struct ControllerInfo { void *vpad; void *kpad[4]; };

uint32_t GetWorkMemorySize(uint32_t unk);
bool Create(const CreateArg &args);
void Destroy(void);
bool AppearInputForm(const AppearArg &args);
bool DisappearInputForm(void);
void Calc(const ControllerInfo &controllerInfo);
bool IsNeedCalcSubThreadFont(void);
bool IsNeedCalcSubThreadPredict(void);
void CalcSubThreadFont(void);
void CalcSubThreadPredict(void);
bool IsDecideOkButton(bool *outIsSelected);
bool IsDecideCancelButton(bool *outIsSelected);
const char16_t *GetInputFormString(void);
State GetStateInputForm(void);
void DrawTV(void);
void DrawDRC(void);

} }
#endif
