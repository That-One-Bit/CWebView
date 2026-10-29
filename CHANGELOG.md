# Unreleased

- Fixed CWMenu renderer parity: shared Windows/Wii/Wii U text elements now wrap within their own element boxes, hard-break overlong words, and ellipsize when the box is too short, matching the documented behavior and Vita implementation. Expanded Vita paired-window static rendering to cover supported panels, separators, footers, checkboxes, segmented/list controls, icons, progress/value elements, and corrected partner header width to use the header element width rather than the full window width. Dual windows remain display-only by design; scrollbars are omitted because they require live viewport state.
- Optimized Canvas 2D presentation by updating existing platform textures in place instead of allocating and freeing a texture for every dirty animation frame. Vita updates aligned texture rows, Wii rewrites the existing GX RGBA8 tiled backing, Wii U uses `SDL_UpdateTexture`, and Windows copies into the existing software texture. Canvas resizing still releases and recreates storage as required. Canvas remains opt-in via `beta-enable-canvas`; this is an allocation/churn optimization, not a reduced-quality mode.
- Fixed Windows CWMenu packaging: the `find -exec cp` command now uses a valid per-file terminator, preventing successful architecture builds from failing during asset staging. Corrected default HTML button vertical padding from 1px to 2px to better match Chromium's native button inset; refreshed the golden renderer trace for this intentional 2px height change.
- Optimized the opt-in Canvas 2D rasterizer's common opaque paint path to avoid per-pixel floating-point alpha compositing, and restricted path-fill scan conversion to the path's vertical bounds. This reduces CPU work for animated/small shapes without introducing a quality-reduction mode or changing the opt-in status of Canvas.

- Completed a bug/compatibility audit pass: fixed stale menu UI test linkage for MIME/file activation, made the Windows cross-build gate validate the architectures actually emitted instead of hardcoding x64+ARM64, corrected the supported-CSS inventory, and recorded background-layer properties whose values are parsed but not yet applied by the painter. Updated the golden renderer reference for the intentional `display:block` button full-width behavior. Validation: shared/main/path/Canvas/menu UI gates and the malformed-input error audit passed; Windows cross-build skipped because MinGW is unavailable here.

- Fixed Windows ARM64 compilation against SDK/MinGW headers that expose the DPI-awareness constant but not the `SetProcessDpiAwarenessContext` declaration. CWV now resolves the API dynamically and falls back to `SetProcessDPIAware`, preserving per-monitor-v2 awareness when supported without requiring the newer declaration at compile time.
- Extended the shared experimental Canvas 2D subset with `rect()`, including closed-path fill/stroke behavior and bounded rejection of non-finite/extreme coordinates. Added regression assertions for filled rect paths in both manifest fixture layouts.
- Extended the existing Canvas hardware smoke page rather than replacing it: a second animated canvas below a tall spacer exercises scrolling, partial viewport clipping, redraw while visible, and persistence after scrolling away and back. Updated the guide and supported-methods reference to describe the current subset and platform-specific texture upload implementations.
- Confirmed the shared Canvas rasterizer is already wired through RGBA texture-upload functions on Vita, Wii U, Wii, and Windows. Hardware/display validation is still required on each target; the desktop fixture only validates the shared JavaScript-to-backing-store path.
- Added first-pass PS Vita system suspend/resume awareness: power events are received on a dedicated callback thread, the main loop pauses page/input/render updates while suspended, and controller/touch sampling plus stale input state are restored on resume. Added `docs/VITA_LIFECYCLE.md`; physical Vita/Vita3K validation remains required.
- Added a limited `navigator.platform` / `navigator.userAgent` identity surface for platform-aware launch pages and expanded the shared optional default launcher with per-platform control summaries. Added a regression check for both `navigator.platform` and `window.navigator.platform`.
- Fixed the Windows Files-browser filename buffer warning, disabled Windows DPI bitmap virtualization, added F11 borderless fullscreen, and switched scaled framebuffer presentation from HALFTONE to point sampling to avoid blurred output. True dynamic-resolution rendering and GPU acceleration remain future work.
- Added visible, visual-only markers for all active PS Vita front-touch contacts; existing direct touchscreen input dispatch is unchanged.
- Added a clean 8x8 RGBA replacement for the malformed `nested/pixel.png` fixture at `test_pages/replacement-pixel.png` and documented replacing the local webapp copy.
- Documented installing LLVM-MinGW's x86_64-hosted ARM64 cross-toolchain; the Windows ARM64 output can be built from an x64 PC without ARM64 hardware.

- Hardened the HTML tag scanner so `>` inside quoted attributes does not terminate a tag, and apostrophes in unquoted values are not mistaken for quote delimiters. Close-tag matching now skips comments and raw-text payloads (`script`, `style`, `title`, `textarea`), fixing false nesting caused by markup-looking text inside comments/scripts and avoiding needless scans through script contents.
- Reduced CSS selector-matching work on Vita/Wii by rejecting tag-qualified selectors with a cheap case-insensitive tag check before copying and parsing the selector. Added error-audit fixtures for quoted `>`, comments with fake nested tags, script strings containing markup, and apostrophes in unquoted attributes.
- Hardened Canvas 2D rectangle clipping against NaN, infinity, and extreme finite coordinates before integer conversion, preventing undefined behavior from malformed or adversarial script values.
- Added Canvas 2D `strokeRect()` with `strokeStyle` and `lineWidth`, using clipped RGBA strips and the existing alpha-compositing path. Canvas dimension resets restore both new context properties to their defaults; regression fixtures verify the outline and untouched interior.
- Added initial Canvas 2D path support: `beginPath`, `moveTo`, `lineTo`, `closePath`, `fill`, and `stroke`, plus `quadraticCurveTo`, `bezierCurveTo`, and `arc`. Curves and arcs are flattened into bounded line segments; fill uses even-odd scanline filling, and strokes are clipped to the backing store before rasterization. Path storage is capped at 1,024 points per canvas.
- `canvas.getContext("2d")` now returns the same cached context object for a canvas, preserving `fillStyle` and `globalAlpha` across repeated calls; assigning `width` or `height` resets both the bitmap and context state, even when the assigned dimension is unchanged.
- Replaced the per-frame RGBA-to-BMP-to-texture round trip with a platform-specific RGBA upload seam. Vita aligns texture rows, Wii packs GX RGBA8 tiles, Wii U uploads SDL RGBA textures, and Windows stores RGBA pixels directly. Canvas drawing now uses logical backing dimensions rather than any hardware-padded texture width.
- Expanded Canvas 2D fixtures to cover context identity/state, alpha compositing, huge clipped rectangles, and invalid numeric arguments.
- Added a visible PS Vita Canvas smoke page and a short hardware-test guide. The page uses the currently implemented Canvas 2D subset, now including rectangle strokes, path filling, arcs, and quadratic/Bézier curves, and documents the required `web/package.json` opt-in flag.

- Removed Vita's production START-to-quit binding and debug-build START overlay toggle. START is now forwarded to page input instead, with press/release events on Vita, Wii, and Wii U; normal platform/homebrew exit handling remains owned by the launcher or system.

## 2026-10-23 — Wii U ProcUI lifecycle, scrolling, and DevTools diagnostics

- Initialize `WHBProc` and keep the Wii U main loop gated by `WHBProcIsRunning()`, then shut the wrapper down during normal exit and handled startup failures. This services the foreground/background/exit lifecycle used when RPX/WUHB apps launch as regular titles under Aroma/Tiramisu.
- Link the WUT `libwhb.a` wrapper explicitly and fail the Wii U build early with a clear diagnostic if it is missing.
- Added native SDL finger gesture handling for page scrolling. A tap still dispatches a pointer click; a drag scrolls the nearest nested vertical/horizontal region, without synthesizing a click at the end of a scroll gesture. Disabled SDL's duplicate touch-to-mouse synthesis for this path.
- Fixed D-pad scrolling when Wii U input is configured for D-pad mode: SDL hat up/down now scroll vertically and left/right horizontally instead of always cycling focus.
- Fixed the shared DevTools Performance panel's row-name mismatch: the live FPS, frame count/time, and scroll/zoom rows now resolve both semantic native-template names and the numbered rows in `default.cwm`.
- Extended the desktop SDL stub with finger-event and WHBProc declarations so the Wii U syntax gate covers these paths.
- Validation: Wii U syntax gate, shared-source gate, production/debug main gates, and `git diff --check` passed. Native devkitPro build and console verification remain pending.

## 2026-10-22 - Wii U button text fit and transient pressed states

### Wii U text measurement

- Corrected the TTF width seam: SDL_ttf reports glyph width in framebuffer pixels,
  while shared HTML layout uses 960x544 virtual-canvas coordinates. The measured
  width is now converted back through the horizontal fit, so intrinsic button
  widths and centered labels account for the Wii U's 854x480 output scaling.
- Fixed block-button label placement to anchor to the button's own x-position, not
  the page left edge; this also prevents overflow for centered/nested buttons.
- Checked the Wii U cursor path: Wiimote IR coordinates are mapped from 1280x720
  into the same 960x544 virtual canvas used by pointer hit-testing; GamePad mouse
  and touch coordinates are converted through the inverse fit. No cursor-coordinate
  change was needed.

### Pressed / `:active` state across platforms

- Added transient interaction state for pointer/touch targets and their ancestors,
  so CSS `:active` styles apply while a press is held and clear on release/cancel.
- Added a focused-control release hook and wired it to Vita Cross, Wii/Wii U A,
  Windows Return/Space, and Wii U Return. The Wii USB keyboard has press-only events,
  so its Enter activation is treated as a brief press.
- Focused buttons still darken while held, but the effect no longer sticks after a
  controller activation; VCM's separate persistent active styling remains intact.

### Validation

- Wii U and Wii syntax/build gates pass.
- Shared-source gate and main gate pass; cursor, CWMenu, theme, and error-audit
  functional checks passed before the optional ASan pass timed out.
- Windows cross-compiler is not installed in this environment, so its gate was skipped.
- Real Wii U hardware/Cemu visual confirmation is still required.

## 2026-10-22 - Canvas manifest discovery and navigable Files picker

### Canvas 2D opt-in on console packages

- `cwv-args` now checks both the executable/package root and `web/package.json`.
  A root manifest without `cwv-args` no longer masks the web package's flag.
- Missing or invalid manifests produce a runtime diagnostic, making it clear why
  `getContext("2d")` may return `null` on a package that expects Canvas support.
- Added Canvas regression coverage for both manifest locations.

### CWMenu Files picker

- The Files picker now opens at `app0:/web` instead of accidentally listing the
  app root when `CWV_WEBPAGE` is just `index.html`.
- Replaced the always-expanded tree with directory navigation. Directories open
  on confirmation, `..` returns to the parent, and navigation is clamped at the
  web root. Files remain sorted after folders and HTML pages still use normal
  navigation/history.
- Folder entries have a `[DIR]` label and selected folders use an outset-style
  highlight (bright top/right edges, dark bottom/left edges).

### Validation

- Canvas 2D regression passes with a root-level manifest and with only
  `web/package.json` present.
- `bash desktop_test/build_cwv_args_unit.sh` passes.
- `bash desktop_test/build_main_gate.sh` passes in production and debug modes.
- Shared-source, Wii, and Wii U build gates pass.
- A full Vita SDK build and hardware validation remain for the user's device.

## 2026-10-21 (90) - Wii startup diagnostics and log compatibility

### Make Wii runtime logs easier to find

- Keep the existing `cwebview.log` filename for compatibility and also write the
  same messages to `CWV.log` in the app folder. Each file is attempted separately.
- Log whether `assets/font.ttf` loaded and whether `cwmenu/default.cwm` loaded,
  to distinguish missing menu content from a text-rendering/font failure.
- Log the initial page URL and explicit success/failure status (including the
  host error) so a blank page can be separated from a failed file load.

### Validation

- `bash -n sources/build-wii.sh` passes.
- `bash desktop_test/build_wii_gate.sh` passes.
- Hardware/emulator verification is still needed to determine whether the Wii's
  blank webpage and cursor scaling symptoms are fixed; these diagnostics make
  the next device-side report actionable but do not claim to fix rendering yet.

## 2026-10-21 (89) - Wii U duplicate-basename object collision

### Wii U link failure after the CSS viewport helper change

- Changed the Wii U build to preserve each source file's relative path in its
  object-file path instead of writing every object as `build/wiiu/obj/<basename>.o`.
  This prevents distinct files named `cwv_css.c`, `cwv_html.c`, or other repeated
  basenames from overwriting one another before linking. That collision can
  silently discard the object containing `cwv_css_viewport_width/height`, matching
  the reported undefined references even though the CSS source appears to compile.
- Kept the source directories separated for shared-engine and Wii U platform
  objects, and create the corresponding object subdirectories as needed.

### Validation note

- `bash -n sources/build-wiiu.sh` passes.
- The full Wii U SDK link still needs to be rerun on the user's devkitPro setup.

## 2026-10-20 (88) - Wii/Wii U SDK build fixes

### Wii linker dependencies

- Added the devkitPro `ppc-bzip2` and `ppc-brotli` packages to the optional
  toolchain install/verification prompt because the installed static FreeType
  build can reference both codecs.
- Linked `libbz2`, `libbrotlidec`, and `libbrotlicommon` after FreeType so its
  unresolved symbols can be satisfied by the static linker. Expanded the
  failure diagnostics to report whether these libraries are present.

### Wii U shared header lookup

- Added `sources/shared/sources/core` to the Wii U platform compile checks and
  full-build include paths. Shared CSS sources include `cwv_renderer.h` from
  that directory, which was not covered by the shared source root alone.

### Validation note

These changes address the reported link/include failures. This environment
lacks the user's devkitPro console SDK setup, so the final Wii/Wii U target
links still need to be confirmed on the machine that produced the reported
log. The Vita path/name truncation warnings are separate and are not build
failures.

## 2026-10-08 (87) - Position edge offsets and shared launcher template

### CSS positioning regression fixes

`right` and `bottom` were already accepted by the CSS registry and stored on
nodes, but absolute/fixed layout only positioned from the normal-flow origin
and did not use the edge offsets. Layout now measures the rendered element
subtree, then applies `left`/`top` or (when those are absent) `right`/`bottom`
against the viewport approximation. Flow cursors are restored so positioned
boxes do not consume sibling layout space. Added a focused desktop regression
page and gate covering right-only, bottom-only, and fixed right+bottom.

This does not claim full CSS containing-block semantics, percentage offsets,
fixed-on-scroll behavior, or flex/grid support; those remain outside the
Halloween stability scope.

### Build and source-path fixes

- Fixed `desktop_test/es2015_unit/build_unit.sh` to include the reorganized
  `sources/shared/sources` root. The unit binary builds and all cases pass.
- Added the missing `SDL_RenderSetClipRect` declaration to the desktop Wii U
  SDL stub. The Wii U syntax gate passes; this does not replace a real SDK build.
- Removed the obsolete `esm_module` assumption from the build-path gate; the
  remaining Vita scroll module is checked normally.
- Moved the canonical launcher template to `sources/shared/assets/` and
  updated CMake/Vita build references. It is now a shared cross-platform
  template rather than a Vita-owned source asset. Current automatic generation
  remains wired to Vita; Wii/Wii U/Windows launcher staging is still a separate
  integration item, and their current entrypoint behavior is unchanged.

### Validation

Passed: position-edge regression, main production/debug syntax, JS, ES2015
unit, Wii U and Wii syntax gates, Vita keyboard gate, build-path/shared-source
checks, application-root, cursor, CWMenu, loghook, menu hotkey/UI, theme, ES6
C99/C11 portability, BMP, CWI, terminal, and TypeScript (including ASan/UBSan).
The complete gate runner exceeded the environment time limit during the image
cache gate; Windows cross-compilation was skipped because MinGW is unavailable.
No real console SDK build or hardware test was performed.

## 2026-10-07 (86) - Shared source tree reorganization

### Responsibility-based source layout
Reorganized the console-neutral engine from the flat `sources/shared/`
directory into `sources/shared/sources/` with explicit groups:

- `core/` for the renderer/backend, platform seam, host, image codecs,
  cursor and project configuration;
- `web/html/` and `web/css/` for the HTML and CSS implementations;
- `web/js/modern/` for the single CwvESM ES2015+ transform and edition
  libraries;
- `web/js/runtime/` for JavaScript glue and the vendored Duktape runtime;
- `web/typescript/` for the TypeScript preprocessing layer;
- `menu/` for CWMenu/native-menu implementation and `menu/scripts/` for
  declarative `.cwm` files.

Build scripts, CMake, desktop gates, documentation and include paths were
updated to use the new tree. `sources/shared/sources/README.md` documents
the boundary and include convention.

### Wiring checks
The shared compile gate now recursively checks the reorganized first-party
source tree and separately syntax-checks vendored Duktape. The Vita wiring
gate was made recursive as well and caught that `cwv_bmp.c` existed in the
shared tree but was absent from the Vita target; it is now linked there.
The shared menu UI remains explicitly excluded from the Vita target because
that port uses its separate native menu path.

The build-path gate was also corrected to stop probing the removed
`CwvESM.suprx` module directory. Targeted shared-source, Vita-wiring and
build-path checks pass in the available desktop environment. Full console
SDK/hardware validation remains subject to the release-plan limitations in
HANDOFF.md.

## 2026-10-07 (83) - CWI link fix on every platform, unified optimization stage, growth guard

### Vita link error + every-platform audit (83)
The Vita build failed at link with "undefined reference to
cwv_cwi_decode": the CWI codec (sources/shared/sources/core/cwv_cwi.c) was only in
the Windows source list. Audited all four platforms: added it to the
Vita CMake target list and the Wii explicit source list (the Wii U
globs sources/shared/*.c, Windows already listed it - both fine).
Windows keeps it; every port now links the same codec.

### "vita -y" at the interactive prompt (83)
The build.sh menu read the whole reply line as one target, so typing
"vita -y" produced "unknown target". The prompt now word-splits: the
first word is the target, the rest pass through as build args (so the
prompt accepts exactly what the CLI accepts; prompt args are prepended
to command-line args). Documented in the usage text.

### One optimization stage for every port (83)
New tools/optimize_web.sh: minification (tools/minify_web.py) + CWI
packing (tools/cwi_pack_stage.sh) + a combined savings line, run in
that order over the staged copy with the same degrade-gracefully rules
on every platform. All four builds now stage through it - the Wii U and
Windows ports previously shipped verbatim content and now ship
byte-identical optimized content with the consoles. The savings report
ties the steps together: "[opt] web content compacted: 66 KB -> 58 KB
(-12.4%)". The Vita's inline minification fallback moved into the
shared stage (one implementation, same loud fallback warning).

### Growth guard + stray-sibling cleanup (83)
Measured: CWI always embeds the original file (lossless ORIG chunk)
next to the pre-decoded pixels, so it is never smaller than the PNG -
tiny art ~1.6x, flat UI panels ~2-3x, noisy photos 4x+. cwi_pack_stage
now ships a packed CWI only when it stays within CWI_MAX_GROWTH times
the original (default 1.5); anything bigger ships as the smaller
original PNG (compact files win unless CWI is genuinely justified).
CWI_MAX_GROWTH=0 packs unconditionally for load-speed priority; the
active guard value is printed on the step line. Also: staged foo.cwi
copies sitting next to a foo.png are unreachable by URL (pages request
the .png) and only bloat the package, so they are removed with a count
in the summary (hand-referenced .cwi files without a same-name PNG are
kept); the stray webapp/favicon.cwi fixture was deleted from the repo.

### Gates (83)
build_term_gate.sh section 8 rewritten: all four scripts must run the
shared stage; forced mode (CWI_MAX_GROWTH=0) packs flat and noisy
fixtures, default mode keeps 1.6x art as PNG, the packaging-folder
skip, stray-sibling removal, idempotency, the pack/unpack roundtrip,
and the optimize_web.sh end-to-end run (minified HTML + packed CWI +
savings line) are all asserted.

## 2026-10-07 (81) - Single entry point: build.sh dispatcher, interactive init, app version on menus

### One build.sh at the project root (81)
The four per-platform scripts moved into sources/ (build-vita.sh,
build-wii.sh, build-wiiu.sh, build-windows.sh) and a new ./build.sh at
the project root dispatches to them. The target can be a name in any
case (vita / WII / Windows), the menu number (1=vita 2=wii 3=wiiu
4=windows), "all" (every platform in order, with a per-platform summary),
or nothing at all on a terminal (interactive numbered menu). Extra args
pass straight through (./build.sh vita -y --webapp myapp/). --resolve
<target> prints which script a target maps to (used by the term gate).
Every script now computes the project root as its own directory's
PARENT (sources/), and all gate/doc references were updated.

### Interactive init (81)
./build.sh init scaffolds a webapp: it creates webapp/index.html when
missing and writes webapp/package.json by ASKING about every key
(Enter keeps the shown default; -y writes all defaults without asking).
It walks the common keys (name, shorthand, version, author, target,
description, webpage, webappDirectory, icon), the Vita keys (titleId,
icon, wallpaper, startLogo, liveareaTemplate, storageDirectory,
launchPage, sfoVersion), the Wii/Wii U keys (icons, cursor art) and the
five feature switches plus the Vita debug flag. An existing
package.json is merged - unknown keys survive, and answered values
replace old ones. Cursor art uses the dual-purpose "cursor" key (78/79
convention: a string value is art, a bool is the feature switch).

### App version on the menus, in bright white (81)
Every build script stamps the manifest "version" into the binary as
CWV_APP_VERSION (-D flag; the Vita build passes it through CMake, which
gains a CWV_APP_VERSION cache string and compile definition). The main
menu now shows it right beside the internal CWebView version:
"Version: <production - build>  App: <version>", with the App segment
in bright white (RGBA8(255,255,255,255)). Implemented on the Vita main
menu, the Vita DevTools page and the shared menu UI: the shared main
menu previously had NO version line at all (the DevTools line showed the
platform name under a "Version:" label); it now shows the internal
version + platform + bright-white app version there, and a proper
version line on the main menu (Vita parity, x+420/y+162). The shared
sources get their own real sources/shared/sources/core/cwv_version.h (mirroring the
CMake cache defaults 0.1.1/0.4.4.5) with a CWV_APP_VERSION fallback;
the Vita generated header and the desktop stub gained the same fallback.

## 2026-10-07 (82) - CWI packing is a listed build step on every platform

tools/cwi_pack_stage.sh (new, shared) walks the STAGED web content of
each port and converts every PNG/BMP in place into a CWI container:
same filename, CWIF magic. The engine already sniffs the magic at fetch
time (cwv_backend.c cwv_cwi_is) and takes the pre-decoded fast path, so
page URLs still say "foo.png" and nothing in the webapp changes; the
lossless ORIG chunk keeps the source bytes (tools/cwi_unpack.py restores
them byte-for-byte). The step is listed in the output:
"[cwi] packing CWI fast-load images ... / [cwi] packed N image(s)".

Wired into all four builds at the staging point: Vita (after the
packaging-asset exclusion, before the VPK copy), Wii (after the staged
web/ copy), Wii U (after the web/ copy, whose packaging-asset exclusion
already ran), Windows (inside stage_web, as a bash subprocess - that
script is POSIX sh).

Rules: console packaging metadata folders (vita/, wii/, wiiu/ under the
staged content) are skipped so the OS packers keep real PNG bytes; the
pass is idempotent (already-CWI files are detected by magic and
skipped); failures degrade gracefully like minification - the image
ships as its original PNG with a warning, never aborting the build
(the malformed nested/pixel.png fixture exercises exactly this path).
Locked in the term gate: all four scripts run the step, the summary
line shape, the metadata-folder skip, idempotency, and the
pack/unpack roundtrip are all asserted.

## 2026-10-07 (80) - Vita/Wii build fixes, features subcategory, shared build-script colors

Fixes for the first real-hardware toolchain run of (79), plus the
requested features tree and colored output on every build script. All 26
gates green (one new).

### Vita: nonexistent vita2d symbol (80)

sources/vita/cwv_platform.c called vita2d_set_clip_rect(), which does not
exist in libvita2d; the real function is vita2d_set_clip_rectangle(
x_min, y_min, x_max, y_max) (same argument order). Fixed. Why no gate saw
it: the desktop gate compiled cwv_platform_stub.c, never the real Vita
file. build_vita_kbm_gate.sh now also syntax-checks the REAL
sources/vita/cwv_platform.c against the stub headers with
-Werror=implicit-function-declaration (the stub header declares the real
clip + texture-creation API). Verified the gate fails with the typo put
back. The same pass found a missing <stdio.h> (snprintf only worked via a
transitive include) - added.

### Wii: every library "cannot find -l..." (80)

The -L list was built as a string with literal \' pairs, then "cleaned"
with tr -d "\'" - which removes the quote characters but NOT the
backslashes, so the linker received -L\/opt/devkitpro/libogc/lib\ for
every directory. All eleven installed libraries were reported missing
(the log showed them "ok" right below). The list is now a bash array
passed as "${WII_LIBDIR_ARGS[@]}" (also safe for paths with spaces).
Locked in by build_term_gate.sh, which runs the REAL block from
build-wii.sh and fails on any backslash/quote in a -L argument.

### Features are a subcategory of the console (80)

The metadata preview on Vita, Wii and Wii U now shows features as
root > console > features > key (see docs/BUILD_OUTPUT.md), one key per
line with its state, instead of a single "cursor:on zoom:on ..." line.
Windows has no feature switches and shows none.

### Shared colors on all four build scripts (80)

New tools/cwv_term.sh (sourced by build-vita/wii/wiiu/windows.sh):
green = compile/success/on, blue = link/convert/package, cyan = labels,
yellow = warnings, red = errors/off, dim = placeholders. Auto-off when
not a terminal, NO_COLOR honored, CWV_COLOR=always|never overrides.
Compiler diagnostics get -fdiagnostics-color=always (Wii, Wii U,
Windows) and CMake's native colors are forced on for Vita. The old
wiiu_* helpers now delegate to the shared palette. Metadata previews use
the same labeled/aligned fields everywhere (Vita's long labels no longer
overflow their column).

### Windows summary honesty (80)

"Built: ...arm64/CWebView.exe" and "complete: x64 + arm64" were printed
even when the ARM64 compiler was missing and skipped. The summary now
lists only artifacts that exist.

### Gates

New desktop_test/build_term_gate.sh (palette meanings, pipe/NO_COLOR
safety, tree shape, colorize rules, Wii -L array; verified to FAIL on the
old escaped form). build_paths_check.sh now syntax-checks all four build
scripts and requires each to source the shared palette. Docs live in
docs/BUILD_OUTPUT.md (README untouched, per direction).

## 2026-10-06 (78) - --debug wins over the manifest, editable default launcher, deduped assets, build-script metadata UX on all four platforms

Console-output polish: every key that shapes a platform's OUTPUT is now
shown and confirmable at build-script start on ALL FOUR platforms (Wii
U had it; Vita gained feature-key lines; Wii and Windows got the full
display + confirm + `n -<key>` + `/s` save-back flow), the default Vita
launcher became a real editable file, the duplicated packaging assets
were consolidated, and the `--debug` flag now overrides the manifest in
both directions. All gates green.

### --debug overrides the manifest (78)

build-vita.sh treats an explicit `--debug` CLI flag as authoritative
EITHER WAY: with the flag, `debug: false` in the manifest no longer
silently reverts to production - the debug overlay builds. The
manifest key alone keeps working as before. Documented in the schema
description and docs/MANIFEST_KEYS.md.

### Default launcher: a real file, not an embedded string (78)

sources/vita/assets/default_launcher.html now IS the launcher (plain
editable HTML); CMake substitutes {{APP_NAME}}/{{WEBPAGE}} and stages
the result as __vita_default_launcher.html when the manifest sets
launchPage to "". The CMake-embedded copy (and its escaping) is gone.
Verified equivalent to the old generated output; the VPK required-
files check is unchanged.

### Asset dedup + sources/vita/assets as THE Vita packaging home (78)

The four default packaging images and template.xml were byte-identical
duplicates in /assets/ and sources/vita/assets/; the /assets/ copies
are gone. /assets/ now holds only cross-port art (cursors, font +
license); every build's fallback points at sources/vita/assets.

### build-wii.sh metadata UX (78)

The Wii build now shows every recognized key on start (name,
shorthand, version, author, description, icon, cursor art, webpage,
webapp dir, feature keys) and confirms: [Y] applies the manifest;
`n -<key> ...` edits only flagged fields; /s writes them back to the
wii section; /p keeps them for this build. New wiring: wii.webpage
becomes the CWV_WII_WEBPAGE entry define, wii.webappDirectory selects
the staged webapp, wii.cursor (string) overrides cursor art (boolean
false stays the feature flag), and the manifest's wii.icon already
worked. The save-back was tested live (n -version /s).

### build-windows.sh metadata UX (78)

Same display + confirm + selective-edit grammar. Two keys shape the
Windows output: webpage (new CWV_WIN_WEBPAGE define passed to BOTH
arches, picked up by sources/win/main.c with the old default kept) and
webappDirectory (staged as web/). Name/version/author/description are
displayed as informational. The full build passed end-to-end with the
define; n -webpage /s tested live. -y/--yes runs non-interactively.

### Vita + Wii U displays: feature keys (78)

The Vita and Wii U metadata previews now show the five project feature
keys' state alongside the packaging keys, so every output-affecting
key is on screen at once on every platform.

### Every packaging asset is webapp-overridable (79)

LiveArea template.xml joins the overridable set: manifest key
vita.liveareaTemplate (aliases livearea_template, template; CLI
--livearea-template; `n -livearea-template` in the edit flow),
convention webapp/vita/template.xml, then the repo default in
sources/vita/assets/ (CMake now receives -DVITA_LIVEAREA_TEMPLATE -
the cache variable existed but the script never passed it). Cursor
art gained the same convention on all three consoles
(webapp/<console>/cursor.png after the manifest string, before the
repo default), and the Wii icon gained webapp/wii/icon.png. The
example webapp ships webapp/vita/template.xml (a copy of the default,
like its icon.png) and demonstrates the new key; resolution order and
conventions are documented in docs/MANIFEST_KEYS.md's new asset
override table. Tested live: display shows the key, n -livearea-
template /s saves it, the flow continues.

### Schema/doc updates (78, 79)

cwebview.schema.json: the wii section gains the packaging keys it now
consumes (name/shorthand/version/author/description/icon/cursor dual-
purpose/webpage/webappDirectory) and drops the stale "compile-check
only" note; vita.cursor/wiiu.cursor/wiiu/wii cursor become
["boolean","string"] matching the builds' dual-purpose reality;
vita gains liveareaTemplate. MANIFEST_KEYS.md follows, including the
new asset override conventions table.

### Gates

desktop_test/build_paths_check.sh's CMake-source regex now includes
.html so the launcher template file is checked instead of its
truncated ".h" ghost. Full suite re-run green.

## 2026-10-06 (77) - Full-key example manifest, raw-image compatibility note, manifest reference doc

Packaging polish pass: the example webapp now demonstrates every metadata
key end developers can use, and raw image formats got a gentle nudge
toward .cwi. All 25 gates green (verified the note fires with a
runtime_log-wired one-off; the golden harness output is unchanged).

### Example manifest with every key (77)

webapp/package.json now exercises EVERY canonical key in
webapp/cwebview.schema.json (validated with jsonschema): root gains
description and icon; the vita section gains launchPage (empty string =
generated default launcher - the example build now demos it
deliberately), sfoVersion, buildVersion and debug, plus the
name/shorthand/version/author/webappDirectory/webpage overrides; the
wiiu section gains the same overrides. All values equal the build
defaults so nothing changes behavior except the intentional launcher
demo. Alias names (appName, shortName, ...) remain accepted but the
canonical names are preferred.

### Raw-image compatibility note (77)

PNG/JPEG/BMP stay fully supported on every port - but a console pays for
the decode at page load, so a successful non-CWI decode now logs ONE
note per image (the URL cache dedupes): a console "warn" (visible in
the on-device DevTools where web devs look) mirrored to the runtime log
for script-free pages - the same dual-surface pattern as img_diag. It
recommends tools/cwi_pack.py; it is a hint, never a blocker. Verified
firing with a dedicated runtime_log-wired check; the golden reference
is byte-identical (harness pages have no runtime log wired).

### docs/MANIFEST_KEYS.md (77)

Reference-style table of every manifest key (canonical name, aliases,
defaults, which build consumes it) generated from the schema and the
build scripts' alias tables, so the user-facing docs can paraphrase it
without keeping the key list in two places. README intentionally not
touched (user direction).

## 2026-10-06 (75-76) - Per-element scroll regions, SVG hostile-geometry hardening

Regions turn any overflowing box into a scrollable, focusable control -
and the SVG parser got the same hardening lessons the <img> decoder fuzz
taught, closing every shared failure class between the two elements.
All 25 gates green (scroll_region, svg_edge and svg_hostile added to the
golden harness).

### Per-element scroll regions (75)

Any box (div/section/svg/form control) with explicit overflow
(overflow/overflow-x/overflow-y, auto or scroll; hidden clips without
scrolling) whose content overflows is a scroll region:

- Chrome focus rules: joins the tab/DPAD ring like a control - positive
  tabindex orders first, tabindex="-1" never enters the cycle.
- Focused region scrolls INSIDE first; leftover scroll delta reaches the
  page (nested-scroll-container rule). Region-internal scroll_max is
  baked at draw time; scroll_to(0) resets content AND the page.
- Focusing a control/link inside a region reveals it in the scroll
  window (reveal is region-local, never scrolls the page).
- Content past a region edge is clipped via a new port seam
  cwv_set_clip/cwv_clip_disable (win, wii, wiiu, vita, desktop stub).
- Focused regions show their own scrollbars; nested regions clip
  against their parent's window; display:none regions never activate.
- Fixed while wiring: focus rings and select popups now shift with
  region scroll offsets (they drew at unshifted positions), and
  region-clipped content no longer extends the PAGE's scrollable area
  (the wide-strip page-scrollbar parity bug).
- overflow-x/overflow-y setters now set overflow_explicit so inline
  per-axis declarations classify the box at all.
- Demos: webapp/scroll_region.html (index linked), golden page
  scroll_region.html scenario 3 (tab order, reveal, region-first
  scroll, page fall-through all locked in the trace).

### SVG hostile-geometry hardening (76)

Bug + memory pass over <svg> for every class of issue <img> suffered:

- NaN/inf attributes (atof accepts them; they pass every "<=" check)
  now fall back to defaults; width="inf" is auto (300x150) like Chrome.
- The <svg> box caps at 4096px a side; baked shape coordinates clamp to
  +/-16384 so scanline fills, focus rings and page extents stay bounded
  - no console hangs, no runaway page scrollbars.
- Point-arena overflow rejects the shape WHOLE (arena rolled back),
  never draws it truncated - the CWI rule that a truncated payload is
  invalid, not valid-with-less. svg_parse_path/points/text push sites
  all check the push result now.
- Stale svg_text_harvest comment fixed (folding happens at text-creation
  time, (73)).
- Locked by the new svg_hostile.html + svg_edge.html golden pages:
  NaN/1e9/inf sizes, zero and NaN viewBoxes, giant circles/ellipses/
  polylines/paths, arena-overflow path, plus a valid shape rendered
  exactly after all the abuse.

## 2026-10-06 (71-74) - CWI image format, SVG <text>, two-axis scrolling

The compiled-image format David asked for, the <text> element completing
the SVG subset, and a full pass making x/y scrolling work simultaneously on
every port. All 27 gates green.

### CWI compiled image format (71)

Custom container for console UI art: pre-decoded RGBA with a shared
lossless-RLE payload (no PNG inflate on load), original file embedded as a
chunk for byte-identical round-trips. Magic sniffing means .cwi is accepted
WHEREVER images load - <img src>, CSS url()/background-image, data URIs,
favicon - with no HTML/CSS/JS changes needed; JS/canvas treats a decoded
CWI texture like any other image (toDataURL still emits PNG). Full
toolchain: tools/cwi_pack.py / cwi_unpack.py CLIs (byte-identical
round-trip verified), shared cwv_cwi.c decoder wired into all five ports
(Vita, Wii, WiiU, Win WIC-less path, desktop stub), 847-case integrity
fuzz unit gate (build_cwi_unit.sh, truncation/oversized/trailing-bytes
rejected, boundary-exact files accepted), demo assets webapp/favicon.cwi
and a data-URI demo.

### SVG <text> (72-73)

`x`/`y` baseline placement, `fill` glyph color (Chrome default black),
`font-size` in px (clamped 16..200, rendered by the resolved page font).
Content folds into the shape node at PARSE time - never a staged flow
node, so it cannot advance layout or leak into focus/hit tests (the
class of silent-layout-drift bug <img> suffered). Runs before the first
<text> in an <svg> are dropped like Chrome drops orphan svg text, and a
boundary guard stops the fold at the current <svg> root.

### <img>-class bug pass on <svg> (73)

Wii texture staging leaked the decoder's RGBA buffer on every BMP/CWI
load (fixed on both success and failure paths); Win keeps transfer
ownership (freed by cwv_tex_free) and the stub decodes nothing; CWI rides
the same per-page URL-keyed image cache as PNG/BMP (no repeat decodes);
per-page resets of the SVG point arena and viewBox stack verified.
Hostile-input pass: unclosed <svg>/<text>, font-size 99999/-5, NaN
coordinates - all parse, paint and scroll without crash or drift.

### Two-axis simultaneous scrolling (74)

Both scroll axes adjust at the same time, driven by one smooth motion:
- Vita/Wii U analog sticks already apply x+y per frame (circling the
  stick orbits the viewport) - verified, unchanged.
- Vita touch drag was Y-smooth but X only moved in per-frame 7px steps:
  rewritten as a true 2D pan that engages at 7px total displacement from
  touch start (the Y module's own threshold) and then tracks BOTH axes
  every frame; without the optional scroll module the native path pans
  Y as well (degraded builds keep two-axis touch scroll).
- Keyboard/gamepad Left/Right horizontal scroll was MENU-ONLY on Wii,
  Win and Wii U (browsing pages could not scroll sideways at all): added
  60px steps matching Up/Down, all four directions read independently.
- The on-screen scrollbar thumbs follow both axes during any two-axis
  motion, and thumb-drag plus analog (or keys) works at the same time.

## 2026-10-05 (67-70) - Final pre-release feature pass

The SVG subset David greenlit, the position/background/units keys, and
the ARM64 Windows toolchain - the last feature work before release
auditing.

### Inline SVG (67)

Static inline `<svg>` subset: the root is an ordinary box node
(width/height attributes, Chrome's 300x150 default, uniform viewBox
scaling), children are void shape records with baked points in a
4096-float per-page arena. Shapes: rect, circle, ellipse, line, polyline,
polygon, path, g (passthrough); defs/title/desc ignored with subtree.
fill/stroke/stroke-width presentation attributes; path commands
M m L l H h V v C c S s Q q T t A a Z z (curves flattened at parse,
arcs as chords). Painting uses portable shared primitives: scanline
polygon/ellipse fills, stepped line strokes, platform fill_circle -
zero per-port code. Documented gaps: CSS-styled SVG internals, <text>,
SMIL, transforms, gradients, masks, external .svg sources, JS
createElementNS, per-shape hit tests.

### CSS position keys (70)

position: relative/absolute/fixed (sticky renders as relative) with
top/right/bottom/left offsets. Absolute/fixed pull the box out of the
flow (static-position fallback like Chrome); fixed is scroll-exempt in
paint AND hit tests; relative shifts the box plus children after layout
so every port inherits it without code changes.

### CSS background layer keys (68)

background-repeat (repeat/no-repeat/repeat-x/repeat-y),
background-position (keywords/px/%), background-size (cover/contain/
px/%), background-attachment (fixed = scroll-exempt),
background-clip/background-origin (border/padding/content-box), plus
the same keywords recognized inside the `background:` shorthand.
Percent positions/sizes resolve against box-minus-image at draw time,
Chrome-style (50% centers).

### CSS units (69)

The full family: vw/vh/vmin/vmax, svw/svh/lvw/lvh/dvw/dvh (small/
large/dynamic all = the full viewport on a console - no browser chrome
to shrink it), and in/cm/mm/q/pt/pc at Chrome's 96 reference px/inch.
The real viewport is published per port at load (cwv_css_set_viewport).

### Windows ARM64 toolchain

llvm-mingw 20260922 (ucrt, clang 23) installed at /opt/llvm-mingw (the
path build-windows.sh auto-detects). build-windows.sh and the desktop
win gate now build and verify BOTH .exe machine types (0x8664 AMD64 +
0xaa64 ARM64); previously the ARM64 leg auto-skipped.

### Demo pages

webapp/svg_demo.html (every shape + viewBox + defs), 
webapp/css_units_position_demo.html (all units, position kinds,
background layers). All 26 desktop gates PASS, FAILURES=0.

## 2026-10-05 (65/66) - Chrome-parity CSS batch + the full <img> audit

The CSS/text/Chrome-parity batch and the end-to-end <img> audit David
asked for, with every fix regression-checked against the golden
reference (three of its layouts were RE-BLESSED because they had
absorbed real bugs - details below).

### CSS properties added (all in CSS_PROPERTIES[], style attributes
### AND <style>/link rules alike)

- `opacity` - composited against the page color behind each node
  (`mix_rgba`), including text, borders and images.
- `visibility` - `hidden` (and `collapse`): layout box kept, hit-testing
  and DPAD focus off, nothing painted (the `.ghost` class finally
  works).
- `text-decoration` - `underline`, `line-through`, `none` (also
  `overline`, parsed and drawn).
- `text-transform` - `uppercase`, `lowercase`, `capitalize`.
- `letter-spacing`, `word-spacing` - px, applied by the shared text
  painter (positive AND negative spacing).
- `line-height` - a CSS factor on the engine's 28px line box
  (unitless, px and em forms all accepted).
- `white-space` - `nowrap`, `pre`, `pre-wrap` (normal unchanged).
- `min-width`, `max-width` - clamp at flow entry; `max-width` also
  clamps an auto-width full block, exactly like CSS.
- `box-sizing` - `content-box` widens an explicit width by
  padding+border (the engine's native model stays border-box).
- `width` in PERCENT - `width:80%` now resolves against the containing
  block at flow entry instead of misreading as 80px; `width:0`
  collapses the box instead of reading as auto (new `has_w`/`w_pct`
  bookkeeping in CwvNode).
- A CSS width now NARROWS the element's content flow: text inside a
  `width:300px` panel wraps at 300px instead of painting outside its
  box.

### HTML tags added

- `<u>`, `<s>`, `<del>`, `<strike>`, `<ins>` (underline/line-through
  UA defaults, `strike`+`del` strike-through, `ins` underline) and
  `<mark>` (yellow highlight box).

### The <img> audit

Verified sound (no action needed): magic-byte decoder dispatch
(PNG/JPEG/BMP; extensionless URLs and query strings work), PNG
preflight + the 4096px GPU texture cap, the 24-entry per-page texture
cache (group-freed on page leave AND shutdown; refuses instead of
evicting so no node can hold a dangling texture), the broken-image
placeholder + compiled-in glyph + alt text, link-wrapped images
(anchor href inherited for hit-testing), object-fit
(contain/cover/fill), image-rendering (pixelated/auto filter sync),
aspect-preserving size derivation from width/height attributes and
the tabindex model.

Fixed:

- Oversized-image clamp was 320px, not the content width:
  `content_w` used `page_right - page_left` (margin minus margin,
  ~= 0), so EVERY oversized image clamped to the 320px fallback. Now
  the real flow width (viewport minus page margins) is used, like
  Chrome.
- `data:` URI images (base64 payloads - the canvas.toDataURL form)
  now decode inline with no file or network round trip; resolve_url
  passes `data:` URLs through untouched.
- `%XX` escapes in local paths percent-decode like Chrome on a
  file: URL (`sp%20ace.png` opens `sp ace.png`).
- `width:50%` on an image resolves against the content width
  (previously collapsed to 1px once percentage widths landed).

Fixed along the way (regression traces the reference gate caught):

- WORD-WRAP TEXT LOSS (real bug since the wrap code was written): on
  the first space wrap the continuation pointer `p` was repointed
  INSIDE the scratch line buffer, whose NUL silently ended the walk -
  dropping the rest of the paragraph (basic.html lost "to exceed the
  viewport width of the Vita screen." on EVERY run and the golden had
  blessed it). The continuation is now derived in the SOURCE buffer.
- FLOW NARROWING LEAKED PAST ITS ELEMENT: the save of the parent flow
  happened AFTER the CSS width resolution narrowed it, so a
  `width:60px` div permanently narrowed the flow for every sibling
  after it (styled.html's `.note` rendered in a stale 400px box;
  bgimage_test.html's final <img> clamped to 60px). The save now
  happens BEFORE the narrowing. Both golden layouts were re-blessed
  for this; the third re-bless covered the recovered basic.html
  paragraph.

### Known gaps (documented in docs/RENDERING_AUDIT.md, deliberate)

GIF/WEBP/SVG decoding (needs per-port decoders), srcset /
loading=lazy, `new Image()` from JS (scripting layer), non-base64
data: URLs, percentage heights.

### Test coverage

- webapp/nested/img_demo.html: data: URI image, `width:50%` image,
  `sp%20ace.png` escaped-path image (asset "sp ace.png" added).
- docs/RENDERING_AUDIT.md: full audit findings + the new CSS batch.
- Golden reference re-blessed 3x (recovered wrapped line, fixed
  flow-leak layouts, img clamp fix); all 24 gates green.

---

## 2026-10-05 (64) - Real-build fixes: Wii net header, Wii U script UX, Windows cursor + PC controls

Fixes from David's on-device build run (2026-10-05), where the real
toolchains caught what the desktop gates could not, plus the Windows
control feedback.

- Wii fetch (fatal): `cwv_fetch_net.c` included `<ogc/net.h>`, which does
  NOT exist in libogc - the BSD socket layer lives in `gc/network.h`,
  included as `<network.h>` (verified against libogc master). Fixed the
  include, renamed the desktop gate stub to match the real header
  (stubs/wii/network.h; the old stub's "libogc master, ogc/net.h" comment
  documented a header that was never there), and corrected README-WII.md.
- Wii `-Wformat-truncation`: "../"-climbing URL join could write
  base[600]+input into url[1024]; the join buffer is now 2048.
- build-wiiu.sh: (1) `n -target` was rejected ("Unknown metadata flag") -
  `-target` is now accepted like on Vita (informational on Wii U; the RPX
  is always CWebView.rpx, the prompt says so); (2) answering 'n' to the
  toolchain install prompt EXITED even with a full toolchain installed -
  'n' now skips the package sync and continues (the existing devkitPPC
  check still exits with a clear message when the compiler is genuinely
  absent). build-wii.sh had the same exit-on-'n' flaw; fixed there too.
- Windows cursor bug (user report "the cursor just doesn't show up"):
  WM_SETCURSOR hid the system cursor because "the engine draws its own"
  - but the Windows port never draws the art cursor (console-only
  feature), so nothing was drawn at all. Now the OS cursor stays visible
  and becomes CSS-aware: WM_SETCURSOR asks cwv_host_cursor_at() and picks
  the matching system shape (hand/IBeam/wait/cross/help/sizeall/no);
  `cursor: none` is honored; the Settings "CSS cursors" toggle reverts to
  the plain arrow.
- Windows PC-native controls (user request): LShift+RShift+Space (or F1)
  opens/closes the CWMenu - the PC stand-in for L+R+SELECT; Space now
  activates the focused element (and confirms in menus) like the console
  Cross; Ctrl+R reloads alongside F5; Ctrl+wheel / Ctrl+= / Ctrl+- /
  Ctrl+0 zoom in/out/reset (75-200%, persisted via the Settings dirty
  flag). Full map documented in sources/win/README-WIN.md and the README
  KBM section (the "same grammar everywhere" claim now says Windows uses
  its own PC map).

## 2026-10-05 (63) - KBM on every port that can take it + one CWMenu row grammar

Keyboard-and-mouse support, port by port, plus the long-standing style gap
between the script-defined CWMenus and the native Settings/DevTools screens
closed with a single shared row grammar.

- PS Vita: new `sources/vita/cwv_kbm_vita.c/.h` - Bluetooth HID keyboard and
  mouse through SceHid (sceHidKeyboardRead/sceHidMouseRead, re-enumeration so
  devices paired mid-run still attach). The keyboard synthesizes SceCtrl
  bits, so the page, CWMenu, Settings and DevTools customize mode all answer
  it like a second D-pad/stick; a BT mouse drives the virtual cursor
  (left tap, wheel page scroll, right click opens the menu) independent of
  the rear-pad toggle. main.c polls it once per frame; CMake links
  SceHid_stub and exports SceHid.
- Wii: USB keyboards via libwiikeyboard (KEYBOARD_Init(NULL) - the NULL
  callback means no stdin takeover). One shared key grammar with Windows:
  F1 menu, arrows, PgUp/PgDn, Home, Tab, Return, Escape, F5; build-wii.sh
  links -lwiikeyboard.
- Wii U: USB keyboards and mice through SDL2 (SDL_KEYDOWN with the same key
  grammar, SDL_MOUSEWHEEL for vertical steps + tilt-wheel horizontal
  scroll). The SDL2 desktop stub now mirrors SDL 2.0 keyboard/wheel events
  with the real keycode constants.
- Windows: unchanged (already native keyboard/mouse/tilt-wheel).
- New desktop gate `build_vita_kbm_gate.sh` compiles the Vita KBM unit
  against VitaSDK-mirroring stubs (hid.h checked 1:1 against upstream);
  registered in run_all_gates.sh (24 gates).
- CWMenu style unification (user request): the script-defined menus
  (default.cwm main/page_operations, extras.cwm more_main) now render with
  the exact Settings/DevTools row grammar in cwv_menu_ui.c - selected row =
  theme fill 0.23 + 4px accent bar, every row a 1px separator, 0.72 labels;
  main-menu action rows switched from the old 2px-edge boxes to the same
  grammar; footer back buttons use the Vita accent-fill + theme 0.92 edges;
  actionable text rows in custom menus get full row chrome. Tabs, theme
  slots and window chrome were already shared (cwm_theme) and stay untouched.

## 2026-10-03 (62) - ESM: one built-in provider everywhere + transform fast paths

Explained and unified ESM across the ports. Wii U, Wii and Windows already
ran modern JS through the shared built-in transformer; the Vita was the only
port loading a separate CwvESM.suprx runtime module - and that module
duplicated the transformer in RAM (the built-in fallback had to be linked
into the eboot anyway), slowed every boot by a module load, silently
disabled the TypeScript stripper while it was active, and was stubbed to
"return 0" on Vita3K so pages ran raw ES6 while the UI claimed ESM was on.

- Removed the CwvESM.suprx module entirely (sources/vita/esm_module,
  sources/vita/cwv_host_esm.c/.h, the CMake module/staging/stub/pack
  wiring and the VPK required-entry). The Vita now registers the shared
  built-in provider (cwv_host_enable_ts_builtin +
  cwv_host_enable_es_builtin) exactly like the other ports, keeps the
  boot self-test, and reports its verdict in DevTools, the debug overlay
  and the runtime log.
- Added transform fast paths in vw_es2015_transform: the import/export
  phase and the class/async block phase are skipped when their keywords
  are absent from the source. A plain ES5 script now costs one linear
  scan and zero heap allocations instead of two full passes plus an
  8x-source-length buffer (the Wii benefits most: ~24 MB of usable RAM).
  The lexical pass still always runs - destructuring and default
  parameters have no keyword markers. Keyword-in-string/comment false
  positives only make a pass run exactly as before, which is harmless.
- Updated the DevTools/console ESM strings on the Vita (module states
  are gone), the shared header comments, the Windows/Wii/Wii U platform
  rows, docs (EXTENDING_THE_RENDERER Recipe 8, PORT_ROADMAP,
  SUPPORTED_METHODS, SOURCE_MAP, VITA3K_PISKEL_TRIAGE) and the desktop
  main-gate/shared-gate scripts that referenced the module.

## 2026-10-03 (61) - Ship the git repo itself in the delivered archive

USER DIRECTION (2026-10-03): "We still want the GIT repo to exist?
Don't remove git at all... add this stuff back."  The lint-round zip
packaged only git-tracked WORKING-TREE files, which dropped the .git
directory - the full history the project carries on the local tree.
Delivered archives now ship CWebView/ with .git intact (full history
+ the commits above), so unzipping over the local project keeps the
repo.  Regenerated content (build/, desktop harness binaries, trace
outputs) stays out as before.  Also ignore python bytecode
(__pycache__/, *.pyc) left behind by tool lint runs, and the Windows
gate now invokes build-windows.sh via `bash` so gates survive an
extraction that drops Unix exec bits (the archive stores them, but
not every unzip honors them).

## 2026-10-03 (60) - Lint & hygiene round: clean repo, no shipped artifacts

USER DIRECTION (2026-10-03): "Do some linting and return the full repo
with the unnecessary files/folders removed.  Anything regenerated
should be removed and added to the gitignore if not already done so."
Plus: do not minify the webapp on Windows.

- **Artifact purge**: deleted stale build outputs that had crept into
  the tree - desktop_test/{es6_test, harness_layout, harness_repro,
  harness_webapp, harness_webapp_bin, user_test, es2015_unit/a.out} and
  the regenerated desktop_test/outputs/{diff,new,old,webapp_run}.txt.
  The latter four were previously TRACKED; git rm --cached keeps them
  out of the repo for good (golden_reference.txt stays tracked - it is
  the deliberate golden the reference gate compares against).
- **.gitignore**: added desktop_test/harness_* (covers the individual
  harness_old/new/ref lines), user_test, es6_test, es2015_unit/a.out.
- **Execute bits restored** on every .sh (the zip round-trip dropped
  them; build-windows.sh was unfetchable via ./ until re-chmodded),
  and git records the +x on tracked scripts.
- **Windows gate paths fixed**: build_win_gate.sh still checked the
  pre-unification build-windows/x64 layout; the unified build/ tree
  made the gate fail even though the exes linked fine.  Now checks
  build/windows/{x64,arm64}/CWebView.exe machine types as before.
- **No minified webapp on Windows** (user direction): stage_web now
  copies webapp/ verbatim next to the exe instead of running
  tools/minify_web.py.  Minification stays a console-build choice
  (Vita/Wii) where flash space matters.
- **Lint pass** (gcc/mingw -Wall -Wextra -Wshadow -Wpointer-arith
  -Wmissing-prototypes -Wstrict-prototypes over sources/shared +
  sources/win): all clean except five functions with no prototypes.
  cwv_backend_{set_zoom,get_zoom,clear_focus,set_scrollbars} were
  declared as function-local `extern` lines at each cwv_host_* call
  site - hoisted into cwv_backend.h once; cwv_host_fetch_url had no
  prototype anywhere - added to cwv_host.h.  No more warnings under
  the strict set.
- **Script lint**: bash -n passes on all 34 shell scripts (dash's
  sh -n "errors" are the documented bashisms in #!/usr/bin/env bash
  scripts), tools/*.py compile clean, no tabs or trailing whitespace
  in the C sources.
- 23 gates green after all of the above (reference golden unchanged -
  the prototype moves are declaration-only, no behavior change).

## 2026-10-03 (59) - Windows render parity II: image filtering like the consoles

USER DIRECTION (2026-10-03): "make sure that rendering is as similar
as possible to other platforms on windows."  Follow-up to (58): the
text-fit fix was only half the gap - image SAMPLING also diverged.

- **Image filtering parity** (sources/win/cwv_platform.c): the other
  ports sample textures on GPU hardware whose default filter is LINEAR
  (vita2d GPU, Wii U SDL, Wii GX); CSS image-rendering: pixelated is
  what switches to POINT.  The Windows software blit hard-coded
  NEAREST for every image - blocky on Windows, smooth on the
  consoles.  blit_scaled now honors the per-texture mag filter:
  bilinear sampling (sample centers, edge-clamped, alpha blended) for
  LINEAR, hard nearest for POINT - and textures load with the LINEAR
  default like the GPU ports.  The misleading `nearest` blit argument
  is gone; the filter comes from the texture itself.
- **Engine fix (shared, all ports)**: CwvNode.image_filter_state now
  starts at -1 ("never pushed") in node_defaults - image_rendering and
  image_filter_state both read 0 until first paint, so the
  draw-site's state check NEVER fired and the texture kept its
  load-time filter forever.  First paint now always syncs the texture
  with the CSS value (auto -> LINEAR, pixelated -> POINT) on every
  port.
- **Canvas present**: frame_end now presents with HALFTONE instead of
  COLORONCOLOR - identical at the default 960x544 (1:1), but a resized
  window gets the smooth TV-like scaling a console output gets instead
  of dropped-pixel nearest.
- golden reference regenerated (--update): the expected new
  "TEXFILTER linear" first-paint pushes (12 occurrences) - drift is
  the engine fix working, not a regression.
- 23 gates green; both Windows architectures (x64 + ARM64) rebuild
  clean.

## 2026-10-03 (58) - Windows render parity: text fits its elements; scroll documented

USER DIRECTION (2026-10-03): make the Windows port render exactly how
the other ports do - "the major thing is that text is too large for its
element container and overflows outside of it. Maybe add mouse scroll
support too?"

- **Text overflow fixed** (BUG 59): the engine draws text at 32 px per
  renderer unit with a 28 px/unit LINE BOX (cwv_html.c line_height);
  the Vita PGF and Wii U TTF calibrations keep the glyph cell inside
  that line box.  The Windows port passed the engine's 32 px/unit size
  straight into CreateFontW as the EM size - GDI's cell (ascent +
  descent + internal leading, ~1.15x em) then measured ~36 px/unit
  against the 28 px line box, so text overflowed every container.
  Fix (sources/win/cwv_platform.c): per-face calibration - a one-time
  GetTextMetrics probe measures the real cell ratio, and every measure/
  draw scales the em so the CELL height lands at exactly 28 px/unit
  (font_fit).  Wrapping still measures what is drawn (both go through
  the same factor).  The system-font seam now follows the engine's
  32 px/unit contract instead of the fixed CWV_WIN_BASE_FONT_PX=24;
  CWMenu/menu text is visually unchanged (~24 px at z=1).  The old
  constant is documented as removed in cwv_win_internal.h.
- **Mouse scroll**: already present and verified in the message loop
  (WM_MOUSEWHEEL -> cwv_host_scroll_by page scroll, CWMenu wheel
  scrolling when a menu is open; README-WIN.md input table documents
  it) - no code change needed, now documented so it is discoverable.
- Both architectures rebuild clean (x64 + ARM64 exes via build-windows.sh;
  desktop win gate links real binaries).

## 2026-10-03 (57) - Wii build ships: boot.dol packaging, software keyboard, HTTP fetch

USER DIRECTION (2026-10-03): "make the Wii binary be built; finish the
Wii build and make sure the features supported on Vita/WiiU are
supported for the Wii build."  The Wii port was engine-complete but
compile-check only - this round links it and closes the feature gap.

- **Software keyboard** (NEW sources/wii/cwv_ime_wii.c/.h): libogc has
  no IME, so the keyboard is code-drawn through the engine primitives -
  QWERTY + digit/symbol layouts, one-shot SHIFT, IR-pointer key taps,
  D-pad navigation, blinking caret, password dots, B cancels.  The
  engine IME seam is now implemented (was a -1 stub): textfields open
  it, and the main-menu "Open URL" row prefills it with the current
  page relative to web/ (the Vita convention: "./nested/x.html",
  "../" climbs clamped at web/, remote URLs refused).  main.c routes
  ALL input to it while open (modal) and draws it last over page +
  cursor, before GRRLIB_Render (the same "draw last" rule as the Wii U
  swkbd).
- **HTTP fetch** (NEW sources/wii/cwv_fetch_net.c): libogc net stack
  (if_config DHCP once, lazily - offline boots never stall), DNS,
  HTTP/1.0 + "Connection: close" (no chunked coding), 10s read
  timeout, 2MB cap.  https:// is refused: libogc ships lwip WITHOUT
  TLS; local + http:// pages and fetch() work, https reports failure
  through the engine (documented, not hidden).
- **boot.dol packaging** (build-wii.sh): full devkitPPC compile of the
  engine tree, GRRLIB/libogc link (homebrew template link line:
  -lgrrlib -lpngu -lfreetype -ljpeg -lpng -lz -lfat -lwiiuse -lbte
  -logc -lm), elf2dol -> boot.dol, and the HBC-ready SD package
  build/wii/sdcard/apps/cwebview/{boot.dol, meta.xml, icon.png, web/,
  assets/} driven by the package.json "wii" manifest section, with
  progressive build percentages.  The stale "NOT YET PACKAGED" echo is
  gone.
- **Boot fixes found by the packaging round**: fatInitDefault() was
  never called (sd: was never mounted - settings, web pages, fonts and
  the log would all have failed on hardware); the runtime log wrote to
  a HARDCODED sd:/apps/cwebview/ path instead of the package folder the
  app actually runs from (argv[0] root).
- **Desktop Wii gate** (build_wii_gate.sh): now also syntax-checks the
  keyboard and fetch modules; new libogc-mirroring stubs ogc/net.h
  (if_config/net_gethostbyname/net_socket/net_connect/net_send/
  net_recv/net_setsockopt/net_close), ogc/fat.h, and GRRLIB_FlushTex -
  wrong constants still fail on the desktop, not on the console.
- Docs: README-WII.md "done/not yet" list + manifest note updated.
- STILL NOT DONE (honest): real-hardware/Dolphin validation - the
  first boot.dol flash should happen in Dolphin with the SD pack
  mounted; TTF baseline anchoring (GRRLIB_PrintfTTF draws TOP-anchored
  while the engine speaks baselines) is tuned at that flash if page
  text sits high/low (same BUG 58 family as the Windows round).

## 2026-10-03 (56) - Repo organization round: artifact purge + .gitignore

USER DIRECTION (2026-10-03): organization round - minimize files and
shrink the repo WITHOUT removing anything important. First console
build round passed on all four platforms (Vita/Wii U/Windows/Wii).

- Purged every compiled test artifact from the tree and from tracking:
  ASan builds, harness binaries, stale pre-rename binaries
  (es6_test). desktop_test shrinks 34MB -> 2.5MB; the only trace kept
  is outputs/golden_reference.txt (the regression fixture the
  check_reference gate diffs against). old/new/ref_run/ref_diff/
  webapp_run/webapp_trace/layout_trace are gate OUTPUTS, regenerated
  on every run - deleted.
- Added .gitignore covering the unified build/ tree, per-platform
  build dirs, object files, and every gate-produced binary/output so
  they never get committed again (they previously entered history,
  bloating .git to 26MB).
- Removed stray local build output dirs (build/, build-windows/) from
  the tree.
- Fixed the two Wii main.c warnings from the build round (unused
  'ticks' counter; %.255s precision for the boot-screen URL copy).
- Gates: 23/23 green after cleanup (binaries rebuild from source by
  the gates themselves, proving nothing load-bearing was removed).

## 2026-10-02 (55) - Console build fixes from first real build round

USER BUILD ROUND (Vita/Wii U/Windows/Wii, 2026-10-02): Vita failed on
strcasecmp, Wii failed on the removed GRRLIB_SetPixel API, Windows
reported a text offset, missing web/ staging, and a unified build
output tree (user direction).

- BUG 58 (Vita): cwv_js.c used strcasecmp() for the *.ts extension
  check - not declared under VitaSDK. Replaced with the file-local
  js_ci_eq() (same case-insensitive semantics, already used for the
  type="text/typescript" attribute match).
- BUG 59 (Wii): GRRLIB master removed GRRLIB_SetPixel (4.4.1 called it
  GRRLIB_SetPixelTotexImg). cwv_platform.c now writes GX_TF_RGBA8
  texels directly (4x4 blocks, 32-byte A/R plane + 32-byte G/B plane,
  matching GRRLIB's own RawTo4x4RGBA layout), rounds the texture to
  4-pixel multiples for allocation, restores the logical w/h for layout,
  and flushes once via GRRLIB_FlushTex. No GRRLIB texel API dependency
  anymore.
- BUG 57 (Windows): build-windows.sh never staged the webapp. The exe
  loads <app dir>/web/index.html, so the minified webapp is now staged
  as web/ next to each built exe (same rule as Vita/Wii U app dirs).
- BUG 58b (Windows): text was offset from its element by the font
  ascent - engine draw sites pass a BASELINE y (documented at the Wii U
  seam) but TextOutW defaults to TA_TOP. draw_text_impl now sets
  SetTextAlign(TA_LEFT | TA_BASELINE). Also fixed the two
  -Wformat-truncation warnings in sources/win/main.c (precision-limited
  copies into s_last_url / s_menu_url_prefill).
- Unified build output tree (user direction 2026-10-02):
  Windows build-windows/ -> build/windows/{x64,arm64}/, Vita VPK ->
  build/vita/<TARGET>.vpk (was project root), Wii compile artifacts ->
  build/wii/ (was /tmp), Wii U already build/wiiu/.
- NOTE for local trees synced by unzipping the release archive: files
  RENAMED in this repo (cwv_es6_core.*, es6_test*, es6_unit/,
  es6.html) linger in such trees and get picked up by the
  sources/shared/*.c globs. Delete them once (see the hand-off list in
  chat) - the archive does not contain them.
- Suite: 23 gates, FAILURES=0 (all desktop gates re-run after the
  seam fixes; the win gate compiles the Windows sources with mingw).

## 2026-10-01 (54) - Per-edition ES sources (ES#### year scheme), ES2023/ES2024 libraries

USER DIRECTION: start on more ESMs, with sources SEPARATED BY VERSION
NAME (ES#### with the year; ES6 files rename too). Continue edge-case
bug testing; console build results verified by the user ("no errors;
passed" flow).

- Reorganize the ES language layer by edition year: cwv_es6_core.c/.h
  -> cwv_es2015_core.c/.h (ES6 = ES2015), API vw_es6_transform ->
  vw_es2015_transform, hooks type VwEs6Hooks -> VwEsHooks,
  cwv_host_enable_es6_builtin -> cwv_host_enable_es_builtin. Gates and
  fixtures renamed to the year scheme (es6_unit -> es2015_unit,
  build_es6_test.sh -> build_es2015_test.sh, es6.html -> es2015.html,
  build_es6_portability_gate.sh -> build_es2015_portability_gate.sh).
  CHANGELOG history entries keep the original names they were written
  with.
- Split the monolithic 12KB library prelude out of the core into ONE
  FILE PER SPEC EDITION: sources/shared/sources/web/js/modern/cwv_es2015_lib.c ..
  cwv_es2024_lib.c, each exposing cwv_es<year>_lib_prelude().
  cwv_es_editions.c concatenates them in spec order (the ES2015 base
  first: later editions extend its Promise/jobs runtime). The core now
  contains only the syntax transform engine. The split is byte-exact
  (verified programmatically); every host and the CwvESM suprx use the
  combined prelude through the same accessor.
- Add genuinely new editions: ES2023 gains toSorted/toReversed/
  toSpliced/with (findLast/findLastIndex already existed); ES2024 adds
  Object.groupBy, Map.groupBy and Promise.withResolvers.
- Wire the edition files into every build: Vita CMake (app + esm_module
  CMake), build-windows.sh, the Wii U glob, and all desktop gates.
- New gate desktop_test/build_es_editions_unit.sh: evaluates the
  combined prelude in a fresh Duktape context and checks observable
  behavior for EVERY edition (30+ assertions incl. async drains),
  normal + ASan/UBSan. Suite now 23 gates, FAILURES=0.
- Update docs/SUPPORTED_METHODS.md with the per-edition source layout
  and the new ES2023/ES2024 library coverage.

## 2026-10-01 (53) - TypeScript support (type stripper + engine wiring), error-handling audit gate

USER DIRECTION: implement TypeScript ("I wouldn't think it would be that hard");
answer where new options should live (project/CWM/build settings).

- Add the shared TypeScript stripper `sources/shared/sources/web/typescript/cwv_ts_core.c/.h`
  (vw_ts_strip): one scanner, one output, a small context stack
  (params / declarators / class bodies / nested blocks). Strips
  annotations, interfaces/type aliases/declare/import type, rewrites
  enums to the classic JS pattern, handles class modifiers, param
  properties, accessors, method generics, extends type args, implements
  (dropped), overload signatures (dropped), index signatures (dropped),
  as/satisfies casts, non-null assertions, attached-form generic calls
  and catch (e: T). Plain JS passes through untouched; unsupported
  constructs (namespaces, decorators, .tsx) return -2 so the console
  reports an honest error instead of emitting broken JS.
- Wire TS into the engine: renderer ts_transform hook
  (js_set_ts_transform), backend setter (cwv_backend_set_ts_transform),
  host provider (cwv_host_enable_ts_builtin) registered on all four
  ports next to the ES6 builtin; Vita CMake, Windows build script and
  the Wii U glob pick up cwv_ts_core.c. Scripts route by
  type="text/typescript"/"application/typescript"/"module/typescript"
  or src="*.ts" (new js_run_ts_source path); the strip runs BEFORE the
  ES6 transform so TS and modern JS compose. Inline TS without a
  provider logs an explicit error instead of running raw TS.
- Add the error-handling audit gate `desktop_test/build_error_audit.sh`
  (+ error_audit_test.c, testdata/error_audit/): malformed HTML/CSS
  pages and a broken CWMenu script through the REAL engine, normal and
  ASan/UBSan; JS error semantics verified browser-accurate (a syntax
  error kills only its own script; later scripts run). It found and
  fixed a CWMenu `pair` validation tautology (the always-false check
  never fired). Now also runs a TS page end-to-end (enum, class with
  param properties, arrow with annotation + return type, casts,
  non-null) and asserts zero console errors.
- Add the TS unit gate `desktop_test/build_ts_unit.sh` (ts_unit.c):
  38 cases covering every supported/unsupported construct, passthrough
  safety for plain JS, normal + ASan/UBSan. Both gates joined
  desktop_test/run_all_gates.sh (22 gates, FAILURES=0).
- Update docs/SUPPORTED_METHODS.md with the TypeScript subset and its
  documented limits (no type checking, no namespaces/decorators/.tsx,
  no object-literal method params, no annotations inside template
  expressions).

## 2026-10-01 (52) - App-specific logging, debug-build logging gate, BMP on Wii, Windows port (AMD64 + ARM64)

- Fix the Vita logging bug behind the garbled IMG diagnostics: Vita's printf
  lacks %zu (printed "zu" and shifted later arguments). Cast all logged sizes
  to unsigned long; audit every format string reaching a log.
- Make runtime logs app-specific. Vita: ux0:data/CWebView/<namespace>/CWV.log,
  namespace from storageDirectory (default Title ID), directory chain created
  lazily on first write. Windows: <exe folder>/CWV.log. Update stale doc
  references to the old fixed ux0:data/CWV.log path.
- Disable file logging in debug/dev builds (CWV_DEBUG=1) on Vita, Wii and Wii
  U per user direction; production builds keep the forensic log.
- Confirm images on all platforms and fix the real gap found: BMP silently
  failed on Wii (GRRLIB sniffs only PNG/JPEG). Add a portable, unit-gated
  decoder sources/shared/cwv_bmp.{c,h} (24/32bpp BI_RGB both row orders,
  standard-mask BI_BITFIELDS; RLE/palettes rejected with logged reasons) and
  wire it into the Wii texture loader via GRRLIB_CreateEmptyTexture/SetPixel.
  Document the full per-platform image matrix in docs/PORT_ROADMAP.md.
- Add desktop_test/bmp_unit.c + gate (11 assertions) and extend the Wii gate
  (shared decoder syntax check; declare GRRLIB_WidthTTF which was missing from
  the stub and warning).
- Start the Windows port (user direction: amd64/arm64 or universal):
  sources/win/cwv_platform.c implements the whole seam natively - Win32/GDI
  software frame (DIB section), WIC decode via CreateDecoderFromStream
  (CreateDecoderFromMemory is Win8+/missing from mingw's vtable), GDI text
  with a ttf-family parser and private font loading, a Win32 IME dialog,
  WinHTTP fetch (http/https), ShellExecute browser, LOCALAPPDATA cache dir.
  sources/win/main.c: app loop mirroring the console ports (host create,
  built-in ES6 transformer + self-test, load <exe dir>/web/index.html,
  mouse/wheel/key mapping, app-specific CWV.log with the same debug gate).
- Add build-windows.sh with progressive-percentage output: cross-builds real
  CWebView.exe for AMD64 (x86_64-w64-mingw32-gcc) and ARM64
  (aarch64-w64-mingw32-clang, auto-detected), verified machine types.
- Add desktop_test/build_win_gate.sh (link both PEs, verify machine types,
  SKIP cleanly without cross toolchains) and desktop_test/run_all_gates.sh;
  all 20 gates pass.
- Add docs/PORT_ROADMAP.md (platform/image matrix + Android, Switch, Unix,
  Linux plans) and sources/win/README-WIN.md; link both from README and
  update SUPPORTED_METHODS with per-platform image decoding.

Verification: 20/20 desktop gates green (shared main/js/es6/cwmenu/loghook
gates, Wii U + Wii + ES6 portability syntax gates, path/approot/cursor/
img-cache/menu/theme units, BMP unit, Windows AMD64+ARM64 link gate with
PE machine-type checks). git diff --check clean. Console/hardware image
rendering and Windows runtime behavior still need device runs; no VitaSDK
or Windows machine available in the sandbox.

## 2026-09-30 (51) - Build completion output, support inventory and PNG triage

- Remove the redundant Vita builder Built target message after CMake completion;
  retain a separate validated/copied VPK result and final artifact path.
- Add docs/SUPPORTED_METHODS.md with HTML, CSS, JS-TS and WASM separators,
  extracted from the actual tag/property/method declarations. Explain value/API
  subsets, ES2022/ES13 feature subsets versus conformance, and planned-only
  TypeScript/WASM/WASI support. Link it from README and the historical audit.
- Reject short local image data before magic-byte reads. Reject full-cache
  requests before native decode, preserving already-cached lookup behavior.
- Preflight PNG signature/IHDR, 1..4096 dimensions, chunk bounds and IDAT/IEND
  before the native loader. Remove unsafe signature-skipped retries, including
  the built-in broken-image path. Log PNG decode entry/return with dimensions.
  Preflight does not validate CRCs/compressed pixels or make the length-ignoring
  native Vita buffer loader a fully bounded reader.
- Add six image rejection cases: one/seven-byte local data, truncated IHDR/tail,
  8192px dimensions and an out-of-bounds chunk. Verify no texture allocation.
- Add docs/VITA3K_PISKEL_TRIAGE.md and update HANDOFF. The supplied log establishes
  a Vita3K host write violation following the loading_bg.png open record, not
  the faulting function. Record the mutex/config issues and missing reproduction
  evidence without claiming these hardening changes fix the reported crash.

Verification: all 18 desktop build/check scripts passed, then transformer unit
execution reported ALL UNIT CASES PASSED. Image cache/rejection tests pass with
balanced texture ownership; golden renderer comparison remains unchanged.
bash -n build-vita.sh and git diff --check passed. No VitaSDK/Vita3K runtime
verification or exact Piskel-Sharp dev-build reproduction was available.

## 2026-09-30 (50) - Class execution regressions and Vita C portability

- Extend class execution with basic private-field/private-method name rewriting,
  base fields before the constructor body, derived fields after direct super(),
  static inheritance and super handling in accessor bodies. Private names are
  ordinary __name properties, not enforced privacy or brand-checked identities;
  complete super receiver semantics remain open.
- Fix empty parameter copying, ordinary methods named get/set being mistaken for
  accessors, duplicated parentheses and trailing commas in super call rewrites,
  class-member comment handling, and missing statement terminators after emitted
  class declarations. Preserve the hash-rewritten allocation if a subsequent
  super-rewrite allocation fails instead of leaving a dangling source pointer.
- Resolve the user's Vita transformer build errors: replace POSIX strdup with
  the local heap-copy helper and correct that helper's owned return type to
  char *. Remove unused locals, explicitly mark the unused optional-chain hook,
  and clarify misleading comment-scan indentation. No warning suppression or
  feature-test macros were introduced.
- Add build_es6_portability_gate.sh: strict C99/C11 syntax checks with
  -Wall -Wextra -Werror.
- Add es_class_selftest.html in test_pages and its desktop testdata mirror:
  22 class assertions through the actual load/transform/compile/execute/DOM path.
  Check the exact summary, success color, PASS rows and absence of FAIL rows.
  A deliberate failed assertion was verified to fail the gate, then restored.
- Refresh HANDOFF, BUILD_NOTES, TEST_PAGE and the public transformer header.
  Remove stale unsupported-feature claims and unverified console-completion
  claims. Explicitly retain WASM, AssemblyScript, TypeScript and WASI as planned,
  not implemented, work.

Verification: all 18 desktop build/check scripts passed after the final source
fix, followed by ALL UNIT CASES PASSED from the transformer unit executable.
The integration gate checks 23 ES6, 21 ES7 and 22 class-page assertions. Seven
class probes also passed an AddressSanitizer/UndefinedBehaviorSanitizer run
(leak detection disabled in the temporary driver). Golden renderer comparison
remained unchanged. The class-page mirrors are byte-identical; git diff --check
passed. VitaSDK is unavailable here: the actual ./build-vita.sh -y VPK build
and console runtime remain to be verified on the user's SDK installation.

## 2026-09-26 (49) - ES7+ self-test page; transformer fixes the page flushed out

USER DIRECTION: write rigorous test pages for the new JS additions
  (spread / destructure / for-of patterns / pattern params); also answer
  "is the Cemu RPX-direct boot failure just Cemu?".

NEW PAGE: test_pages/es7_selftest.html (mirrored into
  desktop_test/testdata/), same t() harness as the ES6 page: 21 tests -
  object spread (basic/overwrite/override/chains/nested), array + call
  spread (incl. literals around a spread), var + paren destructuring,
  for-of array patterns (braced and single-statement bodies), object /
  array pattern parameters, pattern defaults, plain defaults, rest
  params, braceless arrow with array-pattern params.  es6_test.c loads
  it through the REAL transform+Duktape pipeline and pins the console
  line, the #summary row and its green color, and all 21 PASS rows.

TWO REAL TRANSFORMER BUGS the page caught (both were invisible because
  the old 20-case dbg harness only tested one-liners in isolation):
  1. Braceless arrow bodies are re-transformed via a RECURSIVE pass
     whose output starts empty - output_expr_position refused an empty
     output, so a body BEGINNING with "[" (e.g. `() => [...a, ...b]
     .join()`) kept its raw spread.  try_array_spread now accepts a
     start-of-output "["; statement-start patterns still lose to
     try_assign_destructure_array, which runs first.
  2. Complex (pattern/default/rest) ARROW parameter lists were rebuilt
     without a "function" keyword: `var ap = ([a, b]) => a + b` became
     `var ap = (__vw_pN) { ... }` - a syntax error.  The rebuild now
     binserts "function " after the btruncate (order matters: the
     insert shifts positions, and doing it before the truncate ate the
     opening paren - first patch attempt produced "f__vw_p2)").

CEMU ANSWER (no code change): RPX-direct is already covered by the
  BUG 43 fs:/vol/content + /vol/code probes.  If the old
  "FSC: File create failed ... wiiu/CWebView.log" text still appears,
  that is the PRE-(46) fallback path - rebuild the RPX first.  The
  decisive Cemu-log line is our OSReport: "CWV: app root (vol/content
  ...)" (found) vs "CWV: no app root" (not found; stage web/ beside the
  RPX).  Real hardware (Homebrew Launcher) passes a real argv[0], so it
  exercises a different path than Cemu RPX-direct.

GATES: all 11 green after the fixes (check_reference zero-diff at 5419
  lines, es6 gate now also covers the es7 page).

## 2026-09-26 (48) - ES7+ spread, paren destructure, for-of patterns, pattern params: all 20 harness cases green

USER DIRECTION: continue the ES7+ transformer work and record WASM /
  AssemblyScript / TypeScript / WASI viability in the handoff.

ES7+ CORE (sources/shared/cwv_es6_core.c): object/array spread
  transforms to Object.assign (prelude gained __vw_aread/__vw_spread);
  paren assignment destructure `({a} = o);` -> `var __vw_dN = o; a =
  __vw_dN["a"];`; for-of with array/object patterns
  `for (var [k, v] of arr)` -> indexed loop + destructure; pattern
  params `function p({x, y})` -> `function p(__vw_pN) { var x =
  __vw_pN["x"]; ... }` via build_params + prologue injection.

FIXES DURING THE HUNT: params_need_rewrite counted a pattern's own
  `{` into the nesting BEFORE the nest==0 test, so patterns were never
  detected; the output-tail scan-back in try_for_of_pattern broke one
  past the opening bracket (var-check read `r [` instead of `var`);
  destructure_emit's object branch rejects dupn'd patterns ending at
  a NUL (*end == 0 guard), so try_paren_destructure now passes the
  SOURCE pointer and build_params / for-of append a non-NUL char.
  All 33 temporary PD/DE probe fragments were removed from the file.

DEBUGGING LESSON (hit twice): inserting probe statements on their own
  line inside one-line `if (...) return 0;` bodies makes the return
  unconditional - the fallout (dead code deleted by GCC) looks like an
  engine bug.  Probe by replacing the return in place.

GATES: all 11 green (check_reference.sh zero-diff at 5419 lines,
  build_main/js/es6/cwmenu_unit/loghook/wiiu/wii/paths/shared, and
  es6_unit).  HANDOFF.md refreshed: ES transformer state section added,
  planned-modules section (WASM/AssemblyScript/TypeScript/WASI viable,
  Node/PHP not viable) recorded from today's direction.

## 2026-09-26 (46) - Wii U Cemu report: FSC log-create fix, WUHB boot message, Wii wiiuse include, truncation warnings

USER DIRECTION (hardware report): the Wii U error screen still shows on
  boot and Cemu logs "FSC: File create failed for
  <Cemu>/sdcard/wiiu/CWebView.log"; the Wii build dies at
  sources/wii/main.c:32 `ogc/wpad.h: No such file or directory`; the
  Wii U build still prints -Wformat-truncation notes at
  cwv_platform.c:186.

ANALYSIS: the Cemu log shows the title loaded was the WUHB itself
  (.../build/wiiu/CWebView.wuhb [WUHB]) and the mlc01 SD card has
  NOTHING staged (not even sdcard/wiiu/ - which is why the log's
  create failed).  Per the owner's own rule (33) the WUHB carries only
  the RPX - web/ deliberately stays outside - so WUHB-direct in Cemu
  can never find content.  Three real bugs on our side though:

WII BUILD FIX (the fatal one): libogc has no ogc/wpad.h - the Wiimote
  API lives in wiiuse/ (include/wiiuse/wpad.h; ogc/pad.h at line 31
  passing while line 32 failed was the tell).  sources/wii/main.c now
  includes <wiiuse/wpad.h> and the desktop stub moved to
  desktop_test/stubs/wii/wiiuse/wpad.h to mirror the real header.

WII U LOG FALLBACK FIX (the FSC failure): with no app root the log
  went to sd:/wiiu/CWebView.log, assuming sd:/wiiu/ exists - a fresh
  Cemu mlc01 has no sdcard/wiiu/ at all, so FSCreateFile failed and
  diagnostics vanished (the exact class of bug 35).  The fallback now
  targets the CANONICAL app folder fs:/vol/external01/wiiu/apps/
  CWebView/{CWebView.log,settings.cfg} and a new
  wiiu_ensure_dir_chain() mkdir -p's the whole path first (WUT fs
  devoptab -> FSCreateDir).  Even with nothing staged, the log now
  lands somewhere.

WII U BOOT SCREEN: the no-root branch now LEADS with the WUHB answer
  ("Loaded a .wuhb? It carries NO web/ by design") and spells out the
  Cemu route: copy build/wiiu/content/* to <Cemu>/mlc01/sdcard/wiiu/
  apps/CWebView/, then File -> Load executable -> the staged
  CWebView.rpx, NOT the .wuhb.  Hardware/Aroma users get their line
  too (fs:/vol/external01/wiiu/apps/ is scanned automatically).

TRUNCATION WARNINGS: scan_apps_dir's snprintf join (apps_dir + '/' +
  d_name can exceed 256) is now a memcpy build into an oversized
  cand[514] with an explicit length check, the winning root is copied
  by memcpy (never silently truncated - too-long candidates are
  skipped), and the g_root_source note uses %.73s precision.  Verified
  silent at gcc -Wall -Wextra -Wformat-truncation=2.

USER ACTION (not a code change): to run in Cemu, either
  CWV_CEMU_MLC=/mnt/c/Users/<user>/AppData/Roaming/Cemu/mlc01
  ./build-wiiu.sh -y  (auto-deploys the staged package), or copy
  build/wiiu/content/* to that mlc01/sdcard/wiiu/apps/CWebView/
  manually, then Load executable the RPX.  Loading the .wuhb directly
  cannot work - the WUHB is rpx-only by design (33).

TESTS: all gates green (wiiu, wii, main, shared, cwmenu unit, img
  cache, loghook, approot, paths, shared sources, golden reference,
  menu hotkey).  The truncation fix additionally proven warning-free
  at -Wformat-truncation=2.

## 2026-09-26 (45) - CSS cursors, centered hotspot, settings polish, build fixes

USER DIRECTION: prefer the cursor target at the CENTER of the art (so
  round cursors can be small), support real cursors via the CSS
  `cursor` rule (owner will draw a Chrome-style cursor pack), gate it
  behind a setting, and clean up the crowded settings menu without
  changing its style.  Plus: fix the pasted build failures.

CURSOR HOTSPOT (owner preference): the shared cursor art contract is
  now CENTER-anchored - the pointer position is the art's center, so
  round cursors are exact.  cwv_cursor_set_hotspot(CWV_CURSOR_TIP)
  restores the old top-left-tip contract for tip-shaped art; the
  code-drawn fallback arrow stays tip-anchored (sources/shared/sources/core/cwv_cursor.h).

CSS CURSOR RULE: `cursor: auto|default|none|pointer|text|wait|
  crosshair|help|move|grab|grabbing|not-allowed|zoom-in|zoom-out` is
  parsed (sources/shared/sources/web/css/cwv_css.c css_prop_cursor), inherited like
  color (cwv_html.c), and resolved per frame on the element under the
  pointer (cwv_host_cursor_at, cwv_renderer.h CwvCursorInfo).
  `cursor: url(img) [x y]` loads engine-owned custom art (local file
  or the fetch hook for http/https) with the CSS hotspot applied.
  The port applies the resolved style via cwv_cursor_set_style();
  pack art loads lazily from <package>/assets/cursors/<keyword>.png
  with failed loads remembered (no per-frame retry storm), falling
  back to console art, then the code arrow.  cursor:none hides the
  cursor but taps still land.

SETTING: Settings > Appearance gains the "CSS cursors" row (default.cwm
  content-row-3, action toggle_css_cursors), persisted in settings.cfg
  as css_cursors=0|1, default on.  The Vita per-frame hook (main.c)
  only queries the engine when the row is On.

SETTINGS CLEANUP: the Appearance tab's fourth row fits the existing
  template rhythm (rows 0-3, no crowding; the controls tab keeps its
  project-gated three).

BUILD FIXES (from the pasted hardware report):
  - build-wiiu.sh line 664: CWV_CEMU_MLC unbound under set -u -> all
    reads are ${CWV_CEMU_MLC:-} now.
  - build-wii.sh: the platform compile-checks pass -DGEKKO -DHW_RVL
    (gctypes.h "Invalid platform selected" was the missing flag), and
    sources/wii/cwv_platform.c uses the current GRRLIB API:
    GRRLIB_LoadTTF(buf, size), GRRLIB_Circle(x,y,r,color,filled);
    the desktop stub (desktop_test/stubs/wii/grrlib.h) mirrors the
    real header so the Wii gate checks the same signatures.
  - Wii U truncation warnings: cand[] is bounded to g_app_root's size
    (cwv_platform.c scan_apps_dir) and the boot screen's line[] is 512
    (main.c draw_boot_screen), silencing both -Wformat-truncation notes.

TESTS: cursor_test.c extended (26 checks: center/tip hotspot switch,
  style application, cursor:none tap-through, missing-pack fallback,
  failed-load memory, url() art + hotspot anchoring at the exact
  TEXSCALE coordinates, engine art never freed).  All gates green:
  cursor, menu_ui, cwmenu_unit, wiiu, wii, main, shared, es6, js,
  theme, menu_hotkey, img_cache, loghook, approot, paths, shared
  sources, golden reference (5419 lines identical).

DOCS: CONFIGURATION.md - hotspot contract, the CSS cursor rule, the
  Chrome-style pack layout (assets/cursors/), and the setting.

NOTE FOR THE OWNER: the pack art set to draw is default/pointer/text/
  wait/crosshair/help/move/grab/grabbing/not-allowed/zoom-in/zoom-out
  (.png, drawn 1:1, on-screen size ~24-48px, CENTER-anchored).

## 2026-09-26 (44) - Wii U web boot: verified Cemu behavior + mlc01 deploy (homebrew-standard research)

USER DIRECTION: the Wii U "app0:/web/ on start" error is THE bug and
  must be fixed WITHOUT packaging web/ into the RPX; check how other
  homebrew apps handle their content folder.

RESEARCH (what other homebrew do): on hardware the Homebrew Launcher
  standard carries the app - HBL launches sd:/wiiu/apps/<app>/<app>.rpx
  passing the FULL path as argv[0] and chdir()ing into the folder, which
  is why ordinary homebrew just open side files with relative paths.
  Our probe chain already implements that standard (argv[0] folder ->
  cwd -> canonical SD -> SD scan).  Cemu is the outlier and its source
  (src/Cafe/CafeSystem.cpp, PrepareForegroundTitleFromStandaloneRPX)
  settles the reported failure mode:
    - "File -> Load executable" passes an EMPTY argstr: no argv[0], no
      usable cwd, so steps 1-2 cannot match (that is why the engine
      kept the Vita-default URL root and showed the app0:/web/ error).
    - The loaded rpx's own folder is mounted at /internal/code/ - an
      FSC-internal path the app CANNOT open through the fs: devoptab.
      The folder next to a bare directly-loaded .rpx is invisible.
    - /vol/content mounts from the host ONLY for the game title layout
      (rpx inside a folder literally named "code" with a "content"
      sibling) and from a loaded WUHB's content partition.
    - The one always-visible writable tree is the emulated SD card:
      <Cemu>/mlc01/sdcard/ == fs:/vol/external01/.

FIX (build-wiiu.sh, BUG 44): optional automatic Cemu SD deploy - build
  with CWV_CEMU_MLC=<Cemu>/mlc01 and the staged package is copied to
  <Cemu>/mlc01/sdcard/wiiu/apps/CWebView/, mirroring how homebrew is
  dropped onto a real SD card; the runtime SD scan then finds it.  Web
  content stays OUTSIDE the RPX/WUHB (33).  The package-ready summary
  now states the mlc01 copy as the Cemu route and explains why bare
  direct-loading cannot work (instead of implying it could).

DOCS/ACCURACY (43 follow-up): README-WIIU.md - the /vol/content probe
  row and the Cemu testing section rewritten around the VERIFIED Cemu
  behavior above (the (43) claim "Cemu mounts the rpx folder at
  /vol/content" was too broad - only the code/content title layout and
  wuhb content mounts do); boot-screen hint corrected the same way and
  now tells the user exactly what to copy where.

GATES: all desktop gates green (build_and_compare.sh excluded as
  always - it needs the original single-file backend as an argument).

## 2026-09-26 (43) - cursor hides behind CWMenu; Cemu app0:/web/ boot fix; GRRLIB freetype2

USER DIRECTION: the pointer cursor must stop rendering whenever ANY
  CWMenu is open (it used to draw on top of the dimmed page outside the
  menu window too); troubleshoot the Wii U "app0:/web/" boot error in
  Cemu although web/ exists next to the RPX; fix the Wii build failure
  from the pasted output (GRRLIB_ttf.c: ft2build.h not found).

CURSOR (all three ports): cwv_cursor_draw() is now skipped while any
  menu is open - Wii U (s_menu.mode != CWV_MENU_CLOSED), Wii (same
  shared-shell gate) and Vita (legacy g_menu.mode == MENU_CLOSED).
  Placement/tap routing keeps updating (A taps the menu at the cursor
  position); only the rendering is suppressed.

WII U ROOT PROBE (BUG 43, cwv_platform.c): Cemu's "File -> Load
  executable" on a bare .rpx passes no usable argv[0]/cwd and mounts
  the loaded RPX's OWN FOLDER as the title content dir - so after
  argv[0]/cwd/canonical/SD-scan all miss, the probe now tries
  fs:/vol/content, then fs:/vol/code (the folder NEXT TO the RPX seen
  through Cemu's mount - NOT content inside the package, which stays
  rejected), and finally app0: (a hand-packed WUHB with web/ inside).
  Without a match the engine kept the Vita default URL root and
  reported "app0:/web/" - the reported error.  Defense in depth:
  map_path() rewrites any app0:/-prefixed path onto the resolved root
  once one is known.  Boot-screen hint and README-WIIU.md document the
  extended chain (including the mlc01/sdcard copy route).

WII BUILD (build-wii.sh, BUG 43): the synthesized
  powerpc-eabi-pkg-config shim only emitted -I<portlibs>/ppc/include,
  but GRRLIB's Makefile asks `pkg-config freetype2 libpng libjpeg
  --cflags` and ft2build.h lives in the freetype2/ SUBDIR - the shim
  now adds -I.../include/freetype2 (and -lfreetype to --libs), plus a
  warning when the ppc-freetype headers are missing.  pngu (libpng
  only) was unaffected, which is why the build died exactly at
  GRRLIB_ttf.c.

GATES: all desktop gates green (build_and_compare.sh excluded as
  always - it needs the original single-file backend as an argument).

## 2026-09-26 (42) - CWMenu 4: linked menu scripts + More submenu + modern page operations

USER DIRECTION: support multiple/linking CWMenu scripts so a .cwm can
  reference another .cwm, open it, and render it instead of the main
  menu; add a main-menu button row that opens a "modified main menu"
  (same rows, devtools, page meta) in the inline settings/devtools
  grammar as a SUBMENU, without substantially changing the stock main
  menu.  Also modernize the Page Operations menu.

PARSER (cwmenu_api.c/h, version 4): new cwmenu_script_link() parses a
  second .cwm INTO the state the primary script built - theme slots and
  project defaults merge, vars merge, every `menu <name>` block REPLACES
  that menu (compiled-in targets and dynamic menus alike), and the
  reported script version folds to the maximum.  Menu blocks now reset
  their content on entry in BOTH modes, so a linked script restyles one
  menu without redefining the whole file (also makes duplicate `menu`
  blocks in a single file replace instead of append).
  cwm_script_version_supported() -> 4.

HOST SHELL (cwv_menu_ui.c/h):
  - new element action `open_script:<file>[|<menu>]`: the port resolves
    <file> against its cwmenu directory through the new
    CwvMenuPlatformOps.load_script hook and links it (CWMenu 4); the
    linked script's <menu> (default "main") then opens as a CUSTOM menu.
    A missing script fails soft: the current menu stays open and the
    main footer shows "script not found: <file>" until the next action.
  - the main menu CONFIRM handler is now template-driven (rows read
    their action from the loaded script, like the touch path), so
    scripts can re-target main rows without C changes.
  - CUSTOM menus support the main menu's live meta: elements named
    page-icon / page-info draw the real favicon, title, url and control
    mode, so a linked "modified main menu" shows the live page.
  - main menu gains the "More" row (open_script:extras.cwm|more_main);
    rows re-spaced 204/242/280/318, D-pad selection 0..4.
  - PAGE OPERATIONS modernized: rounded window chrome (template
    `corner rounded`), the live page URL right-aligned on the title
    line, and right-aligned state values on the Back/Forward rows
    ("available"/"oldest page"/"newest page") - the settings-grammar
    value rhythm.  The stock main menu is otherwise untouched.

SCRIPTS (sources/shared/cwmenu):
  - extras.cwm (NEW, CWMenu 4): the "more_main" submenu - the stock
    main menu's row set (Page Operations / Open URL / Settings /
    DevTools) plus the live page meta strip, laid out in the inline
    settings/devtools grammar with rounded chrome.  Inherits
    default.cwm's theme; adding a theme block there re-skins the whole
    shell while linked.
  - default.cwm: More row added; page_operations window rounded.

PORTS: Wii U and Wii implement load_script (bare file names resolved
  against <app root>/cwmenu, path separators rejected); build-wiiu.sh
  now stages EVERY sources/shared/sources/menu/scripts/*.cwm (CMake already globs all
  of them into the VPK).  Vita's legacy UI does not use the shared ops
  struct, so open_script: rows are inert there until it adopts the
  shared shell.

TESTS: cwmenu_unit section 10 (link: register/replace/merge/version
  fold/idempotent re-link/missing-file fail-soft) and the main-menu
  layout gate updated to the 4-row More layout; menu_ui_test gains the
  full More-row journey (D-pad + touch, live meta, submenu actions,
  fail-soft footer feedback).

GATES: all desktop gates green.

## 2026-09-25 (41) - hardware report: three build-script fixes

USER DIRECTION: ran all three builds on the real WSL/toolchain tree.
  Two aborted, one mislabeled the manifest - all three fixed here.

BUG 41a (build-vita.sh: "VITA_CURSOR: unbound variable" at line 506):
  the cursor-art resolution block sat AFTER the CMAKE_ARGS array that
  consumes $VITA_CURSOR, so set -u aborted before it was ever assigned.
  The block is hoisted ABOVE CMAKE_ARGS and the manifest getter is now
  BOOLEAN-SAFE: vita.cursor doubles as the feature switch, so JSON
  true = feature on + default art (the old getter printed the string
  "True" and treated it as an art path).

BUG 41b (build-wiiu.sh: "manifest wiiu.cursor not found: True - falling
  back to the default"): same boolean-vs-path confusion in the Wii U
  staging step; boolean/number values are now ignored as art overrides
  (strings only), so the warning only fires for a real missing path.

BUG 41c (build-wii.sh: GRRLIB pngu build dies - "powerpc-eabi-pkg-config:
  not found" -> "pngu.c: png.h: No such file or directory"): pngu's
  Makefile runs `$(PREFIX)pkg-config libpng --cflags`, i.e.
  powerpc-eabi-pkg-config, which ships with the ppc portlibs in
  $DEVKITPRO/portlibs/ppc/bin - on PATH only in a devkitPro-configured
  shell, not a plain WSL root shell.  build-wii.sh now puts the
  portlibs bin on PATH before the GRRLIB sub-makes; if the wrapper is
  absent it synthesizes a minimal one (libpng --cflags/--libs only,
  the single pkg-config call GRRLIB makes) and warns when png.h is
  missing.  Re-run ./build-wii.sh -y to install GRRLIB.

GATES: all desktop gates green (build_and_compare.sh remains the stale
  legacy tool needing the (36)-removed backend header - not a gate).

## 2026-09-25 (40) - CWMenu 3 project gates + Cursor page mode

TWO batches landed together (the tree carried the CWMenu-3 project work
from the last sessions; the cursor page mode finished today).

PROJECT FEATURE KEYS ((39) wiring): package.json gained per-console
  cursor / zoom / scroll / devtools / history (true/false, default ON
  when absent).  All three build scripts and CMakeLists.txt translate
  them into CWV_<KEY>_ENABLEED defines; sources/shared/sources/core/cwv_project.h is
  the compile-time default.  cwv_project_publish() is now FILL-ONLY:
  runtime/project values always win over host-supplied ones.
  webapp/cwebview.schema.json documents the five new keys.
  Features that are off disappear from the menu entirely:

CWMENU SCRIPT VERSION 3: new `project` block + `when <expr>` element
  conditionals.  The host injects effective values with cwm_project_set()
  before loading; `when zoom` (or `when !devtools`) drops every element
  whose switch is off at parse time.  default.cwm upgraded to v3 and
  now gates the cursor/zoom/scroll/history menu items.  Unit tests:
  section-9 defaults + the `when` gates in cwmenu_unit.c, plus the
  permanent project gate in the cwmenu unit suite; menu_ui_test asserts
  the whole DevTools control-mode override cycle.

PORT GATING: Vita, Wii U and Wii cursor / zoom / scroll input paths now
  sit behind the CWV_*_ENABLED gates (Wii U cursor+zoom axis, Wii
  art-load path, Vita dispatch).  All feature-off compile flavors pass
  the syntax gates.

CURSOR PAGE MODE ((40)): <html cwv-control-mode="cursor"> (backend id 4).
  - Vita: the pad becomes a mouse - left stick + D-pad move the shared
    pointer cursor, Cross taps (hold to drag), Circle back, Triangle
    refresh, Square menu, L/R history; native stick scroll/zoom axes are
    skipped so the stick stays the pointer; a stranded tap is cancelled
    if the page swaps mode mid-press.  CWV_CURSOR_ENABLED=0 falls back
    to the None behavior.
  - Shared UI: cwv_control_mode_name() helper and a REAL BUG FIX - all
    three ports labeled backend mode 1 "Standalone" and 2 "Touch"
    (the old override numbering); browser/standalone/touch were
    displayed wrong.  Now 1 Browser / 2 Standalone / 3 Touch / 4 Cursor.
  - DevTools override cycles Default -> Browser -> Standalone -> Touch
    -> Cursor using the backend numbering (the override row on all
    three ports, since the menu is shared).
  - Demos: webapp/control_cursor.html (new), and control_modes.html +
    control_standalone.html fixed - they still used the DEAD
    vita-control-mode attribute the shared parser never read, so they
    were silently parsing as mode 0.  The webapp harness renders the
    new cursor page.

GATES: all build_*.sh / check_*.sh green (main gate covers production
  + debug).  desktop_test/build_and_compare.sh is now a STALE tool - it
  needs the original single-file backend and its header, which (36)
  removed; do not treat its argument error as a gate failure.

## 2026-09-23 (38) - console cursor placement + package.json "cursor" override key; archives now ZIP

USER DIRECTION: "console-separated cursors... they would go in the app's
shared assets, not the webapp" + a package.json override key.

PLACEMENT (as directed): the APP's shared assets folder (repo root
  assets/, sibling of font.ttf), per console:
  - assets/cursor-vita.png   touch ring (26x26 RGBA)
  - assets/cursor-wii.png    pointer arrow (24x38 RGBA)
  - assets/cursor-wiiu.png   pointer arrow (24x38 RGBA)
  All three now EXIST (generated at 4x supersampling, downsampled).
  The build stages the art into each package at the fixed runtime path
  <package>/assets/cursor-<console>.png (Vita: app0:/assets/cursor-vita.png,
  which is where sources/vita/main.c already looked).

OVERRIDE KEY: per-console "cursor" in package.json (webapp-relative,
  exactly like "icon"; absolute paths pass through) - chosen over the
  suggested CSS-like "--override-cursor" so it matches the existing
  manifest style:
    "vita": { "cursor": "vita/cursor.png" }, etc.
  Resolution order in the build: manifest key -> repo default
  assets/cursor-<console>.png -> no art = code-drawn arrow (warn, not
  an error).

WIRING:
- build-vita.sh: resolves VITA_CURSOR (manifest vita.cursor -> default)
  and passes -DVITA_CURSOR_PNG; CMakeLists packs it into the VPK at
  assets/cursor-vita.png (appended AFTER the pack-args reset - the
  first attempt appended before set(CWV_PACK_ARGS) and would have been
  clobbered, caught in review).
- build-wiiu.sh: stages assets/cursor-wiiu.png into the package next
  to font.ttf, with the same manifest wiiu.cursor resolution.
- build-wii.sh: documented the contract; staging lands with the
  packaging step (the app loop port) - runtime path already matches.
- sources/shared/sources/core/cwv_cursor.c: removed the hardcoded
  "app0:/assets/cursor.png" generic fallback (app0: is Vita-only;
  shared code never hardcodes it) - missing art now simply means the
  code-drawn arrow.  Header contract updated.
- webapp/cwebview.schema.json: "cursor" documented on all three console
  sections; CONFIGURATION.md gained a "Pointer cursor art" section;
  README-WII's art line mentions the override.

ARCHIVE FORMAT: user direction - from this entry on, all archives
  return as .zip (not .tar.gz).

All 17 desktop gates pass (build_and_compare is a tool, not a gate).

## 2026-09-23 (37) - hardware report: build-wii.sh GRRLIB build fails, "Please set DEVKITPPC in your environment"

USER REPORT: ./build-wii.sh -y reaches the GRRLIB source build and
  dies in lib/pngu: "Makefile:7: *** Please set DEVKITPPC in your
  environment ... Stop."  The toolchain check had passed and the clone
  succeeded.

CAUSE: build-wii.sh sets DEVKITPPC as a plain shell variable for its
  own toolchain checks, but never EXPORTS it.  GRRLIB's pngu/Makefile
  reads DEVKITPPC from the ENVIRONMENT; a sub-make sees neither the
  variable nor DEVKITPRO (whose install target also uses it).

FIX: build-wii.sh now exports DEVKITPPC and derives + exports
  DEVKITPRO (/opt/devkitpro by default, dirname of DEVKITPPC) before
  the GRRLIB build/install, so both the compile and the install target
  see them.  No other changes.

All 17 desktop gates pass.

## 2026-09-23 (36) - hardware report: Vita link failure (undefined cwv_cursor_*); CMake shared-source wiring gate

USER REPORT (on the previous archive, reproducible in the latest):
  build-vita.sh fails at link: undefined reference to cwv_cursor_init/
  _draw/_position/_show/_place in sources/vita/main.c.  Also a
  -Wformat-truncation warning from cwv_host.c set_error under the
  Vita's gcc 15.

CAUSE: sources/shared/sources/core/cwv_cursor.c (added in (33) when the cursor
  asset moved into the package folder) was never added to the
  VitaWebview target's source list in CMakeLists.txt.  The Wii U
  build GLOBS sources/shared/*.c so it linked fine; every desktop
  gate compiles the file too - but the Vita build lists sources
  explicitly and the desktop suite never links the real Vita target,
  so nothing caught it until a real vitasdk build.

FIXES:
- CMakeLists.txt: sources/shared/sources/core/cwv_cursor.c added to the
  VitaWebview target (right after cwv_broken_image.c).
- New gate desktop_test/check_shared_sources.sh: every
  sources/shared/*.c must appear in CMakeLists.txt unless whitelisted
  (whitelist: cwv_menu_ui.c, the Wii U-only menu renderer), CMake
  must not name nonexistent files, and the whitelist must stay
  current.  Negative-tested by removing cwv_cursor.c from the list:
  the gate catches it.  Desktop suite is now 17 gates.
- cwv_host.c: host->last_error widened 256 -> 320 bytes, sized for
  the 300-byte message buffers callers pass (kills the gcc-15
  -Wformat-truncation warning; no behavior change).

The user is on the (34) archive; both fixes apply to the latest tree
- this changelog entry ships in the (36) archive.

## 2026-09-23 (35) - hardware report: Wii U boot went completely blind (no text, no page, no log); app0: still lived in pgf_load_default

USER REPORT: "I checked multiple places, but nothing could be found!
No text on start, no page on start, and no file was written!"

DIAGNOSIS - all three symptoms were ONE root cause (no app root
resolved), amplified by two separate oversights:
- s_log_path stayed EMPTY when no root matched -> wiiu_runtime_log
  wrote NOTHING anywhere ("no file was written").
- The boot font is loaded from <root>/assets/font.ttf -> NULL without
  a root, and draw_boot_screen returns before drawing anything
  ("no text on start").
- The entry URL could never resolve ("no page on start").
AND, found while tracing this: cwv_pgf_load_default STILL requested
  the Vita-only "app0:/assets/font.ttf" (missed in (33)'s app0 purge)
  -> the Wii U menu font was ALWAYS NULL since (33) - which is why
  "CWMenu still works" showed shapes but no text.

FIXES:
- Built-in 8x8 ASCII fallback font (cwv_font8x8.h, generated from
  assets/font.ttf by the new tools/gen_font8x8.py - high-res render,
  tight crop, downsample; ~1KB of data, NOT the 742KB TTF embedded).
  When the TTF cannot be opened, cwv_pgf_load_default returns a
  singleton bitmap font; all four text seam functions (font/pgf,
  width/draw) render it as scaled pixel rects through
  cwv_draw_rectangle, same virtual-px coordinate/baseline contract as
  the TTF path.  The boot screen and CWMenu are readable even when NO
  file can be opened at all.
- cwv_pgf_load_default now loads <package root>/assets/font.ttf (the
  app0: request is gone), falling back to the bitmap.
- The boot font falls back to the default font (and thus the bitmap)
  instead of leaving draw_boot_screen textless.
- Log and settings ALWAYS have a destination: with no app root they
  go to fs:/vol/external01/wiiu/ (sd:/wiiu/CWebView.log +
  CWebView.settings.cfg - exists on every Aroma / Cemu-with-SD
  setup).  The boot screen now prints the log path it is using.
- Root resolution gained a getcwd() probe between argv[0] and the
  canonical probe - the Homebrew Launcher and some forwarders chdir()
  into the app folder instead of passing a usable argv[0] (labeled
  "cwd" in the diagnostics).
- Boot diagnostics now self-describe: root source (argv[0]/cwd/
  canonical/scan), the launcher's raw argv[0], and the log path.

NOT DONE: the Wii port's boot font has the same blind-failure
potential (mitigated by the Homebrew Channel's guaranteed argv[0]);
an 8x8 fallback there is a follow-up.  All 15 desktop gates pass; the
bitmap renderer itself is stub-compile-gated (needs Cemu/hardware to
eyeball glyph quality).

## 2026-09-23 (34) - hardware report: build-wii.sh TMP crash; Wii U root resolution hardened (3-step + boot diagnostics)

USER REPORTS, two items:
1. `./build-wii.sh` on a real devkitPro box: packages resolve fine now,
   but `TMP: unbound variable` at line 81.
2. Wii U: the WUHB no longer loads content from inside the package
   (correct), but the app does not FIND the `web/` folder next to it -
   nothing loads; CWMenu still works (it needs no content root).

CAUSE 1: `TMP` was only assigned inside the "devkitPro pacman not
  installed" branch; with the toolchain already present the branch is
  skipped and `set -u` aborts at the GRRLIB section.  Also `sudo make
  install` would fail on a root box without sudo installed (the
  reporter IS root).

CAUSE 2: root resolution relied entirely on the launcher passing the
  package path in argv[0].  HBL does; Aroma's homebrew-on-menu,
  forwarders and (reportedly) Cemu do not - argc/argv arrive empty or
  bare, so step 1 found nothing, the canonical probe assumed the
  folder is NAMED "CWebView", and any user-chosen folder name lost.

FIXES:
- `build-wii.sh`: `TMP` is created once at the top for EVERY path;
  escalation is `SUDO=""` when already root (no sudo dependency).
- Wii U root resolution (`cwv_wiiu_app_root`) is now three-step:
  (1) the folder argv[0] names, validated against its `web/` folder;
  (2) the canonical `fs:/vol/external01/wiiu/apps/CWebView`; (3) a
  scan of `fs:/vol/external01/wiiu/apps/` for `web/` PLUS one of our
  own `CWebView.rpx`/`.wuhb` packages - covering any folder name
  under the standard Wii U homebrew layout, both a `.wuhb` file in
  `wiiu/apps` with `web/` beside it and a package folder.  The old
  fs:/vol/content probe stays gone (content must not load from the
  package).
- Boot diagnostics: the boot screen and OSReport log now say WHICH
  step resolved the root (`argv[0]` / `canonical` / `scan: ...`), and
  when nothing matches they print the raw launcher argv[0] - a
  failing setup identifies itself instead of a blank page.
- Wii port unaffected (the Homebrew Channel always passes boot.dol's
  full path in argv[0], which is guaranteed by HBC itself).

All 15 desktop gates pass (the wiiu scan path is syntax-gated on
desktop; it needs real hardware/Cemu to verify).

## 2026-09-23 (33) - app content root per console: app0: is Vita-only; Wii/Wii U load the package folder

USER DIRECTION: app0: is a REAL Vita device and stays Vita-only.  The
Wii U and Wii must load their web content from the FOLDER THE PACKAGE
SITS IN - `.../exampledir/s.rpx` loads `.../exampledir/web/` - never
from inside the .rpx/.wuhb itself.  The Open URL prompt should resolve
to the package's own web folder.

- Added the per-console app content root to the platform seam
  (`cwv_platform_set_app_root`/`cwv_platform_url_root`/
  `cwv_platform_path_root`, `sources/shared/sources/core/cwv_platform.h`): one
  URL-space <-> physical-storage pair, defaulted to the exact Vita
  layout (`file:///app0` <-> `app0:`) so untouched builds behave
  identically.  All hardcoded `file:///app0/`-vs-`app0:/` prefix/slice
  pairs in `cwv_backend.c` (page load, resource load, image load and
  diagnostics) and `cwv_host.c` (file URL conversion, local href
  resolution, site-rooted `/x` now `<root>/web/x`, error text) now go
  through the configured pair.  A fresh slice fixed a lost separator
  slash (`app0:web/...`) caught by the golden renderer gate.
- Wii U: `cwv_wiiu_app_root_init(argv[0])` takes the package folder from
  the launcher-passed rpx/wuhb path (validated against its `web/`
  folder), the engine URL root is set to it at boot, and the
  `fs:/vol/content` (WUHB content partition) probe is GONE - that was
  "loading from the RPX", which the user rejected.  Log
  (`CWebView.log`), settings (`settings.cfg`), cache, manifest
  (`web/package.json`), favicon, cursor art, default font and the
  CWMenu script all resolve under the package folder.  The boot-failed
  screen explains the `web/`-next-to-package layout, and the native
  Open URL prompt prefills with the package web folder's entry page
  instead of `https://`.
- Wii: the same treatment (`cwv_wii_app_root_init(argv[0])` from the
  Homebrew Channel's boot.dol path, `CWV_WII_APP_ROOT` only as the
  fallback probe), cursor art/font/cache/CWMenu script/entry URL moved
  off the `app0:` translation - which is now deleted (no virtual prefix
  left to map).
- Build scripts: `build-wii.sh` now installs devkitPro's REAL packages
  (`devkitPPC`, `libogc`, `libfat-ogc`, `ppc-libpng/freetype/
  libjpeg-turbo`) - the earlier `wii-devkitppc/wii-libogc/wii-grrlib`
  names were guesses pacman could not resolve - and builds GRRLIB from
  its GitHub source (it has no package, per its README).
  `build-wiiu.sh` drops `wuhbtool --content`: the WUHB is code + icon
  only; web content ships next to it.
- Tests/docs: new `desktop_test/approot_test.c` gate (defaults keep
  Vita exactly, custom root loads and rejects app0-style URLs, hrefs
  and queries stay inside the root, `..` never escapes the package
  folder, restoring defaults round-trips), with the stub mapping the
  test's Wii-style root onto the shared fixtures.  README, both port
  READMEs and header comments updated to the package-folder story.
  All 15 desktop gates pass, including the golden renderer reference.

## 2026-09-23 (32) - Wii port: CWMenu shell, IR cursor, real part draws, bundled font

- Brought the Wii port (libogc + GRRLIB) from platform-layer-only to a full app loop with the shared CWMenu shell: `sources/wii/main.c` drives the host NATIVE at 640x480 (pages adapt like a 4:3 browser window) and scales the Vita-authored menu by `cwv_menu_ui_set_scale(640/960)`; HOME opens/closes the main menu, MINUS/PLUS switch tabs, D-pad navigates, B is Back, A taps the cursor or activates focus.
- Added a shared pointer-cursor module (`sources/shared/cwv_cursor.{h,c}`): console art with a code-drawn fallback arrow, clamped placement, rounded tap positions, draw-only-when-visible, safe destroy. The Wii wires it to the Wiimote IR pointer (always on when tracked, like the Wii Menu) with `app0:/assets/cursor-wii.png`; A taps at the cursor on the page or the menu, release ends the tap wherever it lands. GameCube pad mirrors A/B/D-pad.
- Replaced the Wii stand-in part draws with real `GRRLIB_DrawPart` sub-UV draws (texture-pixel source rects, matching the engine contract), shipped the bundled `assets/font.ttf` as the Wii default face (size-aware width + draw, 32px-per-unit PGF contract), and gave the port a writable `app0:/cache` for remote fonts.
- Gave the shared shell a `fixed_hotkey` flag: a port with a non-remappable menu key (Wii HOME) pins the Preferences "Menu shortcut" row to "HOME (fixed)" instead of a dead cycle. Wii settings persist at `sd:/apps/cwebview/settings.cfg` in the same key=value format as the Wii U, the saved theme applies before the first page paints (the Wii U flash lesson), boot diagnostics retry every 2s, and the devtools live data reports vblank-locked 60fps metrics.
- Hardened the desktop Wii gate: new libogc-mirroring stubs (real WPAD button masks, IR struct, PAD/GRRLIB signatures) and `build_wii_gate.sh` now compiles the whole port surface plus the shared shell units against them. Added `desktop_test/cursor_test.c` (fallback arrow, clamping, tap position, visibility) and menu-UI scale/guard tests (scaled panel geometry, inverse touch map, Vita-only pointer row inert by default and active when enabled, fixed-hotkey row inert). The "Open URL" row reports unavailability instead of a dead button (no libogc IME yet).
- Not done: Wii software keyboard, HTTP fetch, boot.dol packaging, real-hardware/Cemu validation. All 14 desktop gates pass including the golden renderer reference.

## 2026-09-23 (31) - Wii U CWMenu parity, configurable shortcut, text-color stability

- Set the Wii U default menu toggle to **L+R+SELECT** (SDL button indices 6+7+11, confirmed from devkitPro's Wii U SDL2 source). Settings > Preferences now offers L+R+START or either combo and can show/hide the DevTools Exit App control; both persist per app, with old six-field settings accepted. Exit App is hidden by default. Single START/SELECT and B no longer open or exit the application.
- Routed the Wii U port's real D-pad button events (12..15, not joystick-hat-only), and honored settings/DevTools scroll and zoom input modes, scroll speed, and controller axes. Input stays in the Wii U port; Wii-specific source is unchanged. Fixed settings tab return from the optional Close confirmation.
- Expanded the shared CWMenu shell's rendering with generic icons, badges, value/progress/list/segmented/checkbox/shape/line widgets, passive paired-window drawing with touch click-through prevention, dynamic named menus and `open_menu:<name>` script buttons, plus local favicon previews with URL-aware cache cleanup. More advanced Vita-specific bindings and the debug overlay remain out of scope.
- Targeted the Wii U color flashing: the saved theme is set before the first HTML/JS page load; unchanged per-frame theme calls do not repaint body defaults; repeated JS `cssText` color updates propagate to inherited text without marking it self-styled; SDL_ttf uses a bounded 64-entry texture cache keyed by font/color/text and explicitly blends glyph alpha. These are code-level fixes, **not hardware-confirmed**. Cemu/GamePad verification is still required.
- Expanded desktop regression coverage for repeated JS color changes, menu shortcut edges, persistence UI, dynamic menus and paired-window click-through. All 14 desktop gates pass, including the Wii compatibility gate; Wii U compile checks use SDK-mirroring stubs, not DevkitPPC hardware output.

## 2026-09-22 (30) - unified menu geometry and Wii U CWMenu shell (Wii unchanged)

- Unified the bundled page-operations and three settings `.cwm` layouts with the DevTools shell: plain text tabs, 20/106/700/270 content panel, 42px rows, footer separator and action row. Vita settings now paints text tabs and the new separators.
- Added a console-neutral menu UI module in `sources/shared/cwv_menu_ui.{h,c}` for the bundled main menu, page operations, settings and eight DevTools sections. Includes template-backed painting through a four-operation vtable, bounded navigation history, scrollable logs, menu actions, touch/pad navigation, live diagnostics and runtime overrides. This does not replace Vita's legacy menu implementation yet.
- Wired the Wii U port to that shell using SDL2 paint, GamePad/touch input, the native swkbd URL prompt, persisted settings, scoped cache clearing, and live zoom. The page and menu share the 960x544 virtual canvas, scaled by the existing platform seam to 854x480; pointer coordinates use its inverse fit. Added alpha blending for menu overlay and packaged `cwmenu/default.cwm` with RPX/WUHB content.
- Added `desktop_test/menu_ui_test.c` and its build script. All 13 desktop gates pass, including the unchanged Wii compatibility gate. No Wii-specific source changed. DevkitPPC/Cemu/physical GamePad validation remains outstanding; optional CWMenu dynamic-window/widget parity and the Vita-only debug overlay are not yet in the shared shell.

## 2026-09-21 (29) - es6 green-text flash-to-black; Vita GPU "under-allocated" (leak, not a limit); Wii U screen fit

USER REPORTS, two items carried over from (28)'s continuation:
1. es6 pages: text turns green (JS style change works) then flashes to
   black / becomes invisible.
2. Images still broken on Vita after (27)+(28); the GPU-under-allocated
   suspicion needed a real root cause.

CAUSE 1 (green -> black flash, reproduced in a desktop harness):
  cwv_backend_set_theme() re-propagates default text colors on every
  REAL theme flip.  It skipped nodes with an EXPLICIT color but
  re-tinted nodes that INHERITED one - es6_demo sets body { color:
  #f8fafc }, so every child carries the page color via inheritance with
  color_explicit=0.  A light-mode pass repainted them rgba(25,25,30):
  invisible on the dark page (the "flash to black").  Browsers never
  let the app theme overrule the page's cascade.
  FIX: new CwvNode.color_inherited_explicit, set at parse time
  (node_defaults from r->body_color_explicit; node_from_parent carries
  explicit||inherited), and the theme pass now skips both explicitly
  and inherited-explicitly colored nodes.
  VERIFIED: desktop repro (es6_demo + click #inc + 5 simulated frames
  + dark/light flips) - inherited #f8fafc survives, the JS-set green on
  #out survives.  Standing gate: desktop_test/build_theme_test.sh.

CAUSE 2 (Vita GXM exhaustion - a LEAK, not an allocation limit):
  decoded image textures were owned by NODES and freed only in
  backend_shutdown (app exit).  Every navigation re-parsed into the
  same renderer without freeing the previous page's node.image /
  node.bg_image - GPU texture memory accumulated page over page until
  libvita2d/GXM allocations failed and every later image came up as
  the broken-image placeholder.  Also img_test-style pages re-decoded
  the same URL once per tag (11 tags -> 11 textures).
  FIX: renderer-owned per-page image cache (CwvRenderer.img_cache,
  24 entries): load_image keys on the URL, every <img> and CSS
  background-image borrows from it, backend_load frees the whole
  table (plus the page's @font-face webfonts) before re-parsing, and
  backend_shutdown frees it for good.  A full table REFUSES the new
  decode (placeholder + img_diag) rather than evicting: an evicted
  entry could still be referenced by an earlier node, and freeing it
  would leave a dangling texture pointer.  Refusing is visible-but-
  safe and still bounds GPU memory.
  VERIFIED: desktop_test/build_img_cache_test.sh - 11 <img> tags with
  2 unique resolvable URLs -> 2 decodes, textures freed at navigation,
  8 browsing rounds leave no accumulating live textures, shutdown
  balances allocs == frees.

ALSO (backend_load): the same 12-vs-13 prefix-slice bug fixed for
  load_resource in (27) was still present in backend_load itself:
  strncmp(...,12) != 0 passed but url+12 kept the separator slash,
  producing app0://<page>.  Now full-prefix compare + url+13.

WII U SCREEN FIT (sources/wiiu/cwv_platform.c + main.c):
  the GamePad is 854x480, the Vita reference 960x544 - laying out
  directly at 854x480 wrapped every line ~11% earlier and drew text
  ~75% larger than the Vita look (the (28) screenshots).  The engine
  now runs on a VIRTUAL 960x544 canvas (main.c host size); the platform
  seam fits it onto the framebuffer: x/w by 854/960, y/h by 480/544
  (exactly full coverage, no letterbox; the 0.8% non-uniformity IS the
  aspect correction), fonts measured AND drawn at 16 * 480/544 px/unit.
  Text calibration re-measured against the Vita screenshots:
  Vita PGF renders ~16px em per renderer unit (h1 1.5u -> 23px em,
  body 0.75u -> 13.2px em), so WIIU_PGF_PX_PER_UNIT 28 -> 16 - the
  28 was matching the engine's LINE-BOX height, not the Vita's glyph
  size.  Pointer input converts device -> virtual px before the
  engine sees it (cwv_wiiu_fit_pointer).  Boot log states both sizes.

ASSET: webapp/nested/pixel.png was the only grayscale PNG (colortype 0)
  in the package - the one format libvita2d's png reader handles least
  reliably.  Rewritten to RGBA (colortype 6, 91 bytes, same 8x8 pixels):
  the test asset is now the most-compatible format instead of the least.

GATES: 12/12 green (shared, main, wiiu, wii, paths, js, es6, loghook,
  cwmenu, golden reference, + the two new standing gates img_cache and
  theme).  Golden reference trace unchanged (5419 lines).

## 2026-09-21 (28) - images broken on ALL consoles (2 real causes, 1 theory cleared); Wii U text too large; dynamic entry page; lean packaging

USER REPORTS, four items:
1. "Text is now too large" on Wii U (after (27)'s fix); do NOT remove
   font-size CSS rule support.
2. Image rendering broken on ALL platforms - the broken-image
   placeholder shows instead of the actual image; "look for multiple
   causes".  Vita suspicion: GPU under-allocated memory ("isn't a
   definite cause").
3. Wii U should load the webpage DYNAMICALLY, not baked into the
   RPX/WUHB - "maybe via a manifest flag?".
4. All consoles: only the console-specific asset folder should be
   copied into the package - or, if the startup/banner/app images
   aren't dynamically loaded, don't copy them at all.

FIX 1 (text size, sources/wiiu/cwv_platform.c):
  a. WIIU_PGF_PX_PER_UNIT 32 -> 28.  32 overshot the engine's own
     geometry: cwv_backend.c lays out lines at line_height =
     font_size * 28 and puts the pen at baseline = font_size * 21 -
     the engine ALREADY assumes a 28px-per-unit em for every line box
     it draws, so 28 is the internally consistent scale (body 0.75u ->
     21px, ascent touches the line-box top, like Vita).
  b. Baseline anchor bug (the bigger half of "too large / off"):
     draw_utf8 anchored the SDL_ttf surface TOP at y, but the shared
     engine passes y as a BASELINE (vita2d pgf/freetype pen semantics:
     glyphs draw at pen_y - bitmap_top, verified against xerpi's
     libvita2d sources).  Every Wii U text sat a full ascent (~0.8 em)
     too low, overlapping the next line - now draws at
     y - TTF_FontAscent(f), for BOTH font seams.
  c. font-size CSS support untouched: parsing/clamping (0.1..8.0 units)
     and px conversion all live in the shared css code; only the Wii U
     display calibration changed.

FIX 2 (images, multiple causes as suspected):
  a. SHARED PATH BUG (the all-platforms one): load_resource and
     load_image matched the 13-char prefix "file:///app0/" with a
     LENGTH OF 12 and sliced at +12, so every local image URL became
     "app0://web/..." - a DOUBLE-SLASH device path.  On Vita sceIoOpen
     rejects it ("file read failed" -> placeholder); img_diag's own
     comment documents this exact bug pattern but only the DIAGNOSTIC
     had been fixed to 13 - the two loaders still sliced at 12.
     Both now use 13.  This also silently broke every external
     css/js/font fetched via load_resource from app0:.
  b. WII U DECODE BUG: cwv_tex_load_png_buffer/cwv_tex_load_bmp_buffer
     called SDL_RWFromConstMem(buf, -1), which treats the buffer as a
     C string and truncates at the FIRST NUL byte - PNG chunk headers
     are 0x00-heavy, so even after (a) every buffer-decoded image
     failed with "decode/upload failed".  The shared seam now carries
     the buffer LENGTH: cwv_tex_load_png_buffer(buf, len) /
     cwv_tex_load_bmp_buffer(buf, len) (header + all four ports +
     desktop stub updated).  Vita ignores len (libvita2d's libpng
     reader pulls only what it needs); Wii U passes it to
     SDL_RWFromConstMem; Wii passes it through - fixing its
     bmp len=0 instant-NULL on the way.
  c. VITA "GPU UNDER-ALLOCATED" THEORY - INVESTIGATED, CLEARED:
     vita2d_init_advanced(4 MiB) (the main.c fallback) configures the
     TEMPORARY vertex/fragment pool, whose libvita2d DEFAULT is
     1 MiB (DEFAULT_TEMP_POOL_SIZE, xerpi vita2d.c) - 4 MiB is BIGGER,
     not smaller; textures allocate from the separate GPU heap and
     were never VRAM-starved by it.  The placeholder on Vita was cause
     (a) all along.

FIX 3 (dynamic webpage, sources/wiiu/main.c): the entry page is no
  longer baked into the RPX.  At boot the app reads the webapp's own
  manifest, web/package.json (shipped inside the web/ content), and
  takes its "webpage" field - the SAME field the build scripts read -
  so repointing the entry page is a content edit on the SD card, no
  rebuild.  Precedence: argv[1] > manifest "webpage" > compile-time
  CWV_WIIU_WEBPAGE fallback (always works, e.g. custom test builds).
  Manifest values are webapp-relative and get the "web/" prefix;
  traversal ("..") values are rejected.  Scanner verified against the
  real webapp/package.json, spaced variants, and missing-key fallback.

FIX 4 (lean packaging): webapp/{vita,wiiu,wii} are BUILD-TIME
  packaging assets only - LiveArea icon/bg0/startup (build-vita.sh) and
  the HBL icon (build-wiiu.sh, copied to app0:/icon.png separately) -
  and are NEVER loaded at runtime by the renderer (verified: zero
  references in sources/).  They are therefore excluded from the
  staged web/ content on BOTH the Vita VPK (minified copy) and the
  Wii U SD package (and so the WUHB) - no console ships the others'
  asset folders anymore.  build-vita.sh + build-wiiu.sh syntax-checked.

RESULT: all 11 gates green, golden zero-diff (the app0 double-slash
  was invisible to the desktop harness - POSIX fopen collapses "//",
  which is exactly why it only broke on console), committed.

## 2026-09-20 (27) - Wii U: fix tiny/blurry/off-center text (system-font seam unit mismatch)

USER REPORT + SCREENSHOT: after (26)'s font fix, text renders but is
  very small, blurry, and not centered in its box. Asked to try the
  coreinit system font.
  INVESTIGATED "coreinit system font": wut exposes NO font API from
  coreinit (checked the full include/coreinit/ header list - no font
  header of any kind) and the real Cafe OS system font (the
  FontRasterizer/nn_font library used by the Wii U Menu) is Nintendo-
  private; the only community access to it is binary-patching plugins
  like "System Font Replacer", not a stable SDK call. There is no
  legitimate way to draw the real system font from a homebrew RPX, so
  the bundled TTF stays the system-font stand-in - fixed below instead.
  ROOT CAUSE (unit mismatch, not a missing feature): the shared engine
  has TWO font seams with DIFFERENT size units:
    - cwv_font_* (@font-face files): caller premultiplies to literal
      PIXELS before calling ("unsigned sz = (unsigned)(n->font_size *
      32.0f)" - cwv_html.c measure_text_width, and cwv_backend.c's
      draw sites do the same "* 32 * z").
    - cwv_pgf_* (system font): on Vita this is vita2d_pgf's "scale", a
      UNITLESS multiplier of vita2d's fixed default glyph size, so the
      shared engine passes the RAW renderer-unit font_size (its own
      internal scale, always in ~0.1-8.0, see cwv_css.c
      css_prop_font_size / BODY_FONT_SIZE=0.75) straight through with
      no *32.
  The Wii U port's (26) fix routed BOTH seams through the same
  font_handle(size) expecting literal pixels - so cwv_pgf_draw_text
  got handed ~0.75-2.0 as a POINT SIZE, floored to the 4px minimum:
  a genuinely ~4px font, hence tiny and blurry once anti-aliased. The
  "off-center" look was a side effect of the same bug: the box
  centering math (line_height() etc.) is calibrated for a real
  ~24-28px line, so it visibly misplaces a 4px one.
  FIX (sources/wiiu/cwv_platform.c): added WIIU_PGF_PX_PER_UNIT=32.0f -
  the exact calibration documented in cwv_css.c's own comment ("the
  renderer measures fonts at 32 px per unit") - and multiply by it in
  cwv_pgf_text_width/cwv_pgf_draw_text before opening the TTF handle.
  The @font-face seam (cwv_font_text_width/cwv_font_draw_text) is
  untouched: its caller already sends pixels.
  RESULT: all 11 gates green, golden zero-diff, committed. Default
  body text now opens at 0.75*32=24px (matching Vita's visual scale);
  buttons/headings scale proportionally with their own font-size.

## 2026-09-20 (26) - Wii U: page text now renders (system-font seam); colored build output; accurate Cemu banner

USER REPORT: after the --emit-relocs fix the app boots in Cemu: <div>s
  render, links work, but NO text is drawn.  Vita unaffected.
  ROOT CAUSE: the shared engine draws all ordinary page text through
  the SYSTEM-FONT seam (r->system_pgf -> cwv_pgf_text_width /
  cwv_pgf_draw_text - cwv_backend.c:1422 etc.), while the Wii U port
  only implemented the @font-face seam (cwv_font_*) and STUBBED the
  pgf functions as "no default font yet" returning 0 - so every page
  text draw silently did nothing.  (Vita was unaffected because its
  cwv_pgf_* maps to the real vita2d system PGF font.)
  ADDITIONAL LATENT BUG FIXED: TTF_Init was never called anywhere, so
  even the webfont/boot-screen paths were dead on the console.
  FIXES (sources/wiiu/cwv_platform.c):
  - Both seams now share one SDL2_ttf implementation: handles are
    (file, size)-cached TTF_Fonts (16-entry ring, lazy open, dead-file
    detection), so text renders at the engine's requested pixel size
    (measure and draw round through the same clamp -> layout matches
    glyphs at any zoom).
  - cwv_pgf_load_default() loads the bundled assets/font.ttf ONCE as a
    singleton; cwv_pgf_free stays a no-op (app-lifetime), and
    cwv_font_free guards against freeing the singleton.
  - TTF_Init done lazily once in font_handle(); failure disables text
    (NULL/0), never crashes.
  - OSReport trace on system-font load ("CWV: default font loaded
    (path)") and on missing font, so the Cemu log.txt tells you
    immediately if the bundle lacks the TTF.
  BUILD SCRIPT (build-wiiu.sh), user request:
  - Colored output in build-vita.sh's style: helpers wiiu_err/warn/ok
    (red/yellow-bold/green ANSI, warn uses the exact 33;1 sequence of
    the Vita script) applied to all [wiiu] ERROR/NOTE/degraded-mode
    lines and success lines; the unified [NN%] progress lines stay
    plain so cross-platform output format is unchanged.
  - Removed the wrong reminder "(File -> Load executable; .wuhb/.elf
    are NOT loadable)": on real console .wuhb IS the Aroma/HBL
    loadable artifact and .rpx loads from the HBL SD layout, and Cemu
    accepts .wuhb/.rpx in File -> Load executable.  Banner now states:
    "Cemu: File -> Load executable: CWebView.wuhb or CWebView.rpx".
  - NOTE (not in scope, flagged only): the legacy Wii port's cwv_pgf_*
    seam is also stubbed (sources/wii/cwv_platform.c); when that port
    is picked back up it needs the same system-font fix.
  - RESULT: all 11 gates green, golden zero-diff, committed.

## 2026-09-20 (25) - Wii U: fix unlaunchable RPX (no relocations) + add RPX verification gate

USER REPORT: Cemu crashed on boot. Attached a crash dump, a full Cemu
  log with the PPC crash state, and the built CWebView.rpx.
  DIAGNOSIS (definitive, from the RPX + Cemu source):
  - Cemu's PPC dump showed IP=LR-slot=0xC0001250, i.e. execution jumped
    into the 0xC0000000 "loadmem" region (the .fimport_* import tables).
  - Parsing the RPX symtab: 0xC0001250 is the .fimport_coreinit slot of
    MEMGetAllocatableSizeForExpHeapEx (symbol #7893), called by wut's
    startup heap init - so the crash was the FIRST OS import call.
  - Disassembling the .text at the call site: the wut import thunk
    "lis r12,-0x4000; addi r12,0x1250; mtctr r12; bctr" still contained
    its LINK-TIME slot address, proving the thunks were never rewritten.
  - The RPX section table contains ZERO SHT_RELA (type 4) sections -
    there was no relocation data for any loader (Cemu or the console)
    to apply.
  - ROOT CAUSE: build-wiiu.sh linked with -T wut.ld but WITHOUT the
    canonical wut.specs flags; the missing piece is --emit-relocs,
    which is what makes ld keep relocation data in the final ELF.
    Without it every import thunk keeps its raw 0xC000xxxx address.
    This would have crashed on real hardware too - not a Cemu bug
    (secondary Cemu robustness bug: it AVs in getJumpTableBase instead
    of raising a graceful PPC fault, Cemu 2.6).
  FIXES:
  - build-wiiu.sh link step now mirrors wut.specs exactly:
    -T wut.ld --gc-sections --emit-relocs --no-eh-frame-hdr
    -z nocopyreloc (hand-rolled, not -specs, so the build does not
    depend on DEVKITPRO being exported at link time).
  - New post-elf2rpl verification: the RPX is parsed and the build
    ABORTS if it contains no SHT_RELA sections, so a reloc-less (and
    therefore unlaunchable) RPX can never ship again.
  - ALSO EXPLAINED: the log line "Section 7 uncompresses to 32 bytes
    but sectionSize is 44" is elf2rpl's benign .fexports quirk (Cemu
    self-corrects the size); not related to the crash.
  - RESULT: all 11 gates green, golden zero-diff, committed.

## 2026-09-20 (24) - fix build-wiiu.sh compile-check: wut headers missing from the path

USER REQUEST: "./build-wiiu.sh -y" aborted at the main.c compile-check
  with "fatal error: coreinit/debug.h: No such file or directory" after
  changelog 23 added the OSReport include.
  ROOT CAUSE: the early compile-check invocations of powerpc-eabi-gcc
  (cwv_platform.c and main.c) did not include the wut sysroot
  (/opt/devkitpro/wut/include) - only the FULL build further down had
  it (its own WUT_INC discovery).  main.c previously included no wut
  header directly, so the drift was invisible until now.
  FIX: the WUT_INC discovery (wut include + wiiu portlibs include) now
  runs once, right after the devkitPPC check, and is reused by BOTH
  compile checks AND the real build's CFLAGS - the duplicate discovery
  in the real-build section was removed so the paths can never drift
  apart again.
- RESULT: bash -n clean, all 11 gates green, golden zero-diff, committed.

## 2026-09-20 (23) - Second Cemu minidump decoded: identical, deterministic; OSReport boot trace added

USER REQUEST: fix the Cemu crash (new minidump + mlc01/sdcard listing
attached; sdcard layout confirmed correct: wiiu/apps/CWebView present).
  EVIDENCE: parsed the second Windows minidump - IDENTICAL signature to
  the first: 0xC0000005 at Cemu.exe+0x28b1d6, same fault offset from
  the Wii U memory base, i.e. the crash is 100% deterministic and the
  path fix from (22) did not move it (expected: the fault happens
  BEFORE any app file I/O).
  DEEP DIVE (disassembled the official Cemu 2.6 Windows binary around
  the faulting RVA and cross-read Cemu source):
  - The faulting instruction is a per-PPC-instruction table lookup
    used by Cemu's recompiler debug/dispatch layer
    (entry = *(base + 0x20000000 + (addr>>2)*8), gated by the
    "Recompiler initialized" flag and hCPU->skippedCycles).
  - The PPC address being looked up is 0xC0001250 - a STACK-REGION
    address, i.e. our app's PPC control flow reached the stack region
    as code during startup.  The table page for that region is not
    committed, so Cemu itself AVs instead of reporting the emulated
    PPC fault (a Cemu robustness bug, but triggered by our app).
  - Unresolved imports are NOT the mechanism: Cemu maps unknown
    imports to graceful HLE trampolines that log, not garbage jumps.
  - Confirmed from Cemu source: swkbd/erreula HLE modules exist in
    Cemu (imports resolve); .elf/.wuhb remain unloadable in Cemu, so
    .rpx stays the only Cemu test artifact.
  FIX (this round) - boot visibility instrumentation:
  - sources/wiiu/main.c now emits an OSReport startup trace
    ("CWV: main entered", "SDL_Init ok", "window ok", "renderer ok",
    "host created", "calling initial host_load", "page loaded /
    boot screen active") plus OSReport on every early-error return.
    Cemu mirrors OSReport into its log window/log.txt, so the NEXT
    crash log will show exactly how far boot got (or that it never
    reached main(), which would put the fault inside wut's own
    __init_wut - e.g. the FSA devoptab mount - and that distinction
    is the whole point).
  - Desktop gate: added desktop_test/stubs/coreinit/debug.h mirroring
    wut 1.9.1's OSReport so main.c still compiles cleanly.
  - NEXT DIAGNOSTIC STEPS handed to the user (documented in chat):
    run once with Cemu interpreter mode (General settings -> CPU) so
    the PPC fault surfaces as a readable Cemu crash report instead
    of a Cemu.exe AV, and attach CWebView.rpx for a section/import
    table audit (the "Section 7: 44 -> 32 bytes" loader line is
    analyzed but unverified against our actual RPX).
- RESULT: all 11 gates green, golden zero-diff, committed.

## 2026-09-20 (22) - Cemu crash root-caused and fixed (path story unified for console + emulator)

USER REQUEST: fix the cause(s) of Cemu crashing for all .rpx/.elf/.wuhb;
real Wii U is the priority, Cemu is the secondary testing target, both
must work completely and in the same manner.
  EVIDENCE: parsed the user's Windows minidump of the Cemu 2.6 crash
  (MDMP, exception stream): 0xC0000005 access violation INSIDE Cemu.exe
  (+0x28b1d6), reading Wii-U-memory-base + 0x2A0024A0 - i.e. Cemu's
  fast-path dereferenced a garbage PPC-derived pointer right after app
  startup.  Read Cemu's own RPL loader source (rpl.cpp) to understand
  the "Section 7 uncompresses to 32 bytes but sectionSize is 44" log
  line (benign: Cemu shrinks sectionSize to the decompressed size).
  Read wut's devoptab source (devoptab_fsa*.cpp): wut's ONLY device is
  "fs" (FSA-based, SD mounted at fs:/vol/external01).
  ROOT CAUSE (ours): cwv_platform.c mapped app0:/X to "rom:/X" - a
  device that does not exist anywhere.  fopen fell through newlib's
  default device ("fs") into FSA as the INVALID root-relative path
  "/web/index.html": the initial page could NEVER load on any target,
  and the malformed FSA path is what faulted inside Cemu's FS layer.
  FIX (same manner on both targets, no emulator-only code path):
  - app0:/X now resolves through REAL probed roots (stat, fallback
    fopen; probed once, cached, accessor cwv_wiiu_app_root()):
      1. fs:/vol/content                       (WUHB / Aroma)
      2. fs:/vol/external01/wiiu/apps/CWebView (SD / HBL / Cemu)
    Cemu emulates the console SD card (backed by <Cemu>/mlc01/sdcard/),
    so the SD layout works identically on hardware and in the emulator.
  - Boot diagnostics screen: on initial load failure the app draws the
    cause + expected roots on screen and retries every 2s - identical
    on console and Cemu, so missing-content setups are VISIBLE (in
    Cemu, stderr is invisible and a silent crash taught us nothing).
  - wiiu_runtime_log gives up on the file after the first failed write
    (no more per-line failed FS round-trips when no SD is attached).
  - Joystick: SDL_InitSubSystem(SDL_INIT_JOYSTICK) once + open the
    stick once (was: open/close EVERY FRAME - wrong on hardware too).
  - .elf and .wuhb in Cemu: NOT loadable formats (Cemu issue #962 for
    wuhb; the .elf is elf2rpl's input).  Banner + README-WIIU.md now
    say exactly that: Cemu always loads the .rpx, console gets both.
- Docs: README-WIIU.md gained "File paths: one story for console AND
  Cemu" + a step-by-step Cemu testing section (mlc01/sdcard layout,
  log location).  Build banner updated likewise.
- RESULT: all 11 gates green (desktop stubs extended with
  SDL_INIT_JOYSTICK/SDL_InitSubSystem for the new init), golden
  zero-diff, committed.

## 2026-09-20 (21) - WUHB for real (verified wuhbtool CLI), Cemu answer, clean.sh

USER REQUESTS: (a) make the Wii U build support Aroma/WUHB "for actual
building", and would this support Cemu?  (b) continue organization with
a build-folder clean script.
  (a) The previous WUHB attempt (from session 16) was GUESSWORK that
  could never have worked: it passed the CONTENT DIRECTORY as input and
  invented flags (--long-name, --drc-only).  Verified the real CLI
  against a working wuhbtool frontend's source (germanvanadium/wuhbgui,
  src/WUHB/WUHBCreator.java) plus WUHB-Packager: positional
  "wuhbtool <input.rpx> <output.wuhb>" with --short-name, --name (long
  name), --author, --icon, --drc-image/--tv-image (splash, skipped) and
  --content (content partition).
  Rewritten block:
  - input is the RPX; the staged app dir goes to --content so ONE
    .wuhb carries web/, assets/, meta.xml, icon - the Aroma-native
    packaging philosophy (reuse the console UX, don't recreate it).
  - wuhbtool discovery: PATH, /opt/devkitpro/tools/bin, $DEVKITPPC/bin,
    /opt/devkitpro/wut/bin, then a pacman -Ss wuhbtool-guided package
    install (the dkp package name varies across repo generations:
    wut-tools / wiiu-tools - so the script asks the user's own repo
    instead of guessing), then WUHBTOOL= env override.  Any failure is
    non-fatal: the RPX path is unaffected.
  - progressive output: "[ 98%] Packing WUHB (Aroma bundle: rpx +
    content partition)".
  - CEMU ANSWER: yes for the RPX (File -> Load executable; that stays
    the PC-testing artifact), NO for WUHB - .wuhb is a console-side
    Aroma container, not an emulator format.  Banner + README-WIIU
    document both, including sd:/wiiu/apps/ and the Homebrew on Menu
    plugin for native-menu launching.
  - Harness-verified both paths: fake wuhbtool asserts the exact
    positional + flag invocation; not-found path exercises the
    pacman-guided install attempt and the graceful skip.
  (b) ORGANIZATION: new clean.sh at the repo root - removes build/
    (vita CMake tree, wiiu ELF/RPX/WUHB/staging, ESM extension builds),
    stray root VPKs, and the wii/wiiu compile-check scratch objects in
    /tmp; supports -y, -n/--dry-run (size report), -h; interactive
    confirm otherwise; NEVER touches sources/webapp/assets/git.  The
    root Makefile's clean target now delegates to it (all platforms,
    not just the vita tree).  Tested dry-run + real run.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (20) - -lwutcrt does not exist (libwut.a already contains wutcrt); formal all-optional manifest schema

USER REPORT: link died with
"cannot find -lwutcrt: No such file or directory".
  My (18) mistake: I read wut's source tree (libraries/wutcrt is a
  separate CMake target) and assumed a matching libwutcrt.a.  wut's
  actual PACKAGE Makefile (repo root Makefile) builds ONE libwut.a from
  ALL sources INCLUDING libraries/wutcrt (crt0_rpx.s is in the SOURCES
  list), and installs only include/lib/share - so the pacman package
  ships libwut.a + libwutd.a, no libwutcrt.a.
  Fix: dropped -lwutcrt; -Wl,-u,__rpx_start stays and force-extracts
  crt0_rpx.o (the __rpx_start entry that calls __init_wut/main/exit)
  from -lwut itself.  Comment in the script now documents this so it
  does not regress.
  Verified with the fake-devkitPro harness (libwutcrt.a removed): link
  args contain -u,__rpx_start + wut.ld, correct order, no -lwutcrt.
USER-DIRECTED (2): formal, all-optional ("non-req") manifest schema.
  - webapp/cwebview.schema.json: JSON Schema draft 2020-12 documenting
    every recognized root field (name/shorthand/version/author/target/
    webpage/webappDirectory/description/icon) and the vita/wiiu/wii
    section overrides - zero required fields, defaults documented per
    property, aliases noted.
  - webapp/package.json now points at it via
    "$schema": "./cwebview.schema.json" (local file works offline and
    in VS Code validation; swapping to a hosted private-GitHub raw URL
    later means editing only that one value).
  - Verified: schema is valid JSON, package.json conforms key-by-key,
    and the build scripts keep their tolerant parsing (schema is for
    editors/docs; builds do not enforce it).  CONFIGURATION.md
    documents the schema and the hosting option.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (19) - freetype lives in portlibs/ppc not portlibs/wiiu; manifest = the user's canonical format; "target" documented

USER REPORT 1: build died at
"[wiiu] ERROR: required portlib missing: /opt/devkitpro/portlibs/wiiu/lib/libfreetype.a".
  Root cause: my (18) assumption was wrong.  devkitPro ships the wiiu
  SDL2 ports' dependencies as SHARED ppc portlibs - verified against
  the wut-packages PKGBUILDs: wiiu-sdl2_ttf depends on ppc-freetype +
  ppc-harfbuzz, wiiu-sdl2_image on ppc-libjpeg-turbo/ppc-libpng/
  ppc-libwebp, and those install to /opt/devkitpro/portlibs/ppc (the
  Wii + Wii U shared prefix), NOT portlibs/wiiu.
  Fix: the link block now searches BOTH prefixes for every lib
  (wiiu_portlib_dir helper), passes both -L paths, and hard-checks
  only the link-proven set (freetype harfbuzz webp) across both.  The
  optional dependency chain follows the PKGBUILD deps: harfbuzz ->
  freetype -> webp/sharpyuv/jpeg/png/brotlidec/brotlicommon/
  brotlienc/bz2/z (freetype is built --with-brotli/bzip2/png/zlib per
  its PKGBUILD), each linked only when installed, in correct static
  order (dependents first).  Error message now names both search paths
  and the pacman command (the libs arrive as deps of wiiu-sdl2_ttf /
  wiiu-sdl2_image).
  Verified with the fake-devkitPro harness in the ppc-only layout
  (nothing in portlibs/wiiu): required libs found, full order
  asserted, tiff skipped (image port builds --disable-tif), and a
  truly-missing freetype still produces the clear error.
USER-DIRECTED FORMAT (2): webapp/package.json now uses the canonical
manifest format - shared facts at the ROOT (name "CWebView Example",
shorthand "CWV", version, author, target "VitaWebview", webpage,
webappDirectory "."), console sections overriding per console (vita:
titleId CWEBVIEWX / icon / wallpaper / startLogo / storageDirectory;
wiiu: shorthand / webpage / icon / description; wii: icon).  Both
build scripts already parsed section-over-root with root fallback, so
the format needed no parser change on the vita side; the wiiu preview
gained a Target line via a new json_get_root() (root-only lookup, so
an unrelated console section can never hijack a root key).
  "target" ANSWER (user asked "what does this do?"): it names the
  native app target.  build-vita.sh uses it as the CMake target (VPK =
  VitaWebview.vpk, engine gets -DVITA_TARGET); on Wii U it is
  informational only (the RPX is always CWebView.rpx).  Documented in
  CONFIGURATION.md, including that JSON has no comment syntax (the
  // example comment stays out of the real file).
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (18) - REAL BUILD FIX 2: the wut link line (extern "C" on cwv_ime_draw; wut.ld + wutcrt + lib order)

USER REPORT: after (17) the link step ran and failed with
"undefined reference to cwv_ime_draw" (main.c:166) plus a wall of
GX2*/AX*/ProcUI*/WPAD*/KPAD*/KBD*/GFD*/DMAE* from libSDL2, FT_*/hb_*
from libSDL2_ttf, WebP* from libSDL2_image, and "cannot find entry
symbol _start".
  THREE separate bugs, all in MY wiiu glue:
  1) cwv_ime_swkbd.h had NO extern "C" guard.  The .cpp includes it
     first, so cwv_ime_draw() (declared ONLY there) was compiled
     C++-mangled while main.c (C) references the plain symbol.
     cwv_ime_open/poll only linked by luck: they are also declared in
     cwv_platform.h, which has the guard.  Added #ifdef __cplusplus
     extern "C" to the header; verified with nm that all three seam
     functions now export unmangled (T cwv_ime_draw/open/poll).
  2) The link line was NOT a wut RPX link:
       - missing -Wl,-T,$DEVKITPRO/wut/share/wut.ld: wut.ld is what
         places code/data in the RPX memory map and collects the
         .fimport_* OS-import sections (confirmed against wut's
         share/wut.ld: ENTRY(__rpx_start), KEEP(*(.crt0)), the
         fimport_* sections).  Without it the ELF gets the default
         layout and elf2rpl emits an unlaunchable RPX.
       - missing wutcrt: wut's entry (__rpx_start, crt0_rpx.s) lives in
         the SEPARATE wutcrt library, and ENTRY() alone does not pull an
         archive member - added -Wl,-u,__rpx_start + -lwutcrt before
         -lwut (crt0_rpx calls __init_wut/main/exit which -lwut
         resolves).  This is also why ld warned "cannot find entry
         symbol _start".
       - STATIC ORDER: -lwut was FIRST on the line, so no libwut member
         was ever pulled and every SDL2 reference to wut's OS import
         stubs (GX2*, AX*, ...) came out undefined.  Correct order:
         objs, SDL2_ttf, SDL2_image, SDL2, freetype, harfbuzz, webp
         (all REQUIRED - the portlibs SDL2 ports are built with them;
         the script hard-errors with the package name if one is
         missing), optional sharpyuv/jpeg/png/tiff/bz2/z linked only
         when installed, then wutcrt, wut, m.
  3) VERIFIED: fake-devkitPro harness (stub g++ that captures the link
     args) asserts wut.ld -T, -u,__rpx_start, the full -l ordering,
     optional-lib gating (sharpyuv in, jpeg out), and the 17-step loop
     still produces link -> elf2rpl -> staging.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (17) - REAL BUILD FIX: the Wii U link step never ran (elf2rpl "readElf failed")

USER REPORT: ./build-wiiu.sh -y compiled everything, then died at
"Generating CWebView.rpx (elf2rpl)" with
"Could not open .../build/wiiu/CWebView.elf for reading / ERROR: readElf
failed."
  Root cause: MY bug in the (13) unified-percentage loop.  The unit
  kinds are parsed with kind="${unit%% *}" (FIRST WORD ONLY), so the
  unit "LINK ELF" parsed as kind="LINK" - and the dispatcher's
  elif [[ "$kind" == "LINK ELF" ]] could never match.  The link step
  fell into the catch-all else branch (printing the staging label), so
  CWebView.elf was NEVER linked and elf2rpl had nothing to read.  The
  user's log shows it exactly: [ 76%] last object -> [ 82%] "Staging
  SD package" where "[ 82%] Linking CXX executable CWebView.elf"
  belonged.
  Fix: unit renamed to single-word "LINK" (comment added that kinds are
  first-word-only) and the dispatcher compares kind=="LINK".  "RPX"
  and "PACKAGE" were already single-word and unaffected.
  Why the gates missed it: build_wiiu_gate.sh is a syntax/compile-check
  gate; no gate executes the loop without devkitPPC.  Added a BEHAVIORAL
  test this time instead: the build-loop section was run against a FAKE
  devkitPPC (stub powerpc-eabi-gcc/g++ that create the -o target, stub
  elf2rpl) - with the full real unit list (17 steps, [ 0%]..[ 94%]),
  the loop now emits [ 82%] Linking CXX executable CWebView.elf, the
  fake elf lands in build/wiiu/CWebView.elf, elf2rpl copies it to the
  .rpx, and staging is the final step.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (16) - per-console images live WITH the webapp; manifest documents vita/wiiu/wii; webappDirectory "." fixed

USER-DIRECTED LAYOUT CHANGE (16): each console's packaging images
(icon / wallpaper / start logo) now belong in the WEBAPP folder, one
subfolder per console - NOT in the project's assets/ directory:

  webapp/vita/{icon,wallpaper,start-logo}.png
  webapp/wiiu/icon.png
  webapp/wii/icon.png

- FILES MOVED (git mv): assets/default_icon.png -> webapp/vita/icon.png,
  default_wallpaper.png -> webapp/vita/wallpaper.png,
  default_start_logo.png -> webapp/vita/start-logo.png, and
  assets/icon.png -> webapp/wiiu/icon.png.  assets/ keeps only the
  shared engine assets (font.ttf, FONT_LICENSE.txt, template.xml).
  webapp/wii/icon.png added as the Wii placeholder.
- build-vita.sh: defaults come from the webapp folder first (new
  console_image_default() helper: WEBAPP_DIR, webapp/, web/), with the
  old assets/ paths kept as fallbacks so unmoved trees keep building.
  Manifest icon/wallpaper/startLogo values still resolve via
  resolve_asset_path() - which was already manifest-relative, so
  "vita/icon.png" lands in webapp/vita/icon.png naturally.
- build-wiiu.sh: WIIU_ICON defaults to the webapp-relative
  "wiiu/icon.png"; the staging step resolves manifest-relative values
  against the manifest's directory (same semantics as the Vita script),
  absolute paths pass through, and assets/icon.png remains the
  last-resort fallback.  The webapp resolution block moved BEFORE the
  staging section so the icon resolution knows $WEBAPP.
- webapp/package.json now documents all three console sections side by
  side as examples: "vita" (icon/wallpaper/startLogo/storageDirectory),
  "wiiu" (shorthand/webpage/icon/description), "wii" (icon, for the
  future packaging - build-wii.sh does not consume it yet, noted in
  README-WII.md).
- REAL BUG FIXED ALONG THE WAY: "webappDirectory": "." in the manifest
  was treated as CWD-RELATIVE, resolving the webapp dir to the project
  root ("ERROR: . has no index.html").  It is MANIFEST-RELATIVE like
  every other manifest path ("." = the manifest's own folder).  Fixed
  in build-wiiu.sh (both the Y-apply and the selective-edit paths) and
  the same in build-vita.sh.
- VERIFIED ON DESKTOP: wiiu harness shows the wiiu-section preview
  (Name/Shorthand/Version/Description/Icon), meta.xml generated from
  it, icon staged from webapp/wiiu/icon.png, webapp staged correctly;
  vita harness shows all three defaults resolving into webapp/vita/.
  CONFIGURATION.md documents the convention and the three-section
  manifest example.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (15) - webpage define crash fixed; "web/" is a first-class webapp folder name

REAL BUILD FIX (user report): ./build-wiiu.sh died at line 316 with
"-DCWV_WIIU_WEBPAGE=\"web/index.html\": No such file or directory".
  Root cause: MY bug in (14) - the -DCWV_WIIU_WEBPAGE token was
  appended OUTSIDE the quoted CFLAGS="..." assignment, so the line
  parsed as a COMMAND ("run -DCWV_WIIU_WEBPAGE=..." with CFLAGS as env
  prefix) instead of an assignment.  bash -n cannot catch this (it is
  valid command syntax); it only fires at runtime.  The define now
  lives INSIDE the quoted assignment as
  -DCWV_WIIU_WEBPAGE=\"$WIIU_APP0_WEBPAGE\" and the token was
  verified to survive the loop's unquoted $CFLAGS expansion as ONE
  gcc argument: -DCWV_WIIU_WEBPAGE="web/index.html".
- WEBAPP FOLDER NAMED "web/" NOW SUPPORTED (user renamed webapp -> web
  as a workaround): "web" is recognized everywhere "webapp" is - as a
  WEBAPP directory candidate (webapp/ still wins if both exist, for
  back-compat), as a manifest candidate ($ROOT/web/package.json), and
  as a default-font candidate ($ROOT/web/assets/font.ttf).  The
  no-webapp note now says "no webapp/ or web/ folder".  build-vita.sh
  got the same one-line fallback (webapp/ missing + web/ present ->
  use web/) so a renamed tree keeps building for both consoles.
- VERIFIED ON DESKTOP with a web/-renamed tree: manifest found and
  previewed from web/package.json, values applied, WEBAPP resolved to
  web/, app0 page = web/<page>, staging correct; CFLAGS token shape
  checked against a fake compiler.
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (14) - fetch unit compiles against the REAL wut sockets; webapp manifest shown + editable like the Vita build

REAL BUILD FIX (user report): ./build-wiiu.sh died compiling
cwv_fetch_nets2.cpp - SO_RCVTIMEO / SO_SNDTIMEO were not declared
(4 errors).  Root cause: same class of bug as the swkbd guess (13),
on the network side - those two constants DO NOT EXIST in wut's
nsysnet socket surface (verified against wut 1.9.1's
include/sys/socket.h, which lists the SO_* options it implements;
send/recv timeouts are simply not among them).  Desktop glibc defines
them, so the wiiu gate stayed green while the real SDK build failed.

- TIMEOUTS REWRITTEN THE NATIVE WAY: a sock_wait() helper bounds every
  blocking send()/recv() with select() (wut's sys/select.h, the
  standard console pattern) and a 15s cap.  No invented option numbers:
  a setsockopt constant the stack does not implement could fail
  silently or hang the seam.  connect() and gethostbyname() keep their
  previous blocking behavior (unchanged risk profile).  A send timeout
  still fails the fetch (-1); a recv timeout keeps what arrived and
  finishes like the old timeout path did.
- WEBAPP MANIFEST SUPPORT (user request, build-vita.sh UX mirrored):
  build-wiiu.sh now recognizes the webapp manifest (package.json;
  keys under "wiiu", with top-level name/version/author and common
  aliases, so ONE shared manifest serves both consoles).  It SHOWS a
  "Preview of recognized Wii U settings" block, then applies the values
  wholesale (Y / -y / non-interactive), supports selective editing with
  prompts ("n -name -version ..."), and /s writes ONLY the selected
  fields back into the manifest's "wiiu" object - the exact grammar
  build-vita.sh established.  Recognized keys: name, shorthand,
  version, author, description, icon, webpage, webappDirectory.
- THE VALUES ARE ACTUALLY CONSUMED (not just previewed): meta.xml is
  now GENERATED from the effective metadata at package time (XML-
  escaped via xml.sax.saxutils.escape, auto release_date; python3-less
  environments fall back to the static sources/wiiu/meta.xml template,
  and the defaults equal that template so a manifest-less build is
  byte-identical in intent); WUHB gets --short-name/--long-name/--author
  from the manifest instead of the hardcoded CWebView values; the
  webpage entry point feeds -DCWV_WIIU_WEBPAGE into BOTH main.c
  compiles (compile-check and full build) and drives the staging
  validation ("$WEBAPP/$WIIU_WEBPAGE" must exist - no more hardcoded
  index.html check); an explicit --webapp flag still beats the
  manifest's webappDirectory.
- VERIFIED END-TO-END ON DESKTOP (harness on the script's metadata
  section): default application from a wiiu section + top-level
  fallbacks; selective edit + /s save-back touching only the selected
  keys; meta.xml generation with < > & escaped.  Plus the stale
  build-wiiu.sh header comment fixed (it still claimed "the app loop
  is NOT ported yet" - the script builds the full app).
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (13) - Wii U IME: the REAL nn::swkbd (wut 1.9.1), unified build progress, native-applets-first

REAL BUILD FIX (user report): ./build-wiiu.sh died compiling
cwv_ime_swkbd.cpp - nine errors.  Root cause: the file was written
against a GUESSED API (ShowKeyboard, ControllerArg,
GetDefaultControllerArg, GetRequiredStringBufferSize,
KeyboardMode::Password) that DOES NOT EXIST in wut 1.9.1.  The real
nn::swkbd is an IN-APP keyboard LIBRARY the app pumps per frame, not a
blocking dialog - I verified every symbol against the v1.9.1 tagged
headers (the user's compiler errors match them line for line).

- cwv_ime_swkbd.cpp REWRITTEN against the real API:
  Create(workMemory + FSClient) -> AppearInputForm -> per frame
  VPADRead + Calc + CalcSubThreadFont/Predict (only when asked) ->
  IsDecideOkButton / IsDecideCancelButton -> GetInputFormString ->
  DisappearInputForm -> Destroy.  Work memory stays aligned
  (aligned_alloc(0x1000) - user-contributed note: unaligned work
  memory crashes the library).
- Seam contract UNCHANGED (zero engine edits): cwv_ime_open only
  BRINGS UP the keyboard (fast, non-blocking); the engine's per-frame
  cwv_ime_poll pumps Calc and returns 1 = Enter / 2 = cancel.  New
  sources/wiiu/cwv_ime_swkbd.h + cwv_ime_draw(): main.c draws the
  keyboard LAST in the frame (after the page, before
  SDL_RenderPresent) - wut's documented placement, same pattern as the
  wut erreula sample.
- WIRING FIXES the old TODO never did: initial text (inputFormArg.
  initialText), password (PasswordMode::Hide - there is no
  KeyboardMode::Password), single-line (ConfigArg.disableNewLine),
  maxTextLength = buffer_len - 1.  HARDWARE CAVEAT documented in the
  file: SDL2 owns the GX2 swap; the swkbd/SDL2 GX2 context mixing is
  the first thing to verify on hardware.
- DESKTOP STUBS NOW MIRROR THE REAL wut 1.9.1 API, so a guessed API
  fails at the gate instead of mid-devkitPPC-build: swkbd_cpp.h
  rewritten; NEW stubs/vpad/input.h + stubs/coreinit/filesystem.h
  (FSAddClient/FS_STATUS_OK per the real coreinit/filesystem.h).
  build_wiiu_gate.sh gains -I../sources/wiiu (main.c includes the new
  local header).
- NATIVE-APPLETS-FIRST AUDIT (user direction): swept the Wii U port
  for hand-rolled replicas of system-provided things.  Outcome: IME is
  now native (the old file said it "replaces the earlier hand-drawn
  keyboard"); the remaining candidates are documented as policy in
  README-WIIU.md - error dialogs in the future CWMenu shell = nn::erreula
  (NOT a hand-drawn dialog), open_browser stays -1 (SYSLaunchTitle
  passes no URL and exits the app - no real hand-off exists).  STALE
  STATUS TEXT fixed everywhere it lied: README-WIIU.md's "not yet done"
  list (font/IME/app loop/fetch/packaging were all already done) and
  cwv_platform.c's header comment ("no native IME in wut").  The real
  remaining work: the CWMenu shell port + the swkbd hardware check.
- BUILD OUTPUT UNIFIED (user direction): build-wiiu.sh now prints the
  same progressive style as build-vita.sh (whose percentages come from
  CMake): "[ NN%] Building C object: ..." for every unit, Linking,
  RPX, Staging, ending "[100%] Built target CWebView"; build-vita.sh
  ends on the matching "[100%] Built target".  Fixed two bugs of my
  own from that rework before they shipped (elf2rpl discovery moved
  before its first use; a duplicate staging echo) and the stale final
  banner (now names the real gap: the CWMenu shell, not "main.c is
  still Vita-only").
- RESULT: all 11 gates green, golden zero-diff 5419.

## 2026-09-20 (12) - shared is Vita-free: suprx ESM binding moved to the Vita port + standalone gate

REAL BUILD FIX (user report): ./build-wiiu.sh died in the shared engine
compile - cwv_host.c:4 fatal error: cwv_esm_api.h: No such file or
directory.  Root cause: cwv_host.c moved to shared (entry 5) but kept
the Vita-only CwvESM.suprx wiring; the Vita CMake silently supplies
sources/vita/esm_module/src in its include path, so only a build that
compiles shared/ WITHOUT it (build-wiiu.sh, the newest port) noticed.
User direction: separate the Vita-specific stuff from the shared
sources, moving it to the Vita source; splitting/merging allowed, no
one-line file copies.

- NEW sources/vita/cwv_host_esm.c + .h: the Vita ES6 PROVIDER.  All
  that is Vita-only about ES6 lives here now: filling CwvESMHostApi
  (cwv_esm_api.h, the suprx's host API v2), calling cwv_esm_initialize,
  and registering the module's cwv_esm_transform / cwv_esm_prelude.
  main.c gains `#include "cwv_host_esm.h"` next to its existing direct
  cwv_esm_api.h include (it polls cwv_esm_enabled itself).
- SHARED gets the console-neutral half (same behavior, honest names):
  - `cwv_host_es6_services(host, &services)`: the three host services
    every provider needs (evaluate / resolve_url / load_file), host
    already bound in; the callbacks were already in cwv_host.c, they
    just get a struct + a getter instead of being Vita-private.
  - `cwv_host_install_es6(...)`: the ONE registration path for any
    provider (transform + prelude -> backend).
  - `cwv_host_enable_esm_fallback` RENAMED to `cwv_host_enable_es6_builtin`:
    "fallback" was Vita-POV naming - the compiled-in transformer
    (cwv_es6_core.c) is the PRIMARY provider on Wii U / Wii and in the
    desktop tests, and only Vita's SECOND choice.  Updated all three
    main.c callers + the es6_test.c comment.
  - cwv_host.h: cwv_host_enable_esm declaration REMOVED (it lives in
    sources/vita/cwv_host_esm.h now); the ES6 hook signatures are
    spelled out so the shared header does not need cwv_backend.h.
- CMakeLists.txt: sources/vita/cwv_host_esm.c added to the app target.
- NEW 11TH GATE build_shared_gate.sh: EVERY sources/shared/*.c must
  compile with NOTHING but sources/shared on the include path.  This is
  the guard that would have caught today's bug - the main gate still
  compiles cwv_host.c with the Vita module include path, so shared
  rot was invisible to it.  build_main_gate.sh now syntax-checks
  cwv_host_esm.c too (it only runs on the Vita, exactly what that gate
  exists for).
- DESKTOP STUB DEBT (pre-existing in the archive, broke js/es6/loghook/
  golden/wiiu gates): the engine seam grew cwv_console_fetch_bin +
  cwv_console_cache_dir (entries 6-8) but the desktop test doubles never
  caught up - four gates could not even LINK.  Fixed in the stubs, not
  the engine:
  - cwv_platform_stub.c: cwv_console_fetch_bin = the simulated network
    (same tiny red 8x8 PNG the old curl stub "downloads", now shared
    via stub_fake_net_png() in stub_impl.c so old-vs-new comparisons
    stay honest); cwv_console_cache_dir = -1 (no cache location on
    desktop; no test page uses remote @font-face, golden confirmed).
  - stubs/SDL2/SDL.h: SDL_Rect typedef (the Wii U platform layer draws
    texture parts with integer rects; the stub only had SDL_FRect).
- RESULT: golden zero-diff 5419 again (the fetch_bin gap had made the
  remote-image node silently change size); ALL ELEVEN gates green.
- Legacy naming noted, deliberately NOT swept in this change (functional
  rebrand only): VitaMenuTemplate / vita_menu_* in cwmenu_api +
  native_menu_templates are pre-rebrand NAMES for shared CWMenu shell
  data (the Wii U shell port will consume them unchanged).

## 2026-09-19 - Image load diagnostics + image-rendering CSS + console color check

## 2026-09-20 (11) - Wii U fetch via nn::ac + nets2 sockets

User direction: keep porting the Wii U side using DevkitPro's native
libraries - nn network (nets2) for fetching, not a portlib.

- NEW sources/wiiu/cwv_fetch_nets2.cpp (2nd/last C++ unit): implements
  the seam's cwv_console_fetch with the native stack:
  - nn::ac::Initialize/Connect, lazily once (auto-connect; HBL is
    usually already online, socket path tried even on AC error)
  - wut's BSD socket surface (sys/socket.h) = nsysnet/nets2 backed:
    gethostbyname, socket, connect, send, recv, close
  - raw HTTP/1.1 GET (Connection: close), 15s send/recv timeouts,
    256 KiB body cap (same shape as the Vita's sceHttp path),
    status-line parse, CRLFCRLF header split, status code returned
  - https:// REFUSED with -1: nets2 has no TLS (documented in-file;
    the engine surfaces it to the page like any fetch failure)
- Removed the old cwv_console_fetch stub from cwv_platform.c.
- WIIU GATE: g++ -fsyntax-only's the fetch unit too (new
  stubs/nn/ac.h; POSIX socket headers compile against the desktop's
  real ones); build-wiiu.sh compiles it with g++ like the IME unit.

All TEN gates green; golden zero-diff 5419.  Wii U pending: CWMenu
shell port, menus/DevTools.
## 2026-09-20 (10) - Wii U: native nn:swkbd keyboard (hand-drawn one retired)

User direction: (1) confirmed <input type="text"> IS the HTML API -
the earlier <textfield> replacement stands; is_textfield /
renderer_textfield_* are only internal C identifiers.  (2) Asked why
not use wut's nn:swkbd native keyboard - right call, so the hand-drawn
keyboard is REPLACED by the real system applet.

- NEW sources/wiiu/cwv_ime_swkbd.cpp (only C++ unit in the app):
  implements the C seam cwv_ime_open/cwv_ime_poll over nn::swkbd per
  the user's impl guide - aligned_alloc(0x1000) work buffer (unaligned
  crashes the applet), CreateArg.maxStringLength = buffer_len-1,
  GetDefaultControllerArg, KeyboardMode::Password for password fields,
  hint text from the seam title, ShowKeyboard (blocks while the
  applet owns the DRC - app loop pause is the expected model), result
  staged wchar_t->UTF-16, Destroy + free.  Seam contract unchanged:
  poll reports Enter(1)/cancel(2) on the next frame, ZERO engine
  edits.  Known TODO: initial text pre-fill (needs the real header's
  initialString field on hardware).
- Removed: the hand-drawn SDL keyboard (kb block in cwv_platform.c),
  main.c's ime_active/ime_draw glue.
- cwv_platform.h: extern "C" guards (C header now includable from
  the C++ unit).
- WIIU GATE: g++ -fsyntax-only on the .cpp too (new
  stubs/nn/swkbd/swkbd_cpp.h mirrors the real header's field paths);
  build-wiiu.sh compiles it with powerpc-eabi-g++ -fno-exceptions
  -fno-rtti and links with g++ (C++ runtime).

All TEN gates green; golden zero-diff 5419.  Wii U pending: fetch,
CWMenu shell, menus/DevTools.
## 2026-09-20 (9) - Wii U software keyboard

User direction: continue porting; asked where webapp files live
(answer: inside the project folder next to the build scripts -
build-vita.sh defaults to ./webapp or the package.json
webappDirectory key; build-wiiu.sh takes --webapp DIR, default
./webapp, else test_pages/).

- SOFTWARE KEYBOARD (sources/wiiu/cwv_platform.c): no native IME in
  wut, so the keyboard is drawn by the port with the same seam calls
  the engine uses (SDL rects + the bundled DejaVu font).  The SHARED
  backend drives it as-is: renderer_textfield_open -> cwv_ime_open on
  textfield activation, renderer_textfield_update -> cwv_ime_poll
  every frame - textfields now work on Wii U with NO engine changes.
  Grid: qwerty/asdf/zxcv/digits/punct rows + Space/Bksp/Enter/Cancel
  row; hat moves, A types, B cancels; password fields show dots.
- main.c: while cwv_wiiu_ime_active() the keyboard owns SDL events
  (page input skipped); cwv_wiiu_ime_draw() paints after the page.
- WIIU GATE HARDENED: now syntax-checks platform.c too (not just
  main.c) - it immediately flushed out a pile of missing SDL surface
  in the desktop stub (SDL_image/SDL_ttf headers, RWops, RenderGeometry
  vertex fans, RenderFillRectF/RenderCopyF, FRect/PointF) and the
  stale never-opens IME stub that would have collided with the real
  keyboard.  All resolved; stub now mirrors the real SDL surface.

All TEN gates green; golden zero-diff 5419.  Wii U pending: fetch
backend, CWMenu shell port, menus/DevTools.
## 2026-09-20 (8) - Wii U: installable app (full build) + runtime log

User direction: make the Wii U port an installable/runnable app while
continuing the real port - not a bare-minimum scaffold.

- BUNDLED FONT: assets/font.ttf = DejaVu Sans (libre, redistribution
  permitted; license note in assets/FONT_LICENSE.txt) - the engine can
  actually render text on a console with no system font.  The font is
  a build REQUIREMENT now (build-wiiu.sh errors if absent).
- PACKAGE ASSETS: sources/wiiu/meta.xml (HBL metadata) + generated
  assets/icon.png (128x128).
- RUNTIME LOG seam: new cwv_file_append in cwv_platform.h (Vita
  sceIoOpen APPEND / Wii U + Wii stdio "ab" / stub no-op).  wiiu
  main.c registers cwv_host_set_runtime_log -> writes
  sd:/wiiu/apps/CWebView/CWebView.log + stderr, boot lines included
  - the same debugging story as the Vita's ux0:data/VWWV.log (engine
  failures incl. IMG: lines land there even on script-free pages).
- FULL BUILD in build-wiiu.sh: compiles shared/*.c + duktape + wiiu
  platform + app loop, links -lwut -lSDL2 -lSDL2_image -lSDL2_ttf,
  elf2rpl -> CWebView.rpx, stages build/wiiu/content/ =
  sd:/wiiu/apps/CWebView package (rpx/meta/icon/web/assets), optional
  wuhbtool -> CWebView.wuhb for Aroma; --webapp DIR selects content
  (default ./webapp, else test_pages).  Prints SD install path.
- Still pending (platform features for later): CWMenu shell port,
  software keyboard, fetch backend, Wii U menu/DevTools.

All TEN gates green; golden zero-diff 5419.
## 2026-09-20 (7) - CWMenu rebrand + Wii app loop + env-install hardening

- REBRAND (user direction): VWM -> CWMenu, script extension .vwm ->
  .cwm, everywhere: vwm_menu_api.c/.h -> cwmenu_api.c/.h,
  sources/shared/vwm/default.vwm -> sources/shared/sources/menu/scripts/default.cwm,
  packaged path vwm/ -> cwmenu/, main.c loads
  app0:/cwmenu/default.cwm, functions vwm_menu_* -> cwmenu_*,
  macros VWM_* -> CWM_*, docs/VWM_SCRIPT.md -> docs/CWM_SCRIPT.md,
  vwm_unit.c/build_vwm_unit.sh -> cwmenu_unit.c/build_cwmenu_unit.sh.
  The rebrand caught a REAL BUG the text sweep alone would have
  missed: the script-version header guard matched 'V''W''M' char by
  char and silently reported version 1 for every script - replaced
  with a strncmp("CWMenu",6) prefix check (cwmenu_unit pins it).
- ENV INSTALL (user direction): build-wiiu.sh + build-wii.sh now
  ensure their own host tools (curl/git via apt-get, prompted, -y
  auto-yes) before the devkitPro pacman installer - so a fresh
  machine can bootstrap EITHER console end to end from one script.
  build-vita.sh already had full host+SDK+ports bootstrap.
- WII APP LOOP: sources/wii/main.c - the shared host on the Wii's
  640x480 screen (PAD_Init + WPAD_Init + GRRLIB), built-in ESM,
  A=activate, Up/Down=focus cycle, HOME=quit.  build_wii_gate.sh now
  syntax-checks platform.c AND main.c (stubs/wii/ogc/{pad,wpad}.h);
  build-wii.sh powerpc-eabi compile-checks both files.  README-WII
  updated: app loop done, IR pointer/menus/keyboard/fetch/packaging
  remain.

All TEN gates green (cwmenu_unit now 10 checks incl. version pin);
golden zero-diff 5419.
## 2026-09-20 (6) - Wii port started + Wii U default font

User direction: continue porting; start the Wii too (user note: Wii
uses .rel relocatable modules for load-on-demand code - the suprx
analog).

WII (sources/wii/, started):
- cwv_platform.c on libogc + GRRLIB: app0:/ -> sd:/apps/cwebview/
  mapping (override -DCWV_WII_APP_ROOT), PNG/JPEG/BMP textures
  (GRRLIB sniffs format), rects/circles/clear, ABGR->RGBA color
  conversion on every draw, TTF fonts, graceful console services
  (no browser/fetch yet).  Honest TODOs: part-texture draws need GX
  per-vertex UVs (stand-in scales the whole texture), default font
  (bundle .ttf), software keyboard, app loop (Wii U main.c is the
  template), libogc network fetch.
- README-WII.md documents all of it, incl. the .rel module story:
  ESM/Scroll can later ship as .rel; built-in transformer first.
- build-wii.sh: devkitPro pacman (same installer path as Wii U) +
  wii-devkitppc/wii-libogc/wii-grrlib packages, then powerpc-eabi
  compile-check of the platform layer.
- NEW 10TH GATE build_wii_gate.sh: syntax-checks the Wii platform.c
  against a desktop GRRLIB stub (stubs/wii/grrlib.h).  It immediately
  caught two real bugs: mkdir missing its mode argument (also latent
  in the Wii U platform.c - fixed there too) and a missing u16
  typedef in the stub.

WII U (continued):
- cwv_pgf_load_default now loads the BUNDLED font at
  app0:/assets/font.ttf (packaged like web/ files); absent file =
  NULL handle = empty-box text, never a crash.  build-wiiu.sh warns
  when no font is found and names the expected path.

All TEN gates green; golden zero-diff 5419.
## 2026-09-20 (5) - host moved to shared + Wii U app loop

User asked to (a) confirm the archive stands alone in an empty folder
and (b) continue the Wii U port.  Standalone VERIFIED: the buildfix zip
was extracted into a fresh empty folder and all 8 gates passed there,
plus an absolute-path scan (31cav/mnt//home//Users/) came back empty.
One packaging bug found by that test: the zip lost the executable bit
on scripts - fixed below by setting the mode bits in the zip itself.

HOST SPLIT (the biggest remaining shared move, per the user's rule):
- cwv_host.c/.h moved sources/vita -> sources/shared; ZERO console SDK
  references remain (grep-enforced by the paths gate).
- New console-services seam in cwv_platform.h: cwv_console_init/
  _shutdown (sysmodule loads + apputil bring-up), cwv_console_open_
  browser (the SceAppUtilLaunchWebBrowser hand-off), cwv_console_fetch
  (the blocking sceHttp GET for page fetch()).
- Vita impls live in sources/vita/cwv_platform.c (code moved verbatim
  from the host, incl. the psp2/net/http.h __has_include chain);
  host_load's sceIoGetstat became cwv_file_size and the favicon reader
  became cwv_file_* - pure seam calls.
- Desktop test stub + Wii U impl get graceful no-op versions (fetch
  returns -1, browser -1).

WII U APP LOOP (sources/wiiu/main.c, the real start of the port):
- wut+SDL2 entry: SDL_Init, 854x480 GamePad window, renderer handed to
  the platform layer, cwv_host_create, ESM via the BUILT-IN transformer
  (no suprx on Wii U; RPX/RPL later), loads file:///app0/web/index.html.
- Frame loop: mouse/touch -> pointer events, A button -> focus
  activate, hat Up/Down -> focus cycle, left stick -> smooth scroll,
  START -> quit, then cwv_host_update + cwv_host_draw + present.
- NEW 9TH GATE desktop_test/build_wiiu_gate.sh: syntax-checks the Wii U
  main.c against a minimal desktop SDL2 stub (stubs/SDL2/SDL.h) so the
  port cannot rot between real devkitPPC runs.
- build-wiiu.sh now compile-checks BOTH the platform layer and main.c
  with the real powerpc-eabi toolchain.

Still Wii U TODO: bundled default TTF, software keyboard UI, menu
system port (CWMenu/DevTools drawing), fetch network backend, WUHB
packaging - see sources/wiiu/README-WIIU.md.

All NINE gates green in the tree; golden zero-diff 5419.
## 2026-09-20 (4) - real-build warning fixes + Wii U module types

User ran the real VitaSDK build: 100% VPK built; the final failure was
their OLD build-vita-wsl.sh verification list still expecting
VitaWebESM.suprx/VitaWebScroll.suprx while the new CMake (correctly)
stages CwvESM.suprx/CwvScroll.suprx.  The standalone tree is consistent
on the new names (main.c loads app0:sce_module/CwvESM.suprx +
CwvScroll.suprx); use the NEW build-vita.sh - the VPK their failed run
produced is actually valid.

- cwv_platform.c: SceImeDialogParam.initialText const-cast warning
  fixed (SDK field is non-const; dialog only reads it).
- cwv_html.c tag_setup_li: unused ctx parameter now (void)ed.
- sources/wiiu/README-WIIU.md: Wii U module types documented (user
  note: NO suprx support - executables are RPX, shared libs are RPL;
  ESM/Scroll link into the rpx first, split to .rpl only if needed).

All EIGHT gates green; golden zero-diff 5419.
## 2026-09-20 (3) - standalone distributable + Wii U port started

User direction: the project must be BUILDABLE AND MOVEABLE into any new
folder (distributable), with NO fixed codebase paths, build scripts that
INSTALL build dependencies when missing/outdated, everything else movable
moved into sources/shared, and the Wii U port STARTED.

The uploaded base (pre-CWV snapshot) was diffed against the lineage: its
content is entirely carried by the current tree (only stale webkit
remnants and .bak/.pre035 junk were extra, not carried forward).

- cwmenu/ DATA moved to sources/shared/cwm (pure data parsed by the shared
  CWMenu parser; CMake packaging path updated).  sources/vita now contains
  ONLY console-specific code, data and assets.
- build-vita.sh dependency management:
  * SDK resolution order: $VITASDK -> /usr/local/vitasdk (existing) ->
    <project>/.vitasdk (project-local, standalone + movable).
  * ensure_host_tools: git/make/cmake/python3 installed via apt-get
    when missing (confirm prompt, -y auto-yes).
  * ensure_vitasdk: missing SDK -> official vdpm bootstrap (clone +
    bootstrap-vitasdk.sh + install_all.sh) after confirmation.
    --update-sdk refreshes via vdpm update/upgrade.
    --no-sdk-install skips (CI-friendly).
  * Missing curl/OpenSSL/bzip2/zstd ports: auto-installed via vdpm
    instead of a hard error (still verified after install).
- NEW build-wiiu.sh + sources/wiiu/cwv_platform.c + README-WIIU.md:
  wut + SDL2 seam implementation (rom:/ file mapping, SDL2_image
  textures, SDL2_ttf fonts, RenderGeometry circles, scaled draws).
  Script installs devkitPro pacman + wiiu packages when missing, then
  compile-checks the layer with the real powerpc-eabi toolchain.
  Honest status: app loop (main.c), software keyboard (no native IME in
  wut), bundled default font, HTTP fetch and WUHB packaging remain.

All EIGHT gates green in the standalone tree; golden zero-diff 5419.
## 2026-09-20 (2) - platform seam: shared engine is console-free

User direction: parsing logic and the basic-reading backends belong in
sources/shared so every console reuses them; a script's parsing form
goes to shared, only the drawing form stays console-specific.

NEW SEAM: sources/shared/sources/core/cwv_platform.h is the ONLY console surface the
engine may touch - files (cwv_file_*), texture decode/upload
(cwv_tex_*), fonts (cwv_pgf_/cwv_font_*), draw primitives
(cwv_draw_*/cwv_clear_screen), and the on-screen keyboard
(cwv_ime_open/cwv_ime_poll, UTF-16 via cwv_utf16).  ONE implementation
per console, linked at build time:
  sources/vita/cwv_platform.c       - forwards to real vita2d/sceIo/
                                      sceImeDialog (the textfield IME
                                      dialog code moved here from the
                                      backend, unchanged)
  desktop_test/cwv_platform_stub.c  - identical forwards to the test
                                      stubs, so the golden trace is
                                      byte-identical (PROVEN: zero-diff
                                      5419).

The backend's load_file/load_file_bin/font-cache-write/texture filter
calls were rewritten to the platform API 1:1; renderer.h no longer
includes any console header (vita2d.h / psp2/ime_dialog.h gone; the
RGBA8 macro now lives in cwv_platform.h).  MOVED to sources/shared
(zero console calls - pure parsing/model, verified): cwmenu_api.c/.h
and native_menu_templates.c/.h.  cwv_photo_preview stays vita-side
(console drawing).  cwv_scroll.h is referenced by main.c only.

NEW GATE: desktop_test/build_paths_check.sh - static VPK-build audit
the desktop can run without the VitaSDK: bash -n build-vita.sh, every
CMake-referenced source file exists, every exports.yml NID names a
function that exists in the module sources, and sources/shared contains
NO direct console SDK call (grep-enforced).  The real VPK still needs
the user's VitaSDK run of build-vita.sh; the main gate now ALSO
syntax-compiles sources/vita/cwv_platform.c, so the Vita-side
implementation is compile-verified too.

All EIGHT gates green (paths check is the 8th).  Golden zero-diff.
## 2026-09-20 (1) - CWebView rebrand + cross-platform restructure

User decision (large change): the project is now CWebView, a
cross-platform webapp wrapper.  Target platforms by priority:
PS Vita, Wii U, Wii.  Shorthand CWV (was VWWV).

NEW LAYOUT (as requested):
  sources/shared/   console-agnostic engine: cwv_renderer.h,
                   cwv_backend.c/.h, cwv_css.c, cwv_html.c, cwv_js.c,
                   cwv_broken_image.c, cwv_es6_core.c/.h, js/ (Duktape)
  sources/vita/    console port: main.c, cwv_host.c/.h, cwmenu_api,
                   native_menu_templates, cwv_boot_display,
                   cwv_photo_preview, cwv_version.h.in, cwmenu/default.cwm,
                   assets/, esm_module/ (ESM suprx), scroll_module/
  build-vita.sh    renamed from build-vita-wsl.sh; -y/--yes, --debug,
                   manifest flags, APP_NAME/TITLE_ID env overrides all
                   preserved.  SEMANTIC CHANGE: ROOT is now the script's
                   own directory (project root) - it previously expected
                   to sit one level ABOVE a folder named "vita".  The
                   minify call is now an absolute "$ROOT/tools/..." path.
                   Output VPK lands in the project root.

RENAMES (global, incl. CMake targets, exports.yml NID names, docs,
test pages): VitaWeb* -> Cwv* (types/functions: CwvRenderer,
cwv_host_create, ...); VitaWebView/VitaWebview -> CWebView;
VWWV -> CWV (log is now ux0:data/CWV.log; default TITLEID CWV00001;
SHORTNAME CWV); VITA_WEB_DEBUG -> CWV_DEBUG; VITA_WEBPAGE ->
CWV_WEBPAGE; VITA_LAUNCH_PAGE -> CWV_LAUNCH_PAGE; VITA_APP_DATA_DIR ->
CWV_APP_DATA_DIR.  HTML attributes are BREAKING for existing pages:
vita-control-mode/-modal, vita-button-wait, vita-target are now
cwv-control-mode/-modal, cwv-button-wait, cwv-target.
Modules: VitaWebESM.suprx -> CwvESM.suprx, VitaWebScroll.suprx ->
CwvScroll.suprx.  CWMenu keeps its name (historical DSL; rename later
if wanted).  desktop_test/original_vita_web_backend.c intentionally
NOT renamed (golden-comparison baseline).  Assets (LiveArea) moved to
sources/vita/assets/.  sources/README.md = porting guide for Wii U/Wii.

Verification: ALL SEVEN GATES GREEN with golden reference ZERO-DIFF
(5419 identical) - the rebrand is provably behavior-identical.
KNOWN NEXT STEP (documented in sources/README.md): the shared engine
still draws via the vita2d subset and reads via sceIo (97 call sites,
all in cwv_backend.c + 8 in cwv_html.c) and drives the textfield IME
via sceImeDialog; the first task for a new console (or for finishing
the split) is the cwv_platform.h indirection with per-console
implementations (Vita: direct forwards; desktop: forwards to the
stubs so the golden trace stays identical).
## 2026-09-19 (8) - the diagnostic blind spot: console needs a JS context

User report: images still don't render, DevTools console says
"(no js context)" and shows NO line - file verified present, path
verified correct, image verified valid.  Question: is anything in
VWWV.log?

Answer until this round: NO.  THE ROOT BLIND SPOT: the console ring
lives inside the Duktape JS context, and the context is created only
when a page has <script>.  js_console_add_line() silently RETURNED
when r->js == NULL - so on every script-free page, ALL image
diagnostics (every reason string added across the last five rounds)
were dropped before they could be shown anywhere.  DevTools honestly
said "(no js context)" and the user could never see why.  This is why
the per-image error line never arrived despite being "added" twice.

FIX: renderer-level runtime-log hook.  cwv_backend_set_runtime_
log(backend, fn) (opaque-host wrapper: cwv_host_set_runtime_log)
stores a printf-style callback in CwvRenderer; main.c registers
write_runtime_log (ux0:data/VWWV.log) right after host creation.
img_diag now mirrors EVERY failure line to the log - always, context
or not:

  IMG: image app0:/web/nested/pixel.png: file read failed
       [page dir: app0:/web/nested/]

The <img>-without-src diagnostic in html.c mirrors too.  The offset in
the [page dir] format was off by one ("file:///app0/" is 13 chars;
slicing at +12 doubled the slash - "app0://file"); fixed to +13.
Desktop console is unchanged (context pages keep their lines).

New permanent gate: desktop_test/build_loghook_test.sh + loghook_test.c
- loads a SCRIPT-FREE page with a missing image, asserts the console
is empty ("(no js context)") AND the log hook fired with the exact
expected line.  Proves the chain end to end; golden untouched (the
harness never registers the hook).  All seven gates green.
## 2026-09-19 (7) - the image-path audit: where the path ends up

User report: EVERY image now shows the broken glyph (the glyph itself
was the win: layout and painting are proven, this is purely the load
path), and they asked to check where the path actually ends up compared
to the webpage file.  Full audit of the load chain:

  <img src> -> resolve_url(r->base_url, src) -> "file:///app0/web/<page
  dir>/<src>" -> device path "app0:/web/<page dir>/<src>" -> sceIo.
  src resolves against the PAGE'S OWN DIRECTORY (Chrome semantics),
  verified end to end on desktop: the stub maps app0:/ onto real files,
  so the golden genuinely exercises the resolution, not a fake load.

Two REAL Chrome-semantics bugs found and fixed:
  1. base_dir() stopped at '?query' but not '#fragment' - a page whose
     URL carried a fragment (in-page anchors, location.assign with
     #x) resolved every image against "page.html#frag/" -> all broken.
  2. resolve_url() passed '?query'/'#fragment' from the REF through to
     the file path - src="pixel.png?v=2" tried to stat a file literally
     named "pixel.png?v=2".  Local file refs now strip both (remote
     http refs keep their query, it is part of the request there).

The failure diagnostic now answers the user's exact question in one
console line, no guesswork:
  "image app0:/web/nested/pixel.png: file read failed
   [page dir: app0:/web/nested/]"
Device paths for both the attempted file AND the page's directory, so
a paste from hardware settles wrong-path vs missing-file instantly.

Verified NOT the cause: the build-time minifier leaves img attributes
untouched and copies PNGs byte-identical (tested on img_demo.html);
the entry URL's "?vita=1" was already stripped for base_url use.

Pinned: img_test.html gains src="pixel.png?pin=1" (loads - query strip)
and the harness loads the page itself as img_test.html#frag (fragment
URL page: all images still load).  Golden re-blessed 4365 -> 5419.
All six gates green, zero warnings.

CONTENT-SIDE NOTE: the user's real webapp has NO image references; the
broken images are on the test pages.  If a test page sits in
web/nested/ but pixel.png was left at web/ root (or not copied), every
image is correctly reported as missing - the new console line will say
exactly that, and the fix is ../pixel.png in the src or moving the file.
## 2026-09-19 (6) - Open URL: web-root-relative resolution (the double-nesting fix)

Prerendering idea from the user: discussed and DROPPED by choice - the
draw pass is already cheap (offscreen cull, once-per-load decodes) and a
cached frame breaks under scroll/zoom/focus/JS mutation. No code.

THE BUG (user report): typing "./nested/img_demo.html" into Open URL
gave .../web/nested/nested/img_demo.html.  The IME prefill was made
web-root-relative last round ("./nested/second.html"), but the resolver
still resolved input against the CURRENT PAGE'S directory, so the
subpath doubled wherever you were one level deep.

Now the whole box speaks one convention, matching the prefill:
  "./file" / bare "file"  -> app0/web/file (the WEB ROOT, always)
  "../file" (repeatable)  -> climb one segment, clamped at web/ -
                             "../" can never escape into app0/ proper
  "web/...", "app0/...", "file:///..." -> unchanged convenience forms
  remote http(s) -> still refused (webapp wrapper, not a browser)

The ../ climber was rewritten to handle both string shapes correctly
(plain-segment tails drop just that segment, slash tails drop the last
directory) with a "web is never removable" guard on every path.  The
rewrite was simulated against 12 RFC-3986 cases before touching C
(mid-URL climbs like "a/../b/../../c.html" pass); the first draft
over-climbed by one segment and the simulation caught it.  All six
gates green, golden unchanged (main.c is outside the golden path).
## 2026-09-19 (5) - bundled broken-image glyph (real asset, not primitives)

Swapped the hand-drawn mountain-and-sun glyph for the ACTUAL broken-image
icon the user supplied: embedded as a compiled-in byte array
(src/backend/cwv_broken_image.c, 765 bytes) so a failed <img>
always has a real icon, with zero dependency on the user's web/
directory (this is an ENGINE asset, not page content - a webapp might
not ship one).  Decoded once, lazily, on the first failed image
(broken_image_texture() in cwv_backend.c); the texture is cached
and reused for every subsequent failure.  Drawn at native pixel size
times zoom, in the corner of the existing placeholder box - same spot
the old glyph occupied, same diagnostic value (visible glyph = load
failure, not a layout failure).

New source file wired into CMakeLists.txt AND every desktop_test build
script that compiles cwv_backend.c (check_reference, js_test,
es6_test, build_and_compare).  New stub vita2d_draw_texture_scale,
traced (TEXSCALE lines).  Golden re-blessed 4710 -> 4365 (net smaller:
the old glyph was ~16 primitive calls per failure, the icon is one
draw call).  All six gates green, zero warnings.

Answering the standing question: <img> and CSS background-image are
TWO SEPARATE paths, both real. <img src> parses via tag_parse_img,
resolves the URL, calls load_image, and on failure draws this
placeholder. background-image (added last round) is CSS-only: it never
shows a broken-image icon on failure, exactly like Chrome - a failed
background just leaves the element's background COLOR showing, with a
console error for the reason.
## 2026-09-19 (4) - background-image, the failed-image glyph, and the silent-path audit

The user's device shows images as "just the background color" with NO
console error - so the failure was in a path that never logs.  Found and
closed every silent path:

**CSS background-image (the prime suspect).**  `background:
url(...)` / `background-image: url(...)` were parsed as SOLID COLORS
ONLY - the url() was silently dropped, nothing loaded, nothing logged,
and the element showed its plain background color: exactly the reported
symptom.  Now supported end to end: the CSS layer stores the raw url()
(quoted or bare), the HTML parse resolves it against the page URL and
loads the tile with the same diagnostics as <img>, and the paint pass
tiles it from the box's top-left (Chrome's background-repeat: repeat),
clipping edge tiles as texture sub-regions - no scissor, zoom-aware.
In the shorthand, the color AFTER the url() is now parsed too
(`background: url(x) #cfe8ff` paints the color under the tiles).
Failed background loads log the reason to the console.

**The default failed-image glyph (user request).**  Every <img> that
exists but cannot load now draws a tiny mountain-and-sun picture with a
bold red slash, next to the existing placeholder box - built from plain
primitives, no bundled asset, tracks zoom.  This is the path/render
splitter: glyph visible = layout and painting are fine, the failure is
in loading; no glyph and no box = the element never made it into
layout at all.

**<img> with no usable src** was the one remaining fully silent failure
(no load attempted = no diagnostic anywhere); it now logs an error to
the console like Chrome's DevTools.

**Open URL placeholder** is now the current page's path RELATIVE TO
web/: app0/web/nested/second.html prefills ./nested/second.html.

Pinned by bgimage_test.html (tiling, shorthand color layer, failed
background, failed img with the glyph); golden re-blessed 4306 -> 4710.
All gates green, zero warnings.
## 2026-09-19 (3) - HTML lists (<ul>/<ol>/<li>) and <blockquote>

The audit's top two ranked elements, both Chrome-shaped:

**<ul> / <ol> / <li>** - real list rendering end to end:
- Each list reserves 24px of marker room; because block containers
  narrow the content flow by their padding, NESTED lists indent one
  level per depth, exactly like the browser.
- Markers: disc (default for <ul>), circle, square, decimal (default
  for <ol>, right-aligned so multi-digit numbers keep a clean edge),
  and none.  Numbers come from a tiny per-parse stack, so nested
  <ol>s count independently and an outer list resumes after an inner
  one closes.
- New CSS keys: `list-style-type` (disc/circle/square/decimal/none) and
  the `list-style` shorthand, settable on the list OR on an <li>
  (item wins, like Chrome).
- Markers inherit the item's text color and zoom correctly.

**<blockquote>** - the Chrome user-agent inset: 1em vertical margins
plus a 40px left indent.

Pinned end to end by lists_test.html (bullets, numbers, all marker
kinds, nesting, blockquote) in the golden harness - re-blessed 4237 ->
4306, pure append, zero drift on existing pages.  All six gates green,
zero warnings.

Also noted: the main-menu Open URL row was already fully wired (CWMenu
row + IME + dispatch) - no work was needed there.
## 2026-09-19 (2) - PNG loader fallback + offscreen cull + console cache + warning fix

**Images: PNG decode fallback.**  Some libvita2d builds expect the 8-byte
PNG signature to be pre-consumed by the caller; others skip it internally.
The decoder now retries with the signature skipped when the straight call
fails - safe on either generation, and combined with last round's console
diagnostics (which print the exact failure reason per image) this closes
every decode path we can reach from C.

**Optimizations (kept deliberately readable):**
- Offscreen cull in `draw_node`: a node entirely outside the viewport
  paints nothing, so scrolling a long page draws only the visible nodes
  instead of the whole document.  Partially visible nodes still paint, so
  nothing pops in or out.  Golden re-blessed 5158 -> 4237 lines (921
  removed lines were all draws below the viewport).
- DevTools Console tab cache: the ~16 KB ring dump plus the parse and
  word-wrap of every row used to run every single frame; it now re-runs
  only when a new console line actually lands.
- (Intentionally NOT done: caching the z-index paint sort - it would need
  a document-dirty flag threaded through every style mutation, and a
  stale-paint bug is a far worse trade than an O(n) insertion sort.)

**Build warnings fixed.**  The ESM self-test `console.warn` was one
truncation warning away from gcc's -Wformat-truncation on the real SDK:
message shortened and the report bounded with %.120s.  The image
diagnostic buffer is now sized for its worst case.  Main gate compiles
with zero warnings in both flavors.
**Images: every failure is now visible and diagnosable.**
- Local images no longer share the 256 KB page-file cap: `load_file_bin`
  reads them binary-exact up to 4 MB (a large photo silently failing was
  the most likely "no image renders" cause - the old generic loader
  refused anything over VW_MAX_HTML).
- Every image that falls back to its alt-text placeholder now logs a
  console error naming the exact reason: file read failed, over the 4 MB
  cap, over the 4096px GPU texture limit, or decode/upload failed (bad
  data, or GPU memory full) - with byte size and magic bytes.  On device,
  DevTools > Console now answers "why is this image broken".
- Over-4096px PNGs (the GPU limit; the JPEG loader downscales, PNG does
  not) are caught before uploading garbage to GXM.

**New CSS key: `image-rendering`.**  `pixelated` (and the `crisp-edges`
alias) = hard nearest-neighbor sampling - retro art and pixel fonts stay
chunky when scaled; `auto` (default) = smooth linear.  Pinned end-to-end
by a pixelated demo image in the golden harness (re-blessed at 5158).

**Console colors verified, not removed.**  The level check runs on the
stored prefix BEFORE the display strip, so error/warn/hint/info rows keep
their colors with the prefix hidden (proven by compiling and running the
exact shipped logic: error red, warn gold, hint blue, info blue-white,
log white by design like a real browser).

## 2026-09-19 - CWMenu text wrapping + honest ESM diagnostics + tab-stop registry

**CWMenu text now wraps inside its element (never over the window).**
- New shared painter `menu_draw_text_wrapped` in main.c: word-boundary
  wrapping into the element's own width (hard break for a single oversized
  word), 22*scale console-proven line pitch, and when wrapped lines would
  exceed the element height the last visible line ends with "...".
- Applied to the DevTools live rows (URLs, errors, stats - the ones that
  ran past the window edge) and the dual-window partner's text elements.

**ESM "exports were not wired" is now a diagnosis, not a guess.**
- New boot-time wiring probe: `cwv_esm_enabled()` returns 1 only if
  the module's own code really ran, so the app can tell "the NIDs were
  never wired" (Vita3K stubs, or a stale suprx built apart from the eboot)
  apart from "the module ran but its self-test failed".  The runtime log,
  DevTools Platform row and the one-time console warning all carry the
  actual self-test report (`transform=FAIL(rc=...)`) plus the wired flag
  and the actionable advice (rebuild eboot + CwvESM.suprx together).
- Pages keep working either way: the built-in fallback transformer is
  identical to the module's, which is why the ES6 test/demo pages run fine
  while the message appears.

**The tab-stop list now lives in the source, as requested.**
- `VW_FOCUS_RULES` in cwv_backend.c: one row per element kind with
  what X does (button/select/a/input/textarea/img), and
  `renderer_is_focusable` is driven by it.  docs table stays in sync.

**Build script: minification can never break a build again.**
- `./tools/minify_web.py` is called by RELATIVE path (`$ROOT` resolved one
  directory too high on real setups); on failure the build prints a loud
  gold message and ships the unminified webapp instead of aborting.

**File orientation banners** at the top of all four backend files, so the
  answer to "where do I write a draw function / where does text draw" is
  in the file you open first: parsing+layout in cwv_html.c, styles in
  css.c, pixels (and the text painter) in backend.c, scripting in js.c.

## 2026-09-19 - X activates focused links + comment-proof parsing + plain CWMenu text

**X now opens links in EVERY control mode (the links.html bug).**
- Root cause: pages without `cwv-control-mode` run in the default/custom
  mode, where CROSS only forwarded a raw button event to page JS and never
  activated the DPAD-focused element - tab cycling visibly highlighted the
  `<a><div>...</div></a>` block but X did nothing. CROSS now ALSO activates
  the focused stop in that mode (links navigate, buttons press, selects
  open, textfields open the IME), while page JS keeps receiving the raw
  button state as before.
- Link clicks now follow CHROME ORDER everywhere (DPAD and touch): click
  listeners run FIRST, then the href navigates. A listener cancels the
  navigation only by calling `event.preventDefault()` (listeners now
  receive a small event object: `type`, `defaultPrevented`,
  `preventDefault()`). A pure-fragment href ("#top") fires the click but
  never reloads the page.
- The DPAD focus ring is painted as a LAST pass, so elements that draw
  after the focused link can no longer cover its bottom edge (that is why
  only the LAST link on links.html showed a complete ring).

**Comments never render and never break rules (HTML/CSS/JS).**
- CSS: block comments (`/* ... *` + `/`) and the legacy `<!--`/`-->` guards
  are stripped BEFORE parsing, anywhere in the sheet - including BEFORE a
  selector (a leading comment used to swallow the whole rule) and between
  declarations. JS comments are handled by the engine; HTML comments were
  already skipped. New golden page `comments_test.html` pins all of it.

**Console reads like a browser's.**
- DevTools console rows no longer display the "log: ", "info: ", "warn: ",
  "error: ", "hint: " level prefix - the row color carries the severity.
  (Storage keeps the prefix; repeat detection and error lookups still work.)

**CWMenu text elements are plain text now.**
- The DevTools card treatment (soft panel + 4px accent edge + bottom
  separator) is gone by default. New optional CWMenu element key
  `border <sides>` opts into chrome: a comma/space separated list of
  `top right bottom left all fill none` (fill = the soft panel background).
- Build step simplified: the VPK ships the readable webapp sources exactly
  as written (the renderer strips comments at runtime anyway);
  tools/minify_web.py stays for optional manual use. The unused
  menu_template_find_action helper was removed (real-SDK -Wall clean).

## 2026-09-19 - Real image rendering + CSS z-index/object-fit + IME real-hardware fix

**<img> finally renders real content (and the white blob is gone).**
- `vita2d_draw_texture_part_scale` was called with the ARGUMENTS IN THE WRONG
  ORDER (the old call passed the texture part as the screen rect), which
  painted a white/garbage blob instead of the image. The call now matches the
  real libvita2d signature `(texture, x, y, part_x, part_y, part_w, part_h,
  x_scale, y_scale)` - the desktop stub mirrors the real signature so the
  golden trace pins it.
- The decoder is picked from the file's MAGIC BYTES (JPEG/PNG/BMP), not the
  URL extension, so extensionless web URLs (`.../logo?size=x`) decode
  correctly. Internet images load with NO JavaScript (libcurl fetch, same
  path as `<link>`/`<script src>`).
- Chrome sizing rules: no width/height at all = the image's intrinsic (real
  pixel) size; one side given = the other side derived to keep the aspect
  ratio; oversized images shrink to the content width keeping the ratio.
- `alt` attribute: a broken image draws a Chrome-style placeholder box with
  thin border + the alt text. Images also honor CSS `border` and take DPAD
  focus when they carry an explicit `tabindex`.
- New CSS properties (registry rows in `cwv_css.c`):
  `z-index` (stable paint-order sort in `backend_draw`; equal values keep
  document order) and `object-fit` (`contain` letterboxes, `cover` crops,
  anything else stretches/fills).

**Button labels are vertically centered like Chrome.**
- The old formula dropped the pen HALF a line height below the box middle;
  with PGF/TTF pen semantics (glyphs extend ~75% of the line box above the
  pen) the correct offset is a QUARTER line. Every button label moved up by
  25% of a line box; visible in the golden trace as the intended baseline
  change.

**Plain inline `<a>` elements are real DPAD stops now.**
- A box-less inline link reserves no node of its own, so its first child
  node (text run, or a wrapped block) becomes the ONE focus stop per link
  and receives the focus ring, like a browser. Positive `tabindex` values
  order the stops first, then document order (existing behaviour); links
  without `href` are not stops, like Chrome.

**IME keyboard freeze on REAL hardware - root cause found and fixed.**
- Real Vitas require two things Vita3K silently fakes: a one-time
  `sceCommonDialogSetConfigParam` at boot, and a per-frame
  `vita2d_common_dialog_update()` between `vita2d_end_drawing()` and
  `vita2d_swap_buffers()`. Without them the IME keyboard NEVER composites
  into the framebuffer: the app keeps running (START still exits) but looks
  frozen with an invisible keyboard. Both are now done (`init_common_dialog`
  in main.c mirrors VitaShell's boot pattern, reading the system
  language/enter-button via APPUTIL when available). Needs confirmation on a
  real Vita; Vita3K behaviour is unchanged.

**Minification moved to BUILD time.**
- New `tools/minify_web.py`: the readable webapp sources stay the single
  source of truth; `build-vita.sh` stages a conservatively minified copy
  (HTML comments + CSS whitespace removed, JS and binary assets untouched)
  into `vita/build/webapp_min` and packages THAT. `<pre>/<textarea>/<script>`
  content is preserved verbatim so template literals never change.

**Tests.**
- New golden pages `img_test.html` (intrinsic/attrs/CSS sizing, object-fit,
  remote extensionless URL, broken-image alt, tabindex order, link stops)
  and `sub/img_climb.html` (../ URL resolution now climbs directories like a
  browser and never above the site root). Simulated network in the desktop
  curl stub delivers a deterministic PNG so remote-image loading is pinned
  without touching the internet. Golden reference re-blessed (5076 lines);
  all other suites pass (js, es6, es6_unit, cwmenu_unit, main gate both flavors).
  NOTE: `build_and_compare.sh` now reports the intentional image/button/link
  diffs against the ORIGINAL backend; `check_reference.sh` is the regression
  gate going forward (its header documents the re-bless policy).


## Custom JS API removed: standard DOM only (plus ES6 fixes)
- **The non-standard element API is gone** (user request): setText/getText,
  setValue/getValue, setStyle, setClass, hide/show, setActive/isActive,
  on("activate"), index, document.controlMode, document.onExit and
  document.reload are all removed. Pages now use the STANDARD DOM surface
  only: `el.textContent`, `el.value`, `el.style.cssText`, `el.className`,
  `el.hidden`, `el.addEventListener/removeEventListener("click", fn)`,
  `document.location.assign`. Standard-DOM pages run here unmodified, and
  pages written for this renderer run anywhere. All testdata/demo pages
  were converted; the golden trace is still zero-diff (4277 lines).
- **START is a plain quit** again (debug builds: debug overlay toggle).
  The onExit exit-redirect hook was removed with the rest of the API.
- **Theme no longer flickers** (user report: green for less than a
  second): `cwv_backend_set_theme()` now returns without touching
  any node when the requested theme equals the current one, so the
  per-frame apply_runtime_overrides() call can no longer revert a JS
  inline style. A REAL theme switch still re-propagates default text
  colors, but explicitly-styled nodes are flagged (color_explicit) and
  keep their color. es6_test pins both: 5x same-theme calls leave the
  #out text #34d399, and a real dark/light switch does not revert it.
- **Nested arrow fix in the ES6 transformer**: an arrow EXPRESSION body
  was copied verbatim, so an arrow nested inside it (e.g.
  `() => [1].map(x => x * 2)`) never got converted and the script died
  with a SyntaxError. Expression bodies are now transformed recursively
  (same policy as for..of); 3 new es6_unit cases pin the exact output.
- **New ES6 self-test page** (testdata + test_pages es6_selftest.html):
  plain ES6 exercising the transformer (let/const, arrows, template
  literals, for..of) and the prelude library (Map/Set, Array/String/
  Object/Number/Math extensions) end-to-end on a brand-new page - the
  JS/ES6 surface is engine-level, available to ANY page in the web dir,
  not hardcoded per page. es6_test loads it, expects 23 passed / 0
  failed, a green summary and every PASS row in the console.
- **Console colors** (user request): log rows render white, error red,
  warn gold, info light blue.
- Docs updated: EXTENDING_THE_RENDERER.md JS API list + recipes,
  CONFIGURATION.md START description.

## #out color fix: JS inline styles now re-propagate inheritance
- **Stale parse-time inheritance fixed** (user report: `#out` never turned
  green in the ES6 demo): the engine copies inherited properties into
  child nodes at PARSE time, so `el.style.cssText = "color: ..."` (or
  `setStyle`) only repainted the container box - its child text node kept
  the old color. New `style_apply_inline()` in cwv_css.c (used by
  js setStyle and style.cssText) re-propagates the four inherited
  properties (color, bold/font-weight, centered/text-align, font-size)
  into the subtree after applying, with a CSS-correct guard: a
  descendant only takes the new value while it still carries exactly the
  value it originally inherited; anything set by its own rule or style
  attribute wins. Pinned by es6_test: after the 3rd #inc activation the
  #out text node must be #34d399, and after #reset back to the body
  color #f8fafc. Golden still zero-diff; js, es6_unit and the dual
  -flavor main gate all pass.

## Menu Tests removed + live DevTools tabs + rendering audit
- **Menu Tests is gone** (user request, CWMenu 2 feedback round 3): the whole
  demo section - 23 .cwm menus, the C template arrays + registry rows, and
  every MENU_TESTS branch in main.c (draw/touch/dpad/CROSS/CIRCLE paths,
  focus table, window-demo toggles, sample widget fields) - is deleted.
  The main menu is recentered on its three real rows (Page Operations /
  Open URL / Settings) and the native menu no longer carries a fifth mode.
  Window keys (anchor/size/corner/crop/pair) stay fully supported by the
  parser; cwmenu_unit pins them with an embedded probe script instead of the
  removed demo menus.
- **DevTools tabs show live data** (user request): renderer tab lists
  engine, viewport+zoom, DOM node count, JS active/inactive and the last
  JS error; input tab lists the last pressed button and the D-pad focus
  target (#id / <tag>, or (none)); page tab lists title/url/version,
  console line+error+warn counts, and JS state. New read-only host
  getters `cwv_host_node_count` / `cwv_host_focus_info`.
- **Back is a real button again** in all 8 DevTools tab menus (plain-text
  styling from the previous round is kept for the tab strip only).
- **docs/RENDERING_AUDIT.md**: honest inventory of supported tags/CSS plus
  ranked, spec-referenced suggestions (lists, blockquote, border-radius,
  max-width, pre/code, checkbox inputs, line-height; flex/tables noted
  as big later projects).

## Built-in ESM fallback + START overlay binding + Open URL relative refs
- **Built-in ESM fallback (the real Vita3K fix)**: the boot self-test
  sometimes reports FAIL on Vita3K even after the module loads, because
  Vita3K silently stubs unresolved cross-module exports to "return 0" -
  `cwv_esm_initialize` "succeeds" without running, so pages silently
  run raw ES6 (the exact "module loaded, SELF-TEST FAIL" signature). The
  app now embeds the SAME transformer source the suprx is built from
  (`cwv_es6_core.c`, added to the app target in CMakeLists). When the
  module self-test fails, main.c calls the new
  `cwv_host_enable_esm_fallback()` (exact-signature adapters, no
  function-pointer casts), re-runs the self-test, and keeps pages working
  on real hardware AND on the emulator. The state is reported honestly
  everywhere: Platform row ("ACTIVE via BUILT-IN FALLBACK"), console chip
  (`on(pass*_builtin)`), overlay row ("builtin fallback"), and a one-time
  `console.warn` on the first page (not an error - pages work). A total
  fallback failure stays a loud console.error. Pinned end-to-end by the
  new es6_test Case D: stubbed module -> FAIL -> fallback -> PASS -> the
  CRLF page reloads with zero errors.
- **START replaces the DevTools button** (user request): the 8 per-tab
  "Show Debug Info" buttons are REMOVED (they overflowed the footer
  width). In DEBUG builds START now toggles the overlay (replacing its
  close-app meaning there - exit stays reachable via DevTools > Close and
  Settings > Exit); in PRODUCTION builds START keeps closing the app
  unless the page intercepts it. The DevTools footer hint prints the
  current flavor's START meaning.
- **DevTools de-buttoned** (user feedback): the tab strip and the Back row
  are now PLAIN TEXT (no boxes) - active tab bright with an accent
  underline, inactive muted; the footer row is back to full width.
- **Open URL = HTML relative references** (user request): the dialog is
  pre-filled with the CURRENT page as a compact `./file.html` value (no
  giant file:// string, no hidden appended prefix). Input follows HTML
  semantics against the current page's directory: `./x.html` and bare
  `x.html` load siblings, each `../` climbs one directory (clamped at the
  app0 root), `web/...` stays web-root-relative, `app0/...` and full
  `file:///...` pass through. Remote http(s) is still refused.
- Tests: all suites green - golden 4277 zero-diff, js, es6 (incl. new
  Case D), es6_unit, cwmenu_unit (dbg removal + plain-text pins), user_test
  (CRLF full flow), main gate both flavors warning-free.

## ESM self-test at boot + production gate for the debug overlay
- **ESM self-test (the "same errors every load" root cause made
  detectable)**: a tiny ES6 probe (arrow fn + template literal) is pushed
  through the SAME transform/compile/prelude hooks real page scripts use,
  at boot, right after `cwv_host_enable_esm()`. Three checks:
  `transform=` (the module MUST report a real rewrite of the probe),
  `compile=` (the rewritten source must be valid ES5.1 in Duktape),
  `prelude=` (the ES6 library must be installable). Why: on Vita3K a
  suprx whose exported NIDs did not resolve is silently stubbed to
  "return 0", which looks exactly like "module enabled" while every
  script runs raw ES6 -> "SyntaxError: unterminated statement" on every
  page, every load. Case B of the new regression literally reproduces
  that signature and reports it with the cause named, at boot.
  Surfaces: runtime log, DevTools > Platform (ACTIVE, self-test PASS /
  ACTIVE but SELF-TEST FAILED), the Console tab status chip
  (on(pass)/on(FAIL)), the debug overlay ESM row, and a one-time
  console.error injected into the first page when it fails.
- **Debug overlay is debug-build-only** (user request): new compile-time
  `CWV_DEBUG` flag. Production (default) compiles the overlay out:
  SELECT ignores it, SELECT+X customize mode is gone, and a leftover
  `dbgshow=1` in settings.cfg cannot re-enable it. (The DevTools "Show
  Debug Info" button was later removed entirely - see the next section.)
  Enable with `build-vita.sh --debug` or the webapp manifest
  `"debug": true` (`false`/`0` forces production). The build summary
  prints the flavor.
- **Apostrophe battery in the permanent unit suite**: 7 new es6_unit
  cases pin that apostrophes/quotes inside strings, comments, arrow
  bodies, template literals and HTML-attribute strings are never
  touched, split or re-quoted by the transformer.
- **Durable main.c/host.c syntax gate** (`desktop_test/build_main_gate.sh`):
  the previously ephemeral /tmp stub headers are now in-tree
  (desktop_test/stubs), and the gate compiles BOTH flavors
  (`CWV_DEBUG=0` and `1`) so a mismatched #if/#endif pair in either
  flavor fails on the desktop.
- Tests: golden reference 4277 lines zero-diff; js_test, es6_test
  (now with the self-test A/B/C regression), es6_unit (with apostrophe
  battery), cwmenu_unit all pass; main gate passes in both flavors.

## ESM module rename + console repeat badge + debug overlay
- **CwvES6 is now CwvESM** (the ES2015+ module the architecture
  calls for): .suprx name, CMake targets/stubs, exported functions
  (`cwv_esm_*`), host call (`cwv_host_enable_esm`), UI rows and
  docs all use the ESM name. The transformer core files keep their
  `cwv_es6_core.*` names - they describe the ES6->ES5 conversion.
- **Console repeat badge**: identical consecutive console lines collapse
  into one row and show a DevTools-style count badge (accent-colored
  circle + number) that grows live as the error repeats, so a page that
  throws the same error every frame no longer floods the 64-line ring.
- **Debug overlay** (on-page diagnostics): SELECT toggles it,
  SELECT+X opens customize mode (d-pad + X flip rows). Rows: FPS, frame
  count, last button press, last JS error, ESM module state, uptime,
  page URL, console totals. Styled through the user's accent color;
  visibility + row selection persist in settings.cfg.
- **DevTools "Show Debug Info" button** (user request): every DevTools
  tab now has a button next to Back that toggles the on-page debug
  overlay, persists the choice to settings.cfg, and drops back to the
  page so the change is visible immediately (CROSS or touch; also
  reachable with d-pad navigation). SELECT on the page still toggles
  it instantly, and both paths persist.
- **Clearer ES6+ failure hints**: a compile failure now prints ONE hint
  naming the exact unsupported construct (class / async / generators /
  spread / destructuring / object shorthand) instead of the confusing
  two-line module-vs-engine explanation. DevTools > Platform now shows a
  live "ESM module: ACTIVE / NOT LOADED" row plus an honest
  "Duktape = ES5.1 only" engine row.
- **Found by testing**: Duktape 2.7 natively parses object shorthand
  `{a, b}` - it was never the source of the "unterminated statement"
  errors (those come from genuinely unsupported syntax: spread, class,
  destructuring). The hint for shorthand stays as a fallback only.
- Tests: js_test pins the badge dedup, the spread/class hints and the
  console stats; user_test (the CRLF full-flow pin) still loads the real
  app files with zero errors; golden 4277 lines zero-diff; es6, es6_unit
  and cwmenu_unit suites all pass; main.c/host.c syntax gate clean.

## WebKit plan removed from the project
- The Sony 3.61-3.65 WebKit migration (`webkit/`, `vita_webkit_probe.c/.h`,
  the `CWV_ENGINE=legacy|webkit|auto` build option, the JSC/WebCore
  adapters, and the related tools/docs) is DELETED. Reason: the required
  SDK/runtime dependencies (vita-cairo, WTF, JavaScriptCore, WebCore) cannot
  be obtained or rebuilt reliably. The native HTML/CSS + Duktape renderer
  is the permanent engine; DevTools' "migration" section now shows the
  internal stack (native renderer / Duktape JS / ES6 module).
- Behavior is unchanged in shipped builds: the engine was always `legacy`
  by default, so no runtime code path differs - this is a source cleanup.

## Real-build fix: correct sceHttp header path
- The Vita build failed with `psp2/http.h: No such file or directory` - the
  real VitaSDK header is `<psp2/net/http.h>`. The include now
  auto-detects (`__has_include`) `psp2/net/http.h`, then `psp2/http.h`
  (old SDK snapshots), and if neither exists fetch() support is compiled
  OUT: the app still builds and pages run, fetch() just reports failure.
- Fixed a real-API mismatch: the body read must use `sceHttpReadData()`
  (the earlier name came from a desktop stub); signatures and constant
  values were verified against the official VitaSDK headers
  (SCE_SYSMODULE_HTTP=0x0002, SCE_SYSMODULE_SSL=0x0003, etc.).
- Desktop stubs now mirror the real headers exactly (path, names, values)
  so the syntax gate catches this class of mistake.

## fetch() for scripts + Open URL row + CRLF line-ending fix
- **Script network access**: pages can call
  `fetch(url, callback)` - an http/https GET through sceHttp with 10 s
  send / 15 s receive timeouts and a 256 KiB body cap. The callback ALWAYS
  runs: `status >= 0` is the HTTP code with the body in the second arg;
  `status === -1` means failure (no Wi-Fi, bad URL, timeout). fetch never
  throws and never crashes the app, even if the callback itself throws.
  This is data access, not browsing - remote pages are never rendered.
- **Open URL main-menu row works now**: opens the Vita keyboard and loads
  the entered URL. Local pages resolve against the web root
  (`pages/editor.html`, `./x.html`, `app0/web/x.html`, full
  `file:///app0/...` URLs all work); remote http(s):// URLs are refused
  with a console error; a wrong path logs to DevTools > Console. Wired to
  both touch and the X button.
- **CRLF line endings fully supported**: a Windows-saved script with CRLF
  endings used to break the ES6 transformer (the `\r` at a line-comment end
  swallowed the next line's keyword - `const` on the line after a
  `// header` comment became part of the comment, so it reached the engine
  raw). Fixed, with 6 CRLF regression cases in the es6 unit tests and the
  user's own CRLF files pinned by desktop_test/user_test.c. LF and CRLF
  now produce identical results; LF stays the recommended default for new
  files.
- ES6 module failure is visible: DevTools > Platform shows
  `ES6 OFF (module missing)` and raw-ES6 compile failures log a `hint:`
  line naming the construct and the missing module.

## Console log viewport + script diagnostics + partner-window tap guard
- DevTools Console tab is a real console now: the whole ring buffer renders
  as a scrolling, color-coded log (error/warn/hint/info/log) in the content
  panel - no more button-like card rows. Long lines word-wrap, a scrollbar
  appears only when rows overflow the viewport, D-pad up/down pages the log,
  and it auto-follows the newest output (unless you are mid-drag).
- Console tab top-right shows a JS/ES6/line-count status chip.
- Scripts that fail to compile log a `hint:` console line naming the
  unsupported ES6 construct (class / async-await / spread / destructuring /
  generators) that caused it - the "same errors every load" case.
- Fixed a memory leak: the ES6 transform buffer was not freed when a script
  failed to compile or run.
- Dual-window partner is display-only: taps inside the floating partner
  frame are swallowed so they can no longer hit hidden active-menu
  elements underneath it.
- The console template in cwmenu/default.cwm drops its four fixed text rows;
  scrollbar item lists may now be empty.

## 0.4.4.15 - CWMenu 2 polish: 8-tab Menu Tests tour, dual windows on top

- **Menu Tests page 1 reworked** (user feedback: pages were crowded) - now 8
  tabs: Basics / Text / Controls / Data / Shapes / Lines / Styles / More.
  The dense Basics and Shapes pages split into new Text and Lines tabs;
  every content row and tab is taller (44px+), so demos are bigger touch
  targets; labels center vertically in their rows.
- **Dual window ordering** - the `pair` partner now renders AFTER the active
  menu, painting on top where windows overlap, so a partner window can sit
  beside or over the active menu without being hidden by its chrome.
- **DevTools > Platform** shows the live CWMenu script version and where it
  loaded from (packaged default vs. a per-app data-dir override) - a stale
  override is the usual reason new menu features "don't appear".
- **cwmenu_unit**: +2 checks (first-page 8-tab row present in all 7 page-0
  menus; enlarged row height) - 34 total, all pass. Golden renderer trace,
  JS, and ES6 suites unchanged and passing.

## 0.4.4.14 - CWMenu 2: script-owned theming, window keys, dual windows

- **CWMenu 2 version header** - the parser accepts and records any `CWMenu <n>`
  header, warns on newer scripts, and ignores unknown constructs line by
  line (forward compatible both directions).
- **Theme block** - menu colors now live in the script: `accent0..5` (with
  `labelN` display names), `text-normal/light/dim`, `neutral`, `overlay`.
  The built-in C colors are now the fallback, not the source; an app re-skins
  the whole native shell by editing its .cwm file.
- **Window keys** - `window` elements accept `anchor X Y` (absolute position),
  `size S` (0.75-1.25 chrome scale), `corner rounded|sharp`, and
  `crop L T R B` (edge insets).
- **Dual windows** - `pair <menu>` renders a second menu as a live window
  with its own anchor/size/corner/crop; active menu keeps all input, one
  level deep. `vita_native_menu_find()` resolves any loaded menu by name.
- **Dynamic app menus** - unregistered `menu` blocks land in a dynamic
  registry (8 x 32 elements), so apps can define their own settings,
  devtools, and custom menus with zero C changes.
- **Zoom level** is a real CWMenu slider in Settings > Appearance (6 stops,
  0.75x-2.00x) instead of a text row.
- **Menu Tests: Windows tab** (8th advanced tab) - five toggles live-edit
  the partner window at runtime: size, corner, crop, pair on/off, offset.
- **Bug fixes**: target registry raised to 64 (34 targets silently exceeded
  the old cap of 32, which would have emptied every menu at boot);
  `corner rounded` no longer parses inverted; theme label lines no longer
  count as parse errors; dynamic-menu name copies are bounded.
- **Tests**: new `desktop_test/build_cwmenu_unit.sh` - 33 checks covering the
  whole CWMenu 2 contract (version, theme, window keys, pair lookup, zoom
  slider, menu completeness, dynamic registry, CWMenu 3 probe). Golden
  renderer trace (4277 lines), JS, and ES6 suites all still pass.
- **Docs**: new `docs/CWM_SCRIPT.md` - full CWMenu 2 script reference.

## 0.4.4.5 — Discrete CWMenu slider input fix
- Keyboard, D-pad, CROSS, and other discrete slider adjustments now use the exact number of positions declared by each slider's `stops` property.
- Brightness no longer has eight hidden keyboard positions behind six rendered positions; six stops map directly across `0.25` through `2.0`.
- Pitch-stop-count sliders now respect their own declared stop count instead of always allowing the global `2..6` range.
- Scroll-speed sliders also clamp their input to their declared stop count.
- Existing touch coordinate mapping and six-stop maximum are preserved.


## 0.4.4.5 CWMenu slider update
- All CWMenu slider stop counts are now declarative, with a maximum of six stops.
- The Menu Tests pitch slider uses six stops.
- Settings Controls Scroll speed uses the same pitch-slider visual treatment with Slow/Normal/Fast stops.
- Fast scroll speed is increased to 2.25x the normal hardware scroll step.
- Existing CWMenu menu surfaces remain script-defined.
## 0.4.4.5 — CWMenu scrollbar API
- Added the reusable Vita Webview Menu (CWMenu) scrollbar API.
- Added `VITA_MENU_SCROLLBAR` as a template element kind.
- Scrollbars now appear automatically when template content exceeds its inset viewport.
- Settings and DevTools scrollbar rendering uses the shared scrollbar model.

## 0.1.1 - 0.4.4.5 — Production/build version correction

- Corrected the production/build display to `0.1.1 - 0.4.4.5`.
- Updated the default CMake build identifier, build-script fallback, and package metadata to `0.4.4.5`.
- Removed the temporary backend-version menu entry; no separate backend version is exposed by this change.


## 0.1.1 - 0.4.4.13 — Menu Tests compile cleanup / backend version

- Added the forward declaration for `menu_style_accent_color` before Menu Tests focus rendering, eliminating the implicit/conflicting declaration error.
- Marked the Menu Tests focus renderer's intentionally unused font parameter as unused.
- Routed slider rendering through the shared `menu_draw_slider` helper so it is no longer an unused static function.
- Added backend version `3.4.5` to the generated version definitions.
- Added the backend version to the DevTools Page tab while keeping the production/build version display unchanged.
- Added a template slot for the backend version in the DevTools Page inset.


## Menu widget geometry update

- Added template-owned icon, progress, value, and list widget kinds.
- Added rectangle and circle decoration primitives.
- Added `VITA_MENU_LINE` with multi-point/disconnected line syntax using `_` as a segment separator.
- Added line/shape examples to the DevTools templates without changing existing menu interaction behavior.
# 0.1.1 - 0.4.4.13

- Restored Back/Forward/Reload page operations through a dedicated, tabless Page Operations menu.
- Replaced the main menu Reload row with Page Operations while preserving its position and template-driven layout.
- DevTools informational tabs now share the same header/separator/content structure and carry optional inset scrollbar templates for future content growth.
- DevTools content scrolling is template-derived and remains hidden when content fits.

# Changelog

## Current - DevTools override/exit UI

- Expanded the DevTools Overrides viewport to show exactly five complete override entries at once.
- Kept the Overrides heading fixed while only the override rows scroll.
- Updated override scrolling/selection geometry to match the five-row viewport.
- Changed the DevTools Close tab from immediate shutdown to an in-menu confirmation screen.
- The confirmation displays the current page/application title and provides **No** and **Yes** actions.
- **No**, CIRCLE, or switching to another DevTools tab cancels the exit request.
- **Yes** shuts the application down through the same exit path used by START.
- Touch and D-pad navigation both support the confirmation controls.
- Production/build display remains `0.1.1 - 0.4.4.13`.

### Build script: -y / --yes
- Added `-y` / `--yes` to automatically answer yes to build-script confirmation prompts.

## Latest
- DevTools Button wait override now invokes the native Vita keyboard (IME) so arbitrary values such as `2s 3s` can be entered; preset values remain convenience shortcuts.
- The override is passed directly to the page input policy and is not restricted to hardcoded durations.

## Menu/UI update

- Main native menu redesigned around the CWebView concept layout: page identity card, favicon/title/URL metadata, large action rows, and Vita navigation footer.
- Added native menu row icons and active-row highlighting.
- Kept displayed version as `0.1.1 - 0.4.4.13`.
- Fixed nested text in buttons being laid out on top of one another; descendants now share the button focus target while occupying separate line boxes.

## 0.4.4.14

- Larger, correctly rendered CWV textfields.
- Select included explicitly in DPAD focus test.
- Native menu now shows production/build version metadata.

## 0.4.4.13

- Added page metadata to the native WebView menu.
- The main menu now displays the current document title from `<title>`.
- Added local PNG/JPEG/BMP favicon loading from `<link rel="icon">` / `<link rel="shortcut icon">`.
- Added `favicon.png` to the main test page.
- DevTools Page section now reports the actual page title.
- The current page URL remains visible in the menu; URL editing can be added later through the Vita IME without changing the metadata system.

### 0.4.4.9
- Fixed DPAD focus so descendants that inherit link/button hit-testing are not treated as independent focus targets.
- Added semantic `focusable_self` tracking for actual interactive elements.
- Added `disabled` and `tabindex` handling to the legacy focus model.
- DPAD focus order now follows browser-like positive-tabindex ordering, then implicit DOM order, with wrapping.
- Added `webapp/dpad_focus.html` to test nested interactive content, disabled controls, and tabindex ordering.

## 0.4.4.7

- Added optional source-backed WebKit C API include/library configuration.
- Added `CwvKit-external-build` for WebKit trees that expose the PlayStation CMake port.
- Added `build-webkit-vita.sh` with conservative source/port detection.
- Kept the legacy renderer as the usable fallback while WebCore/compositor integration is incomplete.
- Added `webkit/BUILD_PLAN.md` documenting the source/runtime boundary and build modes.

## 0.4.4.1 — WebKit host routing bring-up

- Connected the new `CwvKitEngine` ABI to `CwvHost` instead of leaving it as an unused parallel interface.
- `CWV_ENGINE=auto` now attempts the WebKit engine and safely falls back to the legacy renderer when the source-backed engine is not ready.
- `CWV_ENGINE=webkit` now fails explicitly rather than silently rendering with the legacy engine.
- WebKit update, draw, evaluation, pointer, focus, and VCM paths now have a dedicated host routing point.
- Added build-script options `--engine` and `--webkit-source`.
- Kept Sony 3.61–3.65 WebKit source out of the distribution; provide a local source tree when working on the source-backed platform port.

# 0.4.3.10

- Corrected vertical centering of native `<select>` labels by compensating for the Vita2D font baseline/glyph ascent.
- Documented the CSS compatibility boundary: the Vita2D diagnostic renderer is not Chromium and does not claim complete modern Chromium CSS parity.
- Kept CSS ownership separated so the eventual WebKit page backend can provide the actual page's CSS implementation while CWebView continues to own Vita-native controls.

## 0.4.3.9 — 2026-08-18

- Fixed the native `<select>` picker covering the closed control when the HTML
  select has no explicit CSS height.
- Kept the selected `<select>` label visible while its option picker is open.
- Added per-application persistent-data namespaces through `package.json`
  `vita.storageDirectory` (with `storageDirectory`/`dataDirectory` aliases).
- Native Webview settings now live under
  `ux0:data/CWebView/<application-data-directory>/settings.cfg` instead of
  one global `settings.cfg`.
- Defaults to the application Title ID when no storage directory is supplied.

## 0.4.3.8 — 2026-08-18

- Fixed touch interaction for native `<select>` controls: the first tap now
  opens the picker and leaves it open instead of immediately selecting the
  current option and closing.
- Touching an option now darkens it while the finger is over it and commits the
  option on release.
- Tapping outside an open picker dismisses it without changing the selection.
- Added transient touch-state tracking so scrolling/cancelled pointer gestures
  do not leave a select in a pressed state.
- Preserved D-pad/CROSS selection behavior and the existing selected/hovered
  option visuals.

## 0.4.32

- Added persistent VCM active state: activating `vcm="circle"` (or another VCM) marks that element as the current active control and clears the previous VCM.
- Added renderer support for CSS `:active` selectors on VCM elements, including tag, class, ID, and comma-separated selectors.
- `:active` styles are applied as a temporary render style, so changing VCM controls does not permanently mutate the base CSS.
- Kept `cwv-control-mode` as the `<html>`-level control policy; `vcm` remains the per-element Vita Control Method.
- Expanded the standalone VCM test page to verify `:active` styling on both links and buttons.

## 0.4.31

- Fixed `<select>` popup placement so it is anchored below the control, or above it when there is not enough room below.
- Prevented the closed `<select>` label from being rendered underneath its open option popup.
- Kept `<select>` option labels independently rendered and centered in the popup rows.
- Fixed the CSS `double` border rasterization so the two strokes have a real pixel gap on the Vita display instead of collapsing into one border.
- Preserved the existing 12px renderer inset and scrollbar geometry.

# 0.4.28

- Fixed native `<button>` default text so it remains black in both Light and Dark page themes; explicit CSS `color` still overrides the default.
- Split Settings physical navigation into two independent controls: D-pad browses the inset setting rows, while L Analog browses Undo/Save/Back.
- Added an accent-colored selection outline to the currently selected non-button Settings row.
- Settings D-pad selection now wraps through Scroll input, Zoom input, Zoom level, Theme, and Accent color without entering the action-button column.
- Settings action buttons can be selected with L Analog and activated with Cross, making Save accessible without moving the D-pad selection into the button column.
- Touch selection now updates the same logical selection state used by physical controls.

# 0.4.25

- Fixed physical zoom being reset every frame by runtime override application.
- Physical zoom now persists in the live settings value.
- Zoom now keeps the viewport center stable while changing scale.
- Added a scrollable, centered Settings content pane with a right-side scrollbar and touch dragging.
- Fixed Settings touch coordinates to use the same scrolled content coordinates as rendering.
- Native button default text now remains black in light mode instead of being overwritten by page-default theme text.
- D-pad page scrolling now explicitly clears the webpage focus ring so yellow focus borders do not remain while scrolling.

## 0.4.19

- Fixed L Analog zoom selection.
- Added horizontal scrolling for analog and D-pad control modes.
- D-pad control mode now scrolls instead of consuming Up/Down for focus cycling.
- Added native scrollbar-thumb touch dragging for both axes.
- Scrollbars are hidden when content fits the viewport.
- Added a small bottom breathing room only when content actually overflows.
- Hardened native menu CROSS handling so a held CROSS cannot repeatedly activate/close menus.
- Refresh button now uses U+27F3 (⟳).

## 0.4.15
- Added horizontal document scrolling and overflow detection.
- Zoomed pages can now scroll horizontally when content extends beyond the viewport.
- Added native WebKit-like vertical/horizontal scrollbar indicators when an axis overflows.
- Touch page dragging can scroll both axes independently of `cwv-control-mode`.

## 0.4.14

- Added native renderer zoom support.
- Default zoom input is R Analog.
- Default scrolling input is L Analog.
- Zoom is independent of `cwv-control-mode`.
- Zoom is clamped to 0.75x-2.0x and updates scrolling bounds.
- Added zoom state to lightweight DevTools performance information.

## v0.4.13 — Native menu combo and persistent settings fix

- Changed the native Webview menu shortcut to **L + R + SELECT**.
- Fixed persistent settings being loaded and then immediately erased by the native menu state initialization.
- Settings are now initialized before `settings_load()` and therefore remain active after startup.
- Made `settings.cfg` parsing tolerant of line order, CRLF/LF line endings, and whitespace around `=`.
- Added a runtime log entry showing the loaded `scroll`, `zoom`, and `dark` values to make configuration testing easier.


- Moved DevTools into the main native Webview menu instead of using a separate always-on overlay/combo.
- Added dedicated DevTools category buttons under the page information section: Performance, Renderer, Input, and Page.
- Added a separate centered DevTools submenu with buttons for each diagnostic category and a Back button.
- Added lightweight Performance diagnostics (frame counter and scroll/content measurements).
- Added Renderer diagnostics (host status, parser/renderer state, content height, and last error).
- Added Input diagnostics (configured scroll/zoom inputs, native touch priority, and D-pad focus behavior).
- Added Page diagnostics (current URL, title metadata status, and local multi-page navigation status).
- Removed the old persistent DevTools overlay and its L+R+SELECT toggle so diagnostics no longer occupy rendering space unless opened through the Webview menu.
- Kept DevTools passive: no background profiler, polling thread, page JavaScript injection, or continuously growing diagnostic buffers were added.
- DevTools remains native-shell UI and is not affected by the page's `cwv-control-mode`.

# 0.4.11 - Native shell menu touch support + lightweight DevTools

- Added a native centered browser-style shell menu opened with **L + Up + SELECT**.
- The shell menu is drawn above the page with a translucent dimming layer and is independent of the webpage's `cwv-control-mode`.
- Added rectangular native controls for Back, Forward, Refresh, Settings, and Close.
- Added a separate Settings view with Undo, Save, and Back controls plus scroll/zoom input and theme settings.
- Added front-touch hit testing for the native menu and settings view; menu touches are captured before webpage input.
- Added a small in-app navigation history for local page Back/Forward actions.
- Kept the existing lightweight `L + R + SELECT` DevTools overlay and expanded its diagnostics with scroll/content information.
- No per-frame DevTools polling or extra page JavaScript was added; the overlay is only drawn while visible.
- Settings are stored in `ux0:data/CWebView/settings.cfg`.

# 0.4.10 - 2026-08-17

### Renderer viewport origin
- Removed the unintended document-level top inset from the native renderer.
- Documents now begin at the top of the Vita viewport instead of receiving an
  extra renderer-added vertical margin.
- Kept the existing horizontal 24px Vita safe-area inset unchanged.
- Kept the intentional 16px **bottom** scroll inset from v0.4.7; this is only
  for reaching the end of a long page comfortably and is not applied at the top.

### Crash-log investigation
- Reviewed the supplied Vita3K `CWV.log`. The log does not contain a Vita
  exception/fault record or a native crash backtrace. The renderer initializes
  successfully and `CwvScroll` loads successfully; the log also contains
  long-running sessions and a clean `CwvScroll unload rc=0` shutdown.
- The only explicit renderer error in the supplied log is the existing test
  navigation request for `app0:/web/page2.html`, which does not exist in that
  test package.
- No scrolling-module failure is demonstrated by the supplied log, so no
  speculative changes were made to the scrolling implementation.


## 0.4.9
- Fixed CwvScroll stub generation when using the WSL build script.
- `build-vita.sh` now exports `$VITASDK/bin` before CMake runs so nested `vita_create_stubs()` Makefiles can locate `arm-vita-eabi-as` and the other VitaSDK binutils.
- Preserved the 0.4.8 renderer margin, scrolling, and focus changes.
# 0.4.8 - Horizontal margin field build fix

- Fixed a renderer build error introduced by the button-flow margin work: `CwvNode` now contains `margin_left` and `margin_right` alongside the existing vertical margins.
- Preserved the existing 8px default right margin for intrinsic `<button>` controls.
- Kept the 0.4.7 scrolling, bottom inset, and Tab/Shift+Tab focus behavior unchanged.


## 0.4.7

- Added a 16px document-bottom inset so the final element can be scrolled slightly above the viewport edge.
- Added native focus cycling through links and buttons in document order, intentionally ignoring `tabindex` for now.
- Added a visible focus outline and automatic scrolling to keep the focused element visible.
- Vita D-pad Down acts as Tab (forward) and D-pad Up acts as Shift+Tab (backward) for the native renderer.
- Added the corresponding `CwvInput.keyDown/keyUp` Tab events for future/full WebKit input handling.
- Fixed `CwvScroll.suprx` module start/stop return values for VitaSDK versions that do not define `SCE_KERNEL_START_SUCCESS` / `SCE_KERNEL_STOP_SUCCESS`.

# 0.4.6 - Touch / R-Analog page scrolling

- Added a dedicated `CwvScroll.suprx` user-mode extension for page-scrolling input policy.
- Added touch-drag scrolling with a small movement threshold so ordinary taps remain clicks.
- Added right-analog (`RAnalog`) scrolling with a deadzone and speed ramp.
- Added native renderer page offsets and content-height clamping, so content taller than the 960x544 viewport can be reached instead of being clipped permanently.
- Adjusted native hit testing to account for the current scroll offset.
- Added touch-drag click cancellation so dragging a button/link does not activate it on release.
- Added default horizontal separation between intrinsic `<button>` controls.
- Added VPK validation for `sce_module/CwvScroll.suprx`.

The scroll module is packaged as an application-local user module under `sce_module/`; it is not a taiHEN kernel plugin. VitaSDK exposes the user-module loader through `sceKernelLoadStartModule`, and its controller/touch APIs are available to the application/module environment.

# 0.4.5

- Fixed VPK packaging so the final archive is normalized to contain exactly one `sce_sys/param.sfo`.
- Added a post-pack VPK normalization helper for VitaSDK/vita-pack-vpk combinations that produce an archive without a usable SFO entry.
- Replaced text-based `unzip | grep` VPK validation with Python `zipfile` validation, including duplicate-SFO detection.
- Slightly increased native button horizontal padding to better match the desktop layout test.

## 0.4.3

- Fixed VPK packaging so `sce_sys/param.sfo` is explicitly added to the archive.
  This handles VitaSDK/vita-pack-vpk revisions where `-s` selects the SFO but does not
  create the archive entry.
- Kept the explicit `eboot.bin` packaging and all webapp/ES6/LiveArea resources.
- Cleaned up native renderer warning-producing one-line `if` statements and selector
  buffer sizing.

# 0.4.2 - Text baseline / clean rebuild reliability

- Raised normal text baselines slightly within their line boxes so large headings are visually centered instead of sitting against the lower edge of colored backgrounds.
- The WSL build script and Makefile now use CMake `--clean-first` for VPK builds, preventing stale native renderer object files from surviving source/archive timestamp changes.
- No webapp HTML files were changed in this release; the existing index, links, layout, and button test pages are preserved.

# 0.4.0 - Text element / em spacing correction

- Restored browser-like default heading font sizes, including `<h1>` at 2em.
- Restored browser-like vertical spacing for paragraphs and headings using em-relative values.
- Fixed text fragments so their parent element margins are not duplicated.
- Block elements now retain a layout box even when they have no background/border, allowing text-element spacing to participate correctly in flow.
- Added `em`/`rem` handling for supported margin/padding/font-size declarations.
- Fixed child positioning so text-element spacing is not collapsed away by the parent layout pass.

# 0.3.9 - Vertical flow / line-box correction

- Fixed collapsed first-child margins being removed from the parent flow without moving the painted parent box, which caused the visible strip above headings such as `<h1>` inside `<header>`.
- Fixed collapsed last-child margins being subtracted from the parent content height, which made block backgrounds too short and pulled following elements upward into the previous element.
- Preserved collapsed margins in the vertical flow so consecutive block elements retain browser-like separation instead of becoming vertically scrunched together.
- Matched the layout line-height metric to the text baseline metric used by the Vita2D drawing path, preventing glyphs from extending into the next element's line/background.
- Adjusted button baseline placement to use the button's actual content box, removing the extra lower line-box area visible beneath controls.

# 0.3.6 - Browser-like default spacing and control line-box fix


- Fixed the package metadata webapp-directory flow: `webappDirectory` is now treated as the configured webapp root and no longer gets followed by the unconditional `Webapp directory [...]` prompt. Relative `webappDirectory` values are resolved relative to `package.json`.
- Added a small browser-like user-agent spacing model for `<p>` and heading elements (`h1`-`h6`) instead of treating all non-button elements as zero-margin blocks.
- Added a default top document inset corresponding to the browser-style body margin while retaining the Vita renderer's horizontal safe-area inset.
- Kept element margins separate from padding so painted backgrounds do not incorrectly consume margin space.
- Fixed parent background sizing so first/last child margins only collapse when the parent has no padding/border on that side; padded containers therefore retain their child margins inside the painted box.
- Tightened button text baseline placement against the button's measured box to remove the visible excess lower line-height while preserving intrinsic sizing.
- Kept the 0.3.5 inline button flow, intrinsic-width sizing, pressed state, and navigation behavior.

## 0.3.5 - Button inline-flow/rendering fix

- Fixed inline `<button>`/`<a><button>` layout using the parent formatting position instead of the child text node's final cursor position.
- Prevented button children from moving the surrounding inline cursor during intrinsic-size measurement.
- Fixed multiple inline buttons appearing separated, offset, or overlapping when they share a line.
- Kept buttons intrinsic-width by default while preserving full-width behavior for explicit `display:block`.
- Centered button labels inside the button's own box rather than against the page.
- Fixed explicit `display:inline` buttons to retain intrinsic sizing and button interaction.
- Preserved the existing pressed/darkened button state and link/navigation behavior.

## 0.3.3 — Renderer inline-flow / box-height correction

- Tightened the native line-box calculation so `<div>`/`<header>` backgrounds no longer inherit the old oversized 36px-per-font-unit line box.
- Kept block elements full-width by default while preserving intrinsic sizing for `<button>`/other inline-block controls.
- Button labels are centered inside the intrinsic button content box instead of being positioned relative to the page.
- `text-align:center` on a block container now centers its direct text against the container width.
- Inline buttons continue to share the same formatting line and only wrap when their intrinsic widths no longer fit.
- Explicit `display:block`, `display:inline`, `display:inline-block`, and `display:none` continue to override the supported element defaults.
- Preserved button pressed/darkened state, black default button text, nested `<a>` navigation, and native-browser targets.

## 0.3.2

- Added the resolved **Webapp directory** to the package.json metadata preview.
- When package metadata is accepted, the directory containing the selected JSON file is now used as the webapp directory by default.
- Supports an optional `webappDirectory` JSON field to explicitly override that directory.
- This removes the need to repeatedly answer the interactive `Webapp directory [...]` prompt when using the normal `webapp/package.json` layout.

## 0.3.1 — Inline button flow and CSS display

- Changed the default `<button>` layout to participate in the current inline formatting line instead of forcing a new line after every button.
- Multiple intrinsic-width buttons now remain on the same line until they no longer fit, at which point they wrap to the next line.
- Preserved the working button pressed/darkened state and black default text color.
- Explicit `style="display:block"` on a button now overrides the button's default inline-block display.
- Added a general display-mode path for `block`, `inline`, `inline-block`, and `none`, with element defaults matching common browser behavior for the currently supported elements.
- Text nodes now participate in their parent's inline formatting context instead of being treated as block-sized nodes, preventing their line-height from becoming artificial container height.
- Block containers continue to use their descendant content extent for background height rather than adding a synthetic trailing line-height.
- Added layout-test coverage for multiple same-line buttons and explicit display overrides.

## 0.3.0b — Block sizing and button defaults

- Removed synthetic vertical space from block `<div>`, `<header>`, and other block containers.
- Block backgrounds now end at the actual content/padding/border extent.
- Kept block elements full-width by default while keeping inline-block controls intrinsic-width.
- Separated button label layout from block text layout so button labels do not force page width.
- Added explicit-vs-inherited color tracking so buttons default to black text without overriding an explicitly supplied CSS `color`.
- Preserved existing link, multi-page, `cwv-target`, native-browser, and button interaction behavior.

## 0.3.0 — Renderer intrinsic sizing / box-height correction (2026-08-17)

- Rechecked the native layout model against the known-good 0.1.0 renderer baseline.
- Separated block-container geometry from inline/inline-block control geometry.
- Styled `<div>`, `<header>`, and other block containers now derive their background height from the actual bottom edge of rendered descendants instead of treating the container's line-height as additional content.
- Restored the 0.1.0-style small visual surround for block backgrounds without adding that surround to document flow.
- `<button>` remains an `inline-block` control and now has a dedicated intrinsic-width layout path rather than sharing block sizing behavior.
- Button width is calculated from its rendered label plus padding/border when no CSS width is supplied.
- Explicit button `width` remains respected.
- Default button text color is now black, matching the expected native/default HTML appearance.
- Button layout advances the document cursor correctly instead of being treated like an inline text fragment.
- Nested button content is constrained to the button's content box while the button itself remains shrink-to-fit.
- Preserved the existing link, nested-anchor, native-browser target, pressed-state, CSS color, heading-size, and multi-page navigation work.

Validation:
- Renderer source passes a local `gcc -std=c99 -Wall -Wextra -fsyntax-only` check using Vita2D/PSP2 API stubs.
- A full VitaSDK/VPK build was not performed in this environment because the VitaSDK toolchain is not installed here.

# CWebView Changelog

## 0.4.3.5 — 2026-08-18
### Native `<select>` picker and touch/DPAD polish
- Fixed native `<select>` label alignment: the closed control now centers its text horizontally using the actual Vita font metrics and vertically centers the baseline inside the control.
- Fixed selected-label persistence so choosing an option always updates the closed `<select>` text instead of leaving it blank.
- Added a small native disclosure arrow to the closed `<select>` control.
- Reworked the native option picker so touch movement tracks a separate hovered row and commits the hovered option on release.
- Added a subtle darkening highlight for the hovered/focused option in both light and dark themes; DPAD navigation uses the same hover state.
- Kept the selected option distinct from the hover state so the current selection remains readable while another option is being previewed.
- Fixed duplicate touch forwarding in the main Vita input loop, which could cause controls such as `<select>` to toggle twice in one touch sample.
- Updated the forms test page to explicitly exercise `text-align:center` on `<select>`.

This changelog records the major changes made during the CWebView development sessions. It is intentionally cumulative so future project ZIPs can update this same file instead of losing the history.

## 0.2.7 — 2026-08-17
### Renderer rewrite from the 0.1.0 baseline
- Rebuilt the native HTML layout/parser path around the known-good 0.1.0 rendering model instead of layering more fixes onto the later compatibility implementation.
- Kept depth-aware recursive HTML parsing so nested tags are parsed as elements rather than becoming literal text.
- Restored per-element heading defaults (`h1`–`h6`) while still inheriting color/font-family and other inheritable properties from parents.
- Restored reliable drawable text-node creation for direct text and nested inline elements.
- Styled containers (`div`, `header`, `main`, `section`, etc.) now create a background/border node before their children, allowing the container styling to remain visible behind nested content.
- Reworked block/inline cursor advancement and text wrapping to prevent nested elements from consuming or replacing one another's layout.
- Kept the existing CSS selector engine, inline/external CSS loading, image loading, custom-font loading, URL resolution, and LiveArea/build infrastructure.
- The renderer now has one authoritative recursive HTML path rather than a legacy linear parser plus incremental compatibility patches.

## 0.2.4 — 2026-08-16/17
### Renderer compatibility fix
- Reworked the native renderer using the known-good 0.1.0 rendering behavior as the compatibility baseline.
- Restored reliable creation/rendering of visible HTML elements inside nested containers.
- Fixed the regression where nested `<div>`, `<header>`, `<main>`, `<p>`, and heading elements could disappear instead of becoming drawable nodes.
- Preserved the newer recursive HTML parsing infrastructure while restoring the older node-to-render path where necessary.
- Preserved inline CSS, selector matching, image handling, custom-font infrastructure, and the newer resource-loading work.
- Continued work on heading sizing and styled container backgrounds.

## 0.2.3 — 2026-08-16/17
### HTML/CSS renderer regression investigation
- Compared the newer recursive renderer against earlier working renderer versions.
- Fixed heading-style inheritance so `<h1>`–`<h6>` do not have their default heading sizes overwritten by parent font sizes.
- Improved styled container handling so backgrounds can be represented as drawable boxes behind child content.
- Continued CSS/layout compatibility work without changing the application-level wrapper API.

## 0.2.2 — 2026-08-16/17
### Renderer, CSS and development infrastructure
- Added the native renderer's development/diagnostic overlay infrastructure for future devtools-style functionality.
- Expanded CSS selector and style handling.
- Added/continued support for borders, backgrounds, inherited styles, and nested HTML nodes.
- Continued image/font/resource handling improvements.

## 0.2.1 — 2026-08-16/17
### HTML parsing and LiveArea packaging
- Improved HTML nesting and recursive parsing.
- Fixed parsing/rendering of nested elements and text content.
- Added LiveArea packaging options for application icon, wallpaper, and startup logo.
- Added default icon, wallpaper, and startup-logo artwork when no custom paths are supplied.
- Added metadata/configuration support for selecting these assets by path.
- Added support for selecting the webpage entry point from configuration/build arguments.

## 0.2.0 — 2026-08-16
### Native HTML renderer milestone
- Introduced the native HTML diagnostic renderer behind the `CwvHost` backend boundary.
- Added basic HTML text and block layout.
- Added inline styles.
- Added simple tag/class/id CSS selector matching.
- Added external CSS resource loading.
- Added image resource loading.
- Added basic font loading and `@font-face` support.
- Added relative URL resolution from the webpage entry point.
- Added the ES6 module host component and staged `CwvESM.suprx` into the VPK.
- Added renderer/backend diagnostic stages and runtime probing infrastructure.

## 0.1.x — initial wrapper / renderer work
### Working renderer baseline
- Established the original working native rendering path used by the 0.1.0 backup.
- Added the basic webpage wrapper lifecycle and native drawing backend.
- Added the initial HTML/text rendering behavior that later renderer versions use as a compatibility reference.

## Build and packaging fixes
These changes span multiple versions rather than belonging to a single renderer milestone.

### VitaSDK / linker fixes
- Fixed Vita build/link configuration for FreeType's BZip2 dependency.
- Fixed curl's Zstandard dependency by explicitly accounting for the required libraries in the link configuration.
- Kept the `CwvESM` target building and packaging independently from the main `CWebView` target.

### VPK installation / LiveArea fixes
- Added strict validation and normalization of Vita LiveArea PNG assets.
- Added ImageMagick-based image normalization to the build process.
- Normalized icon, wallpaper, and startup-logo dimensions to Vita-compatible sizes.
- Added VPK integrity checks after packaging.
- Added checks for required VPK entries including `eboot.bin`, `param.sfo`, LiveArea assets, the webpage entry point, and `CwvESM.suprx`.
- Addressed the VitaShell installation failure (`0x8010113D`) caused by incompatible LiveArea image packaging.

### Build-script configuration
- Added command-line options for:
  - `--icon`
  - `--wallpaper`
  - `--start-logo`
  - webpage entry-point selection
- Added metadata aliases for the same settings in `package.json`.
- Added automatic fallback to built-in default artwork when no custom asset is supplied.
- Added path resolution so assets can be supplied relative to the project/webapp configuration.
- Added build-time diagnostics showing the selected application metadata and assets.
- Added ImageMagick detection with a clear build error when it is unavailable.

## Current known work / not yet complete
- Devtools-style menu triggered by a Vita button combination still needs to be tested and refined.
- Full browser-style CSS layout is not implemented yet (for example flexbox/grid/transforms).
- JavaScript integration is still incomplete.
- The renderer still needs broader HTML/CSS compatibility testing beyond the current diagnostic page.
- The real WebKit backend remains a separate longer-term integration path; the current native backend is intentionally a lightweight renderer rather than a full browser engine.


## v0.28 - Link & Multi-Page Navigation (2026-08-17)

- Added native `<a href>` hit detection to the 0.1.0-based renderer.
- Added `cwv-target="app"` for local CWebView navigation (default for anchors).
- Added `cwv-target="browser"` / `cwv-target="external"` recognition for external/native-browser targets.
- Added relative local URL resolution, including simple `.` / `..` path handling.
- Added support for navigating between separately packaged HTML files.
- Added link metadata to rendered nodes so an `<a>` can contain a styled `<div>` and the visible div acts as the clickable region.
- Added a simple link underline indicator where a link has no background.
- Added `webapp/links.html` and `webapp/nested/second.html` as a multi-page navigation test.
- Added a link test entry to the default `index.html`.
- The native browser target is currently detected and reported, but the actual Vita Browser launch remains a follow-up integration step.

## v0.28.1 — Link Build Fix

- Fixed the v0.28 link implementation's renderer structure mismatch.
- Added link state directly to `CwvNode` (`is_link`, `href`, `link_target`).
- Added pending navigation state to `CwvRenderer`.
- Propagated anchor link state into nested elements and text nodes, including `<div>` inside `<a>`.
- Corrected `CwvBackendOps` initialization to include pointer-event and navigation callbacks in the proper order.
- Removed the unused local `slash` variable from `resolve_local_href()`.

## 0.2.9 — 2026-08-17
### Renderer layout model / intrinsic controls
- Reworked the renderer layout pass around HTML's default display categories instead of treating every drawable element as a block.
- Added explicit internal display modes: `block`, `inline`, and `inline-block`.
- Restored block-container sizing behavior from the 0.1.0 baseline: a styled `<div>` now derives its height from its actual child content without adding an extra trailing line-height.
- Removed the renderer's unconditional post-block spacing from container height calculations.
- Kept normal block elements full-width by default, matching browser-style block formatting.
- Changed `<button>` to `inline-block` behavior with intrinsic/shrink-to-fit width when no CSS width is specified.
- Added Vita2D text-width measurement for intrinsic control sizing instead of estimating button width from the page width.
- Added vertical button padding and border-aware content placement.
- Added button grouping so pressing text, a nested element, or the button's background updates the same visual control; this fixes the previous case where a child text node received the pressed state while the background node remained unchanged.
- Preserved `<a>` descendant propagation, so links containing `<div>` or `<button>` remain clickable.
- Kept explicit CSS `width`, `height`, `display`, margin, and padding overrides.
- Added a syntax-only renderer validation using Vita2D API stubs; the modified renderer passes `-std=c99 -Wall -Wextra` syntax checking.

## 0.2.8.7 - Anchor descendant link propagation
- Fixed `<a>` link state not propagating to descendant elements such as `<div>`.
- Anchor href/target now propagate through nested drawable nodes for hit-testing.
- Removed the incorrect link-style block that referenced the element's style string as though it were a node.
- Standalone `<a>` text continues to render through the normal text-node path.
- No CSS/font/layout behavior was changed in this update.

Build note: VITASDK was not available in the current build environment, so the Vita VPK could not be rebuilt here. The source tree is ready to build with the existing VitaSDK setup.

## 0.2.8 — 2026-08-17
### Links, native browser target and buttons
- Fixed the native-browser link path so `cwv-target="browser"` / `cwv-target="external"` uses Vita's AppUtil web-browser launcher instead of only reporting that a browser target was requested.
- Added AppUtil initialization support to the host.
- Added `<button>` rendering with a native-style default background, border, padding, and text treatment.
- Added button pressed-state hit testing and a darker pressed appearance while the touchscreen is held down.
- Button descendants inherit the button interaction state, allowing normal nested button content to be pressed as one control.
- Changed touchscreen pointer dispatch so native controls receive a true press event and a release event, allowing pressed visual feedback without changing the existing JavaScript input bridge.

## 0.3.7 renderer layout fix
- Resolve first/last child vertical margin collapse for block backgrounds.
- Start block children after their parent's padding/border instead of painting content into the padding area.
- Tighten renderer line boxes to reduce artificial blank space beneath text and controls.
- Keep button hit testing limited to the actual rendered control rectangle.

### 0.4.16
- Added CSS `overflow`, `overflow-x`, and `overflow-y` handling for the document viewport.
- `hidden` suppresses scrolling/scrollbars for that axis; `visible` suppresses the native viewport scrollbar; `auto` remains the default; `scroll` forces a scrollbar even when there is no overflow.
- Added optional `vita.launchPage` package metadata for a custom HTML launcher/default page that can link to multiple app entry pages.
- Added launch-page validation to the WSL build script and CMake configuration.

## 0.4.21

- Added native accent-color use to the Settings zoom slider and related controls.
- Added intrinsic `<select>`/`<option>` behavior with touch selection and D-pad/CROSS interaction.
- Added lightweight native option picker rendering without changing webpage layout.
- Fixed CSS `width`/`height` parsing to honor `px`, `em`, and `rem` values instead of only numeric pixels.
- Added renderer focus cycling for buttons, links, selects, inputs, and textareas.
- Kept the built-in empty-`launchPage` launcher behavior and documented it.

## 0.4.22
- Added `cwv-control-mode="Browser|Standalone|Touch"` to `<html>`.
- Kept missing/unknown mode as `None` for backwards compatibility.
- Added `vcm="..."` explicit physical-control bindings for Standalone mode.
- Browser mode now provides browser-style Cross/Circle/Square/D-pad controls plus Triangle refresh and L/R back/forward.
- Touch mode suppresses page physical controls while preserving touchscreen input and the native L+R+SELECT menu shortcut.

## 0.4.23 - DevTools, theme defaults, and runtime overrides

- Added DevTools Performance, Renderer, Input, Page, Overrides, Console, and Web Platform sections.
- Added lightweight rolling FPS and frame-time diagnostics while preserving the cumulative frame counter.
- Added temporary DevTools overrides for VCM, scroll input, zoom input, theme, and scrollbar visibility.
- Added a clear-overrides action via TRIANGLE.
- Added native light/dark default page colors: explicit webpage `color`/`background-color` declarations continue to win.
- Added runtime scrollbar visibility control for DevTools overrides.
- Kept accent colors confined to CWebView native UI.

## 0.4.26
- Main Webview Menu now exposes a single `DevTools` button in the upper-right corner instead of individual DevTools category buttons.
- DevTools tab strip now has its own scrollable viewport and native scrollbar.
- DevTools tab scrollbar supports touch dragging and keyboard/D-pad selection keeps the selected tab visible.
- DevTools menu state resets its tab scroll position when entering/leaving the menu.

## 0.4.29 viewport inset
- Restored the small browser-like page inset around all four sides of the rendered document.
- The inset is renderer chrome rather than CSS, so pages without body margins still get the consistent 12 px breathing room.
- Scrollbars remain on the physical screen edge and are not shifted inward by the page inset.
- Overflow calculations preserve the bottom/right inset only when content actually exceeds the viewport, avoiding spurious scrollbars on short pages.

## 0.4.30
- Restored the browser viewport inset to exactly 12px on every side.
- Removed the accidental extra right/bottom document inset caused by scrollbar geometry.
- Kept scrollbar chrome on the physical right/bottom screen edges without consuming another 12px of page inset.
- Made the native Webview menu a true accent-derived theme using darkened accent modifiers for panels, headers, buttons, borders, and scrollbar tracks.
- Centered closed `<select>` labels inside their controls.
- Added a subtle alternate hover/highlight color for unselected `<select>` options in the native option picker.
- Made dark-mode default page backgrounds black when no page background is explicitly supplied.

## 0.4.4.0 — WebKit migration foundation

- Added a CwvKit engine ABI isolated from WebKit private classes.
- Added a Sony 3.61–3.65 WebKit source staging location and import helper.
- Added explicit `CWV_ENGINE=legacy|webkit|auto` build selection.
- Kept the hand-written renderer as the safe default during the multi-build migration.
- Added a WebKit migration test page covering CWV attributes, forms, modern CSS, and module-script syntax.
- Added documentation describing the planned JavaScriptCore/WebCore/compositor/input migration stages.

## 0.4.4.2
- Continued WebKit migration with a generated source-configuration boundary.
- Added `webkit/tools/configure-vita-webkit.sh` to inspect a local Sony 3.61-3.65 WebKit source tree without bundling Sony source.
- Added a source-free generated configuration fallback so the normal Vita build remains reproducible.
- Added a WebKit modern-feature migration test page covering modules, grid, custom properties, `clamp()`, `:has()`, and VCM attributes.
- Added a CMake `CwvKit-source-info` target for diagnosing the selected WebKit source tree.

## 0.4.4.3

- Added the first real JavaScriptCore C-API bridge under `webkit/jsc/`.
- Added an independent JSC initialization/evaluation milestone before WebCore.
- Kept weak C-API references so legacy builds remain linkable until JSC is linked.
- Added `CwvKit-jsc-info` build diagnostics and a JSC smoke-test script.


## 0.4.4.5

- Added optional source-backed JavaScriptCore include/library configuration.
- Added `VITA_WEBKIT_JSC_INCLUDE_DIR` and `VITA_WEBKIT_JSC_LIBRARY` CMake cache variables so a locally built JSC can be linked without modifying CWebView source.
- Added `CwvKit-jsc-config-info` for diagnosing the JSC link configuration.
- Kept the legacy renderer available so existing webpages remain testable while WebKit is brought up incrementally.
- No Sony WebKit binaries or firmware components are bundled.

## 0.4.4.7
- Added the WebCore/DOM host boundary as a separate layer from JavaScriptCore.
- Added WebKit source-tree validation for WTF, JavaScriptCore, and WebCore.
- Added a WebKit source manifest generator to make the large source import measurable before compiling.
- Added a real DOM-operation migration test page using querySelector, querySelectorAll, textContent, and addEventListener.
- CMake now detects the presence of WebCore/JSC/WTF and exports those capabilities to the Vita target.
- Legacy rendering remains the usable fallback until the source-backed WebCore compositor is linked.

### 0.4.4.8
- Added a dedicated JavaScriptCore-first Vita cross-build helper.
- Added `CwvKit-jsc-source-build` CMake target.
- Added an explicit ARM Vita CMake toolchain generated from `VITASDK`.
- The WebKit build now stops at JSC before attempting WebCore, making the first Vita-specific compiler failure actionable.

## 0.4.4.11
- Added the CWV `<textfield>` element for legacy-renderer Vita keyboard input.
- `<textfield>` opens the official Vita IME dialog on touch or DPAD/CROSS activation.
- Supports `value`, `placeholder`, `type="password"`, and `multiline` attributes.
- Text is returned to the page control after pressing Enter; Cancel leaves the previous value unchanged.
- Added a dedicated textfield test page.
- Loaded `SCE_SYSMODULE_IME` and linked `SceIme_stub`.

## 0.4.4.13 - DevTools navigation and expandable overrides

- Removed the large DevTools action row from the main Webview menu; DevTools is now opened only from the compact button in the upper-right corner.
- Made DevTools tabs compact and text-sized so the tab strip wastes less horizontal space.
- Added release-to-repress behavior to native menu navigation consistently across D-pad, face buttons, triggers, analog menu navigation, and touch activation.
- Added a DevTools runtime override for `cwv-button-wait`, with Default (Page-defined), `release`, `none`, `1s`, and `1.5s 0.5s` modes.
- Added a temporary zoom-level override to DevTools.
- Added a scrollbar to the DevTools Overrides panel so additional runtime overrides can be added without redesigning the panel.

### Menu template refactor
- Settings inset tab content now uses named template elements for its panel, title, separator, rows, and status area.
- DevTools inset tab content now uses named template elements for its panel, title, separator, override rows, scrollbar, and close confirmation controls.
- Touch hit testing and override scrolling now resolve the same template geometry used for rendering.

### Template architecture: per-tab inset menus
- Split Settings into dedicated `vita_menu_settings_appearance_template`, `vita_menu_settings_controls_template`, and `vita_menu_settings_preferences_template` constants.
- Split DevTools into dedicated Performance, Renderer, Input, Page, Overrides, Console, Platform, and Close template constants.
- Rendering and touch hit-testing now select the same active per-tab template, so each inset tab can be restyled independently without changing sibling tabs.
- Overrides scrolling derives its geometry from the active Overrides template.

### Menu template architecture cleanup
- Every native menu and inset tab now owns a complete `VitaMenuTemplate`, including a `window` frame element.
- Rendering and touch hit-testing use the same active template/frame origin.
- Added `native_menu_open()` so menu systems can be swapped without duplicating state initialization.
- Added `CWV_PAGE_OPERATIONS_MENU_MODE` as a build-time menu-system override; setting it to `MENU_DEVTOOLS` makes the Page Operations entry launch the complete DevTools system.
- Kept each Settings and DevTools tab as an independent template constant.

### Menu polish
- Standardized all DevTools inset content rows with the Overrides card treatment: inset panel, accent edge, and separator.
- Kept each DevTools tab's content in its own template so layouts remain independently editable.
- Added a dedicated bottom-left `Back` control to Page Operations; page-history Back remains a separate operation.

### Menu widgets / decorations
- Added a template-driven `VITA_MENU_SLIDER` example to DevTools Performance (`Pitch (UI demo)`) with three stopping points: Low, Normal, High.
- The sample slider supports touch selection and D-pad LEFT/RIGHT adjustment.
- Added `VITA_MENU_DECORATION` template elements for lightweight accent/marker graphics.
- Template scroll extent calculation ignores decorative and slider-only elements so visual ornaments do not unexpectedly change scrolling.

### Menu widgets expansion
- Added generic `VITA_MENU_CHECKBOX`, `VITA_MENU_SEGMENTED`, and `VITA_MENU_BADGE` template primitives.
- Added a Performance-tab frame-pacing checkbox example with touch and CROSS activation.
- Added a Renderer-tab three-way Render Quality segmented control with touch and D-pad LEFT/RIGHT/CROSS interaction.
- Added a Renderer-tab status badge example.
- Kept widget geometry template-owned so new menu authors can add controls without hard-coding their layout in the renderer.

### Menu Tests / navigation cleanup
- Restored the `Menu Tests` entry as the fourth main-menu action and added a template-owned bottom-left `Back` button to the main menu.
- Added a template-owned `Back` button to every Menu Tests tab.
- Added per-tab D-pad focus maps so only actual test objects plus Back become D-pad stops; L/R changes Menu Tests tabs.
- Added CROSS activation for the interactive Menu Tests controls and LEFT/RIGHT adjustment for the pitch, quality, and list examples.
- Fixed the Menu Tests list widget so multiple entries render horizontally inside its template bounds instead of overflowing vertically into the footer.
- Removed demonstration-only pitch/checkbox controls and renderer decoration widgets from DevTools; those experiments now live exclusively under Menu Tests.
- Moved native menu object/layout definitions into the CWMenu declarative script system. Buttons, controls, progress/value widgets, decorations, shapes, lines, and scrollbars are now defined by `cwmenu/default.cwm` rather than compiled C layout arrays.
- Added per-application `cwmenu/default.cwm` override support.
- Packaged CWMenu scripts into the VPK under `cwmenu/`.


## 0.4.4.5 — CWMenu action contracts

- Added declarative `action` properties to CWMenu menu objects.
- Settings controls now dispatch through CWMenu actions for theme, accent, zoom, scroll input, zoom input, scroll speed, cache clearing, and preference reset.
- Menu Tests interactive controls consume their CWMenu action instead of hard-coded object-name checks.
- Existing behavior and the 0.4.4.5 feature set are preserved.

## 0.4.4.5 — Menu Tests reactive combinations

- Added live Menu Tests coverage for reactive CWMenu combinations involving pitch, pitch-stop count, brightness, and accent tint.
- Added a dynamic pitch preview whose stop markers follow the configurable 2–6 pitch-stop slider.
- Added a second advanced Menu Tests page for the remaining three-value and all-value combinations.
- Added independent CWMenu templates for every new combination; no new menu content is hardcoded into the renderer.
- Added CWMenu focus/input handling for variable-backed sliders so LEFT/RIGHT, CROSS, touch, and L/R navigation remain consistent.

## 2026-10-07 (84) - Region-first scroll dispatch, title-ID data root, Files menu

### Chrome-style scroll dispatch (84)
User report: focusing/typing inside a scrollable element scrolled the
PAGE immediately while the element's own container stayed put. New shared
dispatch (cwv_backend.c): the selected element's own region absorbs the
delta first, then each ancestor region in turn, and only the leftover
reaches the page - the region scrolls fully before the page moves.
Two new host entry points (cwv_host_scroll_at / cwv_host_scroll_x_at)
route a touch drag to the region under the finger's screen point (with a
graceful fallback to the old by-delta path for any backend without the
new op), so drag direction follows what is under the fingertip instead
of always paging the document. The clip-seam math for zoomed rendering
now subtracts the node's region scroll offsets before scaling, so
region-scrolled content can no longer drift outside its box when zoomed.

### Vita data root = TITLE ID (84)
User report (his build): settings and CWV.log lived under the
TITLE-derived "CWebView" folder, so every CWebView-based app shared one
ux0:data/CWebView/<ns>/ root (and collided with non-CWebView apps).
The app data root is now the app's own TITLE ID: ux0:data/<TITLE_ID>/
settings.cfg flat by default; a custom package.json storageDirectory
stays a namespace segment under the title-ID root. The runtime log,
cache-clear and boot diagnostic use the same builder. Settings saved
under the pre-84 root still load exactly once (one-time migration read,
logged as "settings: migrated <old> -> <new>"); the next save writes
the new path.

### Files menu (84)
New native MENU_FILES on Vita: a filesystem tree browser for the
packaged web content. A "Files" button is drawn natively to the LEFT of
the DevTools button (a script-level `files` element takes precedence for
its rect; the fallback works on legacy v3 CWMenu scripts, which is what
the user's build ships). The menu lists app0:web recursively (dirs
first then case-insensitive alpha, per-directory sort; sizes shown;
512-entry and depth-8 caps; scan logged to CWV.log). Input: d-pad
up/down, L/R page jump, CROSS on an .html/.htm row navigates through
the normal Open URL path (history + load + page shown), O/back returns,
touch tap selects and tap-on-selected opens, finger drag scrolls with a
scrollbar. Feature key `vita.files:false` compiles the whole feature out
(menu mode, button, touch/dpad branches, CWMenu default, main-template
case), mirroring the cursor/zoom/scroll/devtools/history key plumbing;
documented in docs/MANIFEST_KEYS.md. default.cwm gains `files on` and a
`files` button element; the C-side native draw keeps the button present
on legacy scripts.

### Stubs (84)
desktop_test/stubs/psp2/io/dirent.h now mirrors the real VitaSDK header
(d_stat is a full SceIoStat instead of an opaque 48-byte pad), so
st_mode/st_size reads compile in the desktop syntax gate like on the
real SDK.

### Gates (84)
sources/vita/main.c passes a -Wall -Wextra syntax check against the
SDK-mirroring stubs with CWV_FILES_ENABLED both on and forced to 0.
Full suite: all 26 desktop gates, the golden reference check
(6815 lines, identical), and the ES2015 unit binary are green.
VitaSDK is still absent from this sandbox: rerun ./build.sh vita -y on
the real installation (VPK link/packaging/boot, Files menu smoke test,
and the settings migration) before calling (84) verified on hardware.

### Handoff (84)
HANDOFF.md refreshed: new "Project terminology" section (CWV, ports,
CwvESM, CWMenu/CWM, CWI + growth guard, gates vs golden harness,
webapp, feature keys, title-ID root), a "Current standing" section for
2026-10-07, and an explicit "Soon to come" endgame list for the next
session if the final hardware verification slips.

## 2026-10-07 (85) - Data layout correction: <data>/CWebView/$TITLEID, release plan

### The path, corrected (85)
User correction to (84): the app data root should be
<data>/CWebView/$TITLEID, not the flat ux0:data/<TITLE_ID>/ root.
Settings, CWV.log and the cache now live at
ux0:data/CWebView/<TITLE_ID>/ again (a custom package.json
storageDirectory replaces the title-ID segment, which is what keeps
two CWebView-based apps from colliding - the real (84) complaint).
app_data_prepare mkdirs the chain ux0:data/CWebView/<ns>/. Settings
saved by a (84) build under the flat title-id root still load once
(one-time read migration, logged) and the next save writes the
corrected root; installs upgrading from (83) or earlier keep their
files exactly where they are, because that layout already was
<data>/CWebView/<ns>. docs/MANIFEST_KEYS.md storageDirectory row and
the HANDOFF terminology/standing sections are updated to match.

### Release plan on record (85)
RELEASE DATE: October 31st (2026); the app must be polished with
minimal bugs by then. Recorded in HANDOFF.md as the standing quality
bar: hardware truth pass first (VitaSDK/VPK, Files menu smoke test,
Wii U Cemu, Wii boot.dol), Piskel Sharp end-to-end second, soak + triage
of the newest changes (scroll regions, Files menu) third, optional
polish last, and WASM/AssemblyScript/TypeScript/WASI stay post-release.

### Gates (85)
sources/vita/main.c -Wall -Wextra syntax gate against the SDK-mirroring
stubs passes; full desktop suite re-run green. VitaSDK still absent
from this sandbox - rerun ./build.sh vita -y on the real installation
and confirm the corrected data path on hardware.

## 2026-10-23 — Nested horizontal scrolling and Vita pointer release

- Horizontal wheel/drag scrolling inside an explicitly horizontally scrollable nested region no longer chains leftover movement to the page when the region reaches either edge. Vertical scroll chaining is unchanged.
- Vita rear-touchpad pointer now starts visibly at the screen center when enabled and remains visible after the finger lifts, making the cursor's location discoverable between taps. Turning the pointer setting off still hides it; CSS `cursor:none` remains respected by the cursor renderer.
- Vita CROSS release now clears transient pressed and `:active` visuals, including stale VCM-active styling, in standalone mode as well as browser/focused-control paths.
- Archive packaging intentionally omits the user-maintained `webapp/` tree and image/font payloads under `assets/`; non-media asset metadata/license files remain eligible for inclusion.
- Validation: shared, main production/debug, Wii, Wii U, Vita keyboard/mouse, cursor, CWMenu, and error-audit functional gates passed. The optional AddressSanitizer run exceeded the environment timeout; no Vita hardware test was performed.


## 2026-10-23 — menu cleanup and platform diagnostics

- Removed the CWM main-menu More row and removed the bundled `extras.cwm` linked-menu demo.
- Adjusted the Wii U Wiimote IR cursor mapping to account for normalized KPAD coordinates instead of collapsing movement near the top-left corner. Hardware verification is still required.
- Standardized the Wii U log filename to `CWV.log` beside the app/package root, matching the Wii and Windows convention. Vita's per-title log path remains `ux0:data/CWebView/<title-id>/CWV.log`.
- The CWM Files button is still only fully backed by the Vita native file browser; cross-platform Wii U/Windows/Wii browsing remains outstanding.

## Unreleased — file-template groundwork
- Added a shared, case-insensitive filename-to-MIME registry for HTML, XHTML,
  text/source formats, images, audio, video, PDF, and common binary formats.
- Added MIME-kind classification so the future CWMenu Files browser can choose
  a file template by media type instead of treating every non-HTML file as inert.
- Unknown extensions safely map to `application/octet-stream`; MIME lookup is
  extension metadata, not content validation or a security boundary.


## 2026-10-23 — Cross-platform CWMenu Files browser

- Implemented a shared Files view for the CWM Files action on Windows, Wii, and Wii U. It lists entries under `<app-dir>/web/`, navigates subdirectories, and opens HTML/XHTML files. The parent action never permits navigation above `web/`; the platform listing callbacks reject traversal-like relative paths.
- Added keyboard/pad selection for the Files button in the main menu, directory navigation, and percent-encoding for spaces and URL-sensitive filename characters.
- Windows enumerates entries with Win32 `FindFirstFileA`; Wii and Wii U use their platform C library's `opendir`/`readdir`/`stat` support. The shared menu owns the view, selection, path policy, and URL construction.
- MIME registry remains metadata only: non-HTML/XHTML files are recognized but not opened because image/audio/video/text/PDF/binary templates were explicitly left out of scope.
- Validation: shared, main production/debug, Wii, and Wii U syntax gates passed; MIME test passed. Windows cross-compiler is not installed, so the Windows port gate was skipped. No native SDK build or physical-console test was performed.


## 2026-10-26 — Configurable Windows architectures

- Windows cross-build targets now come from `webapp/package.json` via `windowsArchitectures` or `windows.architectures`.
- Added x86 and ARM32 compiler targets alongside x64 and ARM64, with per-target compiler overrides and clearer missing-toolchain handling.
- Kept the default architecture list as x64 + ARM64 for compatibility.
- Documented host-vs-target compiler selection and architecture configuration.

## Unreleased — input and packaging fixes

- Windows no longer dispatches pointer-release events for ordinary mouse movement, preventing hover from activating links or controls. Left-button-down now reaches the shared renderer so `:active`/pressed button styling can appear during the press.
- Windows packages now include the runtime `cwmenu/*.cwm` scripts beside the executable, fixing missing CWMenu script assets in staged builds.
- Explicit `display: block` buttons use the normal block-width layout instead of shrinking to their label, keeping the label centered within the control.
- Wii U diagonal D-pad hat events now dispatch vertical and horizontal scroll independently.

### CSS property behavior audit (2026-10-29)

- Implemented parsed background-image layer settings in the shared painter: repeat mode, position, sizing, fixed attachment, origin, and clip box.
- Split `background-origin` from `background-clip`; corrected single-value top/bottom background-position parsing.
- Corrected viewport-relative percentage handling for inset offsets and applied explicit approximate percentage resolution to margin/padding and selected box dimensions. CSS `box-sizing` now defaults to `content-box`, with `border-box` as an explicit opt-in.
- Added right/end text alignment and distinct `white-space: pre-wrap` / `pre-line` behavior.
- Added `docs/CSS_AUDIT.md` covering every registered property and spelling out the renderer's unsupported/incomplete CSS subset.
