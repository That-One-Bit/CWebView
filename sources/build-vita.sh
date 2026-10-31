#!/usr/bin/env bash
set -euo pipefail
# The script lives IN the project root (sources/ layout) and builds from it.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tools/cwv_term.sh"
VITA_DIR="$ROOT"; BUILD_DIR="$VITA_DIR/build"
# No fixed codebase paths anywhere: the SDK location resolves as
#   $VITASDK (explicit) -> /usr/local/vitasdk (existing install)
#   -> $ROOT/.vitasdk (project-local, so the tree stays standalone and
#      movable; the build can bootstrap it there on first run).
if [[ -n "${VITASDK:-}" ]]; then :
elif [[ -x /usr/local/vitasdk/bin/arm-vita-eabi-gcc ]]; then VITASDK=/usr/local/vitasdk
else VITASDK="$ROOT/.vitasdk"
fi
WEBAPP_DIR="${WEBAPP_DIR:-$ROOT/webapp}"
if [[ ! -d "$WEBAPP_DIR" && -d "$ROOT/web" ]]; then WEBAPP_DIR="$ROOT/web"; fi   # webapp/ renamed to web/
# vita_create_stubs() invokes a nested Makefile.  That Makefile resolves
# arm-vita-eabi-as/ld/etc. by name, so make sure the VitaSDK toolchain is
# inherited by both CMake and the nested stub-generation build.
export PATH="$VITASDK/bin:$PATH"
APP_NAME="${APP_NAME:-}"; TITLE_ID="${TITLE_ID:-}"; SHORTNAME="${SHORTNAME:-}"; VERSION="${VERSION:-}"; SFO_VERSION="${SFO_VERSION:-}"; BUILD_VERSION="${BUILD_VERSION:-}"; AUTHOR="${AUTHOR:-}"; TARGET="${TARGET:-}"
APP_DATA_DIR="${APP_DATA_DIR:-}"
WEBPAGE="${WEBPAGE:-}"
LAUNCH_PAGE="${LAUNCH_PAGE:-}"
GENERATE_LAUNCH_PAGE=0
WEBAPP_DIR_FROM_META=0
AUTO_YES=0
# Debug build: 1 = compile in the on-page debug overlay + live DevTools
# "Show Debug Info" button.  0 (default/production) = overlay compiled
# out; the button stays visible but inert.  Enable with --debug or the
# webapp manifest key "debug": true (or 1); "false"/"0" forces production.
DEBUG_BUILD=0
DEBUG_CLI=0
ICON="${ICON:-}"; WALLPAPER="${WALLPAPER:-}"; START_LOGO="${START_LOGO:-}"; LIVEAREA_TEMPLATE="${LIVEAREA_TEMPLATE:-}"
# Optional direct CLI overrides. Paths may be absolute or relative to the project
# metadata JSON (when present).
while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) AUTO_YES=1; shift ;;
    --update-sdk) UPDATE_SDK=1; shift ;;
    --no-sdk-install) NO_SDK_INSTALL=1; shift ;;    --icon) [[ $# -ge 2 ]] || { echo "--icon requires a path" >&2; exit 1; }; ICON="$2"; shift 2 ;;
    --wallpaper) [[ $# -ge 2 ]] || { echo "--wallpaper requires a path" >&2; exit 1; }; WALLPAPER="$2"; shift 2 ;;
    --start-logo|--startlogo) [[ $# -ge 2 ]] || { echo "--start-logo requires a path" >&2; exit 1; }; START_LOGO="$2"; shift 2 ;;
    --livearea-template|--template) [[ $# -ge 2 ]] || { echo "--livearea-template requires a path" >&2; exit 1; }; LIVEAREA_TEMPLATE="$2"; shift 2 ;;
    --launch-page|--launchpage) [[ $# -ge 2 ]] || { echo "--launch-page requires a path" >&2; exit 1; }; LAUNCH_PAGE="$2"; shift 2 ;;
    --debug) DEBUG_BUILD=1; DEBUG_CLI=1; shift ;;
    *) break ;;
  esac
done
PACKAGE_JSON="${VITA_PACKAGE_JSON:-}"
if [[ -z "$PACKAGE_JSON" ]]; then
  for candidate in "$WEBAPP_DIR/package.json" "$ROOT/package.json"; do
    if [[ -f "$candidate" ]]; then PACKAGE_JSON="$candidate"; break; fi
  done
fi

json_get() {
  local file="$1" key="$2"
  python3 -c 'import json,sys
p,k=sys.argv[1:]
try:
 d=json.load(open(p,encoding="utf-8"))
except Exception:
 raise SystemExit(0)
v=d.get("vita",{}) if isinstance(d,dict) else {}
v=v if isinstance(v,dict) else {}
a={"name":["appName"],"titleId":["titleID","title_id","TITLE_ID"],"shorthand":["shortName","shortname","short_name"],"version":["appVersion","app_version"],"sfoVersion":["sfo_version","sfoVersion"],"buildVersion":["build_version","buildVersion"],"author":["publisher"],"target":["nativeTarget","native_target"],"icon":["iconPath","icon_path"],"wallpaper":["wallpaperPath","wallpaper_path"],"startLogo":["start-logo","startLogoPath","start_logo"],"liveareaTemplate":["livearea_template","template"],"webpage":["webPage","entrypoint","entryPoint","entry"],"launchPage":["launchPage","launch_page","launcher","defaultPage"]}
if k=="storageDirectory":
 for obj in (v,d):
  if isinstance(obj,dict):
   for key in ("storageDirectory","dataDirectory","storage_directory","data_directory"):
    if obj.get(key) not in (None,""):
     print(str(obj[key])); raise SystemExit(0)
   storage=obj.get("storage")
   if isinstance(storage,dict) and storage.get("directory") not in (None,""):
    print(str(storage["directory"])); raise SystemExit(0)
keys=[k]+a.get(k,[])
for src in (v,d):
 if isinstance(src,dict):
  for x in keys:
   if src.get(x) not in (None,""):
    print(str(src[x])); raise SystemExit(0)' "$file" "$key"
}

ask_yes_no() {
  # Returns a Y/yes answer without prompting when -y/--yes was supplied.
  # The helper is intentionally limited to confirmation prompts; ordinary
  # metadata/value prompts remain interactive.
  local prompt_text="$1" default_answer="${2:-Y}" answer
  if ((AUTO_YES)); then
    echo "$prompt_text y" >&2
    REPLY="y"
    return 0
  fi
  read -r -p "$prompt_text" answer || true
  REPLY="${answer:-$default_answer}"
}

json_has_key() {
  local file="$1" key="$2"
  python3 -c 'import json,sys
p,k=sys.argv[1:]
try:
 d=json.load(open(p,encoding="utf-8"))
except Exception:
 raise SystemExit(1)
v=d.get("vita",{}) if isinstance(d,dict) else {}
if not isinstance(v,dict): v={}
# Match json_get(): both vita.<key> and a top-level alias count.
# A key existing with null/empty-string is still considered present.
raise SystemExit(0 if k in v or (isinstance(d,dict) and k in d) else 1)' "$file" "$key"
}


if [[ -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" && -t 0 ]]; then
  if command -v python3 >/dev/null 2>&1; then
    META_NAME="$(json_get "$PACKAGE_JSON" name)"; META_TITLE_ID="$(json_get "$PACKAGE_JSON" titleId)"; META_SHORTNAME="$(json_get "$PACKAGE_JSON" shorthand)"; META_VERSION="$(json_get "$PACKAGE_JSON" version)"; META_SFO_VERSION="$(json_get "$PACKAGE_JSON" sfoVersion)"; META_BUILD_VERSION="$(json_get "$PACKAGE_JSON" buildVersion)"; META_AUTHOR="$(json_get "$PACKAGE_JSON" author)"; META_TARGET="$(json_get "$PACKAGE_JSON" target)"; META_ICON="$(json_get "$PACKAGE_JSON" icon)"; META_WALLPAPER="$(json_get "$PACKAGE_JSON" wallpaper)"; META_START_LOGO="$(json_get "$PACKAGE_JSON" startLogo)"; META_LIVEAREA_TEMPLATE="$(json_get "$PACKAGE_JSON" liveareaTemplate)"; META_WEBPAGE="$(json_get "$PACKAGE_JSON" webpage)"; META_LAUNCH_PAGE="$(json_get "$PACKAGE_JSON" launchPage)"; META_WEBAPP_DIR="$(json_get "$PACKAGE_JSON" webappDirectory)"; META_STORAGE_DIR="$(json_get "$PACKAGE_JSON" storageDirectory)"
    META_DEBUG="$(json_get "$PACKAGE_JSON" debug)"
    META_LAUNCH_PAGE_PRESENT=0
    if json_has_key "$PACKAGE_JSON" launchPage; then META_LAUNCH_PAGE_PRESENT=1; fi
    [[ -n "$META_WEBAPP_DIR" ]] || META_WEBAPP_DIR="$(cd "$(dirname "$PACKAGE_JSON")" && pwd)"
    echo
    echo "Found project metadata: $PACKAGE_JSON"
    echo "Preview of recognized Vita settings:"
    cwv_field Name "${META_NAME:-<not set>}" 19
    cwv_field "Title ID" "${META_TITLE_ID:-<not set>}" 19
    cwv_field Shorthand "${META_SHORTNAME:-<not set>}" 19
    cwv_field Version "${META_VERSION:-<not set>}" 19
    cwv_field Author "${META_AUTHOR:-<not set>}" 19
    cwv_field Target "${META_TARGET:-<not set>}" 19
    cwv_field Icon "${META_ICON:-<default>}" 19
    cwv_field Wallpaper "${META_WALLPAPER:-<default>}" 19
    cwv_field "Start logo" "${META_START_LOGO:-<default>}" 19
    cwv_field "LiveArea template" "${META_LIVEAREA_TEMPLATE:-<default>}" 19
    cwv_field "Webapp directory" "${META_WEBAPP_DIR:-$WEBAPP_DIR}" 19
    # Manifest "debug": true/1 enables the debug-overlay build;
    # "false"/"0" forces production.  The CLI --debug flag OVERRIDES
    # the manifest either way (user direction, 2026-10-06 (78)): an
    # explicit command-line request always wins over the file.
    if [[ "$DEBUG_CLI" -eq 1 ]]; then
      DEBUG_BUILD=1
    else
      case "${META_DEBUG,,}" in
        true|1)  DEBUG_BUILD=1 ;;
        false|0) DEBUG_BUILD=0 ;;
        *) : ;;
      esac
    fi
    if [[ "$DEBUG_BUILD" -eq 1 ]]; then cwv_field "Debug build" "${C_YEL}on${C_OFF} (debug overlay compiled in)" 19
    else cwv_field "Debug build" "off (production)" 19; fi
    cwv_field "Data directory" "${META_STORAGE_DIR:-<Title ID>}" 19
    if [[ "$META_LAUNCH_PAGE_PRESENT" -eq 1 && -z "$META_LAUNCH_PAGE" ]]; then
      cwv_field "Launch page" '<generated default launcher>' 19 19
    else
      cwv_field "Launch page" "${META_LAUNCH_PAGE:-<same as webpage>}" 19 19
    fi
    # (78) feature keys affect the COMPILE output (39) - show their state
    # here too, so every key that shapes the VPK is on screen at once.
    VITA_FEATURES=""
    for key in cursor zoom scroll devtools history files; do
      fval="$(python3 -c 'import json,sys
try:
 v=json.load(open(sys.argv[1],encoding="utf-8")).get("vita",{})
 print(str(v.get(sys.argv[2],"")).strip().lower())
except Exception: pass' "$PACKAGE_JSON" "$key")"
      if [[ "$fval" == "false" || "$fval" == "0" ]]; then VITA_FEATURES="$VITA_FEATURES $key:off"
      else VITA_FEATURES="$VITA_FEATURES $key:on"; fi
    done
    cwv_features_tree vita "$VITA_FEATURES"
    echo
    ask_yes_no "Use the provided JSON metadata? [Y/n or n -name -icon /s]: " Y
    use_meta="$REPLY"
    case "${use_meta:-Y}" in
      y|Y|yes|YES)
        [[ -n "$META_NAME" ]] && APP_NAME="$META_NAME"; [[ -n "$META_TITLE_ID" ]] && TITLE_ID="$META_TITLE_ID"; [[ -n "$META_SHORTNAME" ]] && SHORTNAME="$META_SHORTNAME"; [[ -n "$META_VERSION" ]] && VERSION="$META_VERSION"; [[ -n "$META_AUTHOR" ]] && AUTHOR="$META_AUTHOR"; [[ -n "$META_TARGET" ]] && TARGET="$META_TARGET"; [[ -n "$META_ICON" ]] && ICON="$META_ICON"; [[ -n "$META_WALLPAPER" ]] && WALLPAPER="$META_WALLPAPER"; [[ -n "$META_START_LOGO" ]] && START_LOGO="$META_START_LOGO"; [[ -n "$META_LIVEAREA_TEMPLATE" ]] && LIVEAREA_TEMPLATE="$META_LIVEAREA_TEMPLATE"; { [[ -n "$META_WEBAPP_DIR" ]] && [[ "$META_WEBAPP_DIR" != /* ]] && [[ -n "$PACKAGE_JSON" ]] && META_WEBAPP_DIR="$(dirname "$PACKAGE_JSON")/$META_WEBAPP_DIR"; [[ -n "$META_WEBAPP_DIR" ]] && WEBAPP_DIR="$META_WEBAPP_DIR" && WEBAPP_DIR_FROM_META=1; }; [[ -n "$META_STORAGE_DIR" ]] && APP_DATA_DIR="$META_STORAGE_DIR"; if [[ "$META_LAUNCH_PAGE_PRESENT" -eq 1 ]]; then if [[ -n "$META_LAUNCH_PAGE" ]]; then LAUNCH_PAGE="$META_LAUNCH_PAGE"; else LAUNCH_PAGE="__vita_default_launcher.html"; GENERATE_LAUNCH_PAGE=1; fi; fi ;;
      n*|N*)
        # Selective metadata mode. Example: `n -name -icon /s`. The -name/-icon
        # flags specify which values to change; /s writes those changes back to
        # package.json, while /p explicitly means do not persist them.
        META_EDIT_SAVE=0
        META_EDIT_FIELDS=()
        read -r -a META_ARGS <<< "${use_meta#?}"
        for arg in "${META_ARGS[@]}"; do
          case "$arg" in
            /s|/S) META_EDIT_SAVE=1 ;;
            /p|/P) META_EDIT_SAVE=0 ;;
            -name) META_EDIT_FIELDS+=(name) ;;
            -title-id|-titleid) META_EDIT_FIELDS+=(titleId) ;;
            -shorthand|-shortname) META_EDIT_FIELDS+=(shorthand) ;;
            -version) META_EDIT_FIELDS+=(version) ;;
            -author) META_EDIT_FIELDS+=(author) ;;
            -target) META_EDIT_FIELDS+=(target) ;;
            -icon) META_EDIT_FIELDS+=(icon) ;;
            -wallpaper) META_EDIT_FIELDS+=(wallpaper) ;;
            -start-logo|-startlogo) META_EDIT_FIELDS+=(startLogo) ;;
            -livearea-template|-template) META_EDIT_FIELDS+=(liveareaTemplate) ;;
            -webpage|-entrypoint) META_EDIT_FIELDS+=(webpage) ;;
            -launch-page|-launchpage) META_EDIT_FIELDS+=(launchPage) ;;
            -webapp-directory|-webappdir) META_EDIT_FIELDS+=(webappDirectory) ;;
            -storage-directory|-storage|-data-directory) META_EDIT_FIELDS+=(storageDirectory) ;;
            "") ;;
            *) echo "Unknown metadata flag: $arg" >&2; exit 1 ;;
          esac
        done
        if ((${#META_EDIT_FIELDS[@]}==0)); then
          echo "No metadata fields selected; ignoring JSON metadata."
        else
          meta_has_field(){ local needle="$1" x; for x in "${META_EDIT_FIELDS[@]}"; do [[ "$x" == "$needle" ]] && return 0; done; return 1; }
          meta_prompt(){ local field="$1" label="$2" current="$3" value; if meta_has_field "$field"; then read -r -p "$label [$current]: " value || true; printf '%s' "${value:-$current}"; else printf '%s' "$current"; fi; }
          APP_NAME="$(meta_prompt name "Name" "${META_NAME:-$APP_NAME}")"
          TITLE_ID="$(meta_prompt titleId "Title ID (9 chars)" "${META_TITLE_ID:-$TITLE_ID}")"
          SHORTNAME="$(meta_prompt shorthand "Shorthand" "${META_SHORTNAME:-$SHORTNAME}")"
          VERSION="$(meta_prompt version "Version" "${META_VERSION:-$VERSION}")"
          AUTHOR="$(meta_prompt author "Author" "${META_AUTHOR:-${APP_NAME:-CWebView}}")"
          TARGET="$(meta_prompt target "Native target" "${META_TARGET:-$TARGET}")"
          ICON="$(meta_prompt icon "Icon" "${META_ICON:-$ICON}")"
          WALLPAPER="$(meta_prompt wallpaper "Wallpaper" "${META_WALLPAPER:-$WALLPAPER}")"
          START_LOGO="$(meta_prompt startLogo "Start logo" "${META_START_LOGO:-$START_LOGO}")"
          LIVEAREA_TEMPLATE="$(meta_prompt liveareaTemplate "LiveArea template" "${META_LIVEAREA_TEMPLATE:-$LIVEAREA_TEMPLATE}")"
          WEBPAGE="$(meta_prompt webpage "Webpage entry point" "${META_WEBPAGE:-$WEBPAGE}")"
          if meta_has_field launchPage; then
            if [[ -n "$META_LAUNCH_PAGE" ]]; then
              LAUNCH_PAGE="$(meta_prompt launchPage "Launch page (empty = generated default)" "$META_LAUNCH_PAGE")"
            else
              read -r -p "Launch page (empty = generated default) [generated]: " launch_input || true
              if [[ -n "$launch_input" ]]; then LAUNCH_PAGE="$launch_input"; else LAUNCH_PAGE="__vita_default_launcher.html"; GENERATE_LAUNCH_PAGE=1; fi
            fi
          fi
          WEBAPP_DIR="$(meta_prompt webappDirectory "Webapp directory" "${META_WEBAPP_DIR:-$WEBAPP_DIR}")"
          [[ -n "$WEBAPP_DIR" && "$WEBAPP_DIR" != /* ]] && [[ -n "$PACKAGE_JSON" ]] && WEBAPP_DIR="$(dirname "$PACKAGE_JSON")/$WEBAPP_DIR"   # manifest-relative
          APP_DATA_DIR="$(meta_prompt storageDirectory "Application data directory" "${META_STORAGE_DIR:-${APP_DATA_DIR:-$TITLE_ID}}")"
          WEBAPP_DIR_FROM_META=1

          if ((META_EDIT_SAVE)); then
            if command -v python3 >/dev/null 2>&1; then
              export VITA_METADATA_SAVE_FIELDS="$(IFS=,; echo "${META_EDIT_FIELDS[*]}")"
              META_LAUNCH_PAGE_SAVE="$LAUNCH_PAGE"
              if [[ "$LAUNCH_PAGE" == "__vita_default_launcher.html" ]]; then META_LAUNCH_PAGE_SAVE=""; fi
              python3 - "$PACKAGE_JSON" "$APP_NAME" "$TITLE_ID" "$SHORTNAME" "$VERSION" "$AUTHOR" "$TARGET" "$ICON" "$WALLPAPER" "$START_LOGO" "$LIVEAREA_TEMPLATE" "$WEBPAGE" "$META_LAUNCH_PAGE_SAVE" "$WEBAPP_DIR" "$APP_DATA_DIR" <<'PY'
import json,sys
path,*values=sys.argv[1:]
keys=['name','titleId','shorthand','version','author','target','icon','wallpaper','startLogo','liveareaTemplate','webpage','launchPage','webappDirectory','storageDirectory']
try:
    with open(path,encoding='utf-8') as f: data=json.load(f)
except Exception as e:
    print(f'Unable to read {path}: {e}',file=sys.stderr); raise SystemExit(1)
if not isinstance(data,dict): data={}
vita=data.get('vita')
if not isinstance(vita,dict): vita={}; data['vita']=vita
# Only selected fields are persisted; values for unselected fields are left untouched.
# The shell passes the selected field list through an environment variable.
selected=set(filter(None, __import__('os').environ.get('VITA_METADATA_SAVE_FIELDS','').split(',')))
for key,value in zip(keys,values):
    if key in selected:
        vita[key]=value
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
      *) echo "Ignoring JSON metadata; using manual/default values." ;;
    esac
  else
    echo "Found $PACKAGE_JSON, but python3 is unavailable; skipping metadata import."
  fi
fi

if [[ -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" && "$GENERATE_LAUNCH_PAGE" -eq 0 && -z "${LAUNCH_PAGE:-}" && -t 0 ]] && command -v python3 >/dev/null 2>&1; then
  if json_has_key "$PACKAGE_JSON" launchPage; then
    META_LAUNCH_PAGE="$(json_get "$PACKAGE_JSON" launchPage)"
    if [[ -z "$META_LAUNCH_PAGE" ]]; then
      LAUNCH_PAGE="__vita_default_launcher.html"
      GENERATE_LAUNCH_PAGE=1
    fi
  fi
fi

if [[ -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" && -t 0 && -z "${WEBPAGE:-}" ]]; then
  if command -v python3 >/dev/null 2>&1; then
    META_WEBPAGE="$(json_get "$PACKAGE_JSON" webpage)"
    if [[ -n "$META_WEBPAGE" ]]; then
      echo
      echo "Found webpage entry point in $PACKAGE_JSON: $META_WEBPAGE"
      ask_yes_no "Use the provided webpage? [Y/n]: " Y
      use_webpage="$REPLY"
      case "${use_webpage:-Y}" in
        y|Y|yes|YES) WEBPAGE="$META_WEBPAGE" ;;
        *) echo "Ignoring JSON webpage; using the normal webpage prompt." ;;
      esac
    fi
  fi
fi

prompt(){ local var="$1" label="$2" default="$3" value; if [[ -n "${!var:-}" ]]; then return; fi; if [[ -t 0 ]]; then read -r -p "$label [$default]: " value || true; printf -v "$var" '%s' "${value:-$default}"; else printf -v "$var" '%s' "$default"; fi; }
prompt APP_NAME "Name" "CWebView"; prompt TITLE_ID "Title ID (9 chars)" "CWV00001"; prompt SHORTNAME "Shorthand" "CWV"; prompt VERSION "Production version" "${META_VERSION:-0.1.1}"
if [[ -z "${SFO_VERSION:-}" ]]; then SFO_VERSION="${META_SFO_VERSION:-01.01}"; fi
if [[ -z "${BUILD_VERSION:-}" ]]; then BUILD_VERSION="${META_BUILD_VERSION:-0.4.4.5}"; fi
prompt AUTHOR "Author" "${APP_NAME:-CWebView}"; prompt TARGET "Native target" "CWebView"
if [[ -z "${APP_DATA_DIR:-}" ]]; then APP_DATA_DIR="${TITLE_ID}"; fi
if [[ -n "${CWV_WEBAPP_OVERRIDE:-}" ]]; then WEBAPP_DIR="$CWV_WEBAPP_OVERRIDE"; WEBAPP_DIR_FROM_META=1;
elif [[ -t 0 && "$WEBAPP_DIR_FROM_META" -eq 0 ]]; then read -r -p "Webapp directory [$WEBAPP_DIR]: " input || true; WEBAPP_DIR="${input:-$WEBAPP_DIR}"; fi
if [[ -z "${WEBPAGE:-}" ]]; then if [[ -t 0 ]]; then read -r -p "Webpage entry point [index.html]: " input || true; WEBPAGE="${input:-index.html}"; else WEBPAGE="index.html"; fi; fi
if [[ -z "${LAUNCH_PAGE:-}" ]]; then LAUNCH_PAGE="$WEBPAGE"; fi
# A package.json webappDirectory is relative to that package.json, not to the
# shell's current working directory. Keep environment/interactive paths as-is.
if [[ "$WEBAPP_DIR_FROM_META" -eq 1 && "$WEBAPP_DIR" != /* && -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" ]]; then
  WEBAPP_DIR="$(cd "$(dirname "$PACKAGE_JSON")" && pwd)/$WEBAPP_DIR"
  WEBAPP_DIR="$(cd "$WEBAPP_DIR" 2>/dev/null && pwd || printf '%s' "$WEBAPP_DIR")"
fi

resolve_asset_path(){
  local value="$1"
  [[ -z "$value" ]] && { printf '%s' ""; return; }
  if [[ "$value" = /* ]]; then printf '%s' "$value"; return; fi
  local base
  if [[ -n "$PACKAGE_JSON" && -f "$PACKAGE_JSON" ]]; then base="$(cd "$(dirname "$PACKAGE_JSON")" && pwd)"; else base="$ROOT"; fi
  printf '%s/%s' "$base" "$value"
}
# Per-console packaging images live WITH THE WEBAPP (user direction,
# 2026-09-20 (16)): webapp/vita/{icon,wallpaper,start-logo}.png - the
# old project-assets defaults remain as fallbacks for trees that keep
# them there, and custom values still go through resolve_asset_path().
console_image_default() {
  local rel="$1" fallback="$2" c
  for c in "$WEBAPP_DIR" "$ROOT/webapp" "$ROOT/web"; do
    [[ -n "$c" && -f "$c/$rel" ]] && { printf '%s' "$c/$rel"; return; }
  done
  # (78) the four default packaging images live in sources/vita/assets
  # (their CMake home); the copies that used to sit in /assets/ were
  # byte-identical duplicates and are gone.
  printf '%s' "$ROOT/sources/vita/assets/$fallback"
}
ICON="${ICON:-$(console_image_default vita/icon.png default_icon.png)}"
WALLPAPER="${WALLPAPER:-$(console_image_default vita/wallpaper.png default_wallpaper.png)}"
START_LOGO="${START_LOGO:-$(console_image_default vita/start-logo.png default_start_logo.png)}"
# LiveArea template (79): every packaging asset is webapp-overridable.
# Manifest vita.liveareaTemplate / --livearea-template (absolute or
# webapp-relative), convention webapp/vita/template.xml, then the repo
# default sources/vita/assets/template.xml.  A custom template must
# keep the <image>/<startup-image> filenames (bg0.png / startup.png -
# they are what CMake stages the wallpaper/start logo as).
LIVEAREA_TEMPLATE="${LIVEAREA_TEMPLATE:-$(console_image_default vita/template.xml template.xml)}"
ICON="$(resolve_asset_path "$ICON")"
WALLPAPER="$(resolve_asset_path "$WALLPAPER")"
START_LOGO="$(resolve_asset_path "$START_LOGO")"
LIVEAREA_TEMPLATE="$(resolve_asset_path "$LIVEAREA_TEMPLATE")"
if [[ -n "$LIVEAREA_TEMPLATE" && ! -f "$LIVEAREA_TEMPLATE" ]]; then
  echo "LiveArea template not found: $LIVEAREA_TEMPLATE - using the default." >&2
  LIVEAREA_TEMPLATE="$ROOT/sources/vita/assets/template.xml"
fi

# VitaShell is unusually strict about LiveArea PNGs.  The images under
# sce_sys must be 8-bit indexed (palette) PNGs; RGBA/RGB true-color PNGs can
# make VitaShell abort installation with 0x8010113D even when the VPK itself
# is otherwise valid.  Normalize all three assets at build time so both the
# built-in defaults and user-supplied images follow the same rule.
ASSET_TMP_DIR="$BUILD_DIR/livearea-assets"
rm -rf "$ASSET_TMP_DIR"
mkdir -p "$ASSET_TMP_DIR"
trap 'rm -rf "$ASSET_TMP_DIR"' EXIT

normalize_vita_png() {
  local src="$1" dst="$2" expected="$3" label="$4"
  [[ -f "$src" ]] || { echo "$label not found: $src" >&2; exit 1; }
  command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1 || {
    echo "ImageMagick is required to package Vita LiveArea PNGs safely." >&2
    echo "Install ImageMagick (the 'magick' or 'convert' command) and rebuild." >&2
    exit 1
  }
  local dims
  dims="$(identify -format '%wx%h' "$src" 2>/dev/null || true)"
  [[ "$dims" == "$expected" ]] || {
    echo "$label must be exactly $expected; got ${dims:-unknown}: $src" >&2
    exit 1
  }
  if command -v magick >/dev/null 2>&1; then
    magick "$src" -colors 256 -type Palette -define png:color-type=3 PNG8:"$dst"
  else
    convert "$src" -colors 256 -type Palette -define png:color-type=3 PNG8:"$dst"
  fi
  local type
  type="$(file -b "$dst" 2>/dev/null || true)"
  [[ "$type" == *"8-bit colormap"* && "$type" == *"non-interlaced"* ]] || {
    echo "Failed to create Vita-compatible indexed PNG for $label: $dst" >&2
    echo "Detected: $type" >&2
    exit 1
  }
}

normalize_vita_png "$ICON" "$ASSET_TMP_DIR/icon0.png" "128x128" "Icon"
normalize_vita_png "$WALLPAPER" "$ASSET_TMP_DIR/bg0.png" "840x500" "Wallpaper"
normalize_vita_png "$START_LOGO" "$ASSET_TMP_DIR/startup.png" "280x158" "Start logo"
ICON="$ASSET_TMP_DIR/icon0.png"
WALLPAPER="$ASSET_TMP_DIR/bg0.png"
START_LOGO="$ASSET_TMP_DIR/startup.png"

# ---------------------------------------------------------------------------
# Build dependency management.  Missing host tools are installed via the
# system package manager; a missing (or outdated, with --update-sdk)
# VitaSDK is bootstrapped from the official vdpm installer.  Everything
# installs into the $VITASDK chosen above - never a hardcoded codebase path.
# ---------------------------------------------------------------------------
confirm() {  # confirm <question>; honors -y
  [[ $AUTO_YES -eq 1 ]] && return 0
  local reply
  read -r -p "$1 [y/N] " reply || return 1
  [[ "$reply" == [Yy]* ]]
}
ensure_host_tools() {
  local missing=()
  for t in git make cmake python3; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Missing host tools: ${missing[*]}"
    if command -v apt-get >/dev/null 2>&1; then
      confirm "Install them with apt-get now?" || { echo "Install the tools above, then re-run." >&2; exit 1; }
      SUDO=""; [[ $EUID -ne 0 ]] && SUDO="sudo"
      $SUDO apt-get update -y && $SUDO apt-get install -y "${missing[@]}"
    else
      echo "This system has no apt-get; install: ${missing[*]} manually, then re-run." >&2
      exit 1
    fi
  fi
}
ensure_vitasdk() {
  # --update-sdk: refresh an existing install to the latest upstream.
  if [[ ${UPDATE_SDK:-0} -eq 1 ]]; then
    if [[ -x "$VITASDK/bin/vdpm" ]]; then
      echo "Updating VitaSDK ports to latest via vdpm..."
      "$VITASDK/bin/vdpm" update && "$VITASDK/bin/vdpm" upgrade || { echo "vdpm update failed; see the messages above." >&2; exit 1; }
      echo "VitaSDK updated at $VITASDK"
    else
      echo "--update-sdk: no vdpm at $VITASDK; a fresh bootstrap would refresh everything anyway." >&2
    fi
    return 0
  fi
  # Fresh install when absent.
  if [[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then return 0; fi
  [[ ${NO_SDK_INSTALL:-0} -eq 1 ]] && { echo "VitaSDK not found at $VITASDK (--no-sdk-install given)." >&2; exit 1; }
  echo "VitaSDK not found at $VITASDK"
  confirm "Download and install the VitaSDK there now? (official vdpm bootstrap; needs internet, can take 10+ minutes)" || {
    echo "Re-run with VITASDK pointing at an existing SDK, or install manually: https://vitasdk.org" >&2; exit 1; }
  mkdir -p "$BUILD_DIR"
  rm -r "$BUILD_DIR/vdpm" 2>/dev/null || true
  git clone --depth=1 https://github.com/vitasdk/vdpm "$BUILD_DIR/vdpm"
  ( cd "$BUILD_DIR/vdpm" && ./bootstrap-vitasdk.sh -p "$VITASDK" && ./install_all.sh ) || {
    echo "vdpm bootstrap failed; see the messages above." >&2; exit 1; }
  [[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ]] || { echo "Bootstrap finished but the compiler is missing; the install did not complete." >&2; exit 1; }
  echo "VitaSDK installed at $VITASDK"
}
ensure_host_tools
ensure_vitasdk
[[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ]] || { echo "VitaSDK not found at $VITASDK" >&2; exit 1; }

# CWebView links libcurl statically.  The installed VitaSDK curl archive
# uses OpenSSL, so all three archives must be present before CMake is invoked.
# Checking here turns the old wall of OpenSSL undefined-reference errors into
# one actionable message.
VITA_LIB_DIR="$VITASDK/arm-vita-eabi/lib"
for required in \
  "$VITA_LIB_DIR/libcurl.a" \
  "$VITA_LIB_DIR/libssl.a" \
  "$VITA_LIB_DIR/libcrypto.a" \
  "$VITA_LIB_DIR/libbz2.a" \
  "$VITA_LIB_DIR/libzstd.a"; do
  [[ -f "$required" ]] || MISSING_PORTS=1
done
if [[ ${MISSING_PORTS:-0} -eq 1 ]]; then
  echo "Missing VitaSDK libraries under $VITA_LIB_DIR (curl/OpenSSL/bzip2/zstd ports)."
  if [[ -x "$VITASDK/bin/vdpm" ]]; then
    confirm "Install the missing ports with vdpm now?" || { echo "Install them manually (vdpm install curl openssl bzip2 zstd), then re-run." >&2; exit 1; }
    "$VITASDK/bin/vdpm" install curl openssl bzip2 zstd || { echo "vdpm port install failed; see the messages above." >&2; exit 1; }
    for required in \
      "$VITA_LIB_DIR/libcurl.a" \
      "$VITA_LIB_DIR/libssl.a" \
      "$VITA_LIB_DIR/libcrypto.a" \
      "$VITA_LIB_DIR/libbz2.a" \
      "$VITA_LIB_DIR/libzstd.a"; do
      [[ -f "$required" ]] || { echo "Still missing after vdpm install: $required" >&2; exit 1; }
    done
  else
    echo "vdpm not found; install the ports (vdpm install curl openssl bzip2 zstd), then re-run." >&2
    exit 1
  fi
fi

[[ -z "$ICON" || -f "$ICON" ]] || { echo "Icon not found: $ICON" >&2; exit 1; }
[[ -z "$WALLPAPER" || -f "$WALLPAPER" ]] || { echo "Wallpaper not found: $WALLPAPER" >&2; exit 1; }
[[ -z "$START_LOGO" || -f "$START_LOGO" ]] || { echo "Start logo not found: $START_LOGO" >&2; exit 1; }
[[ -f "$WEBAPP_DIR/$WEBPAGE" ]] || { echo "Webpage entry point not found: $WEBAPP_DIR/$WEBPAGE" >&2; exit 1; }
if [[ "$LAUNCH_PAGE" == "__vita_default_launcher.html" ]]; then
  GENERATE_LAUNCH_PAGE=1
else
  [[ -f "$WEBAPP_DIR/$LAUNCH_PAGE" ]] || { echo "Launch page not found: $WEBAPP_DIR/$LAUNCH_PAGE" >&2; exit 1; }
fi
[[ "$TITLE_ID" =~ ^[A-Za-z0-9_-]{9}$ ]] || { echo "Title ID must be exactly 9 characters." >&2; exit 1; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?([.-][A-Za-z0-9]+)?$ ]] || { echo "Production version must look like 0.1.1." >&2; exit 1; }
[[ "$SFO_VERSION" =~ ^[0-9]{2}\.[0-9]{2}$ ]] || { echo "SFO version must use ##.## (got: $SFO_VERSION)." >&2; exit 1; }
[[ -n "$BUILD_VERSION" ]] || { echo "Build version cannot be empty." >&2; exit 1; }
# (83) ONE optimization stage, shared by every port (tools/optimize_web.sh):
# minify (comments/CSS whitespace; JS and binaries untouched) + CWI
# packing + a combined savings line, with the same degrade-gracefully
# rules on every platform.  The readable webapp/ sources stay the single
# source of truth; only this staged copy is compacted.
MINIFIED_WEBAPP_DIR="$BUILD_DIR/webapp_min"
# shellcheck disable=SC1091
source "$ROOT/tools/optimize_web.sh" && optimize_web "$WEBAPP_DIR" "$MINIFIED_WEBAPP_DIR" vita
echo "Webapp staged into the VPK copy: $MINIFIED_WEBAPP_DIR"
# Console-specific packaging images (webapp/vita, webapp/wiiu, webapp/wii)
# are consumed at BUILD time only (LiveArea icon/bg0/startup above read
# them from the SOURCE webapp); the renderer never loads them at runtime,
# so they must NOT ship inside the VPK's web content - the Vita package
# would otherwise carry the Wii U/Wii folders too.  Verified: zero runtime
# references in sources/ (grep wallpaper|logo -> build scripts only).
rm -r -f "$MINIFIED_WEBAPP_DIR/vita" "$MINIFIED_WEBAPP_DIR/wiiu" "$MINIFIED_WEBAPP_DIR/wii"

# Pointer cursor art (2026-09-23 (38)): the APP's shared assets, not the
# webapp.  Resolution order: the manifest's vita.cursor (webapp-relative
# like "icon"), then the repo's assets/cursor-vita.png.  Staged into the
# VPK at assets/cursor-vita.png; the runtime path app0:/assets/cursor-vita.png
# is unchanged.  Missing art = code-drawn arrow (no build failure).
# BUG 41 (hardware report 09/25/2026): this block must run BEFORE
# CMAKE_ARGS consumes $VITA_CURSOR below - it used to sit after it, so
# the -DVITA_CURSOR_PNG line read an unset variable under set -u and the
# build aborted with "VITA_CURSOR: unbound variable".  The manifest key
# also doubles as the FEATURE switch: boolean true = feature on + default
# art (NOT an art path named "True"), so only STRING values are art paths.
VITA_CURSOR="${VITA_CURSOR:-}"
if [[ -z "$VITA_CURSOR" && -f "$PACKAGE_JSON" ]] && command -v python3 >/dev/null 2>&1; then
  VITA_CURSOR="$(python3 -c 'import json,sys
try:
 v=json.load(open(sys.argv[1],encoding="utf-8")).get("vita",{}).get("cursor","")
 print(v if isinstance(v,str) else "")
except Exception: pass' "$PACKAGE_JSON")"
fi
[[ -z "$VITA_CURSOR" ]] || VITA_CURSOR="$(resolve_asset_path "$VITA_CURSOR")"
if [[ -z "$VITA_CURSOR" || ! -f "$VITA_CURSOR" ]]; then
  # convention (79): webapp/vita/cursor.png, like the packaging images
  for c in "$WEBAPP_DIR" "$ROOT/webapp" "$ROOT/web"; do
    [[ -n "$c" && -f "$c/vita/cursor.png" ]] && { VITA_CURSOR="$c/vita/cursor.png"; break; }
  done
fi
if [[ -z "$VITA_CURSOR" || ! -f "$VITA_CURSOR" ]]; then
  VITA_CURSOR="$ROOT/assets/cursor-vita.png"
fi
if [[ -f "$VITA_CURSOR" ]]; then
  echo "Cursor art: $VITA_CURSOR (packaged as assets/cursor-vita.png)"
else
  echo "Cursor art: none found - the code-drawn arrow will be used."
  VITA_CURSOR=""
fi
# CSS cursor pack (2026-09-26 (45)): assets/cursors/<keyword>.png, lazily
# loaded from app0:/assets/cursors by cwv_cursor.c.  Optional - missing art
# falls back to the console cursor art, then the code arrow.
if [[ -d "$ROOT/assets/cursors" ]]; then
  mkdir -p "$VITA_DIR/assets/cursors"
  cp "$ROOT"/assets/cursors/*.png "$VITA_DIR/assets/cursors/" 2>/dev/null || true
  echo "CSS cursor pack: $ROOT/assets/cursors -> assets/cursors/"
fi

CMAKE_ARGS=(
  -S "$VITA_DIR" -B "$BUILD_DIR"
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
  -DVITA_WEBAPP_DIR="$MINIFIED_WEBAPP_DIR" -DCWV_WEBPAGE="$WEBPAGE"
  -DCWV_LAUNCH_PAGE="$LAUNCH_PAGE" -DVITA_GENERATE_LAUNCH_PAGE="$GENERATE_LAUNCH_PAGE"
  -DVITA_APP_NAME="$APP_NAME" -DVITA_TITLEID="$TITLE_ID" -DVITA_SHORTNAME="$SHORTNAME"
  -DCWV_APP_VERSION="${META_VERSION:-0.0}" -DVITA_VERSION="$SFO_VERSION" -DVITA_PRODUCTION_VERSION="$VERSION" -DVITA_BUILD_VERSION="$BUILD_VERSION" -DVITA_AUTHOR="$AUTHOR" -DVITA_TARGET="$TARGET"
  -DCWV_APP_DATA_DIR="$APP_DATA_DIR" -DVITA_ICON="$ICON" -DVITA_WALLPAPER="$WALLPAPER"
  -DVITA_CURSOR_PNG="$VITA_CURSOR"
  -DVITA_START_LOGO="$START_LOGO"
  -DVITA_LIVEAREA_TEMPLATE="$LIVEAREA_TEMPLATE"
  -DCWV_DEBUG="$DEBUG_BUILD"
)

# Project feature keys (2026-09-23 (39)): package.json vita.{cursor,zoom,
# scroll,devtools,history} = false -> -DCWV_<KEY>_ENABLED=0 for CMake
# (absent/true = enabled).  Mirrors build-wiiu.sh / build-wii.sh.
if [[ -f "$PACKAGE_JSON" ]] && command -v python3 >/dev/null 2>&1; then
  for key in cursor zoom scroll devtools history files; do
    val="$(python3 -c 'import json,sys
try:
 v=json.load(open(sys.argv[1],encoding="utf-8")).get("vita",{})
 print(str(v.get(sys.argv[2],"")).strip().lower())
except Exception: pass' "$PACKAGE_JSON" "$key")"
    if [[ "$val" == "false" || "$val" == "0" ]]; then
      CMAKE_ARGS+=("-DCWV_${key^^}_ENABLED=0")
      echo "[vita] project key '$key': disabled"
    fi
  done
fi

# WSL/Windows-mounted trees can expose generated files with timestamps slightly
# ahead of the Linux clock. Normalize the build tree before invoking CMake so
# make does not report "Clock skew detected" or make incorrect incremental decisions.
if [[ -d "$BUILD_DIR" ]]; then
  find "$BUILD_DIR" -type f -exec touch {} + 2>/dev/null || true
fi
# (80) native CMake colors (green compile, blue link/convert) + gcc
# diagnostics colors (red error:, yellow warning:) are forced ON whenever
# the shared palette is on - CMake only colors when it sees a TTY, and
# some wrappers (tmux/ssh/WSL launchers) hide it.
if [[ $CWV_USE_COLOR -eq 1 ]]; then
  export CLICOLOR_FORCE=1 GCC_COLORS="${GCC_COLORS:-error=01;31:warning=01;33:note=01;36:caret=01;32:locus=01:quote=01}"
  CMAKE_ARGS+=(-DCMAKE_COLOR_MAKEFILE=ON -DCMAKE_COLOR_DIAGNOSTICS=ON)
fi
cmake "${CMAKE_ARGS[@]}"
find "$BUILD_DIR" -type f -exec touch {} + 2>/dev/null || true
cmake --build "$BUILD_DIR" --target "${TARGET}-vpk" --clean-first -j"$(nproc)"
VPK="$BUILD_DIR/$TARGET.vpk"
[[ -f "$VPK" ]] || { echo "VPK was not produced: $VPK" >&2; exit 1; }
unzip -t "$VPK" >/dev/null || { echo "VPK archive validation failed: $VPK" >&2; exit 1; }
python3 - "$VPK" "$WEBPAGE" "$LAUNCH_PAGE" <<'PY'
import sys, zipfile

vpk, webpage, launch_page = sys.argv[1], sys.argv[2], sys.argv[3]
required = {
    "eboot.bin",
    "sce_sys/param.sfo",
    "sce_sys/icon0.png",
    "sce_sys/livearea/contents/bg0.png",
    "sce_sys/livearea/contents/startup.png",
    "sce_sys/livearea/contents/template.xml",
    f"web/{webpage}",
    f"web/{launch_page}",
    "sce_module/CwvScroll.suprx",
}
with zipfile.ZipFile(vpk) as zf:
    names = zf.namelist()
missing = sorted(required - set(names))
if missing:
    raise SystemExit("VPK is missing: " + ", ".join(missing))
if names.count("sce_sys/param.sfo") != 1:
    raise SystemExit("VPK must contain exactly one sce_sys/param.sfo entry")
PY
# unified output tree (user direction 2026-10-02): build/vita/<target>.vpk
mkdir -p "$ROOT/build/vita"
cp -f "$VPK" "$ROOT/build/vita/$TARGET.vpk"
echo "VPK validated and copied successfully."
echo "Built: $ROOT/build/vita/$TARGET.vpk"
