/* Per-edition library prelude gate (2026-10-01).
 *
 * Evaluates the COMBINED prelude (cwv_es_editions_prelude, ES2015..ES2024
 * chunks in spec order) in a fresh Duktape context and asserts one or
 * more observable behaviors per edition. This catches: a chunk that
 * stops parsing, an edition assigned to the wrong file (feature
 * missing because its dependency never loaded), and ordering bugs
 * (e.g. Promise.any evaluated before the base Promise exists).
 */
#include <stdio.h>
#include <string.h>
#include "web/js/runtime/duktape/duktape.h"
#include "web/js/modern/cwv_es_libs.h"

static int check(duk_context *ctx, const char *expr, int expect)
{
    int got;
    if (duk_peval_string(ctx, expr) != 0) {
        printf("FAIL (throw) %s\n  %s\n", expr, duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return 1;
    }
    got = duk_to_int(ctx, -1);
    duk_pop(ctx);
    if (got != expect) {
        printf("FAIL %s => %d (expected %d)\n", expr, got, expect);
        return 1;
    }
    printf("PASS %s\n", expr);
    return 0;
}

/* Sets the comparison up front, drains the job queue, then reads __v. */
static int check_async(duk_context *ctx, const char *setup, int expect)
{
    int fails = 0;
    char expr[512];
    if (duk_peval_string(ctx, setup) != 0) {
        printf("FAIL (throw) %s\n  %s\n", setup, duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return 1;
    }
    duk_pop(ctx);
    snprintf(expr, sizeof(expr), "__vw_drain(); var __v = 0;");
    duk_peval_string(ctx, expr); duk_pop(ctx);
    /* after drain, read the value the setup stored in window.__r */
    duk_eval_string(ctx, "__r");
    if (duk_to_int(ctx, -1) != expect) {
        printf("FAIL async %s => %d (expected %d)\n", setup,
               duk_to_int(ctx, -1), expect);
        fails++;
    } else {
        printf("PASS async %s\n", setup);
    }
    duk_pop(ctx);
    return fails;
}

int main(void)
{
    duk_context *ctx = duk_create_heap_default();
    const char *prelude;
    int fails = 0;

    if (!ctx) { printf("FAIL duk heap\n"); return 1; }
    prelude = cwv_es_editions_prelude();
    if (!prelude) { printf("FAIL prelude assembly\n"); return 1; }
    if (duk_peval_string(ctx, prelude) != 0) {
        printf("FAIL prelude does not parse:\n  %s\n", duk_safe_to_string(ctx, -1));
        return 1;
    }
    duk_pop(ctx);

    /* ES2015 (base) - what later chunks extend */
    fails += check(ctx, "Promise.resolve(41).then(function(v){__r=v+1;});0", 0);
    fails += check_async(ctx, "Promise.resolve(41).then(function(v){__r=v+1;});", 42);
    fails += check(ctx, "[1,2,3,4].find(function(x){return x>2;}) === 3 ? 1:0", 1);
    fails += check(ctx, "typeof __vw_spread('ab')[1] === 'string' ? 1:0", 1);
    fails += check(ctx, "Object.assign({}, {a:1}).a === 1 ? 1:0", 1);
    fails += check(ctx, "new Map().set('k',5).get('k') === 5 ? 1:0", 1);
    fails += check(ctx, "new Set().add(2).has(2) ? 1:0", 1);

    /* ES2016 */
    fails += check(ctx, "[1,2,3].includes(2) ? 1:0", 1);
    fails += check(ctx, "[1,2,3].includes(9) ? 1:0", 0);

    /* ES2017 */
    fails += check(ctx, "('7').padStart(3,'0') === '007' ? 1:0", 1);
    fails += check(ctx, "('7').padEnd(3,'-') === '7--' ? 1:0", 1);
    fails += check(ctx, "Object.entries({a:1})[0][0] === 'a' ? 1:0", 1);
    fails += check(ctx, "Object.values({a:3,b:4}).length === 2 ? 1:0", 1);

    /* ES2018 */
    fails += check_async(ctx, "__r=0; Promise.resolve(1).finally(function(){__r=7;});", 7);
    fails += check(ctx, "__vw_oread({a:1,b:2},['a']).b === 2 ? 1:0", 1);
    fails += check(ctx, "__vw_aread([1,2,3],1)[1] === 3 ? 1:0", 1);

    /* ES2019 */
    fails += check(ctx, "[1,[2,[3]]].flat(2).join() === '1,2,3' ? 1:0", 1);
    fails += check(ctx, "[1,2].flatMap(function(x){return [x,x];}).length === 4 ? 1:0", 1);
    fails += check(ctx, "Object.fromEntries([['a',1]]).a === 1 ? 1:0", 1);
    fails += check(ctx, "('  x').trimStart() === 'x' ? 1:0", 1);
    fails += check(ctx, "('x  ').trimEnd() === 'x' ? 1:0", 1);

    /* ES2020 */
    fails += check(ctx, "typeof globalThis === 'object' ? 1:0", 1);
    fails += check_async(ctx, "__r=0; Promise.allSettled([1, Promise.reject(2)]).then(function(r){"
                   "__r = (r.length === 2 && r[0].status === 'fulfilled' && "
                   "r[1].status === 'rejected') ? 9 : 0;});", 9);

    /* ES2021 */
    fails += check(ctx, "('aXbXc').replaceAll('X','-') === 'a-b-c' ? 1:0", 1);
    fails += check_async(ctx, "__r=0; Promise.any([Promise.reject(1), Promise.resolve(5)])."
                   "then(function(v){__r=v;});", 5);

    /* ES2022 */
    fails += check(ctx, "[1,2,3].at(-1) === 3 ? 1:0", 1);
    fails += check(ctx, "('abc').at(1) === 'b' ? 1:0", 1);
    fails += check(ctx, "Object.hasOwn({a:1},'a') ? 1:0", 1);

    /* ES2023 */
    fails += check(ctx, "(function(){var a=[3,1,2]; return a.toSorted()[0] === 1 && a[0] === 3;})() ? 1:0", 1);
    fails += check(ctx, "[1,2,3].toReversed()[0] === 3 ? 1:0", 1);
    fails += check(ctx, "[1,2,3,4].toSpliced(1,2,9).join() === '1,9,4' ? 1:0", 1);
    fails += check(ctx, "[1,2,3].with(1,9).join() === '1,9,3' ? 1:0", 1);
    fails += check(ctx, "[1,2,3].findLast(function(x){return x<3;}) === 2 ? 1:0", 1);
    fails += check(ctx, "[1,2,3].findLastIndex(function(x){return x<3;}) === 1 ? 1:0", 1);

    /* ES2024 */
    fails += check(ctx, "Object.groupBy([1,2,3,4], function(x){return x%2;})[0].length === 2 ? 1:0", 1);
    fails += check(ctx, "Map.groupBy([1,2], function(x){return x%2;}).size === 2 ? 1:0", 1);
    fails += check_async(ctx, "__r=0; var w = Promise.withResolvers(); "
                   "w.resolve(6); w.promise.then(function(v){__r=v;});", 6);

    duk_destroy_heap(ctx);
    if (fails) { printf("ES EDITIONS GATE: %d FAILURES\n", fails); return 1; }
    printf("ES EDITIONS GATE PASSED (ES2015..ES2024 prelude features)\n");
    return 0;
}
