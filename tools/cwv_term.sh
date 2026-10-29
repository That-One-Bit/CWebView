# tools/cwv_term.sh - shared terminal UX for the four build-*.sh scripts
# (2026-10-07 (80)).  SOURCE it, don't run it:
#     source "$ROOT/tools/cwv_term.sh"
#
# Colors follow https://no-color.org and the usual rules: on only when
# stdout is a terminal, off when NO_COLOR is set, forced on with
# CWV_COLOR=always (CI logs with ANSI support), forced off with
# CWV_COLOR=never.  Everything degrades to the exact plain text the
# scripts printed before, so piping into a file stays clean.
#
# Palette (one meaning per color, used by ALL four scripts):
#   green   compiling / success / "on"
#   blue    link / convert / generate / package steps
#   cyan    section headers and keys in the metadata preview
#   yellow  warnings and non-default / edited values
#   red     errors and "off"
#   dim     informational / "<default>" placeholders

cwv_color_init() {
  CWV_USE_COLOR=0
  case "${CWV_COLOR:-auto}" in
    always) CWV_USE_COLOR=1 ;;
    never)  CWV_USE_COLOR=0 ;;
    *) if [[ -z "${NO_COLOR:-}" && -t 1 && "${TERM:-dumb}" != "dumb" ]]; then CWV_USE_COLOR=1; fi ;;
  esac
  if [[ $CWV_USE_COLOR -eq 1 ]]; then
    C_RED=$'\033[31;1m'; C_GRN=$'\033[32;1m'; C_YEL=$'\033[33;1m'
    C_BLU=$'\033[34;1m'; C_CYN=$'\033[36;1m'; C_DIM=$'\033[2m'
    C_BOLD=$'\033[1m';   C_OFF=$'\033[0m'
  else
    C_RED=""; C_GRN=""; C_YEL=""; C_BLU=""; C_CYN=""; C_DIM=""; C_BOLD=""; C_OFF=""
  fi
}
cwv_color_init

cwv_err()  { printf '%s%s%s\n' "$C_RED" "$*" "$C_OFF" >&2; }
cwv_warn() { printf '%s%s%s\n' "$C_YEL" "$*" "$C_OFF" >&2; }
cwv_ok()   { printf '%s%s%s\n' "$C_GRN" "$*" "$C_OFF"; }
cwv_step() { printf '%s%s%s\n' "$C_BLU" "$*" "$C_OFF"; }
cwv_head() { printf '%s%s%s\n' "$C_CYN" "$*" "$C_OFF"; }

# cwv_field LABEL VALUE [WIDTH]  - "  Label:   value" with a cyan label and
# a dim value when it is a "<placeholder>" (an unset / default value).
cwv_field() {
  local label="$1" value="$2" width="${3:-12}" pad
  printf -v pad '%-*s' "$width" "$label:"
  if [[ "$value" == "<"*">" ]]; then
    printf '  %s%s%s %s%s%s\n' "$C_CYN" "$pad" "$C_OFF" "$C_DIM" "$value" "$C_OFF"
  else
    printf '  %s%s%s %s\n' "$C_CYN" "$pad" "$C_OFF" "$value"
  fi
}

# cwv_features_tree CONSOLE "key:on key:off ..."
# Prints the feature switches as a subcategory of the console:
#
#   <console>
#     features
#       cursor: on
#       zoom:   off
#
# (the manifest's root > console > features > key shape, as a tree).
cwv_features_tree() {
  local console="$1" list="$2" item key state color
  printf '  %s%s%s\n' "$C_CYN" "$console" "$C_OFF"
  printf '    %sfeatures%s\n' "$C_CYN" "$C_OFF"
  for item in $list; do
    key="${item%%:*}"; state="${item##*:}"
    if [[ "$state" == "off" ]]; then color="$C_RED"; else color="$C_GRN"; fi
    printf '      %-9s %s%s%s\n' "$key:" "$color" "$state" "$C_OFF"
  done
}

# cwv_colorize - stdin filter for build output.  Rules, first match wins:
#   error / fatal error / undefined reference / *** Error  -> red
#   warning:                                               -> yellow
#   "[ NN%] Building|building|compiling ..."               -> green
#   "[ NN%] Linking|Converting|Generating|Creating|Staging|
#           Packing|linking|Packing|Writing|Staging" etc.  -> blue
#   "[ NN%] Built target"                                  -> default (quiet)
# Lines pass through UNCHANGED when color is off, so it is always safe to
# pipe through it.  Uses awk (POSIX), no GNU extensions.
cwv_colorize() {
  if [[ $CWV_USE_COLOR -ne 1 ]]; then cat; return; fi
  awk -v R="$C_RED" -v G="$C_GRN" -v Y="$C_YEL" -v B="$C_BLU" -v O="$C_OFF" '
    {
      line = $0
      if (line ~ /(^|[ :])(fatal )?error:|undefined reference|\*\*\* |^make.*Error|Error [0-9]+$|cannot find -l|FAILED|failed/) { print R line O; next }
      if (line ~ /warning:|WARNING|NOTE:/)                                                                  { print Y line O; next }
      if (line ~ /^\[ *[0-9]+%\] *(Linking|linking|Converting|Generating|Creating|Staging|Packing|Building stubs|Writing)/) { print B line O; next }
      if (line ~ /^\[ *[0-9]+%\] *(Building|building|compiling|Compiling)/)                                 { print G line O; next }
      if (line ~ /^\[ *[0-9]+%\] *(built|Built target)/)                                                    { print line; next }
      if (line ~ /^\[[a-z]+\]/)                                                                              { print B line O; next }
      print line
      fflush()
    }'
}
