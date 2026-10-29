# Windows input and CWMenu smoke test

Run this after building Windows from the project root. Test x64 first; repeat on
ARM64 when a working ARM64 cross-compiler is available.

## Package checks

- Confirm each `build/windows/<arch>/` contains `CWebView.exe`, `web/`, and
  `cwmenu/default.cwm`.
- Launch the executable and check `CWV.log` for a successful CWMenu script load.
- Right-click and press `F1`; CWMenu should open and close without a script error.

## Pointer and button checks

- Move the pointer over a link without clicking. It must not navigate.
- Move over a button without clicking. It must not activate.
- Press and hold the left mouse button on a button. Its `:active` styling should
  be visible while held; release to activate once.
- Press on a button, drag off it, and release. It should not activate if released
  outside the button.
- Check a `button { display: block; }` with a known width/container. The control
  should use the available block width and its label should be centered.
- Verify links still show the expected CSS-aware cursor when CSS cursors are on.

## Scrolling checks

- Use a touchpad gesture that combines horizontal and vertical movement, or use
  a mouse with tilt-wheel support. Confirm both axes can move during one gesture.
- On Wii U, use a diagonal D-pad hat event in D-pad scroll mode and confirm both
  page offsets can change. This last check requires hardware/emulator input that
  reports diagonal hat values.

This is a smoke-test checklist, not proof of all native-platform input behavior.
The Wii/Wii U pointer stuck at the corner and a cross-platform manual cursor mode
remain separate hardware-dependent investigations.
