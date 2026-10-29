#!/bin/sh
# Theme-flip color gate (2026-09-21 (29)): a browser never lets the app
# theme overrule the page's cascade.  Nodes that inherited the page's
# explicit body color (es6_demo sets #f8fafc) must keep it through dark
# and light passes; the JS-styled #out must keep its explicit green.
# Regresses if a theme pass ever re-tints inherited/explicit colors.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    -o theme_test flash_repro.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
    ../sources/shared/sources/web/js/modern/cwv_es2015_core.c \
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
../sources/shared/sources/web/js/modern/cwv_es_editions.c \
    -lm
./theme_test >/dev/null
echo "THEME TEST PASSED (inherited page colors survive theme flips)"
