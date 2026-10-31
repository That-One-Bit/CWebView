#ifndef CWV_HOST_H
#define CWV_HOST_H
#include <stddef.h>   /* size_t (console dump) */
/* Deliberately self-contained: cwv_host.h does not include
 * cwv_backend.h (the ES6 hook signatures are spelled out below), so
 * the host wrapper stays usable wherever the backend header is not on
 * the include path. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CwvHost CwvHost;
/* CwvCursorInfo is defined in cwv_renderer.h; spelled out here so this
 * header stays self-contained (same pattern as the ES6 hooks). */
typedef struct CwvCursorInfo CwvCursorInfo;
typedef enum CwvHostStatus {
    CWV_STATUS_UNINITIALIZED = 0,
    CWV_STATUS_INITIALIZED,
    CWV_STATUS_LOADING,
    CWV_STATUS_READY,
    CWV_STATUS_ERROR
} CwvHostStatus;

enum { CWV_BACKEND_NOT_IMPLEMENTED = -100 };

CwvHost *cwv_host_create(int width, int height);
int cwv_host_load(CwvHost *host, const char *url);
int cwv_host_eval(CwvHost *host, const char *javascript);
int cwv_host_update(CwvHost *host);
int cwv_host_pointer_event(CwvHost *host, int x, int y, int down);
void cwv_host_pointer_cancel(CwvHost *host);
/* CSS cursor at the screen point (2026-09-26 (45)): fills *out with the
 * style under the pointer (keyword, or URL art + its CSS hotspot) and
 * returns 1; returns 0 with out->style=CWV_CSTYLE_AUTO when the plain
 * default applies.  Call per frame before drawing the pointer cursor;
 * the returned img is engine-owned - draw it, never free it. */
int cwv_host_cursor_at(CwvHost *host, int x, int y, CwvCursorInfo *out);
int cwv_host_scroll_by(CwvHost *host, float delta);
int cwv_host_scroll_to(CwvHost *host, float offset);
int cwv_host_set_zoom(CwvHost *host, float zoom);
float cwv_host_zoom(const CwvHost *host);
float cwv_host_scroll_offset(const CwvHost *host);
int cwv_host_scroll_x_by(CwvHost *host, float delta);
/* (84) point-aware scroll: the scroll region under the screen point
 * consumes the delta first (touch drag dispatch); falls back to the
 * page-only scroll when the backend predates the op. */
int cwv_host_scroll_at(CwvHost *host, float delta, int x, int y);
int cwv_host_scroll_x_at(CwvHost *host, float delta, int x, int y);
/* (84) PAGE-only horizontal scroll (page scrollbar drag); falls back to
 * cwv_host_scroll_x_by on backends without the op. */
int cwv_host_scroll_page_x_by(CwvHost *host, float delta);
float cwv_host_scroll_x_offset(const CwvHost *host);
float cwv_host_content_width(const CwvHost *host);
float cwv_host_content_height(const CwvHost *host);
int cwv_host_focus_cycle(CwvHost *host, int direction);
int cwv_host_focus_clear(CwvHost *host);
int cwv_host_focus_activate(CwvHost *host);
int cwv_host_vcm_activate(CwvHost *host, const char *control);
int cwv_host_control_mode(const CwvHost *host);
const char *cwv_host_button_wait(const CwvHost *host);
int cwv_host_set_button_wait_override(CwvHost *host, const char *spec);
int cwv_host_set_theme(CwvHost *host, int dark);
int cwv_host_set_scrollbars(CwvHost *host, int visible);
int cwv_host_draw(CwvHost *host);
int cwv_host_open_image_preview(CwvHost *host, const char *data_uri);
CwvHostStatus cwv_host_status(const CwvHost *host);
const char *cwv_host_last_error(const CwvHost *host);
const char *cwv_host_current_url(const CwvHost *host);
const char *cwv_host_page_title(const CwvHost *host);
const char *cwv_host_favicon_url(const CwvHost *host);
void cwv_host_destroy(CwvHost *host);

/* ---- ES6 support -------------------------------------------------------
 * The engine runs ES5.1; ES6 is added by registering two hooks with the
 * backend: a source transformer (rewrites ES6 syntax before scripts
 * run) and a library prelude (installs Map/Set, Array.find, ... into
 * every fresh JS context).  Two providers exist:
 *
 *   built-in   cwv_host_enable_es_builtin()  - shared; the transformer
 *              is compiled into the app (cwv_es2015_core.c).  Used by
 *              EVERY port (2026-10-03: the Vita's CwvESM.suprx runtime
 *              module was removed - it duplicated the transformer in
 *              RAM and was silently stubbed on Vita3K).
 *
 * Both providers need the same three console-neutral host services
 * (evaluate / resolve / read); they live in shared and are handed out
 * below so the Vita glue never reaches into host internals. */

/* The three host services any ES6 provider needs, host already bound
 * into `userdata`. */
typedef struct CwvHostEs6Services {
    void *userdata;                                       /* the CwvHost */
    int (*evaluate)(void *userdata, const char *javascript);
    int (*resolve_url)(void *userdata, const char *base_url,
                       const char *specifier, char *out, size_t out_size);
    int (*load_file)(void *userdata, const char *url, char *out,
                     size_t out_size);
} CwvHostEs6Services;

/* 0 = services filled (host ready); -1 = host missing / not ready. */
int cwv_host_es6_services(CwvHost *host, CwvHostEs6Services *out);

/* Register whichever provider the port chose (providers call this; apps
 * normally call an enable_*() wrapper instead).  The signatures are the
 * backend's CwvSourceTransformFn / CwvJsPreludeFn, spelled out so this
 * header needs no backend include. */
int cwv_host_install_es6(CwvHost *host,
                              int (*transform)(const char *in, char *out,
                                               size_t cap, void *userdata),
                              void *transform_data,
                              const char *(*prelude)(void *userdata),
                              void *prelude_data);

/* Built-in provider (see above): one call, works on any console. */
int cwv_host_enable_es_builtin(CwvHost *host);

/* Registers the built-in TypeScript stripper (cwv_ts_core.c) with the
 * backend. TS scripts (type="text/typescript" or src="*.ts") are
 * stripped to JavaScript before the ES6 transform runs, so TS and
 * modern-JS features compose. No-op (0) without a backend. */
int cwv_host_enable_ts_builtin(CwvHost *host);

/* Mirror renderer diagnostics (image-load failures) into the app's
 * runtime log (registered with the backend on the host's behalf). */
int cwv_host_set_runtime_log(CwvHost *host, void (*fn)(const char *fmt, ...));

/* ESM self-test: runs a real ES6 probe through the registered transform
 * + prelude (the same hooks page scripts use) and reports transform=/
 * compile=/prelude= detail into `report`.  Returns 1 on full pass, 0 if
 * any check failed.  Call AFTER a successful enable; main.c shows the
 * result in DevTools > Platform and the debug overlay. */
int cwv_host_esm_selftest(CwvHost *host, char *report, size_t cap);

/* JavaScript status for the DevTools console tab. */
int cwv_host_js_active(CwvHost *host);
int cwv_host_node_count(CwvHost *host);
void cwv_host_focus_info(CwvHost *host, char *buf, size_t cap);
/* Most recent console.error line from the page, or "". */
const char *cwv_host_js_last_error(CwvHost *host);
/* Dump the JS console ring into buf, return the number of lines. */
int cwv_host_console(CwvHost *host, char *buf, size_t size);
void cwv_host_console_stats(CwvHost *host, int *errors, int *warns, int *lines);

/* URL fetch through the host's console network seam (LINT 2026-10-03:
 * prototype moved here from nowhere - the definition had none). */
int cwv_host_fetch_url(CwvHost *host, const char *url,
                       char *body, size_t cap);
/* START-exit redirect: returns 1 when the page intercepted the exit via
 * document.onExit(fn) (keep running), 0 otherwise (quit as before). */

#ifdef __cplusplus
}
#endif
#endif
