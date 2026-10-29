#!/bin/sh
# Cursor gate (2026-09-23): the shared pointer cursor module must behave
# identically on every console - art contract fallback, clamped placement,
# rounded tap positions, and paint-only-when-visible.
set -e
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    -o cursor_test cursor_test.c stub_impl.c \
    ../sources/shared/sources/core/cwv_cursor.c \
    cwv_platform_stub.c \
    -lm
./cursor_test
echo "CURSOR TEST PASSED (fallback arrow, clamping, tap position)"
