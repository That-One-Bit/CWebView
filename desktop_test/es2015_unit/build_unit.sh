#!/bin/sh
# Rebuild the ES6 transformer unit test (plain gcc, no Vita SDK).
# Usage: es2015_unit/build_unit.sh && es2015_unit/unit
cd "$(dirname "$0")"
gcc -Wall -Wextra -O1 -I../../sources/shared/sources \
    unit.c ../../sources/shared/sources/web/js/modern/cwv_es2015_core.c \
../../sources/shared/sources/web/js/modern/cwv_es2015_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2016_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2017_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2018_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2019_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2020_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2021_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2022_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2023_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es2024_lib.c \
../../sources/shared/sources/web/js/modern/cwv_es_editions.c \
    -o unit || exit 1
echo "unit test built"
