#!/bin/sh
# Regression gate for the CURRENT renderer behavior.
#
# build_and_compare.sh proves old-vs-new preservation and is the tool for a
# refactor.  When we deliberately FIX a rendering bug (like the div
# background/margin fix) the old backend is, by definition, wrong - so the
# reference to compare against becomes the post-fix trace saved in
# outputs/golden_reference.txt.  Run this after any renderer change that is
# NOT meant to alter layout output; zero diff means no accidental drift.
#
# To re-bless the reference after an INTENTIONAL behavior change:
#   ./check_reference.sh --update
set -e
cd "$(dirname "$0")"
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
    -o harness_ref -lm
./harness_ref > outputs/ref_run.txt 2>&1
if [ "$1" = "--update" ]; then
    cp outputs/ref_run.txt outputs/golden_reference.txt
    echo "reference updated ($(wc -l < outputs/golden_reference.txt) lines)"
    exit 0
fi
if [ ! -f outputs/golden_reference.txt ]; then
    echo "no saved reference yet - run ./check_reference.sh --update first"
    exit 1
fi
if diff outputs/golden_reference.txt outputs/ref_run.txt > outputs/ref_diff.txt; then
    echo "PASS: identical to golden reference ($(wc -l < outputs/ref_run.txt) lines)"
else
    echo "FAIL: drift from golden reference - see outputs/ref_diff.txt"
    head -30 outputs/ref_diff.txt
    exit 1
fi
