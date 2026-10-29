#!/usr/bin/env bash
# MAIN.C SYNTAX GATE
# Compiles the app-side sources (main.c + cwv_host.c +
# cwmenu_api.c) against the desktop stub headers, WITHOUT linking:
# this catches syntax errors / bad prototypes in the files that only
# run on the Vita, which the desktop suites never execute.
#
# Both build flavors are checked:
#   production (-DCWV_DEBUG=0): the debug overlay is compiled OUT;
#   debug      (-DCWV_DEBUG=1): the overlay paths are compiled IN.
# A mismatched #if/#endif pair in one flavor fails here.
set -euo pipefail
cd "$(dirname "$0")"

S=../sources/vita
INC="-I stubs -I $S -I ../sources/shared/sources -I ../sources/vita/scroll_module/src"
SRC="$S/main.c ../sources/shared/sources/core/cwv_host.c $S/cwv_platform.c ../sources/shared/sources/menu/cwmenu_api.c"

for dbg in 0 1; do
  echo "== main gate: CWV_DEBUG=$dbg"
  gcc -std=gnu11 -O1 -Wall -Wextra -fsyntax-only \
      -DCWV_ENGINE_ID=0 -DCWV_DEBUG=$dbg \
      -DCWV_WEBPAGE='"'"'index.html'"'"' -DCWV_LAUNCH_PAGE='"'"'index.html'"'"' \
      -DVITA_TITLEID='"'"'GATE00001'"'"' -DCWV_APP_DATA_DIR='"'"'ux0:data/gate'"'"' \
      $INC $SRC
done
echo "MAIN GATE PASSED (production + debug)"
