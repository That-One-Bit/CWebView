# Shared source layout

CWebView keeps all console-neutral engine implementation under this directory.
The `sources/` tree is organized by responsibility so web-language code does
not get mixed with the renderer core, menu DSL, or platform seam.

```text
sources/shared/sources/
├── core/
│   ├── cwv_backend.*          # resources, drawing, input, public engine API
│   ├── cwv_bmp.*              # BMP decoder
│   ├── cwv_broken_image.c     # built-in broken-image asset
│   ├── cwv_cursor.*           # shared cursor policy
│   ├── cwv_cwi.*              # CWI image container decoder
│   ├── cwv_host.*              # host/app loop integration
│   ├── cwv_platform.h         # ONLY console seam
│   ├── cwv_project.h          # project/feature configuration
│   └── cwv_renderer.h         # renderer-facing types and declarations
├── menu/
│   ├── cwmenu_api.*           # CWMenu DSL parser
│   ├── cwv_menu_ui.*          # shared native menu UI
│   ├── native_menu_templates.*
│   └── scripts/*.cwm          # declarative CWMenu scripts
├── web/
│   ├── css/
│   │   └── cwv_css.c          # CSS parser/property registry
│   ├── html/
│   │   └── cwv_html.c         # HTML parser/tag registry/layout
│   ├── js/
│   │   ├── modern/            # CwvESM ES2015+ transform and ES edition libs
│   │   └── runtime/           # JS glue + vendored Duktape runtime
│   └── typescript/
│       └── cwv_ts_core.*      # TypeScript-to-JS stripping/preprocessing
└── cwv_version.h              # shared version fallback
```

The `web/js/modern` directory is the source-level home of the single
CwvESM pipeline. `web/js/runtime/duktape` is vendored third-party runtime
code and is kept separate from CWebView's JavaScript glue.

Build scripts should reference the grouped paths directly. The shared compile
gate recursively checks first-party `.c` files, and the Vita source-wiring gate
ensures every shared implementation is present in the Vita CMake target unless
it is deliberately excluded with an explicit whitelist.

Headers are included using paths relative to `sources/shared/sources`
(e.g. `core/cwv_renderer.h` or `web/js/modern/cwv_es_libs.h`). This keeps
include resolution deterministic after the source tree is reorganized.
