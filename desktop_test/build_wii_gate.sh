#!/usr/bin/env bash
# Wii platform gate: syntax-checks the whole Wii port surface against the
# libogc-mirroring desktop stubs so the port cannot rot between real
# devkitPPC build runs (sources/build-wii.sh does the real compile).
#   - sources/wii/*.c  (platform seam + app loop with the CWMenu shell)
#   - the shared shell units the port links (menu UI, CWMenu API, menu
#     templates, cursor) against the SAME stubs, proving no SDK leakage
set -euo pipefail
cd "$(dirname "$0")"

WII_INC="-I../sources/shared/sources -I./stubs/wii -I./stubs/wii/ogc"

gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/wii/cwv_platform.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/wii/main.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/wii/cwv_ime_wii.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/wii/cwv_fetch_net.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/shared/sources/menu/cwv_menu_ui.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/shared/sources/menu/cwmenu_api.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/shared/sources/menu/native_menu_templates.c
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $WII_INC \
  ../sources/shared/sources/core/cwv_cursor.c
echo "WII GATE PASSED (platform.c + main.c + ime + fetch + shared shell units against libogc-mirroring stubs)"
