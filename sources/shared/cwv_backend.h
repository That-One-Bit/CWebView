#ifndef CWV_BACKEND_H
#define CWV_BACKEND_H

#include <stddef.h>   /* size_t (console dump) */

#ifndef CWV_ES6_HOOK_TYPES
#define CWV_ES6_HOOK_TYPES
/* ES6 extension hook types (same as CwvSourceTransformFn /
 * CwvJsPreludeFn in cwv_renderer.h - defined once here so either
 * public header can be included alone). */
typedef int (*CwvSourceTransformFn)(const char *in, char *out,
                                        size_t cap, void *userdata);
/* Network fetch hook for the page's fetch(): >=0 = HTTP status,
 * -1 = failure (no network / bad URL / error - never a crash). */
typedef int (*CwvFetchHook)(const char *url, char *body, size_t cap,
                                void *userdata);
typedef const char *(*CwvJsPreludeFn)(void *userdata);
#endif



#ifdef __cplusplus
extern "C" {
#endif

typedef struct CwvBackend CwvBackend;
/* CwvCursorInfo is defined in cwv_renderer.h; forward-declared here so
 * this header stays self-contained. */
typedef struct CwvCursorInfo CwvCursorInfo;

#ifndef CWV_BACKEND_NOT_IMPLEMENTED
#define CWV_BACKEND_NOT_IMPLEMENTED (-38)
#endif

typedef struct CwvBackendOps {
    int  (*initialize)(CwvBackend *backend, int width, int height);
    int  (*load)(CwvBackend *backend, const char *url);
    int  (*eval)(CwvBackend *backend, const char *javascript);
    int  (*update)(CwvBackend *backend);
    int  (*draw)(CwvBackend *backend);
    int  (*pointer_event)(CwvBackend *backend, int x, int y, int down);
    /* CSS cursor lookup at a screen point (2026-09-26 (45)); NULL when
     * the backend has no page hit-test. */
    int  (*cursor_at)(CwvBackend *backend, int x, int y, CwvCursorInfo *out);
    void (*pointer_cancel)(CwvBackend *backend);
    int  (*scroll_by)(CwvBackend *backend, float delta);
    int  (*scroll_to)(CwvBackend *backend, float offset);
    float (*scroll_offset)(CwvBackend *backend);
    int  (*scroll_x_by)(CwvBackend *backend, float delta);
    /* (84) point-based scroll dispatch (touch/pointer drag): the deepest
     * scroll region under the screen point consumes the delta first; the
     * leftover reaches the page.  NULL = fall back to scroll_by/x_by. */
    int  (*scroll_at)(CwvBackend *backend, float delta, int x, int y);
    int  (*scroll_x_at)(CwvBackend *backend, float delta, int x, int y);
    /* (84) PAGE-only scroll (no region dispatch) for explicit page
     * chrome like the page scrollbar drag.  NULL = scroll_x_by. */
    int  (*scroll_page_x_by)(CwvBackend *backend, float delta);
    float (*scroll_x_offset)(CwvBackend *backend);
    float (*content_width)(CwvBackend *backend);
    float (*content_height)(CwvBackend *backend);
    int  (*focus_cycle)(CwvBackend *backend, int direction);
    int  (*focus_activate)(CwvBackend *backend);
    int  (*vcm_activate)(CwvBackend *backend, const char *control);
    int  (*control_mode)(CwvBackend *backend);
    const char *(*button_wait)(CwvBackend *backend);
    const char *(*navigation_url)(CwvBackend *backend);
    const char *(*navigation_target)(CwvBackend *backend);
    void (*shutdown)(CwvBackend *backend);
} CwvBackendOps;

struct CwvBackend {
    const CwvBackendOps *ops;
    void *userdata;
};

/* The backend ABI is intentionally isolated from CwvHost.  The initial
 * implementation is a safe unavailable backend; the native
 * adapter can replace it without changing the public wrapper API. */
const CwvBackendOps *cwv_backend_default_ops(void);

/* ---- JavaScript support (implemented in cwv_js.c) -------------------
 * These are OPTIONAL additions to the ops table above: main.c can call them
 * directly, exactly like cwv_backend_default_ops().
 */

/* Full JS console ring buffer, one "level: message" line per \n.
 * Returns the number of console lines (0 when the page has no scripts). */
int cwv_backend_console_dump(CwvBackend *backend, char *out, size_t cap);
void cwv_backend_console_stats(CwvBackend *backend, int *errors, int *warns, int *lines);

/* 1 when the current page has a live JavaScript context.  (DevTools console
 * "Console status" row.) */
int cwv_backend_js_active(CwvBackend *backend);

/* Most recent console.error line, or "" (DevTools "Last error" row). */
const char *cwv_backend_js_last_error(CwvBackend *backend);

/* Register an ES6->ES5 source transformer (the built-in provider does
 * this - see docs/EXTENDING_THE_RENDERER.md, Recipe 8).  Without one, all
 * JavaScript must be ES5.1. */
/* Register the TypeScript stripper (runs before the ES6 transform on
 * scripts the page marked as TypeScript). */
void cwv_backend_set_ts_transform(CwvBackend *backend,
                                  int (*transform)(const char *in, char *out,
                                                   size_t cap, void *userdata),
                                  void *userdata);
void cwv_backend_set_source_transform(CwvBackend *backend,
                                           CwvSourceTransformFn fn,
                                           void *userdata);

/* Mirror renderer diagnostics (image-load failures) into the app's
 * runtime log so script-free pages - which have no JS context and thus
 * no visible console - still report WHY something failed. */
void cwv_backend_set_runtime_log(CwvBackend *backend,
                                      void (*fn)(const char *fmt, ...));

/* Register the network fetch backend for the page's fetch() global.
 * NULL (the default) makes fetch() fail gracefully with status -1. */
void cwv_backend_set_fetch_hook(CwvBackend *backend,
                                     CwvFetchHook fn, void *userdata);

/* Register the ES6 library prelude (Map/Set, Array.find, ... - provided
 * by the built-in provider, injected at JS context creation). */
void cwv_backend_set_js_prelude(CwvBackend *backend,
                                     CwvJsPreludeFn fn, void *userdata);

/* ESM SELF-TEST: prove the registered ES6 transform + library prelude
 * actually work, end to end, right now.  This closes the last silent
 * failure mode of the ESM chain: a provider that was registered but
 * does not actually convert (e.g. a broken build).  Checks:
 *   1. transform probe - a let/arrow/template-literal snippet MUST be
 *      rewritten (return 1, no leftover "let "/"=>" in the output);
 *   2. compile probe   - the transformed output MUST compile in Duktape;
 *   3. prelude probe   - the prelude hook MUST return source that
 *      installs padStart and Map.
 * Returns 1 when every check passes, 0 otherwise.  `report` (optional)
 * receives a compact "transform=... compile=... prelude=..." line. */
int cwv_backend_esm_selftest(CwvBackend *backend, char *report, size_t cap);

/* Light/dark theme: re-propagates default text colors ONLY when the
 * theme really changes - a no-op call is guaranteed side-effect free. */
int cwv_backend_set_theme(CwvBackend *backend, int dark);

/* Zoom / focus / scrollbar controls (LINT 2026-10-03: these used to be
 * declared as function-local `extern` lines at every cwv_host_* call
 * site - now declared once here like every other backend entry). */
int   cwv_backend_set_zoom(CwvBackend *backend, float zoom);
float cwv_backend_get_zoom(CwvBackend *backend);
int   cwv_backend_clear_focus(CwvBackend *backend);
int   cwv_backend_set_scrollbars(CwvBackend *backend, int visible);


#ifdef __cplusplus
}
#endif

#endif
