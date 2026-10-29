# Wii port status

Same pattern as the Wii U: the shared engine only ever sees
`sources/shared/sources/core/cwv_platform.h`; this port implements it on
**libogc + GRRLIB** in `sources/wii/cwv_platform.c`, with the app loop
in `sources/wii/main.c`.

Done:
- **Package-folder content root** (2026-09-23 (33), user direction):
  app0: is a REAL Vita device and stays Vita-only.  The Wii loads
  everything from the FOLDER THE PACKAGE SITS IN - the Homebrew Channel
  passes boot.dol's full path in argv[0], so `.../exampledir/boot.dol`
  loads its web content from `.../exampledir/web/`.  The engine's URL
  root is pointed at that folder at boot (`cwv_platform_set_app_root`),
  so URLs name the real folder and the URL bar stays honest; the
  compile-time `CWV_WII_APP_ROOT` is only the fallback when argv[0] is
  unusable.  Writable cache directory (`<root>/cache`) for remote fonts.
- Texture decode + upload (PNG/JPEG/BMP; GRRLIB sniffs the format).
- Draw primitives (rects, circles), screen clear, ABGR->RGBA color
  conversion for all GRRLIB calls, TTF fonts through GRRLIB_LoadTTF.
- **Part-texture draws** through `GRRLIB_DrawPart` (real per-UV
  sub-rects since GRRLIB 4.4): background tiles and object-fit crops
  are true part draws, not whole-texture stand-ins.
- **Bundled default font**: `<root>/assets/font.ttf` (the same libre
  font the Wii U packages, see `assets/FONT_LICENSE.txt`) is the
  default face; text width and draw honor the size/scale arguments.
- **App loop** (`sources/wii/main.c`): drives the shared host NATIVE
  at 640x480 (the page adapts like any 4:3 browser window), with:
  - **Wiimote IR cursor** (`cwv_cursor` shared module; art from
    `<root>/assets/cursor-wii.png` or the manifest `wii.cursor` override;
code-drawn fallback arrow) - always
    on when tracked, like the Wii Menu. A taps at the cursor (page or
    menu), release ends the tap wherever it lands.
  - **GameCube pad**: A/B and D-pad mirror the wiimote.
  - **CWMenu shell** (the shared native menu, scaled 640/960 from the
    Vita canvas via `cwv_menu_ui_set_scale`): HOME opens/closes the
    main menu (fixed, like the Wii Menu - the Preferences "Menu
    shortcut" row shows "HOME (fixed)"), MINUS/PLUS switch settings/
    devtools tabs, D-pad navigates rows, B is Back/Cancel.
  - Settings persistence at `sd:/apps/cwebview/settings.cfg` (same
    key=value format as the Wii U; `menu_hotkey` is not stored because
    HOME is fixed), runtime log at `CWV.log`, clear-cache reports
    through the shared status chip.
  - Saved theme applies before the first page paints (the Wii U flash
    lesson), boot diagnostics screen with a 2s retry when the page is
    missing, devtools live data (60fps vblank-locked metrics).
- ES2015+ support: the built-in transformer (no .rel module yet).

Done (2026-10-03 packaging round):
- **Software on-screen keyboard** (`cwv_ime_wii.c`): libogc has no IME,
  so the keyboard is code-drawn through the engine's own primitives -
  QWERTY + digits/symbols layouts, one-shot SHIFT, IR-pointer taps,
  D-pad navigation, B cancels.  The engine IME seam
  (cwv_ime_open/cwv_ime_poll) is fully implemented: textfields open it,
  and the main-menu "Open URL" row prefills it with the current page
  (relative to web/, the Vita convention: "./nested/x.html", "../"
  climbs, remote URLs refused).
- **HTTP fetch** (`cwv_fetch_net.c`): libogc net stack, lazy DHCP on
  the first fetch (offline boots never stall), HTTP/1.0 +
  "Connection: close" (no chunked coding), 2MB cap, 10s read timeout.
  https:// is refused - libogc ships lwip WITHOUT TLS; local pages and
  http:// work, https:// fetches report failure through the engine.
- **Packaging**: `build-wii.sh` now compiles the whole tree with
  devkitPPC, links against GRRLIB/libogc (the homebrew template link
  line), converts to boot.dol (elf2dol) and stages the HBC-ready SD
  package `build/wii/sdcard/apps/cwebview/` (boot.dol, meta.xml, icon,
  minified web/, font + cursor assets) from the package.json "wii"
  manifest section, with progressive build percentages.

Not yet done:
1. Real-hardware validation (devkitPPC not available in the dev
   sandbox - the desktop gates syntax-check against libogc-mirroring
   stubs, including the real WPAD button masks, IR struct and the
   network.h (gc/network.h) surface; first flash should happen in Dolphin: load
   boot.dol with the SD pack mounted - IR pointer = mouse).

Desktop gate: `desktop_test/build_wii_gate.sh` compiles the whole port
surface (platform.c, main.c, menu UI, CWMenu API, templates, cursor)
against stubs that mirror the REAL libogc headers, so wrong constants
fail on the desktop, not on the console.

## Webapp manifest

`package.json` (the webapp manifest) recognizes a `"wii"` section for the
future packaging values, mirroring the `"vita"` and `"wiiu"` sections -
e.g. `"icon": "wii/icon.png"`, resolved against the manifest directory
like the other consoles. `build-wii.sh` consumes it for meta.xml and the
icon (name/version/author/description/icon), falling back to the common
top-level aliases so one shared package.json documents all consoles.

## Modules on the Wii: .rel files

Per the owner's note: the Wii supports **.rel** relocatable modules -
code loaded on demand, with memory released when the module is not
needed. That is the Wii's analog of the Vita's suprx and the Wii U's
RPL. ESM/Scroll equivalents can therefore ship as .rel files once the
app loop exists; the built-in transformer (compiled in, same as the
Wii U plan) comes first, and .rel is the optimization if load-time
memory or hot-swapping becomes a real need.
