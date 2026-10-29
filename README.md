# CWebView

A native console webpage wrapper (PS Vita, Wii U, Wii). The native
application is **webapp-agnostic**: pass any directory containing an
`index.html`, and the build packages it under `app0:/web/` on Vita -
on Wii U and Wii the web content lives in a `web/` folder NEXT TO the
package, and the app resolves that folder at runtime (see
`cwv_platform_set_app_root` in `sources/shared/sources/core/cwv_platform.h`).

Piskel-Sharp is only one possible client; it is not part of the wrapper's
native architecture.

## CSS / Chromium compatibility

CWebView deliberately keeps the native renderer separate from the page
engine. The renderer implements a lightweight Vita-native subset for controls
and diagnostics; it is **not** a Chromium implementation. Therefore it cannot
truthfully claim support for every CSS property introduced by current Chromium.
Modern Chromium continues to add CSS features (for example, newer container
query behavior, typed CSS arithmetic, scroll-triggered animations, and newer
pseudo-elements), so a finite hand-written renderer cannot provide complete
feature parity.

Page styling is handled by the project's own native renderer (see
`docs/EXTENDING_THE_RENDERER.md`). Unknown CSS declarations are ignored by
the native diagnostic renderer instead of being treated as if they were fully
implemented.

The current renderer does support the CSS needed by its native UI controls,
including `text-align`, colors, backgrounds, borders, padding/margins,
font sizing/families/weights, display modes, overflow, and the Vita-specific
control states.

## Current backend status

The native host now has a real backend boundary and safe navigation pipeline:

1. Validate a URL under the app content root (`file:///app0/...` on
   Vita; the package's own folder on Wii U and Wii).
2. Verify the requested resource exists in the VPK.
3. Dispatch navigation/evaluation/update/draw through a backend interface.
4. Only report `READY` after a backend actually accepts navigation.

The backend is a native HTML/CSS renderer with a JavaScript engine (Duktape
2.7.0) - see `docs/EXTENDING_THE_RENDERER.md`. (An earlier plan explored a
Sony 3.61-3.65 WebKit migration; it was removed: the runtime/SDK
dependencies, such as vita-cairo and WTF, cannot be obtained or rebuilt
reliably, so the native renderer is the permanent engine.)

## Modern JavaScript

ES2015+ handling is provided by the single integrated **CwvESM** pipeline
under `sources/shared/sources/web/js/modern/`. It transforms supported
modern JavaScript into the engine's ES5.1-compatible execution path and
provides the built-in library preludes. It is linked directly into every
port; there is no separate `CwvESM.suprx` runtime module anymore.

The Vita-specific `CwvScroll.suprx` remains a separate module because
scroll/input policy is intentionally isolated from the renderer.

## Build

From WSL, the preferred setup is to put `package.json` in the project/webapp
root and set `webappDirectory` there. The build script automatically uses that
directory and does not prompt for it again. For example:

```json
{
  "webappDirectory": ".",
  "webpage": "index.html"
}
```

The script still accepts `--webapp /path/to/webapp` as an explicit override, as
well as `--title-id`, `--version`, and `--name`. Relative `webappDirectory` paths
are resolved relative to the package.json that supplied them.

For automated builds, pass `-y` (or `--yes`) to automatically answer **yes** to
all build-script Y/N confirmation prompts. It does not fill in ordinary metadata
value prompts; those still use their normal defaults or supplied environment
variables. For example:

```bash
./build-vita.sh -y
```

For a **debug build** (on-page debug overlay compiled in; in debug builds
START toggles the overlay and SELECT toggles/collapses it, in production
builds neither key touches it):

```bash
./build-vita.sh -y --debug
```

Equivalently, put `"debug": true` in the webapp `package.json` manifest
(see CONFIGURATION.md).

## Native shell menu and DevTools

The native Webview menu is opened with **L + R + SELECT**. It is a native overlay, is not affected by the page's `cwv-control-mode`, and can be operated with the front touchscreen.

### DevTools control-mode override

The DevTools control-mode override includes **Default (Page-defined)** as a selectable value. Selecting it removes only the temporary control-mode override and restores the page's `<html cwv-control-mode>` value; it does not clear the other DevTools overrides.

### Vita control modes

The `<html>` element can opt into a control policy with `cwv-control-mode`:

```html
<html cwv-control-mode="Browser">
```

Supported modes are:

- **None** (default): preserves the existing compatibility behavior.
- **Browser**: D-pad cycles focus, Cross activates, Circle goes back, Square opens the Webview Menu, Triangle refreshes, and L/R navigate back/forward.
- **Standalone**: physical page controls are opt-in with `vcm` attributes, for example `<button vcm="cross">Open</button>`. Supported values are `cross`, `circle`, `square`, `triangle`, `up`, `down`, `left`, `right`, `l`, and `r`.
- **Touch**: page physical controls are ignored. Touch input and touch scrolling remain active. The native L+R+SELECT menu shortcut always remains available.
- **Cursor**: the pad becomes a mouse. Left stick and D-pad move the shared pointer cursor, Cross taps (or taps-and-holds to drag) whatever is under it, Circle goes back, Triangle refreshes, Square opens the Webview Menu, and L/R navigate back/forward. Native stick scroll/zoom axes are skipped so the stick stays the pointer. Requires the cursor feature (`vita.cursor` in package.json); with it compiled out, Cursor pages fall back to the None behavior.

`cwv-control-mode` affects only Vita physical-control routing. It does not modify the webpage's CSS or DOM.

The current native renderer does not yet execute arbitrary webpage JavaScript, so JavaScript registration of VCM handlers will be added when the DOM/JavaScript runtime is enabled. The `vcm` HTML attribute is the supported standalone binding in this revision.

The main menu contains browser-style navigation controls, Settings, and DevTools. DevTools is organized into separate Performance, Renderer, Input, and Page views and only draws diagnostics while the menu is open.

### Settings physical navigation

The Settings menu uses separate controls for the two columns:

- **D-pad:** selects the inset, non-button settings rows (Scroll input, Zoom input, Zoom level, Theme, and Accent color).
- **L Analog:** selects the left-side action buttons (Undo, Save, Back).
- **Cross:** activates the currently selected item.
- **Left/Right:** changes the selected setting value when a D-pad setting row is selected.

The selected inset row is outlined using the current native accent color. This selection is independent of webpage focus and `cwv-control-mode`. Touch selection uses the same logical selection state.

### Persistent settings and application data

CWebView keeps application data **per application**, rather than sharing one
global settings file. The package metadata can choose the application data
namespace with `vita.storageDirectory` (or the equivalent top-level
`storageDirectory` / `dataDirectory` aliases). For example:

```json
{
  "name": "My Vita App",
  "titleId": "MYAPP0001",
  "vita": {
    "storageDirectory": "MyVitaApp"
  }
}
```

Native Webview settings are then stored at:

```text
ux0:data/CWebView/MyVitaApp/settings.cfg
```

If no storage directory is specified, the application's **Title ID** is used,
so two different Vita applications do not share settings by default. The
namespace is restricted to a safe directory name and cannot escape the
`ux0:data/CWebView/` root.

This namespace is also the storage foundation for future named Webview
presets and application-data APIs; those features can use the same per-app
location without changing the package format.

The current settings file format is:

```text
scroll=1
zoom=0
zoom_level=100
dark=1
accent=0
```


### Native input defaults

The default native controls are:

- **L Analog**: page scrolling
- **R Analog**: page zoom
- **Touch**: page touch scrolling/input and native menus when a native menu is open

### Keyboard + mouse (KBM) support

Every port that can take a keyboard/mouse does. The console keyboard paths
share one grammar: **F1** toggles the main menu, arrows scroll the page
(Up/Down vertical, Left/Right horizontal - all four read independently, so
diagonal key combos adjust both axes at once) or move the menu cursor,
PgUp/PgDn page-scroll, Home jumps to top, Tab cycles page focus, Return
activates, Escape clears focus, F5 reloads. Two-axis smooth motion works
everywhere: the Vita/Wii U analog sticks apply x and y every frame (circle
the stick and the page orbits), the Vita touch pad pans both axes together
after a 7px engage (the optional scroll module covers y when present), and
a tilt-wheel mouse on Win/Wii U sends both deltas in one gesture. Windows
instead uses a PC-native map (right click / LShift+RShift+Space / F1 for
the menu, Ctrl+wheel zoom, Ctrl+R reload) - see its bullet below.

- **Windows**: PC-native grammar - the CSS-aware OS cursor, left click
  activates, right click and LShift+RShift+Space (or F1) open the CWMenu,
  wheel/tilt-wheel scroll, Ctrl+wheel and Ctrl+=/-/0 zoom, Ctrl+R or F5
  reload (see sources/win/README-WIN.md for the full map).
- **PS Vita**: Bluetooth HID keyboards and mice via SceHid
  (`cwv_kbm_vita.c`). A paired keyboard synthesizes controller bits, so
  the page, CWMenu, Settings, and DevTools all respond to it; a paired
  mouse drives the virtual cursor with left-click tap, wheel scroll,
  and right-click opening the menu.
- **Wii**: USB keyboards via libwiikeyboard (`KEYBOARD_Init(NULL)` - no
  stdin takeover). Same grammar as above.
- **Wii U**: USB keyboards and mice via SDL2 (SDL_KEYDOWN /
  SDL_MOUSEWHEEL), same grammar; the mouse wheel scrolls vertically and
  tilts horizontally.

Zoom is a native renderer feature and is independent of `cwv-control-mode`. It currently ranges from 0.75x to 2.0x and is centered on the screen. `settings.cfg` can override the input modes:

```text
scroll=1
zoom=0
zoom_level=100
dark=1
```

`scroll=0` selects R Analog, `scroll=1` selects L Analog, and `scroll=2` selects D-pad. `zoom=0` selects R Analog, `zoom=1` selects L Analog, and `zoom=2` disables analog zoom. `zoom_level` stores the initial zoom percentage (75-200).

The native menu layout is documented in `docs/NATIVE_MENU_TEMPLATES.md`; edit `src/native_menu_templates.c` for the template geometry and `src/main.c` for behavior/dynamic values.

### CWV `<textfield>`

`<textfield>` is a CWebView extension used by the legacy renderer to invoke the PSVita system keyboard:

```html
<textfield id="name" placeholder="Your name"></textfield>
<textfield value="Initial value"></textfield>
<textfield type="password"></textfield>
```

Tap the field, or focus it with DPAD and press CROSS, to open the Vita IME. Press Enter to commit the value; Cancel leaves the previous value unchanged. This is a CWV compatibility control. Ordinary HTML `<input type="text">` is also supported; `<textfield>` remains as a CWV convenience alias.

### Page title and favicon

The native menu reads the loaded local document metadata. `<title>` is shown as the page title, and `<link rel="icon" href="...">` / `<link rel="shortcut icon" href="...">` may provide a PNG, JPEG, or BMP favicon. If no icon link is present, `favicon.png` in the page directory is used as a fallback candidate. The current URL remains visible in the menu and DevTools.

## Native menu templates

Every native menu is a complete `VitaMenuTemplate` array. The first element is the `window` frame; the header, content, controls, footer, and scroll geometry belong to that menu's template as well. Rendering and touch hit-testing use the same active template, so changing a template cannot leave the shell using stale coordinates.

Settings and every DevTools tab remain separate template constants. To add a menu, add its template, register it in `native_menu_template()`, and give it a `NativeMenuMode`/`native_menu_open()` state initializer.

The Page Operations launch target is intentionally swappable at build time:

```c
#define CWV_PAGE_OPERATIONS_MENU_MODE MENU_PAGEOPS
#define CWV_SETTINGS_MENU_MODE MENU_SETTINGS
#define CWV_DEVTOOLS_MENU_MODE MENU_DEVTOOLS
```

Each main-menu launch target is independently swappable. For example, changing `CWV_PAGE_OPERATIONS_MENU_MODE` to `MENU_DEVTOOLS` makes the Page Operations entry open the complete DevTools system instead. The same mechanism works for Settings and the top-right DevTools button.

## Native menu template widgets

`native_menu_templates.c` supports a few reusable visual primitives for menu authors:

- `VITA_MENU_SLIDER` — a multi-stop horizontal slider. The current DevTools Performance template demonstrates three stops (`Low`, `Normal`, `High`).
- `VITA_MENU_DECORATION` — lightweight template-owned graphics. Current decoration labels include `accent`, `marker`, and `dot`.

The Performance `Pitch (UI demo)` slider is intentionally a UI/input test widget; it does not alter an audio pipeline. It demonstrates how a future menu can expose a three-stop value while keeping its geometry in the template. Touch chooses the nearest stop and D-pad LEFT/RIGHT moves between stops.

## Current language and method support

See [Supported methods and language features](docs/SUPPORTED_METHODS.md), with
HTML, CSS, JS-TS and WASM separators and explicit implementation limits.

## Platform ports

- Windows (AMD64 + ARM64): see [sources/win/README-WIN.md](sources/win/README-WIN.md).
- Full platform status and the Android / Switch / Unix / Linux roadmap:
  [docs/PORT_ROADMAP.md](docs/PORT_ROADMAP.md).

## Build gates

`desktop_test/run_all_gates.sh` runs every pre-ship gate (18 shared +
BMP unit + Windows cross-build).
