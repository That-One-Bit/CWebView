/* cwv_es2015_core.h - shared modern-JS handling pipeline used by the
 * built-in ESM provider (every port) and by the desktop test.
 *
 * Edition naming (2026-10-01): ES6 = ES2015, so this file carries the
 * 2015 name. The SYNTAX passes for later editions (exponentiation,
 * async/await, ?. ??, logical assignment, class fields/private) still
 * live in this one engine file; the LIBRARY prelude for every edition
 * ES2016..ES2024 lives in its own cwv_es<year>_lib.c (see
 * cwv_es_libs.h).
 *
 * This file is deliberately FREE of Vita SDK dependencies so it can be
 * compiled into the host app AND any test binary unchanged.
 *
 * What it does (enough for typical "modern-ish" page scripts):
 *   1. `import` / `export` handling (needs the hooks below):
 *        import "a.js";                     -> run a.js first, strip the line
 *        import x from "a.js";               -> run a.js first, strip the line
 *        import {a, b as c} from "a.js";    -> run a.js first, strip the line
 *        export function f() {...}         -> function f() {...}
 *        export const/let/var x = ...       -> var x = ...
 *        export default <decl-or-expr>      -> <decl-or-expr>
 *        export { a, b };                   -> (dropped; names are globals)
 *      Module model: "files, loaded in dependency order, all in one shared
 *      global scope".  Names exported by a module are plain globals for the
 *      importer (a pragmatic fit for an offline app packager).
 *   2. Lexical ES6 -> ES5 (always on, no hooks needed):
 *        let / const -> var
 *        (a, b) => body / a => body          -> function (a, b) { ... }
 *        `text ${expr} more`                -> ("text " + (expr) + " more")
 *        for (const x of list) body         -> an indexed loop; the list is
 *                                              captured once (like real
 *                                              for..of); works for arrays
 *                                              and strings; for..in is left
 *                                              untouched
 *      String / comment / template / regex literals are skipped correctly
 *      while scanning, so code inside them is never rewritten.
 *   3. Additional handling (see the regression pages for tested forms):
 *      array/object/call spread, destructuring and pattern parameters,
 *      default/rest parameters, async/await through Promise chains;
 *      class constructors/methods, get/set accessors, fields, inheritance,
 *      super calls and basic private-name execution.
 *      Base fields run before the constructor body. Derived fields run
 *      after a direct top-level super() or the implicit parent call.
 *      Private names map to ordinary __name properties: privacy/brand
 *      checks and collision-free private identity are NOT implemented.
 *      This is not complete ECMAScript conformance. Unsupported forms
 *      can remain verbatim and cause an engine syntax error; arbitrary
 *      conditional super, static super and super accessor receiver
 *      semantics are not certified by the class regression suite.
 *
 * vw_es2015_transform() returns 1 and fills dst when it rewrote anything,
 * 0 when the source had nothing to do (the caller should run the original).
 */

#ifndef CWV_ES2015_CORE_H
#define CWV_ES2015_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Host callbacks the transformer needs.  Any pointer may be NULL:
 *   resolve_url / load_file NULL  -> `import` lines are left untouched
 *                                    (and produce an engine syntax error)
 *   evaluate NULL                 -> imported files are not run
 * resolve_url(ud, base, spec, out, cap): make `spec` absolute; the app-side
 *   implementation resolves against the CURRENT PAGE base when base is "".
 * load_file(ud, url, out, cap): read a text file into out, NUL-terminate,
 *   return the length or -1.
 * evaluate(ud, js): run JS source (already ES5) right now.
 */
typedef struct VwEsHooks {
    void *userdata;
    int  (*resolve_url)(void *ud, const char *base, const char *spec,
                        char *out, size_t cap);
    int  (*load_file)(void *ud, const char *url, char *out, size_t cap);
    int  (*evaluate)(void *ud, const char *javascript);
} VwEsHooks;

/* The ES6 LIBRARY prelude: ES5.1 source that installs Array.find,
 * String.padStart, Object.assign, Number.isInteger, Math.sign, Map, Set
 * (and more) directly into the engine.  Hosts evaluate it once per JS
 * context.  This is direct support - no conversion. */
const char *vw_es6_prelude(void);

/* Transform `src` into `dst`.  Returns 1 (dst holds ES5 source to run),
 * 0 (nothing to transform: run src unchanged), -1 (internal error). */
int vw_es2015_transform(const char *src, char *dst, size_t cap,
                     const VwEsHooks *hooks);

#ifdef __cplusplus
}
#endif

#endif
