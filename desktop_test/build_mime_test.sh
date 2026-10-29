#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -I ../sources/shared/sources \
  ../sources/shared/sources/core/cwv_mime.c mime_test.c -o /tmp/cwv_mime_test
/tmp/cwv_mime_test
rm -f /tmp/cwv_mime_test
echo "MIME registry test passed"
