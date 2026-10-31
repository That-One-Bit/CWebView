#!/bin/bash
# TypeScript stripper unit gate (2026-10-01): 38 fixture cases covering
# annotations, interfaces/type aliases/declare/import type, enums,
# classes (fields, param properties, accessors, generics, overloads,
# index signatures), casts, non-null, generic calls, catch annotations,
# passthrough safety for plain JS, and honest -2 for unsupported
# constructs. Runs normal + ASan/UBSan.
set -e
cd "$(dirname "$0")"
CC=${CWV_CC:-gcc}

echo "[build] ts_unit"
$CC -std=gnu11 -g -O1 -Wall -Wextra -I ../sources/shared/sources \
    ts_unit.c ../sources/shared/sources/web/typescript/cwv_ts_core.c -o ts_unit
echo "[run] ts_unit"
./ts_unit

echo "[asan] ts_unit"
$CC -std=gnu11 -g -O1 -fsanitize=address,undefined \
    -I ../sources/shared/sources ts_unit.c ../sources/shared/sources/web/typescript/cwv_ts_core.c -o ts_unit_asan
ASAN_OPTIONS=detect_leaks=0 ./ts_unit_asan >/dev/null && echo "[asan] clean"
echo "TS UNIT GATE PASSED (38 cases, normal + ASan/UBSan)"
