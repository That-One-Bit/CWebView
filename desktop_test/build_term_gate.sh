#!/usr/bin/env bash
# Build-script UX gate (2026-10-07 (80), moved paths (81)): locks the
# shared terminal library (tools/cwv_term.sh), the Wii link-flag
# construction and the build.sh dispatcher.
#   - color is OFF when piped (no stray escapes in logs) and honors NO_COLOR
#   - CWV_COLOR=always forces it on; palette meanings are stable
#   - features render as  <console> > features > key: state
#   - the Wii -L list is a clean array (no backslashes/quotes: the
#     "-L\/opt/devkitpro/...\" bug made every installed lib "not found")
#   - cwv_colorize classifies compile/link/warning/error lines
#   - build.sh resolves number/lowercase/uppercase targets to the right
#     script in sources/ and rejects unknown ones
set -euo pipefail
cd "$(dirname "$0")/.."
fail() { echo "TERM GATE FAILED: $*" >&2; exit 1; }

# 0. dispatcher (81)
[[ -x build.sh ]] || fail "build.sh is not executable"
for pair in "1:vita" "2:wii" "3:wiiu" "4:windows" "VITA:vita" "wii:wii" \
            "WiiU:wiiu" "Windows:windows" "vita:vita"; do
  q="${pair%%:*}"; want="${pair##*:}"
  got="$(bash build.sh --resolve "$q")"
  [[ "$got" == *"/sources/build-$want.sh" ]] || fail "build.sh '$q' -> '$got' (wanted $want)"
done
bash build.sh --resolve all >/dev/null 2>&1 && fail "build.sh --resolve all should not name one script"
bash build.sh --resolve bogus >/dev/null 2>&1 && fail "build.sh accepted 'bogus'"

# 1. piped => no escapes
out="$(bash -c 'source tools/cwv_term.sh; cwv_ok hi; cwv_features_tree wii "cursor:on zoom:off"' )"
[[ "$out" != *$'\033'* ]] || fail "escapes leaked into piped output"

# 2. NO_COLOR beats auto
out="$(NO_COLOR=1 bash -c 'source tools/cwv_term.sh; cwv_ok hi')"
[[ "$out" != *$'\033'* ]] || fail "NO_COLOR ignored"

# 3. forced on => green ok / red off
out="$(CWV_COLOR=always bash -c 'source tools/cwv_term.sh; cwv_ok hi')"
[[ "$out" == *$'\033[32;1mhi'* ]] || fail "cwv_ok is not green when forced"
out="$(CWV_COLOR=always bash -c 'source tools/cwv_term.sh; cwv_features_tree vita "zoom:off"')"
[[ "$out" == *$'\033[31;1moff'* ]] || fail "feature off is not red"

# 4. tree shape (plain)
out="$(CWV_COLOR=never bash -c 'source tools/cwv_term.sh; cwv_features_tree wiiu "cursor:on history:off"')"
expect=$'  wiiu\n    features\n      cursor:   on\n      history:  off'
[[ "$out" == "$expect" ]] || fail "tree shape changed:
$out"

# 5. colorize classification
cz() { CWV_COLOR=always bash -c 'source tools/cwv_term.sh; printf "%s\n" "$1" | cwv_colorize' _ "$1"; }
[[ "$(cz '[ 12%] Building C object x.c.obj')" == *$'\033[32;1m'* ]]  || fail "compile line not green"
[[ "$(cz '[ 12%] Building stubs X')"               == *$'\033[34;1m'* ]]  || fail "stubs line not blue"
[[ "$(cz '[ 14%] Linking C executable X')"          == *$'\033[34;1m'* ]]  || fail "link line not blue"
[[ "$(cz 'a.c:1:2: warning: unused')"               == *$'\033[33;1m'* ]]  || fail "warning not yellow"
[[ "$(cz 'a.c:1:2: error: boom')"                   == *$'\033[31;1m'* ]]  || fail "error not red"
[[ "$(cz 'ld: cannot find -lfoo')"                 == *$'\033[31;1m'* ]]  || fail "link error not red"
[[ "$(CWV_COLOR=never bash -c 'source tools/cwv_term.sh; echo "a.c: error: x" | cwv_colorize')" == "a.c: error: x" ]] \
  || fail "colorize altered text with color off"

# 6. the Wii -L array: run the REAL block from sources/build-wii.sh
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/libogc/lib/wii" "$tmp/portlibs/ppc/lib"
block="$(sed -n '/^WII_LIBDIR_ARGS=()/,/^WII_LIBDIRS=/p' sources/build-wii.sh)"
[[ -n "$block" ]] || fail "WII_LIBDIR_ARGS block not found in sources/build-wii.sh"
args="$(DEVKITPRO="$tmp" DEVKITPPC="$tmp/none" GRRLIB_DIR="$tmp/none" bash -c "$block"'
printf "%s\n" "${WII_LIBDIR_ARGS[@]}"')"
[[ "$args" == *"-L$tmp/libogc/lib/wii"* ]] || fail "expected -L dir missing"
[[ "$args" != *'\'* && "$args" != *"'"* ]] || fail "backslash/quote in -L args (the old escaped bug)"
while IFS= read -r a; do [[ "$a" == -L/* ]] || fail "bad -L arg: $a"; done <<<"$args"

# 7. the old escaped -L append form must stay gone
if grep -q 'WII_LIBDIR_ARGS+=("-L\\' sources/build-wii.sh; then
  fail "old escaped -L form is back"
fi

# 8. The shared optimization stage (minify + CWI packing) on every platform (82/83)
[[ -x tools/cwi_pack_stage.sh ]]   || fail "tools/cwi_pack_stage.sh is not executable"
[[ -x tools/optimize_web.sh ]]     || fail "tools/optimize_web.sh is not executable"
for sc in sources/build-vita.sh sources/build-wii.sh sources/build-wiiu.sh sources/build-windows.sh; do
  grep -q 'optimize_web' "$sc" || fail "$sc does not run the shared optimization stage"
done
bash -n tools/cwi_pack_stage.sh  || fail "cwi_pack_stage.sh has syntax errors"
bash -n tools/optimize_web.sh    || fail "optimize_web.sh has syntax errors"
ct="$(mktemp -d)"; trap 'rm -rf "$tmp" "$ct"' EXIT
python3 - "$ct" <<'PNGEOF'
import struct, zlib, sys, os, random
ct = sys.argv[1]
os.makedirs(ct + "/wii")
def chunk(t, b):
    c = t + b
    return struct.pack(">I", len(b)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
def png_bytes(w, h, rows):
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows))
            + chunk(b"IEND", b""))
# flat 64x64 red image (packed CWI keeps the lossless ORIG inside)
flat = b"".join(b"\x00" + bytes([255,0,0,255]) * 64 for _ in range(64))
open(ct + "/page.png", "wb").write(png_bytes(64, 64, flat))
open(ct + "/wii/icon.png", "wb").write(png_bytes(64, 64, flat))   # packaging PNG: must stay real PNG
open(ct + "/page.cwi", "wb").write(png_bytes(64, 64, flat))       # stray sibling: must be removed
# noisy 64x64 image: CWI is ~2x -> over the 1.5 guard -> must stay a PNG
random.seed(7)
noise = b"".join(b"\x00" + bytes(random.getrandbits(8) for _ in range(64*4)) for _ in range(64))
open(ct + "/noisy.png", "wb").write(png_bytes(64, 64, noise))
PNGEOF
cp "$ct/page.png" "$tmp/orig.png"   # kept OUTSIDE the staged dir so the packer cannot touch it

# forced mode: CWI_MAX_GROWTH=0 packs everything packable
out="$(CWI_MAX_GROWTH=0 bash tools/cwi_pack_stage.sh "$ct" gate 2>/dev/null)"
[[ "$(head -c4 "$ct/page.png")" == "CWIF" ]]        || fail "forced mode: flat PNG was not packed to CWI"
[[ "$(head -c4 "$ct/noisy.png")" == "CWIF" ]]       || fail "forced mode: noisy PNG was not packed"
[[ "$(head -c4 "$ct/wii/icon.png")" == "$(printf '\x89PNG')" ]] || fail "packaging PNG was converted (must stay a real PNG)"
[[ ! -f "$ct/page.cwi" ]]                           || fail "stray .cwi sibling survived next to the packed PNG"
[[ "$out" == *"packed 2 image(s)"* ]]                || fail "forced-mode summary wrong: $out"
[[ "$out" == *"1 packaging asset(s) kept as PNG"* ]] || fail "packaging skip count missing: $out"
[[ "$out" == *"stray .cwi sibling(s) removed"* ]]    || fail "sibling cleanup missing from summary: $out"
[[ "$out" == *"packing CWI fast-load images"* && "$out" == *"growth guard 0x"* ]] || fail "the step line must show the active guard"
out2="$(CWI_MAX_GROWTH=0 bash tools/cwi_pack_stage.sh "$ct" gate 2>/dev/null)"
[[ "$out2" == *"packed 0 image(s)"* ]] || fail "second run is not idempotent: $out2"
python3 tools/cwi_unpack.py "$ct/page.png" -o "$ct/restored.png" >/dev/null 2>&1
python3 - "$tmp/orig.png" "$ct/restored.png" <<'CMP'
import sys
orig = open(sys.argv[1], "rb").read()
if open(sys.argv[2], "rb").read() != orig:
    sys.exit("roundtrip mismatch")
CMP

# default mode: the 1.5 growth guard keeps deflate-friendly art as the
# smaller PNG (compact files win unless CWI is genuinely justified)
ct2="$(mktemp -d)"; trap 'rm -rf "$tmp" "$ct" "$ct2"' EXIT
cp "$tmp/orig.png" "$ct2/page.png"
outd="$(bash tools/cwi_pack_stage.sh "$ct2" gdef 2>/dev/null)"
[[ "$(head -c4 "$ct2/page.png")" == "$(printf '\x89PNG')" ]] || fail "growth guard failed: 1.6x flat PNG was bloated into CWI"
[[ "$outd" == *"1 kept smaller as PNG"* ]] || fail "growth-guard count missing from summary: $outd"
[[ "$outd" == *"growth guard 1.5x"* ]]     || fail "default guard value not shown: $outd"

# the full stage: minify + pack + savings line, one run
mkdir -p "$ct2/full"
cp "$ct2/page.png" "$ct2/full/page.png"
printf '<!doctype html>\n<!-- a comment minification must remove -->\n<html><body>hi</body></html>\n' > "$ct2/full/index.html"
outf="$(CWI_MAX_GROWTH=0 bash tools/optimize_web.sh "$ct2/full" "$ct2/staged" fgate 2>/dev/null)"
[[ "$(head -c4 "$ct2/staged/page.png")" == "CWIF" ]] || fail "optimize_web.sh did not pack the staged content"
grep -q 'a comment minification must remove' "$ct2/staged/index.html" && fail "optimize_web.sh did not minify the staged HTML"
[[ "$outf" == *"web content compacted"* || "$outf" == *"staged verbatim"* ]] || fail "optimize_web.sh prints no savings line: $outf"

echo "TERM GATE PASSED (palette, NO_COLOR/pipe safety, features tree, colorize rules, dispatcher, Wii -L array)"
