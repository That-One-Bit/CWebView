#!/usr/bin/env bash
# Vita KBM gate (2026-10-05): syntax-checks the Bluetooth HID module
# (cwv_kbm_vita.c) against psp2 stubs mirroring the REAL VitaSDK
# headers (verified 1:1 against vitasdk/vita-headers upstream), so the
# SceHid integration cannot rot between real sources/build-vita.sh runs.
set -euo pipefail
cd "$(dirname "$0")"

VITA_INC="-I../sources/vita -I./stubs"

gcc -std=gnu11 -Wall -Wextra -fsyntax-only $VITA_INC \
  ../sources/vita/cwv_kbm_vita.c

# (80) The REAL Vita platform layer, with implicit declarations as ERRORS:
# a mistyped vita2d symbol (vita2d_set_clip_rect vs the real
# vita2d_set_clip_rectangle) is only a warning by default under gcc and
# a hard error under the Vita toolchain, so it slipped past the gates.
gcc -std=gnu11 -Wall -Wextra -Werror=implicit-function-declaration \
  -fsyntax-only -DCWV_APP_DATA_DIR='"GATE"' -I../sources/shared/sources -I../sources/vita -I./stubs \
  ../sources/vita/cwv_platform.c

echo "VITA KBM GATE PASSED (Bluetooth HID keyboard + mouse against VitaSDK-mirroring stubs)"
