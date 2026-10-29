/* cwv_ime_wii.c - Wii software on-screen keyboard (there is no IME in
 * libogc; this is the Wii's answer to the Vita OSK and the Wii U swkbd).
 *
 * UX model (mirrors the rest of the Wii port):
 *   - IR pointer + A taps keys directly (the Wii's primary input);
 *   - D-pad moves a highlight, A activates it (GameCube pad included);
 *   - B cancels; OK applies (engine reads the UTF-16 buffer, poll 1);
 *   - SHIFT one-shots capitals in the letter layout; 123/ABC switches
 *     the letter/digit-symbol layouts.
 * Drawn with the engine's own primitives through the pgf text seam, so
 * no SDK leaks past cwv_platform.c (the desktop Wii gate compiles this
 * file against the same libogc-mirroring stubs as the rest of the port).
 */
#include "cwv_ime_wii.h"
#include <grrlib.h>
#include <string.h>

/* ---- geometry (640x480, panel in the lower half) --------------------- */
#define IME_W           600
#define IME_X           20
#define IME_Y           140
#define IME_H           330
#define IME_TITLE_Y     152
#define IME_FIELD_X     40
#define IME_FIELD_Y     176
#define IME_FIELD_W     560
#define IME_FIELD_H     34
#define IME_KEYS_X      40
#define IME_KEY_W       48
#define IME_KEY_H       36
#define IME_KEYS_ROW0   220
#define IME_ROW_PITCH   40
#define IME_ACT_Y       352
#define IME_ACT_H       44

/* letter layouts: 10 wide, 3 letter rows; 4th row is empty */
static const char k_letters[2][3][11] = {
    { "qwertyuiop", "asdfghjkl", "zxcvbnm" },
    { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" }
};
/* digit/symbol layout (layout 2): three full rows */
static const char k_symbols[3][11] = {
    "1234567890",
    ".,:;!?-()",
    "+*%=@\"[]"
};

/* action row: key ids < 0 are commands */
#define IME_ACT_SHIFT  (-1)
#define IME_ACT_LAYOUT (-2)
#define IME_ACT_SPACE  (-3)
#define IME_ACT_BKSP   (-4)
#define IME_ACT_OK     (-5)
#define IME_ACT_NONE   (-99)

typedef struct {
    int x, w;              /* on the action row */
    int key;               /* IME_ACT_* or IME_ACT_NONE (inert slot) */
    const char *label;
} ImeAction;

/* SHIFT is inert in the symbol layout; the row keeps 5 uniform slots. */
static const ImeAction k_actions_letters[5] = {
    {  40,  90, IME_ACT_SHIFT,  "SHIFT" },
    { 134,  90, IME_ACT_LAYOUT, "123"   },
    { 228, 180, IME_ACT_SPACE,  "SPACE" },
    { 412,  90, IME_ACT_BKSP,   "BKSP"  },
    { 506,  94, IME_ACT_OK,     "OK"    }
};
static const ImeAction k_actions_symbols[5] = {
    {  40,  90, IME_ACT_NONE,   ""      },
    { 134,  90, IME_ACT_LAYOUT, "ABC"   },
    { 228, 180, IME_ACT_SPACE,  "SPACE" },
    { 412,  90, IME_ACT_BKSP,   "BKSP"  },
    { 506,  94, IME_ACT_OK,     "OK"    }
};

/* ---- state ----------------------------------------------------------- */
static struct {
    int active;             /* keyboard on screen                     */
    int result;             /* 1 = OK (Enter), 2 = closed otherwise   */
    int password;
    cwv_utf16 *buf;         /* engine-owned buffer, edited in place   */
    int buf_len;            /* capacity in UTF-16 units               */
    int nchars;             /* characters typed                        */
    int shift;              /* one-shot capitals                        */
    int layout;             /* 0 letters, 1 digits/symbols             */
    int sel_row, sel_col;   /* highlight (3 = action row)              */
    int sel_act;            /* highlight slot on the action row        */
    unsigned frames;        /* caret blink                              */
} ime;

/* UTF-16 helpers: keys are ASCII, so a BMP (no surrogate) converter
 * covers every layout glyph; existing content is copied verbatim. */
static int u16_len(const cwv_utf16 *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}
static void u16_to_utf8(const cwv_utf16 *in, int n, char *out, size_t cap) {
    size_t o = 0;
    int i;
    for (i = 0; i < n && o + 4 < cap; i++) {
        unsigned c = (unsigned)in[i];
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) {                 /* 2-byte BMP */
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {                              /* 3-byte BMP */
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = 0;
}

/* ---- engine seam ------------------------------------------------------ */
int cwv_ime_wii_open(const cwv_utf16 *title, const cwv_utf16 *initial,
                     int password, int multiline,
                     cwv_utf16 *buffer, int buffer_len) {
    (void)title; (void)multiline;
    if (!buffer || buffer_len < 4) return -1;
    memset(&ime, 0, sizeof(ime));
    ime.buf = buffer;
    ime.buf_len = buffer_len;
    ime.password = password ? 1 : 0;
    ime.result = 2;
    /* prefill: copy the initial text into the live buffer */
    {
        int n = u16_len(initial);
        if (n >= buffer_len) n = buffer_len - 1;
        if (n > 0) {
            memcpy(buffer, initial, (size_t)n * sizeof(cwv_utf16));
            ime.nchars = n;
        }
        buffer[ime.nchars] = 0;
    }
    ime.active = 1;
    ime.sel_row = 0;
    ime.sel_col = 0;
    ime.sel_act = -1;
    return 0;
}

int cwv_ime_wii_active(void) { return ime.active; }

int cwv_ime_wii_poll(cwv_utf16 *buffer, int buffer_len) {
    (void)buffer; (void)buffer_len;   /* edits land in the live buffer */
    if (!ime.active) return ime.result;
    return 0;
}

/* ---- editing --------------------------------------------------------- */
static void ime_close(int ok) {
    ime.result = ok ? 1 : 2;
    ime.active = 0;
    if (ime.buf) ime.buf[ime.nchars] = 0;
}
static void ime_insert(unsigned ch) {
    if (ime.nchars + 1 >= ime.buf_len) return;
    ime.buf[ime.nchars++] = (cwv_utf16)ch;
    ime.buf[ime.nchars] = 0;
}
static void ime_backspace(void) {
    if (ime.nchars > 0) ime.buf[--ime.nchars] = 0;
}

static void ime_press_key(int key) {
    if (key >= 0x20 && key < 0x80) {
        ime_insert((unsigned)key);
        if (ime.shift) ime.shift = 0;   /* one-shot capitals */
    } else {
        switch (key) {
        case IME_ACT_SHIFT:  ime.shift = !ime.shift; break;
        case IME_ACT_LAYOUT: ime.layout = !ime.layout;
                             ime.shift = 0; break;
        case IME_ACT_SPACE:  ime_insert(' '); break;
        case IME_ACT_BKSP:   ime_backspace(); break;
        case IME_ACT_OK:     ime_close(1); break;
        default: break;                 /* inert slot */
        }
    }
}

/* letter for (row, col) under the current layout/shift state */
static int ime_letter_key(int row, int col) {
    char c;
    if (ime.layout == 0) {
        int s = ime.shift != 0;
        if (row > 2) return IME_ACT_NONE;
        c = k_letters[s][row][col];
        if (!c) return IME_ACT_NONE;
        return (int)(unsigned char)c;
    }
    if (row > 2) return IME_ACT_NONE;
    c = k_symbols[row][col];
    if (!c) return IME_ACT_NONE;
    return (int)(unsigned char)c;
}

/* ---- input ----------------------------------------------------------- */
void cwv_ime_wii_input(int event) {
    if (!ime.active) return;
    switch (event) {
    case CWV_IME_W_UP:
        if (ime.sel_row < 3) { ime.sel_row = 3; ime.sel_act = 4; }
        else { ime.sel_row = 2; ime.sel_col = 0; ime.sel_act = -1; }
        break;
    case CWV_IME_W_DOWN:
        if (ime.sel_row < 3) { ime.sel_row = 3; ime.sel_act = 0; }
        else { ime.sel_row = 0; ime.sel_act = -1; }
        break;
    case CWV_IME_W_LEFT:
        if (ime.sel_row == 3 && ime.sel_act >= 0) {
            ime.sel_act = (ime.sel_act + 4) % 5;
            break;
        }
        ime.sel_col = (ime.sel_col + 9) % 10;
        break;
    case CWV_IME_W_RIGHT:
        if (ime.sel_row == 3 && ime.sel_act >= 0) {
            ime.sel_act = (ime.sel_act + 1) % 5;
            break;
        }
        ime.sel_col = (ime.sel_col + 1) % 10;
        break;
    case CWV_IME_W_CONFIRM:
        if (ime.sel_row == 3) {
            if (ime.sel_act >= 0) {
                const ImeAction *a = ime.layout ? k_actions_symbols
                                                : k_actions_letters;
                ime_press_key(a[ime.sel_act].key);
            }
        } else {
            ime_press_key(ime_letter_key(ime.sel_row, ime.sel_col));
        }
        break;
    case CWV_IME_W_CANCEL:
        ime_close(0);
        break;
    default:
        break;
    }
}

/* ---- pointer --------------------------------------------------------- */
static int ime_action_at(int x, int y, int *key) {
    const ImeAction *acts = ime.layout ? k_actions_symbols
                                       : k_actions_letters;
    int i;
    if (y < IME_ACT_Y || y >= IME_ACT_Y + IME_ACT_H) return 0;
    for (i = 0; i < 5; i++) {
        if (x >= acts[i].x && x < acts[i].x + acts[i].w) {
            *key = acts[i].key;
            return 1;
        }
    }
    return 0;
}
static int ime_letter_at(int x, int y, int *key) {
    int row, col;
    if (y < IME_KEYS_ROW0 || y >= IME_KEYS_ROW0 + 3 * IME_ROW_PITCH)
        return 0;
    if (x < IME_KEYS_X || x >= IME_KEYS_X + 10 * IME_KEY_W) return 0;
    row = (y - IME_KEYS_ROW0) / IME_ROW_PITCH;
    col = (x - IME_KEYS_X) / IME_KEY_W;
    *key = ime_letter_key(row, col);
    return *key != IME_ACT_NONE;
}
void cwv_ime_wii_pointer(int x, int y) {
    int key;
    if (!ime.active) return;
    if (x < IME_X || x >= IME_X + IME_W) return;
    if (y < IME_KEYS_ROW0 - 8) return;      /* title/field: not keys */
    if (ime_action_at(x, y, &key) || ime_letter_at(x, y, &key))
        ime_press_key(key);
}

/* ---- draw ------------------------------------------------------------ */
/* GRRLIB colors are 0xRRGGBBAA (see cwv_to_grrlib in cwv_platform.c). */
#define COL_DIM      0x000000B4
#define COL_PANEL    0x2E3440FF
#define COL_FIELD    0x1B1F26FF
#define COL_KEY      0x4C566AFF
#define COL_KEY_SEL  0x88A2C0FF
#define COL_OK       0x3D6B4FFF
#define COL_OK_SEL   0x5E9B7CFF
#define COL_BORDER   0x909CB0FF

static void ime_label(void *font, int cx, int top, float px,
                      unsigned engine_color, const char *text) {
    /* engine text colors are ABGR (cwv_pgf_* contract); the pgf seam
     * converts, so pass ABGR straight through.  Vertical anchor: the
     * pgf seam draws TOP-anchored, so bias down from the cell top. */
    int w = cwv_pgf_text_width(font, px / 32.0f, text);
    cwv_pgf_draw_text(font, (float)(cx - w / 2),
                      (float)(top + (int)(px * 0.5f) - 2),
                      engine_color, px / 32.0f, text);
}
void cwv_ime_wii_draw(void *font) {
    char tmp[512];
    int row, col, i;
    const ImeAction *acts;
    if (!ime.active) return;
    ime.frames++;

    GRRLIB_Rectangle(0, 0, 640, 480, COL_DIM, 1);          /* dim page */
    GRRLIB_Rectangle((f32)IME_X, (f32)IME_Y, (f32)IME_W, (f32)IME_H,
                     COL_PANEL, 1);
    GRRLIB_Rectangle((f32)IME_X, (f32)IME_Y, (f32)IME_W, 2.0f,
                     COL_BORDER, 1);
    if (!font) return;

    /* title (fixed, small) */
    ime_label(font, IME_X + IME_W / 2, IME_TITLE_Y, 14.0f,
              0xFFC0C8D0, "Open URL / text entry");

    /* text field with content (or dots) + blinking caret */
    GRRLIB_Rectangle((f32)IME_FIELD_X, (f32)IME_FIELD_Y,
                     (f32)IME_FIELD_W, (f32)IME_FIELD_H, COL_FIELD, 1);
    {
        const float size = 18.0f / 32.0f;   /* 18px text */
        int n = ime.nchars;
        int max_w = IME_FIELD_W - 20;
        int caret_x = IME_FIELD_X + 8;
        char vis[192];
        const char *v = NULL;

        if (ime.password) {
            int show = n > 120 ? 120 : n;   /* dots for the tail */
            int k;
            for (k = 0; k < show; k++) vis[k] = '*';
            vis[show] = 0;
            v = vis;
            while (show > 0 &&
                   cwv_pgf_text_width(font, size, v) > max_w) {
                vis[--show] = 0;            /* drop front dot */
                v = vis + (0);
            }
        } else {
            /* keep the TAIL visible when the field overflows */
            int show = n;
            while (show > 0) {
                u16_to_utf8(ime.buf + (n - show), show,
                            vis, sizeof(vis));
                if (cwv_pgf_text_width(font, size, vis) <= max_w)
                    break;
                show--;
            }
            v = vis;
        }
        if (n > 0 && v) {
            int w = cwv_pgf_text_width(font, size, v);
            cwv_pgf_draw_text(font, (float)(IME_FIELD_X + 8),
                              (float)(IME_FIELD_Y + 26), 0xFFE8E8E8,
                              size, v);
            caret_x = IME_FIELD_X + 8 + w + 2;
        }
        if ((ime.frames / 32) % 2 == 0)
            GRRLIB_Rectangle((f32)caret_x, (f32)(IME_FIELD_Y + 7),
                             2.0f, (f32)(IME_FIELD_H - 14),
                             0xFFFFFFFF, 1);
    }

    /* letter/digit grid */
    for (row = 0; row < 3; row++) {
        int y = IME_KEYS_ROW0 + row * IME_ROW_PITCH;
        for (col = 0; col < 10; col++) {
            int key = ime_letter_key(row, col);
            int sel = (ime.sel_row == row && ime.sel_col == col &&
                       ime.sel_act < 0);
            int x = IME_KEYS_X + col * IME_KEY_W;
            if (key == IME_ACT_NONE) continue;   /* empty slot */
            GRRLIB_Rectangle((f32)(x + 1), (f32)(y + 1),
                            (f32)(IME_KEY_W - 2), (f32)(IME_KEY_H - 2),
                            sel ? COL_KEY_SEL : COL_KEY, 1);
            tmp[0] = (char)key;
            tmp[1] = 0;
            ime_label(font, x + IME_KEY_W / 2, y + 8, 20.0f,
                      0xFFF0F4F8, tmp);
        }
    }

    /* action row */
    acts = ime.layout ? k_actions_symbols : k_actions_letters;
    for (i = 0; i < 5; i++) {
        int sel = (ime.sel_row == 3 && ime.sel_act == i);
        if (acts[i].key == IME_ACT_NONE) continue;
        GRRLIB_Rectangle((f32)(acts[i].x + 1), (f32)(IME_ACT_Y + 1),
                        (f32)(acts[i].w - 2), (f32)(IME_ACT_H - 2),
                        sel ? (acts[i].key == IME_ACT_OK ? COL_OK_SEL
                                                         : COL_KEY_SEL)
                            : (acts[i].key == IME_ACT_OK ? COL_OK : COL_KEY),
                        1);
        ime_label(font, acts[i].x + acts[i].w / 2, IME_ACT_Y + 12, 18.0f,
                  0xFFF0F4F8, acts[i].label);
    }
    /* shift state hint */
    if (ime.layout == 0 && ime.shift)
        ime_label(font, IME_X + 34, IME_ACT_Y + IME_ACT_H + 4, 12.0f,
                  0xFF88A2C0, "shift");
}
