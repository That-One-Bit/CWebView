#!/bin/sh
# BMP decoder unit gate (2026-10-01): runs the real shared decoder on
# desktop for every platform that links it (currently Wii; the same file
# is compiled into the Wii gate and is available to any port whose native
# image library lacks BMP).
set -e
cd "$(dirname "$0")"
gcc -std=c99 -Wall -Wextra -Werror -I ../sources/shared/sources \
    -o bmp_unit bmp_unit.c ../sources/shared/sources/core/cwv_bmp.c
./bmp_unit
echo "BMP UNIT GATE PASSED (24/32bpp + orientation + rejections)"
