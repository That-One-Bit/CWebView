# Extending the Renderer (for humans who mostly know Python)

(For a quick "which file owns what" map, see docs/SOURCE_MAP.md first.)

This is a guide to the renderer files — the tiny HTML/CSS engine
inside Vita WebView. If you can read Python, you can read this file. The whole
engine is ONE file, no libraries, no regex — just C pointers walking through
text. Read the banner comment at the top of the file first: it explains how a
page flows through the code (load → CSS → parse → layout → draw) and has a
section map.

**The golden rule:** the renderer is built around lookup tables. To make it
understand something new, you add a row to a table (and maybe write one small
function). You almost never need to edit the tricky recursive parser itself.

The engine is split into four files, one per responsibility (plus the shared
`cwv_renderer.h` data model and the vendored Duktape JS engine in
`src/backend/js/`):

- `cwv_css.c`  - CSS side:  `CSS_PROPERTIES[]` registry, selectors, rules
- `cwv_html.c` - HTML side: `TAG_TABLE[]` registry, parser, layout
- `cwv_js.c`   - JS side:   `JS_GLOBAL_METHODS[]`, `JS_DOCUMENT_METHODS[]`,
                      `JS_ELEMENT_METHODS[]`, `JS_CONSOLE_METHODS[]`
- `cwv_backend.c` - the rest: resources, drawing, input, public API

---

## Images and stacking (how <img> works now)

- `load_image()` (cwv_backend.c) fetches the src through the normal
  resource loader (local `file:///` or http(s) via libcurl - no JS needed)
  and picks the decoder from the file's MAGIC BYTES, not the URL extension.
- `tag_parse_img()` (cwv_html.c) implements Chrome sizing: no
  width/height = intrinsic (real pixel) size; one side = the other derived
  to keep the ratio; oversized shrinks to the content width. A failed load
  paints a placeholder box with the `alt` text instead.
- CSS `z-index` re-orders PAINTING (a stable sort in `backend_draw`, so
  equal values keep document order) and `object-fit: contain|cover`
  letterbox/crop the texture inside the node box.
- An `<img>` with an explicit `tabindex` is a DPAD focus stop (and gets the
  focus ring), like Chrome; without one it is not.
- A box-less inline `<a>` gives its FIRST child node the focus stop and the
  anchor's tabindex (one stop per link), so plain text links cycle with
  DPAD and show the ring.

## Recipe 1: Add a new CSS property (5 minutes)

Say you want to support `letter-spacing`. Two steps.

**Step 1** — write a tiny handler function near the others in SECTION 4:

```c
static void css_prop_letter_spacing(CwvNode *n, const char *v) {
    float x = css_length_px(v, n->font_size);   /* parse "2px" / "0.1em" */
    if (x >= -20 && x < 200) n->letter_spacing = x;   /* needs a struct field */
}
```

(If the property needs a value the `CwvNode` struct doesn't have yet, add
the field to the struct in SECTION 1, e.g. `float letter_spacing;`. Reset it
in `node_defaults()` too — that function is the "factory" for fresh nodes.)

**Step 2** — add ONE line to the `CSS_PROPERTIES[]` table:

```c
static const CssPropertyMap CSS_PROPERTIES[] = {
    { "color",           css_prop_color },
    ...
    { "letter-spacing",  css_prop_letter_spacing },   /* <-- your line */
};
```

Done. Any `style="letter-spacing: 2px"` or rule `p { letter-spacing: 1px }`
now reaches your function with the raw value text (`"2px"`). Unknown values
are ignored, so partial support is safe. You never touch the parser.

Useful helpers your handler can call (all in SECTION 3):

| helper               | what it does                                  |
|----------------------|-----------------------------------------------|
| `css_length_px(v, n->font_size)` | "12px"/"1.5em"/"2rem" → pixels     |
| `css_font_size(v, n->font_size)` | font-size value → renderer units    |
| `parse_color(v, fallback)`       | "#aabbcc"/"red" → packed RGBA       |
| `parse_lengths(v, fs, &a,&b,&c,&d)` | 1–4 value shorthands             |
| `css_border_style_value(v)` / `css_overflow_value(v)` | keyword → int |

## Recipe 2: Add a new HTML tag (10 minutes)

Every tag is one row of `TAG_TABLE[]` in SECTION 7:

```c
typedef struct TagInfo {
    const char  *name;            /* lower-case tag name */
    unsigned    flags;           /* TAGF_* kind-of-tag bits */
    float       font_scale_em;   /* heading font scale (1.0 = normal) */
    const char *margin_top_em;   /* default margins, e.g. "1em"; NULL = none */
    const char *margin_bottom_em;
    float       margin_right_px; /* e.g. buttons reserve 8px of line space */
    TagParseFn  parse_full;      /* rare: replace the whole parse step */
    TagSetupFn  setup;           /* common: customize the node while parsing */
    TagDrawFn   draw;            /* optional: custom painting */
} TagInfo;
```

**Level 1 — just a name.** Add `{ "aside", 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL, NULL },`.
`<aside>` now renders like a generic block: CSS classes, ids, style, borders,
backgrounds all work automatically because the generic parser already handles
all of that for every tag.

**Level 2 — with a setup hook.** If the tag carries special attributes, write
a small setup function (SECTION 8 has the real examples: `tag_setup_a`,
`tag_setup_button`, ...):

```c
static void tag_setup_meter(TagCtx *c, CwvNode *n) {
    char maxv[32] = {0};
    /* c->q .. c->gt is this tag's attribute text, use find_attr on it */
    if (find_attr(c->q, c->gt, "max", maxv, sizeof(maxv)))
        n->w = (float)atof(maxv);          /* or store it on the node */
}

/* and in TAG_TABLE[]: */
{ "meter", 0, 1.0f, NULL, NULL, 0.0f, NULL, tag_setup_meter, NULL },
```

The hook runs AFTER style/id/class and CSS rules are applied, so CSS still
wins over your defaults — copy that order from the existing hooks (they use
`if (!n->has_border)` style guards so CSS can override them).

**Level 3 — custom visuals.** Give the tag a `draw` hook:

```c
static void tag_draw_meter(TagDrawCtx *ctx) {
    /* Everything you need is in ctx: r, n, sx/sy/sw/sh (screen box),
       z (zoom), baseline, text_color. Vita2D calls paint here. */
    float fill_w = ctx->sw * 0.75f;
    vita2d_draw_rectangle(ctx->sx, ctx->sy, fill_w, ctx->sh, rgba(70,105,150,255));
}
```

`draw` fully replaces the default text painting for that tag (background,
border and focus ring are still painted by `draw_node()` before your hook
runs). Draw hooks are also where you would put a JS-driven canvas later.

**Flags cheat-sheet:**

- `TAGF_VOID` — no closing tag, no children (like `<img>`)
- `TAGF_IGNORED` — skip the whole subtree (like `<script>`)
- `TAGF_HEADING` — bold + keeps its own font size when nested
- `TAGF_INLINE` — default `display:inline` (like `<span>`)
- `TAGF_INLINE_BLOCK` — default `display:inline-block` (like `<button>`)

**The rare one:** `parse_full` is for tags that manage their own recursion
(`<html>`, `<body>`, `<img>`). Look at `tag_parse_img()` — it's ~40 lines and
the simplest real example. You will know if you need it.

**One-line text field:** `<input type="text">` (or `type="password"`) is the
supported single-line text control — it takes IME focus on the device, shows
a caret and placeholder, and JS reads/writes it with `getValue()` /
`setValue()`. It replaces the old `<textfield>` tag (which the parser no
longer recognizes: change `<textfield>` to `<input type="text">` in your
pages). `<textarea>` remains out of scope for now.

## Recipe 3: use CSS to draw what you can't code

`border-style: double` and dashed/dotted borders, text-align center,
`overflow: hidden|visible|scroll`, `display: none|inline|inline-block`,
width/height, margins, padding — it's all already supported per-tag AND per
CSS selector (`#id`, `.class`, `tag`, `tag.class`, `tag#id.class`, and
`:active` for VCM bindings). Check `CSS_PROPERTIES[]` for the current list
before writing code — CSS is cheaper than C.

## Proving you didn't break anything (the golden test)

The `desktop_test/` folder that ships with this refactor contains a tiny
desktop harness that compiles the Vita code on a normal PC (Linux/mac/gcc)
with stubbed Vita libraries, renders five sample pages, and prints a full
dump of every parsed node plus every draw call:

```bash
cd desktop_test
./build_and_compare.sh old.c new.c   # diffs the two, prints PASS/FAIL
```

If the diff is empty, your refactor is behaviorally identical — same layout
coordinates, same pixels, same control interactions. It also feeds pointer
events, focus cycling, select popups and VCM activation through the pages so
interaction code is covered too. Run it after every change you make.

Notes:
- The stub fonts use `width = strlen * size * 12` (like the Vita fallback
  path), so numbers differ from real hardware — but they differ *identically*
  for old and new code, which is what matters for regression testing.
- Real-hardware QA is still worth doing before shipping a VPK: the harness
  cannot test the IME dialog or real font metrics.

## Layout invariants (the rules the flow engine now follows)

Three invariants were added while fixing container/margin bugs. If you touch
`cwv_html.c`, keep these true:

1. **`CwvNode.depth`** counts nesting *flow contexts*, not DOM elements.
   The parser stamps it (`n.depth = r->parse_depth`) and increments
   `r->parse_depth` only for elements that create a flow context — a block
   or a painted box (see the `flow_context` variable in `parse_html_range`).
   Box-less inline elements (`<a>`, `<b>`, plain `<span>`) do NOT open a
   level, so their children stay direct children of the surrounding block,
   exactly like CSS inline flow.
   `layout_block()` uses `depth` to tell a box's direct children
   (`depth + 1`) from deeper descendants; margin collapse/containment rules
   (first/last child) consider only BLOCK-LEVEL direct children, and the
   last-child margin is contained (not collapsed out) whenever the parent has
   padding or a border — that is what fixed div backgrounds painting one
   line short and cards sized wrong.
2. **Page insets**: `r->page_left / page_right / page_top` replace the old
   hardcoded 12px. They come from `<body>` margin + padding (CSS or inline
   style, via `tag_parse_body`), clamped horizontally to a 12px Vita
   safe-area minimum; the top offset applies as written. All layout code
   reads these instead of literal `12.0f` / `24.0f`.
3. **Inline-block controls** (`layout_inline_block`) measure shrink-to-fit
   width from TEXT nodes, images and nested non-block boxes — never from a
   block child box (a `<p>` inside a `<button>` carries the full flow width,
   which used to make buttons overflow the screen). Child placement stacks
   block children on new lines (a button with `<span>` + `<p>` content shows
   two lines, like a browser) and honors the same contained-margin rule.

## Limits to keep in mind (fixed-size memory)

`VW_MAX_NODES` (512 layout nodes), `VW_MAX_RULES` (256 CSS rules),
`VW_MAX_HTML` (256 KB), `VW_MAX_TEXT` (4 KB per text run). Pages that exceed
these are truncated, not crashed — but a page can silently lose elements, so
bump the limits if a busy page looks wrong.

## JavaScript support (implemented!)

The renderer runs a real JavaScript engine: Duktape 2.7.0 (MIT license,
https://duktape.org), vendored as three files in `src/backend/js/` - no
dependencies, compiles anywhere C99 does, including the Vita. The glue lives
in `cwv_js.c` and follows the same table pattern as CSS and HTML.

### How scripts run

1. `backend_load()` parses the page (script tags are skipped by the parser),
   then `js_harvest_and_run()` collects `<script>...</script>` blocks and runs
   them. The JS context is only created if the page actually has scripts -
   script-free pages behave exactly like before (proven by the golden test).
2. `backend_update()` calls `js_tick()` each frame: due `setTimeout` /
   `setInterval` callbacks run there.
3. Activating a button/link (DPAD, tap, or Vita control-modal combo) calls
   its JS "activate" listener; for links and the exit request the listener
   REPLACES the default behavior.

### The JS API

    console.log/info/warn/error(...)      ring buffer (see below)
    document.getElementById("id")        element or null
    document.querySelector("sel")        element or null
    document.querySelectorAll("sel")     array of elements
    document.location.assign(url)        navigate
    el.textContent = t / read           read/write text content
    el.value = v / read                  same, for <input>/<textarea>/<select>
    el.style.cssText = "color: red; ..." uses the CSS_PROPERTIES[] table
    el.className = "cls"                 set class + re-apply CSS rules
    el.hidden = true / false            display:none toggle
    el.addEventListener("click", fn)     activation listener
    el.removeEventListener("click", fn) detach an activation listener
    setTimeout/setInterval/clearTimeout/clearInterval
    fetch(url, callback)                 network GET, see below
    window.innerWidth / window.innerHeight
    (plus full ES5.1: JSON, Math, RegExp, Date, ...)

The element API is the STANDARD DOM surface (textContent, value, hidden,
className, style.cssText, addEventListener/removeEventListener). The early
custom helpers (setText/setStyle/setClass/hide/show/setActive, on("activate"),
document.controlMode/onExit/reload) were removed, so pages written for this
renderer run anywhere - and standard-DOM pages run here unmodified.

**fetch(url, callback) - network for scripts:**

```js
fetch("https://api.example.com/data.json", function (status, text) {
    if (status === 200) {
        var data = JSON.parse(text);
        document.getElementById("out").textContent = data.title;
    } else {
        /* 404, timeout, no Wi-Fi, plane mode: -1 or the HTTP status.
           fetch NEVER crashes the app and never throws. */
        document.getElementById("out").textContent = "offline";
    }
});
```

- The callback ALWAYS runs: `status >= 0` is the HTTP status code with the
  response body in `text`; `status === -1` means failure (no internet, bad
  URL, timeout, or this host has no network backend - e.g. the desktop
  tests). `text` is `""` on failure.
- The return value is `true` (request completed) or `false` (failed), so
  scripts can branch without a callback.
- v1 limits: synchronous (the app pauses until the request finishes;
  timeouts are 10 s send / 15 s receive), http/https GET only, response
  body capped at 256 KiB, no POST/headers.
- This is DATA access, not browsing: the main project never renders remote
  pages. Full browsing is the planned separate "Vita WebView Online"
  project.
- Implementation: `cwv_host_fetch_url()` in `cwv_host.c` does the
  sceHttp work; the renderer side is `js_fetch()` in `cwv_js.c`, and
  hosts register the backend with `cwv_backend_set_fetch_hook()`.
  The sceHttp API lives in `<psp2/net/http.h>` in the VitaSDK (older SDK
  snapshots may name it `psp2/http.h`); the include is auto-detected with
  `__has_include`, and if an SDK has NEITHER header, fetch() support is
  compiled out - the app still builds, and page fetch() calls just report
  failure via the graceful (-1, "") path.

**Open URL (main menu):** the main-menu row "open-url" opens the Vita
keyboard PRE-FILLED with the current page as a small relative reference
(`./editor.html`) - not a giant `file://` string. Input follows HTML
relative-reference semantics, resolved against the CURRENT page's
directory:

| you type | loads |
| --- | --- |
| `./x.html` (or bare `x.html`) | `x.html` next to the CURRENT page |
| `../x.html` (repeatable `../../`) | one directory up per `../` |
| `web/pages/x.html` | app web root: `app0/web/pages/x.html` |
| `app0/...` / `file:///app0/...` | app root / full URL as-is |

Remote `http(s)://` URLs are refused with a console error (this app is a
webapp wrapper, not a browser - scripts can use `fetch()` for network
data). A wrong path logs an error to DevTools > Console and leaves the
app running on the current page.

**About element objects:** every `getElementById`/`querySelector` call
returns its OWN element object that remembers which node it points at.
Grab a handle once, keep using it:

```js
var out = document.getElementById("out");
out.textContent = "hello";   /* still the same element later */
```

A handle stores the node's index, so after `document.location.assign()`
(the page re-parses) old handles go stale — look them up
again after navigating. (Before 0.4.4.14 every lookup returned one shared
object that pointed at whichever element was looked up LAST; that is fixed
and pinned by `desktop_test/es2015_test.c`.)

### Recipe 4: add a new JS function (one row, like CSS properties)

Write the function in `cwv_js.c`:

```c
static duk_ret_t js_my_thing(duk_context *ctx) {
    CwvRenderer *r = js_get_renderer(ctx);   /* the page */
    const char *s = duk_to_string(ctx, 0);       /* argument 0 */
    /* ... do something ... */
    duk_push_int(ctx, 42);                       /* return value */
    return 1;                                    /* how many values pushed */
}
```

Add one line to the matching registry (`JS_GLOBAL_METHODS[]` for global
functions, `JS_DOCUMENT_METHODS[]` for `document.*`, `JS_ELEMENT_METHODS[]`
for element methods, `JS_CONSOLE_METHODS[]` for `console.*`):

```c
static const JsFn JS_DOCUMENT_METHODS[] = {
    ...
    { "myThing", js_my_thing },
};
```

### Recipe 5: Vita button combos -> :active styling and JS

Nothing new to build - it is the existing VCM system plus the JS layer:

- give an element a control-modal binding: `<div id="panel" vcm="cross">`
- style the pressed state with the existing `:active` selector support:
  `#panel:active { background: #ff0000; }`
- and/or run page script when the combo fires:
  `document.getElementById("panel").addEventListener("click", fn)`
- the :active PAINT is native: while a VCM combo is active, draw_node()
  applies the page's `:active` rules through a temporary node copy, so no
  script is needed for the styling to light up (or revert).

### Recipe 6: the START controller button

START is page input, not an application-exit shortcut. On supported controller
ports, `window.CwvInput.buttonDown("START")` and
`window.CwvInput.buttonUp("START")` are emitted while CWMenu is closed. Use
those events to implement application-specific behavior. Normal application
exit remains the responsibility of the platform/homebrew environment; the
removed `document.onExit` hook is not part of the public API.

### Recipe 7: the DevTools console

The JS console is a fixed 64-line ring buffer in the renderer. The DevTools
native menu can display it with the new public functions (call them directly
like `cwv_backend_default_ops()`):

| CWMenu row (devtools_console) | call |
|--------------------------|------|
| "Last error"             | `cwv_backend_js_last_error(backend)` -> last error line or "" |
| "Runtime log"            | `cwv_backend_console_dump(backend, buf, cap)` -> all lines |
| "Console status"         | `cwv_backend_js_active(backend)` -> 1 = JS running |

Refresh the rows each frame while the devtools console menu is open, the
same way the Performance page reads its stats.

### External files (automatic - no recipe needed)

- `<script src="app.js"></script>` loads the file with the same resource
  loader as images (file:/// inside the VPK, or http(s) via the curl cache)
  and runs it through the same path as inline scripts.
- `<link rel="stylesheet" href="style.css">` was already wired to the CSS
  parser; both are now covered by the desktop tests (js3.html).
- ES modules: see the ESM section below - `import`/`export` inside
  scripts are handled by the transformer, not by the core.

### Recipe 8: ESM / modern-JS support - the built-in transformer

WHY A TRANSFORMER AND NOT AN ENGINE PATCH: Duktape 2.7.0 is the final upstream
release (the project is dormant), and no ES6 parser was ever shipped for it.
Forking duk_lexer.c / duk_compiler.c for let/const scoping, arrow functions
and template literals would mean rewriting the compiler's binding and
function-instantiation model - months of work, a permanent fork, and every
future engine fix lost. So modern JS is split by KIND:

- SYNTAX (arrows, let/const, templates, import/export, for..of, classes,
  async/await, spread, destructuring, default parameters, optional
  chaining, nullish coalescing): converted per script at load time by
  `vw_es2015_transform()` in `cwv_es2015_core.c` - plain C, no SDK
  dependencies, shared by every port.
- LIBRARY (Array.find, String.padStart, Object.assign, Number.isInteger,
  Math.sign/trunc, Map, Set, and the later editions): provided DIRECTLY
  as a prelude evaluated into every fresh JS context - real functions in
  the engine, no conversion involved (`cwv_es_editions_prelude()`).

The core engine runs ES5.1 only, BY DESIGN. One provider exists on every
platform - the built-in registered by `cwv_host_enable_es_builtin()`
(sources/shared/sources/core/cwv_host.c):

```c
cwv_host_enable_ts_builtin(host);   /* optional: the TypeScript stripper */
cwv_host_enable_es_builtin(host);   /* transformer + library prelude     */
```

ALREADY WIRED in this source tree: all four ports (Vita, Wii U, Wii,
Windows) call the two lines above at boot, then run
`cwv_host_esm_selftest()` - a canary page evaluated through the whole
chain, whose verdict shows up in DevTools > Platform, the debug overlay
and the runtime log. The former CwvESM.suprx runtime module was REMOVED (2026-10-03): it kept
a second copy of the transformer in RAM (the fallback had to be linked into
the eboot anyway), slowed every boot by a module load, and on Vita3K its
exported NIDs were silently stubbed, so pages ran raw ES6 while the UI
claimed ESM was on. CwvESM is now one integrated shared pipeline everywhere,
with a single code path to test.

Performance (the fast paths, 2026-10-03): the transform used to pay
for every script - even plain ES5 pages - a full import/export scan, a
class/async scan AND an 8x-source-length heap copy. Now each phase is
skipped when the keywords it rewrites are absent from the source
(plain substring checks; a keyword inside a string or comment only
makes the pass run as before, which is harmless). A script without
`import`, `export`, `class`, `async` or `await` costs one linear scan
and zero allocations.

What it converts (every script source: inline, external, eval, import):

- `let` / `const`                      -> `var`
- `(a, b) => body` / `x => body`      -> `function (a, b) { ... }`
- `` `text ${expr} more` ``            -> `("text " + (expr) + " more")`
- `for (const x of list)`             -> indexed loop (list captured once;
                                          arrays and strings)
- `import "./a.js";`                   -> load + run a.js first, strip line
- `export function f() {...}`         -> `function f() {...}`
- `export const/let/var/default`      -> plain declaration
- classes, async/await, spread, destructuring and default parameters
  are handled by the ES2023/ES2024 edition passes (see
  `cwv_es_editions.c` and the edition library files).

Module model: files are loaded in dependency order into one shared global
scope, and every export is simply a global for importers - the pragmatic
fit for an offline app packager. for..of over arrays and strings IS
converted; for..in is real ES5 and never touched.

**Example page:** `test_pages/es6_demo.html` is a ready-to-run ES6 demo —
drop it into your `webapp/` directory and open it in the app. It uses
arrows, template literals, for..of, Map and Set, with three buttons whose
listeners update the UI. `desktop_test/es2015_test.c` loads the same page and
pins its behavior (button counting, the scan output, reset), so any change
that breaks the demo breaks the test.

**When modern-JS scripts fail (troubleshooting):** the transformer must
be ENABLED for the conversion to run. If it is off, raw ES6 reaches the
engine and every `let`/`const`/arrow triggers "unterminated statement" or
"unexpected token" errors - the classic symptom being the SAME errors on
every load. Three built-in signals tell you what happened:
1. DevTools > Platform shows the ESM state (`ESM OFF` when the provider
   did not install - which should never happen on a normal boot).
2. A script that fails to compile while the transformer is off logs a
   `hint:` console line naming the construct.
3. `cwv_host_js_last_error()` / the runtime log record the raw engine
   error plus the transform status.
Windows-edited files (CRLF line endings) are fine - the transformer
handles both LF and CRLF; that exact bug (a `\r` at a line-comment end
swallowing the next line's keyword) was found and fixed, with regression
tests in `desktop_test/es2015_unit`.

- No DOM re-parsing: `createElement`/`innerHTML` are not available (the
  parser builds the tree once per page load; `el.textContent`/`style`/
  `hidden` mutate the existing nodes instead).
- `el.hidden`/`textContent` on a box element also updates its direct text
  content (see `js_text_owner` in cwv_js.c for the heuristic).
- The JS engine adds one malloc'd context per scripted page (roughly
  300-500 KB heap while scripts run); script-free pages allocate nothing.


## DPAD tab stops and X activation (the input contract)

DPAD UP/DOWN cycles focus through the page's "tab stops" (like pressing
Tab in a browser). CROSS activates the focused stop. This is the full
list of what is selectable and what X does to it:

| Element                        | Tab stop?                    | X does                                          |
|--------------------------------|------------------------------|-------------------------------------------------|
| `<button>`                     | yes                          | fires JS click listeners + pressed visual       |
| `<select>`                     | yes                          | opens the native picker                         |
| `<input type="text/password">` | yes                          | opens the IME keyboard                          |
| `<a href="...">` (any layout)  | yes                          | fires click listeners, then navigates (cancel with `event.preventDefault()`) |
| `<a href="#frag">`             | yes                          | fires click listeners only (no reload)          |
| `<a>` without href             | no                           | -                                               |
| `<img tabindex="N">`           | only with explicit tabindex  | fires its click listeners, if any               |
| plain div/span/p/td etc.       | no                           | -                                               |
| disabled or display:none       | never                        | -                                               |

`tabindex="N"`: positive values are visited first (lowest to highest),
everything else follows document order; `tabindex="-1"` removes an
element from the cycle.

Control modes (the `cwv-control-mode` attribute on `<html>`):
- browser: X activates, CIRCLE = browser back;
- standalone: X/CIRCLE fire the page's `vcm="cross"/"circle"` bindings;
- touch: no DPAD focus, everything is taps;
- default/custom (no attribute): page JS receives raw CROSS/CIRCLE etc.
  button events as before, AND X additionally activates the focused
  stop - so every tab-selectable element is X-clickable in every mode.

Links whose click listener must NOT navigate: call
`event.preventDefault()` inside the listener - exactly like Chrome.

## About CWMenu

CWMenu files (`cwmenu/default.cwm`) are the app's own DSL for the native menu
system - button rows, tabs, scrollbars, actions like `devtools`, `settings`,
`console`. That side lives in main.c / cwmenu_api.c (NOT in the renderer).
The renderer meets CWMenu at two attributes: `cwv-control-mode`
(browser/standalone/touch) and `vcm="buttonname"` / `cwv-control-modal`
(the on-screen control modal). The DSL itself is unchanged by the renderer
refactor; new menu rows (like the exit-redirect toggle) are plain CWMenu
`element` rows whose actions call the functions documented above.
Full CWMenu 2 reference (theme block, window keys, dual windows, dynamic app
menus, versioning): see `docs/CWM_SCRIPT.md`.
