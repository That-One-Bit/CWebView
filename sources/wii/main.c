/* main.c - Wii app loop for CWebView (libogc + GRRLIB).
 *
 * STATUS: engine-first port, modeled on the Wii U app loop.  Drives the
 * SHARED host on the Wii's 640x480 screen - the page is laid out NATIVE
 * (pages adapt to 640px like any 4:3 browser window) and the shared
 * CWMenu shell (authored for the 960x544 Vita canvas) is scaled by
 * cwv_menu_ui_set_scale().  The software keyboard (cwv_ime_wii.c - no
 * IME in libogc, so it is code-drawn and driven by this loop) and HTTP
 * fetch (cwv_fetch_net.c, libogc net; https refused - no TLS in
 * libogc) round out the Vita/Wii U feature set.  See README-WII.md.
 *
 * Input (wiimote + GameCube pad both wired):
 *   IR pointer  -> cursor (always on: the Wii's primary input IS the
 *                  pointer); art from <package folder>/assets/cursor-wii.png
 *   A           -> tap at the cursor (page or open menu); focus
 *                  activation when not pointing
 *   B           -> cancel/back (closes one menu level)
 *   HOME        -> open/close the CWMenu main menu (fixed, like the
 *                  Wii Menu's HOME behavior; the hotkey row shows
 *                  "HOME (fixed)" and cannot be remapped)
 *   MINUS/PLUS  -> settings/devtools tab switch
 *   D-pad       -> menu navigation / page focus cycle
 *   GC pad      -> same pad navigation + A/B as the wiimote
 */
#include "core/cwv_host.h"
#include "core/cwv_platform.h"
#include "menu/cwv_menu_ui.h"
#include "core/cwv_cursor.h"
#include "core/cwv_project.h"
#include "core/cwv_renderer.h"   /* CwvCursorInfo, CWV_CSTYLE_* (45) */
#include "cwv_ime_wii.h"  /* software keyboard (modal, this loop feeds it) */
#include <grrlib.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>   /* libogc: Wiimote/WPAD lives in wiiuse/, not ogc/ (report 2026-09-26: ogc/wpad.h does not exist) */
#include <fat.h>
#include <dirent.h>
#include <sys/stat.h>           /* fatInitDefault: sd: is only visible to stdio after it */
#include <wiikeyboard/keyboard.h> /* USB keyboard (libwiikeyboard): KEYBOARD_Init(NULL)
                                      polls via KEYBOARD_GetEvent; a NULL callback means
                                      the library does NOT hijack stdin (upstream skips
                                      its key-buffer thread + devoptab) - verified against
                                      devkitPro/libogc keyboard.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef CWV_WII_WEBPAGE
#define CWV_WII_WEBPAGE "web/index.html"
#endif

#define WII_SCREEN_W 640
#define WII_SCREEN_H 480

/* The CWMenu templates are authored against the Vita 960x544 virtual
 * canvas; scale the whole menu down uniformly to the Wii's width. */
#define WII_MENU_SCALE (640.0f / 960.0f)

/* Settings live in the folder the package sits in (set at boot). */
static char s_prefs_path[300] = "settings.cfg";

static CwvHost *s_host;
extern void cwv_wii_app_root_init(const char *argv0);
extern const char *cwv_wii_app_root(void);
static CwvMenuUi s_menu;
static CwvCursor s_cursor;              /* Wiimote IR cursor */
static CwvMenuPaint s_menu_paint;
static CwvMenuPlatformOps s_menu_ops;
static void *s_menu_font;
static int s_running = 1;
static char s_entry_url[1024];
static int s_boot_failed;

/* ------------------------------------------------------------------ */
/* CWMenu paint vtable (GRRLIB via the platform seam)                  */
/* ------------------------------------------------------------------ */
/* CWMenu 4 script linking: resolve a bare file name against the app's
 * cwmenu directory and link it (names only, no path separators). */
static int wii_files_list(const char *relative, CwvMenuFileEntry *out,
                          int capacity, void *ud)
{
    char path[1024];
    DIR *dir;
    struct dirent *ent;
    int n = 0;
    (void)ud;
    if (!out || capacity <= 0) return 0;
    if (relative && (strstr(relative, "..") || strchr(relative, '\\'))) return -1;
    if (snprintf(path, sizeof(path), "%s/web/%s%s", cwv_wii_app_root(),
                 relative && *relative ? relative : "",
                 relative && *relative ? "/" : "") >= (int)sizeof(path)) return -1;
    dir = opendir(path);
    if (!dir) return 0;
    while ((ent = readdir(dir)) != NULL && n < capacity) {
        char full[1100]; struct stat st;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(full, sizeof(full), "%s%s", path, ent->d_name) >= (int)sizeof(full)) continue;
        if (stat(full, &st) != 0) continue;
        snprintf(out[n].name, sizeof(out[n].name), "%s", ent->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode);
        ++n;
    }
    closedir(dir);
    return n;
}
static int wii_load_script(const char *file, void *ud)
{
    char path[512];
    (void)ud;
    if (!file || !*file) return -1;
    if (strchr(file, '/') || strchr(file, '\\')) return -1;
    snprintf(path, sizeof(path), "%s/cwmenu/%s", cwv_wii_app_root(), file);
    return vita_native_menu_templates_link(path);
}

static void menu_fill(float x, float y, float w, float h, unsigned c, void *ud)
{
    (void)ud; cwv_draw_rectangle(x, y, w, h, c);
}
static void menu_circle(float x, float y, float r, unsigned c, void *ud)
{
    (void)ud; cwv_draw_fill_circle(x, y, r, c);
}
static void menu_text(float x, float y, unsigned c, float z, const char *t, void *ud)
{
    (void)ud;
    if (s_menu_font) cwv_pgf_draw_text(s_menu_font, x, y, c, z, t);
}
static float menu_text_width(float z, const char *t, void *ud)
{
    (void)ud;
    return s_menu_font
        ? (float)cwv_pgf_text_width(s_menu_font, z, t)
        : (float)(t ? (int)strlen(t) : 0) * 8.0f * z;
}
static void menu_favicon(float x, float y, float w, float h,
                         const char *url, void *ud)
{
    /* like the Wii U: local app-root file favicons only */
    static void *tex;
    static char source[512];
    char path[512];
    char prefix[300];
    const char *ext, *end;
    size_t len;
    snprintf(prefix, sizeof(prefix), "%s/", cwv_platform_url_root());
    (void)ud;
    if (!url || strncmp(url, prefix, strlen(prefix)) != 0) {
        if (tex) { cwv_tex_free(tex); tex = NULL; }
        source[0] = 0;
        return;
    }
    if (strcmp(url, source) != 0) {
        if (tex) cwv_tex_free(tex);
        tex = NULL;
        snprintf(source, sizeof(source), "%s", url);
        end = strpbrk(url + strlen(prefix), "?#");
        len = end ? (size_t)(end - (url + strlen(prefix)))
                  : strlen(url + strlen(prefix));
        if (len + strlen(cwv_platform_path_root()) + 1 < sizeof(path)) {
            size_t pl = strlen(cwv_platform_path_root());
            memcpy(path, cwv_platform_path_root(), pl);
            memcpy(path + pl, url + strlen(prefix) - 1, len);   /* keep '/' */
            path[len + pl] = 0;
            ext = strrchr(path, '.');
            if (ext && !strcasecmp(ext, ".png"))
                tex = cwv_tex_load_png_file(path);
            else if (ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")))
                tex = cwv_tex_load_jpeg_file(path);
        }
    }
    if (tex && cwv_tex_width(tex) > 0 && cwv_tex_height(tex) > 0)
        cwv_draw_texture_scale(tex, x, y,
            w / (float)cwv_tex_width(tex),
            h / (float)cwv_tex_height(tex));
}
static void menu_line(float x1, float y1, float x2, float y2,
                      unsigned c, void *ud)
{
    (void)ud;
    /* GRRLIB has no line primitive; cover it with a 1px rect like the
     * Vita (the menu only draws horizontal separators). */
    float x = x1 < x2 ? x1 : x2, y = y1 < y2 ? y1 : y2;
    float w = (x2 > x1 ? x2 - x1 : x1 - x2) + 1.0f;
    float h = (y2 > y1 ? y2 - y1 : y1 - y2) + 1.0f;
    cwv_draw_rectangle(x, y, w < 1.0f ? 1.0f : w, h < 1.0f ? 1.0f : h, c);
}

/* ------------------------------------------------------------------ */
/* platform ops                                                        */
/* ------------------------------------------------------------------ */
static void wii_log(const char *fmt, void *ud)
{
    char path[384];
    FILE *f;
    const char *root = cwv_wii_app_root();
    (void)ud;
    if (!fmt) return;

    /* Produce exactly one app log. Keep the established CWV.log name used
     * by the other platform ports and diagnostics documentation. */
    snprintf(path, sizeof(path), "%s/CWV.log", root);
    f = fopen(path, "ab");
    if (f) {
        fprintf(f, "%s\n", fmt);
        fclose(f);
    }
}
/* Runtime log hook: same file as the platform log, but varargs like the
 * other ports (cwv_backend_set_runtime_log).  The Wii previously never
 * registered it, so IMG/console diagnostics silently vanished here. */
static void wii_runtime_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    wii_log(buf, NULL);
}

static int wii_save_prefs(void *ud)
{
    CwvMenuUi *ui = (CwvMenuUi *)ud;
    char out[256];
    int n;
    n = snprintf(out, sizeof(out),
        "scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\ndark=%d\n"
        "accent=%d\nshow_exit_app=%d\ncss_cursors=%d\n",
        ui->settings.scroll_mode, ui->settings.scroll_speed,
        ui->settings.zoom_mode,
        (int)(ui->settings.zoom_level * 100.0f + 0.5f),
        ui->settings.dark_mode, ui->settings.accent,
        ui->settings.show_exit_app, ui->settings.css_cursors);
    return n > 0 && n < (int)sizeof(out)
        ? cwv_file_write_all(s_prefs_path, out, (size_t)n) : -1;
}
static int wii_load_prefs(void *ud)
{
    CwvMenuUi *ui = (CwvMenuUi *)ud;
    char b[256];
    long len;
    int fd, n;
    len = cwv_file_size(s_prefs_path);
    if (len < 0) return -1;
    fd = cwv_file_open_read(s_prefs_path);
    if (fd < 0) return -1;
    n = cwv_file_read(fd, b, sizeof(b) - 1);
    cwv_file_close(fd);
    if (n <= 0) return -1;
    b[n] = 0;
    {
        char *line = b;
        while (line && *line) {
            char *next = strchr(line, '\n');
            char key[32] = {0};
            int value;
            if (next) *next = 0;
            if (sscanf(line, " %31[^= ] = %d", key, &value) == 2) {
                if (!strcmp(key, "scroll")) ui->settings.scroll_mode = value;
                else if (!strcmp(key, "scroll_speed")) ui->settings.scroll_speed = value;
                else if (!strcmp(key, "zoom")) ui->settings.zoom_mode = value;
                else if (!strcmp(key, "zoom_level")) ui->settings.zoom_level = (float)value / 100.0f;
                else if (!strcmp(key, "dark")) ui->settings.dark_mode = value;
                else if (!strcmp(key, "accent")) ui->settings.accent = value;
                else if (!strcmp(key, "show_exit_app")) ui->settings.show_exit_app = value;
                else if (!strcmp(key, "css_cursors")) ui->settings.css_cursors = value;
            }
            if (!next) break;
            line = next + 1;
        }
    }
    return 0;
}
/* Open URL (main-menu row): opens the SOFTWARE keyboard (cwv_ime_wii.c),
 * prefilled with the current page relative to web/ (the same convention
 * the Vita uses: "./nested/x.html", swap a name for a sibling, "../"
 * climbs, "web/..." is the web-root form; remote URLs are refused -
 * scripts use fetch()).  The keyboard is modal: the app loop routes all
 * input to it; when it closes with OK, wii_open_url_go navigates. */
static cwv_utf16 s_url_buf[512];
static int s_url_ime_open = 0;

static void wii_open_url_prompt(void *ud)
{
    const char *title = "Open URL";
    cwv_utf16 t16[32], i16[256];
    const char *cur;
    char prefill[256];
    size_t i;
    (void)ud;
    if (s_url_ime_open) return;

    prefill[0] = 0;
    cur = cwv_host_current_url(s_host);
    if (cur) {
        const char *root = cwv_platform_url_root();
        size_t rlen = strlen(root);
        /* "<root>/web/nested/x.html" -> "./nested/x.html" */
        if (strncmp(cur, root, rlen) == 0 && cur[rlen] == '/')
            snprintf(prefill, sizeof(prefill), "./%s", cur + rlen + 1);
        else
            snprintf(prefill, sizeof(prefill), "%.250s", cur);
    }
    for (i = 0; i + 1 < sizeof(t16) / sizeof(t16[0]) && title[i]; i++)
        t16[i] = (cwv_utf16)(unsigned char)title[i];
    t16[i] = 0;
    for (i = 0; i + 1 < sizeof(i16) / sizeof(i16[0]) && prefill[i]; i++)
        i16[i] = (cwv_utf16)(unsigned char)prefill[i];
    i16[i] = 0;

    if (cwv_ime_open(t16, i16, 0, 0, s_url_buf,
                     (int)(sizeof(s_url_buf) / sizeof(s_url_buf[0]))) == 0)
        s_url_ime_open = 1;
    else
        wii_log("Open URL: keyboard open failed", NULL);
}

/* Navigation after the keyboard closes with OK - the same resolution
 * rules as the Vita (see its open_url_navigate): relative refs are
 * ALWAYS web-root-relative, "../" climbs (clamped at web/), remote
 * URLs are refused. */
static void wii_open_url_go(const cwv_utf16 *text)
{
    char input[512];
    const char *root = cwv_platform_url_root();
    char web_root[600];
    /* 2048: "../"-climbing join of base[600] + keyboard input (up to
     * ~512 chars each way) must never truncate (-Wformat-truncation). */
    char url[2048];
    size_t i;
    int n = 0;

    for (i = 0; text[i] && n < (int)sizeof(input) - 1; i++)
        input[n++] = (char)(text[i] & 0xFF);      /* ASCII form data */
    input[n] = 0;
    if (!input[0]) return;

    if (strncmp(input, "http://", 7) == 0 ||
        strncmp(input, "https://", 8) == 0) {
        {
            char msg[320];
            snprintf(msg, sizeof(msg),
                     "Open URL: remote URL rejected (%.200s)", input);
            wii_log(msg, NULL);
        }
        cwv_host_eval(s_host,
            "console.error('Open URL: remote browsing is not supported. "
            "Use a local page like ./editor.html; scripts can call fetch().')");
        return;
    }
    snprintf(web_root, sizeof(web_root), "%s/web", root);

    if (strncmp(input, "web/", 4) == 0)
        snprintf(url, sizeof(url), "%s/%s", web_root, input + 4);
    else if (strncmp(input, "./", 2) == 0)
        snprintf(url, sizeof(url), "%s/%s", web_root, input + 2);
    else if (input[0] == '/')
        snprintf(url, sizeof(url), "%s%s", root, input);
    else {
        /* "../" chains climb one directory each, clamped at web/ (the
         * same rule the Vita prefill/editor uses; the package root and
         * system dirs are only reachable through the "/" form) */
        const char *p = input;
        char base[600];
        snprintf(base, sizeof(base), "%s", web_root);
        while (strncmp(p, "../", 3) == 0) {
            size_t blen = strlen(base);
            char *last = strrchr(base, '/');
            if (last && (size_t)(last - base) > strlen(web_root))
                *last = 0;              /* pop one dir, never above web/ */
            p += 3;
            (void)blen;
        }
        snprintf(url, sizeof(url), "%s/%s", base, p);
    }
    {
        char msg[320];
        snprintf(msg, sizeof(msg), "Open URL: navigating to %.200s", url);
        wii_log(msg, NULL);
    }
    if (cwv_host_load(s_host, url) != 0)
    {
        char msg[320];
        snprintf(msg, sizeof(msg), "Open URL: load failed: %.200s",
                 cwv_host_last_error(s_host));
        wii_log(msg, NULL);
    }
}
static void wii_exit_app(void *ud)
{
    (void)ud;
    s_running = 0;
}
static int wii_clear_cache(void *ud)
{
    char dir[512];
    (void)ud;
    if (cwv_console_cache_dir(dir, sizeof(dir)) < 0)
        return CWV_MENU_STATUS_NONE;
    return CWV_MENU_STATUS_CACHE_CLEARED;
}

/* ------------------------------------------------------------------ */
/* live data for the devtools pages (like the Wii U menu_fill_live)    */
/* ------------------------------------------------------------------ */
static void wii_fill_live(CwvMenuData *d, int frame)
{
    const char *s;
    memset(d, 0, sizeof(*d));
    d->frame_count = (unsigned)frame;
    /* GRRLIB blocks on vblank; the console frame is one refresh long. */
    d->frame_ms = 1000.0f / 60.0f;
    d->fps = 60.0f;
    d->scroll_offset = cwv_host_scroll_offset(s_host);
    d->zoom = cwv_host_zoom(s_host);
    d->viewport_w = WII_SCREEN_W;
    d->viewport_h = WII_SCREEN_H;
    d->node_count = cwv_host_node_count(s_host);
    d->js_active = cwv_host_js_active(s_host);
    s = cwv_host_js_last_error(s_host);
    if (s) snprintf(d->js_last_error, sizeof(d->js_last_error), "%s", s);
    s = cwv_host_page_title(s_host);
    if (s) snprintf(d->page_title, sizeof(d->page_title), "%s", s);
    s = cwv_host_current_url(s_host);
    if (s) snprintf(d->page_url, sizeof(d->page_url), "%s", s);
    snprintf(d->version_display, sizeof(d->version_display), "Wii");
    /* backend ids: 1 browser, 2 standalone, 3 touch, 4 cursor (0 none) */
    snprintf(d->control_mode_name, sizeof(d->control_mode_name), "%s",
             cwv_host_control_mode(s_host) == 1 ? "Browser" :
             cwv_host_control_mode(s_host) == 2 ? "Standalone" :
             cwv_host_control_mode(s_host) == 3 ? "Touch" :
             cwv_host_control_mode(s_host) == 4 ? "Cursor" : "None");
    cwv_host_focus_info(s_host, d->dpad_focus, sizeof(d->dpad_focus));
    snprintf(d->platform_row_esm, sizeof(d->platform_row_esm),
             "ESM module: built-in ES2015 transformer");
    snprintf(d->platform_row_status, sizeof(d->platform_row_status),
             "Wii platform integration is active (640x480 native).");
}

/* ------------------------------------------------------------------ */
/* boot diagnostics (page missing / unreadable)                        */
/* ------------------------------------------------------------------ */
static void wii_draw_boot_screen(void *font)
{
    int y = 60;
    const char *lines[5];
    char url[256];
    int i;
    snprintf(url, sizeof(url), "%.255s", s_entry_url);
    lines[0] = "CWebView (Wii) - the page did not load";
    lines[1] = "Check the web/ folder next to boot.dol.";
    lines[2] = "Data root: sd:/apps/cwebview/";
    lines[3] = url;
    lines[4] = "Retrying every 2s... HOME opens the menu.";
    cwv_draw_rectangle(0, 0, 640, 480, RGBA8(16, 17, 22, 255));
    for (i = 0; i < 5; i++) {
        if (font)
            cwv_pgf_draw_text(font, 24, (float)y,
                i == 0 ? RGBA8(235, 190, 120, 255) : RGBA8(190, 210, 235, 255),
                0.66f, lines[i]);
        y += 34;
    }
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* USB keyboard frame pump (2026-10-05).  Deliberately mirrors the     */
/* Windows port's WM_KEYDOWN mapping so a keyboard behaves identically */
/* on both platforms: F1 menu, arrows scroll (or move the menu),      */
/* Enter activates, Esc cancels/clears focus, PgUp/PgDn page-scroll    */
/* (or menu L1/R1), Home tops the page, Tab cycles focus, F5 reloads. */
/* ------------------------------------------------------------------ */
static void wii_keyboard_frame(void)
{
    keyboard_event ev;
    int menu_open = cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED;

    if (cwv_ime_wii_active())
        return; /* the software keyboard is modal: keys are page keys */

    while (KEYBOARD_GetEvent(&ev) > 0) {
        if (ev.type != KEYBOARD_PRESSED)
            continue;
        switch (ev.symbol) {
        case KS_F1:
            if (menu_open) cwv_menu_ui_close(&s_menu);
            else cwv_menu_ui_open(&s_menu, CWV_MENU_MAIN);
            menu_open = !menu_open;
            break;
        case KS_Return:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CONFIRM);
            else {
                cwv_host_focus_activate(s_host);
                cwv_host_focus_release(s_host); /* keyboard API has no key-up */
            }
            break;
        case KS_Escape:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CANCEL);
            else cwv_host_focus_clear(s_host);
            break;
        case KS_Up:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_UP);
            else cwv_host_scroll_by(s_host, -60.0f);
            break;
        case KS_Down:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_DOWN);
            else cwv_host_scroll_by(s_host, 60.0f);
            break;
        case KS_Left:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_LEFT);
            /* 2026-10-06 (74): horizontal scrolling existed only inside
             * menus - browsing pages had NO left/right scroll at all,
             * so both axes could never be adjusted together.  Match the
             * Win/WiiU key grammar (60px steps, same as Up/Down). */
            else cwv_host_scroll_x_by(s_host, -60.0f);
            break;
        case KS_Right:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_RIGHT);
            else cwv_host_scroll_x_by(s_host, 60.0f);
            break;
        case KS_Prior:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_L1);
            else cwv_host_scroll_by(s_host, -480.0f);
            break;
        case KS_Next:
            if (menu_open) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_R1);
            else cwv_host_scroll_by(s_host, 480.0f);
            break;
        case KS_Home:
            if (!menu_open) cwv_host_scroll_to(s_host, 0.0f);
            break;
        case KS_Tab:
            if (!menu_open) cwv_host_focus_cycle(s_host, 1);
            break;
        case KS_F5: {
            const char *cur;
            if (!menu_open && (cur = cwv_host_current_url(s_host)) != NULL &&
                cwv_host_load(s_host, cur) == 0)
                cwv_menu_ui_record_page(&s_menu);
            break;
        }
        default:
            break;
        }
    }
}

int main(int argc, char **argv)
{
    char entry_page[512];
    const char *entry_src = "default";
    int frame = 0;
#if CWV_CURSOR_ENABLED
    int cursor_a = 0;          /* cursor A tap in flight */
#endif
    static char s_url_root[320];

    /* THE APP ROOT IS THE FOLDER THE PACKAGE SITS IN (user direction,
     * 2026-09-23 (33)): the Homebrew Channel passes boot.dol's path in
     * argv[0], so .../exampledir/boot.dol loads .../exampledir/web/.
     * app0: stays Vita-only; the engine's URL space points at this
     * real folder so the URL bar stays honest. */
    cwv_wii_app_root_init(argc > 0 ? argv[0] : NULL);
    {
        const char *root = cwv_wii_app_root();
        snprintf(s_url_root, sizeof(s_url_root), "file:///%s", root);
        cwv_platform_set_app_root(s_url_root, root);
        snprintf(s_prefs_path, sizeof(s_prefs_path), "%s/settings.cfg", root);
    }

    /* sd: must be MOUNTED before the first fopen - settings, web/
     * pages, fonts, cursor art and the runtime log all live there. */
    if (!fatInitDefault())
        fprintf(stderr, "warning: SD mount failed; content may not load\n");
    PAD_Init();
    WPAD_Init();
    /* USB keyboard (2026-10-05): inert when no keyboard is plugged -
     * KEYBOARD_Init starts a small poll thread and queues events; we
     * drain them in the frame loop. NULL callback = no stdin takeover. */
    KEYBOARD_Init(NULL);
    /* IR pointer setup: buttons + IR, mapped to the real screen so
     * wpad->ir.x/y are screen pixels (WPAD_SetVRes does the mapping). */
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, WII_SCREEN_W, WII_SCREEN_H);
    if (GRRLIB_Init() != 0) {
        fprintf(stderr, "GRRLIB_Init failed\n");
        return 1;
    }

    s_host = cwv_host_create(WII_SCREEN_W, WII_SCREEN_H);
    if (!s_host) { fprintf(stderr, "host create failed\n"); return 1; }
    cwv_host_set_runtime_log(s_host, wii_runtime_log);

#if CWV_CURSOR_ENABLED
    /* Cursor art: <package folder>/assets/cursor-wii.png (build-wii.sh
     * stages it next to the boot.dol; package.json wii.cursor overrides
     * the source art).  CWV_CURSOR_ENABLED=0 compiles the cursor out. */
    {
        char art[400];
        snprintf(art, sizeof(art), "%s/assets/cursor-wii.png",
                 cwv_wii_app_root());
        cwv_cursor_init(&s_cursor, art);
        /* CSS cursor pack (2026-09-26 (45)): <pkg>/assets/cursors/.
         * Missing art falls back to the console art, then the arrow. */
        snprintf(art, sizeof(art), "%s/assets/cursors", cwv_wii_app_root());
        cwv_cursor_set_style_dir(&s_cursor, art);
    }
#endif
    s_menu_font = cwv_pgf_load_default();
    if (!s_menu_font)
        wii_log("ERROR: menu font failed to load from assets/font.ttf", NULL);
    else
        wii_log("menu font loaded: assets/font.ttf", NULL);

    s_menu_paint.fill_rect = menu_fill;
    s_menu_paint.fill_circle = menu_circle;
    s_menu_paint.draw_text = menu_text;
    s_menu_paint.text_width = menu_text_width;
    s_menu_paint.favicon = menu_favicon;
    s_menu_paint.draw_line = menu_line;
    s_menu_ops.save_prefs = wii_save_prefs;
    s_menu_ops.load_prefs = wii_load_prefs;
    s_menu_ops.open_url_prompt = wii_open_url_prompt;
    s_menu_ops.exit_app = wii_exit_app;
    s_menu_ops.clear_cache = wii_clear_cache;
    s_menu_ops.log = wii_log;
    s_menu_ops.load_script = wii_load_script;
    s_menu_ops.files_list = wii_files_list;
    s_menu_ops.ud = &s_menu;
    cwv_menu_ui_init(&s_menu, &s_menu_paint, &s_menu_ops, s_host,
                     WII_SCREEN_W, WII_SCREEN_H);
    cwv_menu_ui_set_scale(&s_menu, WII_MENU_SCALE);
    s_menu.fixed_hotkey = 1;    /* HOME is the menu key, like the Wii Menu */
    s_menu.settings.show_exit_app = 1;   /* the Wii needs an in-app exit */

    {
        char cwm_path[512];
        int rc;
        snprintf(cwm_path, sizeof(cwm_path), "%s/cwmenu/default.cwm",
                 cwv_wii_app_root());
        rc = vita_native_menu_templates_load(cwm_path);
        if (rc != 0)
            wii_log("ERROR: CWMenu script unavailable: cwmenu/default.cwm", NULL);
        else
            wii_log("CWMenu script loaded: cwmenu/default.cwm", NULL);
    }
    (void)wii_load_prefs(&s_menu);

    /* Saved zoom BEFORE the first page paints, same clamp as the Vita
     * (0.75x - 2.0x).  The config zoom_level was previously loaded and
     * displayed in the menu but never applied to the backend. */
    if (s_menu.settings.zoom_level >= 0.75f && s_menu.settings.zoom_level <= 2.0f)
        (void)cwv_host_set_zoom(s_host, s_menu.settings.zoom_level);

    /* Saved theme BEFORE the first page paints (the Wii U flash lesson). */
    (void)cwv_host_set_theme(s_host, cwv_menu_ui_effective_dark(&s_menu));

    /* ES2015+ support: the built-in transformer (no .rel module yet). */
    cwv_host_enable_es_builtin(s_host);
    cwv_host_enable_ts_builtin(s_host);

    if (argc > 1 && argv[1][0]) {
        snprintf(entry_page, sizeof(entry_page), "%s", argv[1]);
        entry_src = "argv";
    } else {
        snprintf(entry_page, sizeof(entry_page), "%s", CWV_WII_WEBPAGE);
    }
    /* The URL names the package's own folder (app0: is Vita-only). */
    snprintf(s_entry_url, sizeof(s_entry_url), "%s/%s",
             cwv_platform_url_root(), entry_page);
    wii_log("boot: CWebView Wii 640x480, page (from s_entry_url) below", NULL);
    wii_log(s_entry_url, NULL);
    (void)entry_src;

    {
        int load_rc = cwv_host_load(s_host, s_entry_url);
        if (load_rc != 0) {
            char msg[512];
            s_boot_failed = 1;
            snprintf(msg, sizeof(msg), "ERROR: initial page load failed (rc=%d): %s",
                     load_rc, cwv_host_last_error(s_host));
            wii_log(msg, NULL);
            fprintf(stderr, "initial load failed: %s\n",
                    cwv_host_last_error(s_host));
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "initial page loaded: %.480s", s_entry_url);
            wii_log(msg, NULL);
        }
    }

    while (s_running) {
        u32 wdown, pdown;
        WPADData *wpad;
#if CWV_CURSOR_ENABLED
        int pointing = 0, px = 0, py = 0;
#endif
        int menu_open;
        CwvMenuData live;
        frame++;

        WPAD_ScanPads();
        PAD_ScanPads();
        wii_keyboard_frame();   /* USB keyboard events (no-op if none) */
        wdown = WPAD_ButtonsDown(WPAD_CHAN_0);
        pdown = PAD_ButtonsDown(0);

        /* Content may appear late on the SD card - retry every 2s. */
        if (s_boot_failed && (frame % 120) == 0) {
            if (cwv_host_load(s_host, s_entry_url) == 0) {
                s_boot_failed = 0;
                cwv_menu_ui_record_page(&s_menu);
            }
        }

#if CWV_CURSOR_ENABLED
        /* IR pointer: the cursor is ALWAYS on when tracked (the Wii's
         * pointer is the primary input, matching the Wii Menu). */
        wpad = WPAD_Data(WPAD_CHAN_0);
        if (wpad && wpad->ir.valid) {
            pointing = 1;
            px = wpad->ir.x;
            py = wpad->ir.y;
            cwv_cursor_place(&s_cursor, (float)px, (float)py,
                             WII_SCREEN_W, WII_SCREEN_H);
        }
        cwv_cursor_show(&s_cursor, pointing);
#endif

        menu_open = cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED;
        {
            int a_pressed = (wdown & WPAD_BUTTON_A) || (pdown & PAD_BUTTON_A);
            int b_pressed = (wdown & WPAD_BUTTON_B) || (pdown & PAD_BUTTON_B);
            int up    = (wdown & WPAD_BUTTON_UP)    || (pdown & PAD_BUTTON_UP);
            int down  = (wdown & WPAD_BUTTON_DOWN)  || (pdown & PAD_BUTTON_DOWN);
            int left  = (wdown & WPAD_BUTTON_LEFT)  || (pdown & PAD_BUTTON_LEFT);
            int right = (wdown & WPAD_BUTTON_RIGHT) || (pdown & PAD_BUTTON_RIGHT);

            /* The software keyboard is MODAL (cwv_ime_wii.c): while it
             * is on screen every event goes to it and the page, the
             * menus and HOME stay untouched. */
            if (cwv_ime_wii_active()) {
                if (a_pressed) {
                    if (pointing)
                        cwv_ime_wii_pointer(px, py);   /* tap a key */
                    else
                        cwv_ime_wii_input(CWV_IME_W_CONFIRM);
                }
                if (b_pressed) cwv_ime_wii_input(CWV_IME_W_CANCEL);
                if (up)    cwv_ime_wii_input(CWV_IME_W_UP);
                if (down)  cwv_ime_wii_input(CWV_IME_W_DOWN);
                if (left)  cwv_ime_wii_input(CWV_IME_W_LEFT);
                if (right) cwv_ime_wii_input(CWV_IME_W_RIGHT);
            } else {
            /* HOME toggles the menu, fixed (Wii Menu behavior). */
            if (wdown & WPAD_BUTTON_HOME) {
                if (menu_open) cwv_menu_ui_close(&s_menu);
                else cwv_menu_ui_open(&s_menu, CWV_MENU_MAIN);
                menu_open = !menu_open;
            }

            if (a_pressed) {
#if CWV_CURSOR_ENABLED
                if (pointing) {
                    /* tap at the cursor, like a touch press */
                    cursor_a = 1;
                    if (menu_open) cwv_menu_ui_touch(&s_menu, px, py, 1);
                    else cwv_host_pointer_event(s_host, px, py, 1);
                } else
#endif
                if (menu_open) {
                    cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CONFIRM);
                } else {
                    cwv_host_focus_activate(s_host);
                }
            }
            if (b_pressed && menu_open)
                cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CANCEL);
            if ((wdown & WPAD_BUTTON_MINUS) && menu_open)
                cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_L1);
            if ((wdown & WPAD_BUTTON_PLUS) && menu_open)
                cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_R1);
            else if (!menu_open && ((wdown & WPAD_BUTTON_PLUS) || (pdown & PAD_BUTTON_START)))
                (void)cwv_host_eval(s_host, "window.CwvInput && window.CwvInput.buttonDown('START')");

            if (menu_open) {
                if (up) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_UP);
                if (down) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_DOWN);
                if (left) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_LEFT);
                if (right) cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_RIGHT);
            } else {
                if (up) cwv_host_focus_cycle(s_host, -1);
                if (down) cwv_host_focus_cycle(s_host, 1);
            }
            } /* !cwv_ime_wii_active() */

            /* A release ends both pointer taps and focused-control presses. */
            {
                u32 wup = WPAD_ButtonsUp(WPAD_CHAN_0);
                u32 pup = PAD_ButtonsUp(0);
                if (!menu_open && ((wup & WPAD_BUTTON_PLUS) || (pup & PAD_BUTTON_START)))
                    (void)cwv_host_eval(s_host, "window.CwvInput && window.CwvInput.buttonUp('START')");
                if ((wup & WPAD_BUTTON_A) || (pup & PAD_BUTTON_A)) {
#if CWV_CURSOR_ENABLED
                    if (cursor_a) {
                        int cx, cy;
                        cursor_a = 0;
                        if (!cwv_ime_wii_active() &&
                            cwv_cursor_position(&s_cursor, &cx, &cy)) {
                            if (menu_open) cwv_menu_ui_touch(&s_menu, cx, cy, 0);
                            else cwv_host_pointer_event(s_host, cx, cy, 0);
                        }
                    }
#endif
                    cwv_host_focus_release(s_host);
                }
            }
        }

        /* engine frame */
        cwv_host_update(s_host);
        cwv_menu_ui_apply_runtime(&s_menu);

        /* Open URL: the keyboard reports close/apply through the seam
         * (0 = still open, 1 = OK, 2 = cancelled). */
        if (s_url_ime_open && !cwv_ime_wii_active()) {
            int rc = cwv_ime_poll(s_url_buf,
                                 (int)(sizeof(s_url_buf) /
                                       sizeof(s_url_buf[0])));
            s_url_ime_open = 0;
            if (rc == 1)
                wii_open_url_go(s_url_buf);
        }

        /* draw: page, menu, cursor LAST */
        if (s_boot_failed)
            wii_draw_boot_screen(s_menu_font);
        else
            cwv_host_draw(s_host);
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            wii_fill_live(&live, frame);
            cwv_menu_ui_draw(&s_menu, &live);
        }
#if CWV_CURSOR_ENABLED
        /* The cursor vanishes while ANY CWMenu is open (user direction,
         * 2026-09-26 (43)) - it used to render on top of the dimmed
         * page outside the menu window as well.  Placement/tap routing
         * keeps running (A taps the menu at the cursor position);
         * only the draw is skipped. */
        if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED) {
            /* CSS cursor (2026-09-26 (45)): apply the engine's
             * under-pointer style each frame; the settings toggle
             * gates it, NULL restores the default art. */
            CwvCursorInfo info;
            int info_x, info_y;
            if (s_menu.settings.css_cursors &&
                cwv_cursor_position(&s_cursor, &info_x, &info_y) &&
                cwv_host_cursor_at(s_host, info_x, info_y, &info))
                cwv_cursor_set_style(&s_cursor, &info);
            else
                cwv_cursor_set_style(&s_cursor, NULL);
            cwv_cursor_draw(&s_cursor);
        }
#endif
        /* software keyboard LAST (page, menu, cursor, keyboard) */
        cwv_ime_wii_draw(s_menu_font);
        GRRLIB_Render();
    }

    cwv_menu_ui_close(&s_menu);
#if CWV_CURSOR_ENABLED
    cwv_cursor_destroy(&s_cursor);
#endif
    cwv_host_destroy(s_host);
    GRRLIB_Exit();
    return 0;
}
