#!/usr/bin/env bash
# SHARED GATE: every engine file in sources/shared must compile with
# NOTHING but sources/shared in the include path.  "Shared" means the
# same code builds on every console, so a port header or a console SDK
# reference is a build break waiting for the next port.
#
# This gate exists because Vita-only binding code once lived inside
# shared/cwv_host.c: the Vita build quietly supplied its own include
# path and passed, and only sources/build-wiiu.sh (which compiles shared/*.c
# standalone) noticed.  Run it the same way as the other gates:
#   desktop_test/build_shared_gate.sh
set -euo pipefail
cd "$(dirname "$0")"

INC="-I ../sources/shared/sources"
while IFS= read -r -d '' src; do
  echo "  CC ${src#../sources/shared/sources/}"
  gcc -std=gnu11 -Wall -Wextra -fsyntax-only $INC "$src"
done < <(find ../sources/shared/sources -type f -name '*.c' ! -path '*/web/js/runtime/duktape/duktape.c' -print0 | sort -z)
# Vendored Duktape is checked separately because it is third-party source,
# but it must still compile through the same shared include boundary.
gcc -std=gnu11 -Wall -Wextra -fsyntax-only $INC ../sources/shared/sources/web/js/runtime/duktape/duktape.c
echo "SHARED GATE PASSED (sources/shared compiles standalone - no port or console headers needed)"
