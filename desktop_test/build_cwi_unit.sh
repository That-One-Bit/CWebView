#!/bin/sh
# CWI unit gate (2026-10-05 (71)): python-encoder fixture vs the real C
# decoder - pixel-exact, plus full truncation-fuzz rejection.
set -e
cd "$(dirname "$0")"
python3 cwi_gen_fixture.py /tmp
gcc -std=c99 -Wall -Wextra -Werror -I ../sources/shared/sources -I /tmp \
    -o cwi_unit cwi_unit.c ../sources/shared/sources/core/cwv_cwi.c
./cwi_unit
echo "CWI UNIT GATE PASSED (lossless decode + fuzz rejection)"
