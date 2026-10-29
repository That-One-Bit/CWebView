# PS Vita suspend and resume lifecycle

This note documents CWV's Vita-specific lifecycle handling and the current
JavaScript/HTML method subset. It is a source guide, not a claim that every
behavior has been validated on physical hardware.

## Relevant source files

- `sources/vita/main.c` — Vita input/render loop and power callback handling.
- `sources/vita/cwv_platform.c` — Vita graphics, files, and platform seams.
- `sources/shared/sources/core/cwv_host.c` and `cwv_host.h` — page lifecycle,
  updates, navigation, input dispatch, and shared host API.
- `sources/shared/sources/web/js/runtime/cwv_js.c` — registered JavaScript
  host methods and `setTimeout` / `setInterval` scheduling.
- `sources/shared/sources/web/html/cwv_html.c` — registered HTML tag table.
- `sources/shared/sources/web/css/cwv_css.c` — registered CSS property table.
- `docs/SUPPORTED_METHODS.md` — user-facing list of supported HTML/CSS/JS and
  language features; this is the best starting point for building test pages.
- `docs/EXTENDING_THE_RENDERER.md` — architecture and extension guide.

## Vita lifecycle behavior

CWV registers a user-mode `ScePower` callback from a dedicated callback thread.
The callback only updates flags; it does not call CWV, Vita2D, or logging code.
The main thread observes those flags and:

1. Stops page updates, input processing, drawing, and buffer swaps while the
   OS reports the app suspended.
2. Cancels pointer input and sends release events for known CWV button names to
   reduce stuck pointer/button states across the transition.
3. On resume, reasserts controller and front-touch sampling, clears stale touch
   and edge-detection state, and returns to the regular render loop.
4. Logs callback registration and suspend/resume transitions to `CWV.log`.

The JavaScript timer queue is only ticked from the normal host update loop.
CWV therefore does not run callbacks while its render/update loop is paused.
The scheduler uses C `clock()` time; confirm on hardware whether the particular
Vita runtime's process clock pauses as expected during system sleep. JavaScript
`Date` is wall-clock based and may naturally advance while the console sleeps.

## Important limits / test items

- The implementation is not yet certified on a physical Vita or Vita3K.
- Callback delivery and the exact event sequence should be checked on hardware.
- This first pass preserves the existing Vita2D renderer rather than destroying
  and rebuilding it during every resume. If the GPU context or textures are
  invalidated on the target firmware, add a tested renderer recovery path.
- The optional Bluetooth HID and scroll module behavior after sleep needs
  hardware testing. Controller/touch sampling is reasserted, but device-specific
  reconnection behavior is not guaranteed by that call alone.
- Test repeated suspend/resume, suspend while touch/button input is held,
  suspending from a native dialog, long sleeps, and a page with active timers.

## Supported page APIs

For the actual supported subset, use `docs/SUPPORTED_METHODS.md`. In brief,
CWV has a small DOM-like API (`getElementById`, `querySelector`,
`querySelectorAll`, event listeners, text/value/style accessors), timer methods,
console methods, a host-backed GET `fetch`, limited navigation, and a partial
modern-JavaScript transformation pipeline. This is not full browser DOM, Fetch,
Canvas, WebGL, or Worker support by default; Canvas is separately opt-in and
its supported methods are documented in `docs/CANVAS_VITA_TEST.md` and the
shared Canvas implementation.
