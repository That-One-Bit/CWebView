/* ts_unit.c - desktop unit test for the TypeScript type stripper.
 *
 * Each case strips the TS source and compares against the expected
 * JavaScript (whitespace-normalized). Also covers the safety cases:
 * plain JS passes through unchanged (return 0), ternaries/object
 * literals/regex survive, and unsupported constructs return -2.
 * Build: see build_ts_unit.sh.  Exit 0 on success.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "web/typescript/cwv_ts_core.h"

static int failures;

static void norm(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    int ws = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            ws = 1;
            continue;
        }
        if (ws && o) out[o++] = ' ';
        ws = 0;
        out[o++] = *s;
    }
    out[o] = 0;
}

static void check(const char *ts, const char *expect)
{
    char buf[8192], got[8192], ex[8192];
    int rc = vw_ts_strip(ts, buf, sizeof(buf));
    norm(buf, got, sizeof(got));
    norm(expect, ex, sizeof(ex));
    if (rc != 1) {
        printf("FAIL [%s]\n  rc=%d (expected 1 = transformed)\n", ts, rc);
        failures++;
        return;
    }
    if (strcmp(got, ex) != 0) {
        printf("FAIL [%s]\n  got:    [%s]\n  expect: [%s]\n", ts, got, ex);
        failures++;
        return;
    }
    printf("PASS [%s]\n", ts);
}

static void check_passthrough(const char *js)
{
    char buf[8192], got[8192], ex[8192];
    int rc = vw_ts_strip(js, buf, sizeof(buf));
    norm(buf, got, sizeof(got));
    norm(js, ex, sizeof(ex));
    if (rc != 0) {
        printf("FAIL passthrough [%s]\n  rc=%d (expected 0)\n", js, rc);
        failures++;
        return;
    }
    if (strcmp(got, ex) != 0) {
        printf("FAIL passthrough [%s]\n  got:    [%s]\n  expect: [%s]\n",
               js, got, ex);
        failures++;
        return;
    }
    printf("PASS passthrough [%s]\n", js);
}

static void check_unsupported(const char *ts, const char *why)
{
    char buf[8192];
    int rc = vw_ts_strip(ts, buf, sizeof(buf));
    if (rc != -2) {
        printf("FAIL unsupported [%s]\n  rc=%d (expected -2)\n", ts, rc);
        failures++;
        return;
    }
    if (why && !strstr(vw_ts_unsupported_reason(), why)) {
        printf("FAIL unsupported [%s]\n  reason: %s (expected %s)\n",
               ts, vw_ts_unsupported_reason(), why);
        failures++;
        return;
    }
    printf("PASS unsupported [%s] (%s)\n", ts, vw_ts_unsupported_reason());
}

int main(void)
{
    /* ---- annotations ---- */
    check("let x: number = 5;", "let x = 5;");
    check("let a: string, b: number[] = [1];", "let a, b = [1];");
    check("const s!: string = 'y';", "const s = 'y';");
    check_passthrough("var re = /a:b/g;");            /* regex survives */
    check("function f(a: number, b?: string): void { log(a); }",
          "function f(a, b) { log(a); }");
    check("function g<T>(x: T): T { return x; }",
          "function g(x) { return x; }");
    check("function h(this: App, a: number) { return a; }",
          "function h( a) { return a; }");
    check_passthrough("let t = cond ? a : b;");       /* ternary */
    check_passthrough("let o = { a: 1, b: 2 };");    /* object literals */
    check("const m = items.map((x: string, i: number) => x + i);",
          "const m = items.map((x, i) => x + i);");
    check("const cb = (a: number): ((b: number) => void) => a;",
          "const cb = (a) => a;");
    check("let fn = function (n: number): number { return n; };",
          "let fn = function(n) { return n; };");

    /* ---- interfaces / type aliases / declare / import type ---- */
    check("interface Point { x: number; y: number; }"
          "let p: Point = { x: 1, y: 2 };",
          "let p = { x: 1, y: 2 };");
    check("type Alias = string | number; let q: Alias = 3;",
          "let q = 3;");
    check("declare var ambient: string; let z = ambient;",
          " let z = ambient;");
    check("import type { P } from './p'; let w = 1;",
          " let w = 1;");
    check("export type Q = { a: number }; let v = 2;",
          "let v = 2;");

    /* ---- enums ---- */
    check("enum Color { Red, Green, Blue }",
          "var Color;(function(Color){Color[Color[\"Red\"] = 0] = \"Red\";"
          "Color[Color[\"Green\"] = 1] = \"Green\";"
          "Color[Color[\"Blue\"] = 2] = \"Blue\";})(Color || (Color = {}));");
    check("const enum Dir { Up = 1, Down = 'd' }",
          "var Dir;(function(Dir){Dir[\"Up\"] = 1;Dir[Dir[\"Up\"]] = \"Up\";"
          "Dir[\"Down\"] = 'd';})(Dir || (Dir = {}));");

    /* ---- classes ---- */
    check("class A { private v: string = 'x';"
          " constructor(private p: number, q: string) { this.q = q; }"
          " get val(): number { return this.p; }"
          " set val(n: number) { this.p = n; }"
          " m<T>(x: T): T { return x; } }",
          "class A { v = 'x';"
          " constructor(p, q) {this.p=p; this.q = q; }"
          " get val() { return this.p; }"
          " set val(n) { this.p = n; }"
          " m(x) { return x; } }");
    check("class B<T> extends Base implements Iface, Other { "
          "static count: number = 0; "
          "[key: string]: any; "
          "overload(a: string): void; "
          "overload(a: number): void; "
          "overload(a: any): void { this.a = a; } }",
          "class B extends Base { static count = 0; "
          "overload(a) { this.a = a; } }");
    check("abstract class C { abstract go(): void; "
          "concrete(n: number = 2): number { return n; } }",
          "class C { concrete(n = 2) { return n; } }");

    /* ---- casts / non-null / generic calls ---- */
    check("let u = v as number;", "let u = v ;");
    check("let u2 = v as const;", "let u2 = v ;");
    check("let r = w satisfies number;", "let r = w ;");
    check("let arr = pair<number>(3);", "let arr = pair(3);");
    check("let nn = maybe!.deep!.value;", "let nn = maybe.deep.value;");
    check_passthrough("let cmp = a < b > (c);");  /* comparisons */
    check_passthrough("let flag = a !== b;");

    /* ---- overload declarations dropped ---- */
    check("function ov(a: string): void;\nfunction ov(a: any) { log(a); }",
          "function ov(a) { log(a); }");

    /* ---- catch annotation ---- */
    check("try { x(); } catch (e: unknown) { log(e); }",
          "try { x(); } catch (e) { log(e); }");

    /* ---- passthrough (plain JS is never rewritten) ---- */
    check_passthrough("let a = { b: 1 ? 2 : 3 };");
    check_passthrough("let t = c ? x : y; let o = {a: 1};");
    check_passthrough("if (a < b && c > d) { f(); }");
    check_passthrough("let s = 'a:b'; let t2 = \"x\"; let tpl = `${a}: ok`;");

    /* ---- unsupported constructs: honest -2 ---- */
    check_unsupported("namespace N { export const x = 1; }", "namespaces");
    check_unsupported("declare module \"pkg\" { export const y: number; }",
                      "ambient module");
    check_unsupported("@dec class D {}", "decorators");

    printf("%s\n", failures ? "TS UNIT: FAILURES" : "TS UNIT: ALL PASS");
    return failures ? 1 : 0;
}
