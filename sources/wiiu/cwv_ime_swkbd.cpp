/* cwv_ime_swkbd.cpp - Wii U NATIVE software keyboard (wut's nn::swkbd).
 *
 * API REALITY CHECK (2026-09-20 (13)): the first version of this file
 * was written against a GUESSED API (ShowKeyboard / ControllerArg /
 * GetRequiredStringBufferSize) that does not exist in wut 1.9.1 - it
 * never compiled against the real SDK.  The REAL library model
 * (wut include/nn/swkbd/swkbd_cpp.h, confirmed against v1.9.1):
 *
 *   Create(workMemory + FSClient)  ->  AppearInputForm(AppearArg)
 *   then EVERY FRAME:
 *     VPADRead -> Calc(ControllerInfo)            (input handling)
 *     CalcSubThreadFont / CalcSubThreadPredict   (only when asked for)
 *     DrawTV() / DrawDRC()  LAST in the frame    (on top of the page)
 *   until IsDecideOkButton / IsDecideCancelButton
 *   then GetInputFormString -> DisappearInputForm -> Destroy.
 *
 * INTEGRATION with the SDL2 app loop: cwv_ime_open() only BRINGS UP the
 * keyboard (fast, non-blocking - the engine keeps running; the seam
 * contract in cwv_platform.h is unchanged).  The engine's per-frame
 * cwv_ime_poll() pumps Calc + the decision handling.  main.c calls
 * cwv_ime_draw() (see cwv_ime_swkbd.h) after it has drawn the page and
 * before SDL_RenderPresent(), which is the documented "draw last"
 * placement.
 *
 * Work buffer: aligned_alloc(0x1000, ...) - an unaligned work memory
 * buffer crashes the library (alignment note contributed by the user).
 *
 * HARDWARE CAVEAT (to confirm on the first flash): SDL_RenderPresent()
 * owns the GX2 frame swap; the swkbd draws queue into the same GX2
 * context as SDL's page rendering.  If the keyboard does not appear on
 * hardware, the GX2 context state hand-off between SDL2 and raw swkbd
 * draws is the place to look first.
 */
#include "cwv_ime_swkbd.h"
#include "core/cwv_platform.h"

#include <nn/swkbd/swkbd_cpp.h>
#include <coreinit/filesystem.h>
#include <vpad/input.h>

#include <cstdlib>
#include <cstring>

/* ---- state held while the keyboard is up -------------------------- */
static bool g_active = false;          /* a keyboard is on screen     */
static void *g_work = NULL;            /* swkbd work memory           */
static bool g_fs_up = false;           /* FSInit + client added once  */
static FSClient *g_fs_client = NULL;   /* swkbd loads fonts via it     */
static char16_t *g_hint_text = NULL;   /* must outlive the dialog      */
static char16_t *g_initial_text = NULL;

/* The library keeps the AppearArg pointers; stage the UTF-16 strings on
 * the heap so they stay valid for the whole dialog, then free them at
 * teardown.  Returns NULL for NULL/empty input (the fields default to
 * nullptr and the applet simply shows no hint / no pre-filled text). */
static char16_t *ime_stage_utf16(const cwv_utf16 *in)
{
    if (!in || !in[0]) return NULL;
    size_t n = 0;
    while (in[n]) n++;
    char16_t *out = (char16_t *)malloc((n + 1) * sizeof(char16_t));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) out[i] = (char16_t)in[i];
    out[n] = 0;
    return out;
}

/* Tear the library down and release everything it borrowed. */
static void ime_teardown(void)
{
    nn::swkbd::Destroy();
    free(g_work);
    g_work = NULL;
    free(g_hint_text);
    g_hint_text = NULL;
    free(g_initial_text);
    g_initial_text = NULL;
    g_active = false;
    /* The FSClient stays registered for the app's lifetime - adding and
     * removing it per dialog would just churn the same resource. */
}

/* ---- seam: bring the keyboard up ---------------------------------- */
int cwv_ime_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                 int password, int multiline,
                 cwv_utf16 *buffer, int buffer_len)
{
    (void)buffer;   /* the library owns editing; the text lands in poll */
    if (g_active) return -1;          /* one keyboard at a time */
    if (!buffer || buffer_len <= 1) return -1;

    /* 1. FSClient (one-time): swkbd loads its resources through it. */
    if (!g_fs_up) {
        FSInit();
        g_fs_client = (FSClient *)malloc(sizeof(FSClient));
        if (!g_fs_client) return -1;
        if (FSAddClient(g_fs_client, FS_ERROR_FLAG_NONE) != FS_STATUS_OK) {
            free(g_fs_client);
            g_fs_client = NULL;
            return -1;
        }
        g_fs_up = true;
    }

    /* 2. work memory (aligned - see the header note) + Create. */
    uint32_t work_size = nn::swkbd::GetWorkMemorySize(0);
    g_work = aligned_alloc(0x1000, (size_t)work_size);
    if (!g_work) return -1;

    nn::swkbd::CreateArg createArg;   /* region/language stay English */
    createArg.workMemory = g_work;
    createArg.fsClient = g_fs_client;
    if (!nn::swkbd::Create(createArg)) {
        ime_teardown();
        return -1;
    }

    /* 3. one input form + the full UTF-16 keyboard.
     * Password -> the applet's own hide-the-text mode; single-line
     * (multiline == 0) -> newlines disabled.  KeyboardMode::Full is the
     * only mode that accepts all UTF-16 characters. */
    g_hint_text = ime_stage_utf16(title);
    g_initial_text = ime_stage_utf16(initial);
    nn::swkbd::AppearArg appearArg;
    appearArg.inputFormArg.type = nn::swkbd::InputFormType::Default;
    appearArg.inputFormArg.initialText = g_initial_text;
    appearArg.inputFormArg.hintText = g_hint_text;
    appearArg.inputFormArg.maxTextLength = buffer_len - 1;
    appearArg.inputFormArg.passwordMode = password
        ? nn::swkbd::PasswordMode::Hide
        : nn::swkbd::PasswordMode::Clear;
    appearArg.keyboardArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Full;
    appearArg.keyboardArg.configArg.disableNewLine = !multiline;
    if (!nn::swkbd::AppearInputForm(appearArg)) {
        ime_teardown();
        return -1;
    }

    g_active = true;
    return 0;
}

/* ---- seam: per-frame pump + outcome -------------------------------- */
int cwv_ime_poll(cwv_utf16 *buffer, int buffer_len)
{
    if (!g_active) return 0;

    /* Fresh GamePad data drives the keyboard's own UI: its touch panel
     * presses the on-screen keys, its OK button commits and its B button
     * cancels.  On a read error, hand swkbd no controller this frame
     * rather than garbage. */
    VPADStatus vpad;
    VPADReadError vpad_err = VPAD_READ_NO_SAMPLES;
    VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_err);
    if (vpad_err == VPAD_READ_SUCCESS)
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpad.tpNormal, &vpad.tpNormal);

    nn::swkbd::ControllerInfo info;
    info.vpad = (vpad_err == VPAD_READ_SUCCESS) ? &vpad : NULL;
    info.kpad[0] = NULL; info.kpad[1] = NULL;
    info.kpad[2] = NULL; info.kpad[3] = NULL;
    nn::swkbd::Calc(info);
    if (nn::swkbd::IsNeedCalcSubThreadFont())
        nn::swkbd::CalcSubThreadFont();
    if (nn::swkbd::IsNeedCalcSubThreadPredict())
        nn::swkbd::CalcSubThreadPredict();

    /* Decided yet?  (0 = still open, keep drawing.) */
    bool ok = nn::swkbd::IsDecideOkButton(NULL);
    bool cancelled = nn::swkbd::IsDecideCancelButton(NULL);
    if (!ok && !cancelled) return 0;

    int result = 2;
    if (ok && buffer && buffer_len > 0) {
        /* Copy the result out before teardown frees the library. */
        const char16_t *text = nn::swkbd::GetInputFormString();
        int n = 0;
        while (text && text[n] && n < buffer_len - 1) {
            buffer[n] = (cwv_utf16)text[n];
            n++;
        }
        buffer[n] = 0;
        result = 1;
    }
    nn::swkbd::DisappearInputForm();
    ime_teardown();
    return result;
}

/* ---- draw hook for main.c (see cwv_ime_swkbd.h) -------------------- */
void cwv_ime_draw(void)
{
    if (!g_active) return;
    /* LAST in the frame (wut: "after rendering all other TV graphics")
     * so the keyboard draws on top of the page.  The TV has no page
     * content in this port, but the system keyboard shows on both
     * screens, matching console-wide behavior. */
    nn::swkbd::DrawTV();
    nn::swkbd::DrawDRC();
}
