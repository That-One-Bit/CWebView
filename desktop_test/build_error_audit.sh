#!/bin/bash
# Error-handling audit gate (2026-10-01): malformed HTML/CSS/JS pages and
# a broken CWMenu script go through the REAL engine twice - once normal,
# once under AddressSanitizer - and must never crash, always report.
set -e
cd "$(dirname "$0")"

CC=${CWV_CC:-gcc}
INC="-I stubs -I ../sources/shared/sources -DCWV_APP_DATA_DIR='\"CWV00001\"'"
SRC="error_audit_test.c stub_impl.c cwv_platform_stub.c ../sources/shared/sources/core/cwv_backend.c ../sources/shared/sources/web/css/cwv_css.c ../sources/shared/sources/web/html/cwv_html.c ../sources/shared/sources/web/js/runtime/cwv_js.c ../sources/shared/sources/core/cwv_broken_image.c ../sources/shared/sources/menu/cwmenu_api.c ../sources/shared/sources/menu/native_menu_templates.c ../sources/shared/sources/web/typescript/cwv_ts_core.c ../sources/shared/sources/web/js/modern/cwv_es2015_core.c \
../sources/shared/sources/web/js/modern/cwv_es2015_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2016_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2017_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2018_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2019_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2020_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2021_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2022_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2023_lib.c \
../sources/shared/sources/web/js/modern/cwv_es2024_lib.c \
../sources/shared/sources/web/js/modern/cwv_es_editions.c ../sources/shared/sources/web/js/runtime/duktape/duktape.c"

echo "[build] error_audit"
$CC -std=gnu11 -g -O1 $INC $SRC -lm -o error_audit
echo "[run] error_audit"
./error_audit

if command -v $CC >/dev/null 2>&1 && $CC --version 2>/dev/null | grep -qi clang; then
    SAN=clang
else
    SAN=gcc
fi
echo "[asan] error_audit ($SAN)"
$CC -std=gnu11 -g -O1 -fsanitize=address,undefined $INC $SRC -lm -o error_audit_asan
ASAN_OPTIONS=detect_leaks=0 ./error_audit_asan >/dev/null && echo "[asan] clean"
echo "ERROR AUDIT GATE PASSED (malformed HTML/CSS/JS/CWMenu, normal + ASan/UBSan)"
