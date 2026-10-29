/* ============================================================================
 * FILE ORIENTATION: the JavaScript half of the web backend (Duktape 2.7).
 * Owns the JS context, the console ring, the DOM bindings the scripts see
 * (getElementById, addEventListener, textContent, style.cssText...), timers
 * and the fetch trampoline.  Parsing/layout live in cwv_html.c, styles
 * in cwv_css.c, painting in cwv_backend.c.
 * ============================================================================ */

/* M_PI is a non-standard extension and is absent in some VitaSDK C builds. */
#ifndef CWV_PI
#define CWV_PI 3.14159265358979323846
#endif

/*
 * ============================================================================
 *  Vita WebView - the JavaScript engine (cwv_js.c)
 * ============================================================================
 *
 *  This file gives the native renderer JavaScript support, using the same
 *  "registry table" pattern as the CSS and HTML sides:
 *
 *      CSS_PROPERTIES[]   (cwv_css.c)   one row per CSS property
 *      TAG_TABLE[]        (cwv_html.c)  one row per HTML tag
 *      JS_GLOBAL_METHODS[], JS_CONSOLE_METHODS[], JS_DOCUMENT_METHODS[],
 *      JS_ELEMENT_METHODS[]                  one row per JS function  < HERE
 *
 *  The interpreter itself is Duktape (https://duktape.org, MIT license), a
 *  tiny embeddable ES5.1 engine that compiles anywhere C99 does - including
 *  the Vita. Its whole implementation is vendored in src/backend/js/
 *  (duktape.c + two headers) and needs no other dependencies.
 *
 *  HOW A PAGE'S SCRIPTS RUN
 *  ------------------------
 *   1. backend_load() parses the HTML as usual (script tags are skipped by
 *      the parser), then calls js_harvest_and_run():
 *        - scans r->html for <script>...</script> blocks and copies them
 *          (external <script src=...> files are loaded and run too - the
 *          same resource loader as images/stylesheets)
 *   1b. every source (inline, external, eval) passes through the registered
 *       ES6->ES5 transform hook when one is installed (built-in provider)
 *        - if (and only if) at least one block exists, creates the Duktape
 *          context and registers the API below
 *        - evaluates each block; errors go to console.error, not a crash
 *   2. backend_update() calls js_tick() every frame, which runs due
 *      setTimeout/setInterval callbacks.
 *   3. Activating a button/link that has a JS "activate" listener calls the
 *      listener instead of / in addition to the default behavior (see
 *      renderer_focus_activate and backend_pointer_event in
 *      cwv_backend.c).
 *
 *  Pages WITHOUT scripts never allocate the JS context (r->js stays NULL) -
 *  every entry point here starts with a cheap NULL check, so script-free
 *  pages behave byte-identically to the pre-JS renderer.
 *
 *  SUPPORTED API (ES5.1 + this object model)
 *  -----------------------------------------
 *    console.log/info/warn/error(...)     ring buffer, dump via
 *                                          js_console_dump() (test harness)
 *    document.getElementById("id")        -> element or null
 *    document.querySelector("sel")         -> element or null
 *    document.querySelectorAll("sel")     -> array of elements
 *          (selectors: "tag", "#id", ".class", "tag.class", "tag#id.class",
 *           comma lists - exactly what the CSS engine supports)
 *    document.location.assign("page.html")  navigate
 *    el.textContent = t                   read/write text content
 *    el.value = v                         same, for <input>/<textarea>/<select>
 *    el.style.cssText = "color: red; ..."  SAME table as CSS - any property
 *                                          the CSS registry supports
 *    el.className = "cls"                set class + re-apply CSS rules
 *    el.hidden = true / false             display:none toggle
 *    el.addEventListener("click", fn)   runs when button/link activated
 *    setTimeout(fn, ms) / clearTimeout(id)
 *    setInterval(fn, ms) / clearInterval(id)
 *    window.innerWidth / window.innerHeight
 *
 *  >>> TO ADD A NEW JS FUNCTION <<<
 *      1. Write it:  static duk_ret_t js_my_thing(duk_context *ctx) { ... }
 *         (get arguments with duk_to_string/duk_get_int etc., return the
 *          number of JS return values pushed - usually 0 or 1)
 *      2. Add one line to the right table below:
 *             { "myThing", js_my_thing },
 * ============================================================================
 */

#include "core/cwv_renderer.h"
#include "core/cwv_platform.h"
#include "web/js/runtime/duktape/duktape.h"

/* URL resolution lives in cwv_backend.c; scripts hand us relative refs
 * exactly like they hand them to a browser. */
extern void resolve_url(const char *base, const char *ref, char *out, size_t cap);

#include <time.h>

/* ---------------- limits (fixed-size, like the rest of the renderer) --- */
#define VW_MAX_SCRIPTS     16    /* inline <script> blocks per page */
#define VW_MAX_CONSOLE_LINES 64    /* console ring buffer lines */
#define VW_CONSOLE_LINE   256
#define VW_MAX_TIMERS      16
#define VW_MAX_LISTENERS   64

/* The renderer-side half of the JS runtime.  The other half (functions,
 * timers, listeners) lives inside the Duktape heap, referenced by string
 * keys we keep here. */
struct CwvJs {
    duk_context *ctx;

    /* inline scripts harvested from the page, in source order */
    char *scripts[VW_MAX_SCRIPTS];
    int script_count;

    /* console ring buffer (oldest line falls off the end) */
    char console[VW_MAX_CONSOLE_LINES][VW_CONSOLE_LINE];
    int console_count;
    /* DevTools-style repeat counts: a line identical to the one before it
     * bumps its counter instead of taking a new slot, so a page that spams
     * the same error every frame does not flush the ring. 255 = capped. */
    unsigned char console_repeats[VW_MAX_CONSOLE_LINES];
    int console_errors, console_warns;

    /* timers: setTimeout/setInterval. fn_key is the property name under
     * which the callback function is stored in the JS global "__vw_timers"
     * object (C never touches JS object lifetimes that way). */
    struct {
        int active;
        int is_interval;
        unsigned long long next_ms;   /* when to fire (ms) */
        unsigned long long period;    /* setInterval period, else 0 */
        char fn_key[16];
    } timers[VW_MAX_TIMERS];

    /* "activate" listeners: node index -> callback, stored the same way
     * under the JS global "__vw_events". */
    struct {
        int node_index;
        char fn_key[16];
    } listeners[VW_MAX_LISTENERS];
    int listener_count;
};

/* Milliseconds since boot.  clock() is portable C and works on the Vita;
 * scripts only need "did the timer expire yet", not wall-clock accuracy. */
static unsigned long long js_now_ms(void) {
    return (unsigned long long)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
}

/* The current renderer: stashed inside the Duktape context so every native
 * function can find it without globals. */
static void js_stash_renderer(duk_context *ctx, CwvRenderer *r) {
    duk_push_global_stash(ctx);
    duk_push_pointer(ctx, (void *)r);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("renderer"));
    duk_pop(ctx);
}

static CwvRenderer *js_get_renderer(duk_context *ctx) {
    CwvRenderer *r = NULL;
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("renderer"));
    r = (CwvRenderer *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);
    return r;
}

/* --------------------------------------------------------------------------
 * console ring buffer
 * ------------------------------------------------------------------------ */

/* Append one line to the console buffer.  (duk_... arguments are stringified
 * by the console methods themselves before calling this.) */
static void js_console_add(CwvJs *js, const char *level, const char *msg) {
    int slot;
    char line[VW_CONSOLE_LINE];
    if (!js) return;
    /* build the full line first so repeat detection compares identically
     * formatted text (level prefix included) */
    snprintf(line, VW_CONSOLE_LINE, "%.8s: %.240s", level, msg);
    /* same line twice in a row -> count it, don't store it again */
    if (js->console_count > 0) {
        int last = js->console_count - 1;
        if (js->console_count <= VW_MAX_CONSOLE_LINES &&
            strcmp(js->console[last], line) == 0) {
            if (js->console_repeats[last] < 255) js->console_repeats[last]++;
            return;
        }
    }
    if (level[0] == 'e' && !strncmp(level, "error", 5)) js->console_errors++;
    else if (level[0] == 'w' && !strncmp(level, "warn", 4)) js->console_warns++;
    slot = js->console_count;
    if (slot >= VW_MAX_CONSOLE_LINES) {
        /* buffer full: drop the oldest line (a simple ring) */
        memmove(js->console[0], js->console[1],
                (VW_MAX_CONSOLE_LINES - 1) * VW_CONSOLE_LINE);
        memmove(js->console_repeats, js->console_repeats + 1,
                (VW_MAX_CONSOLE_LINES - 1));
        js->console_repeats[VW_MAX_CONSOLE_LINES - 1] = 0;
        slot = VW_MAX_CONSOLE_LINES - 1;
    }
    /* "%.240s" caps the message so the line can never exceed 256 bytes. */
    snprintf(js->console[slot], VW_CONSOLE_LINE, "%.8s: %.240s", level, msg);
    js->console_repeats[slot] = 1;
    js->console_count = slot + 1;
}

/* Public: add a line from the C side (image loader diagnostics). */
void js_console_add_line(CwvRenderer *r, const char *level, const char *msg) {
    if (!r || !r->js || !level || !msg) return;
    js_console_add(r->js, level, msg);
}

/* Format the whole console into `out` (for the test harness / debugging). */
int js_console_dump(const CwvRenderer *r, char *out, size_t cap) {
    int i, n = 0;
    if (!r || !r->js) { snprintf(out, cap, "(no js context)\n"); return 0; }
    for (i = 0; i < r->js->console_count; i++) {
        n += snprintf(out + n, cap > (size_t)n ? cap - n : 0, "%s\n",
                      r->js->console[i]);
        if (r->js->console_repeats[i] > 1)
            n += snprintf(out + n, cap > (size_t)n ? cap - n : 0,
                          "  [x%u]\n", (unsigned)r->js->console_repeats[i]);
    }
    if (n == 0) snprintf(out, cap, "(console empty)\n");
    return r->js->console_count;
}

/* Totals for the debug overlay: how many error/warn lines and how many
 * ring entries (including repeats collapsed into their first occurrence). */
void js_console_stats(const CwvRenderer *r, int *errors, int *warns,
                      int *lines) {
    if (!r || !r->js) {
        if (errors) *errors = 0;
        if (warns) *warns = 0;
        if (lines) *lines = 0;
        return;
    }
    if (errors) *errors = r->js->console_errors;
    if (warns) *warns = r->js->console_warns;
    if (lines) *lines = r->js->console_count;
}

/* The most recent "error: ..." console line ("" when none) - for the
 * DevTools console's "Last error" row.  The pointer is valid until the next
 * console write; copy it if you keep it. */
const char *js_console_last_error(const CwvRenderer *r) {
    int i;
    if (!r || !r->js) return "";
    for (i = r->js->console_count - 1; i >= 0; i--)
        if (!strncmp(r->js->console[i], "error:", 6)) return r->js->console[i];
    return "";
}

/* Common console.* implementation: stringify every argument, space-joined. */
static duk_ret_t js_console_generic(duk_context *ctx, const char *level) {
    CwvRenderer *r = js_get_renderer(ctx);
    char line[VW_CONSOLE_LINE];
    int nargs = duk_get_top(ctx), i, n = 0;
    for (i = 0; i < nargs; i++) {
        const char *s = duk_safe_to_string(ctx, i);
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s",
                      i ? " " : "", s);
        if ((size_t)n >= sizeof(line)) break;
    }
    if (r && r->js) js_console_add(r->js, level, line);
    return 0;
}

static duk_ret_t js_console_log(duk_context *ctx)   { return js_console_generic(ctx, "log"); }
static duk_ret_t js_console_info(duk_context *ctx)  { return js_console_generic(ctx, "info"); }
static duk_ret_t js_console_warn(duk_context *ctx)  { return js_console_generic(ctx, "warn"); }
static duk_ret_t js_console_error(duk_context *ctx) { return js_console_generic(ctx, "error"); }

/* --------------------------------------------------------------------------
 * element objects
 * ------------------------------------------------------------------------ */

/* Wrap a node index as a JS element object.  The object is a plain JS object
 * carrying a hidden "idx" property; its prototype (built once at init from
 * JS_ELEMENT_METHODS[]) provides all the element methods, so elements are
 * cheap to create and garbage-collect like any other JS object. */
static void js_push_element(duk_context *ctx, int node_index) {
    if (node_index < 0) {
        duk_push_null(ctx);
        return;
    }
    /* Build the wrapper FIRST so it sits below the prototype on the stack.
       duk_set_prototype(idx) takes the prototype from the STACK TOP and
       applies it to the value at `idx`, popping the prototype - so the
       stack must be [wrapper, proto] here, never [proto, wrapper].
       (The old order handed the shared element_proto object to JS: every
       getElementById returned the same object, and its hidden "idx" was
       read through the prototype chain from whichever wrapper happened
       to be created last - elements "worked" only when they were the
       last lookup.) */
    duk_push_object(ctx);                 /* -> [obj] */
    duk_push_int(ctx, node_index);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("idx"));  /* obj.idx */
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("element_proto"));
    duk_remove(ctx, -2);                 /* stash obj no longer needed */
    duk_set_prototype(ctx, -2);           /* obj.__proto__ = element_proto
                                             (pops proto; -> [obj]) */
}

/* The node index hidden inside an element object, or -1. */
static int js_this_element(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("idx"));
    int idx = duk_get_int_default(ctx, -1, -1);
    duk_pop_2(ctx);
    return idx;
}

/* From a box node (an element), find the text-run node that visually owns its
 * content: the first following non-box node that has text, before the next
 * element box starts.  The flat node list has no parent pointers, so this is
 * a heuristic - but the parser emits children contiguously after the box,
 * which makes it reliable in practice.  Returns the element's own index when
 * no text child exists (text can then be painted on the box itself). */
static int js_text_owner(CwvRenderer *r, int node_index) {
    int i;
    if (node_index < 0 || node_index >= r->node_count) return -1;
    if (!r->nodes[node_index].is_box) return node_index;
    for (i = node_index + 1; i < r->node_count; i++) {
        CwvNode *n = &r->nodes[i];
        if (n->is_box) break;             /* a new element starts: stop */
        if (n->is_option) continue;       /* option labels are picker data */
        if (n->text[0]) return i;
    }
    return node_index;
}

/* setText / getText / setValue / getValue --------------------------------- */

static duk_ret_t js_el_set_text(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    const char *text = duk_to_string(ctx, 0);
    int owner;
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    owner = js_text_owner(r, idx);
    if (owner < 0) owner = idx;
    snprintf(r->nodes[owner].text, sizeof(r->nodes[owner].text), "%s", text);
    return 0;
}

static duk_ret_t js_el_get_text(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx), owner;
    if (!r || idx < 0 || idx >= r->node_count) { duk_push_string(ctx, ""); return 1; }
    /* Chrome's textContent/value reads the element's text CONTENT, which
       lives in the child text node for box elements - the same owner the
       setter writes (js_el_set_text).  Reading nodes[idx].text directly
       returned "" for every <div>/<button>/<p>. */
    owner = js_text_owner(r, idx);
    if (owner < 0) owner = idx;
    duk_push_string(ctx, r->nodes[owner].text);
    return 1;
}

/* Same as setText/getText, named for form controls. */
static duk_ret_t js_el_set_value(duk_context *ctx) { return js_el_set_text(ctx); }
static duk_ret_t js_el_get_value(duk_context *ctx) { return js_el_get_text(ctx); }

/* setClass: change the class attribute and re-match the page's CSS rules. */
static duk_ret_t js_prop_class_get(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (!r || idx < 0 || idx >= r->node_count) { duk_push_string(ctx, ""); return 1; }
    duk_push_string(ctx, r->nodes[idx].cls);
    return 1;
}

static duk_ret_t js_prop_class_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    const char *cls = duk_to_string(ctx, 0);
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    snprintf(r->nodes[idx].cls, sizeof(r->nodes[idx].cls), "%s", cls);
    apply_rules(r, &r->nodes[idx], r->nodes[idx].id, r->nodes[idx].cls,
                r->nodes[idx].vcm_active);
    return 0;
}

/* addEventListener("click", fn): the standard activation listener.
 * Returns true when the listener was registered (false: wrong event name
 * or table full). */
typedef struct { const char *name; duk_c_function fn; } JsFn;
static void js_register(duk_context *ctx, const JsFn *table, size_t count);

/* --------------------------------------------------------------------------
 * Experimental Canvas 2D subset. The backing store is deliberately opt-in
 * and bounded; no fake context is returned when the package did not request
 * beta-enable-canvas. Rasterization is clipped to the backing store, paths are
 * capped, and curves/arcs are flattened into line segments for the first pass.
 * ------------------------------------------------------------------------ */
static int js_canvas_context_index(duk_context *ctx) {
    int idx;
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("canvas_idx"));
    idx = duk_get_int_default(ctx, -1, -1);
    duk_pop_2(ctx);
    return idx;
}

static int js_canvas_ensure(CwvNode *n) {
    size_t count;
    if (!n || !n->is_canvas || n->canvas_width <= 0 || n->canvas_height <= 0) return 0;
    if (n->canvas_pixels) return 1;
    count = (size_t)n->canvas_width * (size_t)n->canvas_height;
    if (count > (size_t)1024 * 768 || count > ((size_t)-1) / 4u) return 0;
    n->canvas_pixels = (unsigned char *)calloc(count, 4u);
    if (!n->canvas_pixels) return 0;
    n->canvas_dirty = 1;
    return 1;
}

/* Convert a Canvas rectangle to a clipped integer backing-store region.
 * Validate before float-to-int conversion: script values can be NaN, infinity,
 * or finite magnitudes outside the range of int. */
static int js_canvas_rect_bounds(CwvNode *n, double dx, double dy,
                                double dw, double dh,
                                int *x0, int *y0, int *x1, int *y1) {
    double ex, ey, ax, ay, bx, by;
    if (!n || !x0 || !y0 || !x1 || !y1 ||
        !isfinite(dx) || !isfinite(dy) || !isfinite(dw) || !isfinite(dh) ||
        dw <= 0.0 || dh <= 0.0) return 0;
    ex = dx + dw;
    ey = dy + dh;
    if (dx >= n->canvas_width || dy >= n->canvas_height ||
        ex <= 0.0 || ey <= 0.0) return 0;
    ax = floor(dx); ay = floor(dy);
    bx = ceil(ex); by = ceil(ey);
    if (ax < 0.0) ax = 0.0;
    if (ay < 0.0) ay = 0.0;
    if (bx > n->canvas_width) bx = n->canvas_width;
    if (by > n->canvas_height) by = n->canvas_height;
    if (ax >= bx || ay >= by) return 0;
    /* All values are now bounded by the canvas dimensions. */
    *x0 = (int)ax; *y0 = (int)ay; *x1 = (int)bx; *y1 = (int)by;
    return 1;
}

static void js_canvas_paint_bounds(CwvNode *n, int x0, int y0, int x1, int y1,
                                   unsigned color, double alpha) {
    int x, y;
    unsigned sr, sg, sb;
    double sa;
    if (!n || !n->canvas_pixels || x0 >= x1 || y0 >= y1) return;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    sr = color & 255u;
    sg = (color >> 8) & 255u;
    sb = (color >> 16) & 255u;
    sa = alpha * ((color >> 24) & 255u) / 255.0;

    /* The common opaque case avoids per-pixel floating-point compositing.
       This matters for animated canvases that repaint large solid regions. */
    if (sa >= 1.0) {
        for (y = y0; y < y1; y++) {
            unsigned char *p = n->canvas_pixels +
                ((size_t)y * n->canvas_width + x0) * 4u;
            for (x = x0; x < x1; x++, p += 4) {
                p[0] = (unsigned char)sr;
                p[1] = (unsigned char)sg;
                p[2] = (unsigned char)sb;
                p[3] = 255;
            }
        }
        return;
    }
    for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
        unsigned char *p = n->canvas_pixels + ((size_t)y * n->canvas_width + x) * 4u;
        double da = p[3] / 255.0, oa = sa + da * (1.0 - sa);
        if (oa > 0.0) {
            p[0] = (unsigned char)((sr * sa + p[0] * da * (1.0 - sa)) / oa + 0.5);
            p[1] = (unsigned char)((sg * sa + p[1] * da * (1.0 - sa)) / oa + 0.5);
            p[2] = (unsigned char)((sb * sa + p[2] * da * (1.0 - sa)) / oa + 0.5);
            p[3] = (unsigned char)(oa * 255.0 + 0.5);
        }
    }
}

static double js_canvas_context_alpha(duk_context *ctx) {
    double alpha = 1.0;
    duk_push_this(ctx); duk_get_prop_string(ctx, -1, "globalAlpha");
    if (duk_is_number(ctx, -1)) alpha = duk_get_number(ctx, -1);
    duk_pop_2(ctx);
    if (!isfinite(alpha)) alpha = 1.0;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    return alpha;
}

static unsigned js_canvas_context_color(duk_context *ctx, const char *property) {
    const char *style;
    unsigned color;
    duk_push_this(ctx); duk_get_prop_string(ctx, -1, property);
    style = duk_safe_to_string(ctx, -1);
    color = parse_color(style, rgba(0, 0, 0, 255));
    duk_pop_2(ctx);
    return color;
}


#define CWV_CANVAS_PATH_MAX 1024

static int js_canvas_path_append(CwvNode *n, double x, double y, int move) {
    CwvCanvasPathPoint *next;
    int cap;
    if (!n || !isfinite(x) || !isfinite(y) || fabs(x) > 1.0e9 || fabs(y) > 1.0e9)
        return 0;
    if (n->canvas_path_count >= CWV_CANVAS_PATH_MAX) return 0;
    if (n->canvas_path_count >= n->canvas_path_capacity) {
        cap = n->canvas_path_capacity ? n->canvas_path_capacity * 2 : 32;
        if (cap > CWV_CANVAS_PATH_MAX) cap = CWV_CANVAS_PATH_MAX;
        next = (CwvCanvasPathPoint *)realloc(n->canvas_path, (size_t)cap * sizeof(*next));
        if (!next) return 0;
        n->canvas_path = next;
        n->canvas_path_capacity = cap;
    }
    n->canvas_path[n->canvas_path_count].x = x;
    n->canvas_path[n->canvas_path_count].y = y;
    n->canvas_path[n->canvas_path_count].move = move;
    n->canvas_path_count++;
    n->canvas_path_x = x; n->canvas_path_y = y;
    if (move) {
        n->canvas_path_start_x = x; n->canvas_path_start_y = y;
        n->canvas_path_has_current = 1;
    }
    return 1;
}

static CwvNode *js_canvas_node(duk_context *ctx, CwvRenderer **renderer) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_canvas_context_index(ctx);
    if (renderer) *renderer = r;
    if (!r || idx < 0 || idx >= r->node_count || !r->nodes[idx].is_canvas ||
        !cwv_canvas_enabled() || !js_canvas_ensure(&r->nodes[idx])) return NULL;
    return &r->nodes[idx];
}

static duk_ret_t js_canvas_begin_path(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    if (n) { n->canvas_path_count = 0; n->canvas_path_has_current = 0; }
    return 0;
}

static duk_ret_t js_canvas_move_to(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double x = duk_to_number(ctx, 0), y = duk_to_number(ctx, 1);
    if (n) (void)js_canvas_path_append(n, x, y, 1);
    return 0;
}

static duk_ret_t js_canvas_line_to(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double x = duk_to_number(ctx, 0), y = duk_to_number(ctx, 1);
    if (!n) return 0;
    if (!n->canvas_path_has_current) (void)js_canvas_path_append(n, x, y, 1);
    else (void)js_canvas_path_append(n, x, y, 0);
    return 0;
}

static duk_ret_t js_canvas_close_path(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    if (n && n->canvas_path_has_current)
        (void)js_canvas_path_append(n, n->canvas_path_start_x, n->canvas_path_start_y, 0);
    return 0;
}

/* Add a closed rectangle subpath. Negative dimensions reverse the direction,
 * matching the Canvas 2D rect() contract without allocating a separate path. */
static duk_ret_t js_canvas_rect(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double x = duk_to_number(ctx, 0), y = duk_to_number(ctx, 1);
    double w = duk_to_number(ctx, 2), h = duk_to_number(ctx, 3);
    double x2, y2;
    if (!n || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) ||
        fabs(x) > 1.0e9 || fabs(y) > 1.0e9 ||
        fabs(w) > 1.0e9 || fabs(h) > 1.0e9) return 0;
    x2 = x + w; y2 = y + h;
    if (!isfinite(x2) || !isfinite(y2) || fabs(x2) > 1.0e9 || fabs(y2) > 1.0e9)
        return 0;
    (void)js_canvas_path_append(n, x, y, 1);
    (void)js_canvas_path_append(n, x2, y, 0);
    (void)js_canvas_path_append(n, x2, y2, 0);
    (void)js_canvas_path_append(n, x, y2, 0);
    (void)js_canvas_path_append(n, x, y, 0);
    return 0;
}

/* Save/restore covers the drawing-state properties implemented by CWV. It is
 * intentionally not advertised as full browser state restoration: transforms,
 * clipping regions, and compositing modes do not exist in this subset yet. */
static duk_ret_t js_canvas_save(duk_context *ctx) {
    duk_uarridx_t index;
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("canvas_state_stack"));
    if (!duk_is_array(ctx, -1)) {
        duk_pop(ctx);
        duk_push_array(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("canvas_state_stack"));
    }
    index = (duk_uarridx_t)duk_get_length(ctx, -1);
    duk_push_object(ctx);
    duk_get_prop_string(ctx, -3, "fillStyle"); duk_put_prop_string(ctx, -2, "fillStyle");
    duk_get_prop_string(ctx, -3, "strokeStyle"); duk_put_prop_string(ctx, -2, "strokeStyle");
    duk_get_prop_string(ctx, -3, "lineWidth"); duk_put_prop_string(ctx, -2, "lineWidth");
    duk_get_prop_string(ctx, -3, "globalAlpha"); duk_put_prop_string(ctx, -2, "globalAlpha");
    duk_put_prop_index(ctx, -2, index);
    duk_pop_2(ctx);
    return 0;
}

static duk_ret_t js_canvas_restore(duk_context *ctx) {
    duk_uarridx_t index;
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("canvas_state_stack"));
    if (!duk_is_array(ctx, -1)) { duk_pop_2(ctx); return 0; }
    if (duk_get_length(ctx, -1) == 0) { duk_pop_2(ctx); return 0; }
    index = (duk_uarridx_t)(duk_get_length(ctx, -1) - 1);
    duk_get_prop_index(ctx, -1, index);
    if (duk_is_object(ctx, -1)) {
        const char *properties[] = { "fillStyle", "strokeStyle", "lineWidth", "globalAlpha" };
        size_t i;
        for (i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
            duk_get_prop_string(ctx, -1, properties[i]);
            duk_put_prop_string(ctx, -4, properties[i]);
        }
    }
    duk_pop(ctx);
    duk_del_prop_index(ctx, -1, index);
    duk_pop_2(ctx);
    return 0;
}

static duk_ret_t js_canvas_quadratic_curve_to(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double cx = duk_to_number(ctx, 0), cy = duk_to_number(ctx, 1);
    double x = duk_to_number(ctx, 2), y = duk_to_number(ctx, 3);
    double sx, sy, t; int i;
    if (!n || !isfinite(cx) || !isfinite(cy) || !isfinite(x) || !isfinite(y)) return 0;
    if (!n->canvas_path_has_current) { (void)js_canvas_path_append(n, cx, cy, 1); }
    sx = n->canvas_path_x; sy = n->canvas_path_y;
    for (i = 1; i <= 16; i++) {
        double px, py; t = (double)i / 16.0;
        px = (1-t)*(1-t)*sx + 2*(1-t)*t*cx + t*t*x;
        py = (1-t)*(1-t)*sy + 2*(1-t)*t*cy + t*t*y;
        if (!js_canvas_path_append(n, px, py, 0)) break;
    }
    return 0;
}

static duk_ret_t js_canvas_bezier_curve_to(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double c1x=duk_to_number(ctx,0), c1y=duk_to_number(ctx,1);
    double c2x=duk_to_number(ctx,2), c2y=duk_to_number(ctx,3);
    double x=duk_to_number(ctx,4), y=duk_to_number(ctx,5), sx, sy, t;
    int i;
    if (!n || !isfinite(c1x)||!isfinite(c1y)||!isfinite(c2x)||!isfinite(c2y)||!isfinite(x)||!isfinite(y)) return 0;
    if (!n->canvas_path_has_current) (void)js_canvas_path_append(n, c1x, c1y, 1);
    sx=n->canvas_path_x; sy=n->canvas_path_y;
    for (i=1;i<=24;i++) {
        double u,px,py; t=(double)i/24.0; u=1-t;
        px=u*u*u*sx+3*u*u*t*c1x+3*u*t*t*c2x+t*t*t*x;
        py=u*u*u*sy+3*u*u*t*c1y+3*u*t*t*c2y+t*t*t*y;
        if (!js_canvas_path_append(n,px,py,0)) break;
    }
    return 0;
}

static duk_ret_t js_canvas_arc(duk_context *ctx) {
    CwvNode *n = js_canvas_node(ctx, NULL);
    double cx=duk_to_number(ctx,0), cy=duk_to_number(ctx,1), radius=duk_to_number(ctx,2);
    double start=duk_to_number(ctx,3), end=duk_to_number(ctx,4), delta;
    int ccw=duk_to_boolean(ctx,5), steps, i;
    if (!n || !isfinite(cx)||!isfinite(cy)||!isfinite(radius)||!isfinite(start)||!isfinite(end)||radius<0.0||radius>1.0e6) return 0;
    delta=end-start;
    if (!ccw) { while (delta < 0) delta += 2.0*CWV_PI; if (delta > 2.0*CWV_PI) delta=2.0*CWV_PI; }
    else { while (delta > 0) delta -= 2.0*CWV_PI; if (delta < -2.0*CWV_PI) delta=-2.0*CWV_PI; }
    steps=(int)ceil(fabs(delta)*radius/2.0); if (steps<8) steps=8; if (steps>128) steps=128;
    for (i=0;i<=steps;i++) {
        double a=start+delta*(double)i/steps, x=cx+cos(a)*radius, y=cy+sin(a)*radius;
        if (i==0) {
            if (!n->canvas_path_has_current) (void)js_canvas_path_append(n,x,y,1);
            else (void)js_canvas_path_append(n,x,y,0);
        } else if (!js_canvas_path_append(n,x,y,0)) break;
    }
    return 0;
}

static int js_canvas_path_subpath_end(CwvNode *n, int start) {
    int i;
    for (i=start+1;i<n->canvas_path_count;i++) if (n->canvas_path[i].move) return i;
    return n->canvas_path_count;
}

static int js_canvas_clip_line(CwvNode *n, double *ax, double *ay,
                               double *bx, double *by, double pad) {
    double dx = *bx - *ax, dy = *by - *ay, t0 = 0.0, t1 = 1.0;
    double p[4] = { -dx, dx, -dy, dy };
    double q[4] = { *ax + pad, n->canvas_width + pad - *ax,
                    *ay + pad, n->canvas_height + pad - *ay };
    int i;
    for (i = 0; i < 4; i++) {
        if (fabs(p[i]) < 1e-12) { if (q[i] < 0.0) return 0; }
        else {
            double t = q[i] / p[i];
            if (p[i] < 0.0) { if (t > t1) return 0; if (t > t0) t0 = t; }
            else { if (t < t0) return 0; if (t < t1) t1 = t; }
        }
    }
    *bx = *ax + t1 * dx; *by = *ay + t1 * dy;
    *ax = *ax + t0 * dx; *ay = *ay + t0 * dy;
    return 1;
}

static void js_canvas_stroke_line(CwvNode *n, double ax, double ay, double bx, double by,
                                  unsigned color, double alpha, double width) {
    double dx, dy, steps;
    int i, radius=(int)floor((width-1.0)/2.0);
    if (!js_canvas_clip_line(n, &ax, &ay, &bx, &by, width + 1.0)) return;
    dx=bx-ax; dy=by-ay; steps=fmax(fabs(dx),fabs(dy));
    if (steps < 1.0) steps=1.0;
    for (i=0;i<=(int)steps;i++) {
        double t=(double)i/steps, px=ax+dx*t, py=ay+dy*t;
        int x0=(int)floor(px+0.5)-radius, y0=(int)floor(py+0.5)-radius;
        int x1=x0+2*radius+1, y1=y0+2*radius+1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > n->canvas_width) x1 = n->canvas_width;
        if (y1 > n->canvas_height) y1 = n->canvas_height;
        js_canvas_paint_bounds(n, x0, y0, x1, y1, color, alpha);
    }
}

static duk_ret_t js_canvas_stroke(duk_context *ctx) {
    CwvNode *n=js_canvas_node(ctx,NULL); int i, end; double width=1.0;
    unsigned color; double alpha;
    if (!n || n->canvas_path_count<2) return 0;
    duk_push_this(ctx); duk_get_prop_string(ctx,-1,"lineWidth"); if(duk_is_number(ctx,-1))width=duk_get_number(ctx,-1); duk_pop_2(ctx);
    if (!isfinite(width) || width <= 0.0) return 0;
    if (width > 64.0) width = 64.0;
    color=js_canvas_context_color(ctx,"strokeStyle"); alpha=js_canvas_context_alpha(ctx);
    for(i=0;i<n->canvas_path_count;) {
        end=js_canvas_path_subpath_end(n,i);
        for(int j=i+1;j<end;j++) js_canvas_stroke_line(n,n->canvas_path[j-1].x,n->canvas_path[j-1].y,n->canvas_path[j].x,n->canvas_path[j].y,color,alpha,width);
        i=end;
    }
    n->canvas_dirty=1; return 0;
}

static int js_canvas_compare_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return da < db ? -1 : da > db ? 1 : 0;
}

static duk_ret_t js_canvas_fill(duk_context *ctx) {
    CwvNode *n=js_canvas_node(ctx,NULL); int y,i,end,min_y,max_y;
    unsigned color; double alpha;
    double intersections[CWV_CANVAS_PATH_MAX];
    if(!n||n->canvas_path_count<3)return 0;
    /* Limit scan conversion to the path's vertical bounds rather than
       walking every row of the backing store for small/moving shapes. */
    min_y = n->canvas_height;
    max_y = 0;
    for (i = 0; i < n->canvas_path_count; i++) {
        double py = n->canvas_path[i].y;
        int iy0, iy1;
        if (!isfinite(py)) continue;
        iy0 = (int)floor(py);
        iy1 = (int)ceil(py);
        if (iy0 < min_y) min_y = iy0;
        if (iy1 > max_y) max_y = iy1;
    }
    if (min_y < 0) min_y = 0;
    if (max_y > n->canvas_height) max_y = n->canvas_height;
    if (min_y >= max_y) return 0;
    color=js_canvas_context_color(ctx,"fillStyle"); alpha=js_canvas_context_alpha(ctx);
    for(y=min_y;y<max_y;y++) {
        double scan=y+0.5; int count=0;
        for(i=0;i<n->canvas_path_count;) {
            end=js_canvas_path_subpath_end(n,i);
            if(end-i>=3) {
                int j;
                for(j=i;j<end;j++) {
                    int k=(j+1<end)?j+1:i;
                    double x1=n->canvas_path[j].x,y1=n->canvas_path[j].y,x2=n->canvas_path[k].x,y2=n->canvas_path[k].y;
                    if((y1<=scan&&y2>scan)||(y2<=scan&&y1>scan)) {
                        double x=x1+(scan-y1)*(x2-x1)/(y2-y1);
                        if(count<CWV_CANVAS_PATH_MAX)intersections[count++]=x;
                    }
                }
            }
            i=end;
        }
        if (count < 2) continue;
        qsort(intersections, (size_t)count, sizeof(intersections[0]), js_canvas_compare_double);
        for(i=0;i+1<count;i+=2) {
            int x0=(int)floor(intersections[i]),x1=(int)ceil(intersections[i+1]);
            if (x0 < 0) x0 = 0;
            if (x1 > n->canvas_width) x1 = n->canvas_width;
            if (x0 < x1) js_canvas_paint_bounds(n, x0, y, x1, y + 1, color, alpha);
        }
    }
    n->canvas_dirty=1; return 0;
}

static duk_ret_t js_canvas_fill_rect(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_canvas_context_index(ctx);
    int x0, y0, x1, y1;
    double dx, dy, dw, dh;
    CwvNode *n;
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    n = &r->nodes[idx];
    if (!cwv_canvas_enabled() || !js_canvas_ensure(n)) return 0;
    dx = duk_to_number(ctx, 0); dy = duk_to_number(ctx, 1);
    dw = duk_to_number(ctx, 2); dh = duk_to_number(ctx, 3);
    if (!js_canvas_rect_bounds(n, dx, dy, dw, dh, &x0, &y0, &x1, &y1)) return 0;
    js_canvas_paint_bounds(n, x0, y0, x1, y1,
                           js_canvas_context_color(ctx, "fillStyle"),
                           js_canvas_context_alpha(ctx));
    n->canvas_dirty = 1;
    return 0;
}

/* strokeRect is intentionally axis-aligned and rasterized as four clipped
 * strips. The line width is bounded to avoid pathological work on consoles. */
static duk_ret_t js_canvas_stroke_rect(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_canvas_context_index(ctx), x0, y0, x1, y1, lw;
    int tx0, ty0, tx1, ty1;
    double dx, dy, dw, dh, line_width = 1.0;
    unsigned color;
    double alpha;
    CwvNode *n;
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    n = &r->nodes[idx];
    if (!cwv_canvas_enabled() || !js_canvas_ensure(n)) return 0;
    dx = duk_to_number(ctx, 0); dy = duk_to_number(ctx, 1);
    dw = duk_to_number(ctx, 2); dh = duk_to_number(ctx, 3);
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dw) || !isfinite(dh) ||
        dw == 0.0 || dh == 0.0) return 0;
    /* Negative width/height reverse the rectangle direction in Canvas. */
    if (dw < 0.0) { dx += dw; dw = -dw; }
    if (dh < 0.0) { dy += dh; dh = -dh; }
    duk_push_this(ctx); duk_get_prop_string(ctx, -1, "lineWidth");
    if (duk_is_number(ctx, -1)) line_width = duk_get_number(ctx, -1);
    duk_pop_2(ctx);
    if (!isfinite(line_width) || line_width <= 0.0) return 0;
    if (line_width > 64.0) line_width = 64.0;
    lw = (int)ceil(line_width);
    if (lw < 1) lw = 1;
    if (!js_canvas_rect_bounds(n, dx - line_width / 2.0,
                               dy - line_width / 2.0,
                               dw + line_width, dh + line_width,
                               &x0, &y0, &x1, &y1)) return 0;
    color = js_canvas_context_color(ctx, "strokeStyle");
    alpha = js_canvas_context_alpha(ctx);
    /* Top and bottom edges. */
    if (js_canvas_rect_bounds(n, dx - line_width / 2.0,
                              dy - line_width / 2.0,
                              dw + line_width, (double)lw,
                              &tx0, &ty0, &tx1, &ty1))
        js_canvas_paint_bounds(n, tx0, ty0, tx1, ty1, color, alpha);
    if (js_canvas_rect_bounds(n, dx - line_width / 2.0,
                              dy + dh - line_width / 2.0,
                              dw + line_width, (double)lw,
                              &tx0, &ty0, &tx1, &ty1))
        js_canvas_paint_bounds(n, tx0, ty0, tx1, ty1, color, alpha);
    /* Side edges do not include the already-painted corners. */
    if (dh > 0.0 && js_canvas_rect_bounds(n, dx - line_width / 2.0,
                                         dy + line_width / 2.0,
                                         (double)lw, dh - line_width,
                                         &tx0, &ty0, &tx1, &ty1))
        js_canvas_paint_bounds(n, tx0, ty0, tx1, ty1, color, alpha);
    if (dh > 0.0 && js_canvas_rect_bounds(n, dx + dw - line_width / 2.0,
                                         dy + line_width / 2.0,
                                         (double)lw, dh - line_width,
                                         &tx0, &ty0, &tx1, &ty1))
        js_canvas_paint_bounds(n, tx0, ty0, tx1, ty1, color, alpha);
    n->canvas_dirty = 1;
    (void)x0; (void)y0; (void)x1; (void)y1;
    return 0;
}

static duk_ret_t js_canvas_clear_rect(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_canvas_context_index(ctx), y;
    int x0, y0, x1, y1;
    double dx, dy, dw, dh;
    CwvNode *n;
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    n = &r->nodes[idx];
    if (!cwv_canvas_enabled() || !js_canvas_ensure(n)) return 0;
    dx = duk_to_number(ctx, 0); dy = duk_to_number(ctx, 1);
    dw = duk_to_number(ctx, 2); dh = duk_to_number(ctx, 3);
    if (!js_canvas_rect_bounds(n, dx, dy, dw, dh, &x0, &y0, &x1, &y1)) return 0;
    {
        for (y = y0; y < y1; y++)
            memset(n->canvas_pixels + ((size_t)y * n->canvas_width + x0) * 4u,
                   0, (size_t)(x1 - x0) * 4u);
    }
    n->canvas_dirty = 1;
    return 0;
}

static const JsFn JS_CANVAS_METHODS[] = {
    { "beginPath", js_canvas_begin_path },
    { "moveTo", js_canvas_move_to },
    { "lineTo", js_canvas_line_to },
    { "closePath", js_canvas_close_path },
    { "rect", js_canvas_rect },
    { "save", js_canvas_save },
    { "restore", js_canvas_restore },
    { "quadraticCurveTo", js_canvas_quadratic_curve_to },
    { "bezierCurveTo", js_canvas_bezier_curve_to },
    { "arc", js_canvas_arc },
    { "fill", js_canvas_fill },
    { "stroke", js_canvas_stroke },
    { "fillRect", js_canvas_fill_rect },
    { "strokeRect", js_canvas_stroke_rect },
    { "clearRect", js_canvas_clear_rect }
};

static duk_ret_t js_prop_canvas_width_get(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (!r || idx < 0 || idx >= r->node_count || !r->nodes[idx].is_canvas) {
        duk_push_undefined(ctx); return 1;
    }
    duk_push_int(ctx, r->nodes[idx].canvas_width);
    return 1;
}

static duk_ret_t js_prop_canvas_height_get(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (!r || idx < 0 || idx >= r->node_count || !r->nodes[idx].is_canvas) {
        duk_push_undefined(ctx); return 1;
    }
    duk_push_int(ctx, r->nodes[idx].canvas_height);
    return 1;
}

static void js_canvas_resize(duk_context *ctx, CwvNode *n, int node_index,
                             int width, int height) {
    unsigned char *pixels;
    size_t count;
    char stash_key[40];
    if (!n || !n->is_canvas) return;
    if (width < 1) width = 1;
    if (width > 1024) width = 1024;
    if (height < 1) height = 1;
    if (height > 768) height = 768;
    /* Assigning either HTML canvas dimension resets the bitmap and context,
     * even when the assigned number equals the current value. */
    if (n->canvas_texture) { cwv_tex_free(n->canvas_texture); n->canvas_texture = NULL; }
    free(n->canvas_pixels); n->canvas_pixels = NULL;
    n->canvas_width = width; n->canvas_height = height;
    n->w = (float)width; n->h = (float)height;
    n->canvas_dirty = 1;
    n->canvas_path_count = 0; n->canvas_path_has_current = 0;
    count = (size_t)width * (size_t)height;
    pixels = (unsigned char *)calloc(count, 4u);
    if (pixels) n->canvas_pixels = pixels;
    if (ctx && node_index >= 0) {
        snprintf(stash_key, sizeof(stash_key), "cwv.canvas.context.%d", node_index);
        duk_push_global_stash(ctx);
        duk_get_prop_string(ctx, -1, stash_key);
        if (duk_is_object(ctx, -1)) {
            duk_push_string(ctx, "#000000");
            duk_put_prop_string(ctx, -2, "fillStyle");
            duk_push_number(ctx, 1.0);
            duk_put_prop_string(ctx, -2, "globalAlpha");
            duk_push_string(ctx, "#000000");
            duk_put_prop_string(ctx, -2, "strokeStyle");
            duk_push_number(ctx, 1.0);
            duk_put_prop_string(ctx, -2, "lineWidth");
            duk_push_array(ctx);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("canvas_state_stack"));
        }
        duk_pop_2(ctx);
    }
}

static duk_ret_t js_prop_canvas_width_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (r && idx >= 0 && idx < r->node_count && r->nodes[idx].is_canvas)
        js_canvas_resize(ctx, &r->nodes[idx], idx,
                         duk_to_int(ctx, 0), r->nodes[idx].canvas_height);
    return 0;
}

static duk_ret_t js_prop_canvas_height_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (r && idx >= 0 && idx < r->node_count && r->nodes[idx].is_canvas)
        js_canvas_resize(ctx, &r->nodes[idx], idx,
                         r->nodes[idx].canvas_width, duk_to_int(ctx, 0));
    return 0;
}

static duk_ret_t js_el_get_context(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    char stash_key[40];
    const char *kind = duk_safe_to_string(ctx, 0);
    if (!r || idx < 0 || idx >= r->node_count || !r->nodes[idx].is_canvas ||
        !cwv_canvas_enabled() || !kind || strcmp(kind, "2d") != 0 ||
        !js_canvas_ensure(&r->nodes[idx])) {
        duk_push_null(ctx); return 1;
    }

    /* HTMLCanvasElement.getContext("2d") returns the same context object for
     * the lifetime of a canvas. Keep it rooted in the global stash so repeated
     * getContext calls preserve fillStyle/globalAlpha and object identity. */
    snprintf(stash_key, sizeof(stash_key), "cwv.canvas.context.%d", idx);
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, stash_key);
    if (duk_is_object(ctx, -1)) {
        duk_remove(ctx, -2); /* discard stash, return cached context */
        return 1;
    }
    duk_pop_2(ctx); /* no cached context */

    duk_push_object(ctx);
    duk_push_int(ctx, idx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("canvas_idx"));
    duk_push_string(ctx, "#000000"); duk_put_prop_string(ctx, -2, "fillStyle");
    duk_push_number(ctx, 1.0); duk_put_prop_string(ctx, -2, "globalAlpha");
    duk_push_string(ctx, "#000000"); duk_put_prop_string(ctx, -2, "strokeStyle");
    duk_push_number(ctx, 1.0); duk_put_prop_string(ctx, -2, "lineWidth");
    js_register(ctx, JS_CANVAS_METHODS,
                sizeof(JS_CANVAS_METHODS) / sizeof(JS_CANVAS_METHODS[0]));
    duk_push_global_stash(ctx);
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, stash_key);
    duk_pop(ctx); /* leave context object as the return value */
    return 1;
}

static duk_ret_t js_el_add_event_listener(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    const char *event = duk_to_string(ctx, 0);
    if (!r || !r->js || idx < 0 || idx >= r->node_count) { duk_push_false(ctx); return 1; }
    /* Standard DOM: the activation event is "click" ("activate" was a
       non-standard alias and is gone). */
    if (strcmp(event, "click")) {
        duk_push_false(ctx);
        return 1;
    }
    if (!duk_is_function(ctx, 1) || r->js->listener_count >= VW_MAX_LISTENERS) {
        duk_push_false(ctx);
        return 1;
    }
    /* Store the function under a unique key in the JS "__vw_events" object,
       then remember (node index, key) here. */
    char key[16];
    snprintf(key, sizeof(key), "e%d", r->js->listener_count);
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "__vw_events");
    duk_dup(ctx, 1);                                    /* the function */
    duk_put_prop_string(ctx, -2, key);
    duk_pop_2(ctx);
    r->js->listeners[r->js->listener_count].node_index = idx;
    snprintf(r->js->listeners[r->js->listener_count].fn_key,
             sizeof(r->js->listeners[0].fn_key), "%s", key);
    r->js->listener_count++;
    duk_push_true(ctx);
    return 1;
}

/* removeEventListener: best-effort match by function identity.  Removed
 * listeners are marked with node_index = -2 (never a real node). */
static duk_ret_t js_el_remove_event_listener(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    int i;
    if (!r || !r->js || idx < 0) return 0;
    for (i = 0; i < r->js->listener_count; i++) {
        if (r->js->listeners[i].node_index != idx) continue;
        duk_push_global_object(ctx);
        duk_get_prop_string(ctx, -1, "__vw_events");
        duk_get_prop_string(ctx, -1, r->js->listeners[i].fn_key);
        if (duk_is_function(ctx, -1) && duk_strict_equals(ctx, 0, -1))
            r->js->listeners[i].node_index = -2;         /* removed */
        duk_pop_3(ctx);
    }
    return 0;   /* Chrome: undefined */
}

/* el.hidden (standard DOM): display:none on the element and its direct
 * text content, like the old hide()/show() methods did. */
static duk_ret_t js_prop_hidden_get(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx);
    if (!r || idx < 0 || idx >= r->node_count) { duk_push_false(ctx); return 1; }
    duk_push_boolean(ctx, r->nodes[idx].display_none ? 1 : 0);
    return 1;
}

static duk_ret_t js_prop_hidden_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_this_element(ctx), owner, hide;
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    hide = duk_to_boolean(ctx, 0) ? 1 : 0;
    r->nodes[idx].display_none = hide;
    owner = js_text_owner(r, idx);
    if (owner != idx) r->nodes[owner].display_none = hide;
    /* Chrome hides the whole subtree, not just the element and its text
       child.  Nodes are in document order, so every node deeper than idx
       until depth drops back to idx's level is a descendant. */
    { int i;
      for (i = idx + 1; i < r->node_count; i++) {
          if (r->nodes[i].depth <= r->nodes[idx].depth) break;
          r->nodes[i].display_none = hide;
      }
    }
    return 0;
}

/* ---- standard-DOM accessor properties --------------------------------------
 * Pages written for Chrome assign `el.textContent = "..."`, read/write
 * `el.value` on form controls, and set `el.style.cssText = "..."`.  These
 * accessors live on the element prototype and map onto the same
 * setText/getValue/setStyle code as the named methods.  (style.cssText is
 * write-oriented: reading it back gives "".) */

static duk_ret_t js_prop_text_get(duk_context *ctx)  { return js_el_get_text(ctx); }
static duk_ret_t js_prop_text_set(duk_context *ctx)  { return js_el_set_text(ctx); }
static duk_ret_t js_prop_value_get(duk_context *ctx) { return js_el_get_value(ctx); }
static duk_ret_t js_prop_value_set(duk_context *ctx) { return js_el_set_value(ctx); }

/* The `this` of the cssText pair is the little style object; it carries
 * the element's node index in its own hidden "idx". */
static int js_style_this_idx(duk_context *ctx) {
    int idx;
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("idx"));
    idx = duk_get_int_default(ctx, -1, -1);
    duk_pop_2(ctx);
    return idx;
}

static duk_ret_t js_style_csstext_get(duk_context *ctx) {
    (void)js_style_this_idx(ctx);
    duk_push_string(ctx, "");       /* write-oriented; reads give "" */
    return 1;
}

static duk_ret_t js_style_csstext_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int idx = js_style_this_idx(ctx);
    const char *css = duk_to_string(ctx, 0);
    if (!r || idx < 0 || idx >= r->node_count) return 0;
    style_apply_inline(r, &r->nodes[idx], css);
    return 0;
}

/* el.style -> a per-lookup style object with a cssText accessor. */
static duk_ret_t js_prop_style_get(duk_context *ctx) {
    int idx = js_this_element(ctx);
    if (idx < 0) { duk_push_undefined(ctx); return 1; }
    duk_push_object(ctx);                       /* the style object */
    duk_push_int(ctx, idx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("idx"));
    duk_push_string(ctx, "cssText");
    duk_push_c_function(ctx, js_style_csstext_get, 0);
    duk_push_c_function(ctx, js_style_csstext_set, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER
                          | DUK_DEFPROP_SET_CONFIGURABLE);
    return 1;
}

/* The custom element API (setText/setStyle/on("activate")/setActive/...)
 * was REMOVED: pages must use standard DOM only (textContent, style.cssText,
 * className, hidden, addEventListener) until the engine reaches ES10-level
 * support.  The standard accessors below map onto the same node fields the
 * old methods used. */

/* --------------------------------------------------------------------------
 * document + query engine
 * ------------------------------------------------------------------------ */

/* The selector forms supported are exactly the CSS engine's (see
 * selector_matches in cwv_css.c): "tag", "#id", ".class",
 * "tag.class", "tag#id.class", and comma-separated lists. */

static duk_ret_t js_doc_get_element_by_id(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    const char *id = duk_to_string(ctx, 0);
    int i, found = -1;
    if (!r) { duk_push_null(ctx); return 1; }
    for (i = 0; i < r->node_count; i++) {
        if (r->nodes[i].id[0] && !strcmp(r->nodes[i].id, id)) { found = i; break; }
    }
    /* Not a box element?  Try inline elements (kept in the id map). */
    if (found < 0) {
        for (i = 0; i < r->id_map_count; i++)
            if (!strcmp(r->id_map[i].id, id)) { found = r->id_map[i].node_index; break; }
    }
    js_push_element(ctx, found);
    return 1;
}

static duk_ret_t js_doc_query_selector(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    const char *sel = duk_to_string(ctx, 0);
    int i, found = -1;
    if (!r) { duk_push_null(ctx); return 1; }
    for (i = 0; i < r->node_count; i++) {
        const CwvNode *n = &r->nodes[i];
        if (selector_matches(sel, n->tag, n->id, n->cls, 0)) { found = i; break; }
    }
    js_push_element(ctx, found);
    return 1;
}

static duk_ret_t js_doc_query_selector_all(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    const char *sel = duk_to_string(ctx, 0);
    int i;
    duk_push_array(ctx);
    if (!r) return 1;
    int count = 0;
    for (i = 0; i < r->node_count; i++) {
        const CwvNode *n = &r->nodes[i];
        if (!selector_matches(sel, n->tag, n->id, n->cls, 0)) continue;
        js_push_element(ctx, i);
        duk_put_prop_index(ctx, -2, count++);
    }
    return 1;
}

/* location.href getter/setter: the most common Chrome navigation form. */
static duk_ret_t js_location_href_get(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    duk_push_string(ctx, (r && r->base_url[0]) ? r->base_url : "");
    return 1;
}

static duk_ret_t js_location_href_set(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    const char *url = duk_to_string(ctx, 0);
    if (!r || !url || !url[0]) return 0;
    snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", url);
    return 0;
}

/* document.location.assign(url): navigate after the current frame. */
static duk_ret_t js_doc_location_assign(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    const char *url = duk_to_string(ctx, 0);
    if (!r || !url) return 0;
    snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", url);
    snprintf(r->pending_target, sizeof(r->pending_target), "app");
    return 0;
}

/* --------------------------------------------------------------------------
 * timers: setTimeout / setInterval / clearTimeout / clearInterval
 * ------------------------------------------------------------------------ */

/* Register a timer; helper for both setTimeout and setInterval. */
static duk_ret_t js_timer_common(duk_context *ctx, int is_interval) {
    CwvRenderer *r = js_get_renderer(ctx);
    int slot;
    unsigned long long ms;
    if (!r || !r->js) { duk_push_int(ctx, -1); return 1; }
    if (!duk_is_function(ctx, 0)) { duk_push_int(ctx, -1); return 1; }
    for (slot = 0; slot < VW_MAX_TIMERS; slot++)
        if (!r->js->timers[slot].active) break;
    if (slot >= VW_MAX_TIMERS) { duk_push_int(ctx, -1); return 1; }
    ms = (unsigned long long)duk_get_number_default(ctx, 1, 0.0);
    /* Store the callback under a unique key in the JS "__vw_timers" object. */
    char key[16];
    snprintf(key, sizeof(key), "t%d", slot);
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "__vw_timers");
    duk_dup(ctx, 0);
    duk_put_prop_string(ctx, -2, key);
    duk_pop_2(ctx);
    r->js->timers[slot].active = 1;
    r->js->timers[slot].is_interval = is_interval;
    r->js->timers[slot].period = is_interval ? ms : 0;
    r->js->timers[slot].next_ms = js_now_ms() + ms;
    snprintf(r->js->timers[slot].fn_key, sizeof(r->js->timers[slot].fn_key), "%s", key);
    duk_push_int(ctx, slot);
    return 1;
}

static duk_ret_t js_set_timeout(duk_context *ctx)   { return js_timer_common(ctx, 0); }
static duk_ret_t js_set_interval(duk_context *ctx)  { return js_timer_common(ctx, 1); }

/* clearTimeout and clearInterval are the same function (id = the int the
 * set* call returned). */
static duk_ret_t js_clear_timer(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    int id = duk_get_int_default(ctx, 0, -1);
    if (!r || !r->js || id < 0 || id >= VW_MAX_TIMERS) return 0;
    r->js->timers[id].active = 0;
    return 0;
}

/* --------------------------------------------------------------------------
 * THE REGISTRIES: one row per exposed JS function.
 * To add a function: implement it above, then add one row here.  The
 * tables are walked once when the JS context is created.
 * ------------------------------------------------------------------------ */



/* ---- fetch(url, callback) ------------------------------------------------
 * Minimal synchronous network fetch for page scripts (Android-WebView-style
 * capability, not browsing).  `callback(status, text)` always runs:
 *   status >= 0 : HTTP status code, text = response body (NUL-terminated,
 *                 truncated at JS_FETCH_MAX bytes)
 *   status -1   : failure - no network, bad URL, connection error, or no
 *                 fetch hook registered on this host.  text is "".
 * The return value is `true` when the request completed (status >= 0) and
 * `false` on failure, so scripts can branch without a callback.
 * NEVER THROWS: a throwing callback or a missing hook only produces console
 * lines; the render loop keeps running in every case.  v1 is synchronous
 * (it blocks the render thread until the request finishes or fails) and
 * only supports http/https GET. */
#define JS_FETCH_MAX (256 * 1024)


/* Response body for the promise path: Promise.resolve(body) so the common
 * Chrome pattern  fetch(u).then(r => r.text()).then(s => ...) works. */
static duk_ret_t js_response_text(duk_context *ctx) {
    const char *body = duk_get_string(ctx, -1);   /* stashed body prop (self is at 0) */
    duk_get_prop_string(ctx, 0, "__vw_body");
    body = duk_get_string(ctx, -1);
    duk_pop(ctx);
    duk_get_global_string(ctx, "Promise");
    duk_get_prop_string(ctx, -1, "resolve");
    duk_remove(ctx, -2);
    duk_push_string(ctx, body ? body : "");
    if (duk_pcall(ctx, 1) != DUK_EXEC_SUCCESS) { duk_pop(ctx); duk_push_undefined(ctx); }
    return 1;
}

/* fetch(url) without a callback: Chrome semantics - returns a Promise that
 * resolves to a Response-like {ok, status, text()}.  Rejection only
 * happens for a missing network backend... to keep existing pages simple
 * we still RESOLVE (with ok:false, status:-1), matching the old graceful
 * failure the callback path documents. */
static duk_ret_t js_fetch_promise(duk_context *ctx, CwvRenderer *r, CwvJs *js, const char *url) {
    char *body = (char *)malloc(JS_FETCH_MAX);
    int status = -1;
    if (!body) { duk_push_undefined(ctx); return 1; }
    if (r && r->fetch_hook) {
        status = r->fetch_hook(url, body, JS_FETCH_MAX, r->fetch_userdata);
        if (status < 0) body[0] = 0;
    } else if (js) {
        js_console_add(js, "warn",
            "fetch: no network backend registered on this host; "
            "resolving with ok:false");
    }
    /* Build the Response, then Promise.resolve(response). */
    duk_push_object(ctx);                       /* response */
    duk_push_boolean(ctx, status >= 0);
    duk_put_prop_string(ctx, -2, "ok");
    duk_push_int(ctx, (duk_int_t)status);
    duk_put_prop_string(ctx, -2, "status");
    duk_push_string(ctx, (status >= 0) ? body : "");
    duk_put_prop_string(ctx, -2, "__vw_body");
    duk_push_c_function(ctx, js_response_text, 0);
    duk_put_prop_string(ctx, -2, "text");
    duk_get_global_string(ctx, "Promise");
    duk_get_prop_string(ctx, -1, "resolve");
    duk_remove(ctx, -2);                        /* [response, resolve] */
    duk_swap(ctx, -2, -1);                      /* [resolve, response] */
    if (duk_pcall(ctx, 1) != DUK_EXEC_SUCCESS) { duk_pop(ctx); duk_push_undefined(ctx); }
    free(body);
    return 1;
}

static duk_ret_t js_fetch(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);
    CwvJs *js = (r && r->js) ? r->js : NULL;
    const char *url = duk_safe_to_string(ctx, 0);
    char *body = NULL;
    int status = -1;

    /* Chrome shape: fetch(url) returns a Promise resolving to a Response
       {ok, status, text()}.  The legacy fetch(url, callback) trampoline is
       kept for pages already written against it. */
    int legacy = duk_is_function(ctx, 1);
    /* Resolve relative URLs against the page base like every browser;
       the hook only ever sees absolute URLs otherwise. */
    { char full[1024];
      if (url && url[0] && url[0] != '/' && strncmp(url, "http://", 7) != 0 &&
          strncmp(url, "https://", 8) != 0 && strncmp(url, "file://", 7) != 0 &&
          strncmp(url, "app0:", 5) != 0 && r && r->base_url[0]) {
          resolve_url(r->base_url, url, full, sizeof(full));
          duk_push_string(ctx, full);
          duk_replace(ctx, 0);
          url = duk_safe_to_string(ctx, 0);
      }
    }
    if (!legacy) return js_fetch_promise(ctx, r, js, url);
    body = (char *)malloc(JS_FETCH_MAX);
    if (!body) {                       /* OOM: fail gracefully, no throw */
        if (js) js_console_add(js, "warn", "fetch: out of memory");
        duk_dup(ctx, 1);
        duk_push_int(ctx, -1);
        duk_push_string(ctx, "");
        if (duk_pcall(ctx, 2) != DUK_EXEC_SUCCESS) duk_pop(ctx);
        duk_push_false(ctx);
        return 1;
    }
    if (r && r->fetch_hook) {
        status = r->fetch_hook(url, body, JS_FETCH_MAX, r->fetch_userdata);
        if (status < 0) body[0] = 0;   /* on failure the body is undefined */
    } else if (js) {
        js_console_add(js, "warn",
            "fetch: no network backend registered on this host; "
            "returning failure");
    }
    /* run the callback via duk_pcall so a throwing callback cannot crash */
    duk_dup(ctx, 1);
    duk_push_int(ctx, (duk_int_t)status);
    duk_push_string(ctx, (status >= 0) ? body : "");
    if (duk_pcall(ctx, 2) != DUK_EXEC_SUCCESS) {
        if (js) {
            const char *msg = duk_safe_to_string(ctx, -1);
            js_console_add(js, "error", msg ? msg : "fetch callback error");
        }
        duk_pop(ctx);
    }
    free(body);                        /* duk_push_string already copied it */
    duk_push_boolean(ctx, status >= 0);
    return 1;
}

static const JsFn JS_CONSOLE_METHODS[] = {
    { "log",   js_console_log },
    { "info",  js_console_info },
    { "warn",  js_console_warn },
    { "error", js_console_error },
};

static const JsFn JS_DOCUMENT_METHODS[] = {
    { "getElementById",     js_doc_get_element_by_id },
    { "querySelector",     js_doc_query_selector },
    { "querySelectorAll",  js_doc_query_selector_all },
    { "location",           NULL },   /* replaced by the location object below */
};

static const JsFn JS_ELEMENT_METHODS[] = {
    { "addEventListener",    js_el_add_event_listener },
    { "getContext",          js_el_get_context },
    { "removeEventListener", js_el_remove_event_listener },
};

static const JsFn JS_GLOBAL_METHODS[] = {
    { "setTimeout",    js_set_timeout },
    { "setInterval",   js_set_interval },
    { "clearTimeout",  js_clear_timer },
    { "clearInterval", js_clear_timer },
};

/* Register one table onto a JS object that is on top of the stack. */
static void js_register(duk_context *ctx, const JsFn *table, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (!table[i].fn) continue;   /* placeholder rows are filled later */
        duk_push_c_function(ctx, table[i].fn, DUK_VARARGS);
        duk_put_prop_string(ctx, -2, table[i].name);
    }
}

/* Build the whole JS API on a fresh context. */
static void js_install_api(duk_context *ctx, CwvRenderer *r) {
    size_t n;

    /* console */
    duk_push_object(ctx);
    js_register(ctx, JS_CONSOLE_METHODS,
                sizeof(JS_CONSOLE_METHODS) / sizeof(JS_CONSOLE_METHODS[0]));
    duk_put_global_string(ctx, "console");

    /* fetch(url, callback) - network for scripts, fails gracefully */
    duk_push_c_function(ctx, js_fetch, 2);
    duk_put_global_string(ctx, "fetch");

    /* document + document.location.assign */
    duk_push_object(ctx);
    duk_push_object(ctx);                       /* location object */
    duk_push_c_function(ctx, js_doc_location_assign, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "assign");
    /* Chrome pages also set location.href = "page.html" to navigate.
       duk_def_prop expects [.., key, getter, setter] on the stack. */
    duk_push_string(ctx, "href");
    duk_push_c_function(ctx, js_location_href_get, 0);
    duk_push_c_function(ctx, js_location_href_set, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);
    duk_put_prop_string(ctx, -2, "location");   /* document.location = {...} */
    n = sizeof(JS_DOCUMENT_METHODS) / sizeof(JS_DOCUMENT_METHODS[0]);
    { size_t i;
      for (i = 0; i < n; i++) {
        if (!JS_DOCUMENT_METHODS[i].fn) continue;
        duk_push_c_function(ctx, JS_DOCUMENT_METHODS[i].fn, DUK_VARARGS);
        duk_put_prop_string(ctx, -2, JS_DOCUMENT_METHODS[i].name);
      }
    }
    duk_put_global_string(ctx, "document");

    /* timers (globals need the global object pushed as their target) */
    duk_push_global_object(ctx);
    js_register(ctx, JS_GLOBAL_METHODS,
                sizeof(JS_GLOBAL_METHODS) / sizeof(JS_GLOBAL_METHODS[0]));
    duk_pop(ctx);

    /* window object (innerWidth/innerHeight values frozen at load time) */
    duk_push_object(ctx);
    duk_push_number(ctx, (double)r->width);
    duk_put_prop_string(ctx, -2, "innerWidth");
    duk_push_number(ctx, (double)r->height);
    duk_put_prop_string(ctx, -2, "innerHeight");
    duk_put_global_string(ctx, "window");      /* window = { innerWidth, ... */

    /* Small navigator identity surface used by shared launch pages. This is
     * deliberately platform identity, not a claim of full browser UA/API
     * compatibility. */
    duk_push_object(ctx);
    duk_push_string(ctx, cwv_platform_name());
    duk_put_prop_string(ctx, -2, "platform");
    duk_push_sprintf(ctx, "CWebView (%s)", cwv_platform_name());
    duk_put_prop_string(ctx, -2, "userAgent");
    duk_put_global_string(ctx, "navigator");
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "navigator");
    duk_get_prop_string(ctx, -2, "window");
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, "navigator");
    duk_pop(ctx);
    duk_pop(ctx);
    duk_pop(ctx);
    /* window.location, window.document and the bare `location` global all
       point at the same objects in Chrome; pages use them interchangeably.
       Stack trace is spelled out - a leaked frame here corrupts every
       later duk_put_global_string. */
    duk_push_global_object(ctx);               /* [ global ] */
    duk_get_prop_string(ctx, -1, "document"); /* [ g, document ] */
    duk_get_prop_string(ctx, -1, "location"); /* [ g, document, location ] */
    /* global.location = location (bare identifier form) */
    duk_dup(ctx, -1);                         /* [ g, doc, loc, loc ] */
    duk_put_prop_string(ctx, -4, "location");  /* pop -> [ g, doc, loc ] */
    duk_get_prop_string(ctx, -3, "window");   /* [ g, doc, loc, win ] */
    duk_dup(ctx, -3);                         /* [ g, doc, loc, win, doc ] */
    duk_put_prop_string(ctx, -2, "document");  /* win.document=doc; pop -> [ g, doc, loc, win ] */
    duk_dup(ctx, -2);                         /* [ g, doc, loc, win, loc ] */
    duk_put_prop_string(ctx, -2, "location");  /* win.location=loc; pop -> [ g, doc, loc, win ] */
    duk_pop(ctx);                              /* [ g, doc, loc ] */
    duk_pop_2(ctx);                            /* [ g ] */
    duk_pop(ctx);                              /* [ ] */

    /* hidden event/timer callback stores (plain JS objects) */
    duk_push_object(ctx);
    duk_put_global_string(ctx, "__vw_timers");
    duk_push_object(ctx);
    duk_put_global_string(ctx, "__vw_events");

    /* element prototype, stashed where js_push_element() can find it */
    duk_push_object(ctx);
    js_register(ctx, JS_ELEMENT_METHODS,
                sizeof(JS_ELEMENT_METHODS) / sizeof(JS_ELEMENT_METHODS[0]));

    /* standard-DOM accessors (textContent, value, style) on the prototype */
#define JS_DEF_ACCESSOR(NAME, GET, SET)                                    \
    do {                                                                   \
        duk_push_string(ctx, (NAME));                                      \
        duk_push_c_function(ctx, (GET), 0);                                \
        duk_push_c_function(ctx, (SET), 1);                                \
        duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER                      \
                                  | DUK_DEFPROP_HAVE_SETTER                \
                                  | DUK_DEFPROP_SET_CONFIGURABLE);         \
    } while (0)
    JS_DEF_ACCESSOR("textContent", js_prop_text_get,  js_prop_text_set);
    JS_DEF_ACCESSOR("value",       js_prop_value_get, js_prop_value_set);
    JS_DEF_ACCESSOR("className",  js_prop_class_get, js_prop_class_set);
    JS_DEF_ACCESSOR("hidden",      js_prop_hidden_get, js_prop_hidden_set);
    JS_DEF_ACCESSOR("width",       js_prop_canvas_width_get, js_prop_canvas_width_set);
    JS_DEF_ACCESSOR("height",      js_prop_canvas_height_get, js_prop_canvas_height_set);
#undef JS_DEF_ACCESSOR
    duk_push_string(ctx, "style");            /* getter-only: reading        */
    duk_push_c_function(ctx, js_prop_style_get, 0);   /* el.style builds it  */
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_CONFIGURABLE);

    duk_push_global_stash(ctx);
    duk_swap(ctx, -2, -1);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("element_proto"));
    duk_pop(ctx);

    js_stash_renderer(ctx, r);
}

/* Create the JS context for a page (or the first eval). */
static int js_ensure_context(CwvRenderer *r) {
    duk_context *ctx;
    if (!r) return -1;
    if (r->js) return 0;
    r->js = calloc(1, sizeof(CwvJs));
    if (!r->js) return -1;
    ctx = duk_create_heap_default();
    if (!ctx) { free(r->js); r->js = NULL; return -1; }
    r->js->ctx = ctx;
    js_install_api(ctx, r);

    /* Let the ESM module inject its library prelude (Map/Set, Array.find,
     * String.padStart, ...) directly into the fresh context.  This runs
     * BEFORE any page script, so pages see the built-ins immediately. */
    if (r->prelude_hook) {
        const char *prelude = r->prelude_hook(r->prelude_userdata);
        if (prelude && prelude[0]) {
            if (duk_peval_string(ctx, prelude) != 0) {
                js_console_add(r->js, "error", "ESM prelude failed");
                duk_pop(ctx);
            } else {
                duk_pop(ctx);
            }
        }
    }
    return 0;
}

/* Register the ES6 library prelude hook (see renderer header). */
void js_set_prelude_hook(CwvRenderer *r, CwvJsPreludeFn fn,
                         void *userdata) {
    if (!r) return;
    r->prelude_hook = fn;
    r->prelude_userdata = userdata;
}

void js_set_fetch_hook(CwvRenderer *r, CwvFetchHook fn, void *userdata) {
    if (!r) return;
    r->fetch_hook = fn;
    r->fetch_userdata = userdata;
}

/* Register the ES6->ES5 source transformer (see renderer header).  The
 * hook is normally installed once at startup by main.c via the built-in
 * module is loaded; a NULL fn uninstalls it. */
/* case-insensitive compare for script type attributes */
static int js_ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

void js_set_ts_transform(CwvRenderer *r, CwvSourceTransformFn fn,
                         void *userdata) {
    if (!r) return;
    r->ts_transform = fn;
    r->ts_transform_userdata = userdata;
}

void js_set_source_transform(CwvRenderer *r, CwvSourceTransformFn fn,
                             void *userdata) {
    if (!r) return;
    r->source_transform = fn;
    r->source_transform_userdata = userdata;
}

/* When a script fails to COMPILE, the usual cause (the "normal issue") is
 * modern syntax the ES6 transformer does not convert: class, async/await,
 * rest/spread, destructuring, default params, object shorthand...  Those
 * pass through untouched and Duktape answers with a bare "SyntaxError"
 * that gives no clue.  This scans the ORIGINAL source for the obvious
 * offenders and logs a hint line, so the in-app DevTools console names the
 * real problem instead of the same mystery error on every page load.
 * (A hint is only diagnostic: words inside strings can false-positive,
 * and it is never logged for runtime errors - those mean the script
 * compiled fine and the hint would mislead.) */
/* Does the ORIGINAL (pre-transform) source contain ES6 syntax that only the
 * transformer can convert?  Used when a script fails to compile: if
 * the answer is yes and the transformer never ran, it is not enabled. */
static int js_source_looks_es6(const char *src) {
    static const char *needles[] = {
        "let ", "const ", "=>", "`", "import ", "export ", " of ", NULL
    };
    size_t i;
    if (!src) return 0;
    for (i = 0; needles[i]; i++)
        if (strstr(src, needles[i])) return 1;
    return 0;
}

/* Detect object shorthand `{x, y}` (an object literal whose members are bare
 * identifiers, no `key: value` pairs).  The transformer does not convert it,
 * Duktape cannot parse it, and it never produced a hint before - which is
 * why "unterminated statement" stayed mysterious for shorthand-heavy pages.
 * This is a loose heuristic scan (strings skipped); false positives are
 * fine, it only powers a hint line. */
static int js_scan_object_shorthand(const char *src) {
    const char *p = src;
    int nest = 0;
    const char *body = NULL;
    if (!src) return 0;
    while (*p) {
        if (*p == '\'' || *p == '"' || *p == '`') {
            char q = *p++;
            while (*p && *p != q) p += (*p == '\\' && p[1]) ? 2 : 1;
            if (*p) p++;
            continue;
        }
        if (*p == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (*p == '{') {
            if (nest == 0) body = p + 1;
            nest++;
        } else if (*p == '}') {
            nest--;
            if (nest == 0 && body) {
                /* a shorthand body is only identifiers, commas, whitespace */
                const char *q = body;
                int ok = 0;
                while (q < p && (*q == ' ' || *q == '\t' || *q == '\n' ||
                                 *q == '\r' || *q == ','))
                    q++;
                if (q < p) {
                    ok = 1;
                    while (q < p) {
                        if (isalnum((unsigned char)*q) || *q == '_' || *q == '$')
                            q++;
                        else if (*q == ',' || *q == ' ' || *q == '\t' ||
                                 *q == '\n' || *q == '\r')
                            q++;
                        else { ok = 0; break; }
                    }
                }
                if (ok) return 1;
                body = NULL;
            }
        }
        p++;
    }
    return 0;
}

/* A script failed to COMPILE.  Say ONE clear, actionable thing: what modern
 * construct the ESM module does not convert, and what it does convert.
 * (Only ever called for compile failures - a runtime error means the script
 * compiled fine and this hint would mislead.) */
static void js_syntax_hint(CwvJs *js, const char *src) {
    static const struct { const char *needle; const char *what; } checks[] = {
        { "class ",     "class" },
        { "async ",     "async/await" },
        { "await ",     "async/await" },
        { "function* ", "generators" },
        { "...",        "spread/rest (...)" },
        { "var {",      "object destructuring" },
        { "let {",      "object destructuring" },
        { "const {",    "object destructuring" },
    };
    size_t i;
    char line[VW_CONSOLE_LINE];
    if (!js || !src) return;
    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        if (strstr(src, checks[i].needle)) {
            snprintf(line, sizeof(line),
                     "hint: '%s' is ES6+ the ESM module does not convert yet "
                     "(it converts: let/const, arrows, template literals, "
                     "for..of, import/export) - rewrite or remove it",
                     checks[i].what);
            js_console_add(js, "hint", line);
            return;
        }
    }
    if (js_scan_object_shorthand(src)) {
        js_console_add(js, "hint",
            "hint: 'object shorthand {a, b}' is ES6+ the ESM module does not "
            "convert yet - write {a: a, b: b} instead");
        return;
    }
}

/* Run one piece of JS source; errors land in the console buffer.
 * If an ES6 transformer is registered, the source is converted first. */
/* ES7+ (2026-09-26 (47)): drain the prelude's Promise job queue.  The
 * async/await transform emits Promise chains whose callbacks are queued
 * by the ESM module's prelude; they run AFTER the current script (real
 * microtask semantics).  This calls the prelude-defined __vw_drain()
 * global - a no-op when no module/prelude is loaded, so the engine never
 * depends on the module being present. */
static void js_drain_jobs(CwvRenderer *r)
{
    duk_context *ctx;
    if (!js_is_active(r)) return;
    ctx = r->js->ctx;
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "__vw_drain");
    if (duk_is_function(ctx, -1)) {
        /* duk_pcall replaces the function slot with the result (or the
         * error object), so exactly ONE item is popped in both paths -
         * duk_pop_2 here underflowed the stack and Duktape aborted the
         * process (crash report 2026-09-26 (47), es6 gate). */
        duk_pcall(ctx, 0);
    }
    duk_pop(ctx);            /* drain value / pcall result */
    duk_pop(ctx);            /* global object */
}

/* TypeScript: run the stripper first (if registered), then hand the
 * JavaScript result to the normal ES6->run pipeline. Returns -3 when a
 * registered stripper refused the construct (already hinted). */
static int js_run_source_ex(CwvRenderer *r, const char *source,
                            const char *origin, int is_ts);

static int js_run_source(CwvRenderer *r, const char *source,
                         const char *origin) {
    return js_run_source_ex(r, source, origin, 0);
}

static int js_run_ts_source(CwvRenderer *r, const char *source,
                            const char *origin)
{
    char *stripped = NULL;
    int rc;
    if (r->ts_transform) {
        size_t n = strlen(source), cap = n * 4 + 4096;
        stripped = (char *)malloc(cap);
        if (stripped) {
            rc = r->ts_transform(source, stripped, cap,
                                 r->ts_transform_userdata);
            if (rc == 1) {
                rc = js_run_source_ex(r, stripped, origin, 0);
                free(stripped);
                return rc;
            }
            if (rc == -2) {   /* unsupported construct: honest hint */
                char line[VW_CONSOLE_LINE];
                snprintf(line, sizeof(line),
                         "%s: TypeScript construct this build cannot "
                         "convert (%s) - script skipped",
                         origin, stripped[0] ? stripped : "unknown");
                js_console_add(r->js, "error", line);
                free(stripped);
                return -1;
            }
            if (rc == -1) {   /* overflow: run the original */
                char line[VW_CONSOLE_LINE];
                snprintf(line, sizeof(line),
                         "%s: TypeScript conversion buffer exhausted - "
                         "running the original", origin);
                js_console_add(r->js, "warn", line);
            }
            /* rc == 0: no TypeScript syntax - run the original */
        }
    }
    if (stripped) free(stripped);
    return js_run_source_ex(r, source, origin, 0);
}

static int js_run_source_ex(CwvRenderer *r, const char *source,
                            const char *origin, int is_ts) {
    (void)is_ts;
    {
    duk_int_t rc;
    duk_int_t compile_rc;
    const char *original = source;   /* pre-transform, for diagnostics */
    char *es5 = NULL;
    int transformed = 0;             /* did the ESM module rewrite it? */
    if (!r || !r->js) return -1;
    if (r->source_transform && source && source[0]) {
        size_t n = strlen(source), cap = n * 6 + 4096;  /* room to expand */
        es5 = (char *)malloc(cap);
        if (es5) {
            if (r->source_transform(source, es5, cap,
                                    r->source_transform_userdata) == 1) {
                source = es5;
                transformed = 1;
            } else
                es5[0] = 0;   /* module says "not transformed": run original */
        }
    }
    duk_push_string(r->js->ctx, source);
    duk_push_string(r->js->ctx, origin);
    compile_rc = duk_pcompile(r->js->ctx, DUK_COMPILE_EVAL);
    if (compile_rc == DUK_ERR_NONE)
        rc = duk_pcall(r->js->ctx, 0);
    else
        rc = compile_rc;            /* compile failed: syntax error */
    if (rc != DUK_ERR_NONE) {
        char line[VW_CONSOLE_LINE];
        snprintf(line, sizeof(line), "%s: %s", origin,
                 duk_safe_to_string(r->js->ctx, -1));
        js_console_add(r->js, "error", line);
        duk_pop(r->js->ctx);
        /* Only SYNTAX errors get a hint; a runtime error means the script
         * compiled, so the hint would mislead. */
        if (compile_rc != DUK_ERR_NONE) {
            if (!transformed && js_source_looks_es6(original))
                js_console_add(r->js, "hint",
                    "hint: this script reached the engine as RAW ES6+ - the "
                    "ESM transformer did not convert it. Either the provider "
                    "is not enabled (check DevTools > Platform > ESM row) or "
                    "it answered 'cannot convert'; the engine itself is "
                    "ES5.1 only");
            else
                js_syntax_hint(r->js, source);
        }
        free(es5);                  /* (was leaked on the error path) */
        return -1;
    }
    duk_pop(r->js->ctx);
    free(es5);
    js_drain_jobs(r);   /* ES7+: run promise jobs queued by this script */
    return 0;
}

}

/* Public: evaluate JS right now (backend_eval).  Creates the context on
 * first use so eval works even on pages without <script>. */
int js_eval(CwvRenderer *r, const char *javascript) {
    if (!r || !javascript) return -1;
    if (js_ensure_context(r) < 0) return -1;
    return js_run_source(r, javascript, "eval");
}

int js_is_active(const CwvRenderer *r) {
    return r && r->js && r->js->ctx;
}

/* Public: harvest <script> blocks and run them.  Called at the end of
 * backend_load(); no-op when the page has no inline scripts. */
void js_harvest_and_run(CwvRenderer *r) {
    const char *p;
    int i;
    if (!r || !r->html) return;

    /* Scan for <script ...> ... </script> pairs.  External scripts (src=)
     * are announced and skipped in v1 (an external fetch would need the curl
     * path, which is available - see load_resource in cwv_backend.c -
     * but we keep the first JS version synchronous and local-only). */
    p = r->html;
    while (p && *p) {
        const char *open = strstr(p, "<script");
        const char *gt, *close;
        if (!open) break;
        gt = strchr(open, '>');
        if (!gt) break;
        close = strstr(gt + 1, "</script>");
        if (!close) break;
        {
            size_t n = (size_t)(close - gt - 1);
            char attrbuf[128];
            int is_ts_script = 0;
            const char *attrs = open + 7;              /* the "src..." part */
            size_t al = (size_t)(gt - attrs);
            if (al >= sizeof(attrbuf)) al = sizeof(attrbuf) - 1;
            memcpy(attrbuf, attrs, al);
            attrbuf[al] = 0;

            {
                char typebuf[64];
                const char *typ = NULL;
                if (find_attr(attrs, attrs + al, "type", typebuf,
                              sizeof(typebuf)) && typebuf[0])
                    typ = typebuf;
                is_ts_script =
                    (typ && (js_ci_eq(typ, "text/typescript") ||
                             js_ci_eq(typ, "application/typescript") ||
                             js_ci_eq(typ, "module/typescript")));
            }
            if (strstr(attrbuf, "src=")) {
                /* EXTERNAL SCRIPT: load the file (file:/// inside the VPK,
                   or http(s) via the curl cache - the same loader as images
                   and stylesheets) and run it through the same source path
                   (so the ESM module's transform applies here too). */
                char src[1024];
                if (find_attr(attrs, attrs + al, "src", src, sizeof(src)) &&
                    src[0]) {
                    char url[1024];
                    resolve_url(r->base_url, src, url, sizeof(url));
                    unsigned char *data = NULL;
                    size_t sz = 0;
                    if (!r->js) js_ensure_context(r);
                    if (r->js && load_resource(r, url, &data, &sz) == 0) {
                        char origin[256];
                        data[sz] = 0;   /* text: NUL-terminate */
                        /* src can be up to ~1KB; cap the copy so the
                           warning-free bound is guaranteed ("<script "
                           + 220 chars + ">\" + NUL fits the 256 origin) */
                        snprintf(origin, sizeof(origin), "<script %.220s>",
                                 strrchr(src, '/') ? strrchr(src, '/') + 1
                                                   : src);
                        {
                            size_t sl = strlen(src);
                            int src_ts = (sl > 3 &&
                                          js_ci_eq(src + sl - 3, ".ts"));
                            if (src_ts || is_ts_script)
                                js_run_ts_source(r, (const char *)data, origin);
                            else
                                js_run_source(r, (const char *)data, origin);
                        }
                        free(data);
                    } else if (r->js) {
                        char line[VW_CONSOLE_LINE];
                        snprintf(line, sizeof(line),
                                 "error: cannot load script %.200s", src);
                        js_console_add(r->js, "error", line);
                    }
                }
            } else if (n > 0) {
                /* Inline script: copy the source and remember it. */
                if (!r->js) js_ensure_context(r);
                if (is_ts_script) {
                    if (!r->js) js_ensure_context(r);
                    if (r->js && !r->ts_transform) {
                        char line[VW_CONSOLE_LINE];
                        snprintf(line, sizeof(line),
                                 "TypeScript script with no TS provider "
                                 "loaded - skipping");
                        js_console_add(r->js, "error", line);
                    } else if (r->js) {
                        char *copy = (char *)malloc(n + 1);
                        if (copy) {
                            memcpy(copy, gt + 1, n);
                            copy[n] = 0;
                            js_run_ts_source(r, copy, "<script ts>");
                            free(copy);
                        }
                    }
                } else if (r->js && r->js->script_count < VW_MAX_SCRIPTS) {
                    char *copy = malloc(n + 1);
                    if (copy) {
                        memcpy(copy, gt + 1, n);
                        copy[n] = 0;
                        r->js->scripts[r->js->script_count++] = copy;
                    }
                }
            }
        }
        p = close + 9;
    }
    if (!r->js) return;   /* page had no usable inline scripts */

    for (i = 0; i < r->js->script_count; i++) {
        char origin[32];
        snprintf(origin, sizeof(origin), "<script #%d>", i + 1);
        js_run_source(r, r->js->scripts[i], origin);
    }
}

/* Public: fire due timers.  Called once per frame from backend_update(). */
void js_tick(CwvRenderer *r) {
    unsigned long long now;
    int slot;
    if (!js_is_active(r)) return;
    now = js_now_ms();
    for (slot = 0; slot < VW_MAX_TIMERS; slot++) {
        if (!r->js->timers[slot].active) continue;
        if (r->js->timers[slot].next_ms > now) continue;

        /* Fetch and call the callback stored under the timer's key. */
        duk_push_global_object(r->js->ctx);
        duk_get_prop_string(r->js->ctx, -1, "__vw_timers");
        duk_get_prop_string(r->js->ctx, -1, r->js->timers[slot].fn_key);
        if (duk_is_function(r->js->ctx, -1)) {
            if (duk_pcall(r->js->ctx, 0) != DUK_EXEC_SUCCESS) {
                char line[VW_CONSOLE_LINE];
                snprintf(line, sizeof(line), "timer %d: %s", slot,
                         duk_safe_to_string(r->js->ctx, -1));
                js_console_add(r->js, "error", line);
            }
            duk_pop(r->js->ctx);
        } else {
            duk_pop(r->js->ctx);
        }
        duk_pop_2(r->js->ctx);

        if (r->js->timers[slot].is_interval)
            r->js->timers[slot].next_ms = now + r->js->timers[slot].period;
        else
            r->js->timers[slot].active = 0;
    }
    /* ES7+ (47): promise chains resolved by a timer callback run now,
     * not one frame late. */
    js_drain_jobs(r);
}

/* Public: does this node have an "activate" listener?  (Cheap check used
 * by the C input code to decide between the JS path and the default path.) */
int js_has_listener(const CwvRenderer *r, int node_index) {
    int i;
    if (!js_is_active(r)) return 0;
    for (i = 0; i < r->js->listener_count; i++)
        if (r->js->listeners[i].node_index == node_index) return 1;
    return 0;
}

/* Public: run the node's "activate" listener, if any. */
/* event.preventDefault(): flip a flag on the JS global so the C caller can
 * see whether any listener cancelled the default action (for example, a
 * link click that must NOT navigate). */
static duk_ret_t vw_event_prevent_default(duk_context *ctx) {
    duk_push_global_object(ctx);
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "__vw_pd");
    duk_pop(ctx);
    return 0;
}

/* Fire the element's "click"/"activate" listeners.  Each listener now
 * receives a small event object ({type:"click", defaultPrevented,
 * preventDefault()}) like a real browser.  Returns 1 when any listener
 * called event.preventDefault(). */
int js_notify_activate(CwvRenderer *r, int node_index) {
    int i, prevented = 0;
    if (!js_is_active(r)) return 0;
    for (i = 0; i < r->js->listener_count; i++) {
        if (r->js->listeners[i].node_index != node_index) continue;
        duk_context *ctx = r->js->ctx;
        duk_push_global_object(ctx);
        duk_get_prop_string(ctx, -1, "__vw_events");
        duk_get_prop_string(ctx, -1, r->js->listeners[i].fn_key);
        if (duk_is_function(ctx, -1)) {
            /* reset the preventDefault flag, then build the event object */
            duk_push_global_object(ctx);
            duk_push_false(ctx);
            duk_put_prop_string(ctx, -2, "__vw_pd");
            duk_pop(ctx);
            duk_push_object(ctx);
            duk_push_string(ctx, "click");
            duk_put_prop_string(ctx, -2, "type");
            /* Chrome handlers read event.target / event.currentTarget to
               learn WHICH element was clicked; without them every listener
               had to close over its element manually. */
            js_push_element(ctx, node_index);
            duk_put_prop_string(ctx, -2, "target");
            js_push_element(ctx, node_index);
            duk_put_prop_string(ctx, -2, "currentTarget");
            duk_push_false(ctx);
            duk_put_prop_string(ctx, -2, "defaultPrevented");
            duk_push_c_function(ctx, vw_event_prevent_default, 0);
            duk_put_prop_string(ctx, -2, "preventDefault");
            if (duk_pcall(ctx, 1) != DUK_EXEC_SUCCESS) {
                char line[VW_CONSOLE_LINE];
                snprintf(line, sizeof(line), "activate node %d: %s", node_index,
                         duk_safe_to_string(ctx, -1));
                js_console_add(r->js, "error", line);
                duk_pop(ctx);
            } else {
                duk_pop(ctx);
                duk_push_global_object(ctx);
                duk_get_prop_string(ctx, -1, "__vw_pd");
                if (duk_to_boolean(ctx, -1)) prevented = 1;
                duk_pop(ctx);
            }
        } else {
            duk_pop(ctx);
        }
        duk_pop_2(ctx);
    }
    return prevented;
}


/* Public: free the whole JS runtime (new page load / shutdown). */
void js_shutdown(CwvRenderer *r) {
    int i;
    if (!r || !r->js) return;
    if (r->js->ctx) duk_destroy_heap(r->js->ctx);
    for (i = 0; i < r->js->script_count; i++) free(r->js->scripts[i]);
    free(r->js);
    r->js = NULL;
}
