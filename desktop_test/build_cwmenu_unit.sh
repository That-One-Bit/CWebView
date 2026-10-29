#!/bin/bash
# Build + run the CWMenu 2 parser unit test (desktop, no Vita SDK needed).
# Usage: ./build_cwmenu_unit.sh          (build and run)
set -e
cd "$(dirname "$0")"

echo "[build] cwmenu_unit"
gcc -std=c11 -Wall -Wextra -O2 -I../sources/vita -I../sources/shared/sources \
    cwmenu_unit.c ../sources/shared/sources/menu/cwmenu_api.c ../sources/shared/sources/menu/native_menu_templates.c \
    -o cwmenu_unit

echo "[run] cwmenu_unit"
./cwmenu_unit
