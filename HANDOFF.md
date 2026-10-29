## 2026-10-27 — Canvas 2D portability and scroll test

- Added `rect()`, `save()`, and `restore()` to the shared Canvas 2D context. `save()`/`restore()` cover the currently supported `fillStyle`, `strokeStyle`, `lineWidth`, and `globalAlpha` properties; resizing the canvas resets the state stack. Both manifest fixture layouts test filled rectangle paths, state restoration, and resize-reset behavior.
- Extended `docs/canvas_vita_smoke.html` in place with a second canvas below a tall spacer. Its animated bar tests redraw and bitmap persistence while scrolling and when partially clipped by the viewport. The page now applies to all CWV platform targets despite retaining its historical filename.
- Verified RGBA texture upload functions already exist for Vita2D, Wii U SDL, Wii GX/GRRLIB, and Windows' software texture path. No separate per-platform Canvas rasterizers were needed.
- Updated `docs/CANVAS_VITA_TEST.md` and `docs/SUPPORTED_METHODS.md` with supported APIs, current limitations, setup, platform notes, and scrolling-test instructions.
- Validation: `desktop_test/build_canvas2d_test.sh`, `desktop_test/build_shared_gate.sh`, `desktop_test/build_main_gate.sh`, and `desktop_test/build_paths_check.sh` passed. Native console builds and real-hardware scroll/render validation remain pending.

## 2026-10-23 — PS Vita Canvas smoke-test handoff

- Added `docs/canvas_vita_smoke.html`, a visible hardware test for the current experimental Canvas 2D subset (`getContext("2d")`, `fillStyle`, `strokeStyle`, `lineWidth`, `globalAlpha`, `fillRect`, `strokeRect`, `clearRect`, and repeated context identity).
- Added `docs/CANVAS_VITA_TEST.md` with setup and expected results. Copy the smoke page to `web/canvas-vita-test.html` and merge `"cwv-args": "beta-enable-canvas"` into `web/package.json`; do not overwrite existing manifest metadata.
- On Vita, check `ux0:data/CWebView/<title-id>/CWV.log` for `cwv-args: beta-enable-canvas enabled`. A visible blank canvas despite that log line suggests the draw/texture/render path; a missing line points first to manifest placement/content or package-root resolution.
- Canvas fixture test, argument-parser test, shared-source gate, and Vita keyboard/mouse gate passed. The full gate suite was started but did not complete within the environment limit; `check_reference.sh` failed due to existing golden-reference drift in the `BLOCK BUTTON` x-position (expected 19.00, actual 7.00), and the run later timed out during `build_loghook_test.sh`. This is unrelated to the Canvas smoke page.
- No VitaSDK compiler was available in the current environment, so no native Vita package was produced. Physical Vita visual/performance testing remains the next required validation.

## Latest Canvas 2D increment (2026-10-24)

- Added `beginPath`, `moveTo`, `lineTo`, `closePath`, `fill`, and `stroke`, together with `quadraticCurveTo`, `bezierCurveTo`, and `arc`. The current implementation stores at most 1,024 points per canvas, approximates curves/arcs with line segments, uses even-odd scanline filling, and clips path strokes before rasterization.
- Updated both manifest-layout Canvas fixtures to exercise a filled triangle, quadratic curve, Bézier curve, arc outline, and an extreme-coordinate clipped line. The tests also assert that the arc outline leaves its center transparent.
- Updated `docs/canvas_vita_smoke.html` and `docs/CANVAS_VITA_TEST.md` to exercise the new APIs on real hardware.
- Validation: Canvas fixture test passes for both manifest layouts; ASan/UBSan Canvas runs pass; shared-source, production/debug main, Vita keyboard/mouse, Wii, Wii U, position-edge, JS, path, and diff gates pass. Windows cross-build was skipped because MinGW is not installed. Native Vita SDK compilation and physical Vita validation of path/curve rendering remain pending.
- Known limitations: curve and arc smoothness is approximate; path fills currently use the even-odd rule; full Canvas 2D semantics, joins/caps, transforms, gradients, text, image drawing, and WebGL are not implemented by this increment.
## Latest HTML/CSS parser audit (2026-10-23)

- The HTML scanner now finds tag endings outside quoted attribute values. An apostrophe inside an unquoted attribute value no longer starts a quote state.
- Matching-close scans skip HTML comments and treat `script`, `style`, `title`, and `textarea` as raw-text payloads. This fixes false nesting when a comment or script string contains text resembling an opening tag; raw-text containers also use a cheaper closing-delimiter search rather than parsing every `<` inside the payload.
- CSS matching now has a quick tag-name rejection path before selector copying/tokenization, reducing work when most rules do not apply to a node.
- Added parser regression coverage in `desktop_test/testdata/error_audit/parser_edges.html`; `build_error_audit.sh` passes normally and under ASan/UBSan. Full platform gates are still being run; no console hardware benchmark has been performed.

## Latest user test feedback (2026-10-22)

The Vita `CWV.log` showed no `cwv-args: beta-enable-canvas enabled` message, and
the Canvas smoke page therefore reported that HTML5 Canvas was unsupported. The
runtime previously searched only `<app-root>/package.json`; console content is
usually under `<app-root>/web/`, where the project manifest may live. It now
checks both locations and reports missing/invalid manifest configuration.

The Files picker previously recursively flattened directories and defaulted to
`app0:` when `CWV_WEBPAGE` was `index.html`. It now opens at `app0:/web`, lists
only the current directory, supports folder entry and `..` navigation without
escaping the web root, and visually distinguishes directories with `[DIR]` plus
an outset-style selected outline. Desktop regression tests cover both manifest
locations. Main syntax, shared, Wii, and Wii U gates pass; hardware confirmation
of the Canvas smoke page is still needed.

## Latest Wii symptoms (2026-10-21, diagnostic revision 90)

User reports the Wii builds but webpages do not render, CWMenu opens without
information, no log is found, and the cursor art looks squashed/pixelated. The
Wii app now writes one `CWV.log` in the folder that
contains `boot.dol`. It logs font load status, CWMenu script load status, and
initial page load URL/result. Wii gate and build-script syntax pass locally, but
these symptoms are not yet verified fixed on hardware. After testing, inspect
`CWV.log` first: font/CWMenu failures point to package paths; page-load failure
includes the host error; a successful load with a blank screen points to draw,
layout, or asset decoding. Cursor filtering/scaling remains to be investigated
separately after confirming the actual loaded asset dimensions and display path.

## Latest Wii U rendering/input work (2026-10-22)

The user reports horizontal text overflow specifically inside `<button>` elements
on Wii U. The SDL_ttf text-width functions returned physical framebuffer pixels
while shared layout expected virtual-canvas x coordinates. Both the system-font
and `@font-face` width seams now convert measured widths back through
`WIIU_FIT_X`; glyph rendering itself remains at its intended physical pixel size.

Transient `:active` styling is now tracked for pointer/touch targets and ancestors.
Focused controls use `cwv_host_focus_release()` to clear the pressed/darkened state
on release across Vita, Wii, Wii U, and Windows input paths. The Wii USB keyboard
API exposes press-only events, so Enter is released immediately after activation.
VCM's intentionally persistent active state is kept separate from transient
button-darkening.

Cursor path review: Wii U Wiimote IR position is scaled from 1280x720 to the
960x544 virtual canvas; SDL mouse/GamePad pointer coordinates are converted from
854x480 through the inverse fit before hit-testing. No cursor-coordinate change
was justified by source inspection. Wii U/Wii gates, shared-source gate, and main
gate passed. Cursor/CWMenu/theme/error-audit functional checks passed; the full
script's optional ASan phase timed out. No full Wii U SDK build or hardware test
was performed, and the Windows cross-compiler is unavailable in this environment.

# CWebView handoff (2026-10-07)

New session? Start here. You are continuing an in-progress C/C++
homebrew project: a Chrome-parity HTML/CSS/JS engine for retro
consoles. Read this, then CHANGELOG.md (reverse chronological).

## Latest build issue (2026-10-21, revision 89)

The Wii U link failed with undefined `cwv_css_viewport_width` and
`cwv_css_viewport_height` symbols. The build script used only each source
basename for object filenames, allowing same-named C sources to overwrite the
same `.o` file. The Wii U builder now preserves source-relative paths in object
filenames. `bash -n sources/build-wiiu.sh` passes; a full Wii U SDK build remains
to be verified on the user's machine.

## Layout

- `sources/shared/sources/` - ALL console-neutral engine code, organized by
  responsibility. See `sources/shared/sources/README.md`.
  - `core/` - backend/renderer-facing code, host, platform seam, image
    codecs, cursor and project configuration.
  - `web/html/` - HTML parser/tag registry and layout implementation.
  - `web/css/` - CSS parser/property registry.
  - `web/js/modern/` - the single CwvESM ES2015+ transform and ES edition
    library sources.
  - `web/js/runtime/` - JavaScript glue plus vendored Duktape 2.7.0.
  - `web/typescript/` - TypeScript preprocessing/stripping.
  - `menu/` - CWMenu parser/native menu UI; `menu/scripts/` contains
    declarative `.cwm` files.
  - `cwv_version.h` - shared version fallback; the Vita CMake-generated
    header still takes precedence for the Vita build.
- `sources/vita/` - VitaSDK/vita2d/sceIo/sceHttp/sceIme platform code,
  `main.c`, Vita keyboard/photo/boot helpers, and the separate
  `CwvScroll.suprx` module. CwvESM is no longer a Vita runtime module.
- `sources/wiiu/` - SDL2/wut platform + `main.c` + the two C++ units:
  `cwv_ime_swkbd.cpp` (native nn:swkbd keyboard applet) and
  `cwv_fetch_nets2.cpp` (nn::ac + nets2-socket HTTP fetch, no TLS).
- `sources/wii/` - libogc/GRRLIB platform + `main.c` (earlier stage).
- `sources/win/` - Win32/GDI + WIC + WinHTTP platform + `main.c`.
- `desktop_test/` - golden harness + regression gates; stubs mirror the
  REAL console headers (invented names break real builds).
- `sources/build-vita.sh`, `sources/build-wii.sh`, `sources/build-wiiu.sh`,
  `sources/build-windows.sh` - standalone platform build scripts.
- `webapp/` or `test_pages/` = web content, staged by the builders.

## Rules (user-set, standing)

- Native renderer is the permanent engine. ES2015+ handling lives in
  ONE module (CwvESM; on Wii U linked into the rpx). No ES7+ unless
  asked. LF line endings for new files; CRLF must stay supported.
- Parsing/reusable logic goes in sources/shared; drawing + console
  specifics stay per-console. Prefer DevkitPro NATIVE libs (user
  steered nn:swkbd and nn::ac/nets2 over portlibs/hand-rolled).
- NATIVE SYSTEM APPLETS FIRST (user-set 2026-09-20 (13)): if wut /
  VitaSDK exposes a system applet for a capability, use it - never
  hand-draw a worse replica. On Wii U: text input = nn::swkbd (done);
  error dialogs for the future CWMenu shell = nn::erreula; open_browser
  stays -1 on purpose (SYSLaunchTitle passes no URL and exits the app).
  When adding a new wut API call, mirror its REAL surface in the
  desktop stubs (see desktop_test/stubs/nn/swkbd/) so a guessed API
  fails at build_wiiu_gate.sh, not mid-devkitPPC-build.  Read the actual
  wut headers for the pinned version before writing against them.  This
  ALSO covers socket option constants: wut's nsysnet surface has NO
  SO_RCVTIMEO/SO_SNDTIMEO (desktop glibc defines them, so the gate
  cannot catch it) - socket timeouts on Wii U are select()-based (see
  sock_wait() in sources/wiiu/cwv_fetch_nets2.cpp, fix of (14)).

## Verification and Git workflow

Run the golden renderer check and every desktop gate before shipping:

```sh
cd desktop_test
bash check_reference.sh
for gate in build_main_gate.sh build_js_test.sh build_es2015_test.sh \
    build_cwmenu_unit.sh build_loghook_test.sh build_wiiu_gate.sh \
    build_wii_gate.sh build_paths_check.sh build_shared_gate.sh \
    build_approot_test.sh build_cursor_test.sh build_img_cache_test.sh \
    build_menu_hotkey_test.sh build_menu_ui_test.sh build_theme_test.sh \
    build_es6_portability_gate.sh; do
    bash "$gate" || exit 1
done
bash es2015_unit/build_unit.sh && ./es2015_unit/unit
```

These are 18 build/check scripts, plus execution of the transformer unit
binary. Do not update the golden reference to conceal unexpected drift.
Desktop console gates use SDK stubs; they are not console builds.

The project folder is the Git repository root. Required order:
changes, green gates, dated CHANGELOG entry, then commit. Preserve the
repository structure and return a full ZIP, not a changed-files archive.
Generated outputs are ignored where covered by .gitignore.

## ES6/ES7+ transformer state (2026-09-26)

sources/shared/sources/web/js/modern/cwv_es2015_core.c is the shared modern-JS handling pipeline
(let/const, arrow bodies, template literals, classes, destructuring,
defaults, rest/spread via __vw_aread/__vw_spread in the ESM prelude).
The existing spread/destructuring regression coverage includes: object/array spread (`{a: 1, ...rest}` -> `Object.assign`),
paren assignment destructure `({a} = obj)`, `for (var [k, v] of
arr)` -> indexed loop + destructure, and pattern params `function
p({x, y})` -> `function p(__vw_pN) { var x = __vw_pN["x"]; ... }`.
The es7_selftest.html gate page (es2015_test.c loads it through the REAL
pipeline) is the regression net for these - it caught two bugs the
one-liner harness missed: (1) a braceless arrow body starting with "["
kept its raw spread because output_expr_position refused an empty
recursive output (try_array_spread now accepts start-of-output "[");
(2) complex ARROW param lists were rebuilt without the "function"
keyword (now binserted AFTER the truncate - order shifts positions).
Debugging lesson (hit TWICE): never insert probe statements on their
own line inside one-line `if (...) return 0;` bodies - the return
escapes the if and the dead-code fallout looks like engine bugs
(GCC deleted whole functions).  Probe by REPLACING the return in
place, and destructure_emit's object branch rejects any dupn'd
pattern that ends at a NUL (`*end == 0` guard) - callers must give
it a pattern with a non-NUL char past the closing brace.

## Class and Vita portability follow-up (2026-09-30)

Class execution now covers constructor/method parameter copying, ordinary
methods named `get`/`set`, paired accessors, member comments, empty/nonempty
`super()` and `super.method()` arguments, base fields before constructor
body, derived fields after a direct top-level `super()`, static inheritance,
and basic private-name execution. Class declaration replacements now include
a terminator, so same-line following statements remain valid. The previous
abort was an unprotected
Duktape test-driver evaluation of an undefined `print`, not a transformer
crash. Temporary probes are not part of the shipped sources.

`test_pages/es_class_selftest.html`, mirrored in `desktop_test/testdata/`,
contains 22 assertions checked by the real load/transform/execute/DOM gate.
The existing ES6 and ES7 pages contain 23 and 21 assertions respectively.
The class gate checks the exact summary, its success color, all PASS rows,
and the absence of FAIL rows. A deliberate assertion failure was verified
as a failing gate, then the original page was restored.

Important limits: private names currently map to ordinary `__name` properties,
not enforced privacy or brand checks. Inheritance collisions remain possible.
This is not complete ECMAScript class conformance. Arbitrary conditional/nested
super calls, super accessor receiver semantics, and static super dispatch are
not certified by this change. Piskel Sharp still requires actual console testing.

The Vita-reported `strdup` error is fixed without feature macros or warning
suppression: the local heap-copy helper returns mutable owned `char *`, and
call-spread uses it instead of POSIX `strdup`. Removed genuinely unused locals,
marked the unused optional-chain hook, and clarified comment-scan indentation.
`desktop_test/build_es6_portability_gate.sh` checks C99 and C11 with
`-Wall -Wextra -Werror`. Run it alongside every gate above and actually run
`desktop_test/es2015_unit/unit` after its build script.

VitaSDK is unavailable in this sandbox. A clean strict shared-source compile
and desktop SDK-stub gates do not prove a VPK links, packages, or runs. Re-run
`./build-vita.sh -y` on the user's VitaSDK installation to verify the full build.

## Planned modules: WASM, AssemblyScript, TypeScript and WASI

All four are planned future work, not implemented or verified in this update.

- WASM: evaluate an interpreter and a shared host seam with per-console
  integration. wasm3 and WAMR are candidates, not selected dependencies;
  memory usage and SDK compatibility need measurement.
- AssemblyScript: planned through its compiled WebAssembly output, dependent
  on the WASM runtime and any required host imports.
- TypeScript: planned tooling/integration for compiling TypeScript to JavaScript.
  Feed the output to the modern-JS handling pipeline; do not assume that
  merely targeting ES5 makes every TypeScript application supported.
- WASI: planned host-function integration for WASM modules, with an explicit
  supported API subset, filesystem permissions and per-console platform seams.
- Node.js and PHP runtime ports are not part of the current plan.

## Console SDK build follow-up (2026-10-20, 88)

- Wii: the reported FreeType link failure was missing bzip2/Brotli decoder
  symbols. `build-wii.sh` now requests `ppc-bzip2`/`ppc-brotli` in its optional
  dependency setup and links `-lbz2 -lbrotlidec -lbrotlicommon` after FreeType.
  Confirm those exact library names exist in the installed devkitPro prefix.
- Wii U: `build-wiiu.sh` now includes `sources/shared/sources/core` for both
  compile checks and the full build, so shared CSS can resolve
  `cwv_renderer.h` after the source-tree reorganization.
- These target-specific links cannot be fully verified without the actual
  devkitPro Wii/Wii U toolchains. Re-run both platform builds on the SDK host.

## State and remaining validation

- Vita: the user reported a real build failure in the shared transformer.
  The source fixes and strict host checks are included here. VitaSDK is absent
  in this sandbox, so rerun `./build-vita.sh -y` on the actual installation.
  VPK linking, packaging and console behavior remain unverified here.
- Wii U: the existing engine/app-loop, font, native keyboard, nets2 fetch,
  logging and CWMenu integration are retained. Validate rendering, touch,
  button mappings, keyboard coexistence and persisted settings in Cemu or on
  hardware. Initial-text keyboard prefill remains pending. Host gates do not
  establish successful current RPX/WUHB builds or console runtime behavior.
- Wii: existing platform/app-loop, IR cursor and CWMenu coverage is retained.
  Software keyboard, HTTP fetch, boot.dol packaging and real SDK/hardware
  validation remain pending as recorded in the previous handoff.
- JavaScript: class tests are execution coverage, not full language conformance.
  Enforced private identity, complete super semantics and actual Piskel Sharp
  execution on consoles remain open. Future modules are listed above.
- Builds: retain progressive percentage output across platforms. Treat new
  failures from the user's actual SDK builds as priority fixes.

## Piskel-Sharp crash follow-up (2026-09-30)

See [Vita3K/Piskel-Sharp triage](docs/VITA3K_PISKEL_TRIAGE.md). Confirmed image
input defects are hardened with six decoder-rejection regressions, but the
reported Vita3K host write violation is not reproduced or diagnosed to a
faulting function. Request the exact dev build/image, CWV.log and native stack.
The language-separated current inventory is [Supported methods](docs/SUPPORTED_METHODS.md).
## Logging, images, Windows port (2026-10-01)

- Vita printf does not support %zu (it printed "zu" and shifted the later
  arguments, so IMG size/magic diagnostics were garbage). All log lines now
  cast sizes to unsigned long. Audit all format strings reaching any log.
- Logs are now APP-SPECIFIC: Vita writes ux0:data/CWebView/<namespace>/CWV.log
  (namespace = package.json storageDirectory, default the Title ID). Windows
  writes <exe folder>/CWV.log. Wii U already logged next to the app; Wii keeps
  its app-folder log.
- Debug/dev builds (CWV_DEBUG=1) DISABLE file logging on Vita, Wii and Wii U;
  production builds keep the forensic log. If you want the opposite gating,
  flip the one #if in each port's log function.
- Images on ALL platforms: BMP was silently unsupported on Wii (GRRLIB sniffs
  only PNG/JPEG). Added shared, unit-gated decoder sources/shared/sources/core/cwv_bmp.c
  (24/32bpp uncompressed, both row orders). Matrix: docs/PORT_ROADMAP.md.
- Windows port first cut: sources/win (Win32/GDI + WIC + WinHTTP), builds
  real AMD64 + ARM64 .exe from the same sources (sources/build-windows.sh), gated by
  desktop_test/build_win_gate.sh. Built-in ES6 transformer wired like Wii/WiiU.
  Desktop gate runner: desktop_test/run_all_gates.sh (20 gates).
- Port roadmap (Android, Switch, Unix, Linux): docs/PORT_ROADMAP.md.

## Project terminology (2026-10-07)

Shared vocabulary so new sessions do not re-derive it:

- **CWebView / CWV** - the project: a Chrome-parity HTML/CSS/JS engine for
  retro consoles (Wii, Wii U, Vita, Windows first cut), hand-written
  renderer, Duktape 2.7.0 for JS. Prefix `cwv_` marks engine code.
- **Port** - one console target: `sources/vita`, `sources/wii`,
  `sources/wiiu`, `sources/win`. Platform code only; ALL engine logic
  lives in `sources/shared` (enforced by build_shared_gate.sh).
- **CwvESM** - the ONE modern-JS handling module (ES2015+ classes,
  destructuring, spread via the transform pipeline), now integrated into
  the shared source tree at `sources/shared/sources/web/js/modern/` and
  linked directly into every port. No ES7+ beyond it unless the user asks.
  The former `CwvESM.suprx` provider module and `cwv_esm_api.h` seam are gone.
- **CWMenu (.cwm)** - the native menu DSL: script-defined menus
  (`default.cwm`, `extras.cwm`) parsed by cwmenu_api.c into
  VitaMenuTemplate arrays. Script version matters: legacy v3 scripts run
  with "built-in defaults for newer features"; the host draws some
  elements (e.g. DevTools, Files buttons) natively on top of whatever
  the script provides, so features work on legacy scripts too.
- **CWI (CWebView Image)** - the pre-decoded image package format
  (cwv_cwi.c): pixels plus a lossless ORIG chunk. Packed at build time by
  tools/cwi_pack_stage.sh under the shared optimization stage, with a
  growth guard (CWI_MAX_GROWTH, default 1.5: a CWI larger than the
  original ships as the smaller PNG; 0 = pack unconditionally).
- **Gates** - desktop_test/build_*_gate.sh scripts (~26 total, run by
  run_all_gates.sh). Every one must pass before shipping. Console gates
  compile against SDK STUBS that mirror real SDK headers exactly -
  invented names break real builds, so stubs copy real signatures.
- **Golden harness** - check_reference.sh renders fixture pages and
  compares to a stored reference, line by line. Never update the golden
  reference to conceal drift.
- **webapp** - the user project's web content directory, staged and
  packaged per console; `webapp/` asset overrides beat repo defaults.
- **Feature keys** - package.json per-console booleans (cursor, zoom,
  scroll, devtools, history, files) that compile features out per port;
  `cwv_project.h` holds the define defaults and pushes CWMenu defaults.
- **Title ID / VITA_TITLEID** - the Vita app's product ID (e.g.
  CWEB00042); since (85) app data lives at `<data>/CWebView/$TITLEID`
  (ux0:data/CWebView/<TITLE_ID>/settings.cfg + CWV.log; a custom
  package.json storageDirectory replaces the title-ID segment).

## Current standing (2026-10-08, after (87))

- Four platforms build from one root dispatcher `./build.sh` (vita, wii,
  wiiu, windows, all, interactive menu, `init` scaffolder). Sources live
  in sources/build-*.sh. Build output is progressive-percentage on all
  ports, and every port runs the shared optimization stage (minify ->
  CWI packing -> savings line) as listed steps (83).
- Change (87) fixes the ES2015 unit include path, the Wii U SDL desktop stub,
  and absolute/fixed `right`/`bottom` placement. `desktop_test/build_position_edges_test.sh`
  covers right-only, bottom-only, and fixed right+bottom offsets. The canonical
  default launcher template now lives at `sources/shared/assets/default_launcher.html`;
  Vita build references it from there. Automatic launcher generation is still
  only wired for Vita; Wii/Wii U/Windows currently keep their configured entry
  page, so making the generated launcher the default on those ports remains a
  separate staging/build integration task.
- The previous handoff recorded all 26 desktop gates + golden harness +
  ES2015 unit GREEN before this source-only reorganization, including the
  strict shared portability pass and Vita main syntax checks. For (86), the
  targeted recursive shared-source gate, Vita CMake wiring gate and
  build-path gate are GREEN. The full desktop runner was not completed in
  this sandbox session because its total runtime exceeded the execution
  window; do not treat this session as a fresh all-gates certification.
- Today's work:
  - (74 follow-up) per-element scroll regions: the focused element's
    region scrolls fully before leftover delta reaches the page; touch
    drags scroll the region under the finger; clip-seam math fixed so
    scrolled content cannot drift out of its box under zoom.
  - (84->85) Vita app data root: (84) first moved settings + CWV.log to
    a flat ux0:data/<TITLE_ID>/ root; the user corrected the layout to
    `<data>/CWebView/$TITLEID`, so (85) restored the CWebView parent with
    the TITLE ID as the per-app segment (a custom storageDirectory still
    replaces it). One-time read migration pulls settings forward from
    the (84) flat root; apps coming straight from (83) keep their files
    in place because that layout already was ux0:data/CWebView/<ns>/.
  - (84) Files menu (MENU_FILES): web-content tree browser on Vita.
    Button drawn natively LEFT of DevTools (works on legacy v3 scripts),
    lists app0:web recursively (dirs first, sizes shown, 512-entry cap),
    d-pad/touch/scrollbar input, CROSS/tap opens .html/.htm through the
    normal Open URL path. Feature key `vita.files:false` compiles it out
    everywhere (main menu button, mode, CWMenu default).
- Known-open from earlier sections still stands: VitaSDK is NOT in this
  sandbox, so no VPK link/packaging/run here; Wii/WiiU/Wii hardware and
  Cemu validation remain pending; Piskel Sharp needs real console runs.

## Release plan (2026-10-07)

RELEASE DATE: October 31st (2026). The app ships then and must be
POLISHED with MINIMAL BUGS by that date. That is the standing quality
bar for everything below - new features must land early enough to be
tested, and the final stretch is bug-fixing and polish, not expansion.

From 2026-10-07 that leaves 24 days. Priority order to the release:
1. Hardware/toolchain truth pass (VitaSDK + real VPK boots, Files menu
   smoke test, settings/log path verification, Wii U Cemu pass, Wii
   boot.dol pass) - unverified-on-hardware is the biggest open risk.
2. Piskel Sharp end-to-end on console - the flagship proof of the
   engine + ES6 pipeline; any blocker it hits is a release blocker.
3. Bug triage over polish: decoder/log regressions are locked, scroll
   regions and Files menu are new - soak them in real use first.
4. Only then optional polish (Wii U keyboard prefill, CWMenu extras);
   WASM/AS/TS/WASI stay POST-release - do not pull them in.
Ship rule stays: all gates green -> dated CHANGELOG entry -> commit ->
full ZIP (with .git history).

## Soon to come (if the final items slip)

If a future session picks this up after an interrupted endgame, the
likely remaining work is exactly this list, in order:

1. Verify on the real toolchain: rerun `./build.sh vita -y` on David's
   VitaSDK installation (the (84) changes: title-id data root, legacy
   settings migration, Files menu) and confirm the VPK links, packages,
   boots; check CWV.log lands in ux0:data/CWebView/<TITLE_ID>/.
2. On-console smoke test of the Files menu: button visible left of
   DevTools, tree lists the packaged webapp, CROSS opens a page, touch
   drag scrolls, `vita.files:false` removes it.
3. Settings migration check: an install that saved settings under the
   (84) flat ux0:data/<TITLE_ID>/ root should still load them once and
   save to ux0:data/CWebView/<TITLE_ID>/ on next change (installs coming
   from (83) or earlier are already in place - no migration expected).
4. CHANGELOG + commit discipline: every change ships with a dated
   numbered entry (next number is 87) and a full-zip return (ZIP includes
   .git history; exclude build/ and .vitasdk/).
5. Then the bigger roadmap already on file: Piskel Sharp end-to-end on
   console, enforced private names + full super semantics in classes,
   WASM / AssemblyScript / TypeScript / WASI modules (planned, not
   started - see "Planned modules" above).


## 2026-10-23 — START button ownership

- Removed the Vita main-loop START-to-quit behavior and the debug-build START overlay toggle so applications can use START themselves. The debug overlay remains accessible through its other existing controls.
- Added START press/release forwarding to `window.CwvInput` on Vita, Wii, and Wii U page input paths. Vita button-wait state now has a dedicated START slot.
- Wii U START remains available for the existing optional L+R+START CWMenu chord if configured; the default shortcut remains L+R+SELECT. When a configured chord is consumed to open CWMenu, it cannot simultaneously be delivered as page input.
- The normal platform/homebrew exit mechanism is not replaced. Boot-failure diagnostic screens may still use START to dismiss/exit because no page is running there.
- Validation: source-level checks and build gates pending in this task; real-console button delivery has not been verified.

## 2026-10-23 — Horizontal nested scroll and Vita cursor/active-state follow-up

- Nested horizontal scrollers consume horizontal movement at their own edges instead of bubbling leftover delta to page scrolling. Vertical nested scrolling retains its existing chaining behavior.
- Vita rear-touchpad pointer remains visible between contacts when enabled; it initializes centered after settings are loaded and hides when the pointer option is disabled. Verify coordinate orientation and art on a real Vita.
- CROSS release clears button-pressed, pointer-active, and VCM active visuals so `:active` does not remain darkened after release. Standalone mode now invokes the same focus-release path as browser mode.
- Return archives should omit `webapp/` entirely and exclude image/font files from `assets/`; preserve source, metadata, and user-owned untracked files. Do not alter the root README.
- Gates passed: shared, main production/debug, Wii, Wii U, Vita keyboard/mouse, cursor, CWMenu, and functional error audit. ASAN variant timed out in this environment. Hardware verification remains outstanding.


## 2026-10-23 — Canvas 2D audit and upload path

- Canvas 2D `fillRect` / `clearRect` previously converted raw floating-point coordinates to `int` before clipping. NaN, infinity, or very large finite values could trigger undefined conversion behavior. Shared clipping now validates inputs and clamps double-precision bounds before conversion.
- Repeated `getContext("2d")` calls now return the same per-canvas context object from the JS global stash, preserving context identity and state. Assigning `width` or `height` resets the bitmap and resets `fillStyle` / `globalAlpha`, even if the assigned dimension is unchanged.
- Removed the expensive temporary BMP creation/decode path on every dirty canvas upload. Shared Canvas rasterization calls `cwv_tex_load_rgba_buffer`; each platform owns the final upload: Vita's aligned RGBA texture, Wii's tiled GX RGBA8 format, Wii U's SDL RGBA texture, and Windows' software texture buffer. Draw source dimensions use the logical canvas size so Vita alignment padding is not stretched into the visible image.
- Canvas tests cover both supported manifest locations, repeated context identity, persistent fill style, alpha blending, clipping of extreme coordinates, and safe rejection of invalid numeric values. AddressSanitizer + UndefinedBehaviorSanitizer Canvas fixtures pass.
- Main production/debug, shared, Wii, Wii U, Vita KBM, cursor, CWMenu, and position-edge gates pass. Windows cross-compiler is unavailable; no console SDK build or real-hardware performance/visual test was possible. WebGL remains explicitly unsupported and must not report a working context until an implementation exists.
- Root README remains unchanged. Package archives continue to omit the user-owned `webapp/` payload and media files from root `assets/`.


## Wii U follow-up (2026-10-23)

Latest working tree includes a ProcUI lifecycle correction for Aroma/Tiramisu title launches: `WHBProcInit()` is called before SDL startup, the main loop checks `WHBProcIsRunning()`, and `WHBProcShutdown()` runs on exit and handled initialization failures. `sources/build-wiiu.sh` explicitly links `-lwhb` and checks for `libwhb.a`.

Wii U scrolling now has SDL finger-event gesture support (tap remains a click; drag dispatches vertical/horizontal deltas at the pointer to nested scroll regions) and D-pad hat input scrolls when D-pad mode is selected. The shared DevTools Performance rows map the semantic names used by native templates to `content-section-1/2/3` from `default.cwm`, fixing the empty panel on template-driven Wii/Wii U menus.

Validation run: `desktop_test/build_wiiu_gate.sh`, `desktop_test/build_shared_gate.sh`, `desktop_test/build_main_gate.sh`, `git diff --check`. These are desktop syntax/build gates; the devkitPro RPX/WUHB build and physical console behavior still require user-side testing. If loading music persists after this change, capture `CWebView.log` and confirm the binary was rebuilt with the updated `-lwhb` link step.


## 2026-10-23 menu, pointer, and runtime-log follow-up

- Removed the main-menu More row and stopped shipping `cwmenu/extras.cwm`; the main CWM now uses three action rows plus Back.
- Corrected the Wii U runtime log filename to `CWV.log` in the app/package folder (the no-root fallback remains the canonical app folder). Wii writes `CWV.log` beside the resolved app root. Vita's existing path is `ux0:data/CWebView/<title-id>/CWV.log`; Windows writes beside the executable. These paths are established by source inspection, not physical-console confirmation.
- Adjusted Wii U Wiimote IR cursor mapping to handle normalized KPAD coordinates as well as 1280x720 pixel coordinates, addressing the reported near-zero cursor movement. Needs physical Wii U verification.
- The shared CWM Files browser is now implemented for Windows, Wii, and Wii U. It is rooted at `<app-dir>/web/`, lists folders/files, and opens HTML/XHTML. It does not yet provide templates/viewers for images, audio, video, text, PDF, or binary files. Native SDK builds and hardware smoke tests remain outstanding.
- Validation: Wii U, Wii, shared, main, CWMenu parser and menu UI gates pass. Native SDK builds and hardware tests remain outstanding.

## MIME/file browser continuation
- `sources/shared/sources/core/cwv_mime.{h,c}` provides case-insensitive
  filename MIME lookup and coarse template classes (HTML, XHTML, image, audio,
  video, text, PDF, binary). Unknown types return `application/octet-stream`.
- The shared CWM Files view is now wired to Windows, Wii, and Wii U. Platform
  callbacks enumerate entries under `<app-dir>/web/`; shared code handles
  selection, parent-folder navigation, MIME classification, and opening HTML /
  XHTML URLs. `web/` is the highest allowed directory and path traversal is
  rejected. Non-HTML/XHTML file templates remain intentionally unimplemented.
- Gates passed: shared, main production/debug, Wii, Wii U, and MIME tests.
  Windows cross-compiler was unavailable; native SDK builds and hardware tests
  remain outstanding.


## Windows build, platform launcher, and Vita touch feedback (2026-10-26)

- Enlarged shared Files-browser names to 260 bytes and bounded the Windows `WIN32_FIND_DATAA` name copy to remove the Windows filename truncation warning.
- Added `cwv_platform_name()` to each platform seam and exposed the limited `navigator.platform` / `navigator.userAgent` identity object (also as `window.navigator`). The shared optional launcher template now shows controls for Vita, Wii, Wii U, and Windows. Other navigator APIs are not implemented by this change.
- Added F11 borderless fullscreen, per-monitor DPI-awareness with a legacy fallback, and point-sampled presentation for the fixed 960x544 Windows software framebuffer. This avoids HALFTONE blur but is not yet true native-resolution reflow; the renderer remains CPU/GDI, not GPU accelerated.
- Added visual-only markers for each active front touchscreen contact on Vita. The existing touch input remains the input source; markers are not separate cursor-driven clicks.
- Replaced the bad pixel fixture with `test_pages/replacement-pixel.png`; copy it to local `webapp/nested/pixel.png` if you want the same fixture path. The replacement is a valid PNG and the CWI pack/unpack self-check passes (the packer can correctly elect to keep it as PNG because a tiny checker pattern is not smaller as CWI).
- Validation: shared-source, JS, production/debug main, Vita KBM, Wii, Wii U, path, and diff gates pass. Windows cross-compilation was skipped because `x86_64-w64-mingw32-gcc` is not installed in this environment; native VitaSDK/console testing was not performed.
