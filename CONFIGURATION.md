# Configuration

`vita/build-vita.sh` can prompt for the packaging metadata so the wrapper
is not tied to Piskel-Sharp.

It asks for **Name**, **Title ID**, **Shorthand**, **Version**, **Author**,
**Native target**, and **Webapp directory**. Environment variables with the
same concepts (`APP_NAME`, `TITLE_ID`, `SHORTNAME`, `VERSION`, `AUTHOR`,
`TARGET`, `WEBAPP_DIR`) make it non-interactive.

Example:

```bash
APP_NAME="My Web App" TITLE_ID="MYAPP0001" SHORTNAME="MYA" \
VERSION="01.00" AUTHOR="Me" TARGET="MyWebApp" \
WEBAPP_DIR="/path/to/site" ./vita/build-vita.sh
```

VitaSDK's packaging toolchain uses a 9-character Title ID and `##.##` app
version format. citeturn0search3

## Project metadata JSON

`sources/build-vita.sh` can automatically populate the interactive Vita metadata prompts from a JSON file. By default it checks `webapp/package.json`, then `package.json` beside the `vita/` directory. Use `VITA_PACKAGE_JSON=/path/to/file.json` for another filename/location.

When a JSON file is found, the script displays a preview of the recognized values and asks `Use the provided JSON values? [Y/n]`. Choosing **No** leaves the normal prompts available.

Recognized fields can be top-level or inside a `vita` object. The directory containing the selected JSON file is also used as the default **Webapp directory** when the JSON metadata is accepted. You can make this explicit with `webappDirectory`; when it is present, the build script uses it automatically and does **not** ask for the webapp directory again. Relative values are resolved relative to the `package.json` file.

```json
{
  "name": "WebWeb",
  "titleId": "WEBWEBWEB",
  "shorthand": "WEB",
  "version": "01.00",
  "author": "Webber",
  "target": "CWebView",
  "webappDirectory": "."
}
```

Common aliases such as `titleID`, `shortName`, `appVersion`, and `nativeTarget` are accepted. Other JSON fields are ignored, so a normal web project's `package.json` can be used without being converted into a Vita-specific format.

## Debug builds

By default the VPK is a **production build**: the on-page debug overlay is
compiled out entirely - SELECT does nothing and START is forwarded to the
webpage as a button input. To
get a **debug build** with the overlay compiled in, either pass `--debug` to
`sources/build-vita.sh` or set the manifest field:

```json
{
  "debug": true
}
```

Accepted values: `true`/`1` enables it, `false`/`0` forces a production
build even if `--debug` was passed on the command line. The build summary
prints `Debug build: on/off` so the flavor is always visible. The flag is
compile-time only: nothing a web page or a settings file can do enables the
overlay in a production build.


## Webpage entry point

`package.json` may contain a separate `webpage` field:

```json
{
  "name": "My Web App",
  "webpage": "index.html"
}
```

`webpage` is intentionally independent of the other Vita metadata. The build
script first asks whether to use the JSON metadata and then separately asks
whether to use the JSON webpage entry point. This allows metadata to come from
`package.json` while selecting a different HTML entry point.

The entry point is relative to the webapp directory and is packaged under
`app0:/web/`. Relative CSS, image, and font URLs are resolved from the entry
page's directory.

## LiveArea images

The Vita package always includes an icon, LiveArea wallpaper, and startup logo.
If no custom paths are supplied, CWebView's built-in default artwork is used.

The values can be supplied in `package.json`, either at the top level or inside
`vita`:

```json
{
  "vita": {
    "icon": "vita/icon.png",
    "wallpaper": "vita/wallpaper.png",
    "startLogo": "vita/start-logo.png"
  }
}
```

Relative paths use the same package-JSON-relative path resolution as the other
image/resource settings. Absolute paths are also accepted.

### Per-console packaging images live with the webapp

Each console's packaging images (icon / wallpaper / start logo) belong in the
WEBAPP folder, one subfolder per console - NOT in the project's `assets/`
directory. Relative manifest values resolve against the `package.json`
directory, so the layout is:

```
webapp/
  vita/    icon.png, wallpaper.png, start-logo.png
  wiiu/    icon.png
  wii/     icon.png
```

The same `package.json` can document all three consoles side by side. The
canonical layout keeps the SHARED webapp facts at the top level (name,
shorthand, version, author, webpage, webappDirectory, target) and puts
console-specific values in `vita` / `wiiu` / `wii` sections, where they
OVERRIDE the top level for that console:

```json
{
  "name": "CWebView Example",
  "shorthand": "CWV",
  "version": "00.10",
  "author": "ThatOneBit",
  "target": "VitaWebview",
  "webpage": "index.html",
  "webappDirectory": ".",
  "vita": {
    "titleId": "CWEBVIEWX",
    "icon": "vita/icon.png",
    "wallpaper": "vita/wallpaper.png",
    "startLogo": "vita/start-logo.png",
    "storageDirectory": "Webpage",
    "cursor": "vita/cursor.png"
  },
  "wiiu": {
    "shorthand": "CWV",
    "webpage": "index.html",
    "icon": "wiiu/icon.png",
    "description": "CWebView example pages for the Wii U",
    "cursor": "wiiu/cursor.png"
  },
  "wii": {
    "icon": "wii/icon.png",
    "cursor": "wii/cursor.png"
  }
}
```
(The `cursor` entries are illustrative - absent keys fall back to the
app's default cursor art; see "Pointer cursor art" below.)

(JWT-style comments like `// what does this do?` are not allowed inside the
real file - JSON has no comment syntax - so field explanations live in
`webapp/cwebview.schema.json` and here.)

The manifest has a formal schema: `webapp/cwebview.schema.json`. Every field
is optional (nothing is required - the builds prompt or use defaults for
anything missing), the schema documents each field including what `target`
does, and `package.json` points at it via `"$schema": "./cwebview.schema.json"`
so editors (VS Code and friends) validate and auto-complete the manifest
locally. The build scripts do not enforce the schema; it exists for tooling
and documentation. The pointer can be swapped for a hosted URL later (e.g.
the raw file on a private GitHub repo) by editing the `$schema` value only.

What `target` does: it names the NATIVE app target. `sources/build-vita.sh` uses it
as the CMake target name, so the VPK comes out as `VitaWebview.vpk` and the
engine compiles with `-DVITA_TARGET=VitaWebview`. On Wii U it is currently
informational - the Wii U build always produces `CWebView.rpx` - but the
`wiiu` preview echoes it so one manifest documents the whole project.

`sources/build-vita.sh` and `sources/build-wiiu.sh` both default to these locations (with the
old project-`assets/` paths kept as fallbacks so existing trees keep building).
The `wii` section is recognized as the future source of the Wii packaging
metadata; `sources/build-wii.sh` does not consume it yet (the port is still
compile-check only).

The build script also accepts direct overrides:

```bash
./build-vita.sh \
  --icon webapp/vita/icon.png \
  --wallpaper webapp/vita/wallpaper.png \
  --start-logo webapp/vita/start-logo.png
```

The built-in artwork is sized for the Vita LiveArea format (128x128 icon, 840x500 wallpaper, and 280x158 startup image).

## Pointer cursor art

Console cursors live in the APP's shared assets, NOT the webapp:
`assets/cursor-vita.png` (touch ring), `assets/cursor-wii.png` and
`assets/cursor-wiiu.png` (pointer arrows).  The build stages the chosen
art into each package at `<package>/assets/cursor-<console>.png`,
which is the fixed path the runtime loads.

Override per console with the manifest's `cursor` key (webapp-relative,
exactly like `icon`): the build stages that file instead.  With no art
at all, the shared cursor module draws its built-in arrow.  (A
manifest `cursor` value can also be an absolute path, like `icon`.)

### Cursor hotspot (2026-09-26 (45))

Console cursor art is CENTER-anchored: the pointer position is the
CENTER of the art, so a round cursor is exact - a small circle can be
the entire cursor and still tap what you see.  Ship round art centered
in the image.  Tip-shaped art can restore the old top-left-tip
contract by calling `cwv_cursor_set_hotspot(CWV_CURSOR_TIP)` after
init; the code-drawn fallback arrow is always tip-anchored.

### CSS cursors and the cursor pack (2026-09-26 (45))

Pages can request real cursors with the standard CSS `cursor` property:

- Keywords: `auto, default, none, pointer, text, wait, crosshair,
  help, move, grab, grabbing, not-allowed, zoom-in, zoom-out` -
  inherited like `color`, resolved on the element under the pointer.
- `cursor: url(<image>) [x y]` - custom art loaded by the ENGINE
  (local webapp file or http/https), with an optional CSS hotspot
  `url(cursor.png) 3 4` (pixels from the image's top-left).

Console cursor art is a pack at `<package>/assets/cursors/` (the
build stages the repo's `assets/cursors/` directory when present):

    default.png pointer.png text.png wait.png crosshair.png
    help.png move.png grab.png grabbing.png not-allowed.png
    zoom-in.png zoom-out.png

A Chrome-style pack is the intended art set: `default` = arrow,
`pointer` = hand, `text` = I-beam, `wait` = spinner, and so on.
Missing pack art falls back to the console cursor art, then the
code-drawn arrow; `cursor: none` hides the cursor but taps still land.

The feature is per-app toggleable in Settings > Appearance, row
"CSS cursors" (On by default), persisted in `settings.cfg` as
`css_cursors=0|1`.  With it off, the console cursor art is always
shown regardless of what pages request.

The packaged destinations are:

- `sce_sys/icon0.png`
- `sce_sys/livearea/contents/bg0.png`
- `sce_sys/livearea/contents/startup.png`
- `sce_sys/livearea/contents/template.xml`

## Optional launch/launcher page

`package.json` may also define `vita.launchPage` (aliases: `launch_page`,
`launcher`, or `defaultPage`). It is relative to the webapp directory.
When supplied, this page is loaded first instead of `vita.webpage`.

This is useful for apps that want a small, styled launcher page which links to
multiple parts of the webapp, for example:

```json
{
  "vita": {
    "webpage": "index.html",
    "launchPage": "vita-launcher.html"
  }
}
```

The launcher is just normal HTML/CSS. Nothing special is required in the
renderer, so it can present links such as `editor/index.html`,
`settings/index.html`, or other app pages and use the existing `cwv-target`
link behavior.

## Built-in launcher

If `launchPage`/`launch-page` is present in `package.json` with an empty string, the build generates a small CWebView launcher automatically. It is packaged as the selected launch-page filename and contains an `Open application` link to the configured `webpage` entry point.

For example:

```json
{
  "vita": {
    "webpage": "index.html",
    "launchPage": ""
  }
}
```

The generated launcher is built from the CMake template in `CMakeLists.txt`, so it requires no extra file in the webapp directory. A non-empty `launchPage` continues to use the developer-supplied HTML page.

## Native UI accent

The Webview Settings menu has an `Accent color` setting. The available values are Blue, Red, Orange, Green, Cyan, and Dark Purple. This changes only CWebView's native menu/settings chrome; it never changes webpage CSS or inherited webpage colors. The selected value is stored in `settings.cfg` as `accent=0` through `accent=5`.

## Webapp minification

The webapp sources stay readable; minification happens at BUILD time.
`sources/build-vita.sh` runs `tools/minify_web.py` and packages the staged copy
from `vita/build/webapp_min` (HTML comments + CSS whitespace removed; JS
and binary assets are copied untouched so template literals can never
change). To ship unminified, point the build at your webapp dir directly
with `-DVITA_WEBAPP_DIR`.


## Experimental `cwv-args`

A project may declare a root-level, space-separated `cwv-args` string in
`package.json`, for example:

```json
"cwv-args": "beta-enable-canvas beta-enable-webgl"
```

The runtime reads this string from the app-root manifest once after an app
root is selected. These names are experimental requests, not guarantees of
browser compatibility. At present `beta-enable-canvas` and
`beta-enable-webgl` are recognized but ignored with a diagnostic because the
Canvas 2D and WebGL APIs are not implemented. Unknown switches are also
reported and ignored. Do not add either switch to a normal project expecting
the corresponding API to work. The option is intentionally absent from the
shipped example manifest so existing projects retain their current behavior.


## Windows target architectures

The Windows builder reads `windowsArchitectures` from `webapp/package.json` (or `windows.architectures`) as an array. Supported targets are `x86`, `x64`, `arm32`, and `arm64`. If omitted, it keeps the backward-compatible default `["x64", "arm64"]`. See `sources/win/README-WIN.md` for compiler names, environment overrides, and skip/failure behavior.
