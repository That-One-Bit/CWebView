# Rendering audit

The current language-separated inventory is [Supported methods and language
features](SUPPORTED_METHODS.md), grouped under HTML, CSS, JS-TS and WASM.

The following is the historical September 18 audit. Its gaps and proposed
additions are not a current feature list; use the linked live-registry inventory
and regression pages when checking current support.

# Rendering audit: what the native renderer supports today

Written 2026-09-18 against `src/backend/cwv_html.c` (TAG_TABLE),
`src/backend/cwv_css.c` (CSS_PROPERTIES) and the layout engine.
This is a checklist for anyone adding HTML/CSS support: what works, what
silently degrades, and which additions give the most value per effort.

Spec pointers use the WHATWG HTML Standard and MDN; both match browser
behavior and are the reference this renderer copies where it can.

## 1. HTML tags

Rendered with real behavior:

| Tag | Notes |
| --- | --- |
| `html`, `body` | document root; body margins feed `page_left/right/top` |
| `h1`..`h6` | heading scale 2.00x..0.67x, bold, em-based UA margins |
| `p` | 1em top/bottom UA margin |
| `span`, `strong`, `em`, `b`, `i`, `small`, `label` | inline |
| `a` | inline link: underline, href + cwv-target, focus/touch |
| `button` | inline-block control, VCM aware, margin-right 8px |
| `input` (text/password), `textarea`, `select`, `option` | IME + native modal controls |
| `img` | void, own layout flow |
| `br`, `hr` | void |
| `meta`, `link`, `base`, `col`, `area`, `embed`, `param`, `source`, `track`, `wbr` | void, no visual box |
| `script`, `style`, `head`, `title`, `noscript`, `template` | TAGF_IGNORED, subtree skipped |

**Everything else falls through to a generic block.** `tag_info()` returns
NULL for unknown tags and `node_defaults()` gives them block display with
no margins. That means `div`, `ul`, `ol`, `li`, `table`, `header`,
`footer`, `section`, `nav`, `article`, `pre`, `code`, `blockquote - DONE 2026-09-19` all
*render* (children paint in flow) but get **no user-agent styling**:
no list bullets, no indentation, no monospace font.

## 2. CSS properties

`CSS_PROPERTIES[]` in `cwv_css.c` handles: color, background(-color),
border (+width/style/color), font-weight, font-size, font-family, text-align,
overflow / overflow-x / overflow-y, display (block/inline/inline-block),
width, height, margin(-top/-bottom), padding(-top/-left/-right/-bottom).

2026-10-05 additions (Chrome-parity batch): opacity (composited against
the page via `mix_rgba`), visibility:hidden (layout kept, hit-testing
off), text-decoration (underline/line-through, with the `<u> <ins> <s>
<strike> <del> <mark>` UA defaults), text-transform
(uppercase/lowercase/capitalize), letter-spacing / word-spacing,
line-height (factor on the 28px line box), white-space
(nowrap/pre/pre-wrap), min-width / max-width, box-sizing
(content-box/border-box), and percentage widths - `width:80%` resolves
against the containing block at flow entry instead of misreading as
80px, and a CSS width now NARROWS the block's content flow so the text
wraps inside the painted box.

## 2a. The full `<img>` audit (2026-10-05, 00.10)

Verified sound: magic-byte decoder dispatch (PNG/JPEG/BMP, extensionless
URLs and query strings), PNG preflight + 4096px GPU cap, the 24-entry
per-page texture cache (group-freed on page leave and shutdown, refuses
instead of evicting so no node can hold a dangling texture), broken-image
placeholder + compiled-in glyph + alt text, link-wrapped images (the
anchor's href is inherited for hit-testing), object-fit
(contain/cover/fill), image-rendering (pixelated/auto filter sync),
aspect-preserving size derivation and the focus/tabindex model.

Fixed by the audit:
- **Oversized-image clamp was 320px, not the content width.**
  `content_w` used `page_right - page_left` (margin minus margin ~= 0),
  so every oversized image clamped to the 320px fallback. Now the real
  flow width (viewport minus margins) is used, like Chrome.
- **`width:50%` on an image** collapsed to 1px once percentage widths
  landed; now resolved against the same content width.
- **`data:` URI images** (base64 payloads, the canvas.toDataURL form)
  now decode inline - no file or network round trip.
- **`%XX` escapes in local paths** percent-decode like Chrome on a
  file: URL ("sp%20ace.png" opens "sp ace.png").

Known gaps, deliberately out of this batch: GIF/WEBP/SVG decoding (needs
per-port decoders), `srcset`/`loading=lazy`, `new Image()` from JS
(scripting layer, not the `<img>` parser), percent-encoded
(non-base64) `data:` URLs, and percentage heights (block heights are
content-driven in this engine).

Everything else in a `style=` attribute is silently ignored (unknown keys
don't error). The same parser backs stylesheet rules, so the gap applies
to `link` stylesheets too.

## 3. Suggested additions, ranked by value per effort

Each is a TAG_TABLE / CSS_PROPERTIES row plus a small hook - the two
registry tables are the designed extension points (see
EXTENDING_THE_RENDERER.md).

### Easy wins (a few lines each)

1. **`div`, `header`, `footer`, `section`, `nav`, `article`, `aside`, `main`**
   - Add explicit TAG_TABLE rows (block, no margins). Behavior is
     identical to today's fallthrough, but the tags stop being "unknown"
     and get a place to attach future hooks. Spec: WHATWG 4.3 sections.
2. **`blockquote`** - default margin-left 40px + 1em vertical.
   Spec: HTML LS 15.3.3 "the blockquote element" UA sheet.
3. **`border-radius`** (CSS) - the renderer already draws rounded rects;
   route the value into the box painter. Spec: CSS Backgrounds & Borders 5.
4. **`max-width` / `max-height`** (CSS) - clamp in the same place width /
   height apply. This is what most responsive pages rely on.
   Spec: CSS2.1 10.4 min/max widths.

### Medium (an afternoon each)

5. **Lists: `ul`, `ol`, `li`** - `li` gets `padding-left` (~24px) plus a
   marker box; `ol` numbers via a per-parent counter. Spec: MDN "Styling
   lists"; CSS 2.1 12.5 list-style-type. Start with disc/decimal only.
6. **`pre` / `code`** - `pre`: preserve internal whitespace/newlines in the
   text run (today everything collapses); `code`: monospace font-family.
   Spec: HTML LS 4.5.12, CSS Text 4 white-space.
7. **`input` checkbox / radio** - new draw hook (box / circle, checked
   state), no IME. Reuses button_pressed state. Spec: WHATWG 4.10.5.
8. **`line-height`** (CSS) - multiplies the row height in the text wrapper.
   Spec: CSS2.1 10.8.1.

### Bigger projects (deliberately not scheduled)

9. **Flexbox (`display: flex`)** - a full second layout algorithm.
   Only worth it after easy/medium items ship; keep block layout the default.
10. **`<table>` layout** - needs a three-pass column algorithm; the
    fallthrough (generic blocks) is usually readable enough today.

## 4. Rules of the road

- Unknown tags must keep falling through to generic blocks (never drop
  content) - HTML LS: browsers never hide unknown elements.
- Every new property gets a `css_prop_*` handler row, never an `if`
  buried in `style_apply`.
- Golden test (`desktop_test/check_reference.sh`) must stay zero-diff
  unless a new feature intentionally changes reference pages.
