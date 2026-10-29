/* CWebView per-edition ECMAScript library preludes (2026-10-01).
 *
 * Sources are SEPARATED BY EDITION YEAR (ES####): one file per spec
 * edition, named cwv_es<year>_lib.c, exposing cwv_es<year>_lib_prelude().
 * The syntax transformer itself stays in cwv_es2015_core.c (ES6 = ES2015;
 * later-edition SYNTAX passes live there for now - see its header comment).
 *
 * cwv_es_editions.c concatenates every edition's prelude in spec order
 * (base first: the ES2015 Promise/jobs runtime must exist before later
 * editions extend it) into one string the host evaluates at JS context
 * creation.
 */
#ifndef CWV_ES_LIBS_H
#define CWV_ES_LIBS_H

#include <stddef.h>

const char *cwv_es2015_lib_prelude(void);
const char *cwv_es2016_lib_prelude(void);
const char *cwv_es2017_lib_prelude(void);
const char *cwv_es2018_lib_prelude(void);
const char *cwv_es2019_lib_prelude(void);
const char *cwv_es2020_lib_prelude(void);
const char *cwv_es2021_lib_prelude(void);
const char *cwv_es2022_lib_prelude(void);
const char *cwv_es2023_lib_prelude(void);
const char *cwv_es2024_lib_prelude(void);

/* Combined prelude: every edition in spec order (2015 .. 2024).
 * The result is built once and cached for the process lifetime;
 * returns NULL only on allocation failure. */
const char *cwv_es_editions_prelude(void);

#endif /* CWV_ES_LIBS_H */
