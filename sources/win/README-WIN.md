# CWebView for Windows (x86, AMD64, ARM32, ARM64)

## Text rendering parity (2026-10-03)

The engine's fixed calibration: CSS font sizes live in renderer units,
the @font-face seams are called with pixels pre-multiplied at **32 px
per unit**, and the **line box is 28 px per unit** (see `measure_text_width`
/ `line_height` in `cwv_html.c`).  A font whose glyph CELL height (ascent
+ descent + internal leading) is at most the line box fits its element -
that is what the Vita PGF and the Wii U TTF (`WIIU_PGF_PX_PER_UNIT` /
`WIIU_FONT_FIT`) calibrations produce.

GDI originally drew the engine's 32-px-per-unit size as the EM size
verbatim (`CreateFontW(-px)`), so the cell measured ~36 px per unit
against a 28 px line box: text overflowed every container.  The port now
measures each face's real cell ratio once (`GetTextMetrics` probe at a
100 px em) and scales the em so the CELL lands at 28 px per unit
(`font_fit` in `cwv_platform.c`).  Measure and draw share the factor, so
wrapping measures exactly what is drawn.  The system-font (pgf) seam now
also follows the engine's 32 px per unit contract instead of the old
fixed 24 px - CWMenu text is visually unchanged (~24 px at z=1).

First cut of the native Windows port (2026-10-01). No third-party
dependencies: Win32 + GDI for the window/frame, Windows Imaging
Component for image decode, GDI for text, WinHTTP for `fetch`.

## Controls (PC grammar, 2026-10-05)

The Windows port uses PC-native controls - it mirrors the console
controls where that makes sense and drops what does not:

| Input | Action |
|---|---|
| Mouse move | pointer (the system cursor, CSS-aware: links get the hand, text fields the I-beam, `cursor: none` hides it) |
| Left click | activate the element under the pointer |
| Right click | open/close the CWMenu |
| Wheel | vertical page scroll (menus scroll too) |
| Tilt wheel / touchpad horizontal | horizontal page scroll |
| **Ctrl+wheel / Ctrl+= / Ctrl+- / Ctrl+0** | zoom in / in / out / reset (75%-200%, saved in Settings) |
| **LShift+RShift+Space** or **F1** | open/close the CWMenu (the PC stand-in for the console L+R+SELECT hotkey) |
| **F11** | toggle borderless fullscreen / restore the window |
| Arrow keys | page scroll; menu navigation when the CWMenu is open (D-pad) |
| PgUp/PgDn | page-scroll by a page (L/R shoulder) |
| Home | scroll to top |
| Tab | cycle page focus |
| Return or Space | activate the focused element; confirm in the menu |
| Escape | clear focus; close the menu |
| F5 or Ctrl+R | reload the current page |

The pointer cursor is the OS cursor: the engine resolves the CSS hover
style (`cwv_host_cursor_at`) and `WM_SETCURSOR` picks the matching
system cursor shape (the console art-cursor path is console-only - it
needs on-screen drawing, which Windows does not do).  `cursor: none` is
honored, and the Settings "CSS cursors" toggle reverts to the plain
arrow when off.

## Layout

The app root is the folder `CWebView.exe` sits in; `web/` lives next to
the executable (same layout as the Wii/Wii U SD packages). The runtime
log is app-specific: `<exe folder>/CWV.log` (disabled in debug builds,
see below).

## Build

The selected architectures build from the same C11 sources:

    ./build.sh windows -y

Set the architecture list in `webapp/package.json`. The build accepts either
root-level `windowsArchitectures` or nested `windows.architectures`:

```json
{
  "windowsArchitectures": ["x64", "arm64", "x86", "arm32"]
}
```

Supported values and compiler variables:

- **x64 / AMD64**: `x86_64-w64-mingw32-gcc`; override with `X64_CC`.
- **x86 / 32-bit x86**: `i686-w64-mingw32-gcc`; override with `X86_CC`.
- **arm64 / ARM64**: `aarch64-w64-mingw32-clang` from llvm-mingw; override with `ARM64_CC`.
- **arm32 / 32-bit ARM**: `armv7-w64-mingw32-clang` from llvm-mingw; override with `ARM32_CC`.

If the key is absent, the default remains `["x64", "arm64"]`. Architecture names
are case-insensitive and aliases such as `amd64`, `x86_64`, `i686`, `aarch64`,
`arm`, and `armv7` are accepted. An unavailable non-x64 toolchain is reported
and skipped; if x64 is selected but its compiler is missing, the build fails
with an install/configuration hint. If none of the requested architectures can
be built, the build fails instead of reporting success.

The compiler must run on the host machine while targeting the architecture in
the manifest. An x64 Linux/WSL host can use LLVM-MinGW's Linux x86_64-hosted
package to cross-compile ARM64 Windows binaries; no ARM64 PC is needed. Set the
corresponding `*_CC` variable if a compiler is not on `PATH` or in
`/opt/llvm-mingw/bin` / `/usr/local/llvm-mingw/bin`.

### Installing the ARM64 cross-compiler from an x64 PC

You do **not** need an ARM64 computer. A cross-compiler runs on your x64
build machine and emits ARM64 Windows binaries. Download the **Windows x86_64
host** portable archive from the [official llvm-mingw releases](https://github.com/mstorsjo/llvm-mingw/releases),
extract it to a stable directory such as `C:\llvm-mingw`, and make sure its
`bin` directory contains `aarch64-w64-mingw32-clang.exe` (or the matching
compiler executable shipped by that release).

For this project's MSYS/Linux-like build shell, the easiest layout is to
extract the **Linux x86_64-host** archive into `/opt/llvm-mingw`, so the
compiler is under `/opt/llvm-mingw/bin`. If you build inside WSL/MSYS and
have the Windows archive extracted on the Windows filesystem, point the
build script at a compiler executable that is runnable from that shell:

```bash
export ARM64_CC=/opt/llvm-mingw/bin/aarch64-w64-mingw32-clang
./build.sh windows -y
```

For a Windows-host compiler, add its `bin` folder to Windows `PATH` and run
the build in a compatible Windows shell; a Windows `.exe` compiler cannot
be invoked directly as a Linux ELF executable inside WSL. Verify discovery
with `command -v aarch64-w64-mingw32-clang` (Linux/WSL) or
`where aarch64-w64-mingw32-clang.exe` (Windows). If the compiler is absent,
the script intentionally skips ARM64 and still builds x64.
- MSVC: compile `sources/win/*.c` + the shared engine units with
  `cl.exe` and link `gdi32.lib user32.lib shell32.lib ole32.lib
  windowscodecs.lib winhttp.lib` (x64 and ARM64 targets both work).

Output (unified build tree, 2026-10-02):
`build/windows/<architecture>/CWebView.exe` for each selected and available architecture, each with the minified webapp
staged as `web/` NEXT TO the executable (the exe loads
`<app dir>/web/index.html`).

The desktop gate `desktop_test/build_win_gate.sh` cross-builds both and
verifies each PE's machine type; it SKIPs cleanly on machines without
the cross toolchains.

## Input mapping (first cut)

| Input | Action |
|---|---|
| mouse move / left button | pointer (touch) events |
| wheel, Up/Down, PgUp/PgDn | page scroll |
| Tab | D-pad focus cycle |
| Enter | activate focused control (opens the text dialog) |
| Esc | clear focus |
| F5 | reload current URL |

## ES6+

The built-in transformer is wired the same way as Wii/Wii U
(`cwv_host_enable_es_builtin`); the self-test result lands in CWV.log.

## Known limits (first cut)

- CWMenu (the console menu shell) is not integrated; the port boots
  straight to `web/index.html`.
- Text dialog is a small Win32 window, not a per-platform IME service.
- Scrolling is page-level only; no zoom gestures.
- The renderer currently uses a 960x544 CPU/GDI software framebuffer; F11
  enters borderless fullscreen and Windows DPI bitmap virtualization is
  disabled. The presentation uses point sampling rather than HALFTONE, so
  scaled output stays crisp rather than blurred, but true per-pixel native
  resolution rendering and GPU acceleration are not implemented yet.
