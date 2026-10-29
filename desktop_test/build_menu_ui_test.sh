#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -I../sources/shared/sources -o menu_ui_test \
  menu_ui_test.c ../sources/shared/sources/menu/cwv_menu_ui.c \
  ../sources/shared/sources/menu/cwmenu_api.c ../sources/shared/sources/menu/native_menu_templates.c \
  ../sources/shared/sources/core/cwv_mime.c -lm
./menu_ui_test
