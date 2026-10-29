#!/usr/bin/env bash
# build.sh - CWebView single entry point (user direction 2026-10-07 (81)).
#
#   ./build.sh                    interactive menu (pick by number or name)
#   ./build.sh vita               lowercase name
#   ./build.sh WII               uppercase name
#   ./build.sh 2                 by number
#   ./build.sh all               every platform, in order, with a summary
#   ./build.sh init              scaffold a new app (asks about every key)
#   ./build.sh [target] -y ...   extra args pass through to the script
#
# The build scripts themselves live in sources/ (moved there by (81));
# this wrapper only picks one and hands the arguments over.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT/tools/cwv_term.sh"

# ---- target grammar: number / lowercase / uppercase ----------------------
# Order = the menu shown below; numbers are stable API now.
TARGETS=(vita wii wiiu windows)

resolve_target() {
  local q="${1:-}" i t
  [[ -z "$q" ]] && return 1
  # by number (1..N)
  if [[ "$q" =~ ^[0-9]+$ ]]; then
    (( q >= 1 && q <= ${#TARGETS[@]} )) || return 1
    printf '%s' "${TARGETS[q-1]}"
    return 0
  fi
  # by name, any case
  for i in "${!TARGETS[@]}"; do
    t="${TARGETS[i]}"
    [[ "${q,,}" == "$t" ]] && { printf '%s' "$t"; return 0; }
  done
  return 1
}

script_for() { printf '%s/sources/build-%s.sh' "$ROOT" "$1"; }

usage() {
  cat <<'USAGE'
Usage: ./build.sh [target] [extra args...]

  target:  vita | wii | wiiu | windows | all | init
           (any case: WII, Vita, WINDOWS also work)
           or the menu number: 1=vita 2=wii 3=wiiu 4=windows 5=all
           no target + a terminal = interactive menu
           (the prompt takes extra args too: "vita -y" == build.sh vita -y)

  --webapp DIR  build using this exact web-content directory (all platforms)
  extra args pass through to the chosen build script
  (e.g. -y, --debug for vita).

  init: scaffold a new app - creates/updates webapp/package.json,
        asking about every key (Enter keeps the shown default).
        With -y it writes all defaults without asking.
USAGE
}

# ---- init: ask about every manifest key ---------------------------------
init_app() {
  local auto_yes=0
  [[ "${1:-}" == "-y" ]] && auto_yes=1
  command -v python3 >/dev/null 2>&1 || { cwv_err "init needs python3 (JSON read/write)."; exit 1; }
  [[ -t 0 ]] || auto_yes=1   # no terminal: write defaults silently

  local pj="$ROOT/webapp/package.json"
  [[ -f "$pj" ]] && cwv_head "Existing manifest found - Enter keeps the current value."

  ask() {  # ask VAR "Prompt" "default"  -> sets REPLY_VALUE
    local __var="$1" __prompt="$2" __def="$3" __reply=""
    if [[ $auto_yes -eq 1 ]]; then eval "$__var=\"\$__def\""; return; fi
    printf '%s%s%s [%s]: ' "$C_CYN" "$__prompt" "$C_OFF" "$__def"
    read -r __reply || true
    [[ -z "$__reply" ]] && __reply="$__def"
    eval "$__var=\"\$__reply\""
  }
  ask_bool() {  # ask_bool VAR "Prompt" default(true/false) -> 1/0
    local __var="$1" __prompt="$2" __def="$3" __reply=""
    if [[ $auto_yes -eq 1 ]]; then eval "$__var=\"\$__def\""; return; fi
    printf '%s%s%s [%s]: ' "$C_CYN" "$__prompt" "$C_OFF" "$__def"
    read -r __reply || true
    [[ -z "$__reply" ]] && __reply="$__def"
    case "${__reply,,}" in y|yes|true|1|on) eval "$__var=true" ;; *) eval "$__var=false" ;; esac
  }

  echo; cwv_head "Common keys"
  ask  APP_NAME        "App name"                     "$(python3 -c 'import json;print(json.load(open("webapp/package.json")).get("name","My CWebView App"))' 2>/dev/null || echo 'My CWebView App')"
  ask  APP_SHORT       "Shorthand (3-4 letters)"      "CWV"
  ask  APP_VERSION     "Version"                      "00.10"
  ask  APP_AUTHOR      "Author"                       "Me"
  ask  APP_TARGET      "Native target name"           "CWebView"
  ask  APP_DESC        "Description"                  "$APP_NAME on Wii, Wii U, PS Vita and Windows"
  ask  APP_PAGE        "Webpage (entry file)"         "index.html"
  ask  APP_DIR         "Webapp directory"             "."
  ask  APP_ICON        "Icon (webapp-relative)"       "icon.png"

  echo; cwv_head "Vita keys"
  ask  V_TITLEID       "Title ID (9 chars)"           "CWEBVIEWX"
  ask  V_ICON          "Vita icon (webapp-relative)"  "vita/icon.png"
  ask  V_WALL          "LiveArea wallpaper"           "vita/wallpaper.png"
  ask  V_LOGO          "Start logo"                   "vita/start-logo.png"
  ask  V_TEMPLATE      "LiveArea template.xml"        "vita/template.xml"
  ask  V_STORAGE       "Data dir under ux0:data/"     "Webpage"
  ask  V_LAUNCH        "Launch page (empty = generated launcher)" ""
  ask  V_SFOVER        "SFO version (##.##)"          "01.00"

  echo; cwv_head "Wii / Wii U keys"
  ask  W_ICON          "Wii icon (webapp-relative)"   "wii/icon.png"
  ask  W_CURSOR        "Wii cursor art (blank = default)" ""
  ask  U_ICON          "Wii U icon (webapp-relative)" "wiiu/icon.png"
  ask  U_CURSOR        "Wii U cursor art (blank = default)" ""

  echo; cwv_head "Features (per console)"
  ask_bool F_CURSOR  "Cursor support"        true
  ask_bool F_ZOOM     "Zoom"                 true
  ask_bool F_SCROLL   "Scroll"               true
  ask_bool F_DEVTOOLS "DevTools"             true
  ask_bool F_HISTORY  "History"              true
  ask_bool F_DEBUG    "Vita debug build"     false

  if [[ $auto_yes -eq 0 ]]; then
    echo; read -r -p "Write webapp/package.json now? [Y/n] " ok || ok=y
    [[ "${ok:-Y}" == [Nn]* ]] && { cwv_warn "init cancelled - nothing written."; exit 0; }
  fi

  mkdir -p "$ROOT/webapp"
  [[ -f "$ROOT/webapp/$APP_PAGE" ]] || cat > "$ROOT/webapp/$APP_PAGE" <<'HTML'
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>New CWebView app</title></head>
<body>
  <h1>It runs!</h1>
  <p>Edit webapp/ and rebuild with ./build.sh &lt;platform&gt;</p>
</body>
</html>
HTML

  python3 - "$pj" "$APP_NAME" "$APP_SHORT" "$APP_VERSION" "$APP_AUTHOR" \
              "$APP_TARGET" "$APP_DESC" "$APP_PAGE" "$APP_DIR" "$APP_ICON" \
              "$V_TITLEID" "$V_ICON" "$V_WALL" "$V_LOGO" "$V_TEMPLATE" \
              "$V_STORAGE" "$V_LAUNCH" "$V_SFOVER" "$W_ICON" "$W_CURSOR" \
              "$U_ICON" "$U_CURSOR" \
              "$F_CURSOR" "$F_ZOOM" "$F_SCROLL" "$F_DEVTOOLS" "$F_HISTORY" "$F_DEBUG" <<'PY'
import json, os, sys
pj, name, short, ver, author, target, desc, page, adir, icon, \
    v_tid, v_icon, v_wall, v_logo, v_tpl, v_store, v_launch, v_sfover, \
    w_icon, w_cursor, u_icon, u_cursor, f_cur, f_zoom, f_scroll, f_dev, f_hist, f_dbg = sys.argv[1:]

def feat(x): return x == "true"

data = {}
if os.path.exists(pj):
    try: data = json.load(open(pj, encoding="utf-8"))
    except Exception as e: sys.exit(f"cannot parse existing {pj}: {e}")
data.setdefault("$schema", "./cwebview.schema.json")
data.update({"name": name, "shorthand": short, "version": ver, "author": author,
             "target": target, "webpage": page, "webappDirectory": adir,
             "description": desc, "icon": icon})
vita = data.setdefault("vita", {})
vita.update({"name": name, "shorthand": short, "version": ver, "author": author,
             "webpage": page, "webappDirectory": adir, "titleId": v_tid,
             "icon": v_icon, "wallpaper": v_wall, "startLogo": v_logo,
             "liveareaTemplate": v_tpl, "storageDirectory": v_store,
             "launchPage": v_launch, "sfoVersion": v_sfover,
             "cursor": feat(f_cur), "zoom": feat(f_zoom), "scroll": feat(f_scroll),
             "devtools": feat(f_dev), "history": feat(f_hist), "debug": feat(f_dbg)})
if v_launch: vita["launchPage"] = v_launch
wii = data.setdefault("wii", {})
wii.update({"name": name, "shorthand": short, "version": ver, "author": author,
            "description": desc, "webpage": page, "webappDirectory": adir,
            "icon": w_icon,
            "cursor": feat(f_cur), "zoom": feat(f_zoom), "scroll": feat(f_scroll),
            "devtools": feat(f_dev), "history": feat(f_hist)})
# cursor is dual-purpose (39/78): a STRING value is cursor art, a bool is the feature switch
if w_cursor: wii["cursor"] = w_cursor
wiiu = data.setdefault("wiiu", {})
wiiu.update({"name": name, "shorthand": short, "version": ver, "author": author,
             "webpage": page, "webappDirectory": adir, "icon": u_icon,
             "description": desc,
             "cursor": feat(f_cur), "zoom": feat(f_zoom), "scroll": feat(f_scroll),
             "devtools": feat(f_dev), "history": feat(f_hist)})
if u_cursor: wiiu["cursor"] = u_cursor
json.dump(data, open(pj, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
open(pj, "a", encoding="utf-8").write("\n")
print(f"wrote {pj}")
PY
  cwv_ok "init complete - build with: ./build.sh <platform>"
}

# ---- dispatch -------------------------------------------------------------
main() {
  local target="${1:-}" ; shift || true
  # Normalize the web-content override centrally so the same flag works on
  # every platform, including Windows and Wii (which historically differed).
  local webapp_override="" arg
  local -a forwarded=()
  while (($#)); do
    arg="$1"; shift
    case "$arg" in
      --webapp)
        (($#)) || { cwv_err "--webapp requires a directory" >&2; exit 2; }
        webapp_override="$1"; shift ;;
      --webapp=*) webapp_override="${arg#--webapp=}" ;;
      *) forwarded+=("$arg") ;;
    esac
  done
  if [[ -n "$webapp_override" ]]; then
    [[ -d "$webapp_override" ]] || { cwv_err "webapp directory does not exist: $webapp_override" >&2; exit 2; }
    webapp_override="$(cd "$webapp_override" && pwd)"
    export CWV_WEBAPP_OVERRIDE="$webapp_override"
  fi
  set -- "${forwarded[@]}"
  if [[ "$target" == "--help" || "$target" == "-h" ]]; then usage; exit 0; fi
  # handy for scripts/gates: print which script a target resolves to
  if [[ "$target" == "--resolve" ]]; then
    local r
    r="$(resolve_target "${1:-}")" || { cwv_err "unknown target: '${1:-}'" >&2; exit 1; }
    script_for "$r"
    exit 0
  fi
  if [[ "$target" == "init" ]]; then init_app "$@"; exit 0; fi

  if [[ -z "$target" ]]; then
    if [[ -t 0 ]]; then
      echo "Which platform?"
      local i=1 t
      for t in "${TARGETS[@]}"; do printf '  %s%d) %s%s\n' "$C_CYN" "$i" "$t" "$C_OFF"; i=$((i+1)); done
      printf '  %s%d) all%s\n' "$C_CYN" "$i" "$C_OFF"
      read -r -p "Target [number or name]: " target || exit 1
      [[ -z "$target" ]] && exit 1
      # the prompt accepts extra args on the same line: "vita -y" means
      # target vita with -y passed through (user report (83): the whole
      # line used to be treated as one unknown target string)
      local -a prompt_extra=()
      read -r -a _prompt_words <<< "$target"
      target="${_prompt_words[0]}"
      prompt_extra=("${_prompt_words[@]:1}")
      ((${#prompt_extra[@]})) && set -- "${prompt_extra[@]}" "$@"
    else usage >&2; exit 1; fi
  fi

  if [[ "${target,,}" == "all" ]]; then
    local results=() t rc=0
    for t in "${TARGETS[@]}"; do
      echo; cwv_head "=== $t ==="
      if bash "$(script_for "$t")" "$@"; then results+=("$t: ${C_GRN}ok${C_OFF}")
      else results+=("$t: ${C_RED}FAILED${C_OFF}"); rc=1; fi
    done
    echo; cwv_head "Summary"
    printf '  %b\n' "${results[@]}"
    exit "$rc"
  fi

  local resolved
  resolved="$(resolve_target "$target")" || { cwv_err "unknown target: '$target'"; usage >&2; exit 1; }
  exec bash "$(script_for "$resolved")" "$@"
}

main "$@"
