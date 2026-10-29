#ifndef CWV_TS_CORE_H
#define CWV_TS_CORE_H

/* cwv_ts_core.h - TypeScript type stripper (first cut, 2026-10-01).
 *
 * Converts VALID TypeScript source into the JavaScript the engine
 * already runs: type annotations are removed (not checked - this is a
 * stripper, not a type checker; the page author's types are trusted).
 * The output then goes through the normal ES6 transformer, so TS apps
 * can use classes/arrows/async AND type annotations together.
 *
 *   <script type="text/typescript">
 *     interface P { x: number }          // dropped
 *     enum Color { Red, Green = "g" }    // becomes a JS object
 *     class A<T> implements I {          // implements dropped
 *       private v!: string;              // annotation dropped
 *       constructor(private v: string) {} // param property -> this.v
 *     }
 *     const n = a as number;             // cast dropped
 *     const m = obj!.map;                // non-null dropped
 *   </script>
 *
 * Return codes of vw_ts_strip():
 *   1  transformed (dst holds JavaScript)
 *   0  no TypeScript syntax detected (dst[0] = 0 - run the original)
 *  -1  output buffer overflow (run the original, warn)
 *  -2  TypeScript construct this version cannot convert yet
 *      (namespace/module, decorators, .tsx assertions): run the original
 *      and surface a hint, so the failure is honest instead of silent.
 *
 * Guarantees for the supported subset: strings, template literals,
 * regular expression literals and comments are copied verbatim; ternary
 * `?:`, object literals `{a: 1}` and comparisons `a < b > (c)` are never
 * mistaken for type syntax. Unsupported-but-unambiguous constructs are
 * reported with -2 rather than emitted wrongly. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int vw_ts_strip(const char *src, char *dst, size_t cap);

/* True when the stripper found syntax it refuses to convert (used by
 * callers to compose the console hint without string matching). */
const char *vw_ts_unsupported_reason(void);

#ifdef __cplusplus
}
#endif
#endif
