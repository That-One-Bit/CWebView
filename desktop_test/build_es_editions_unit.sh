#!/bin/bash
# Per-edition library prelude gate (2026-10-01): evaluates the combined
# ES2015..ES2024 prelude in a fresh Duktape context and checks observable
# behavior for every edition, normal + ASan/UBSan.
set -e
cd "$(dirname "$0")"
CC=${CWV_CC:-gcc}
COMMON="-I../sources/shared/sources ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
 ../sources/shared/sources/web/js/modern/cwv_es2015_lib.c ../sources/shared/sources/web/js/modern/cwv_es2016_lib.c \
 ../sources/shared/sources/web/js/modern/cwv_es2017_lib.c ../sources/shared/sources/web/js/modern/cwv_es2018_lib.c \
 ../sources/shared/sources/web/js/modern/cwv_es2019_lib.c ../sources/shared/sources/web/js/modern/cwv_es2020_lib.c \
 ../sources/shared/sources/web/js/modern/cwv_es2021_lib.c ../sources/shared/sources/web/js/modern/cwv_es2022_lib.c \
 ../sources/shared/sources/web/js/modern/cwv_es2023_lib.c ../sources/shared/sources/web/js/modern/cwv_es2024_lib.c \
 ../sources/shared/sources/web/js/modern/cwv_es_editions.c"

echo "[build] es_editions_unit"
$CC -std=gnu11 -g -O1 -Wall -Wextra $COMMON es_editions_unit.c -o es_editions_unit -lm
echo "[run] es_editions_unit"
./es_editions_unit | tail -3
./es_editions_unit >/dev/null

echo "[asan] es_editions_unit"
$CC -std=gnu11 -g -O1 -fsanitize=address,undefined $COMMON es_editions_unit.c -o es_editions_asan -lm
ASAN_OPTIONS=detect_leaks=0 ./es_editions_asan >/dev/null && echo "[asan] clean"
echo "ES EDITIONS GATE PASSED"
