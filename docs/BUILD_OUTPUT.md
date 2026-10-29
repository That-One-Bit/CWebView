# Build script output (colors, metadata preview)

Internal notes for whoever paraphrases this into the end-user docs.
Implementation: `tools/cwv_term.sh`, sourced by `sources/build-vita.sh`,
`sources/build-wii.sh`, `sources/build-wiiu.sh` and `sources/build-windows.sh`.
Gate: `desktop_test/build_term_gate.sh`.

## Palette (one meaning per color, identical on every console)

| Color  | Meaning |
|---|---|
| green  | compiling, success, a feature that is `on` |
| blue   | link / convert / generate / package / stage steps |
| cyan   | metadata labels (`Name:`, `Icon:` ...) and the console/features headers |
| yellow | warnings, notes |
| red    | errors (`error:`, `cannot find -l...`), a feature that is `off` |
| dim    | informational text and `<default>` placeholders |

## When color is on

- Automatically when stdout is a terminal and `TERM` is not `dumb`.
- `NO_COLOR=1` turns it off (https://no-color.org).
- `CWV_COLOR=always` forces it on (CI logs that render ANSI).
- `CWV_COLOR=never` forces it off.
- Redirected to a file or pipe it is off, so logs stay clean.

Compiler diagnostics are colored too: gcc/clang get
`-fdiagnostics-color=always` on the Wii, Wii U and Windows builds, and
the Vita build turns on CMake's native colors (`CMAKE_COLOR_MAKEFILE`,
`CMAKE_COLOR_DIAGNOSTICS`, `CLICOLOR_FORCE`).

## Single entry point and the shared optimization stage

The per-platform scripts live in `sources/` (build-vita.sh, build-wii.sh,
build-wiiu.sh, build-windows.sh).  `./build.sh <target>` at the project
root dispatches to them; the target can be a name in any case
(vita / WII / Windows) or the menu number (1=vita 2=wii 3=wiiu 4=windows),
plus `all` (every platform in order with a summary) and `init` (scaffold
a webapp manifest, asking about every key - Enter keeps the shown
default, `-y` writes all defaults without asking).  The interactive
prompt accepts extra args on the same line too: typing "vita -y" is
exactly `./build.sh vita -y`.

## The optimization stage (minify + CWI packing, one pipeline)

Every port stages its web content through the same tool,
tools/optimize_web.sh: minification first (HTML comments + CSS
whitespace removed; JS and binaries copied untouched), then CWI
packing, then a one-line savings report tying both steps together:

    [cwi] packing CWI fast-load images ... (growth guard 1.5x) ...
    [cwi] packed 12 image(s) for pre-decoded console loads (3 kept
          smaller as PNG) (5 packaging asset(s) kept as PNG)
    [opt] web content compacted: 66 KB -> 58 KB (-12.4%)

The Wii U and Windows ports previously staged their web/ content
verbatim; they now ship byte-identical optimized content with the
consoles (identical rules, identical output, every platform).

### CWI packing rules (tools/cwi_pack_stage.sh)

Each staged PNG/BMP becomes a CWI container *in place* (same filename,
CWIF magic bytes): the engine sniffs the magic at fetch time and takes
the pre-decoded fast path, so page URLs still say `foo.png` and nothing
in the webapp needs to change.  The lossless ORIG chunk keeps the
original bytes, so `tools/cwi_unpack.py` can restore them exactly.

- **Growth guard.** CWI is a LOAD-SPEED format, not a size win: it
  embeds the original file next to the pre-decoded pixels, so it is
  always at least as big as the PNG.  Measured growth: tiny art ~1.6x,
  flat UI panels ~2-3x, noisy photos 4x+.  A packed CWI ships only when
  it stays within CWI_MAX_GROWTH times the original (default 1.5);
  anything bigger ships as the original image (smaller package, normal
  decode).  Set `CWI_MAX_GROWTH=0` to pack unconditionally when you want
  load speed at any package-size cost.
- **Stray-sibling cleanup.** `foo.cwi` next to a packed `foo.png` is
  unreachable (pages request the .png) and only bloats the package, so
  staged copies are removed.  Hand-referenced .cwi files without a
  same-name PNG are kept.
- Console packaging metadata folders (`vita/`, `wii/`, `wiiu/` under
  the staged web content) are skipped - icon0.png, bg0.png, meta.xml
  icons and WUHB art must stay real PNG bytes for the OS packers.
- Idempotent: a rebuild over an already-packed staging dir converts
  nothing (already-CWI files are detected by magic).
- Failure degrades gracefully, like minification: a failed conversion
  ships the original image and prints a warning; the build never aborts
  (the malformed nested/pixel.png fixture exercises exactly this path).

## Features in the metadata preview

The five feature switches are shown as a subcategory of the console,
mirroring the manifest shape `root > <console> > features > key`:

```
  vita
    features
      cursor:   on
      zoom:     on
      devtools: off
```

Shown for Vita, Wii and Wii U. Windows has no feature switches (its
output does not consult them), so it shows none.

## Developer notes

- `cwv_field LABEL VALUE [WIDTH]`, `cwv_features_tree CONSOLE "k:on k:off"`,
  `cwv_ok / cwv_warn / cwv_err / cwv_step / cwv_head`, and the
  `cwv_colorize` stdin filter (for wrapping noisy tools).
- Interactive prompts are never piped through a filter (that would break
  `read -p` and the `-t 0` checks); color is applied at the print site.
