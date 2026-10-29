# Port roadmap

Status of every CWebView platform. The seam (`sources/shared/
cwv_platform.h`) is what a port implements: files, textures, fonts,
primitives, IME, fetch. The Windows port (2026-10-01) proved the seam
covers a full desktop OS, which derisks the ports below.

## Current platforms

| Platform | Status | Image decode | Notes |
|---|---|---|---|
| PS Vita | shipping | libvita2d PNG/JPEG/BMP | built-in transformer (shared) |
| Wii U | shipping | SDL2_image PNG/JPEG/BMP | built-in ES6 transformer |
| Wii | shipping | GRRLIB PNG/JPEG + shared BMP decoder (2026-10-01) | BMP was silently unsupported before |
| Windows x64 | first cut (2026-10-01) | WIC (PNG/JPEG/BMP/TIFF/GIF) | `sources/win`, cross-gated |
| Windows ARM64 | first cut (2026-10-01) | WIC | same sources, llvm-mingw |
| Desktop stubs | test gates | stub (counting) | unit/integration gates |

## Keyboard + mouse (KBM) input

Ports take the keyboard/mouse their platform can offer, with one shared
key grammar (F1 menu, arrows scroll / menu-nav, PgUp/PgDn page scroll,
Home top, Tab focus cycle, Return activate, Escape clear, F5 reload):

| Platform | Path |
|---|---|
| Windows x64/ARM64 | native Win32 messages; tilt-wheel horizontal scroll |
| PS Vita | Bluetooth HID via SceHid (`cwv_kbm_vita.c`); keyboard synthesizes controller bits, mouse drives the virtual cursor (left tap, wheel scroll, right menu) |
| Wii | USB keyboard via libwiikeyboard (`KEYBOARD_Init(NULL)`, no stdin takeover) |
| Wii U | USB keyboard + mouse via SDL2 (`SDL_KEYDOWN`, `SDL_MOUSEWHEEL`) |
| Desktop stubs | `build_vita_kbm_gate.sh` compiles the Vita KBM unit against VitaSDK-mirroring stubs |

Image verification: the desktop gates cover the shared loader + BMP
decoder behavior (`build_img_cache_test.sh`, `build_bmp_unit.sh`); each
port's decoder is compile-gated (`build_wii_gate.sh`,
`build_wiiu_gate.sh`, `build_win_gate.sh`); Vita/Vita3K runtime evidence
comes from CWV.log IMG lines. Console/hardware image rendering still
needs device runs - no console SDK runs in the desktop sandbox.

## Planned ports (user direction 2026-10-01)

### Android
- **Targets**: arm64-v8a, armeabi-v7a, x86_64 (emulator), same sources.
- **Stack**: NDK + ANativeActivity; software framebuffer blitted to an
  `ANativeWindow` (the Windows DIB code ports almost line-for-line).
- **Images**: AImageDecoder (API 24+) covers PNG/JPEG/BMP/WebP.
- **Text**: system fonts via AFont APIs (games-sdk) or a bundled
  FreeType; layout needs a ttf-family parser like the Win port.
- **Fetch**: HttpURLConnection through a JNI bridge (TLS cert store
  for free), or rustls later. IME: the soft-keyboard via JNI.
- **Blocker**: none known; the seam is proven. Largest piece is the
  JNI/host bridge and touch input plumbing.

### Switch (homebrew, libnx)
- **Targets**: aarch64 (AMS/atmosphere).
- **Stack**: devkitA64 + libnx; framebuffer via the display service,
  or SDL2 (already proven by the Wii U port) for the first cut.
- **Images**: stb_image or SDL2_image. Text: SDL2_ttf + FreeType, or
  the shared BMP path where applicable.
- **IME**: libnx swkbd service. Fetch: libcurl (atmosphere-nx curl).
- **Note**: console-only distribution via homebrew; no retail SDK.

### Unix (POSIX family: FreeBSD/OpenBSD and kin)
- **Stack**: same X11/POSIX layer as Linux (below), kept separate from
  Linux-specific pieces (epoll, /proc, udev) so BSD ports fall out of
  the same seam implementation.
- **Fetch**: libcurl. Images: SDL2_image or stb_image. Text: FreeType +
  fontconfig.
- **Blocker**: none technically; needs a CI runner or manual device
  verification per flavor.

### Linux
- **Targets**: x86_64 + aarch64, same sources.
- **Stack**: SDL2 (window/input) or raw X11; either links the shared
  engine unchanged. The software framebuffer from the Windows port is
  reusable as-is.
- **Images**: stb_image (no deps) or SDL2_image; BMP already decodes
  shared (`cwv_bmp.c`).
- **Text**: SDL2_ttf/FreeType + fontconfig. IME: ibus/fcitx via SDL2
  text input events.
- **Fetch**: libcurl. Packaging: .deb + AppImage.
- **Blocker**: none; the Win port removed the last seam unknowns. This
  is the recommended next port because every gate can run natively in
  the build sandbox.
