# Wii U port status (engine-first, not yet hardware-verified)

The shared engine (sources/shared) is console-agnostic and talks only to
`sources/shared/sources/core/cwv_platform.h`. The Vita implementation lives in
`sources/vita/cwv_platform.c`; this port's implementation is
`sources/wiiu/cwv_platform.c`, built on **wut + SDL2** (wiiu-sdl2,
sdl2_image, sdl2_ttf).

Done:
- **Package-folder content root** (2026-09-23 (33), user direction):
  app0: is a REAL Vita device and stays Vita-only.  Web content is NEVER
  packaged into the .rpx/.wuhb - the launcher passes the package's path
  in argv[0], and `.../exampledir/s.rpx` loads its web content from
  `.../exampledir/web/` (validated against the web/ folder; the
  canonical SD layout is the fallback probe).  Log/settings/cache live
  in the same folder.
- Texture decode + upload (PNG/JPEG/BMP via SDL2_image), filters.
- Draw primitives (rects, circle fan via SDL_RenderGeometry, scaled and
  part-scaled texture draws), screen clear.
- TTF fonts through SDL2_ttf (bundled `assets/font.ttf`; the Wii U
  exposes no system font to homebrew).
- The app loop (`sources/wiiu/main.c`): SDL2 events, touch, focus
  navigation, smooth scroll, runtime log on the SD card.
- CWMenu shell (`sources/shared/sources/menu/cwv_menu_ui.{h,c}`): menu state,
  main/page ops/settings/DevTools rendering, pad/touch navigation, live
  diagnostics, scrollable console, and theme/zoom/scrollbar overrides.
  `sources/wiiu/main.c` supplies GamePad input, font/shape painting,
  persistent settings, scoped cache clearing and the native URL IME.
  The runtime `.cwm` asset is staged at `cwmenu/default.cwm`.
  The default shortcut is **L+R+SELECT** (SDL joystick buttons 6+7+11);
  Settings > Preferences can switch to L+R+START or either chord.
  START is forwarded to page scripts as a button input when CWMenu is closed;
  SELECT has no standalone action. B cancels when the menu is open;
  A activates; L/R change tabs. SDL button events 12..15 drive D-pad
  navigation (the devkitPro SDL2 port emits buttons, not hat events).
  Exit App is hidden by default; Preferences can show it and Save persists
  both options to the per-app `settings.cfg` (older settings still load).
  Analog scroll/zoom mode and speed now drive the actual controller paths.
  The shell also draws local page favicons, generic CWMenu 2 widgets,
  passive paired windows, and app-defined menus opened through
  `cwv_menu_ui_open_named()` or a script button with `open_menu:<name>`.
  The passive partner swallows taps over its bounds to prevent click-through.
- HTTP fetch through the Wii U's own network stack
  (`sources/wiiu/cwv_fetch_nets2.cpp`, nn:ac + nsysnet sockets).
- IME text input via the NATIVE system keyboard: wut's `nn::swkbd`
  applet library (`sources/wiiu/cwv_ime_swkbd.cpp`).
- `build-wiiu.sh`: installs devkitPro pacman + the wiiu packages when
  missing, compile-checks the platform layer with the real
  powerpc-eabi toolchain, then builds RPX + WUHB with progressive
  `[ NN%]` output (same style as build-vita.sh's CMake progress).

## Native system applets first

When wut exposes a system applet for a capability, the port MUST use it
instead of hand-drawing a worse replica:

- Text input -> `nn::swkbd` (DONE; the earlier hand-drawn keyboard was
  replaced by it).  The desktop wiiu gate mirrors wut 1.9.1's real API
  surface in `desktop_test/stubs/nn/swkbd/`, so a guessed API fails at
  the gate instead of mid-devkitPPC-build.
- Error dialogs (when the CWMenu shell port needs them) -> `nn::erreula`,
  the system error applet - NOT a hand-drawn dialog.  It follows the same
  Create/Calc/DrawTV/DrawDRC pump pattern as swkbd.
- Socket timeouts -> select() (wut's nsysnet has no SO_RCVTIMEO /
  SO_SNDTIMEO; see sock_wait() in cwv_fetch_nets2.cpp).  Never invent
  SO_* option numbers the console stack does not implement.
- `open_browser` stays -1 on purpose: `SYSLaunchTitle()` swaps to a title
  only when THIS app exits and wut's surface passes no URL argument, so
  there is no real browser hand-off.  Until wut grows one, the engine's
  degrades-gracefully path is the honest behavior.

Remaining verification and parity work:
1. Run RPX in Cemu or on GamePad hardware. Desktop gates test control mapping
   against the published devkitPro SDL2 source and syntax with SDK-mirroring
   stubs, but cannot validate GX2 presentation, texture blending, touch
   calibration, swkbd coexistence, or the observed color flashing on-device.
2. The first page now receives its saved theme *before* HTML/JS parsing;
   unchanged per-frame theme calls no longer repaint defaults. Dynamic JS
   inherited colors update without becoming permanently explicit, and SDL_ttf
   short text uses a bounded GPU texture cache keyed by face/color/text.
   These changes target the reported color flashing, but require hardware
   confirmation. The renderer still cannot emulate all browser CSS behavior.
3. Full parity with every optional Vita feature is not claimed: custom-menu
   scrollbar/content editing, dynamic widget bindings, precise rounded chrome
   crop clipping, Vita-only debug overlay and native error dialog are not
   ported. Error dialogs should use `nn::erreula` when implemented.
4. Hardware verification of swkbd GX2 draw mixing is still required (see
   the caveat in `cwv_ime_swkbd.cpp`).

## Module types on the Wii U

The Wii U has NO suprx/skyx support: executables are **RPX** and shared
libraries are **RPL**. The mapping for this project:

## Artifacts: RPX vs WUHB vs Cemu

`build-wiiu.sh` produces the RPX always and the WUHB when `wuhbtool` is
available (it searches PATH, `/opt/devkitpro/tools/bin`, `$DEVKITPPC/bin`
and `/opt/devkitpro/wut/bin`, then tries installing the dkp package that
the user's own `pacman -Ss wuhbtool` finds; `WUHBTOOL=/path/to/wuhbtool`
overrides everything):

- **`CWebView.rpx`** - the Homebrew Launcher artifact AND the Cemu
  artifact. In Cemu: File -> Load executable, pick the RPX. Cemu does NOT
  run `.wuhb` (unsupported format, see cemu-project/Cemu#962) - the RPX
  is the only loadable artifact for emulator testing. The raw `.elf` is
  an intermediate build artifact (elf2rpl input), not a loadable target
  either - feeding the `.elf` to Cemu only produces garbage; always load
  the `.rpx`.
- **`CWebView.wuhb`** - the Aroma bundle. Real CLI (verified against
  wuhbtool usage): `wuhbtool <rpx> <out.wuhb> --short-name --name
  --author --icon --content`. The whole staged app (web/, assets/,
  meta.xml, icon) rides inside the WUHB as its content partition, so one
  file carries everything. Install by dropping it into `sd:/wiiu/apps/`
  or launching it from the Homebrew on Menu plugin - the .wuhb then
  behaves like a native menu entry, which is the whole point of the
  native-applet philosophy: reuse the console's own UX instead of
  recreating it.

## File paths: one story for console AND Cemu (2026-09-20 crash fix)

History: the engine's URL space was hardcoded to the Vita's
`file:///app0/ <-> app0:` pair.  The first Wii U port mapped `app0:/X`
to a **nonexistent `rom:` device**, so every file open fell through
wut's only devoptab (`fs`, SD mounted at `fs:/vol/external01`) as an
invalid root-relative FSA path.  Consequences: the page could never
load on ANY target, and in Cemu the malformed FSA path faulted INSIDE
the emulator (host access violation on a garbage PPC-derived pointer,
right after startup - Windows minidump from 2026-09-20).

Today (user direction, 2026-09-23 (33)): app0: is Vita-only and the Wii
U loads everything from the FOLDER THE PACKAGE SITS IN.  Content
INSIDE the package (the old WUHB-content probe) stays rejected - but
see step 5 below: when Cemu loads a bare .rpx, `fs:/vol/content` IS
that external next-to-the-package folder seen through Cemu's title
mount, so it returns as a last-resort probe (BUG 43, 2026-09-26).

Root resolution (hardened 2026-09-23 (34) after the first hardware
report: "doesn't find the web directory next to it" - not every
launcher passes a usable argv[0]):

| Step | Root | Covers |
|------|------|--------|
| 1 | folder named by argv[0], validated against its `web/` | HBL and launchers that pass the package path |
| 2 | `fs:/vol/external01/wiiu/apps/CWebView` | the canonical SD layout |
| 3 | scan of `fs:/vol/external01/wiiu/apps/` for `web/` + our own `CWebView.rpx`/`.wuhb` | Aroma homebrew-on-menu, forwarders, Cemu - any folder name, a `.wuhb` file in `wiiu/apps` with `web/` beside it, or a package folder under `wiiu/apps/` |
| 4 | `fs:/vol/content` (then `fs:/vol/code`) | The two REAL mounts that carry a content dir: a loaded WUHB's content partition (Cemu mounts it at /vol/content), and Cemu's title layout - an .rpx inside a folder literally named `code` with a `content` sibling gets that sibling mounted at /vol/content (verified in Cemu's CafeSystem.cpp, 2026-09-26) |
| 5 | `app0:` | a WUHB whose content partition actually carries `web/` (our WUHB is code-only, so this matches only hand-packed bundles) |

As defense in depth, any `app0:/`-prefixed path is rewritten onto the
resolved root at the file seam (`map_path`), so the engine's Vita
default URL root can never reach a device the Wii U lacks once a root
is known.

The boot screen and `CWebView.log` report WHICH step won
(`argv[0]` / `cwd` / `canonical` / `scan: ...`), or - when nothing
matched - the raw launcher argv[0], so a failing setup identifies
itself instead of showing a blank page.

Blind-boot guarantees (2026-09-23 (35), after "no text, no page, no
file was written"): the menu/boot font falls back to a built-in 8x8
bitmap font (`cwv_font8x8.h`, generated from `assets/font.ttf` by
`tools/gen_font8x8.py`) when the TTF cannot be opened, and the log +
settings always write somewhere - with no app root, to
`fs:/vol/external01/wiiu/CWebView.log` and `CWebView.settings.cfg`
(sd:/wiiu/).  The boot screen prints the log path it is using.  The engine URL root is set to the resolved
folder (`cwv_platform_set_app_root`): `.../exampledir/s.rpx` loads
`.../exampledir/web/`.

Because Cemu emulates the console's SD card (backed by a real folder on
the host), the SD layout is the ONE layout that works identically on
hardware and in the emulator - there is deliberately no emulator-only
code path.  When no root matches, the app draws a boot diagnostics
screen (cause + expected roots + retry every 2s) instead of sitting on a
blank page - identical on both targets, so missing-content setups are
visible instead of mysterious.

### Testing in Cemu (secondary target)

How OTHER homebrew handle this (BUG 43 research, 2026-09-26): on
hardware the Homebrew Launcher is the standard - it launches
`sd:/wiiu/apps/<app>/<app>.rpx` passing the full path as argv[0] and
chdir()ing into the folder, which is why ordinary homebrew just open
their side files with plain relative paths.  Our probe chain implements
exactly that standard (steps 1-3 above).  Cemu is the outlier, and its
own source settles what a directly-loaded app can see
(src/Cafe/CafeSystem.cpp, PrepareForegroundTitleFromStandaloneRPX):

- "Load executable" passes an EMPTY argstr - no argv[0], and no
  usable cwd, so steps 1-2 cannot match there.
- The loaded .rpx's own folder is mounted at `/internal/code/`, an
  FSC-internal path the app process cannot open through the `fs:`
  devoptab.  The folder next to the RPX is INVISIBLE.
- `/vol/content` is mounted from the host only when the .rpx sits in
  a folder literally named `code` with a sibling `content` folder
  (the game title layout), and from a WUHB's own content partition.
- The ONE always-visible writable tree is the emulated SD card:
  `<Cemu>/mlc01/sdcard/` == `fs:/vol/external01/`.

So the supported Cemu route mirrors the hardware SD route:

1. Build: `./build-wiiu.sh -y` -> RPX at `build/wiiu/content/CWebView.rpx`
   plus the staged content (`web/`, `assets/`, ...) in the same folder.
2. Copy the staged layout into Cemu's emulated SD card:
   `<Cemu>/mlc01/sdcard/wiiu/apps/CWebView/` must contain
   `CWebView.rpx`, `web/`, `assets/` (same layout as a real SD card).
   Or build with `CWV_CEMU_MLC=<Cemu>/mlc01` and the script deploys it
   automatically (build-wiiu.sh BUG 43).
3. Cemu: File -> Load executable -> pick `CWebView.rpx`.  The SD scan
   (probe step 3) finds the folder wherever it is named.

Direct-loading the bare .rpx WITHOUT the mlc01 copy cannot work on
Cemu by design - the app boots to the diagnostics screen explaining
this (it used to fail as the raw "app0:/web/" error: no probe matched,
so the engine kept its Vita-default URL root).  Alternative Cemu
layouts that also work without any copy: put the .rpx in a folder
named `code` with `web/`, `assets/` in a sibling `content/` folder
(probe step 4), or run with `--cos-mounts` to map a host folder to
`/vol/whatever` yourself.  What is NOT an option: packaging web/ into
the RPX (33, user direction).
4. The runtime log lands at
   `<Cemu>/mlc01/sdcard/wiiu/apps/CWebView/CWebView.log` - same path as
   the console's `sd:/wiiu/apps/CWebView/CWebView.log`.

Hardware stays the primary target: the SD layout IS the real console
layout, and the WUHB is only ever produced for the real thing.

## Install / run

`./build-wiiu.sh` now builds the FULL app: shared engine + Wii U
platform + app loop -> ELF -> `elf2rpl` -> **CWebView.rpx**, staged as
`build/wiiu/content/` = an `sd:/wiiu/apps/CWebView/` package
(rpx, meta.xml, icon.png, `web/` content, `assets/font.ttf`,
`cwmenu/default.cwm`).  If
`wuhbtool` is installed it also packs **CWebView.wuhb** for Aroma.
Copy the package to the SD card and launch from the Homebrew
Launcher.  Engine failures log to
`sd:/wiiu/apps/CWebView/CWebView.log` (VWWV.log-style, script-free
pages included).  Web content: pass `--webapp DIR` (default
`./webapp`, else `test_pages/`).

| Vita (VPK)                     | Wii U (WUHB)                              |
|--------------------------------|-------------------------------------------|
| eboot.bin                      | the app .rpx                              |
| CwvESM (modern-JS pipeline)    | compiled directly into the RPX             |
| sce_module/CwvScroll.suprx     | same choice: link in or ship as .rpl      |

Practical plan: since the Wii U has no per-ESM story (the transformer
core in sources/shared is portable C anyway), link the ESM and Scroll
code directly into the rpx first; split them into .rpl files later only
if load-time memory or hot-swap becomes a real need. RPX/RPL are both
SDK-encrypted container formats handled by the wut toolchain, so the
build script stages them the same way build-vita.sh stages suprx.

Console-side shared-data note: `sources/shared/sources/menu/scripts/default.cwm` and the
CWMenu parser/shell are shared. Console-specific paint/input glue remains
inside each port; no Wii source was changed by this menu work.
