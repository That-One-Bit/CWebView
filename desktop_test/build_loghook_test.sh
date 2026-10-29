#!/bin/sh
# Runtime-log mirror test: image failures on a SCRIPT-FREE page (no JS
# context, so the console shows nothing) must still reach VWWV.log via the
# registered runtime_log hook.  This is the exact blind spot that hid
# every image diagnostic from the device: console lines need a JS context.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    -o loghook_test loghook_test.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/*.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    -lm 2>/dev/null
out=$(./loghook_test)
echo "$out" | grep -q "IMG: image app0:/definitely_missing.png: file read failed \[page dir: app0:/\]" \
  || { echo "LOGHOOK TEST FAILED"; echo "$out"; exit 1; }
echo "$out" | grep -q "(no js context)" \
  || { echo "LOGHOOK TEST FAILED (expected no js context)"; exit 1; }
echo "LOGHOOK TEST PASSED (script-free page failures reach the log)"
