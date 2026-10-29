from pathlib import Path

# ---- Vita: replace the inline minify block + pack call with the shared stage
p = Path("sources/build-vita.sh"); s = p.read_text()
old_start = '''# Minification happens at BUILD time: the readable webapp/ sources stay
# the single source of truth, and a conservatively minified copy is what
# ships inside the VPK (HTML comments + CSS whitespace removed; JS and
# binary assets are copied untouched - see tools/minify_web.py).
MINIFIED_WEBAPP_DIR="$BUILD_DIR/webapp_min"
'''
old_mid = '''# Absolute path on purpose: the project root is the script's own directory,
# so this works from any CWD.
'''
old_minify = '''if ! python3 "$ROOT/tools/minify_web.py" "$WEBAPP_DIR" "$MINIFIED_WEBAPP_DIR"; then
'''
old_end_pack = '''# (82) CWI fast-load packing: staged PNGs/BMPs become CWI containers in
# place (same filenames, CWI magic - the engine sniffs it and skips PNG
# decode at load time).  Runs AFTER the packaging-asset exclusion above,
# so the LiveArea art never gets converted.  Degradation-on-failure
# matches minify_web.py: a failed conversion ships the original PNG.
source "$ROOT/tools/cwi_pack_stage.sh" && cwi_pack_stage "$MINIFIED_WEBAPP_DIR" vita'''
for frag in (old_start, old_mid, old_minify, old_end_pack):
    assert frag in s, frag[:60]
# excise the old minify block: from its comment through the fallback copy
i = s.index(old_start)
j = s.index('echo "Webapp staged into the VPK copy: $MINIFIED_WEBAPP_DIR"')
new = '''# (83) ONE optimization stage, shared by every port (tools/optimize_web.sh):
# minify (comments/CSS whitespace; JS and binaries untouched) + CWI
# packing + a combined savings line, with the same degrade-gracefully
# rules on every platform.  The readable webapp/ sources stay the single
# source of truth; only this staged copy is compacted.
MINIFIED_WEBAPP_DIR="$BUILD_DIR/webapp_min"
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WEBAPP_DIR" "$MINIFIED_WEBAPP_DIR" vita
'''
s = s[:i] + new + s[j:]
# drop the now-orphaned pack call (it moved inside optimize_web.sh)
assert old_end_pack in s
s = s.replace("\n" + old_end_pack, "", 1)
p.write_text(s)

# ---- Wii
p = Path("sources/build-wii.sh"); s = p.read_text()
old = '''echo "[wii] staging web content ..."
python3 "$ROOT/tools/minify_web.py" "$WII_WEBAPP_DIR" "$ROOT/build/wii/webapp_min"
cp -r "$ROOT/build/wii/webapp_min/." "$STAGE/web/"

# (82) CWI fast-load packing (see tools/cwi_pack_stage.sh): the staged
# web/ images are converted in place; the icon/cursor art above stays a
# real PNG because the packer skips the packaging metadata folders.
source "$ROOT/tools/cwi_pack_stage.sh" && cwi_pack_stage "$STAGE/web" wii'''
new = '''echo "[wii] staging web content ..."
# (83) shared optimization stage: minify + CWI packing + savings report
# (tools/optimize_web.sh - the same pipeline every port ships)
mkdir -p "$STAGE/web"
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WII_WEBAPP_DIR" "$STAGE/web" wii'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)

# ---- Wii U
p = Path("sources/build-wiiu.sh"); s = p.read_text()
old = '''cp -r "$WEBAPP" "$APP/web"

# (82) CWI fast-load packing (tools/cwi_pack_stage.sh): in-place CWI
# conversion of the staged web/ images.  The packaging-asset exclusion
# above already removed the console metadata folders.
source "$ROOT/tools/cwi_pack_stage.sh" && cwi_pack_stage "$APP/web" wiiu'''
new = '''# (83) shared optimization stage (tools/optimize_web.sh): the Wii U now
# ships the SAME minified + CWI-packed content as the other ports
# (previously staged verbatim).  The packaging-asset exclusion above
# already removed the console metadata folders.
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WEBAPP" "$APP/web" wiiu'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)

# ---- Windows
p = Path("sources/build-windows.sh"); s = p.read_text()
old = '''        cp -r "$WIN_WEBAPP_DIR/." "$1/web/"
        echo "staged $1/web ($(find "$1/web" -type f | wc -l) files)"
        # (82) CWI fast-load packing: the staged web/ images become CWI
        # containers in place (same filename, CWI magic - the engine
        # sniffs it and skips PNG decode).  bash subprocess: this script
        # is POSIX sh.  Console metadata folders are skipped inside the
        # helper; failures degrade to the original PNG.
        bash "tools/cwi_pack_stage.sh" "$1/web" win'''
new = '''        # (83) shared optimization stage (tools/optimize_web.sh): the
        # Windows port now ships the same minified + CWI-packed content
        # as the consoles (previously staged verbatim).  bash subprocess:
        # this script is POSIX sh.  Failures degrade gracefully inside.
        bash "tools/optimize_web.sh" "$WIN_WEBAPP_DIR" "$1/web" win && \\
            echo "staged $1/web ($(find "$1/web" -type f | wc -l) files)"'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)
print("all four ports wired to optimize_web")
