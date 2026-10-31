#!/bin/bash
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    canvas2d_test.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
    -o canvas2d_test -lm
./canvas2d_test canvas2d_fixture
./canvas2d_test canvas2d_fixture_webmanifest
