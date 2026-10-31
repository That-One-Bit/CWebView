#!/usr/bin/env bash
# Wii U app-loop gate: syntax-checkes sources/wiiu/main.c against the
# desktop SDL2 stub so the port cannot silently rot between real
# devkitPPC build runs (sources/build-wiiu.sh does the real compile).
set -euo pipefail
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -fsyntax-only \
  -I../sources/shared/sources -I../sources/wiiu -I./stubs \
  ../sources/wiiu/main.c
gcc -std=c11 -Wall -Wextra -fsyntax-only \
  -I../sources/shared/sources -I./stubs \
  ../sources/wiiu/cwv_platform.c
g++ -std=c++11 -Wall -Wextra -fsyntax-only -fno-exceptions -fno-rtti \
  -I../sources/shared/sources -I../sources/wiiu -I./stubs \
  ../sources/wiiu/cwv_ime_swkbd.cpp
g++ -std=c++11 -Wall -Wextra -fsyntax-only -fno-exceptions -fno-rtti \
  -I../sources/shared/sources -I./stubs \
  ../sources/wiiu/cwv_fetch_nets2.cpp
echo "WIIU GATE PASSED (main.c + platform.c + both C++ units against wut-1.9.1-mirroring stubs, zero SDK leakage into shared)"
