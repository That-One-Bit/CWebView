/* CWebView combined per-edition prelude assembler (2026-10-01).
 *
 * See cwv_es_libs.h. The base ES2015 chunk is FIRST because the Promise
 * implementation + job queue live there and later editions (finally,
 * allSettled, any, withResolvers) extend them.
 */
#include <stdlib.h>
#include <string.h>
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es_editions_prelude(void)
{
    static char *cache;
    if (cache) return cache;

    static const char *(*const parts[])(void) = {
        cwv_es2015_lib_prelude,
        cwv_es2016_lib_prelude,
        cwv_es2017_lib_prelude,
        cwv_es2018_lib_prelude,
        cwv_es2019_lib_prelude,
        cwv_es2020_lib_prelude,
        cwv_es2021_lib_prelude,
        cwv_es2022_lib_prelude,
        cwv_es2023_lib_prelude,
        cwv_es2024_lib_prelude,
    };
    size_t total = 0;
    size_t i;
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
        total += strlen(parts[i]());
    cache = (char *)malloc(total + 1);
    if (!cache) return NULL;
    cache[0] = 0;
    for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
        strcat(cache, parts[i]());
    return cache;
}
