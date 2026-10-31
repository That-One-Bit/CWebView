#!/usr/bin/env bash
# Shared-source wiring gate (added 2026-09-23 (36), after the Vita link
# broke on hardware: sources/shared/sources/core/cwv_cursor.c was added in (33) but
# the Vita CMake target never got it - every desktop gate passed and
# the real vitasdk link failed with undefined references).
#
# Rules:
#  - every sources/shared/*.c must be compiled into the VITA target
#    (CMakeLists.txt) unless explicitly whitelisted below,
#  - every shared path CMakeLists names must actually exist,
#  - the whitelist must name real files, so it cannot silently rot.
# (The Wii U build globs sources/shared/*.c and cannot rot this way;
#  the Vita build lists files explicitly and CAN.)
set -euo pipefail
cd "$(dirname "$0")/.."

# shared sources intentionally NOT part of the Vita app
WHITELIST_VITA="sources/shared/sources/menu/cwv_menu_ui.c"   # CwvMenuUi is the Wii U menu renderer

fail=0
while IFS= read -r -d '' f; do
  case " $WHITELIST_VITA " in
    *" $f "*) continue ;;
  esac
  if ! grep -q "$f" CMakeLists.txt; then
    echo "SHARED-SOURCE GATE: $f is missing from CMakeLists.txt (Vita target)!" >&2
    fail=1
  fi
done < <(find sources/shared/sources -type f -name '*.c' -print0 | sort -z)
# reverse direction: no dangling CMake source paths
while read -r f; do
  [[ -f "$f" ]] || { echo "SHARED-SOURCE GATE: CMakeLists names $f which does not exist!" >&2; fail=1; }
done < <(grep -oE 'sources/shared/[a-z0-9_/.-]+\.c' CMakeLists.txt | sort -u)
# and the whitelist must stay current
for f in $WHITELIST_VITA; do
  [[ -f "$f" ]] || {
    echo "SHARED-SOURCE GATE: whitelisted $f does not exist!" >&2; fail=1; }
done
[[ $fail -eq 0 ]] || exit 1
echo "SHARED-SOURCE GATE PASSED (every shared .c wired into the Vita CMake target; whitelist current)"
