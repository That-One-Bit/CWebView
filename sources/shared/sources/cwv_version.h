#ifndef CWV_SHARED_VERSION_H
#define CWV_SHARED_VERSION_H
/* CWebView version metadata (81).  The Vita build generates its own
 * cwv_version.h from sources/vita/cwv_version.h.in (CMake configure_file)
 * with the same macro names; this shared copy is the fallback the Wii,
 * Wii U, Windows and desktop-gate compilations resolve instead, and it
 * mirrors the CMakeLists.txt cache defaults so both paths agree. */
#ifndef CWV_PRODUCTION_VERSION
#define CWV_PRODUCTION_VERSION "0.1.1"
#endif
#ifndef CWV_BUILD_VERSION
#define CWV_BUILD_VERSION "0.4.4.5"
#endif
#define CWV_VERSION_DISPLAY CWV_PRODUCTION_VERSION " - " CWV_BUILD_VERSION

/* The APP's own version (package.json "version", e.g. "00.10"), stamped
 * in by every build script via -DCWV_APP_VERSION so the menu can show it
 * beside the internal CWebView version (user direction, 2026-10-07). */
#ifndef CWV_APP_VERSION
#define CWV_APP_VERSION "0.0"
#endif
#endif
