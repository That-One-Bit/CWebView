# Source map - where is what

The renderer is deliberately organized like a small Python package: one
"shared definitions" header (the dataclasses / __init__), and one file per
concern, each driven by plain C tables that read much like Python dicts.

## The shared engine (`sources/shared/sources/`)

The source tree is organized by responsibility rather than by historical
split-file names:

| Directory/file | Role |
|---|---|
| `core/` | Shared renderer/backend types, host integration, platform seam, image codecs, cursor and project configuration |
| `web/html/cwv_html.c` | `TAG_TABLE[]`, HTML parsing and layout |
| `web/css/cwv_css.c` | `CSS_PROPERTIES[]`, CSS parsing/property handling |
| `web/js/runtime/cwv_js.c` | JavaScript API, DOM/event/timer glue and Duktape integration |
| `web/js/runtime/duktape/` | Vendored Duktape 2.7.0 runtime; do not edit as CWebView engine code |
| `web/js/modern/` | CwvESM ES2015+ transformer and ES2015-ES2024 library preludes |
| `web/typescript/cwv_ts_core.c` | TypeScript preprocessing/stripping before modern-JS transformation |
| `menu/` | CWMenu DSL parser, native menu UI/templates and `.cwm` scripts |

To add a CSS property or HTML tag, add the corresponding table entry in the
CSS/HTML implementation (see `docs/EXTENDING_THE_RENDERER.md`). Modern
JavaScript changes belong in `web/js/modern/`; do not create another ESM
provider module.

## The app/port side

| Location | Role |
|---|---|
| `sources/vita/main.c` | Vita app loop, native menus, input, power suspend/resume callbacks, startup and feature wiring |
| `sources/vita/cwv_platform.c` | Vita implementation of the shared platform seam |
| `docs/VITA_LIFECYCLE.md` | Vita suspend/resume design, source map, supported-method pointers, and hardware test checklist |
| `sources/vita/scroll_module/` | Separate `CwvScroll.suprx` input/scroll module |
| `sources/wii/main.c`, `sources/wii/cwv_platform.c` | Wii application and libogc/GRRLIB platform |
| `sources/wiiu/main.c`, `sources/wiiu/cwv_platform.c` | Wii U application and wut/SDL2 platform |
| `sources/win/main.c`, `sources/win/cwv_platform.c` | Windows application and Win32/GDI/WIC/WinHTTP platform |
| `sources/shared/sources/menu/` | Console-neutral CWMenu implementation; individual ports decide which UI path they use |

There is no separate `CwvESM.suprx` module in the current architecture.
CwvESM is the single shared modern-JS pipeline compiled directly into each
port.

## Tests on a PC (gcc only)

    cd desktop_test
    ./build_and_compare.sh original_cwv_backend.c
    ./build_js_test.sh
    ./build_es2015_test.sh
    ./es2015_unit/build_unit.sh && ./es2015_unit/unit

The desktop suite also contains platform-stub gates, source-wiring checks,
image/CWI tests, TypeScript tests and portability checks. See
`desktop_test/run_all_gates.sh` for the authoritative gate order.

## Build targets

The CMake build produces the PS Vita VPK. CwvESM is compiled directly into
the application; only the separate Vita scroll module is staged as
`CwvScroll.suprx`.
