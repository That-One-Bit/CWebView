#!/bin/sh
# Page-scoped image-cache gate (2026-09-21 (29)): every decoded image
# texture is owned by the renderer's per-page cache.  This harness proves
# (1) same-URL <img> tags share ONE decode, (2) navigating away frees the
# previous page's textures (the Vita GXM heap exhaustion was page-over-page
# accumulation), and (3) shutdown frees everything: allocs == frees.
# It regresses the moment anyone reintroduces node-owned textures.
set -e
cd "$(dirname "$0")"
gcc -std=gnu11 -O1 -I stubs -I ../sources/shared/sources \
    -DCWV_APP_DATA_DIR='"CWV00001"' \
    -o img_cache_test img_cache_test.c stub_impl.c \
    ../sources/shared/sources/core/cwv_backend.c \
    cwv_platform_stub.c \
    ../sources/shared/sources/core/cwv_broken_image.c \
    ../sources/shared/sources/web/css/cwv_css.c \
    ../sources/shared/sources/web/html/cwv_html.c \
    ../sources/shared/sources/web/js/runtime/cwv_js.c \
    ../sources/shared/sources/web/js/runtime/duktape/duktape.c \
    -lm
./img_cache_test
echo "IMG CACHE TEST PASSED (shared decodes, freed at navigation, balanced at shutdown)"
