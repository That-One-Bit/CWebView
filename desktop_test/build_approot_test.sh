#!/bin/sh
# Unit gate for the per-console app content root (cwv_platform.h).
# Links the real backend against the desktop stub platform - the same
# set check_reference.sh uses, plus the stub file-IO shim - and drives
# load/href behavior through both the default (Vita) root and a
# Wii-style package-folder root.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -Wall -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    approot_test.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
    -o approot_test -lm
./approot_test
