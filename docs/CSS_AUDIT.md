# CSS property audit

This audit covers every declaration name registered in `sources/shared/sources/web/css/cwv_css.c` (the actual accepted-key registry), checked against the shared layout and paint consumers. Unsupported CSS names are ignored; this file does not claim full Chromium compatibility.

## Property status

| Property or group | Status | Behavior and limits |
|---|---|---|
| `color` | Implemented | Parses the renderer's supported named/hex/rgb colors; inherited text color is propagated. Full CSS Color 4 syntax is not supported. |
| `cursor` | Implemented subset | Common keywords and a single `url()` cursor with optional hotspot; platform ports determine which native cursor forms can be shown. |
| `background`, `background-color`, `background-image` | Implemented subset | Solid color plus one `url()` image. Gradients, multiple layers, and full shorthand grammar are unsupported. |
| `background-repeat` | Implemented subset | `repeat`, `no-repeat`, `repeat-x`, `repeat-y`; `space` and `round` still fall back to repeat and are not equivalent to Chrome. |
| `background-position` | Implemented subset | One/two simple keyword, percentage, or length values; edge-offset syntax and `calc()` are unsupported. |
| `background-size` | Implemented subset | `cover`, `contain`, one/two dimensions, and percentages; full intrinsic sizing rules are simplified. |
| `background-attachment` | Implemented subset | `scroll` and `fixed`; fixed layers are viewport-anchored and clipped to the element. Scroll-container edge cases may differ. |
| `background-origin`, `background-clip` | Implemented | Border/padding/content box choices are distinct and affect solid-color painting and image placement/clipping. |
| `position`, `top`, `right`, `bottom`, `left` | Implemented subset | Static, relative, absolute, fixed; inset percentages use the viewport as the containing-block approximation. `sticky` currently paints as relative, not browser sticky positioning. |
| `width`, `height`, `min-width`, `max-width`, `box-sizing` | Implemented subset | The default is CSS `content-box`; `border-box` is opt-in. Width percentages resolve against the available block width; height/min/max percentage resolution is approximated against viewport dimensions. `auto`/intrinsic sizing differs from full CSS. |
| `margin`, `margin-*`, `padding`, `padding-*` | Implemented subset | Shorthand ordering and pixel/relative lengths are supported. Percentages are resolved using viewport width as an approximation rather than each actual containing block's content width. Auto margins and margin collapsing are not implemented. |
| `border`, `border-width`, `border-style`, `border-color` | Implemented subset | Solid/dashed/dotted/double are represented; groove/ridge/inset/outset approximate to solid. Per-side borders and full shorthand grammar are not implemented. |
| `font-size`, `font-family`, `font-weight` | Implemented subset | Font size and loaded/fallback font selection work. Weight is binary regular/bold, not a full 100–900 family. Browser font metrics can differ by platform. |
| `text-align` | Implemented subset | Left/start, center, and right/end in the renderer's LTR layout; `justify` and full RTL direction handling are unsupported. |
| `text-decoration`, `text-transform`, `letter-spacing`, `word-spacing`, `line-height` | Implemented subset | Basic underline/overline/line-through, ASCII case transforms, spacing, and line-height multiplier. Decoration geometry and non-ASCII text transforms are approximate. |
| `white-space` | Implemented subset | `normal`, `nowrap`, `pre`, `pre-wrap`, and `pre-line` have distinct handling. Whitespace normalization is ASCII-oriented; full Unicode/CSS line breaking is not implemented. |
| `display` | Implemented subset | `none`, `block`, `inline`, and `inline-block`; flex/grid/table layout modes are not implemented. Unknown values do not create browser-equivalent layout. |
| `overflow`, `overflow-x`, `overflow-y` | Implemented subset | Hidden/scroll/auto/visible policy and renderer scroll regions are supported with platform clipping differences. Not every nested overflow combination matches Chrome. |
| `opacity` | Implemented subset | Node/ancestor opacity multiplies into the renderer's software color path; it is not a full isolated stacking-context compositor. |
| `visibility` | Implemented subset | `hidden` and `collapse` hide paint/hit tests while retaining layout; `inherit` preserves the copied parent state. |
| `list-style`, `list-style-type` | Implemented subset | Disc, circle, square, decimal, none; image/custom marker types and full list marker positioning are unsupported. |
| `object-fit` | Implemented subset | `fill`, `contain`, `cover` for image elements. `object-position` is not registered. |
| `image-rendering` | Implemented subset | `pixelated`/`crisp-edges` use nearest sampling; `auto` uses the port's normal filtering. |
| `z-index` | Implemented subset | Stable paint sorting by numeric z-index; full CSS stacking contexts and positioned-element rules are not modeled. |

## Changes in this audit

- Background layer fields were previously parsed but never consumed by the painter. The painter now applies repeat mode, image position and size, fixed attachment, origin, and clipping for a single image layer.
- `background-origin` previously called the `background-clip` handler and had no independent state. It now has its own handler and field.
- One-value `background-position: top` / `bottom` now positions the vertical axis and centers horizontally; other single values center vertically.
- Inset percentage values (`top`/`right`/`bottom`/`left`) now resolve against the relevant viewport axis rather than being treated as raw pixels.
- `text-align: right` / `end` now has a distinct right-aligned paint path instead of falling through to left alignment.
- `white-space: pre-wrap` and `pre-line` no longer collapse into `pre`; they wrap and normalize/preserve whitespace according to their supported semantics.
- Shorthand margin/padding percentages and longhand margin/padding/height/min/max lengths now use viewport-axis approximations instead of interpreting a percentage's numeric part as pixels. `box-sizing` now defaults to CSS `content-box`, with `border-box` as an explicit opt-in. This is still a known approximation for nested containing blocks, not exact CSS percentage resolution.

## Explicitly not claimed as supported

Flexbox, CSS Grid, sticky scroll positioning, gradients, multiple background layers, `calc()`, custom properties/variables, `@supports`, `@media` layout semantics, transforms, pseudo-elements, full font-weight ranges, `text-align: justify`, logical margin/padding properties, per-side borders, and full browser line-breaking/Unicode shaping are outside the registered property subset or remain incomplete. They should not be described as Chrome-compatible features.
