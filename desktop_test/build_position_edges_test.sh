#!/usr/bin/env bash
# Regression test for absolute/fixed CSS left/right/top/bottom offsets.
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -std=gnu11 -O1 -Wall -I desktop_test/stubs -I sources/shared/sources \
  -DCWV_APP_DATA_DIR='"CWV00001"' \
  desktop_test/harness_layout.c desktop_test/stub_impl.c desktop_test/cwv_platform_stub.c \
  sources/shared/sources/core/cwv_backend.c \
  sources/shared/sources/core/cwv_broken_image.c \
  sources/shared/sources/web/css/cwv_css.c \
  sources/shared/sources/web/html/cwv_html.c \
  sources/shared/sources/web/js/runtime/cwv_js.c \
  sources/shared/sources/web/js/runtime/duktape/duktape.c \
  -o /tmp/cwv_harness_position_edges -lm
(cd desktop_test && /tmp/cwv_harness_position_edges > /tmp/cwv_position_edges.log 2>&1)
grep -q 'id=\[right-only\]' /tmp/cwv_position_edges.log
grep -q 'x=840.00 y=10.00 w=100.00 h=40.00.*id=\[right-only\]' /tmp/cwv_position_edges.log
grep -q 'x=27.00 y=489.00 w=80.00 h=30.00.*id=\[bottom-only\]' /tmp/cwv_position_edges.log
grep -q 'x=870.00 y=483.00 w=60.00 h=20.00.*id=\[right-bottom\]' /tmp/cwv_position_edges.log
echo 'POSITION EDGE TEST PASSED (right, bottom, fixed right+bottom)'
