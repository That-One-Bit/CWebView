#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/cwv_args_unit_$$"
trap 'rm -f "$out"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -I ../sources/shared/sources cwv_args_unit.c -o "$out"
"$out"
