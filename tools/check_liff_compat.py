#!/usr/bin/env python3
"""Audit the isolated Lost in Firefly Forest fixture against current CWV APIs.

This is a compatibility gate, not a claim that the game is runnable. It makes
missing browser primitives visible and ensures the experiment remains outside
normal release web content.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "experimental" / "liff-port"
WEB = FIXTURE / "web"
ENGINE = WEB / "html5game" / "Lost.js"
README = ROOT / "README.md"

errors = []
notes = []
if not (WEB / "index.html").is_file():
    errors.append("fixture web/index.html is missing")
if not ENGINE.is_file():
    errors.append("fixture web/html5game/Lost.js is missing")
if (ROOT / "webapp" / "index.html").exists() and (ROOT / "webapp" / "index.html").resolve() == (WEB / "index.html").resolve():
    errors.append("fixture unexpectedly aliases the shipped webapp entry page")
if not (FIXTURE / "package.json").is_file():
    errors.append("experimental package.json is missing")
for required_asset in ("debug.html", "debug_loader.js", "modkit.js", "mods_bundled.js"):
    if not (WEB / required_asset).is_file():
        errors.append(f"fixture web/{required_asset} is missing")

if ENGINE.is_file():
    source = ENGINE.read_text(encoding="utf-8", errors="replace")
    canvas_contexts = sorted(set(re.findall(r"getContext\s*\(\s*['\"](2d|webgl2?|experimental-webgl)['\"]", source, re.I)))
    if re.search(r"\[\s*['\"]webgl['\"]\s*,\s*['\"]experimental-webgl['\"]", source, re.I):
        canvas_contexts.append("webgl-fallback-list")
    if "2d" in canvas_contexts:
        notes.append("GameMaker runtime requests Canvas 2D contexts")
    if any(x.lower().startswith("webgl") or x.lower() == "experimental-webgl" for x in canvas_contexts):
        notes.append("GameMaker runtime requests WebGL contexts")
    for token in ("document.createElement", "requestAnimationFrame", "localStorage", "AudioContext"):
        if token in source:
            notes.append(f"runtime references {token}")

manifest = (FIXTURE / "package.json").read_text(encoding="utf-8") if (FIXTURE / "package.json").is_file() else ""
if '"cwv-args": "beta-enable-canvas beta-enable-webgl"' not in manifest:
    errors.append("experimental cwv-args proposal missing or changed unexpectedly")
if not errors:
    print("LIFF compatibility fixture: PASS (isolated)")
    for note in notes:
        print(f"  REQUIREMENT: {note}")
    print("  CANVAS: opt-in static <canvas> supports getContext('2d'), width/height, fillStyle, globalAlpha, fillRect and clearRect.")
    print("  LIMITATION: dynamic createElement, paths, text, images, transforms, gradients, image-data and compositing modes are not implemented.")
    print("  WEBGL: context/API is not implemented; its switch remains diagnostic-only.")
    print("  CONFIG: package-root cwv-args is parsed once per app-root selection; flags are opt-in and do not change defaults.")
    print("  TEST: desktop_test/build_canvas2d_test.sh exercises the real JS-to-RGBA backing-store path.")
    print("  LIMITATION: this structural check does not execute the game or prove frame stability.")
    sys.exit(0)
for error in errors:
    print(f"FAIL: {error}", file=sys.stderr)
sys.exit(1)
