#!/usr/bin/env bash
# Keep the shared transformer portable to VitaSDK's strict C11 headers.
# Do not enable POSIX/GNU feature macros merely to expose strdup.
set -euo pipefail
cd "$(dirname "$0")"
for standard in c99 c11; do
    echo "== ES6 portability gate: $standard"
    "${CC:-gcc}" -std="$standard" -Wall -Wextra -Werror -fsyntax-only \
        -I ../sources/shared/sources ../sources/shared/sources/web/js/modern/cwv_es2015_core.c \
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
../sources/shared/sources/web/js/modern/cwv_es_editions.c
done
echo "ES6 PORTABILITY GATE PASSED (C99 + C11, warnings are errors)"
