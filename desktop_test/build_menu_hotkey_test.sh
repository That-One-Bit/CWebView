#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -I../sources/wiiu menu_hotkey_test.c -o menu_hotkey_test
./menu_hotkey_test
