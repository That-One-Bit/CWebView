#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "web/js/modern/cwv_es2015_core.h"

static int failures;
static void t(const char *name, const char *in, const char *want_sub) {
    char out[16384];
    int rc = vw_es2015_transform(in, out, sizeof(out), NULL);
    if (rc < 0) { printf("FAIL %-22s rc=%d\n", name, rc); failures++; return; }
    if (!strstr(out, want_sub)) {
        printf("FAIL %-22s\n  in : %s\n  out: %s\n  want: contains [%s]\n",
               name, in, out, want_sub);
        failures++;
    } else printf("ok   %-22s %s\n", name, out);
}
int main(void) {
    /* let/const -> var */
    t("let-basic", "let a = 1;", "var a = 1;");
    t("const-basic", "const b = 2, c = 3;", "var b = 2, c = 3;");
    t("for-let", "for (let i = 0; i < 9; i++) {}",
       "for (var i = 0; i < 9; i++) {}");
    t("not-property", "x.let = 1;", "x.let = 1;");
    /* regression: a declaration DIRECTLY after a comment used to be
       skipped - the comment's own letters ("DOM selections") fooled the
       word-boundary check into seeing an adjacent identifier */
    t("const-after-line-comment", "// DOM selections\nconst out = 1;",
      "// DOM selections\nvar out = 1;");
    t("let-after-block-comment", "/* let/const -> var */\nlet clicks = 0;",
      "/* let/const -> var */\nvar clicks = 0;");
    t("ident-after-comment-safe", "// c\nvar q = infolet + foo.let;",
      "// c\nvar q = infolet + foo.let;");
    /* arrows */
    t("arrow-expr", "var f = (a, b) => a + b;",
      "var f = function (a, b) { return a + b; };");
    t("arrow-block", "var g = (n) => { return n * 2; }",
      "var g = function (n) { return n * 2; }");
    t("arrow-single", "var h = x => x - 1;",
      "var h = function (x) { return x - 1; };");
    t("arrow-in-call", "arr.map(s => s.length)",
      "arr.map(function (s) { return s.length; })");
    t("arrow-in-call2", "arr.reduce((p, c) => p + c, 0)",
      "arr.reduce(function (p, c) { return p + c; }, 0)");
    /* nested arrows inside an expression body: the body is recursively
       transformed (regression: it used to be copied verbatim, leaving the
       inner arrow un-converted -> SyntaxError at runtime) */
    t("arrow-nested-expr",
      "var f = () => [1].map(x => x * 2);",
      "var f = function () { return [1].map(function (x) { return x * 2; }); };");
    t("arrow-nested-expr-single",
      "var f = g => [1].map(x => x * 2);",
      "var f = function (g) { return [1].map(function (x) { return x * 2; }); };");
    t("arrow-nested-tpl",
      "var f = () => `v${[1, 2].map(x => x + 1).join(\",\")}`;",
      "var f = function () { return (\"v\" + ([1, 2].map(function (x) { return x + 1; }).join(\",\")) + \"\"); };");
    /* strings/comments/regex must not be touched */
    t("string-arrow", "var s = \"no => here\";",
      "var s = \"no => here\";");
    t("comment-arrow", "// use => freely\nvar q = 1;",
      "var q = 1;");
    t("regex-not-division", "var re = /a\\/b/g; x", "var re = /a\\/b/g;");
    /* template literals */
    t("template", "var u = `Hi ${name}!`;",
      "(\"Hi \" + (name) + \"!\")");
    t("template-mid", "console.log(`a${1 + 2}b${x * 3}c`)",
       "\"a\" + (1 + 2) + \"b\" + (x * 3) + \"c\"");
    /* for..of */
    t("for-of", "for (const x of [1, 2, 3]) { console.log(x); }",
      "var x = __vw_s0[__vw_i0]");
    t("for-of-let", "for (let i of items) sum += i;",
      "var i = __vw_s1[__vw_i1]");
    t("for-of-nodecl", "for (c of \"str\") { }",
      "c = __vw_s2[__vw_i2]");
    t("for-in-untouched", "for (var k in obj) { }",
      "for (var k in obj) { }");
    t("of-as-word", "var of = 1;", "var of = 1;");
    /* CRLF (Windows-edited) sources: every let/const must still convert.
       Real-world bug: a line comment ending in \r made the transformer miss
       the const on the next line, and "const \r\n x" / "const \r\n" were
       not recognized as keyword starts at all. */
    t("crlf-comment-const", "// header\r\nconst out = 1;\r\nlet n = 2;\r\n",
      "var out = 1;");
    t("crlf-comment-let", "// header\r\nlet clicks = 0;\r\n",
      "var clicks = 0;");
    t("crlf-block-const", "/* c */\r\nconst a = [1, 2];\r\n",
      "var a = [1, 2];");
    t("crlf-const-eol", "var z = 1;\r\nconst\r\nx = 9;\r\n",
      "var\r\nx = 9;");
    t("crlf-for-let", "for (let i = 0; i < 2; i++)\r\n{ }\r\n",
      "for (var i = 0; i < 2; i++)");
    t("crlf-template", "var s = `a\r\nb ${1 + 1}`;",
      "\"a\\nb \" + (1 + 1)");
    /* APOSTROPHE / QUOTE battery: ES5 strings with apostrophes are the
       original "unterminated statement" suspect (case 0 of that
       investigation).  The transformer must convert keywords AROUND
       strings but never touch, split, or re-quote string contents. */
    t("apos-in-dstring", "let m = \"it's fine\";",
      "var m = \"it's fine\"");
    t("dquote-in-sstring", "const s = 'He said \"hi\"';",
      "var s = 'He said \"hi\"';");
    t("apos-escaped", "let q = 'don\\'t';",
      "var q = 'don\\'t';");
    t("apos-in-comment", "// don't convert me\r\nlet x = 1;\r\n",
      "var x = 1;");
    t("apos-in-arrow-body", "let f = (a) => a + \"n't\";",
      "var f = function (a) { return a + \"n't\"; };");
    t("apos-in-template", "let s = `it's ${1 + 1}`;",
      "var s = (\"it's \" + (1 + 1)");
    t("apos-in-html-string", "let html = \"<a href='x'>don't</a>\";",
      "var html = \"<a href='x'>don't</a>\";");

    /* unchanged sources should report 0 */
    char out[1024];
    int rc = vw_es2015_transform("var x = 1;\n", out, sizeof(out), NULL);
    if (rc != 0) { printf("FAIL no-es6 rc=%d\n", rc); failures++; }
    else printf("ok   %-22s (rc=0, no rewrite)\n", "no-es6-source");
    printf(failures ? "%d FAILURES\n" : "ALL UNIT CASES PASSED\n", failures);
    return failures ? 1 : 0;
}
