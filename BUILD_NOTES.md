> **Historical note:** this file contains older build milestones and
> experiments. References below to WebKit or a standalone `CwvESM.suprx`
> describe pre-2026-10-03 architecture and are not the current runtime
> design. For the current layout, use `HANDOFF.md` and `docs/SOURCE_MAP.md`.
>
# Build notes

The wrapper build is now generic rather than Piskel-specific.

`sources/build-vita.sh` accepts interactive values for the application name, title
ID, shorthand, version, author, native target, and webapp directory. The
selected webapp is packaged below `web/` in the VPK.

The CMake build explicitly creates `eboot.bin` with `vita-make-fself` and passes
that real file to `vita-pack-vpk`. This avoids SDK-version-dependent target
/byproduct behavior in `vita_create_vpk()`.

The build script validates the required package components:

- `eboot.bin`
- `sce_sys/param.sfo`
- `web/index.html`
- `sce_module/CwvESM.suprx` (generic optional ES6 extension)

The current WebKit work is intentionally staged. A successful build does not
yet mean that the private 3.65 WebKit/VSL ABI has been implemented; the host
must not claim `READY` until the actual page/navigation/render callbacks are
connected.


The ES6 extension is packaged as an application user module under `sce_module/`.
This is deliberately singular: Vita application module paths conventionally use
`app0:sce_module/...`; `sce_modules/` is not the standard application module
directory. The extension is not automatically loaded yet because its host ABI
must first be connected to the real WebKit JavaScript bridge.

The default backend now accepts a valid webpage URL and draws a native diagnostic
page. This verifies the wrapper lifecycle and vita2d output on hardware without
claiming that HTML has already been rendered.

## Stage 10: display/log isolation diagnostic

This stage deliberately tests the Vita display and filesystem before vita2d or the web backend. On launch it shows three solid color bands for about two seconds and writes the earliest possible log to `ux0:data/CWV.log`. It then creates `ux0:data/CWebView/` with `sceIoMkdir` and continues to the normal vita2d/backend path. Press START during this standalone boot diagnostic to exit; once a webpage is running, START is reserved for page input and no longer quits the app.

If the color bands appear but `ux0:data/CWV.log` does not, the filesystem path/permissions need investigation. If neither appears, the process is not reaching the first display diagnostic and we should stop changing WebKit/backend code and inspect the app entry/SELF/runtime instead.


## CWebView 0.2.0 renderer milestone

The native renderer now resolves a separate webpage entry point, parses basic
CSS rules, loads external stylesheets and images, performs simple block/inline
word wrapping, and supports `@font-face` local and HTTP(S) font sources.
Custom fonts are loaded through vita2d's font-file path; remote fonts are
downloaded and cached under `ux0:data/CWebView/`.

### CwvScroll stub-generation PATH

The build script exports `$VITASDK/bin` before CMake configuration. This is required because VitaSDK's `vita_create_stubs()` generates a nested Makefile that invokes tools such as `arm-vita-eabi-as` by name. The nested make process must inherit the VitaSDK `bin` directory through `PATH`.

## 0.4.31 verification

- Source delimiter/brace balance checked after the select and double-border renderer changes.
- Archive contents verified with `unzip -t`.
- VitaSDK compilation still requires the user WSL/VitaSDK environment.

## Vita shared-transformer portability regression (2026-09-30)

Run `bash desktop_test/build_es6_portability_gate.sh` before a Vita build.
It checks the shared JS transformer under strict C99 and C11 with all
`-Wall -Wextra` warnings treated as errors. Owned string copies use the local
`dupn` allocator returning `char *`; no POSIX `strdup` prototype or feature
macro is needed. This gate is host-only, not a replacement for
`./build-vita.sh -y` with the real VitaSDK.


## Building against a specific web-content directory

The root dispatcher accepts `--webapp DIR` (or `--webapp=DIR`) for every platform. It overrides the manifest's `webappDirectory` and the platform's default content directory for that build only; it does not edit `package.json`.

For the experimental Canvas 2D smoke page on Windows, run from the repository root:

```sh
./build.sh windows --webapp experimental/liff-port/web
```

Add `-y` only if you want the Windows build to accept manifest settings non-interactively. For this experimental fixture, the Windows build also stages the sibling `experimental/liff-port/package.json` beside each executable so the runtime can read its `cwv-args` opt-ins. This is test packaging only; it does not make WebGL available.
