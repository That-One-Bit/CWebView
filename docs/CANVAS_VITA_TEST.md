# Testing the experimental Canvas 2D subset

CWebView's Canvas 2D implementation lives in the shared HTML/JavaScript renderer and is available on the Vita, Wii U, Wii, and Windows backends. It remains **opt-in** through `beta-enable-canvas`; it is not a complete browser Canvas implementation.

## Prepare the web content

1. Copy `docs/canvas_vita_smoke.html` into the packaged content directory as `web/canvas-vita-test.html` (the filename is historical; the page is cross-platform).
2. Ensure `web/package.json` contains the `cwv-args` property with the value `beta-enable-canvas`. Merge the property into the existing JSON object rather than replacing other metadata:

   ```json
   {
     "cwv-args": "beta-enable-canvas"
   }
   ```

3. Build and install the platform package with this page as its entry page, or open the page from CWebView's Files browser.
4. Check the platform's `CWV.log` for `cwv-args: beta-enable-canvas enabled`.

The runtime checks both `<app-dir>/package.json` and `<app-dir>/web/package.json`. The test page and manifest should be inside the packaged `web/` directory for the usual application layout. Canvas access remains disabled if the manifest flag is absent or invalid.

## What the page tests

The first canvas tests colored rectangles, alpha compositing, clearing, rectangle outlines, path filling, `rect()` and saved-state restoration, quadratic and cubic Bézier curves, arcs, and context identity. The second canvas is intentionally placed after a tall spacer and redraws continuously. Scroll through it and check that:

- the canvas follows its normal document position rather than sticking to the screen;
- partial visibility at the viewport edges does not produce jumps or obvious drawing outside the canvas;
- drawing remains intact when scrolling away and back;
- repeated redraws continue after the canvas is scrolled into view;
- the page remains responsive and does not flicker or crash.

This visual test cannot prove pixel-perfect browser parity. Please record the platform, whether the canvas was partially clipped at the viewport edge, whether the moving bar continued, and any relevant log entries.

## Current implementation and limits

Implemented context methods: `getContext("2d")`, `fillRect()`, `strokeRect()`, `clearRect()`, `beginPath()`, `moveTo()`, `lineTo()`, `closePath()`, `rect()`, `save()`, `restore()`, `quadraticCurveTo()`, `bezierCurveTo()`, `arc()`, `fill()`, and `stroke()`.

Implemented context properties: `fillStyle`, `strokeStyle`, `lineWidth`, and `globalAlpha`. `save()`/`restore()` preserve these supported properties only; transforms, clipping regions, and compositing modes are not implemented. Assigning the canvas `width` or `height` resets its bitmap, supported context properties, and saved-state stack. Canvas backing dimensions are bounded to 1024 × 768.

The current path implementation uses bounded line-segment approximations for curves, a simple even-odd fill rule, and software rasterization. It does not yet implement transforms, text, gradients, patterns, image drawing, pixel reads/writes, clipping paths, line caps/joins, dashes, compositing modes, full browser compositing semantics, or WebGL. Alpha is composited into the software backing store using CWV's current pixel-compositing approach; it is not a complete implementation of browser compositing semantics.

## Platform implementation notes

The DOM, JavaScript API, path rasterizer, and RGBA backing store are shared. Platform backends provide `cwv_tex_load_rgba_buffer()` for texture upload:

- Vita: Vita2D texture upload with padded texture width.
- Wii U: SDL RGBA texture upload.
- Wii: RGBA8 texture conversion for GX/GRRLIB.
- Windows: software texture buffer used by the GDI framebuffer renderer.

Having an upload implementation does not replace physical-device validation. The desktop Canvas regression gate exercises the shared JavaScript-to-RGBA backing store; it does not test the Vita2D, SDL, GX, or Windows display paths. In particular, the scroll/redraw test should be run on each physical console where possible.

## Automated regression test

Run `bash desktop_test/build_canvas2d_test.sh`. It runs the Canvas fixture against both supported manifest fixture layouts and checks selected pixels in the shared backing store. Platform-specific builds are separate and require the corresponding toolchains.


## Performance note

Canvas remains opt-in with `beta-enable-canvas`. Dirty frames now update the existing texture rather than destroy/recreate it each frame. The current path still uploads the full canvas backing bitmap when any drawing command marks it dirty; partial dirty-rectangle uploads are not yet implemented. Compare animation smoothness and memory stability on target hardware before release.
