#!/usr/bin/env bash
set -euo pipefail

# Colored build output: the shared palette (tools/cwv_term.sh, 2026-10-07
# (80)) is used by all four build scripts, so green/blue/yellow/red mean
# the same thing everywhere.  The wiiu_* names stay as thin aliases.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/tools/cwv_term.sh"
CWV_GCC_COLOR=""; [[ $CWV_USE_COLOR -eq 1 ]] && CWV_GCC_COLOR="-fdiagnostics-color=always"
wiiu_err()  { cwv_err "$@"; }
wiiu_warn() { cwv_warn "$@"; }
wiiu_ok()   { cwv_ok "$@"; }
# build-wiiu.sh - full Wii U build for CWebView: installs/verifies the
# devkitPro toolchain (wut + the wiiu SDL2 stack), then builds the shared
# engine + wiiu platform + app loop into RPX + WUHB with the same
# progressive "[ NN%]" output style as build-vita.sh.
#
# WEBAPP MANIFEST: a package.json next to the webapp (or the project
# root) is shown and applied like build-vita.sh does - keys live under
# "wiiu" (top-level name/version/author and the common aliases count
# too, so one manifest can serve both consoles).  Interactive runs can
# selectively edit fields (answer "n -name -version ..." to the prompt,
# /s writes the edits back into the manifest); -y accepts the manifest
# as-is; without a tty the defaults are applied silently.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUTO_YES=0
WEBAPP_OVERRIDE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) AUTO_YES=1; shift ;;
    --help) sed -n '12,20p' "$0"; exit 0 ;;
    --webapp) WEBAPP_OVERRIDE="$2"; shift 2 ;;
    --webapp=*) WEBAPP_OVERRIDE="${1#--webapp=}"; shift ;;
    *) echo "unknown flag: $1 (see --help)" >&2; exit 1 ;;
  esac
done

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

# --- webapp manifest (package.json) - the build-vita.sh UX ------------
# Defaults mirror sources/wiiu/meta.xml (the fallback template when
# python3 is unavailable), so a manifest-less build is unchanged.
WIIU_NAME="CWebView"
WIIU_SHORTNAME="CWebView"
WIIU_VERSION="0.10"
WIIU_AUTHOR="ThatOneBit"
WIIU_DESCRIPTION="CWebView for Wii U: the shared CWebView HTML/CSS/JS engine (native renderer + Duktape) on the GamePad. Engine-first port; the native CWMenu shell comes later."
WIIU_ICON="wiiu/icon.png"   # webapp-relative (user direction (16)); resolved at staging
WIIU_WEBPAGE="index.html"

json_get() {
  # json_get FILE KEY - prints the value from the manifest's "wiiu"
  # object, falling back to the common top-level aliases (a shared
  # package.json works for both consoles).  Empty output = not set.
  local file="$1" key="$2"
  python3 -c 'import json,sys
p,k=sys.argv[1:]
try:
 d=json.load(open(p,encoding="utf-8"))
except Exception:
 raise SystemExit(0)
v=d.get("wiiu",{}) if isinstance(d,dict) else {}
v=v if isinstance(v,dict) else {}
a={"name":["appName","title"],"shorthand":["shortName","shortname","short_name"],"version":["appVersion","app_version"],"author":["publisher","developers"],"description":["longDescription","long_description","shortDescription","short_description"],"icon":["iconPath","icon_path"],"webpage":["webPage","entrypoint","entryPoint","entry"],"webappDirectory":["webapp_directory"]}
keys=[k]+a.get(k,[])
for src in (v,d):
 if isinstance(src,dict):
  for x in keys:
   if src.get(x) not in (None,""):
    print(str(src[x])); raise SystemExit(0)' "$file" "$key"
}

json_get_root() { # root-level only (no wiiu-section override)
  local file="$1" key="$2"
  python3 - "$file" "$key" <<'PY'
import json,sys
p,k=sys.argv[1],sys.argv[2]
try:
 d=json.load(open(p,encoding="utf-8"))
except Exception:
 raise SystemExit(0)
if isinstance(d,dict) and d.get(k) not in (None,""):
 print(str(d[k])); raise SystemExit(0)
PY
}

PACKAGE_JSON="${WIIU_PACKAGE_JSON:-}"
if [[ -z "$PACKAGE_JSON" ]]; then
  for candidate in "$ROOT/webapp/package.json" "$ROOT/web/package.json" "$ROOT/package.json"; do
    [[ -f "$candidate" ]] && PACKAGE_JSON="$candidate" && break
  done
fi

if [[ -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" ]] && command -v python3 >/dev/null 2>&1; then
  META_NAME="$(json_get "$PACKAGE_JSON" name)"
  META_SHORTNAME="$(json_get "$PACKAGE_JSON" shorthand)"
  META_VERSION="$(json_get "$PACKAGE_JSON" version)"
  META_AUTHOR="$(json_get "$PACKAGE_JSON" author)"
  # "target" names the native app target.  The Vita build uses it for
  # the CMake target / .vpk name; on Wii U it is informational only
  # (the RPX is always CWebView.rpx).
  META_TARGET="$(json_get_root "$PACKAGE_JSON" target)"
  META_DESC="$(json_get "$PACKAGE_JSON" description)"
  META_ICON="$(json_get "$PACKAGE_JSON" icon)"
  META_WEBPAGE="$(json_get "$PACKAGE_JSON" webpage)"
  META_WEBAPP_DIR="$(json_get "$PACKAGE_JSON" webappDirectory)"

  echo
  echo "Found project metadata: $PACKAGE_JSON"
  echo "Preview of recognized Wii U settings:"
  cwv_field Name "${META_NAME:-<not set>}" 13
  cwv_field Shorthand "${META_SHORTNAME:-<not set>}" 13
  cwv_field Version "${META_VERSION:-<not set>}" 13
  cwv_field Author "${META_AUTHOR:-<not set>}" 13
  cwv_field Target "${META_TARGET:-<not set>}" 13
  cwv_field Description "${META_DESC:-<not set>}" 13
  cwv_field Icon "${META_ICON:-<default webapp/wiiu/icon.png>}" 13
  cwv_field Webpage "${META_WEBPAGE:-<index.html>}" 13
  cwv_field "Webapp dir" "${META_WEBAPP_DIR:-<webapp or test_pages>}" 13
  WIIU_FEATURES=""
  for key in cursor zoom scroll devtools history; do
    fval="$(json_get "$PACKAGE_JSON" "$key")"
    if [[ "$fval" == "False" || "$fval" == "false" || "$fval" == "0" ]]; then WIIU_FEATURES="$WIIU_FEATURES $key:off"
    else WIIU_FEATURES="$WIIU_FEATURES $key:on"; fi
  done
  cwv_features_tree wiiu "$WIIU_FEATURES"
  echo

  if [[ -t 0 && $AUTO_YES -eq 0 ]]; then
    read -r -p "Use the provided JSON metadata? [Y/n or n -name -version ... /s]: " use_meta || true
  else
    [[ $AUTO_YES -eq 1 ]] && echo "Applying the manifest values (-y: non-interactive)."
    use_meta="y"
  fi

  case "${use_meta:-Y}" in
    y|Y|yes|YES)
      [[ -n "$META_NAME" ]] && WIIU_NAME="$META_NAME"
      [[ -n "$META_SHORTNAME" ]] && WIIU_SHORTNAME="$META_SHORTNAME"
      [[ -n "$META_VERSION" ]] && WIIU_VERSION="$META_VERSION"
      [[ -n "$META_AUTHOR" ]] && WIIU_AUTHOR="$META_AUTHOR"
      [[ -n "$META_DESC" ]] && WIIU_DESCRIPTION="$META_DESC"
      [[ -n "$META_ICON" ]] && WIIU_ICON="$META_ICON"
      [[ -n "$META_WEBPAGE" ]] && WIIU_WEBPAGE="$META_WEBPAGE"
      if [[ -n "$META_WEBAPP_DIR" && -z "$WEBAPP_OVERRIDE" ]]; then
        # webappDirectory is MANIFEST-RELATIVE ("." = the manifest's own
        # folder); an explicit --webapp flag still wins.
        [[ "$META_WEBAPP_DIR" = /* ]] || META_WEBAPP_DIR="$(dirname "$PACKAGE_JSON")/$META_WEBAPP_DIR"
        WEBAPP_OVERRIDE="$META_WEBAPP_DIR"
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
          -webpage|-entrypoint) META_EDIT_FIELDS+=(webpage) ;;
          -target) META_EDIT_FIELDS+=(target) ;;
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
        WIIU_NAME="$(meta_prompt name "Name" "${META_NAME:-$WIIU_NAME}")"
        WIIU_SHORTNAME="$(meta_prompt shorthand "Shorthand" "${META_SHORTNAME:-$WIIU_SHORTNAME}")"
        WIIU_VERSION="$(meta_prompt version "Version" "${META_VERSION:-$WIIU_VERSION}")"
        WIIU_AUTHOR="$(meta_prompt author "Author" "${META_AUTHOR:-$WIIU_AUTHOR}")"
        WIIU_DESCRIPTION="$(meta_prompt description "Description" "${META_DESC:-$WIIU_DESCRIPTION}")"
        WIIU_ICON="$(meta_prompt icon "Icon" "${META_ICON:-$WIIU_ICON}")"
        WIIU_WEBPAGE="$(meta_prompt webpage "Webpage entry point" "${META_WEBPAGE:-$WIIU_WEBPAGE}")"
        # target is informational on Wii U (the RPX is always
        # CWebView.rpx) but is overridable for the manifest like on Vita.
        META_TARGET="$(meta_prompt target "Native target" "${META_TARGET:-CWebView}")"
        local_webapp="$(meta_prompt webappDirectory "Webapp directory" "${META_WEBAPP_DIR:-${WEBAPP_OVERRIDE:-$ROOT/webapp}}")"
        [[ -n "$local_webapp" && "$local_webapp" != /* ]] && [[ -n "${PACKAGE_JSON:-}" ]] && local_webapp="$(dirname "$PACKAGE_JSON")/$local_webapp"
        [[ -n "$local_webapp" && "$local_webapp" != "$ROOT/webapp" ]] && WEBAPP_OVERRIDE="$local_webapp"

        if ((META_EDIT_SAVE)); then
          if command -v python3 >/dev/null 2>&1; then
            export WIIU_METADATA_SAVE_FIELDS="$(IFS=,; echo "${META_EDIT_FIELDS[*]}")"
            python3 - "$PACKAGE_JSON" "$WIIU_NAME" "$WIIU_SHORTNAME" "$WIIU_VERSION" "$WIIU_AUTHOR" "$WIIU_DESCRIPTION" "$WIIU_ICON" "$WIIU_WEBPAGE" "$WEBAPP_OVERRIDE" <<'PY'
import json,sys
path,*values=sys.argv[1:]
keys=['name','shorthand','version','author','description','icon','webpage','webappDirectory']
try:
    with open(path,encoding='utf-8') as f: data=json.load(f)
except Exception as e:
    print(f'Unable to read {path}: {e}',file=sys.stderr); raise SystemExit(1)
if not isinstance(data,dict): data={}
wiiu=data.get('wiiu')
if not isinstance(wiiu,dict): wiiu={}; data['wiiu']=wiiu
selected=set(filter(None, __import__('os').environ.get('WIIU_METADATA_SAVE_FIELDS','').split(',')))
for key,value in zip(keys,values):
    if key in selected:
        wiiu[key]=value
with open(path,'w',encoding='utf-8') as f:
    json.dump(data,f,indent=2,ensure_ascii=False)
    f.write('\n')
print(f'Saved selected metadata to {path}')
PY
          else
            echo "Cannot save metadata: python3 is unavailable." >&2
            exit 1
          fi
        fi
      fi
      ;;
    *)
      echo "Ignoring JSON metadata; using manual/default values."
      ;;
  esac
fi

# main.c loads file:///app0/<page>; the webapp is staged under web/.
case "$WIIU_WEBPAGE" in
  web/*) WIIU_APP0_WEBPAGE="$WIIU_WEBPAGE" ;;
  *)     WIIU_APP0_WEBPAGE="web/$WIIU_WEBPAGE" ;;
esac

# --- toolchain: devkitPro pacman (official Wii U homebrew distribution) ---
PAC=""
for c in dkp-pacman /opt/devkitpro/pacman/bin/pacman; do
  command -v "$c" >/dev/null 2>&1 && PAC="$c" && break
done
if [[ -z "$PAC" ]]; then
  echo "devkitPro pacman not found."
  confirm "Download and install the devkitPro pacman now (official installer)?" || {
    echo "Install devkitPro first: https://devkitpro.org/wiki/Getting_Started" >&2; exit 1; }
  TMP=$(mktemp -d)
  # Plain `curl` (no User-Agent) sometimes gets served a small HTML
  # bot-check page instead of the real file by these hosts, which then
  # fails with a confusing "syntax error near unexpected token" once piped
  # into bash. A browser-like UA avoids that, and -f makes curl itself
  # fail on a non-2xx response; we also verify the payload actually looks
  # like the expected file before executing/installing it.
  UA="Mozilla/5.0 (X11; Linux x86_64) build-wiiu.sh/devkitpro-installer"
  case "$(uname -s)" in
    Linux)
      curl -fsSL --retry 3 -A "$UA" -o "$TMP/dkp.sh" \
        https://apt.devkitpro.org/install-devkitpro-pacman
      head -c2 "$TMP/dkp.sh" | grep -qa '^#!' || {
        echo "Download of the devkitPro installer looks wrong (not a shell script)." >&2
        echo "First line was: $(head -n1 "$TMP/dkp.sh")" >&2
        echo "Install manually: https://devkitpro.org/wiki/Getting_Started" >&2
        exit 1
      }
      chmod +x "$TMP/dkp.sh"
      sudo "$TMP/dkp.sh" ;;
    Darwin)
      # macOS has no install *script* - devkitPro ships a signed .pkg
      # installer instead (the old brew.devkitpro.org URL this used to
      # hit doesn't exist).
      PKG="$TMP/devkitpro-pacman-installer.pkg"
      curl -fsSL --retry 3 -A "$UA" -o "$PKG" \
        https://github.com/devkitPro/pacman/releases/latest/download/devkitpro-pacman-installer.pkg
      head -c4 "$PKG" | grep -qa '^xar!' || {
        echo "Download of the devkitPro .pkg installer looks wrong (not a .pkg)." >&2
        echo "Install manually: https://devkitpro.org/wiki/Getting_Started" >&2
        exit 1
      }
      sudo installer -pkg "$PKG" -target / ;;
    *) echo "Unsupported OS for the auto-installer; see https://devkitpro.org/wiki/Getting_Started" >&2; exit 1 ;;
  esac
  command -v dkp-pacman >/dev/null 2>&1 || { echo "dkp-pacman still not on PATH; reopen your shell and re-run." >&2; exit 1; }
  PAC="dkp-pacman"
fi

# --- packages: toolchain + wut + SDL2 stack (installed if missing,
#     upgraded to latest with -y on a re-run is NOT automatic: pass
#     --update below) --- (kept simple: --needed is enough for now)
# NOTE: the PowerPC toolchain package is just "devkitPPC" - it's shared by
# Wii/GameCube/Wii U and has no "wiiu-" prefix (only the Wii U-specific
# portlibs below do). "wiiu-devkitppc" is not a real package.
if confirm "Install/verify wiiu toolchain packages (devkitPPC, wut, wiiu-sdl2, sdl2_image, sdl2_ttf)?"; then
  "$PAC" -Sy --needed devkitPPC wut wiiu-sdl2 wiiu-sdl2_image wiiu-sdl2_ttf
else
  # 'n' is not fatal: skip the package sync and continue with whatever
  # toolchain is already installed (the checks below still exit with a
  # clear message when the compiler is genuinely absent).
  echo "[wiiu] skipping toolchain package sync (build continues with the installed toolchain)."
fi

DEVKITPPC="${DEVKITPPC:-/opt/devkitpro/devkitPPC}"
[[ -x "$DEVKITPPC/bin/powerpc-eabi-gcc" ]] || { echo "devkitPPC not found at $DEVKITPPC" >&2; exit 1; }

# wut system headers (coreinit/*, nn/*, ...) - needed by main.c since the
# OSReport boot trace (see changelog 23) includes <coreinit/debug.h>,
# and by the platform units.  Discovered once, used by the compile checks
# below AND the real build so they can never drift apart again.
WUT_INC=""
for c in /opt/devkitpro/wut/include /opt/devkitpro/portlibs/wiiu/include; do
  [[ -d "$c" ]] && WUT_INC="$WUT_INC -I$c"
done

# --- compile-check the platform layer against the real toolchain ---
echo "[wiiu] compile-checking sources/wiiu/cwv_platform.c ..."
"$DEVKITPPC/bin/powerpc-eabi-gcc" -c -Wall \
  $WUT_INC \
  -I"$ROOT/sources/shared/sources" \
  -I"$ROOT/sources/shared/sources/core" \
  -I/opt/devkitpro/portlibs/wiiu/include/SDL2 \
  "$ROOT/sources/wiiu/cwv_platform.c" -o /tmp/cwv_platform_wiiu.o
echo "[wiiu] platform layer compiles cleanly."
echo "[wiiu] compile-checking sources/wiiu/main.c ..."
"$DEVKITPPC/bin/powerpc-eabi-gcc" -c -Wall \
  $WUT_INC \
  -I"$ROOT/sources/shared/sources" \
  -I"$ROOT/sources/shared/sources/core" \
  -I"$ROOT/sources/wiiu" \
  -I/opt/devkitpro/portlibs/wiiu/include/SDL2 \
  -DCWV_WIIU_WEBPAGE="\"$WIIU_APP0_WEBPAGE\"" \
  "$ROOT/sources/wiiu/main.c" -o /tmp/cwv_main_wiiu.o
echo "[wiiu] app loop compiles cleanly."
# Default font check: the engine needs a bundled TTF to draw text.
FONT_CANDIDATES=("$ROOT/assets/font.ttf" "$ROOT/webapp/assets/font.ttf" "$ROOT/web/assets/font.ttf")
FOUND_FONT=""
for fc in "${FONT_CANDIDATES[@]}"; do [[ -f "$fc" ]] && FOUND_FONT="$fc" && break; done
if [[ -n "$FOUND_FONT" ]]; then
  echo "[wiiu] default font: $FOUND_FONT (packaged as assets/font.ttf)"
else
  wiiu_err "[wiiu] ERROR: no default font at assets/font.ttf - text cannot render. Bundle one (see assets/FONT_LICENSE.txt)."
  exit 1
fi

# ================= real build: ELF -> RPX -> SD package ================
# Fresh content dir per run (no cleanup of old runs needed).
BUILD="$ROOT/build/wiiu"
APP="$BUILD/content"
mkdir -p "$BUILD/obj"
rm -r "$APP" 2>/dev/null || true
mkdir -p "$APP/assets"
echo "[wiiu] building the full app (shared engine + wiiu platform + app loop)..."

CFLAGS="-O2 -g -Wall $WUT_INC -I$ROOT/sources/shared/sources -I$ROOT/sources/shared/sources/core -I$ROOT/sources/wiiu -I/opt/devkitpro/portlibs/wiiu/include/SDL2 -DCWV_WIIU_WEBPAGE=\"$WIIU_APP0_WEBPAGE\""

# Project feature keys (2026-09-23 (39)): package.json wiiu.{cursor,zoom,
# scroll,devtools,history} = false disables that feature at COMPILE time
# (cwv_project.h).  Absent/true = enabled; only the explicit-off key gets a
# define, so the default build stays define-clean.
CWV_PROJECT_FLAGS=""
for key in cursor zoom scroll devtools history; do
  val="$(json_get "$PACKAGE_JSON" "$key")"
  if [[ "$val" == "False" || "$val" == "false" || "$val" == "0" ]]; then
    upper="$(echo "$key" | tr 'a-z' 'A-Z')"
    CWV_PROJECT_FLAGS="$CWV_PROJECT_FLAGS -DCWV_${upper}_ENABLED=0"
    echo "[wiiu] project key '$key': disabled"
  fi
done
CFLAGS="$CFLAGS$CWV_PROJECT_FLAGS"
# (81) app version from the manifest, shown on the menu beside the
# internal CWebView version.
CFLAGS="$CFLAGS -DCWV_APP_VERSION=\"${META_VERSION:-0.0}\""

ELF2RPL=""
for c in "$DEVKITPPC/bin/elf2rpl" /opt/devkitpro/tools/bin/elf2rpl /opt/devkitpro/wut/bin/elf2rpl; do
  [[ -x "$c" ]] && ELF2RPL="$c" && break
done
if [[ -z "$ELF2RPL" ]]; then
  for c in elf2rpl /opt/devkitpro/tools/bin/elf2rpl; do
    command -v "$c" >/dev/null 2>&1 && ELF2RPL="$c" && break
  done
fi
[[ -n "$ELF2RPL" ]] || { echo "elf2rpl not found (part of wut - reinstall wiiu packages)." >&2; exit 1; }

# ---- unified progressive output -------------------------------------------
# Same "[ NN%] Building C object: ..." style as build-vita.sh (whose
# percentages come from CMake) so both consoles report progress the same
# way.  Every real build step is counted; the percent printed alongside
# each step's own line (compiler errors still print in full).
BUILD_UNITS=()
# Keep a defensive exact-path guard here. The shared tree is the sole source
# of engine translation units; if a future list is composed from multiple
# groups, an accidentally repeated path must not be compiled twice.
declare -A SEEN_BUILD_SOURCES=()
add_build_source() {
  local kind="$1" src="$2"
  if [[ -n "${SEEN_BUILD_SOURCES[$src]+present}" ]]; then
    echo "[wiiu] skipping duplicate build source: ${src#"$ROOT/"}" >&2
    return 0
  fi
  SEEN_BUILD_SOURCES[$src]=1
  BUILD_UNITS+=("$kind $src")
}
while IFS= read -r -d '' src; do
  add_build_source "C" "$src"
done < <(find "$ROOT/sources/shared/sources" -type f -name '*.c' -print0 | sort -z)
add_build_source "C" "$ROOT/sources/wiiu/cwv_platform.c"
add_build_source "C" "$ROOT/sources/wiiu/main.c"
add_build_source "C++" "$ROOT/sources/wiiu/cwv_ime_swkbd.cpp"
add_build_source "C++" "$ROOT/sources/wiiu/cwv_fetch_nets2.cpp"
BUILD_UNITS+=("LINK" "RPX" "PACKAGE")   # single-word kinds: kind="${unit%% *}" keeps only the first word
STEPS_TOTAL=${#BUILD_UNITS[@]}
STEP=0
step_pct() { printf "[%3d%%]" $(( STEP * 100 / STEPS_TOTAL )); STEP=$((STEP + 1)); }

OBJS=""
for unit in "${BUILD_UNITS[@]}"; do
  kind="${unit%% *}"; src="${unit#* }"
  if [[ "$kind" == "C" || "$kind" == "C++" ]]; then
    display_src="${src#"$ROOT/"}"
    printf "%s" "$C_GRN"; step_pct; echo " Building $kind object: $display_src${C_OFF}"
    # Keep the source-relative path in the object filename.  Several
    # engine modules have repeated basenames (for example cwv_css.c and
    # cwv_html.c in legacy/compatibility subtrees).  Using basename alone
    # silently overwrites earlier objects, which can leave newly declared
    # functions undefined at link time even though their source compiled.
    case "$src" in
      "$ROOT/sources/shared/sources/"*) obj_rel="${src#"$ROOT/sources/shared/sources/"}" ;;
      "$ROOT/sources/wiiu/"*)          obj_rel="platform/wiiu/${src#"$ROOT/sources/wiiu/"}" ;;
      *)                               obj_rel="extra/${src##*/}" ;;
    esac
    obj="$BUILD/obj/${obj_rel%.*}.o"
    mkdir -p "$(dirname "$obj")"
    if [[ "$kind" == "C" ]]; then
      "$DEVKITPPC/bin/powerpc-eabi-gcc" $CFLAGS $CWV_GCC_COLOR -c "$src" -o "$obj" || exit 1
    else
      "$DEVKITPPC/bin/powerpc-eabi-g++" -fno-exceptions -fno-rtti $CFLAGS $CWV_GCC_COLOR \
        -c "$src" -o "$obj" || exit 1
    fi
    OBJS="$OBJS $obj"
  elif [[ "$kind" == "LINK" ]]; then
    printf "%s" "$C_BLU"; step_pct; echo " Linking CXX executable CWebView.elf${C_OFF}"
    # WUT RPX link, per wut's own toolchain (share/wut.ld):
    #  - wut.ld places code/data for the RPX memory map and collects the
    #    .fimport_* OS-import sections; without it the ELF gets the
    #    default layout and elf2rpl emits an unlaunchable RPX.
    #  - ENTRY(__rpx_start) is defined in wut's crt (libraries/wutcrt/
    #    crt0_rpx.s).  wut's Makefile builds ONE libwut.a from all its
    #    sources INCLUDING wutcrt - the pacman package ships no separate
    #    libwutcrt.a, so -lwutcrt fails to resolve.  ENTRY alone does NOT
    #    pull an archive member, so -u,__rpx_start force-extracts
    #    crt0_rpx.o from -lwut itself (it calls __init_wut/main/exit,
    #    which the rest of libwut resolves).
    #  - STATIC ARCHIVE ORDER: libs left-to-right satisfy symbols needed
    #    to their LEFT.  -lwut must come AFTER the SDL libs (SDL2 needs
    #    wut's GX2/AX/ProcUI/KPAD/... import stubs) - the old line had it
    #    FIRST, so no wut member was ever pulled and every GX2*/AX*/FT_*
    #    came out "undefined reference".
    #  - SDL2_ttf needs freetype+harfbuzz, SDL2_image needs webp - the
    #    devkitPro wiiu portlibs provide them; required ones must exist,
    #    optional ones are linked only if installed.
    # devkitPro ships the wiiu SDL2 ports' dependencies as SHARED ppc
    # portlibs (wiiu-sdl2_ttf depends on ppc-freetype/ppc-harfbuzz,
    # wiiu-sdl2_image on ppc-libwebp/ppc-libjpeg-turbo/ppc-libpng): they
    # install to portlibs/ppc, NOT portlibs/wiiu.  Both prefixes are
    # searched for every lib; only the link-proven-required set
    # (freetype harfbuzz webp) is hard-checked.
    PORTLIBS_WIIU="${DEVKITPRO:-/opt/devkitpro}/portlibs/wiiu/lib"
    PORTLIBS_PPC="${DEVKITPRO:-/opt/devkitpro}/portlibs/ppc/lib"
    WUTLIB="${DEVKITPRO:-/opt/devkitpro}/wut/lib"
    WUTLD="${DEVKITPRO:-/opt/devkitpro}/wut/share/wut.ld"
    [[ -f "$WUTLD" ]] || { wiiu_err "[wiiu] ERROR: wut linker script not found: $WUTLD (reinstall the wut package)."; exit 1; }
    wiiu_portlib_dir() { # echo the prefix containing lib$1.a, or nothing
      [[ -f "$PORTLIBS_WIIU/lib$1.a" ]] && { echo "$PORTLIBS_WIIU"; return; }
      [[ -f "$PORTLIBS_PPC/lib$1.a" ]] && { echo "$PORTLIBS_PPC"; return; }
    }
    for req in freetype harfbuzz webp; do
      [[ -n "$(wiiu_portlib_dir "$req")" ]] || { wiiu_err "[wiiu] ERROR: required portlib lib$req.a not found in $PORTLIBS_WIIU or $PORTLIBS_PPC (it comes as a dependency of the wiiu-sdl2 packages: sudo dkp-pacman -S wiiu-sdl2_ttf wiiu-sdl2_image)."; exit 1; }
    done
    WIIU_LINK_LIBS="-lSDL2_ttf -lSDL2_image -lSDL2"
    # Static order: dependents first, dependencies after (ld resolves
    # left to right).  Optional entries are linked only if installed.
    for opt in harfbuzz freetype webp sharpyuv jpeg png brotlidec brotlicommon brotlienc brotli bz2 z tiff; do
      [[ -n "$(wiiu_portlib_dir "$opt")" ]] && WIIU_LINK_LIBS="$WIIU_LINK_LIBS -l$opt"
    done
    # WHBProc wraps ProcUI and is required for correct foreground/background
    # transitions and clean exit when launched as an Aroma/Tiramisu title.
    [[ -f "$WUTLIB/libwhb.a" ]] || { wiiu_err "[wiiu] ERROR: libwhb.a not found in $WUTLIB (reinstall/update wut)."; exit 1; }
    WIIU_LINK_LIBS="$WIIU_LINK_LIBS -lwhb -lwut -lm"
    # Link flags mirror wut's official link spec (share/wut.specs):
    #   -T wut.ld --gc-sections --emit-relocs --no-eh-frame-hdr -z nocopyreloc
    # CRITICAL: --emit-relocs makes ld keep the relocation data in the
    # final ELF (R_PPC_ADDR16_HA/LO for the OS-import thunks etc.).
    # Without it the ELF resolves everything statically, emits NO .rela
    # sections, elf2rpl then writes an RPX with zero relocations - and
    # the RPL loader never rewrites the import thunks, so every OS call
    # jumps to its raw 0xC000xxxx .fimport slot address (proven via a
    # Cemu crash dump + symtab analysis: the first coreinit call
    # MEMGetAllocatableSizeForExpHeapEx landed on slot 0xC0001250).
    # Hand-rolled instead of -specs=wut.specs so we do not depend on the
    # DEVKITPRO env var (the specs file needs %:getenv at link time).
    "$DEVKITPPC/bin/powerpc-eabi-g++" $OBJS -o "$BUILD/CWebView.elf" \
      -Wl,-T,"$WUTLD" -Wl,--gc-sections -Wl,--emit-relocs \
      -Wl,--no-eh-frame-hdr -Wl,-z,nocopyreloc \
      -Wl,-u,__rpx_start -Wl,-z,muldefs \
      -L"$PORTLIBS_WIIU" -L"$PORTLIBS_PPC" -L"$WUTLIB" \
      $WIIU_LINK_LIBS || exit 1
  elif [[ "$kind" == "RPX" ]]; then
    step_pct; echo " Generating CWebView.rpx (elf2rpl)"
    "$ELF2RPL" "$BUILD/CWebView.elf" "$APP/CWebView.rpx" || exit 1
    # Gate: the RPX must carry relocation sections (SHT_RELA, type 4).
    # Without them no import thunk ever gets rewritten and the app is
    # unlaunchable (see the --emit-relocs note above).
    if command -v python3 >/dev/null 2>&1; then
      RELA_COUNT=$(python3 -c '
import struct,sys
f=open(sys.argv[1],"rb"); d=f.read()
if d[:4]!=b"\x7fELF": sys.exit("RPX: not an ELF/RPL image")
scount=struct.unpack_from(">H",d,0x30)[0]
soff,stsize=struct.unpack_from(">I",d,0x20)[0],struct.unpack_from(">H",d,0x2E)[0]
n=sum(1 for i in range(scount) if struct.unpack_from(">I",d,soff+i*stsize+4)[0]==4)
print(n)
' "$APP/CWebView.rpx") || { wiiu_err "[wiiu] ERROR: RPX verification failed: $RELA_COUNT"; exit 1; }
      if [[ "$RELA_COUNT" -lt 1 ]]; then
        wiiu_err "[wiiu] ERROR: generated RPX contains no relocation sections - the ELF was linked without --emit-relocs, making the app unlaunchable. Aborting."
        exit 1
      fi
      wiiu_ok "[wiiu] RPX verified: $RELA_COUNT relocation section(s) present."
    else
      wiiu_warn "[wiiu] NOTE: python3 not found, skipping RPX relocation check."
    fi
  else
    printf "%s" "$C_BLU"; step_pct; echo " Staging SD package (wiiu/apps/CWebView)${C_OFF}"
  fi
done

# Webapp content: --webapp DIR / CWV_WEBAPP env, else ./webapp, else test_pages.
WEBAPP="${CWV_WEBAPP_OVERRIDE:-${WEBAPP_OVERRIDE:-${CWV_WEBAPP:-}}}"
[[ -z "$WEBAPP" && -n "${WIIU_WEBAPP:-}" ]] && WEBAPP="$WIIU_WEBAPP"
if [[ -z "$WEBAPP" ]]; then
  if [[ -d "$ROOT/webapp" ]]; then WEBAPP="$ROOT/webapp"
  elif [[ -d "$ROOT/web" ]]; then WEBAPP="$ROOT/web"   # "web" is just as good a name
  else WEBAPP="$ROOT/test_pages"; wiiu_warn "[wiiu] NOTE: no webapp/ or web/ folder - staging test_pages as web content."; fi
fi

# Per-console packaging images live WITH THE WEBAPP (user direction,
# 2026-09-20 (16)): webapp/wiiu/icon.png, like webapp/vita/*.png for the
# Vita.  Manifest values resolve against the manifest's directory
# (exactly like build-vita.sh's resolve_asset_path); absolute paths pass
# through; the old project-assets location stays as last-resort fallback.
if [[ "$WIIU_ICON" != /* ]]; then
  WIIU_ICON_FOUND=""
  MANIFEST_BASE=""
  [[ -n "${PACKAGE_JSON:-}" ]] && MANIFEST_BASE="$(dirname "$PACKAGE_JSON")"
  for base in "$MANIFEST_BASE" "$WEBAPP" "$ROOT/webapp" "$ROOT/web" "$ROOT"; do
    [[ -n "$base" && -f "$base/$WIIU_ICON" ]] && { WIIU_ICON="$base/$WIIU_ICON"; WIIU_ICON_FOUND=1; break; }
  done
  [[ -n "$WIIU_ICON_FOUND" ]] || WIIU_ICON="$ROOT/assets/icon.png"
fi

echo "[wiiu] staging the SD package (wiiu/apps/CWebView)..."
echo "[wiiu] generating meta.xml from the effective metadata (name: $WIIU_NAME, version: $WIIU_VERSION, author: $WIIU_AUTHOR)..."
if command -v python3 >/dev/null 2>&1; then
  python3 - "$APP/meta.xml" "$WIIU_NAME" "$WIIU_SHORTNAME" "$WIIU_VERSION" "$WIIU_AUTHOR" "$WIIU_DESCRIPTION" <<'PY'
import sys, datetime
from xml.sax.saxutils import escape
out, name, short, version, author, desc = sys.argv[1:7]
xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<app type="wiiu">
  <name>{escape(name)}</name>
  <codename>{escape(short)}</codename>
  <short_name>{escape(short)}</short_name>
  <long_description>{escape(desc)}</long_description>
  <version>{escape(version)}</version>
  <release_date>{datetime.date.today().strftime("%Y%m%d")}</release_date>
  <developers>
    <dev><name>{escape(author)}</name><type>author</type></dev>
  </developers>
</app>
"""
open(out, "w", encoding="utf-8").write(xml)
PY
else
  wiiu_warn "[wiiu] python3 unavailable: staging the static fallback template meta.xml."
  cp "$ROOT/sources/wiiu/meta.xml" "$APP/meta.xml"
fi
[[ -f "$WIIU_ICON" ]] || { wiiu_err "[wiiu] ERROR: icon not found: $WIIU_ICON"; exit 1; }
cp "$WIIU_ICON" "$APP/icon.png"
cp "$FOUND_FONT" "$APP/assets/font.ttf"
# Pointer cursor (2026-09-23 (38)): the APP's shared assets, not the
# webapp.  Resolution: manifest wiiu.cursor (webapp-relative like the
# icon), then $ROOT/assets/cursor-wiiu.png.  Staged as
# assets/cursor-wiiu.png - the runtime path main.c loads.  No art ->
# the code-drawn arrow (warn only).
WIIU_CURSOR="$(json_get "$PACKAGE_JSON" cursor)"
# (41) the cursor key doubles as the FEATURE switch: boolean true means
# "feature on, default art" - NOT an art path named "True".  Only string
# values are art overrides (json_get stringifies JSON booleans).
case "$WIIU_CURSOR" in ""|True|true|False|false|1|0) WIIU_CURSOR="" ;; esac
WIIU_CURSOR_SRC=""
if [[ -n "$WIIU_CURSOR" ]]; then
  if [[ "$WIIU_CURSOR" != /* ]]; then
    MANIFEST_BASE_C=""
    [[ -n "${PACKAGE_JSON:-}" ]] && MANIFEST_BASE_C="$(dirname "$PACKAGE_JSON")"
    for base in "$MANIFEST_BASE_C" "$WEBAPP" "$ROOT/webapp" "$ROOT/web" "$ROOT"; do
      [[ -n "$base" && -f "$base/$WIIU_CURSOR" ]] && { WIIU_CURSOR_SRC="$base/$WIIU_CURSOR"; break; }
    done
  else
    WIIU_CURSOR_SRC="$WIIU_CURSOR"
  fi
  [[ -n "$WIIU_CURSOR_SRC" ]] || wiiu_warn "[wiiu] manifest wiiu.cursor not found: $WIIU_CURSOR - falling back to the default."
fi
# convention (79): webapp/wiiu/cursor.png, like the icon
if [[ -z "$WIIU_CURSOR_SRC" ]]; then
  for base in "$WEBAPP" "$ROOT/webapp" "$ROOT/web"; do
    [[ -n "$base" && -f "$base/wiiu/cursor.png" ]] && { WIIU_CURSOR_SRC="$base/wiiu/cursor.png"; break; }
  done
fi
[[ -n "$WIIU_CURSOR_SRC" ]] || WIIU_CURSOR_SRC="$ROOT/assets/cursor-wiiu.png"
if [[ -f "$WIIU_CURSOR_SRC" ]]; then
  cp "$WIIU_CURSOR_SRC" "$APP/assets/cursor-wiiu.png"
  echo "[wiiu] cursor art: $WIIU_CURSOR_SRC (packaged as assets/cursor-wiiu.png)"
else
  wiiu_warn "[wiiu] no cursor art - the code-drawn arrow will be used (bundle assets/cursor-wiiu.png or set wiiu.cursor)."
fi
# CSS cursor pack (2026-09-26 (45)): <pkg>/assets/cursors/<keyword>.png,
# lazily loaded by cwv_cursor.c.  Optional - missing art falls back to the
# console cursor art above, then the code-drawn arrow.
if [[ -d "$ROOT/assets/cursors" ]]; then
  cp -a "$ROOT/assets/cursors" "$APP/assets/cursors"
  echo "[wiiu] CSS cursor pack: $ROOT/assets/cursors -> assets/cursors/"
else
  wiiu_warn "[wiiu] no CSS cursor pack (optional: assets/cursors/<keyword>.png; default art is used)."
fi
# Shared CWMenu scripts are runtime assets, not compiled into the RPX.
# CWMenu 4 linking stages EVERY .cwm in sources/shared/sources/menu/scripts (default.cwm
# plus linked extras), so open_script: rows resolve at runtime.
mkdir -p "$APP/cwmenu"
cp "$ROOT"/sources/shared/sources/menu/scripts/*.cwm "$APP/cwmenu/"

[[ -f "$WEBAPP/$WIIU_WEBPAGE" ]] || { wiiu_err "[wiiu] ERROR: $WEBAPP has no $WIIU_WEBPAGE (the engine loads $WIIU_APP0_WEBPAGE)."; exit 1; }
cp -r "$WEBAPP" "$APP/web"
# (83) shared optimization stage (tools/optimize_web.sh): the Wii U now
# ships the SAME minified + CWI-packed content as the other ports
# (previously staged verbatim).  The packaging-asset exclusion above
# already removed the console metadata folders.
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WEBAPP" "$APP/web" wiiu
# Console-specific packaging images (webapp/vita, webapp/wiiu, webapp/wii -
# icon/wallpaper/start-logo) are consumed at BUILD time only (meta.xml icon
# above, LiveArea assets in build-vita.sh); the renderer never loads them
# at runtime, so they must NOT ship inside the web/ content.  Verified:
# zero runtime references in sources/ (grep wallpaper|logo -> build scripts
# only).  This also keeps the Wii U package from shipping the Vita/Wii
# folders and vice versa - only true web content is copied.
rm -r -f "$APP/web/vita" "$APP/web/wiiu" "$APP/web/wii"

# WUHB (Aroma bundle): pack the staged app INTO a .wuhb so it can launch
# from the Wii U Menu / Aroma like a native title.  CLI verified against
# wuhbtool's real usage:  wuhbtool <input.rpx> <output.wuhb>
#   --short-name=<menu short name> --name=<long name> --author=<author>
#   --icon=<png> --content=<dir mounted as the content partition>
# The .rpx (Homebrew Launcher + Cemu) is produced above either way; the
# WUHB is an ADDITIONAL console-side artifact, never a replacement.
WUHBTOOL="${WUHBTOOL:-}"
if [[ -z "$WUHBTOOL" ]]; then
  for c in wuhbtool /opt/devkitpro/tools/bin/wuhbtool "$DEVKITPPC/bin/wuhbtool" /opt/devkitpro/wut/bin/wuhbtool; do
    if [[ "$c" == "wuhbtool" ]]; then
      command -v wuhbtool >/dev/null 2>&1 && WUHBTOOL="wuhbtool" && break
    else
      [[ -x "$c" ]] && WUHBTOOL="$c" && break
    fi
  done
fi
if [[ -z "$WUHBTOOL" ]] && [[ -n "${PAC:-}" ]]; then
  # The dkp package that ships wuhbtool has carried different names
  # across repo generations (wut-tools / wiiu-tools), so ask the user's
  # own pacman repo instead of guessing: install whatever package the
  # repo search finds.  Non-fatal on any failure.
  wuhb_pkg="$("$PAC" -Ss wuhbtool 2>/dev/null | head -n1 | awk '{print $1}')"
  if [[ -n "$wuhb_pkg" ]]; then
    echo "[wiiu] wuhbtool not found: installing the dkp package that ships it ($wuhb_pkg)..."
    "$PAC" -S --needed --noconfirm "$wuhb_pkg" >/dev/null 2>&1       || echo "[wiiu] package install failed - WUHB will be skipped (RPX unaffected)." >&2
    WUHBTOOL="$(command -v wuhbtool || true)"
    [[ -z "$WUHBTOOL" && -x /opt/devkitpro/tools/bin/wuhbtool ]] && WUHBTOOL=/opt/devkitpro/tools/bin/wuhbtool
  fi
fi
if [[ -n "$WUHBTOOL" ]]; then
  # NO --content: web content is NEVER packaged into the WUHB (user
  # direction, 2026-09-23 (33)) - the app loads the web/ folder NEXT
  # TO the package, so the WUHB is code + icon only.
  printf '%s[ 98%%] Packing WUHB (Aroma bundle: rpx only - content lives beside it)%s\n' "$C_BLU" "$C_OFF"
  if "$WUHBTOOL" "$APP/CWebView.rpx" "$BUILD/CWebView.wuhb" \
      --short-name="$WIIU_SHORTNAME" --name="$WIIU_NAME" \
      --author="$WIIU_AUTHOR" --icon="$APP/icon.png"; then
    wiiu_ok "[wiiu] WUHB ready: $BUILD/CWebView.wuhb"
  else
    rm -f "$BUILD/CWebView.wuhb"
    wiiu_warn "[wiiu] wuhbtool failed - the RPX path still works."
  fi
else
  wiiu_warn "[wiiu] wuhbtool not found: WUHB skipped (the .rpx is the Homebrew Launcher/Cemu artifact)."
  wiiu_warn "[wiiu]   to enable: install the dkp wuhbtool package (dkp-pacman -Ss wuhbtool) or set WUHBTOOL=/path/to/wuhbtool."
fi

# --- optional Cemu SD deploy (BUG 43) ---------------------------------------
# Cemu's "File -> Load executable" passes an EMPTY argstr (no argv[0], no
# usable cwd) and mounts the loaded .rpx's folder only at an app-INVISIBLE
# internal path (verified in Cemu's CafeSystem.cpp: standalone mode mounts
# "/internal/code", and /vol/content ONLY when the rpx sits in a folder
# literally named "code" with a sibling "content" folder).  The ONE folder
# a directly-loaded app can always see is Cemu's emulated SD card:
#   <Cemu>/mlc01/sdcard/  ==  fs:/vol/external01/
# so the supported Cemu route is staging there - which the runtime SD scan
# then finds (sources/wiiu/cwv_platform.c probe step 3).  Setting
# CWV_CEMU_MLC=/path/to/<Cemu>/mlc01 (or --cemu-deploy <path>) copies the
# staged package there automatically, mirroring how real homebrew is
# dropped onto the SD card.  Content stays OUTSIDE the RPX/WUHB (33).
if [[ -n "${CWV_CEMU_MLC:-}" ]]; then
  CEMU_APPS="${CWV_CEMU_MLC:-}/sdcard/wiiu/apps/CWebView"
  mkdir -p "$CEMU_APPS" || true
  if cp -a "$APP/." "$CEMU_APPS/"; then
    wiiu_ok "[wiiu] Cemu SD deployed: $CEMU_APPS (File -> Load executable -> CWebView.rpx)."
  else
    wiiu_warn "[wiiu] Cemu SD deploy to ${CWV_CEMU_MLC:-} failed (check the path is <Cemu>/mlc01)."
  fi
  unset CEMU_APPS
fi

cwv_ok "[100%] Built target CWebView (RPX + SD package)"; wiiu_ok "[wiiu] build complete: RPX + SD package + WUHB (when wuhbtool present)"
echo ""
wiiu_ok "======================= Wii U package ready ========================="
echo "  RPX (HBL):    $APP/CWebView.rpx"
echo "  Cemu:         copy the staged layout to <Cemu>/mlc01/sdcard/wiiu/apps/CWebView/"
echo "                (or build with CWV_CEMU_MLC=<Cemu>/mlc01 to deploy automatically),"
echo "                then File -> Load executable -> CWebView.rpx.  Direct-loading the"
echo "                rpx WITHOUT the mlc01 copy cannot work: Cemu passes no argv and"
echo "                the rpx folder is invisible to the app (BUG 43)."
[[ -f "$BUILD/CWebView.wuhb" ]] && echo "  WUHB (Aroma): $BUILD/CWebView.wuhb (sd:/wiiu/apps/ or the Homebrew on Menu plugin)"
echo "  SD layout:    copy $APP/* to the SD card so it reads"
echo "                sd:/wiiu/apps/CWebView/{CWebView.rpx,meta.xml,icon.png,web/,assets/}"
echo "  Runtime log:  sd:/wiiu/apps/CWebView/CWV.log (engine failures,"
echo "                incl. IMG: lines, land there even on script-free pages)"
echo "====================================================================="
wiiu_warn "[wiiu] CWMenu draws through the shared shell; GamePad/Cemu visual and input verification is still required."
