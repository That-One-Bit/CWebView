# Piskel-Sharp / Vita3K crash triage (2026-09-30)

## What the supplied trace establishes

Vita3K v0.2.1 4096-ca2daeae loaded eboot.bin, CwvScroll.suprx and
CwvESM.suprx. Both optional modules returned zero from module_start.

(2026-10-03 note: CwvESM.suprx no longer exists - the Vita now uses the
shared built-in transformer like every other port, so the stubbed-NID
failure mode below can no longer affect ES6 pages. CwvScroll.suprx
remains.)
The application subsequently opened index.html and img/loading_bg.png.
The last image-open record is at 21:31:52.525; at 21:31:52.724 Vita3K
reported a host EXCEPTION_ACCESS_VIOLATION write at 0x47FF79F60.
There is no native stack, guest faulting PC, PNG metadata or JS exception
in the excerpt. The last file-open record does not establish that PNG
reading, decoding or texture upload completed. It cannot identify the
faulting function, prove a CwvESM regression or prove an emulator-only bug.

## Confirmed source defects found during inspection

- build-vita.sh printed its own Built target line after CMake had already
  printed target completion. The script now reports VPK validation/copy,
  leaving the target-completion line to CMake.
- Local image input could reach magic-byte inspection without the minimum
  eight-byte check already applied to remote input. One-byte local files
  made data[1..3] accesses out of bounds. A common pre-decode guard fixes this.
- PNG dimension rejection happened after native decoding/upload. Oversized
  IHDR dimensions are now rejected first, before the native API is called.
- The Vita PNG buffer wrapper ignores its length parameter. The shared PNG
  preflight now checks signature/IHDR, chunk bounds, IDAT and IEND presence.
  This mitigates malformed-container input, but does not replace a bounded
  libpng reader or validate compressed pixels, CRCs and all PNG semantics.
- The speculative retry with the PNG signature removed was not a safe
  portable loader contract. Removed for both normal and broken-image paths.
- A full image cache was detected only after another texture allocation.
  It is now refused before decoding. Existing cached URLs still return first.

None of these defects has been demonstrated to be the cause of this specific
Piskel-Sharp crash: the supplied dev build and image are not in this workspace.
Valid-image decoder bugs, GPU allocation failures, malformed compressed pixels,
other native memory bugs and Vita3K problems remain possible.

## Other trace messages

- openssl.cnf is looked up at an inappropriate embedded SDK-install path
  under app0. This is a packaging/runtime configuration issue; the program
  continued to load modules and the page afterward. It is not shown to be fatal.
- UNKNOWN_LW_MUTEX_ID is a real synchronization error worth investigating.
  Later application activity still occurs. This excerpt lacks the caller and
  therefore cannot attribute it to CwvESM, graphics or emulator startup.
- Missing settings.cfg and data-dir cwmenu/default.cwm are consistent with
  absent per-app overrides. The packaged app0:/cwmenu/default.cwm opens afterward.
- Stubbed firmware imports and the shader-interlock warning are emulator
  limitations/diagnostics. They are not sufficient evidence of this crash's cause.
- Module load/start success does not establish working exported NIDs or the
  application's ESM chain. The trace records only CWV.log write sizes, not its
  self-test verdicts. Read that log before concluding that ESM was healthy.

## Follow-up evidence

Use the full ux0:data/CWV.log, the crashing packaged dev build (or a minimal
reproducer), the exact img/loading_bg.png and a native Vita3K crash stack/dump.
The new runtime IMG PNG decode begin/end records include URL, dimensions and
encoded size and can distinguish rejection, decoder entry and normal return
when reading reaches that point. They do not instrument an earlier file-read
failure. Console/hardware reproduction is still needed; no VitaSDK or Vita3K
execution is available in this sandbox.
