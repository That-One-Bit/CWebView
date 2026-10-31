#ifndef CWV_MENU_UI_H
#define CWV_MENU_UI_H

/* cwv_menu_ui.h - the platform-neutral CWMenu shell.
 *
 * The native menus (main / page operations / settings / devtools) used
 * to be Vita-only drawing code in sources/vita/main.c.  This module
 * implements the bundled menus in sources/shared so Wii U can render
 * the SAME geometry/style. Vita's legacy UI remains in its port:
 *
 *   shared (this file + cwv_menu_ui.c)
 *     - menu state machine, selection, scrolling, overrides
 *     - template-driven layout from the loaded .cwm script
 *     - every fill/text/circle draw routed through CwvMenuPaint
 *     - actions dispatched against the shared CwvHost API
 *
 *   console port (Wii U: SDL2 + SDL2_ttf; Vita uses legacy main.c)
 *     - implements the 4-function CwvMenuPaint vtable
 *     - supplies live data (fps, version, console ring, platform text)
 *     - persists preferences through CwvMenuPlatformOps
 *     - translates pad/touch events into cwv_menu_ui_*() input calls
 *
 * Coordinates: the menu draws on the SAME virtual canvas as the page
 * engine (Vita reference 960x544; the Wii U seam fits that onto the
 * 854x480 GamePad), so template rects need no per-port scaling.
 *
 * CWMenu 2 script (default.cwm) owns geometry and colors; this module
 * only interprets it.  Settings + page operations render in the exact
 * devtools grammar (user request 2026-09-21): text tab strip, content
 * panel at 20/106/700/270, 42px row rhythm, footer-separator row.
 */

#include <stddef.h>
#include "cwv_host.h"
#include "cwmenu_api.h"
#include "native_menu_templates.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- paint vtable (console-specific) -------------------------------- */

typedef struct CwvMenuPaint {
    /* Axis-aligned filled rectangle in ABGR (vita2d-style) RGBA8 layout.
     * The port applies its own premultiply/blend conventions. */
    void (*fill_rect)(float x, float y, float w, float h, unsigned abgr, void *ud);
    /* Filled circle (repeat badges, CWMenu circle shapes). */
    void (*fill_circle)(float cx, float cy, float r, unsigned abgr, void *ud);
    /* Baseline-anchored text; scale is the CWMenu 16px-per-unit em scale
     * used throughout the Vita shell (1.0 == the menu's base text). */
    void (*draw_text)(float x, float y, unsigned abgr, float scale,
                      const char *text, void *ud);
    /* Measured advance width of text at scale. */
    float (*text_width)(float scale, const char *text, void *ud);
    void *ud;
    /* Optional per-port favicon loader/drawer. URL may be local or remote;
     * the port may decline it. Cache image handles outside the shared UI. */
    void (*favicon)(float x, float y, float w, float h,
                    const char *url, void *ud);
    void (*draw_line)(float x1, float y1, float x2, float y2,
                      unsigned abgr, void *ud);
} CwvMenuPaint;

/* ---- live data (filled by the port every frame) --------------------- */

typedef struct CwvMenuData {
    /* Performance section */
    float fps;
    float frame_ms;
    unsigned frame_count;
    float scroll_offset;
    float zoom;
    /* Renderer section */
    int viewport_w, viewport_h;
    int node_count;
    int js_active;
    char js_last_error[96];
    /* Input section */
    char control_mode_name[32];
    char last_button[32];
    char dpad_focus[96];
    /* Page section */
    char page_title[128];
    char page_url[128];
    char version_display[64];
    /* Console section status chip, e.g. "JS on | ESM on(pass) | 3 lines".
     * The port composes this - the ESM state is platform-specific. */
    char console_chip[96];
    /* Platform section rows (ESM/module state is port-specific). */
    char platform_row_esm[256];
    char platform_row_status[160];
    /* Uptime text for the platform section. */
    char uptime[48];
} CwvMenuData;

/* ---- platform operations (console-specific) ------------------------- */

typedef struct CwvMenuPlatformOps {
    /* Persist + restore the settings block (JSON on both consoles). */
    int  (*save_prefs)(void *ud);
    int  (*load_prefs)(void *ud);
    /* "Open URL..." row: launch the IME / system keyboard; the port then
     * loads the result into the host itself. */
    void (*open_url_prompt)(void *ud);
    /* Quit the application (after the devtools Close confirm). */
    void (*exit_app)(void *ud);
    /* Delete the resource/font cache; returns a status message id (see
     * CWV_MENU_STATUS_*). */
    int  (*clear_cache)(void *ud);
    /* Optional runtime log (NULL = drop the line). */
    void (*log)(const char *fmt, void *ud);
    /* CWMenu 4 script linking: resolve <file> against the port's cwmenu
     * directory (e.g. app0:/cwmenu/<file>, sd:/wiiu/apps/<app>/cwmenu/<file>)
     * and vita_native_menu_templates_link() it.  Returns 0 on success.
     * NULL = this port cannot link scripts; `open_script:` rows then do
     * nothing (the row stays visible but inert, like other no-op rows). */
    int  (*load_script)(const char *file, void *ud);
    void *ud;
} CwvMenuPlatformOps;

/* ---- settings state (shared, persisted by the port) ------------------- */

typedef struct CwvMenuSettings {
    int scroll_mode;    /* 0=R stick/analog, 1=L, 2=dpad */
    int scroll_speed;   /* 0..2 slow/normal/fast */
    int zoom_mode;      /* 0=R, 1=L, 2=off */
    float zoom_level;   /* 0.75..2.0 */
    int dark_mode;
    int accent;         /* 0..5 */
    int menu_hotkey;    /* Wii U: 0=L+R+SELECT, 1=L+R+START, 2=both */
    int show_exit_app;  /* Wii U: 0 hides the DevTools Close tab */
    int css_cursors;    /* 1 = pages choose the pointer art via CSS
                           cursor (pack + url()), 0 = always default
                           (2026-09-26 (45)) */
    int dirty;          /* unsaved changes marker */
} CwvMenuSettings;

/* ---- overrides (DevTools > Overrides, cleared on exit) ---------------- */

typedef struct CwvMenuOverrides {
    int control_mode;   /* -1 default, else backend control-mode id:
                          * 1 browser, 2 standalone, 3 touch, 4 cursor */
    int scroll;        /* -1 default, 0..2 */
    int zoom;          /* -1 default, 0..2 */
    int theme;         /* -1 default, 0 light, 1 dark */
    int scrollbars;    /* -1 default, 0 hidden, 1 shown */
    float zoom_level;  /* <=0 = default */
    int button_wait;   /* -1 default, else preset id */
    char button_wait_spec[64];
} CwvMenuOverrides;

/* ---- menu state ------------------------------------------------------ */

typedef enum {
    CWV_MENU_CLOSED = 0,
    CWV_MENU_MAIN,
    CWV_MENU_PAGEOPS,
    CWV_MENU_SETTINGS,
    CWV_MENU_DEVTOOLS,
    CWV_MENU_CUSTOM
} CwvMenuMode;

/* Per-frame pad input events, already debounced by the port. */
typedef enum {
    CWV_MENU_INPUT_CONFIRM,   /* CROSS / A  - activate */
    CWV_MENU_INPUT_CANCEL,    /* CIRCLE / B - close one level */
    CWV_MENU_INPUT_UP,
    CWV_MENU_INPUT_DOWN,
    CWV_MENU_INPUT_LEFT,
    CWV_MENU_INPUT_RIGHT,
    CWV_MENU_INPUT_L1,        /* previous tab (settings/devtools) */
    CWV_MENU_INPUT_R1,        /* next tab */
    CWV_MENU_INPUT_START      /* reserved: platform owns chord detection */
} CwvMenuInput;

typedef struct CwvMenuUi {
    CwvMenuMode mode;
    /* selection indexes, mirroring the Vita shell semantics */
    char custom_name[64];
    int custom_selected;
    int main_selected;
    int pageops_selected;
    int settings_tab;       /* 0 appearance, 1 controls, 2 preferences */
    int settings_item;      /* 0..3, -1 = action row focus */
    int settings_action;    /* 0 undo, 1 save, 2 exit, -1 = content focus */
    int dev_section;       /* 0..7 performance..close */
    int dev_option;         /* overrides list selection */
    int exit_confirm_selected;
    int exit_confirm_section_return;  /* devtools tab to return to after
                                       * canceling the Close confirm */
    /* scrolling (content rows past the panel bottom) */
    float dev_scroll;
    int dev_scroll_drag;
    float settings_scroll;
    int settings_scroll_drag;
    /* touch */
    int touch_down;
    int touch_x, touch_y;
    /* Geometry scale for small screens (Wii 640x480 vs the 960x544 the
     * CWMenu templates are authored for): 1.0 = Vita/Wii U virtual canvas.
     * Applied inside the paint wrappers and inverted for touch input, so
     * ports pass REAL screen pixels in and template pixels out. */
    float scale;
    int   cursor_toggle;   /* shared shell never shows the Vita-only
                              "cycle_pointer" row; stays 0 for Wii U/Wii */
    int   fixed_hotkey;    /* port sets 1 when the menu key can not be
                              remapped (Wii HOME); the hotkey row stops
                              cycling and displays the fixed combo */
    int settings_status;
    /* live menu state the port reads back */
    CwvMenuSettings settings;
    CwvMenuOverrides overrides;
    /* devtools console cache (rebuilt when the ring changes) */
    int console_lines_last;
    int console_errors_last, console_warns_last;
    unsigned console_cache_age;
    char history[16][1024];
    int history_count, history_index;
    /* internal */
    const CwvMenuPaint *paint;
    const CwvMenuPlatformOps *ops;
    CwvHost *host;
    int screen_w, screen_h;
    /* CWMenu 4: last open_script: failure text ("" = none); drawn on the
     * main menu footer, cleared by the next successful script open.
     * Sized for "script not found: " + a full open_script: file token. */
    char script_error[192];
    /* console wrap cache */
    struct {
        const char *seg;   /* points into the console dump buffer */
        int len;
        unsigned color;
        int reps;          /* repeat count badge, 0 = none */
    } console_rows[512];
    int console_row_count;
    char console_dump[16640];
} CwvMenuUi;

/* status ids surfaced on the preferences tab */
enum { CWV_MENU_STATUS_NONE = 0, CWV_MENU_STATUS_CACHE_CLEARED,
       CWV_MENU_STATUS_PREFS_RESET };

/* ---- lifecycle ------------------------------------------------------- */

/* paint/ops/host may be NULL in tests (actions degrade to no-ops). */
void cwv_menu_ui_init(CwvMenuUi *ui, const CwvMenuPaint *paint,
                      const CwvMenuPlatformOps *ops, CwvHost *host,
                      int screen_w, int screen_h);
void cwv_menu_ui_open(CwvMenuUi *ui, CwvMenuMode mode);
void cwv_menu_ui_set_scale(CwvMenuUi *ui, float scale);
/* Open an app-defined menu already loaded by CWMenu, if present. */
int cwv_menu_ui_open_named(CwvMenuUi *ui, const char *name);
void cwv_menu_ui_close(CwvMenuUi *ui);
CwvMenuMode cwv_menu_ui_mode(const CwvMenuUi *ui);

/* ---- per-frame -------------------------------------------------------- */

/* Render the open menu (no-op when closed).  data carries the live
 * devtools values; NULL renders with placeholders. */
void cwv_menu_ui_draw(CwvMenuUi *ui, const CwvMenuData *data);

/* Pad input while a menu is open.  Returns 1 when consumed. */
int cwv_menu_ui_input(CwvMenuUi *ui, CwvMenuInput in);
/* Analog scroll delta while the menu is open, clamped to the content. */
void cwv_menu_ui_scroll(CwvMenuUi *ui, float delta);

/* Touch while a menu is open: down=1 press, 0 release, 2 drag (the port
 * keeps feeding move events with down=2 while the finger is down).
 * Returns 1 when consumed (the port must NOT forward to the page). */
int cwv_menu_ui_touch(CwvMenuUi *ui, int x, int y, int down);

/* Apply the effective (override-aware) theme/scrollbars/zoom/button-wait
 * to the host - the port calls this once per frame while browsing. */
void cwv_menu_ui_apply_runtime(CwvMenuUi *ui);
/* Record a successful host load/navigation, including Open URL from a port. */
void cwv_menu_ui_record_page(CwvMenuUi *ui);

/* Effective values (override-aware), used by the port's input loop. */
int cwv_menu_ui_effective_dark(const CwvMenuUi *ui);
int cwv_menu_ui_effective_scroll_mode(const CwvMenuUi *ui);
int cwv_menu_ui_effective_zoom_mode(const CwvMenuUi *ui);

/* ---- helpers shared by the ports -------------------------------------- */

/* The CWMenu accent/name tables used by Settings > Appearance. */
int cwv_menu_ui_accent_count(void);
const char *cwv_menu_ui_accent_name(int accent);

/* Scroll-speed label ("Slow"/"Normal"/"Fast"). */
const char *cwv_menu_ui_scroll_speed_name(int speed);

#ifdef __cplusplus
}
#endif
#endif
