# Currently supported methods and language features

Updated 2026-10-01 from the live shared-source registries. A registered name
is not a promise of full browser behavior. Desktop tests do not certify Vita3K
or hardware execution of an arbitrary application.

## HTML

Registered tags: `svg` (+ `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`, `path`, `text`, `g`; `defs`/`title`/`desc` ignored), `html`, `body`, `img`, `h1`, `h2`, `h3`, `h4`, `h5`, `h6`, `p`, `span`, `a`, `strong`, `em`, `b`, `i`, `small`, `label`, `ul`, `ol`, `li`, `blockquote`, `button`, `input`, `textarea`, `select`, `option`, `area`, `base`, `br`, `col`, `embed`, `hr`, `link`, `meta`, `param`, `source`, `track`, `wbr`, `script`, `style`, `head`, `title`, `noscript`, `template`.

See `sources/shared/sources/web/html/cwv_html.c:TAG_TABLE` for per-tag behavior. Structural,
inline, control, void and ignored tags have different handling. Script/style
processing is performed separately by the backend. Unrecognized elements
fall back to generic layout; this does not implement their browser semantics.
Inputs, select controls and the CWV textfield alias use native platform UI.

## CSS

Registered properties: `position` (`relative`/`absolute`/`fixed`; `sticky` paints as relative), `top`/`right`/`bottom`/`left`, `color`, `cursor`, `background`, `background-color`, `background-image`, `border`, `border-width`, `border-style`, `border-color`, `font-weight`, `text-align`, `overflow`, `overflow-x`, `overflow-y`, `display`, `width`, `height`, `margin-left`, `margin-right`, `margin-top`, `margin-bottom`, `margin`, `padding-left`, `list-style-type`, `list-style`, `padding-right`, `padding-top`, `padding-bottom`, `padding`, `font-size`, `font-family`, `z-index`, `object-fit`, `image-rendering`, `text-decoration`, `text-transform`, `letter-spacing`, `word-spacing`, `line-height`, `white-space`, `opacity`, `visibility`, `min-width`, `max-width`, and `box-sizing`.

The shared painter applies `background-repeat`, `background-position`, `background-size`, `background-attachment`, `background-clip`, and `background-origin` for a single `url()` background layer. See `docs/CSS_AUDIT.md` for the supported subset and known differences. Gradients, multiple background layers, edge-offset position syntax (for example `right 12px bottom 4px`), and `background-repeat: space/round` are not implemented. Percentage offsets and box lengths that require a containing block use a documented viewport approximation where the current layout pipeline cannot defer resolution.

See `sources/shared/sources/web/css/cwv_css.c:CSS_PROPERTIES` for accepted values. Selector coverage is the engine's subset, not full browser CSS. Flexbox, grid, sticky positioning, and arbitrary browser layout behavior are not implemented by merely accepting those property names. Unknown declarations are ignored. GPU textures are limited to 4096 per dimension; PNG dimensions and container lengths are checked before native decoding.
Per-platform image decoding: PNG/JPEG everywhere (Vita libvita2d, Wii
GRRLIB, Wii U SDL2_image, Windows WIC); BMP on Vita/Wii U/Windows, and on
Wii through the shared cwv_bmp decoder (2026-10-01) - before that, BMP
silently failed on Wii. WebP/GIF/TIFF decode only on Windows (WIC). Raw formats remain fully
supported on every port for compatibility; a successful runtime decode of
a raw format logs a one-line note recommending the `.cwi` container
(pre-decoded, zero-decode loads) - it is a hint, never a blocker
(2026-10-06 (77)).

Per-element scroll regions (2026-10-06 (75)): any box (div, section, the
`<svg>` box, form controls) with explicit `overflow`/`overflow-x`/
`overflow-y` (`auto` or `scroll`; `hidden` clips without scrolling)
becomes a scrollable region when its content overflows. Regions join
the tab/DPAD focus chain like controls (positive `tabindex` orders
first, `tabindex="-1"` never enters, exactly the Chrome focus rules);
focusing a region scrolls its content first - leftover movement reaches
the page (the nested-scroll-container rule). Focusing a control or link
inside a region reveals it in the scroll window. Content past a region's
edge is clipped (port `cwv_set_clip`/`cwv_clip_disable` seam), and
region-clipped content never widens the page's scrollable area, like a
real browser. While a region holds focus it shows its own scrollbars;
the page keeps its bars. Regions nest: each clips against its parent's
window.

## CSS units

`px`, bare numbers, `em`, `rem`, `%` (where percentages apply), and the full
absolute/viewport family: `vw`, `vh`, `vmin`, `vmax`, and the
small/large/dynamic viewport units `svw`/`svh`/`lvw`/`lvh`/`dvw`/`dvh`
(all resolve to the full viewport on a console - there is no browser
chrome to shrink it), plus physical units `in`, `cm`, `mm`, `q`, `pt`, `pc`
(96 reference pixels per inch, Chrome's scale). The viewport for `v*`
units is the real screen size per port (`cwv_css_set_viewport` at load).

## Inline SVG

Static inline `<svg>` subset (2026-10-05 (67)): `width`/`height`/`viewBox`
attributes on the root (viewBox scales children with uniform scale,
preserveAspectRatio="none"-style), shape elements with `fill`/`stroke`/
`stroke-width` presentation attributes, path commands `M m L l H h V v
C c S s Q q T t A a Z z` (curves flattened at parse time; `A`/`a` arcs
draw as a straight chord). Not implemented (documented gaps): CSS-styled
SVG internals, SMIL, transforms, gradients, masks, external
.svg files as img/CSS sources, JS `createElementNS`, per-shape hit
testing (the `<svg>` box is one hit target).

`<text>` (2026-10-06 (73)): `x`/`y` locate the BASELINE (no ascent
correction needed), `fill` colors the glyphs (default black, like
Chrome), `font-size` in px (clamped to 16px..200px; the resolved page
font renders it, so @font-face applies). Text between `<text>` and the
next SVG element is the element's content and folds into the shape node
at parse time - it never enters the page's text flow, focus order, or
hit tests (stray runs before any `<text>` are dropped, like Chrome drops
orphan svg text). Stroked text and `<tspan>` positioning are out of the
subset.

Hostile-geometry hardening (2026-10-06 (76)), the same lessons the
`<img>` decoder fuzz taught: non-finite attributes (`NaN`/`inf`, which
float parsing accepts and every `<=` sanity check then lets through)
fall back to defaults; the `<svg>` box is capped at 4096px per side;
baked shape coordinates are clamped to +/-16384 so scanline fills and
page extents stay bounded (no hangs, no runaway scrollbars); and a
shape whose points overflow the shared point arena is rejected WHOLE -
never drawn truncated - matching the CWI container rule that a truncated
payload is invalid, not valid-with-less. Locked by
`desktop_test/testdata/svg_hostile.html` in the golden harness.

## JS-TS

### JavaScript host methods

Registered functions: `console.log`, `console.info`, `console.warn`, `console.error`, `document.getElementById`, `document.querySelector`, `document.querySelectorAll`, `element.addEventListener`, `element.removeEventListener`, `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`.

Additional bindings include `fetch` (host-backed HTTP(S) GET, not complete
browser Fetch), `document.location.assign`, element textContent/value/style.cssText
accessors and window viewport values. See `sources/shared/sources/web/js/runtime/cwv_js.c` for exact
behavior and event limitations.

Canvas 2D is an opt-in experimental subset (`cwv-args: beta-enable-canvas`). Dirty canvas frames reuse their existing texture where supported; resizing the canvas recreates the backing texture. The current optimization reduces per-frame allocation churn but still uploads the full backing bitmap when dirty.
The current context supports `getContext("2d")`, `fillRect`, `strokeRect`,
`clearRect`, `beginPath`, `moveTo`, `lineTo`, `closePath`, `rect`,
`quadraticCurveTo`, `bezierCurveTo`, `arc`, `save`, `restore`, `fill`, and `stroke`; properties are
`fillStyle`, `strokeStyle`, `lineWidth`, and `globalAlpha`. Curves are approximated
with line segments, and transforms, text, gradients, patterns, image drawing,
pixel APIs, clipping paths, full compositing semantics, WebGL,
workers and complete browser DOM coverage are not supplied. See
`docs/CANVAS_VITA_TEST.md` for the shared implementation, platform upload paths,
and a scrolling/redraw hardware test.

### Language/runtime handling

Duktape 2.7.0 supplies the underlying ES5.1 runtime. The shared built-in transformer handles tested modern
syntax through the shared transform and runtime-prelude pipeline: let/const,
arrows, templates, import/export, for-of, destructuring, default/rest parameters,
spread, class constructors/methods/accessors/fields/inheritance, basic private
names, async/await forms, exponentiation, optional chaining, nullish coalescing
and logical assignment. The prelude supplies selected library facilities such
as Promise, Map, Set, Object.assign and string/array helpers.

Source organization (2026-10-01): the language layer is SEPARATED BY
EDITION YEAR (ES####). cwv_es2015_core.c is the ES2015 syntax transform
engine (later-edition SYNTAX passes live there until each gets its own
module); the library prelude is one file per spec edition:
cwv_es2016_lib.c .. cwv_es2024_lib.c (each exposing
cwv_es<year>_lib_prelude()), concatenated in spec order by
cwv_es_editions.c and evaluated once at JS context creation.
Per-edition library coverage includes ES2016 Array.includes; ES2017
padStart/padEnd, Object.entries/values; ES2018 Promise.finally and the
rest-destructuring helpers; ES2019 flat/flatMap, fromEntries,
trimStart/trimEnd; ES2020 globalThis, Promise.allSettled; ES2021
Promise.any, replaceAll; ES2022 at/hasOwn; ES2023 findLast/findLastIndex
and the change-by-copy methods toSorted/toReversed/toSpliced/with;
ES2024 Object.groupBy, Map.groupBy, Promise.withResolvers.
Gates: desktop_test/build_es_editions_unit.sh (per-edition behavior,
normal + ASan/UBSan) plus the renamed ES2015 transformer gates.

The newest edition with implemented feature subsets is **ES2024 (library
level)**; SYNTAX conversion remains at the **ES2022 (ES13)** level via
class fields/private members. This is NOT full ES2022 conformance. Private names
currently map to ordinary properties without privacy or brand checks; module
imports use a shared global scope, and class/super/async behavior has documented
limits. Passing the 23 ES6, 21 ES7 and 22 class assertions does not prove that a
large browser application is supported.

### TypeScript (added 2026-10-01)

Inline `<script type="text/typescript">` (also `application/typescript`,
`module/typescript`) and external `src="*.ts"` scripts are stripped to
JavaScript by the shared stripper (`sources/shared/sources/web/typescript/cwv_ts_core.c`,
registered via `cwv_host_enable_ts_builtin`) BEFORE the ES6 transform
runs, so TypeScript and modern-JS features compose in one script.

Supported subset (valid TypeScript assumed; this strips types, it does
not type-check):
- Annotations on variables, parameters, return types, class fields;
  optional `?` / definite `!` markers; `let x!: T` definite assignment.
- `interface` blocks (dropped), `type` aliases (dropped), `declare`
  statements (dropped), `import type` / `export type` (dropped).
- `enum` / `const enum` / `export enum`: emitted as the classic JS
  object-literal pattern with numeric reverse mapping.
- Classes: visibility/`readonly`/`abstract`/`override` modifiers,
  constructor param properties (`constructor(private p: number)` ->
  `this.p=p` injection), accessor return types, method generics,
  `extends` with type arguments, `implements` (dropped), overload
  signatures (dropped), index signatures (dropped), static blocks.
- `as` / `satisfies` casts, non-null assertions `x!`, generic call
  sites in the attached form `f<number>(x)` (conservative: spaced
  `f < T > (x)` is never rewritten), `catch (e: T)` annotations,
  `this` parameters (dropped with their comma).
- Nested handling inside defaults, initializers, arrow bodies and
  method bodies (a `let`/arrow/`function` inside any of those still
  strips correctly); ternaries, object literals, labels, comparisons
  and regex literals are preserved by construction.
- Plain-JS scripts pass through untouched (the stripper reports
  "no TypeScript found" and the original runs).

NOT handled (reported to the console as an honest error, the script is
skipped, never silently mis-converted): namespaces/`declare module`,
decorators, `.tsx` JSX syntax, object-literal method params
(`{ m(a: T) {} }` - use named functions), type annotations inside
template-literal expressions, conditional `extends ? :` types, and
`declare module` augmentation. Gates: `desktop_test/build_ts_unit.sh`
(38 unit cases, normal + ASan/UBSan) and the TypeScript end-to-end
section of `desktop_test/build_error_audit.sh` (enum/class/arrow/cast
execution through the real engine).

## WASM

No WebAssembly runtime, WebAssembly browser API or WASI API is implemented.
WASM interpreter integration, AssemblyScript through compiled WASM, and an
explicit WASI host-function subset remain planned. Do not list their methods
as currently supported.
