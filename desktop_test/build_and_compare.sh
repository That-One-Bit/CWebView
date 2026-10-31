#!/bin/bash
# Golden regression test for the Vita WebView renderer.
#
# Usage:
#   ./build_and_compare.sh <old_cwv_backend.c>
#
# Compiles the ORIGINAL single-file backend (old mode) and the CURRENT split
# renderer (new mode: css/html/js/backend + Duktape), runs both against the
# golden pages, and diffs the full behavioral trace (every parsed node, every
# draw call, taps, focus cycling, select popups, scroll, VCM, IME).
# PASS = the split renderer is a drop-in replacement for the original.
#
# The JS feature test is separate:  ./build_js_test.sh && ./js_test

set -e
cd "$(dirname "$0")"

OLD="${1:?usage: build_and_compare.sh original_cwv_backend.c}"

echo "== building old (single file): $OLD"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    -DBACKEND_SRC="\"$OLD\"" \
    harness.c stub_impl.c -o harness_old -lm
./harness_old > outputs/old.txt 2>&1

echo "== building new (split renderer + JS)"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    harness.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
    -o harness_new -lm
./harness_new > outputs/new.txt 2>&1

if diff outputs/old.txt outputs/new.txt > outputs/diff.txt; then
    echo "PASS: outputs identical ($(wc -l < outputs/old.txt) lines of behavior trace)"
else
    echo "FAIL: behavior differs - see outputs/diff.txt ($(wc -l < outputs/diff.txt) diff lines)"
    head -30 outputs/diff.txt
    exit 1
fi
