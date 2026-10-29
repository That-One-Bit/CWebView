/* error_audit_test.c - language error-handling audit (2026-10-01).
 *
 * Feeds deliberately malformed input to every supported language layer
 * and verifies the engine stays alive, keeps rendering, and reports
 * errors through its real channels (JS console ring / script error
 * count) instead of silently continuing:
 *
 *   HTML  - unclosed tags/quotes/comments, bad entities, broken CSS
 *           inside <style>, stray table/list structure
 *   CSS   - unclosed braces, absurd values (in the same page)
 *   JS    - syntax error in one <script>, runtime TypeError, explicit
 *           throw, then a LATER script must still run (engine state
 *           survives per-script failures)
 *   CWMenu- bad version/colors/kinds/coords/styles/pair, dangling block;
 *           cwmenu_script_load must report the error count (negative rc)
 *
 * Build: see build_error_audit.sh (also runs an ASan pass). Exit 0 ok.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#include "core/cwv_renderer.h"
#include "core/cwv_backend.h"
#include "menu/cwmenu_api.h"
#include "menu/native_menu_templates.h"
#include "web/typescript/cwv_ts_core.h"
#include "web/js/modern/cwv_es2015_core.h"

/* ES6 -> ES5 adapter with NULL hooks (no module loader in this test;
 * the syntax conversion is what matters here). */
static int audit_es6_adapter(const char *in, char *out, size_t cap, void *ud)
{
    (void)ud;
    return vw_es2015_transform(in, out, cap, NULL);
}

/* signature adapter: vw_ts_strip has no userdata parameter */
static int audit_ts_adapter(const char *in, char *out, size_t cap, void *ud)
{
    (void)ud;
    return vw_ts_strip(in, out, cap);
}

static int fails;
static void check(int cond, const char *what) {
    printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}

static int load_and_dump(CwvBackend *be, const char *url, char *log, size_t cap)
{
    const CwvBackendOps *ops = cwv_backend_default_ops();
    int rc = ops->load(be, url);
    int i;
    for (i = 0; i < 3; i++) ops->update(be);
    js_console_dump((CwvRenderer *)be->userdata, log, cap);
    return rc;
}

int main(void) {
    CwvBackend be;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    char log[16384];
    int rc;

    stub_set_log(stdout);
    memset(&be, 0, sizeof(be));
    ops->initialize(&be, 480, 272);

    /* ---- HTML + CSS: malformed page must still load and render ---- */
    rc = load_and_dump(&be, "file:///app0/error_audit/html_broken.html",
                       log, sizeof(log));
    check(rc == 0, "malformed HTML page loads (rc==0)");
    check(ops->update(&be) >= 0, "engine updates after malformed parse");
    check(ops->draw(&be) >= 0, "engine draws after malformed parse");

    /* Regression coverage for tag lexing: quoted '>' characters, comments
       containing fake nested tags, and tag-looking text inside script bodies
       must not confuse the recursive close-tag scan. */
    rc = load_and_dump(&be, "file:///app0/error_audit/parser_edges.html",
                       log, sizeof(log));
    check(rc == 0, "HTML lexer edge fixture loads");
    {
        CwvRenderer *renderer = (CwvRenderer *)be.userdata;
        int i, saw_comment_child = 0, saw_after_script = 0, saw_apostrophe = 0;
        for (i = 0; renderer && i < renderer->node_count; ++i) {
            if (strstr(renderer->nodes[i].text, "after-comment")) saw_comment_child = 1;
            if (strstr(renderer->nodes[i].text, "after-script")) saw_after_script = 1;
            if (strstr(renderer->nodes[i].text, "after-unquoted-apostrophe")) saw_apostrophe = 1;
        }
        check(saw_comment_child, "comment contents do not alter nested tag matching");
        check(saw_after_script, "script markup strings do not alter ancestor matching");
        check(saw_apostrophe, "apostrophe in an unquoted attribute does not swallow tag end");
    }

    /* ---- JS: per-script failures must not kill later scripts ------ */
    rc = load_and_dump(&be, "file:///app0/error_audit/js_broken.html",
                       log, sizeof(log));
    check(rc == 0, "js error page loads");
    printf("--- console ---\n%s----------------\n", log);
    check(strstr(log, "probe start") != NULL,
          "valid first script ran (a syntax error only kills its own script)");
    check(strstr(log, "fifth script runs after syntax error") != NULL,
          "script after the syntax-error script runs");
    check(strstr(log, "fourth ran after previous errors") != NULL,
          "script after runtime error + throw runs");
    check(strstr(log, "error") != NULL, "a console error line exists");

    /* ---- CWMenu: malformed script reports a negative error count -- */
    {
        VitaMenuTemplate items[8];
        CWMenuMenuScriptTarget target[1];
        target[0].menu_name = "probe";
        target[0].items = items;
        target[0].capacity = 8;
        target[0].count = 0;
        rc = cwmenu_script_load("../sources/shared/sources/menu/scripts/default.cwm",
                                target, 1);
        check(rc == 0, "well-formed default.cwm loads with rc==0");
        rc = cwmenu_script_load("testdata/error_audit/cwm_broken.cwm",
                                target, 1);
        printf("  broken cwm rc=%d\n", rc);
        check(rc < 0, "malformed cwm reports negative error count");
    }

    /* ---- TypeScript: strip through the REAL engine ---------------- */
    {
        /* register the built-in TS stripper + ES6 transformer, then
         * load a TS page end-to-end (enum, class, generics, casts). */
        char tsbuf[16384];
        cwv_backend_set_source_transform(&be, audit_es6_adapter, NULL);
        cwv_backend_set_ts_transform(&be, audit_ts_adapter, NULL);
        rc = load_and_dump(&be, "file:///app0/error_audit/ts_page.html",
                          tsbuf, sizeof(tsbuf));
        printf("--- ts console ---\n%s----------------\n", tsbuf);
        check(rc == 0, "typescript page loads end-to-end");
        check(strstr(tsbuf, "ts enum 2 On n") != NULL,
              "TS enum codegen runs (numeric + reverse + string)");
        check(strstr(tsbuf, "ts greet hi bob3") != NULL,
              "TS class + param property + annotations run");
        check(strstr(tsbuf, "ts pick 6") != NULL,
              "TS arrow with annotation + return type runs");
        check(strstr(tsbuf, "ts cast g g") != NULL,
              "TS `as` cast and non-null run");
        check(strstr(tsbuf, "plain js still runs after ts") != NULL,
              "plain <script> runs after a TS block");
        check(strstr(tsbuf, "error") == NULL, "no console errors from the TS page");
    }

    /* ---- still alive after all of it ------------------------------ */
    rc = load_and_dump(&be, "file:///app0/js.html", log, sizeof(log));
    check(rc == 0, "clean page still loads after the abuse");
    check(ops->draw(&be) >= 0, "clean draw after the abuse");

    printf("%s\n", fails ? "ERROR AUDIT: FAILURES" : "ERROR AUDIT: ALL PASS");
    return fails ? 1 : 0;
}
