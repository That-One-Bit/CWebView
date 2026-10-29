# Lost in Firefly Forest — CWebView compatibility prototype

**Status: isolated research fixture; not a supported or release-ready port.**

The original built game page is staged in `web/` from the supplied modkit build. It is deliberately outside `webapp/`, so normal CWebView builds do not ship or expose it. The main CWebView README is intentionally unchanged.

## Package switches and current implementation

`package.json` records the opt-in list:

```json
"cwv-args": "beta-enable-canvas beta-enable-webgl"
```

The C runtime reads this root-level key from `package.json` when an app root is selected. `beta-enable-canvas` now enables an experimental, bounded Canvas 2D subset for static `<canvas>` elements: `getContext("2d")`, width/height properties, `fillStyle`, `globalAlpha`, `fillRect`, and `clearRect`. Pixels are stored in a capped RGBA backing buffer and uploaded through the existing platform texture seam only after drawing changes the buffer. Projects without the flag do not allocate Canvas backing buffers.

`beta-enable-webgl` is recognized but remains diagnostic-only because no WebGL context/API is implemented. Unknown switches are diagnosed. The manifest parser accepts a plain quoted, space-separated string; it is intentionally not a general JSON parser.

A cross-platform smoke page is available at `web/canvas2d-smoke.html`. The desktop regression test `desktop_test/build_canvas2d_test.sh` verifies the JS context, drawing, clearing, and pixel bytes.

## Compatibility findings

The supplied GameMaker HTML5 build expects substantially more than the current subset:

- `document.createElement("canvas")` and writable canvas width/height behavior;
- Canvas paths, text, images, transforms, gradients, image-data, clipping, compositing and many additional context properties;
- WebGL context creation and a large WebGL API surface, with some fallback logic;
- browser event dispatch, image loading, timers/frame scheduling, audio and storage behavior.

CWebView still does not implement dynamic `document.createElement`, Canvas 2D APIs beyond the subset above, or WebGL. The LIFF game is not expected to run yet. The implementation deliberately avoids returning a fake context for unsupported APIs; compatibility claims will follow actual regression coverage.

## Isolation and validation

- The fixture is not in the default `webapp/` path.
- No files in the root README were changed.
- `desktop_test/build_cwv_args_unit.sh` tests switch tokenization and manifest extraction.
- `tools/check_liff_compat.py` records the current required API surface and checks that the fixture stays isolated and complete. It does not execute the game or establish runtime stability.
