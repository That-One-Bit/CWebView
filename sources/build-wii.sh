#!/usr/bin/env bash
set -euo pipefail
# build-wii.sh - Wii port scaffolding for CWebView (libogc + GRRLIB).
#
# STATUS: FULL BUILD (2026-10-03): the shared engine + the complete Wii
# port (app loop, CWMenu shell, IR cursor, software keyboard, HTTP
# fetch) link into boot.dol and ship as an HBC-ready SD package
# (build/wii/sdcard/apps/cwebview).  The script (1) installs/verifies
# the Wii homebrew toolchain, (2) compiles the tree with devkitPPC,
# (3) links with GRRLIB/libogc and converts to boot.dol, (4) stages the
# manifest-driven SD layout (boot.dol, meta.xml, icon, web/, assets/).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tools/cwv_term.sh"
CWV_GCC_COLOR=""; [[ $CWV_USE_COLOR -eq 1 ]] && CWV_GCC_COLOR="-fdiagnostics-color=always"
# scratch dir for downloads; shared by the pacman installer AND the
# GRRLIB source build, so it must exist on EVERY path (bug report
# 2026-09-23 (34): "TMP: unbound variable" when devkitPro was already
# installed and the installer branch never ran).
TMP="${TMP:-$(mktemp -d)}"
# escalate only when not root (root boxes often have no sudo at all)
SUDO=""; [[ $EUID -ne 0 ]] && SUDO="sudo"
AUTO_YES=0
for a in "$@"; do
  case "$a" in
    -y|--yes) AUTO_YES=1 ;;
    --help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown flag: $a (see --help)" >&2; exit 1 ;;
  esac
done


# --- project metadata (package.json): the build-vita.sh UX (78) ---------
# Every key that affects the Wii platform OUTPUT is shown on start,
# then confirmed: [Y] applies the manifest; "n -<key> ..." selectively
# edits only the flagged fields; /s writes the edits back to the
# manifest (wii section), /p keeps them for this build only.
# Non-interactive runs (-y or no TTY) apply the manifest silently.
WII_PACKAGE_JSON=""
for cand in "$ROOT/package.json" "$ROOT/webapp/package.json"; do
  [[ -f "$cand" ]] && WII_PACKAGE_JSON="$cand" && break
done

jget() {
  # "wii" section first, then top-level aliases: one shared
  # package.json serves all consoles.
  python3 -c 'import json,sys
try:
 d=json.load(open(sys.argv[1],encoding="utf-8"))
except Exception: raise SystemExit(0)
v=d.get("wii",{}) if isinstance(d,dict) else {}
v=v if isinstance(v,dict) else {}
a={"name":["appName","title"],"shorthand":["shortName","shortname","short_name"],"version":["appVersion","app_version"],"author":["publisher","developers"],"description":["shortDescription","short_description","longDescription","long_description"],"icon":["iconPath","icon_path"],"cursor":["cursorPath","cursor_path"],"webpage":["webPage","entrypoint","entryPoint","entry"],"webappDirectory":["webapp_directory"]}
keys=[sys.argv[2]]+a.get(sys.argv[2],[])
for src in (v,d):
 if isinstance(src,dict):
  for x in keys:
   if src.get(x) not in (None,""): print(str(src[x])); raise SystemExit(0)' "$WII_PACKAGE_JSON" "$1"
}

WII_NAME="CWebView"
WII_SHORTNAME="CWV"
WII_VERSION="0.10"
WII_AUTHOR="ThatOneBit"
WII_DESCRIPTION="CWebView for Wii: the shared HTML/CSS/JS engine (native GRRLIB renderer + Duktape) with the CWMenu shell, IR cursor and software keyboard."
WII_ICON=""
WII_CURSOR=""
WII_WEBPAGE="index.html"
WII_WEBAPP_DIR="$ROOT/webapp"
CWV_PROJECT_FLAGS=""
WII_FEATURES=""

if [[ -n "$WII_PACKAGE_JSON" ]] && command -v python3 >/dev/null 2>&1; then
  META_NAME="$(jget name || true)"; META_SHORTNAME="$(jget shorthand || true)"
  META_VERSION="$(jget version || true)"; META_AUTHOR="$(jget author || true)"
  META_DESC="$(jget description || true)"; META_ICON="$(jget icon || true)"
  META_CURSOR="$(jget cursor || true)"
  # "True"/"False" (python bool str) are feature flags, not art paths
  META_CURSOR="${META_CURSOR,,}"
  [[ "$META_CURSOR" == "true" || "$META_CURSOR" == "false" || "$META_CURSOR" == "1" || "$META_CURSOR" == "0" ]] && META_CURSOR=""
  META_WEBPAGE="$(jget webpage || true)"
  META_WEBAPP_DIR="$(jget webappDirectory || true)"
  # feature keys (39): wii.<key>=false disables at COMPILE time.
  # wii.cursor is dual-purpose: "false" is the feature flag, anything
  # else is the cursor art override (same resolution rule the Wii U
  # build applies to wiiu.cursor).
  for key in cursor zoom scroll devtools history; do
    val="$(python3 -c 'import json,sys
try:
 v=json.load(open(sys.argv[1],encoding="utf-8")).get("wii",{})
 print(str(v.get(sys.argv[2],"")).strip().lower())
except Exception: pass' "$WII_PACKAGE_JSON" "$key")"
    # boolean feature values (true/1/false/0) are NOT art paths; only a
    # real string value resolves as cursor art.
    if [[ "$key" == "cursor" && -n "$val" && "$val" != "false" && "$val" != "0" && "$val" != "true" && "$val" != "1" ]]; then
      META_CURSOR="${META_CURSOR:-$val}"
    fi
    if [[ "$val" == "false" || "$val" == "0" ]]; then
      upper="$(echo "$key" | tr 'a-z' 'A-Z')"
      CWV_PROJECT_FLAGS="$CWV_PROJECT_FLAGS -DCWV_${upper}_ENABLED=0"
      WII_FEATURES="$WII_FEATURES $key:off"
    else
      WII_FEATURES="$WII_FEATURES $key:on"
    fi
  done

  echo
  echo "Found project metadata: $WII_PACKAGE_JSON"
  echo "Preview of recognized Wii settings:"
  cwv_field Name "${META_NAME:-<not set>}" 13
  cwv_field Shorthand "${META_SHORTNAME:-<not set>}" 13
  cwv_field Version "${META_VERSION:-<not set>}" 13
  cwv_field Author "${META_AUTHOR:-<not set>}" 13
  cwv_field Description "${META_DESC:-<not set>}" 13
  cwv_field Icon "${META_ICON:-<assets/icon-wii.png>}" 13
  cwv_field "Cursor art" "${META_CURSOR:-<assets/cursor-wii.png>}" 13
  cwv_field Webpage "${META_WEBPAGE:-<index.html>}" 13
  cwv_field "Webapp dir" "${META_WEBAPP_DIR:-$WII_WEBAPP_DIR}" 13
  cwv_features_tree wii "$WII_FEATURES"
  echo

  if [[ -t 0 && $AUTO_YES -eq 0 ]]; then
    read -r -p "Use the provided JSON metadata? [Y/n or n -name -version ... /s]: " use_meta || true
  else
    [[ $AUTO_YES -eq 1 ]] && echo "Applying the manifest values (-y: non-interactive)."
    use_meta="y"
  fi

  case "${use_meta:-Y}" in
    y|Y|yes|YES)
      [[ -n "$META_NAME" ]] && WII_NAME="$META_NAME"
      [[ -n "$META_SHORTNAME" ]] && WII_SHORTNAME="$META_SHORTNAME"
      [[ -n "$META_VERSION" ]] && WII_VERSION="$META_VERSION"
      [[ -n "$META_AUTHOR" ]] && WII_AUTHOR="$META_AUTHOR"
      [[ -n "$META_DESC" ]] && WII_DESCRIPTION="$META_DESC"
      [[ -n "$META_ICON" ]] && WII_ICON="$META_ICON"
      [[ -n "$META_CURSOR" ]] && WII_CURSOR="$META_CURSOR"
      [[ -n "$META_WEBPAGE" ]] && WII_WEBPAGE="$META_WEBPAGE"
      if [[ -n "$META_WEBAPP_DIR" ]]; then
        [[ "$META_WEBAPP_DIR" = /* ]] || META_WEBAPP_DIR="$(dirname "$WII_PACKAGE_JSON")/$META_WEBAPP_DIR"
        WII_WEBAPP_DIR="$META_WEBAPP_DIR"
      fi
      ;;
    n*|N*)
      # Selective metadata mode (same grammar as build-vita.sh):
      # `n -name -version /s` edits only the flagged fields; /s writes
      # them back to package.json, /p means do not persist them.
      META_EDIT_SAVE=0
      META_EDIT_FIELDS=()
      read -r -a META_ARGS <<< "${use_meta#?}"
      for arg in "${META_ARGS[@]}"; do
        case "$arg" in
          /s|/S) META_EDIT_SAVE=1 ;;
          /p|/P) META_EDIT_SAVE=0 ;;
          -name) META_EDIT_FIELDS+=(name) ;;
          -shorthand|-shortname) META_EDIT_FIELDS+=(shorthand) ;;
          -version) META_EDIT_FIELDS+=(version) ;;
          -author) META_EDIT_FIELDS+=(author) ;;
          -description|-desc) META_EDIT_FIELDS+=(description) ;;
          -icon) META_EDIT_FIELDS+=(icon) ;;
          -cursor) META_EDIT_FIELDS+=(cursor) ;;
          -webpage|-entrypoint) META_EDIT_FIELDS+=(webpage) ;;
          -webapp-directory|-webappdir) META_EDIT_FIELDS+=(webappDirectory) ;;
          "") ;;
          *) echo "Unknown metadata flag: $arg" >&2; exit 1 ;;
        esac
      done
      if ((${#META_EDIT_FIELDS[@]}==0)); then
        echo "No metadata fields selected; ignoring JSON metadata."
      else
        meta_has_field(){ local needle="$1" x; for x in "${META_EDIT_FIELDS[@]}"; do [[ "$x" == "$needle" ]] && return 0; done; return 1; }
        meta_prompt(){ local field="$1" label="$2" current="$3" value; if meta_has_field "$field"; then read -r -p "$label [$current]: " value || true; printf '%s' "${value:-$current}"; else printf '%s' "$current"; fi; }
        WII_NAME="$(meta_prompt name "Name" "${META_NAME:-$WII_NAME}")"
        WII_SHORTNAME="$(meta_prompt shorthand "Shorthand" "${META_SHORTNAME:-$WII_SHORTNAME}")"
        WII_VERSION="$(meta_prompt version "Version" "${META_VERSION:-$WII_VERSION}")"
        WII_AUTHOR="$(meta_prompt author "Author" "${META_AUTHOR:-$WII_AUTHOR}")"
        WII_DESCRIPTION="$(meta_prompt description "Description" "${META_DESC:-$WII_DESCRIPTION}")"
        WII_ICON="$(meta_prompt icon "Icon" "${META_ICON:-$WII_ICON}")"
        WII_CURSOR="$(meta_prompt cursor "Cursor art" "${META_CURSOR:-$WII_CURSOR}")"
        WII_WEBPAGE="$(meta_prompt webpage "Webpage entry point" "${META_WEBPAGE:-$WII_WEBPAGE}")"
        WII_WEBAPP_DIR="$(meta_prompt webappDirectory "Webapp directory" "${META_WEBAPP_DIR:-$WII_WEBAPP_DIR}")"
        [[ -n "$WII_WEBAPP_DIR" && "$WII_WEBAPP_DIR" != /* ]] && WII_WEBAPP_DIR="$(dirname "$WII_PACKAGE_JSON")/$WII_WEBAPP_DIR"

        if ((META_EDIT_SAVE)); then
          if command -v python3 >/dev/null 2>&1; then
            export WII_METADATA_SAVE_FIELDS="$(IFS=,; echo "${META_EDIT_FIELDS[*]}")"
            python3 - "$WII_PACKAGE_JSON" "$WII_NAME" "$WII_SHORTNAME" "$WII_VERSION" "$WII_AUTHOR" "$WII_DESCRIPTION" "$WII_ICON" "$WII_CURSOR" "$WII_WEBPAGE" "$WII_WEBAPP_DIR" <<'WII_SAVE_PY'
import json,sys
path,*values=sys.argv[1:]
keys=['name','shorthand','version','author','description','icon','cursor','webpage','webappDirectory']
try:
    with open(path,encoding='utf-8') as f: data=json.load(f)
except Exception as e:
    print(f'Unable to read {path}: {e}',file=sys.stderr); raise SystemExit(1)
if not isinstance(data,dict): data={}
wii=data.get('wii')
if not isinstance(wii,dict): wii={}; data['wii']=wii
selected=set(filter(None, __import__('os').environ.get('WII_METADATA_SAVE_FIELDS','').split(',')))
for key,value in zip(keys,values):
    if key in selected:
        wii[key]=value
with open(path,'w',encoding='utf-8') as f:
    json.dump(data,f,indent=2,ensure_ascii=False)
    f.write('\n')
print(f'Saved selected metadata to {path}')
WII_SAVE_PY
          else
            echo "Cannot save metadata: python3 is unavailable." >&2
            exit 1
          fi
        fi
      fi
      ;;
    *)
      echo "Ignoring JSON metadata; using the defaults above."
      ;;
  esac
fi

confirm() {
  [[ $AUTO_YES -eq 1 ]] && return 0
  local reply; read -r -p "$1 [y/N] " reply || return 1
  [[ "$reply" == [Yy]* ]]
}
ensure_host_tools() {
  local missing=()
  for t in curl git; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Missing host tools: ${missing[*]}"
    if command -v apt-get >/dev/null 2>&1; then
      confirm "Install them with apt-get now?" || { echo "Install the tools above, then re-run." >&2; exit 1; }
      SUDO=""; [[ $EUID -ne 0 ]] && SUDO="sudo"
      $SUDO apt-get update -y && $SUDO apt-get install -y "${missing[@]}"
    else
      echo "No apt-get here; install ${missing[*]} manually, then re-run." >&2; exit 1
    fi
  fi
}
ensure_host_tools

if [[ -n "${CWV_WEBAPP_OVERRIDE:-}" ]]; then WII_WEBAPP_DIR="$CWV_WEBAPP_OVERRIDE"; fi

# --- toolchain: the same devkitPro pacman as the Wii U -------------------
PAC=""
for c in dkp-pacman /opt/devkitpro/pacman/bin/pacman; do
  command -v "$c" >/dev/null 2>&1 && PAC="$c" && break
done
if [[ -z "$PAC" ]]; then
  echo "devkitPro pacman not found."
  confirm "Download and install the devkitPro pacman now (official installer)?" || {
    echo "Install devkitPro first: https://devkitpro.org/wiki/Getting_Started" >&2; exit 1; }
  case "$(uname -s)" in
    Linux)
      curl -L -o "$TMP/dkp.sh" https://apt.devkitpro.org/install-devkitpro-pacman
      $SUDO bash "$TMP/dkp.sh" ;;
    Darwin)
      curl -L -o "$TMP/dkp.sh" https://brew.devkitpro.org/install-devkitpro-pacman
      bash "$TMP/dkp.sh" ;;
    *) echo "Unsupported OS; see https://devkitpro.org/wiki/Getting_Started" >&2; exit 1 ;;
  esac
  command -v dkp-pacman >/dev/null 2>&1 || { echo "dkp-pacman still not on PATH; reopen your shell and re-run." >&2; exit 1; }
  PAC="dkp-pacman"
fi

# devkitPro's actual package names (verified against the official repos):
# the toolchain is "devkitPPC" and the libraries are "libogc",
# "libfat-ogc" and the ppc-* portlibs - NOT wii-devkitppc/wii-libogc
# (those were guesses and pacman could not resolve them).  GRRLIB has
# NO package: it is built from its GitHub source below.
if confirm "Install/verify the Wii toolchain packages (devkitPPC, libogc, portlibs, GRRLIB deps)?"; then
"$PAC" -Sy --needed devkitPPC libogc libfat-ogc \
  ppc-libpng ppc-freetype ppc-libjpeg-turbo ppc-bzip2 ppc-brotli
else
  # 'n' is not fatal: continue with the installed toolchain (the
  # devkitPPC check below still exits with a clear message if the
  # compiler is genuinely absent).
  echo "[wii] skipping toolchain package sync (build continues with the installed toolchain)."
fi

DEVKITPPC="${DEVKITPPC:-/opt/devkitpro/devkitPPC}"
# BUG 37 (hardware report): these must be EXPORTED - GRRLIB's pngu
# Makefile reads DEVKITPPC from the ENVIRONMENT ("Please set DEVKITPPC
# in your environment"), and a plain shell assignment is invisible to
# sub-makes.  Same for DEVKITPRO (GRRLIB's install target uses it).
export DEVKITPPC
export DEVKITPRO="${DEVKITPRO:-$(dirname "$DEVKITPPC")}"
[[ -x "$DEVKITPPC/bin/powerpc-eabi-gcc" ]] || { echo "devkitPPC not found at $DEVKITPPC" >&2; exit 1; }

# BUG 41 (hardware report 09/25/2026): pngu's Makefile runs
#   `$(PREFIX)pkg-config libpng --cflags`  ==  powerpc-eabi-pkg-config libpng --cflags
# That wrapper ships with the ppc portlibs in $DEVKITPRO/portlibs/ppc/bin,
# which is on PATH only in a devkitPro-configured shell.  In a plain WSL
# root shell (no devkitpro env sourced) the GRRLIB build died with
# "powerpc-eabi-pkg-config: not found" -> pngu.c: png.h: No such file.
# Put the portlibs bin on PATH for the GRRLIB sub-makes; if the wrapper
# is not there, synthesize a minimal one for libpng (the only pkg-config
# call GRRLIB makes).
if [[ -x "$DEVKITPRO/portlibs/ppc/bin/powerpc-eabi-pkg-config" ]]; then
  export PATH="$DEVKITPRO/portlibs/ppc/bin:$PATH"
  echo "[wii] using devkitPro's powerpc-eabi-pkg-config ($DEVKITPRO/portlibs/ppc/bin)."
else
  GRRLIB_BINDIR="$TMP/dkp-bin"
  mkdir -p "$GRRLIB_BINDIR"
  PPC_INCL="$DEVKITPRO/portlibs/ppc/include"
  PPC_LIB="$DEVKITPRO/portlibs/ppc/lib"
  { echo '#!/bin/sh'
    echo '# minimal powerpc-eabi-pkg-config shim (build-wii.sh BUG 41/43):'
    echo '# pngu asks for libpng; the GRRLIB lib Makefile asks for'
    echo '# freetype2 libpng libjpeg at once - ft2build.h lives in the'
    echo '# freetype2/ subdir of the portlibs include, so that -I is'
    echo '# REQUIRED (BUG 43: without it GRRLIB_ttf.c died on ft2build.h).'
    echo "case "\$*" in"
    echo "  *--cflags*) echo "-I$PPC_INCL -I$PPC_INCL/freetype2" ;;"
    echo "  *--libs*)   echo "-L$PPC_LIB -lpng -lz -lfreetype" ;;"
    echo "  *)          echo "-I$PPC_INCL -I$PPC_INCL/freetype2 -L$PPC_LIB -lpng -lz -lfreetype" ;;"
    echo 'esac'
    echo 'exit 0'
  } > "$GRRLIB_BINDIR/powerpc-eabi-pkg-config"
  chmod +x "$GRRLIB_BINDIR/powerpc-eabi-pkg-config"
  export PATH="$GRRLIB_BINDIR:$PATH"
  [[ -f "$PPC_INCL/png.h" ]] || { echo "warning: $PPC_INCL/png.h not found - install ppc-libpng (the pacman step lists it)." >&2; }
  [[ -f "$PPC_INCL/freetype2/ft2build.h" ]] || { echo "warning: $PPC_INCL/freetype2/ft2build.h not found - install ppc-freetype (the pacman step lists it)." >&2; }
  echo "[wii] powerpc-eabi-pkg-config wrapper not found - synthesized one for libpng."
fi

# --- pointer cursor --------------------------------------------------
# main.c loads <package>/assets/cursor-wii.png.  Art resolution (repo
# default assets/cursor-wii.png, or the manifest's wii.cursor override)
# is staged when the Wii PACKAGING step lands (the app loop port).  The
# shared module falls back to a code-drawn arrow when art is missing.
# CSS cursor pack (2026-09-26 (45)): the future packaging step must also
# stage assets/cursors/<keyword>.png (same rule as the console art: the
# code arrow or console art cover a missing pack).

# --- GRRLIB: no devkitPro package exists - build it from source ----------
# (GRRLIB's own README is the source for this: clone, make, make install
#  into /opt/devkitpro; the clone is skipped when already present.)
GRRLIB_DIR="$TMP/grrlib"
# 2026-10-06 (75): GRRLIB's make install targets libogc, but some setups
# carry the headers under the Wii portlibs tree - either counts as installed
# (his build log showed portlibs/wii/include/grrlib.h while the check only
# looked at libogc, forcing a pointless clone+rebuild every run).
GRRLIB_INSTALLED=""
for cand in "$DEVKITPRO/libogc/include/grrlib.h" \
            "$DEVKITPRO/portlibs/wii/include/grrlib.h" \
            "$DEVKITPRO/libogc/lib/libgrrlib.a" \
            "$DEVKITPRO/portlibs/wii/lib/libgrrlib.a"; do
  [[ -f "$cand" ]] && GRRLIB_INSTALLED="$cand" && break
done
if [[ -z "$GRRLIB_INSTALLED" ]]; then
  confirm "GRRLIB is not installed. Download and build it from github.com/GRRLIB/GRRLIB now?" || {
    echo "wii build needs GRRLIB - install it, then re-run." >&2; exit 1; }
  [[ -d "$GRRLIB_DIR" ]] || git clone --depth 1 https://github.com/GRRLIB/GRRLIB.git "$GRRLIB_DIR" || {
    echo "GRRLIB clone failed (check network)." >&2; exit 1; }
  (cd "$GRRLIB_DIR/GRRLIB" && make -j2 && $SUDO make install) || {
    echo "GRRLIB build/install failed." >&2; exit 1; }
else
  echo "[wii] GRRLIB already installed."
fi


# --- full build: shared engine + wii port -> boot.dol --------------------
# The whole feature set now links and ships (2026-10-03): app loop,
# CWMenu shell, IR cursor, software keyboard (cwv_ime_wii.c) and HTTP
# fetch (cwv_fetch_net.c).  Link line mirrors the devkitPPC homebrew
# template + GRRLIB README: -lgrrlib -lpngu -lfreetype -ljpeg -lpng
# -lz -lfat -lwiiuse -lbte -logc -lm.  USB keyboard support (2026-10-05)
# adds -lwiikeyboard (libwiikeyboard ships with libogc; KEYBOARD_Init/
# KEYBOARD_GetEvent, no stdin takeover with a NULL callback).

# unified output tree (user direction 2026-10-02): build/wii/
OBJDIR="$ROOT/build/wii/obj"
STAGE="$ROOT/build/wii/sdcard/apps/cwebview"
mkdir -p "$OBJDIR" "$ROOT/build/wii"

CC="$DEVKITPPC/bin/powerpc-eabi-gcc"
# manifest "webpage" -> the entry page main.c opens (78).  Manifest
# values are webapp-ROOT relative ("index.html"); the runtime path is
# package-root relative ("web/index.html").
if [[ "$WII_WEBPAGE" == web/* ]]; then WII_APP_WEBPAGE="$WII_WEBPAGE"
else WII_APP_WEBPAGE="web/$WII_WEBPAGE"; fi
WII_CFLAGS="-Wall -O2 -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float \
  -I$ROOT/sources/shared/sources \
  -I$ROOT/sources/shared/sources/core \
  -I/opt/devkitpro/libogc/include \
  -I/opt/devkitpro/portlibs/wii/include \
  -DCWV_WII_WEBPAGE=\"$WII_APP_WEBPAGE\" \
  $CWV_PROJECT_FLAGS"
# (81) app version from the manifest, shown on the menu beside the
# internal CWebView version.
WII_CFLAGS="$WII_CFLAGS -DCWV_APP_VERSION=\"${META_VERSION:-0.0}\""

# the whole tree, one object per source (progressive percentages)
WII_SOURCES="
  sources/wii/main.c
  sources/wii/cwv_platform.c
  sources/wii/cwv_ime_wii.c
  sources/wii/cwv_fetch_net.c
  sources/shared/sources/core/cwv_backend.c
  sources/shared/sources/core/cwv_mime.c
  sources/shared/sources/core/cwv_bmp.c
  sources/shared/sources/core/cwv_broken_image.c
  sources/shared/sources/web/css/cwv_css.c
  sources/shared/sources/core/cwv_cursor.c
  sources/shared/sources/core/cwv_cwi.c
  sources/shared/sources/web/js/modern/cwv_es2015_core.c
  sources/shared/sources/web/js/modern/cwv_es2015_lib.c
  sources/shared/sources/web/js/modern/cwv_es2016_lib.c
  sources/shared/sources/web/js/modern/cwv_es2017_lib.c
  sources/shared/sources/web/js/modern/cwv_es2018_lib.c
  sources/shared/sources/web/js/modern/cwv_es2019_lib.c
  sources/shared/sources/web/js/modern/cwv_es2020_lib.c
  sources/shared/sources/web/js/modern/cwv_es2021_lib.c
  sources/shared/sources/web/js/modern/cwv_es2022_lib.c
  sources/shared/sources/web/js/modern/cwv_es2023_lib.c
  sources/shared/sources/web/js/modern/cwv_es2024_lib.c
  sources/shared/sources/web/js/modern/cwv_es_editions.c
  sources/shared/sources/core/cwv_host.c
  sources/shared/sources/web/html/cwv_html.c
  sources/shared/sources/web/js/runtime/cwv_js.c
  sources/shared/sources/menu/cwv_menu_ui.c
  sources/shared/sources/menu/cwmenu_api.c
  sources/shared/sources/menu/native_menu_templates.c
  sources/shared/sources/web/typescript/cwv_ts_core.c
  sources/shared/sources/web/js/runtime/duktape/duktape.c
"
TOTAL="$(echo "$WII_SOURCES" | grep -c . )"
N=0
OBJS=""
for src in $WII_SOURCES; do
  N=$((N+1))
  PCT=$(( N * 88 / TOTAL + 10 ))
  obj="$OBJDIR/$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/')"
  printf '%s[%3d%%] building %s%s\n' "$C_GRN" "$PCT" "$src" "$C_OFF"
  # gcc diagnostics keep their own red/yellow when color is on
  "$CC" $WII_CFLAGS ${CWV_GCC_COLOR:-} -c "$ROOT/$src" -o "$obj"
  OBJS="$OBJS $obj"
done

printf '%s[ 99%%] linking CWebView.elf%s\n' "$C_BLU" "$C_OFF"
# 2026-10-06 (75): the old link line hard-coded two -L paths, but devkitPro
# scatters the Wii libraries across layouts depending on package vintage:
#   libogc:        libogc/lib            OR per-target libogc/lib/wii
#   libfat-ogc:    libfat/lib           OR per-target libfat/lib/wii
#   ppc-* portlibs:portlibs/ppc/lib      (shared ppc portlibs)
#   wii portlibs:  portlibs/wii/lib
#   GRRLIB:        libogc/lib (make install) or the temp build dir
#   newlib -lz:    devkitPPC/powerpc-eabi/lib
# A single "cannot find -logc" on an installed system is a PATH problem,
# not a missing package.  Sweep every plausible directory that exists.
# (80) Build the -L list as a real bash ARRAY.  The old string version
# appended -L\'dir\' and tried to strip the quotes afterwards, but
# "\'" inside double quotes keeps the BACKSLASH, so the linker saw
# -L\/opt/devkitpro/libogc/lib\ for every directory and reported every
# installed library as "cannot find -l...".
WII_LIBDIR_ARGS=()
for d in \
  "$DEVKITPRO/libogc/lib" "$DEVKITPRO/libogc/lib/wii" \
  "$DEVKITPRO/libfat/lib" "$DEVKITPRO/libfat/lib/wii" \
  "$DEVKITPRO/portlibs/wii/lib" "$DEVKITPRO/portlibs/ppc/lib" \
  "$DEVKITPPC/powerpc-eabi/lib" \
  "$GRRLIB_DIR/GRRLIB/GRRLIB" "$GRRLIB_DIR/GRRLIB/lib/pngu"; do
  [[ -d "$d" ]] && WII_LIBDIR_ARGS+=("-L$d")
done
WII_LIBDIRS="${WII_LIBDIR_ARGS[*]:-}"
WII_LIBS="-lgrrlib -lpngu -lfreetype -lbz2 -lbrotlidec -lbrotlicommon -ljpeg -lpng -lz -lfat -lwiiuse -lbte -logc -lwiikeyboard -lm"
echo "[wii] library search dirs: $WII_LIBDIRS"
if ! "$CC" $WII_CFLAGS $OBJS \
  "${WII_LIBDIR_ARGS[@]}" \
  -Wl,-Map,"$ROOT/build/wii/CWebView.map" \
  $WII_LIBS \
  -o "$ROOT/build/wii/CWebView.elf"; then
  cwv_err "[wii] link failed - checking which libraries are actually present:"
  for lib in grrlib pngu freetype bz2 brotlidec brotlicommon jpeg png z fat wiiuse bte ogc wiikeyboard; do
    found=""
    for d in /opt/devkitpro/libogc/lib /opt/devkitpro/libogc/lib/wii \
             /opt/devkitpro/libfat/lib /opt/devkitpro/libfat/lib/wii \
             /opt/devkitpro/portlibs/wii/lib /opt/devkitpro/portlibs/ppc/lib \
             /opt/devkitpro/devkitPPC/powerpc-eabi/lib; do
      for f in "$d/lib$lib.a" "$d/lib$lib.so"; do
        [[ -f "$f" ]] && found="$f" && break 2
      done
    done
    if [[ -z "$found" ]]; then
      cwv_err "  MISSING: lib$lib.a not in any search dir"
    else
      printf '  %sok%s: %s\n' "$C_GRN" "$C_OFF" "$found" >&2
    fi
  done
  exit 1
fi

echo "[wii] converting to boot.dol"
ELF2DOL=""
for cand in "$DEVKITPPC/bin/elf2dol" "$DEVKITPRO/tools/bin/elf2dol" "$(command -v elf2dol || true)"; do
  [[ -x "$cand" ]] && ELF2DOL="$cand" && break
done
if [[ -z "$ELF2DOL" ]]; then
  echo "elf2dol not found (devkitPPC tools) - cannot produce boot.dol." >&2
  exit 1
fi
"$ELF2DOL" "$ROOT/build/wii/CWebView.elf" "$ROOT/build/wii/boot.dol"

# --- SD package (HBC layout: the folder boot.dol sits in IS the root) -----
# The engine reads web/ + assets/ from the folder boot.dol lives in
# (user direction 2026-09-23 (33)) - so the SD layout mirrors that.
echo "[wii] staging the SD package (sd:/apps/cwebview) ..."
[[ -d "$STAGE" ]] && find "$STAGE" -mindepth 1 -delete
mkdir -p "$STAGE/assets" "$STAGE/web"

cp "$ROOT/build/wii/boot.dol" "$STAGE/boot.dol"

# icon: manifest override (webapp-relative) or the repo default
ICON_SRC=""
if [[ -n "$WII_ICON" && -n "$WII_PACKAGE_JSON" ]]; then
  WAPP_DIR="$(dirname "$WII_PACKAGE_JSON")"
  [[ -f "$WAPP_DIR/$WII_ICON" ]] && ICON_SRC="$WAPP_DIR/$WII_ICON"
fi
# convention (79): webapp/wii/icon.png, like the wiiu/vita builds
if [[ -z "$ICON_SRC" ]]; then
  for c in "$WII_WEBAPP_DIR" "$ROOT/webapp" "$ROOT/web"; do
    [[ -n "$c" && -f "$c/wii/icon.png" ]] && { ICON_SRC="$c/wii/icon.png"; break; }
  done
fi
[[ -z "$ICON_SRC" && -f "$ROOT/assets/icon-wii.png" ]] && ICON_SRC="$ROOT/assets/icon-wii.png"
if [[ -n "$ICON_SRC" ]]; then cp "$ICON_SRC" "$STAGE/icon.png"; echo "[wii] icon: $ICON_SRC"
else echo "[wii] no icon found (optional: manifest wii.icon, webapp/wii/icon.png or assets/icon-wii.png)"; fi

# meta.xml (Homebrew Channel)
cat > "$STAGE/meta.xml" <<META
<app version="2">
  <name>${WII_NAME}</name>
  <coder>${WII_AUTHOR}</coder>
  <version>${WII_VERSION}</version>
  <short_description>${WII_DESCRIPTION}</short_description>
  <long_description>${WII_DESCRIPTION} - CWebView: the shared engine with ES2015+ JS (Duktape), the CWMenu shell, IR cursor, software keyboard and HTTP fetch on the Wii.</long_description>
</app>
META

# web content: minified, the same staged copy the other ports ship
echo "[wii] staging web content ..."
# (83) shared optimization stage: minify + CWI packing + savings report
# (tools/optimize_web.sh - the same pipeline every port ships)
mkdir -p "$STAGE/web"
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WII_WEBAPP_DIR" "$STAGE/web" wii

# assets: default font + cursor art (+ optional CSS cursor pack)
cp "$ROOT/assets/font.ttf" "$STAGE/assets/font.ttf"
# art resolution (78): manifest wii.cursor (webapp-relative), then the
# repo default assets/cursor-wii.png; no art -> code-drawn arrow.
WII_CURSOR_SRC=""
if [[ -n "$WII_CURSOR" && -n "$WII_PACKAGE_JSON" ]]; then
  WAPP_DIR="$(dirname "$WII_PACKAGE_JSON")"
  [[ -f "$WAPP_DIR/$WII_CURSOR" ]] && WII_CURSOR_SRC="$WAPP_DIR/$WII_CURSOR"
fi
[[ -z "$WII_CURSOR_SRC" ]] && for c in "$WII_WEBAPP_DIR" "$ROOT/webapp" "$ROOT/web"; do
  [[ -n "$c" && -f "$c/wii/cursor.png" ]] && { WII_CURSOR_SRC="$c/wii/cursor.png"; break; }
done
[[ -z "$WII_CURSOR_SRC" && -f "$ROOT/assets/cursor-wii.png" ]] && WII_CURSOR_SRC="$ROOT/assets/cursor-wii.png"
if [[ -n "$WII_CURSOR_SRC" ]]; then
  cp "$WII_CURSOR_SRC" "$STAGE/assets/cursor-wii.png"
  echo "[wii] cursor art: $WII_CURSOR_SRC"
else
  echo "[wii] no cursor art (code-drawn arrow fallback in the app loop)"
fi
if [[ -d "$ROOT/assets/cursors" ]]; then
  mkdir -p "$STAGE/assets/cursors"
  cp "$ROOT/assets/cursors/"*.png "$STAGE/assets/cursors/" 2>/dev/null || true
  echo "[wii] CSS cursor pack staged"
fi

cwv_ok "[100%] Wii build complete"
echo ""
echo "========================= Wii package ready ========================="
echo "  boot.dol:      $ROOT/build/wii/boot.dol"
echo "  SD card:       copy $ROOT/build/wii/sdcard/* to the card root,"
echo "                 so the app lives at  sd:/apps/cwebview/"
echo "                 (the folder boot.dol sits in IS the app root -"
echo "                 web/ and assets/ are read from beside it)."
echo "  Dolphin:       Dolphin can load boot.dol directly (File -> Open),"
echo "                 with the SD pack mounted (Config -> Paths -> SD:)."
echo "                 IR pointer = mouse, A = click, HOME = menu."
echo "  Runtime log:   sd:/apps/cwebview/CWV.log (engine failures land there)"
echo "======================================================================"
