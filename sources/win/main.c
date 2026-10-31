/* main.c - CWebView for Windows (AMD64 + ARM64).
 *
 * App loop mirrors the console ports: create the host, wire the built-in
 * ES6 transformer, load <app root>/web/index.html, then pump input into
 * the shared host (mouse/touch, wheel/scroll, keys/D-pad) and draw each
 * frame through the GDI seam. The app root is the folder the .exe sits
 * in, so web/ lives next to the executable exactly like the Wii/Wii U
 * SD layouts.
 *
 * Input mapping (first cut; remapping UI is pending):
 *   mouse move/left button      pointer (touch) events
 *   wheel / Up/Down/PgUp/PgDn   page scroll
 *   Tab                          D-pad focus cycle
 *   Enter                        activate focused control (opens the
 *                                keyboard dialog for text inputs)
 *   F5                           reload the current URL
 *   Esc                          clear D-pad focus
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/cwv_platform.h"
#include "cwv_win_internal.h"
#include "core/cwv_host.h"
#include "core/cwv_renderer.h"      /* CwvCursorInfo - CSS-aware WM_SETCURSOR */
#include "menu/cwv_menu_ui.h"
#include "menu/native_menu_templates.h"

static CwvHost *s_host;
static HWND s_hwnd;
static char s_app_dir[512];        /* exe folder, forward slashes */
static char s_last_url[1024];

/* ---- CWMenu shell (shared, same integration as Wii/Wii U) -------- */
static CwvMenuUi s_menu;
static void *s_menu_font;
static cwv_utf16 s_menu_url_text[1024];
static int s_menu_url_ime;
static char s_menu_url_prefill[512];
static void *s_menu_favicon;
static char s_menu_favicon_source[512];
static char s_prefs_path[600];
static int s_esm_ok;
static unsigned s_frame_count;
static float s_frame_ms;

/* ------------------------------------------------------------------ */
/* app-specific runtime log (user direction 2026-10-01): the log lives
 * next to the .exe, so every installed app owns its own CWV.log.       */
/* ------------------------------------------------------------------ */
/* Debug/dev builds keep logging (user correction 2026-10-01: hardware/
 * desktop debugging needs CWV.log; CWV_DEBUG may add verbose lines). */
static void win_runtime_log(const char *fmt, ...)
{
    char line[512];
    char path[600];
    va_list ap;
    int len;
    if (!s_app_dir[0]) return;
    va_start(ap, fmt);
    len = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if (len > (int)sizeof(line) - 3) len = (int)sizeof(line) - 3;
    line[len++] = '\n';
    line[len] = '\0';
    _snprintf(path, sizeof(path), "%s/CWV.log", s_app_dir);
    cwv_file_append(path, line, (size_t)len);
}


/* ---- menu glue: CwvMenuPaint + CwvMenuPlatformOps over the seam ---- */
static int menu_files_list(const char *relative, CwvMenuFileEntry *out,
                           int capacity, void *ud)
{
    char pattern[1200];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;
    (void)ud;
    if (!out || capacity <= 0 || !s_app_dir[0]) return 0;
    if (relative && (strstr(relative, "..") || strchr(relative, '\\'))) return -1;
    if (_snprintf(pattern, sizeof(pattern), "%s\\web\\%s%s*", s_app_dir,
                  relative && *relative ? relative : "",
                  relative && *relative ? "\\" : "") >= (int)sizeof(pattern)) return -1;
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (n >= capacity) break;
        /* WIN32_FIND_DATAA reserves MAX_PATH (260) bytes. The shared
         * menu entry buffer is sized to the same limit, including NUL. */
        {
            size_t name_len = strlen(fd.cFileName);
            if (name_len >= sizeof(out[n].name))
                name_len = sizeof(out[n].name) - 1;
            memcpy(out[n].name, fd.cFileName, name_len);
            out[n].name[name_len] = '\0';
        }
        out[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ++n;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
static int menu_load_script(const char *file, void *ud)
{
    char path[1024];
    (void)ud;
    if (!file || !*file || strchr(file, '/') || strchr(file, '\\'))
        return -1;
    _snprintf(path, sizeof(path), "%s/cwmenu/%s", s_app_dir, file);
    return vita_native_menu_templates_link(path);
}
static void menu_fill(float x, float y, float w, float h, unsigned c, void *ud)
{ (void)ud; cwv_draw_rectangle(x, y, w, h, c); }
static void menu_circle(float x, float y, float r, unsigned c, void *ud)
{ (void)ud; cwv_draw_fill_circle(x, y, r, c); }
static void menu_text(float x, float y, unsigned c, float z, const char *txt, void *ud)
{ (void)ud; if (s_menu_font) cwv_pgf_draw_text(s_menu_font, x, y, c, z, txt); }
static float menu_text_width(float z, const char *txt, void *ud)
{
    (void)ud;
    /* The Windows frame IS the 960x544 virtual canvas (no console
     * fudge factor needed, unlike the Wii U 854x480 fit). */
    return s_menu_font ? (float)cwv_pgf_text_width(s_menu_font, z, txt)
                       : (float)strlen(txt) * 8.0f * z;
}
static void menu_favicon(float x, float y, float w, float h,
                         const char *url, void *ud)
{
    char path[1024];
    char prefix[600];
    const char *ext, *end;
    size_t len, pl;
    (void)ud;
    _snprintf(prefix, sizeof(prefix), "%s/", cwv_platform_url_root());
    if (!url || !*url || strncmp(url, prefix, strlen(prefix)) != 0) {
        if (s_menu_favicon) cwv_tex_free(s_menu_favicon);
        s_menu_favicon = NULL;
        s_menu_favicon_source[0] = 0;
        return;
    }
    if (strcmp(url, s_menu_favicon_source) != 0) {
        if (s_menu_favicon) cwv_tex_free(s_menu_favicon);
        s_menu_favicon = NULL;
        _snprintf(s_menu_favicon_source, sizeof(s_menu_favicon_source),
                  "%s", url);
        end = strpbrk(url + strlen(prefix), "?#");
        len = end ? (size_t)(end - (url + strlen(prefix)))
                  : strlen(url + strlen(prefix));
        pl = strlen(cwv_platform_path_root());
        if (len + pl + 1 >= sizeof(path)) return;
        memcpy(path, cwv_platform_path_root(), pl);
        memcpy(path + pl, url + strlen(prefix) - 1, len);   /* keep '/' */
        path[len + pl] = 0;
        ext = strrchr(path, '.');
        if (ext && (!_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg")))
            s_menu_favicon = cwv_tex_load_jpeg_file(path);
        else if (ext && !_stricmp(ext, ".png"))
            s_menu_favicon = cwv_tex_load_png_file(path);
    }
    if (s_menu_favicon && cwv_tex_width(s_menu_favicon) > 0 &&
        cwv_tex_height(s_menu_favicon) > 0)
        cwv_draw_texture_scale(s_menu_favicon, x, y,
            w / (float)cwv_tex_width(s_menu_favicon),
            h / (float)cwv_tex_height(s_menu_favicon));
}
/* Bresenham through the seam's rectangle primitive: the GDI surface has
 * no line API, but 1px rects compose identically on every port. */
static void menu_line(float x1, float y1, float x2, float y2,
                      unsigned color, void *ud)
{
    int x = (int)x1, y = (int)y1;
    int ex = (int)x2, ey = (int)y2;
    int dx = ex > x ? ex - x : x - ex, dy = ey > y ? ey - y : y - ey;
    int sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1, err = dx - dy;
    (void)ud;
    for (;;) {
        cwv_draw_rectangle((float)x, (float)y, 1.0f, 1.0f, color);
        if (x == ex && y == ey) break;
        if (2 * err > -dy) { err -= dy; x += sx; }
        if (2 * err < dx)  { err += dx; y += sy; }
    }
}
static int menu_save_prefs(void *ud)
{
    CwvMenuUi *ui = (CwvMenuUi *)ud;
    char out[256];
    int n = _snprintf(out, sizeof(out),
        "scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\ndark=%d\n"
        "accent=%d\nmenu_hotkey=%d\nshow_exit_app=%d\ncss_cursors=%d\n",
        ui->settings.scroll_mode, ui->settings.scroll_speed,
        ui->settings.zoom_mode, (int)(ui->settings.zoom_level * 100.0f + 0.5f),
        ui->settings.dark_mode, ui->settings.accent,
        ui->settings.menu_hotkey, ui->settings.show_exit_app,
        ui->settings.css_cursors);
    if (n <= 0 || n >= (int)sizeof(out)) return -1;
    _snprintf(s_prefs_path, sizeof(s_prefs_path), "%s/settings.cfg", s_app_dir);
    return cwv_file_write_all(s_prefs_path, out, (size_t)n);
}
static int menu_load_prefs(void *ud)
{
    CwvMenuUi *ui = (CwvMenuUi *)ud;
    char buf[256];
    int fd = cwv_file_open_read(s_prefs_path), n;
    int sc = 0, spd = 1, zm = 0, zl = 100, dk = 1, ac = 0, hk = 0;
    int show_exit = 0, cc = 1;
    if (fd < 0) return -1;
    n = cwv_file_read(fd, buf, sizeof(buf) - 1);
    cwv_file_close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    if (sscanf(buf, "scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\n"
                   "dark=%d\naccent=%d", &sc, &spd, &zm, &zl, &dk, &ac) != 6)
        return -1;
    {
        const char *extra = strstr(buf, "menu_hotkey=");
        if (extra && sscanf(extra, "menu_hotkey=%d\nshow_exit_app=%d",
                            &hk, &show_exit) != 2)
            return -1;
    }
    {
        const char *extra = strstr(buf, "css_cursors=");
        if (extra) (void)sscanf(extra, "css_cursors=%d", &cc);
    }
    if (sc < 0 || sc > 2 || spd < 0 || spd > 2 || zm < 0 || zm > 2 ||
        zl < 75 || zl > 200 || dk < 0 || dk > 1 || ac < 0 || ac > 5 ||
        hk < 0 || hk > 2 || show_exit < 0 || show_exit > 1 || cc < 0 || cc > 1)
        return -1;
    ui->settings.scroll_mode = sc; ui->settings.scroll_speed = spd;
    ui->settings.zoom_mode = zm;   ui->settings.zoom_level = (float)zl / 100.0f;
    ui->settings.dark_mode = dk;   ui->settings.accent = ac;
    ui->settings.menu_hotkey = hk; ui->settings.show_exit_app = show_exit;
    ui->settings.css_cursors = cc;
    ui->settings.dirty = 0;
    if (s_host) (void)cwv_host_set_zoom(s_host, ui->settings.zoom_level);
    return 0;
}
static void menu_open_url(void *ud)
{
    static const cwv_utf16 title[] = { 'O','p','e','n',' ','U','R','L',0 };
    size_t i;
    (void)ud;
    s_menu_url_text[0] = 0;
    /* Prefill with the entry page loaded at boot (same behavior as the
     * Wii U port). */
    for (i = 0; s_menu_url_prefill[i] && i < 511; i++)
        s_menu_url_text[i] = (cwv_utf16)(unsigned char)s_menu_url_prefill[i];
    s_menu_url_text[i] = 0;
    s_menu_url_ime =
        (cwv_ime_open(title, s_menu_url_text, 0, 0, s_menu_url_text, 1024) == 0);
}
static void menu_quit_app(void *ud)
{ (void)ud; PostQuitMessage(0); }
static int menu_clear_cache(void *ud)
{
    char dir[1024];
    wchar_t wdir[1024], wpat[1100], file[1100];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int failed = 0;
    (void)ud;
    if (cwv_console_cache_dir(dir, sizeof(dir)) != 0)
        return CWV_MENU_STATUS_NONE;
    if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 1024) <= 0)
        return CWV_MENU_STATUS_NONE;
    _snwprintf(wpat, 1100, L"%s\\*", wdir);
    h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) return CWV_MENU_STATUS_NONE;
    do {
        if (fd.cFileName[0] == L'.') continue;
        _snwprintf(file, 1100, L"%s\\%s", wdir, fd.cFileName);
        if (!DeleteFileW(file)) failed = 1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return failed ? CWV_MENU_STATUS_NONE : CWV_MENU_STATUS_CACHE_CLEARED;
}
static void menu_log(const char *msg, void *ud)
{ (void)ud; win_runtime_log("menu: %s", msg); }

/* URL dialog completion (mirrors the Wii U UTF-16 -> UTF-8 decode). */
static void menu_poll_url(void)
{
    char url[2048];
    size_t i = 0, o = 0;
    int done;
    if (!s_menu_url_ime) return;
    done = cwv_ime_poll(s_menu_url_text, 1024);
    if (done == 0) return;
    s_menu_url_ime = 0;
    if (done != 1) return;
    while (s_menu_url_text[i] && o < sizeof(url) - 5) {
        unsigned cp = s_menu_url_text[i++];
        if (cp >= 0xd800 && cp <= 0xdbff) {
            unsigned lo = s_menu_url_text[i];
            if (lo < 0xdc00 || lo > 0xdfff) continue;
            i++; cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) continue;
        if (cp < 0x80) url[o++] = (char)cp;
        else if (cp < 0x800) { url[o++] = (char)(0xc0 | (cp >> 6)); url[o++] = (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { url[o++] = (char)(0xe0 | (cp >> 12)); url[o++] = (char)(0x80 | ((cp >> 6) & 63)); url[o++] = (char)(0x80 | (cp & 63)); }
        else { url[o++] = (char)(0xf0 | (cp >> 18)); url[o++] = (char)(0x80 | ((cp >> 12) & 63)); url[o++] = (char)(0x80 | ((cp >> 6) & 63)); url[o++] = (char)(0x80 | (cp & 63)); }
    }
    url[o] = 0;
    if (!o) return;
    if (cwv_host_load(s_host, url) == 0) {
        cwv_menu_ui_record_page(&s_menu);
        cwv_menu_ui_close(&s_menu);
    } else {
        win_runtime_log("menu: Open URL failed: %s", cwv_host_last_error(s_host));
    }
}

static void menu_fill_live(CwvMenuData *d, int frame, float ms)
{
    const char *s;
    memset(d, 0, sizeof(*d));
    d->frame_count = (unsigned)frame;
    d->frame_ms = ms;
    d->fps = ms > 0.0f ? 1000.0f / ms : 0.0f;
    d->scroll_offset = cwv_host_scroll_offset(s_host);
    d->zoom = cwv_host_zoom(s_host);
    d->viewport_w = CWV_WIN_DEFAULT_W;
    d->viewport_h = CWV_WIN_DEFAULT_H;
    d->node_count = cwv_host_node_count(s_host);
    d->js_active = cwv_host_js_active(s_host);
    s = cwv_host_js_last_error(s_host);
    if (s) snprintf(d->js_last_error, sizeof(d->js_last_error), "%s", s);
    s = cwv_host_page_title(s_host);
    if (s) snprintf(d->page_title, sizeof(d->page_title), "%s", s);
    s = cwv_host_current_url(s_host);
    if (s) snprintf(d->page_url, sizeof(d->page_url), "%s", s);
    snprintf(d->version_display, sizeof(d->version_display), "Windows");
    snprintf(d->control_mode_name, sizeof(d->control_mode_name), "%s",
             cwv_host_control_mode(s_host) == 3 ? "Touch" : "Browser");
    cwv_host_focus_info(s_host, d->dpad_focus, sizeof(d->dpad_focus));
    snprintf(d->platform_row_esm, sizeof(d->platform_row_esm),
             "ESM module: built-in ES6 transformer (%s)",
             s_esm_ok ? "self-test pass" : "SELF-TEST FAILED");
    snprintf(d->platform_row_status, sizeof(d->platform_row_status),
             "Windows platform integration is active (AMD64 + ARM64).");
}

/* ------------------------------------------------------------------ */
/* window + input                                                      */
/* ------------------------------------------------------------------ */
static void reload_current(void) {
    const char *cur = cwv_host_current_url(s_host);
    if (cur && cur[0]) {
        snprintf(s_last_url, sizeof(s_last_url), "%s", cur);
        cwv_host_load(s_host, s_last_url);
    }
}

/* PC zoom controls (Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0): the live
 * value is clamped to the Settings range (75%-200%) and marked dirty so
 * the usual settings save keeps it. */
static void zoom_step(float factor)
{
    float z = s_menu.settings.zoom_level * factor;
    if (z < 0.75f) z = 0.75f;
    if (z > 2.0f) z = 2.0f;
    s_menu.settings.zoom_level = z;
    s_menu.settings.dirty = 1;
    if (s_host) (void)cwv_host_set_zoom(s_host, z);
}

static int s_fullscreen;
static WINDOWPLACEMENT s_windowed_placement = { sizeof(WINDOWPLACEMENT) };
static DWORD s_windowed_style;

static void toggle_fullscreen(HWND hwnd)
{
    if (!s_fullscreen) {
        MONITORINFO mi;
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof(mi);
        if (!GetWindowPlacement(hwnd, &s_windowed_placement)) return;
        s_windowed_style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (!GetMonitorInfoW(monitor, &mi)) return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        s_fullscreen = 1;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, (LONG_PTR)s_windowed_style);
        SetWindowPlacement(hwnd, &s_windowed_placement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        s_fullscreen = 0;
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        RECT rc;
        int down = (wp & MK_LBUTTON) != 0;
        POINTS pt = MAKEPOINTS(lp);
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return 0;
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            if (down) cwv_menu_ui_touch(&s_menu,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 1);
            return 0;
        }
        /* cwv_host_pointer_event() treats down=0 as a release/activation,
         * not as a hover sample.  Sending it for ordinary WM_MOUSEMOVE made
         * every hover behave like a click (including following links).
         * The Windows cursor shape is resolved independently in WM_SETCURSOR.
         * Only feed movement while a real left-button drag is in progress. */
        if (s_host && down)
            cwv_host_pointer_event(s_host,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 1);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT rc;
        POINTS pt = MAKEPOINTS(lp);
        SetCapture(hwnd);
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return 0;
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            cwv_menu_ui_touch(&s_menu,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 1);
            return 0;
        }
        if (s_host)
            cwv_host_pointer_event(s_host,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 1);
        return 0;
    }
    case WM_LBUTTONUP: {
        RECT rc;
        POINTS pt = MAKEPOINTS(lp);
        ReleaseCapture();
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return 0;
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            cwv_menu_ui_touch(&s_menu,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 0);
            return 0;
        }
        if (s_host)
            cwv_host_pointer_event(s_host,
                pt.x * CWV_WIN_DEFAULT_W / rc.right,
                pt.y * CWV_WIN_DEFAULT_H / rc.bottom, 0);
        return 0;
    }
    case WM_RBUTTONUP:
        /* Right-click: context menu (CWMenu), same as a console hotkey. */
        if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED)
            cwv_menu_ui_open(&s_menu, CWV_MENU_MAIN);
        else
            cwv_menu_ui_close(&s_menu);
        return 0;
    case WM_MOUSEWHEEL:
        /* PC convention: Ctrl+wheel zooms, plain wheel scrolls. */
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
            cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED) {
            if ((short)HIWORD(wp) > 0) zoom_step(1.10f);
            else if ((short)HIWORD(wp) < 0) zoom_step(1.0f / 1.10f);
            return 0;
        }
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED)
            cwv_menu_ui_scroll(&s_menu, -(float)(short)HIWORD(wp) / 40.0f);
        else if (s_host)
            cwv_host_scroll_by(s_host, -(float)(short)HIWORD(wp) / 40.0f);
        return 0;
    case WM_MOUSEHWHEEL:
        /* Tilt wheels and touchpads: horizontal scroll at the same scale
         * as the vertical wheel.  Positive delta scrolls right. */
        if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED && s_host)
            cwv_host_scroll_x_by(s_host, (float)(short)HIWORD(wp) / 40.0f);
        return 0;
    case WM_KEYUP:
        if (wp == VK_RETURN || wp == VK_SPACE)
            cwv_host_focus_release(s_host);
        return 0;
    case WM_KEYDOWN: {
        /* PC-specific key grammar (mirrors the console controls where it
         * makes sense and drops what does not - see README-WIN.md):
         *   LShift+RShift+Space or F1 - CWMenu (like L+R+SELECT)
         *   arrows - page scroll / menu navigation (D-pad)
         *   PgUp/PgDn - page-scroll by a page (L/R shoulder)
         *   Tab - cycle focus, Return/Space - activate, Esc - clear
         *   Ctrl+R or F5 - reload
         *   Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0 - zoom (in/out/reset)
         */
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int both_shifts = (GetKeyState(VK_LSHIFT) & 0x8000) != 0 &&
                          (GetKeyState(VK_RSHIFT) & 0x8000) != 0;
        if (wp == VK_F11) { toggle_fullscreen(hwnd); return 0; }
        if (!s_host) return 0;
        if ((wp == VK_SPACE && both_shifts) || wp == VK_F1) {
            /* the PC menu hotkey */
            if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED)
                cwv_menu_ui_open(&s_menu, CWV_MENU_MAIN);
            else
                cwv_menu_ui_close(&s_menu);
            return 0;
        }
        if (ctrl && (wp == 'R' || wp == 'r')) { reload_current(); return 0; }
        if (ctrl && (wp == VK_OEM_PLUS || wp == '='))
            { zoom_step(1.10f); return 0; }
        if (ctrl && (wp == VK_OEM_MINUS || wp == '-'))
            { zoom_step(1.0f / 1.10f); return 0; }
        if (ctrl && wp == '0') {
            /* reset to 100%: zoom_step() multiplies, so set it directly */
            s_menu.settings.zoom_level = 1.0f;
            s_menu.settings.dirty = 1;
            if (s_host) (void)cwv_host_set_zoom(s_host, 1.0f);
            return 0;
        }
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED)
                cwv_menu_ui_open(&s_menu, CWV_MENU_MAIN);
            else
                cwv_menu_ui_close(&s_menu);
            return 0;
        }
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            switch (wp) {
            case VK_DOWN:  cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_DOWN); break;
            case VK_UP:    cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_UP); break;
            case VK_LEFT:  cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_LEFT); break;
            case VK_RIGHT: cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_RIGHT); break;
            case VK_RETURN:
            case VK_SPACE:
                cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CONFIRM); break;
            case VK_ESCAPE: cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_CANCEL); break;
            case VK_NEXT:  cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_R1); break;
            case VK_PRIOR: cwv_menu_ui_input(&s_menu, CWV_MENU_INPUT_L1); break;
            }
            return 0;
        }
        switch (wp) {
        case VK_DOWN:  cwv_host_scroll_by(s_host, 60.0f); break;
        case VK_UP:    cwv_host_scroll_by(s_host, -60.0f); break;
        case VK_LEFT:  /* 2026-10-06 (74): horizontal arrow scrolling was
                          menu-only; both axes can now be adjusted
                          together (hold Up+Right scrolls diagonally). */
                      cwv_host_scroll_x_by(s_host, -60.0f); break;
        case VK_RIGHT: cwv_host_scroll_x_by(s_host, 60.0f); break;
        case VK_NEXT:  cwv_host_scroll_by(s_host, 480.0f); break;
        case VK_PRIOR: cwv_host_scroll_by(s_host, -480.0f); break;
        case VK_HOME:  cwv_host_scroll_to(s_host, 0.0f); break;
        case VK_TAB:   cwv_host_focus_cycle(s_host, 1); break;
        case VK_RETURN:
        case VK_SPACE: cwv_host_focus_activate(s_host); break;
        case VK_ESCAPE: cwv_host_focus_clear(s_host); break;
        case VK_F5:    reload_current(); break;
        }
        return 0;
    }
    case WM_SETCURSOR: {
        /* PC-native pointer (2026-10-05): the system cursor stays
         * VISIBLE and becomes CSS-aware - the engine's hover style picks
         * the Windows cursor shape.  (The old code hid the system cursor
         * intending to draw the engine's art cursor, but the Windows
         * port never draws it - the cursor simply vanished.  The art
         * cursor path is console-only; on Windows the OS cursor is the
         * right tool, so cursor:none is the only hidden state.) */
        POINT pt;
        RECT rc;
        HCURSOR cur = NULL;
        GetCursorPos(&pt);
        GetClientRect(hwnd, &rc);
        ScreenToClient(hwnd, &pt);
        if (pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom) {
            cur = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED &&
                s_host && s_menu.settings.css_cursors) {
                CwvCursorInfo info;
                if (cwv_host_cursor_at(s_host,
                        pt.x * CWV_WIN_DEFAULT_W / (rc.right ? rc.right : 1),
                        pt.y * CWV_WIN_DEFAULT_H / (rc.bottom ? rc.bottom : 1),
                        &info)) {
                    switch (info.style) {
                    case CWV_CSTYLE_POINTER:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_HAND); break;
                    case CWV_CSTYLE_TEXT:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM); break;
                    case CWV_CSTYLE_WAIT:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT); break;
                    case CWV_CSTYLE_CROSSHAIR:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_CROSS); break;
                    case CWV_CSTYLE_HELP:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_HELP); break;
                    case CWV_CSTYLE_MOVE:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL); break;
                    case CWV_CSTYLE_GRAB:
                    case CWV_CSTYLE_GRABBING:
                        /* Windows has no grab cursors; Chrome on Windows
                         * shows the hand for these, not the 4-way move. */
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_HAND); break;
                    case CWV_CSTYLE_NOT_ALLOWED:
                        cur = LoadCursorW(NULL, (LPCWSTR)IDC_NO); break;
                    case CWV_CSTYLE_NONE:
                        /* the page asked for it: honor cursor:none */
                        cur = NULL;
                        break;
                    default:
                        break;
                    }
                }
            }
            SetCursor(cur);
            return TRUE;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* boot                                                                */
/* ------------------------------------------------------------------ */
static void init_app_dir(void) {
    wchar_t wexe[512];
    char exe[512];
    char *slash;
    int n = GetModuleFileNameW(NULL, wexe, 512);
    if (n <= 0) return;
    if (WideCharToMultiByte(CP_UTF8, 0, wexe, -1, exe, sizeof(exe),
                            NULL, NULL) <= 0)
        return;
    slash = strrchr(exe, '\\');
    if (!slash) slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    {
        char *p;
        for (p = exe; *p; p++)
            if (*p == '\\') *p = '/';
        snprintf(s_app_dir, sizeof(s_app_dir), "%s", exe);
    }
}

/* WM_SETCURSOR only arrives when the mouse MOVES.  Chrome re-evaluates the
 * cursor whenever the content under a stationary pointer changes (wheel
 * scroll, navigation, script changes), so re-run the handler each frame
 * while the pointer really is over our client area. */
static void refresh_cursor(void) {
    POINT pt;
    RECT rc;
    if (!s_hwnd || GetForegroundWindow() != s_hwnd || GetCapture() != NULL)
        return;
    if (!GetCursorPos(&pt) || WindowFromPoint(pt) != s_hwnd) return;
    GetClientRect(s_hwnd, &rc);
    ScreenToClient(s_hwnd, &pt);
    if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom) return;
    SendMessageW(s_hwnd, WM_SETCURSOR, (WPARAM)s_hwnd,
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    /* (78) build-windows.sh passes the manifest's "webpage" key here;
     * default is the same path it has always been. */
#ifndef CWV_WIN_WEBPAGE
#define CWV_WIN_WEBPAGE "web/index.html"
#endif
    static const char page[] = CWV_WIN_WEBPAGE;
    char url_root[600], entry_url[1100];
    WNDCLASSW wc;
    MSG msg;
    char report[192];
    int running = 1;
    (void)prev; (void)cmdline;

    /* Opt out of Windows bitmap virtualization so fullscreen/maximized
     * sizes and pointer coordinates use the monitor's real pixel grid.
     * Resolve the newer API dynamically: some MinGW/Windows SDK headers
     * expose the DPI_AWARENESS_CONTEXT constants but omit the function
     * declaration unless a recent _WIN32_WINNT is selected. Dynamic lookup
     * keeps x64 and ARM64 builds compatible with older header sets while
     * still using per-monitor-v2 awareness on Windows 10+ when available. */
    {
        typedef BOOL (WINAPI *CwvSetProcessDpiAwarenessContextFn)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        CwvSetProcessDpiAwarenessContextFn set_context = NULL;
        if (user32) {
            set_context = (CwvSetProcessDpiAwarenessContextFn)
                GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        }
        if (!set_context ||
            !set_context((HANDLE)(LONG_PTR)-4)) /* PER_MONITOR_AWARE_V2 */
            (void)SetProcessDPIAware();
    }

    init_app_dir();
    snprintf(url_root, sizeof(url_root), "file:///%s",
              s_app_dir[0] ? s_app_dir : ".");
    cwv_platform_set_app_root(url_root, s_app_dir[0] ? s_app_dir : ".");

    cwv_console_init();
    win_runtime_log("--- CWebView for Windows boot ---");
    win_runtime_log("app root: %s", s_app_dir[0] ? s_app_dir : "(none)");

    cwv_win_frame_size(CWV_WIN_DEFAULT_W, CWV_WIN_DEFAULT_H);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CWebViewWin";
    RegisterClassW(&wc);

    RECT wr = { 0, 0, CWV_WIN_DEFAULT_W, CWV_WIN_DEFAULT_H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    s_hwnd = CreateWindowExW(0, L"CWebViewWin", L"CWebView for Windows",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             wr.right - wr.left, wr.bottom - wr.top,
                             NULL, NULL, inst, NULL);
    if (!s_hwnd) {
        win_runtime_log("CreateWindow failed (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(s_hwnd, show);
    UpdateWindow(s_hwnd);

    s_host = cwv_host_create(CWV_WIN_DEFAULT_W, CWV_WIN_DEFAULT_H);
    if (!s_host) {
        win_runtime_log("cwv_host_create failed");
        return 1;
    }
    cwv_host_set_runtime_log(s_host, win_runtime_log);

    /* Built-in ES6 transformer (same provider as Wii/Wii U). */
    s_esm_ok = 0;
    cwv_host_enable_ts_builtin(s_host);
    if (cwv_host_enable_es_builtin(s_host) == 0) {
        int st = cwv_host_esm_selftest(s_host, report, sizeof(report));
        s_esm_ok = (st == 1);
        win_runtime_log("ES6 built-in self-test %s: %s",
                        st == 1 ? "PASS" : "FAIL", report);
    } else {
        win_runtime_log("ES6 built-in transformer unavailable");
    }

    /* ---- CWMenu shell (same integration as the Wii/Wii U ports) ---- */
    {
        static CwvMenuPaint paint;
        static CwvMenuPlatformOps ops;
        char script[1024];
        int rc;
        paint = (CwvMenuPaint){menu_fill, menu_circle, menu_text,
                                menu_text_width, NULL, menu_favicon,
                                menu_line};
        ops = (CwvMenuPlatformOps){menu_save_prefs, menu_load_prefs,
                                   menu_open_url, menu_quit_app,
                                   menu_clear_cache, menu_log,
                                   menu_load_script, menu_files_list, &s_menu};
        s_menu_font = cwv_pgf_load_default();
        cwv_menu_ui_init(&s_menu, &paint, &ops, s_host,
                         CWV_WIN_DEFAULT_W, CWV_WIN_DEFAULT_H);
        _snprintf(script, sizeof(script), "%s/cwmenu/default.cwm", s_app_dir);
        rc = vita_native_menu_templates_load(script);
        if (rc != 0)
            win_runtime_log("CWMenu script unavailable (%s): %d", script, rc);
        else
            win_runtime_log("CWMenu %d loaded from %s",
                            cwmenu_script_version(), script);
        _snprintf(s_prefs_path, sizeof(s_prefs_path),
                  "%s/settings.cfg", s_app_dir);
        (void)menu_load_prefs(&s_menu);
        (void)cwv_host_set_theme(s_host, cwv_menu_ui_effective_dark(&s_menu));
    }

    snprintf(entry_url, sizeof(entry_url), "%s/%s", url_root, page);
    if (cwv_host_load(s_host, entry_url) != 0)
        win_runtime_log("initial page load failed: %s (%s)",
                        cwv_host_last_error(s_host), entry_url);
    snprintf(s_last_url, sizeof(s_last_url), "%.1023s", entry_url);
    snprintf(s_menu_url_prefill, sizeof(s_menu_url_prefill), "%.511s", entry_url);
    cwv_menu_ui_record_page(&s_menu);

    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        menu_poll_url();

        cwv_win_frame_begin();
        cwv_host_update(s_host);
        cwv_host_draw(s_host);
        if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
            CwvMenuData live;
            menu_fill_live(&live, (int)s_frame_count, s_frame_ms);
            cwv_menu_ui_draw(&s_menu, &live);
        }
        cwv_win_frame_end(s_hwnd);
        refresh_cursor();

        s_frame_count++;
        Sleep(16);   /* ~60 fps cap; the page renders on demand anyway */
    }

    cwv_host_destroy(s_host);
    cwv_console_shutdown();
    win_runtime_log("shutdown clean");
    return 0;
}
