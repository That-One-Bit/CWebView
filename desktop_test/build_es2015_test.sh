#!/bin/bash
# Build + run the ES6 module integration test (external css/js, import/export,
# arrows, let/const, templates) using the module's transformer core.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    es2015_test.c stub_impl.c \
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
    -o es2015_test -lm
./es2015_test
