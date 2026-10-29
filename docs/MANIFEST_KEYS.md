# Manifest reference: every `package.json` metadata key

Raw reference for maintainers and tooling (generated from
`webapp/cwebview.schema.json` and the build scripts' own alias tables,
2026-10-06 (77)). End-user docs should paraphrase this - it lives here so
the prose docs stay short and the key list stays authoritative in one
place. The schema itself gives live editor validation in VS Code via
`"$schema": "./cwebview.schema.json"`.

EVERY field is optional: the build scripts fall back to interactive
prompts or defaults for anything missing. Shared facts live at the root;
a console section (`vita` / `wiiu` / `wii`) overrides the root for that
console only. Canonical names are preferred; aliases still work.

## Root keys (shared by all consoles)

| Key | Aliases | Default | Used for |
|---|---|---|---|
| `$schema` | - | - | Points at the JSON schema (local relative path works offline). |
| `name` | `appName`, `title` | `Webpage` | App display name. |
| `shorthand` | `shortName`, `shortname`, `short_name` | `CWV` | Short name in console menus (Vita livearea max 12 chars). |
| `version` | `appVersion`, `app_version` | `00.10` | App version. |
| `author` | `publisher`, `developers` | `CWebView` | Author in the packaging metadata. |
| `target` | `nativeTarget`, `native_target` | `VitaWebview` | Native app target. Vita: CMake target name (VPK is `<target>.vpk`, compiles with `-DVITA_TARGET=<target>`). Wii U: informational only. |
| `webpage` | `webPage`, `entrypoint`, `entryPoint`, `entry` | `index.html` | Entry HTML page inside the webapp directory. |
| `webappDirectory` | `webapp_directory` | `.` | Webapp dir relative to the manifest (`.` = the manifest's own dir). |
| `description` | `longDescription`, `long_description`, `shortDescription`, `short_description` | `""` | Long description (Wii U app listing default). |
| `icon` | `iconPath`, `icon_path` | - | Default icon path, relative to the manifest. Consoles override via their section. |

## `vita` section

Console-specific fields plus overrides of the root keys
(`name`, `shorthand`, `version`, `author`, `webappDirectory`, `webpage`).

| Key | Aliases | Default | Used for |
|---|---|---|---|
| `titleId` | `titleID`, `title_id`, `TITLE_ID` | `CWEBVIEWX` | Vita title ID, 9 uppercase alphanumeric characters. |
| `icon` | `iconPath`, `icon_path` | - | Livearea icon (128x128 PNG). |
| `wallpaper` | `wallpaperPath`, `wallpaper_path` | - | Livearea background image. |
| `startLogo` | `start-logo`, `startLogoPath`, `start_logo` | - | Livearea start logo image. |
| `liveareaTemplate` | `livearea_template`, `template` | the built-in default | LiveArea `template.xml` override (webapp-relative; convention: `webapp/vita/template.xml` is picked up automatically). Must keep the `bg0.png` / `startup.png` filenames CMake stages the wallpaper/start logo as. CLI: `--livearea-template` (79). |
| `storageDirectory` | - | the title ID | App data directory name under `ux0:data/CWebView/` ((85): full path `<data>/CWebView/$TITLEID`; a custom value replaces the title-ID segment). |
| `launchPage` | `launch_page`, `launcher`, `defaultPage` | absent | Vita boot page shown before the app. An EMPTY string generates the default launcher (`__vita_default_launcher.html`); a non-empty value is used as-is and must exist in the webapp. Absent = boot straight into `webpage`. |
| `sfoVersion` | `sfo_version` | `01.01` | PARAM.SFO format version, must match `##.##`. |
| `buildVersion` | `build_version` | `0.4.4.5` | Additional build version string written to the SFO. |
| `debug` | - | production | `"yes"`/`"true"`/`1` builds the debug overlay in; `"false"`/`0` forces production; an explicit `--debug` on the command line overrides this key (78). |

## `wiiu` section

Console-specific fields plus overrides of the root keys
(`name`, `shorthand`, `version`, `author`, `webappDirectory`, `webpage`).

| Key | Aliases | Default | Used for |
|---|---|---|---|
| `shorthand` | `shortName`, ... | `CWV` | RPX `short_name` in `meta.xml`. |
| `icon` | `iconPath`, `icon_path` | `wiiu/icon.png` | SD menu icon (128x128 PNG), webapp-relative. |
| `description` | `longDescription`, ... | - | `meta.xml` `long_description` (app listing). |

## `wii` section

Full build since 2026-10-03 (HBC SD package). Console-specific fields
plus overrides of the root keys.

| Key | Aliases | Default | Used for |
|---|---|---|---|
| `icon` | `iconPath`, `icon_path` | `assets/icon-wii.png` | Homebrew Channel `icon.png`, webapp-relative. |
| `cursor` | `cursorPath`, `cursor_path` | `assets/cursor-wii.png` | DUAL-PURPOSE (78): `false` = the feature flag below; a string = cursor ART (webapp-relative, staged as `assets/cursor-wii.png`). |
| `name` | `appName`, `title` | root `name` | HBC `meta.xml` `<name>`. |
| `shorthand` | `shortName`, ... | root `shorthand` | Informational on Wii (HBC meta.xml has no short field). |
| `version` | `appVersion`, ... | root `version` | HBC `meta.xml` `<version>`. |
| `author` | `publisher`, `developers` | root `author` | HBC `meta.xml` `<coder>`. |
| `description` | `shortDescription`, ... | - | HBC `meta.xml` `<short_description>` (long_description stays engine-fixed). |
| `webpage` | `webPage`, `entrypoint`, ... | `index.html` | Entry page `main.c` opens (passed as `CWV_WII_WEBPAGE`). |
| `webappDirectory` | `webapp_directory` | `webapp` | Webapp directory staged as `web/` (minified, like the other consoles). |

## Feature keys (per console section: `vita`, `wiiu`, `wii`)

Project feature keys (`cwv_project.h`, (39)): `false` disables the feature
at compile time on that console; absent = enabled (`true`).

| Key | `false` disables |
|---|---|
| `cursor` | Pointer cursor feature entirely (art, draw, input). On `vita`, `wiiu` and `wii` a STRING value is NOT a feature flag but cursor ART (webapp-relative). |
| `zoom` | Zoom actions/keys + zoom settings rows. |
| `scroll` | Page scrolling (overflow hidden forced after the page CSS, scroll settings rows vanish). |
| `devtools` | DevTools menu entry + action. |
| `history` | History back/forward. |
| `files` | (84) Files menu (web-content tree browser; `vita` only). The main-menu Files button, the MENU_FILES mode and its actions compile out; the button is also removed from CWMenu scripts via `when files`. |

## Asset override conventions (79)

EVERY packaging asset can be overridden from the webapp project, with or
without manifest keys. Resolution order per console: manifest key ->
webapp convention -> repo default.

| Asset | Manifest key | Webapp convention | Repo default |
|---|---|---|---|
| Vita icon0.png | `vita.icon` | `webapp/vita/icon.png` | `sources/vita/assets/default_icon.png` |
| Vita LiveArea wallpaper | `vita.wallpaper` | `webapp/vita/wallpaper.png` | `sources/vita/assets/default_wallpaper.png` |
| Vita start logo | `vita.startLogo` | `webapp/vita/start-logo.png` | `sources/vita/assets/default_start_logo.png` |
| Vita LiveArea template | `vita.liveareaTemplate` | `webapp/vita/template.xml` | `sources/vita/assets/template.xml` |
| Vita cursor art | `vita.cursor` (string) | `webapp/vita/cursor.png` | `assets/cursor-vita.png` |
| Wii U icon | `wiiu.icon` | `webapp/wiiu/icon.png` | `assets/icon.png` |
| Wii U cursor art | `wiiu.cursor` (string) | `webapp/wiiu/cursor.png` | `assets/cursor-wiiu.png` |
| Wii icon | `wii.icon` | `webapp/wii/icon.png` | `assets/icon-wii.png` (if present) |
| Wii cursor art | `wii.cursor` (string) | `webapp/wii/cursor.png` | `assets/cursor-wii.png` |
| Vita default launcher | `vita.launchPage` (empty) | - | generated from `sources/vita/assets/default_launcher.html` |

Windows has no packaging art (the exe uses the native cursor and no
console shell), so there is nothing to override there.

## Worked example

`webapp/package.json` in this tree demonstrates EVERY key above with the
build's default values (validate any change against
`webapp/cwebview.schema.json`; `launchPage: ""` there intentionally turns
the generated default launcher on for the example build).
