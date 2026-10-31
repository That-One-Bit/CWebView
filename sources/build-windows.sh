#!/usr/bin/env bash
# CWebView for Windows (user direction 2026-10-01).
# Cross-builds the native Win32 port for the architectures selected by
# package.json `windowsArchitectures` (default: ["x64", "arm64"]).
# Supported values: x86, x64, arm32, arm64. Each uses a suitable MinGW
# cross-compiler; missing optional toolchains are reported and skipped.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/cwv_term.sh   # cwd is the project root (cd above)
CWV_GCC_COLOR=""; [[ $CWV_USE_COLOR -eq 1 ]] && CWV_GCC_COLOR="-fdiagnostics-color=always"

# (78) -y/--yes: apply the manifest non-interactively.
AUTO_YES=0
for a in "$@"; do
  case "$a" in
    -y|--yes) AUTO_YES=1 ;;
    --help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "unknown flag: $a (see --help)" >&2; exit 1 ;;
  esac
done


# --- project metadata (package.json): the build-vita.sh UX (78) ---------
# Windows output is affected by TWO manifest keys: webappDirectory
# (what gets staged as web/) and webpage (the entry page the exe
# opens, passed as the CWV_WIN_WEBPAGE define).  Name/version/author/
# description are shown for completeness but stay informational here
# (the executable is always CWebView.exe).  Same grammar as the other
# builds: [Y] applies the manifest; "n -webpage ..." edits flagged
# fields; /s writes them back (root level), /p keeps them this build.
WIN_PACKAGE_JSON=""
ROOT_DIR="$(pwd)"
for cand in "$ROOT_DIR/package.json" "$ROOT_DIR/webapp/package.json"; do
  [ -f "$cand" ] && WIN_PACKAGE_JSON="$cand" && break
done

winjget() {
  python3 -c 'import json,sys
try:
 d=json.load(open(sys.argv[1],encoding="utf-8"))
except Exception: raise SystemExit(0)
a={"webpage":["webPage","entrypoint","entryPoint","entry"],"webappDirectory":["webapp_directory"],"name":["appName","title"],"version":["appVersion","app_version"],"author":["publisher","developers"],"description":["shortDescription","short_description","longDescription","long_description"]}
keys=[sys.argv[2]]+a.get(sys.argv[2],[sys.argv[2]])
if isinstance(d,dict):
 for x in keys:
  if d.get(x) not in (None,""): print(str(d[x])); raise SystemExit(0)' "$WIN_PACKAGE_JSON" "$1"
}

WIN_WEBAPP_DIR="$ROOT_DIR/webapp"
WIN_WEBPAGE="index.html"
WIN_ENTRY="web/index.html"

if [ -n "$WIN_PACKAGE_JSON" ] && command -v python3 >/dev/null 2>&1; then
  META_NAME="$(winjget name || true)"
  META_VERSION="$(winjget version || true)"
  META_AUTHOR="$(winjget author || true)"
  META_DESC="$(winjget description || true)"
  META_WEBPAGE="$(winjget webpage || true)"
  META_WEBAPP_DIR="$(winjget webappDirectory || true)"

  echo
  echo "Found project metadata: $WIN_PACKAGE_JSON"
  echo "Preview of recognized Windows settings:"
  cwv_field Name "${META_NAME:-<not set>} ${C_DIM}(informational)${C_OFF}" 13
  cwv_field Version "${META_VERSION:-<not set>} ${C_DIM}(informational)${C_OFF}" 13
  cwv_field Author "${META_AUTHOR:-<not set>} ${C_DIM}(informational)${C_OFF}" 13
  cwv_field Description "${META_DESC:-<not set>} ${C_DIM}(informational)${C_OFF}" 13
  cwv_field Webpage "${META_WEBPAGE:-<index.html>}" 13
  cwv_field "Webapp dir" "${META_WEBAPP_DIR:-$WIN_WEBAPP_DIR}" 13
  echo

  if [ -t 0 ] && [ "$AUTO_YES" -eq 0 ]; then
    read -r -p "Use the provided JSON metadata? [Y/n or n -webpage -webapp-directory ... /s]: " use_meta || true
  else
    [ "$AUTO_YES" -eq 1 ] && echo "Applying the manifest values (-y: non-interactive)."
    use_meta="y"
  fi

  case "${use_meta:-Y}" in
    y|Y|yes|YES)
      [ -n "$META_WEBPAGE" ] && WIN_WEBPAGE="$META_WEBPAGE"
      if [ -n "$META_WEBAPP_DIR" ]; then
        case "$META_WEBAPP_DIR" in /*) WIN_WEBAPP_DIR="$META_WEBAPP_DIR" ;;
          *) WIN_WEBAPP_DIR="$(dirname "$WIN_PACKAGE_JSON")/$META_WEBAPP_DIR" ;;
        esac
      fi
      ;;
    n*|N*)
      META_EDIT_SAVE=0
      META_EDIT_FIELDS=()
      read -r -a META_ARGS <<< "${use_meta#?}"
      for arg in "${META_ARGS[@]}"; do
        case "$arg" in
          /s|/S) META_EDIT_SAVE=1 ;;
          /p|/P) META_EDIT_SAVE=0 ;;
          -webpage|-entrypoint) META_EDIT_FIELDS+=(webpage) ;;
          -webapp-directory|-webappdir) META_EDIT_FIELDS+=(webappDirectory) ;;
          "") ;;
          *) echo "Unknown metadata flag: $arg" >&2; exit 1 ;;
        esac
      done
      if [ "${#META_EDIT_FIELDS[@]}" -eq 0 ]; then
        echo "No metadata fields selected; ignoring JSON metadata."
      else
        win_has_field(){ local needle="$1" x; for x in "${META_EDIT_FIELDS[@]}"; do [ "$x" = "$needle" ] && return 0; done; return 1; }
        win_prompt(){ local field="$1" label="$2" current="$3" value; if win_has_field "$field"; then read -r -p "$label [$current]: " value || true; printf '%s' "${value:-$current}"; else printf '%s' "$current"; fi; }
        WIN_WEBPAGE="$(win_prompt webpage "Webpage entry point" "${META_WEBPAGE:-$WIN_WEBPAGE}")"
        WIN_WEBAPP_DIR="$(win_prompt webappDirectory "Webapp directory" "${META_WEBAPP_DIR:-$WIN_WEBAPP_DIR}")"
        case "$WIN_WEBAPP_DIR" in /*) : ;; *) WIN_WEBAPP_DIR="$(dirname "$WIN_PACKAGE_JSON")/$WIN_WEBAPP_DIR" ;; esac

        if [ "$META_EDIT_SAVE" -eq 1 ]; then
          WIN_METADATA_SAVE_FIELDS="$(IFS=,; echo "${META_EDIT_FIELDS[*]}")"
          WIN_METADATA_SAVE_FIELDS="$WIN_METADATA_SAVE_FIELDS"           python3 - "$WIN_PACKAGE_JSON" "$WIN_WEBPAGE" "$WIN_WEBAPP_DIR" <<'WIN_SAVE_PY'
import json,sys,os
path,webpage,webapp_dir=sys.argv[1:4]
try:
    with open(path,encoding='utf-8') as f: data=json.load(f)
except Exception as e:
    print(f'Unable to read {path}: {e}',file=sys.stderr); raise SystemExit(1)
if not isinstance(data,dict): data={}
selected=set(filter(None, os.environ.get('WIN_METADATA_SAVE_FIELDS','').split(',')))
if 'webpage' in selected: data['webpage']=webpage
if 'webappDirectory' in selected: data['webappDirectory']=webapp_dir
with open(path,'w',encoding='utf-8') as f:
    json.dump(data,f,indent=2,ensure_ascii=False)
    f.write('\n')
print(f'Saved selected metadata to {path}')
WIN_SAVE_PY
        fi
      fi
      ;;
    *)
      echo "Ignoring JSON metadata; using the defaults above."
      ;;
  esac
fi

# entry-page define: manifest "webpage" is webapp-ROOT relative; the
# runtime path is exe-relative ("web/<page>").  Passed to BOTH arches.
case "$WIN_WEBPAGE" in
  web/*) WIN_ENTRY="$WIN_WEBPAGE" ;;
  *) WIN_ENTRY="web/$WIN_WEBPAGE" ;;
esac
WIN_DEFS="-DCWV_WIN_WEBPAGE=\"$WIN_ENTRY\""
# (81) app version from the manifest, shown on the menu
WIN_DEFS="$WIN_DEFS -DCWV_APP_VERSION=\"${META_VERSION:-0.0}\""

# Architectures can be configured in package.json either as a root-level
# `windowsArchitectures` array or `windows: { architectures: [...] }`.
# Keep the default compatible with the historical Windows build.
ROOT=build/windows
WIN_ARCHS="x64 arm64"
if [ -n "$WIN_PACKAGE_JSON" ] && command -v python3 >/dev/null 2>&1; then
  WIN_ARCHS="$(python3 - "$WIN_PACKAGE_JSON" <<'WIN_ARCHS_PY'
import json, sys
try:
    with open(sys.argv[1], encoding="utf-8") as f: d=json.load(f)
except Exception:
    d={}
value=d.get("windowsArchitectures")
if value is None and isinstance(d.get("windows"), dict):
    value=d["windows"].get("architectures")
if value is None:
    value=["x64", "arm64"]
if not isinstance(value, list) or not value or not all(isinstance(x, str) for x in value):
    print("Invalid windowsArchitectures: expected a non-empty array of architecture names", file=sys.stderr)
    raise SystemExit(2)
allowed={"x86", "x64", "arm32", "arm64"}
result=[]
for item in value:
    item=item.strip().lower()
    aliases={"amd64":"x64", "x86_64":"x64", "i386":"x86", "i686":"x86", "aarch64":"arm64", "arm":"arm32", "armv7":"arm32"}
    item=aliases.get(item,item)
    if item not in allowed:
        print(f"Unsupported Windows architecture {item!r}; supported: x86, x64, arm32, arm64", file=sys.stderr)
        raise SystemExit(2)
    if item not in result: result.append(item)
print(" ".join(result))
WIN_ARCHS_PY
  )" || exit $?
fi
mkdir -p "$ROOT"

SHARED_SOURCES="sources/shared/sources/core/cwv_backend.c \
sources/shared/sources/core/cwv_mime.c \
sources/shared/sources/core/cwv_cwi.c \
sources/shared/sources/web/css/cwv_css.c \
sources/shared/sources/web/html/cwv_html.c \
sources/shared/sources/web/js/runtime/cwv_js.c \
sources/shared/sources/core/cwv_broken_image.c \
sources/shared/sources/web/js/modern/cwv_es2015_core.c \
sources/shared/sources/web/js/modern/cwv_es2015_lib.c \
sources/shared/sources/web/js/modern/cwv_es2016_lib.c \
sources/shared/sources/web/js/modern/cwv_es2017_lib.c \
sources/shared/sources/web/js/modern/cwv_es2018_lib.c \
sources/shared/sources/web/js/modern/cwv_es2019_lib.c \
sources/shared/sources/web/js/modern/cwv_es2020_lib.c \
sources/shared/sources/web/js/modern/cwv_es2021_lib.c \
sources/shared/sources/web/js/modern/cwv_es2022_lib.c \
sources/shared/sources/web/js/modern/cwv_es2023_lib.c \
sources/shared/sources/web/js/modern/cwv_es2024_lib.c \
sources/shared/sources/web/js/modern/cwv_es_editions.c \
sources/shared/sources/web/typescript/cwv_ts_core.c \
sources/shared/sources/core/cwv_host.c \
sources/shared/sources/menu/cwv_menu_ui.c \
sources/shared/sources/menu/cwmenu_api.c \
sources/shared/sources/menu/native_menu_templates.c \
sources/shared/sources/core/cwv_cursor.c \
sources/shared/sources/web/js/runtime/duktape/duktape.c"

WIN_SOURCES="sources/win/cwv_platform.c sources/win/main.c"

LIBS="-lgdi32 -luser32 -lshell32 -lole32 -lwindowscodecs -lwinhttp -lshlwapi"

progress() {
    local col="$C_BLU"
    case "$1" in compiling*) col="$C_GRN" ;; built*|*complete*) col="$C_GRN" ;; *skipped*) col="$C_YEL" ;; esac
    printf '%s[%s%%] %s%s\n' "$col" "$2" "$1" "$C_OFF"
}

find_compiler() {
    case "$1" in
      x64)   printf '%s' "${X64_CC:-$(command -v x86_64-w64-mingw32-gcc 2>/dev/null || true)}" ;;
      x86)   printf '%s' "${X86_CC:-$(command -v i686-w64-mingw32-gcc 2>/dev/null || true)}" ;;
      arm64) printf '%s' "${ARM64_CC:-$(command -v aarch64-w64-mingw32-clang 2>/dev/null || { for d in /opt/llvm-mingw/bin /usr/local/llvm-mingw/bin; do [ ! -x "$d/aarch64-w64-mingw32-clang" ] || { printf '%s' "$d/aarch64-w64-mingw32-clang"; break; }; done; })}" ;;
      arm32) printf '%s' "${ARM32_CC:-$(command -v armv7-w64-mingw32-clang 2>/dev/null || { for d in /opt/llvm-mingw/bin /usr/local/llvm-mingw/bin; do [ ! -x "$d/armv7-w64-mingw32-clang" ] || { printf '%s' "$d/armv7-w64-mingw32-clang"; break; }; done; })}" ;;
    esac
}

build_arch() {
    local arch="$1" compiler label out
    compiler="$(find_compiler "$arch")"
    case "$arch" in
      x64) label="AMD64 / x86_64-w64-mingw32" ;;
      x86) label="32-bit x86 / i686-w64-mingw32" ;;
      arm64) label="ARM64 / aarch64-w64-mingw32" ;;
      arm32) label="32-bit ARM / armv7-w64-mingw32" ;;
    esac
    if [ -z "$compiler" ] || [ ! -x "$compiler" ] && ! command -v "$compiler" >/dev/null 2>&1; then
        progress "$arch compiler not found; skipped ($label)" 55
        if [ "$arch" = x64 ]; then
          echo "x64 is selected but x86_64-w64-mingw32-gcc was not found. Install it or remove x64 from windowsArchitectures." >&2
          return 1
        fi
        return 0
    fi
    out="$ROOT/$arch"
    mkdir -p "$out"
    progress "compiling shared engine + Win32 seam ($label)" 25
    "$compiler" -std=c11 -O2 -Wall $CWV_GCC_COLOR \
        -I sources/shared/sources $WIN_DEFS $WIN_SOURCES $SHARED_SOURCES \
        $LIBS -o "$out/CWebView.exe" \
        -Wl,--subsystem,windows
    progress "built $out/CWebView.exe" 55
    BUILT_ARCHS+=("$arch")
}

if [ -n "${CWV_WEBAPP_OVERRIDE:-}" ]; then WIN_WEBAPP_DIR="$CWV_WEBAPP_OVERRIDE"; fi

# --- stage the webapp so the exe finds <app dir>/web/index.html ---------
# BUG 57 (2026-10-02, user report): the pages never got copied for
# Windows, so the exe had nothing to load. The exe resolves its content
# from its own directory (see sources/win/main.c), so web/ must sit
# NEXT TO the binary - the same rule as Vita/Wii U (web/ in the app dir).
# USER DIRECTION (2026-10-03): do NOT minify the webapp on Windows -
# stage the original files verbatim (minification stays a console-build
# choice; the desktop package ships the readable webapp).
stage_web() {
    if [ -d "$WIN_WEBAPP_DIR" ]; then
        mkdir -p "$1/web" "$1/cwmenu"
        # CWMenu scripts are runtime assets, not compiled into the executable.
        # Stage them beside web/ so the Windows menu loader can find
        # <exe-dir>/cwmenu/default.cwm and any linked .cwm files.
        if [ -d "sources/shared/sources/menu/scripts" ]; then
            find "sources/shared/sources/menu/scripts" -maxdepth 1 -type f -name '*.cwm' \
                -exec cp -f {} "$1/cwmenu/" \; \
                || { echo "failed to stage CWMenu scripts for $1" >&2; exit 1; }
        fi
        # (83) shared optimization stage (tools/optimize_web.sh): the Windows
        # port now ships the same minified + CWI-packed content as the
        # consoles (previously staged verbatim).  bash subprocess: this
        # script is POSIX sh.  Failures degrade gracefully inside.
        bash "tools/optimize_web.sh" "$WIN_WEBAPP_DIR" "$1/web" win && \
            echo "staged $1/web ($(find "$1/web" -type f | wc -l) files)"
    else
        echo "webapp directory not found: $WIN_WEBAPP_DIR" >&2
        exit 1
    fi
}

progress "starting Windows build (architectures: $WIN_ARCHS)" 10
BUILT_ARCHS=()
for arch in $WIN_ARCHS; do build_arch "$arch"; done
if [ "${#BUILT_ARCHS[@]}" -eq 0 ]; then
  echo "No Windows architectures were built. Install a selected architecture's cross-compiler." >&2
  exit 1
fi
progress "staging web content next to the executables" 95
for arch in "${BUILT_ARCHS[@]}"; do stage_web "$ROOT/$arch"; done
# Keep the selected project's runtime feature manifest beside the executable.
# When --webapp points at a project subdirectory (e.g. experimental/app/web),
# its parent package.json controls opt-in features such as cwv-args. Do not
# overwrite a manifest in the selected web directory or mutate the source.
if [ -n "${CWV_WEBAPP_OVERRIDE:-}" ] && [ -f "$(dirname "$WIN_WEBAPP_DIR")/package.json" ]; then
  for arch in "${BUILT_ARCHS[@]}"; do cp "$(dirname "$WIN_WEBAPP_DIR")/package.json" "$ROOT/$arch/package.json"; done
  echo "staged runtime manifest from $(dirname "$WIN_WEBAPP_DIR")/package.json"
fi
DONE_ARCHS=""
BUILT_EXES=""; BUILT_WEB=""
for arch in "${BUILT_ARCHS[@]}"; do
  DONE_ARCHS="${DONE_ARCHS}${DONE_ARCHS:+ + }$ROOT/$arch"
  BUILT_EXES="$BUILT_EXES $ROOT/$arch/CWebView.exe"
  BUILT_WEB="$BUILT_WEB $ROOT/$arch/web/"
done
progress "Windows build complete: $DONE_ARCHS" 100
printf '%sBuilt:%s%s\n' "$C_GRN" "$C_OFF" "$BUILT_EXES"
printf '%sContent:%s%s\n' "$C_GRN" "$C_OFF" "$BUILT_WEB"
