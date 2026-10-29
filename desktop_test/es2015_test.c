/* ES6 module integration test: external script + external stylesheet + the
 * CwvESM transformer (same wiring main.c would use, minus the .suprx
 * boundary - cwv_es2015_core.c is the identical file the module compiles).
 * Exit code 0 = everything worked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#include "core/cwv_renderer.h"
#include "web/js/modern/cwv_es2015_core.h"
#include "web/js/modern/cwv_es_libs.h"
#include "core/cwv_backend.h"

static CwvRenderer *g_r;

/* ---- host callbacks (what main.c passes into CwvESMHostApi) -------- */
static int hook_resolve(void *ud, const char *base, const char *spec,
                        char *out, size_t cap) {
    (void)ud;
    if (!base[0]) base = g_r->base_url;   /* "" = current page base */
    resolve_url(base, spec, out, cap);
    return 0;
}
static int hook_load_file(void *ud, const char *url, char *out, size_t cap) {
    unsigned char *d = NULL;
    size_t s = 0;
    (void)ud;
    if (load_resource(g_r, url, &d, &s) < 0) return -1;
    size_t n = s < cap - 1 ? s : cap - 1;
    memcpy(out, d, n);
    out[n] = 0;
    free(d);
    return (int)n;
}
static int hook_evaluate(void *ud, const char *js) {
    (void)ud;
    return js_eval(g_r, js);
}
/* the prelude hook: same source the module's cwv_esm_prelude returns */
static const char *prelude_hook(void *ud) {
    (void)ud;
    return cwv_es_editions_prelude();
}

/* Case-B helper: a transform whose exports were stubbed to "return 0"
 * (exactly what Vita3K does for unresolved NIDs). */
static int stub_transform_returning_zero(const char *in, char *out, size_t cap, void *ud) {
    (void)in; (void)out; (void)cap; (void)ud;
    return 0;
}

/* the transform trampoline: same body as the module's cwv_esm_transform */
static int transform_hook(const char *in, char *out, size_t cap, void *ud) {
    VwEsHooks hooks;
    hooks.userdata = ud;
    hooks.resolve_url = hook_resolve;
    hooks.load_file = hook_load_file;
    hooks.evaluate = hook_evaluate;
    return vw_es2015_transform(in, out, cap, &hooks);
}

static int check(const char *hay, const char *needle) {
    if (!strstr(hay, needle)) { printf("MISSING: %s\n", needle); return 1; }
    return 0;
}

int main(void) {
    stub_set_log(stdout);
    CwvBackend be;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    memset(&be, 0, sizeof(be));
    ops->initialize(&be, 480, 272);

    /* register the ES6 transformer - what main.c does after loading the
       CwvESM.suprx module */
    g_r = (CwvRenderer *)be.userdata;
    js_set_source_transform(g_r, transform_hook, NULL);
    js_set_prelude_hook(g_r, prelude_hook, NULL);

    int rc = ops->load(&be, "file:///app0/js3.html");
    printf("LOAD rc=%d\n", rc);
    if (rc != 0) return 1;

    char log[8192];
    js_console_dump(g_r, log, sizeof(log));
    printf("--- console ---\n%s----------------\n", log);

    int bad = 0;
    bad += check(log, "log: total is 6");           /* arrow + reduce + template */
    bad += check(log, "log: joined ab");            /* for..of over an array */
    bad += check(log, "log: ch o");
    bad += check(log, "log: ch k");                 /* for..of over a string */
    /* ES6 LIBRARY (provided directly by the module prelude, no conversion) */
    bad += check(log, "log: found 2");              /* Array.prototype.find  */
    bad += check(log, "log: padded [00ab]");
    bad += check(log, "log: merged 1 2");           /* Object.assign         */
    bad += check(log, "log: map ok 2");             /* Map + arrow transform */
    bad += check(log, "log: set ok 2");             /* Set                   */

    /* target text came from helper.js: import + export + template */
    int t = -1;
    for (int i = 0; i < g_r->node_count; i++)
        if (!strcmp(g_r->nodes[i].id, "target")) { t = i; break; }
    int owner = -1;
    for (int i = t + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++)
        if (g_r->nodes[i].text[0]) { owner = i; break; }
    printf("target text=[%s]\n", owner >= 0 ? g_r->nodes[owner].text : "(none)");
    bad += !(owner >= 0 && !strcmp(g_r->nodes[owner].text, "hi world from helper"));

    /* external stylesheet: #styled must be green + bold */
    int s = -1;
    for (int i = 0; i < g_r->node_count; i++)
        if (!strcmp(g_r->nodes[i].id, "styled")) { s = i; break; }
    printf("styled color=%08X bold=%d\n", g_r->nodes[s].color, g_r->nodes[s].bold);
    bad += !(g_r->nodes[s].color == 0xFF00FF00 && g_r->nodes[s].bold == 1);

    /* the ES6 arrow listener fires through the activate path */
    g_r->focus_node = s;
    ops->focus_activate(&be);
    ops->update(&be);
    js_console_dump(g_r, log, sizeof(log));
    bad += check(log, "log: activated es6 listener");

    /* ==== ES6 + JavaScript demo page (testdata/es6_demo.html) ============
     * A self-contained example page: inline ES6 script (arrows, template
     * literals, for..of, Map/Set) driving the UI API.  The assertions below
     * are the "ES6 testing" part - they pin the demo's behavior. */
    rc = ops->load(&be, "file:///app0/es6_demo.html");
    printf("DEMO LOAD rc=%d\n", rc);
    if (rc != 0) return 1;
    js_console_dump(g_r, log, sizeof(log));
    printf("--- demo console ---\n%s--------------------\n", log);
    /* find the demo's elements */
    int inc = -1, scan = -1, reset = -1, count = -1, out = -1;
    for (int i = 0; i < g_r->node_count; i++) {
        if (!strcmp(g_r->nodes[i].id, "inc")) inc = i;
        if (!strcmp(g_r->nodes[i].id, "scan")) scan = i;
        if (!strcmp(g_r->nodes[i].id, "reset")) reset = i;
        if (!strcmp(g_r->nodes[i].id, "count")) count = i;
        if (!strcmp(g_r->nodes[i].id, "out")) out = i;
    }
    if (inc < 0 || scan < 0 || reset < 0 || count < 0 || out < 0) {
        printf("MISSING: demo elements not found\n");
        return 1;
    }

    /* activate #inc three times: the arrow listener must update #count,
       and each call goes through a DIFFERENT (correct) element handle -
       this is the regression test for the js_push_element prototype bug
       where getElementById returned one shared object. */
    for (int t = 0; t < 3; t++) {
        g_r->focus_node = inc;
        ops->focus_activate(&be);
        ops->update(&be);
    }
    char ctext[128] = {0};
    for (int i = count + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++)
        if (g_r->nodes[i].text[0]) { snprintf(ctext, sizeof(ctext), "%s", g_r->nodes[i].text); break; }
    printf("count text=[%s]\n", ctext);
    bad += (strcmp(ctext, "Clicks: 3") != 0);

    /* #out's TEXT must have turned green (0x34d399).  This is the
       regression test for stale parse-time inheritance: el.style.cssText
       used to repaint only the <p> box, not its child text node. */
    {
        unsigned green = rgba(52, 211, 153, 255);
        int ti;
        for (ti = out + 1; ti < g_r->node_count && !g_r->nodes[ti].is_box; ti++) {
            if (g_r->nodes[ti].text[0]) break;
        }
        if (ti < g_r->node_count && !g_r->nodes[ti].is_box &&
            g_r->nodes[ti].text[0]) {
            printf("out text color=%08x (want %08x)\n", g_r->nodes[ti].color, green);
            bad += (g_r->nodes[ti].color != green);
        } else {
            printf("MISSING: #out text node not found\n");
            bad++;
        }
    }

    /* #scan: for..of builds the string, Map.get + Array.find feed a
       template literal, and setText lands on #out (NOT on #count). */
    g_r->focus_node = scan;
    ops->focus_activate(&be);
    ops->update(&be);
    char otext[256] = {0};
    for (int i = out + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++)
        if (g_r->nodes[i].text[0]) { snprintf(otext, sizeof(otext), "%s", g_r->nodes[i].text); break; }
    printf("out text=[%s]\n", otext);
    bad += (strcmp(otext, "beta is #2, chars: b e t a ") != 0);
    js_console_dump(g_r, log, sizeof(log));
    bad += check(log, "log: scan: beta is 2");
    bad += check(log, "log: set size 3");

    /* #reset restores the initial UI */
    g_r->focus_node = reset;
    ops->focus_activate(&be);
    ops->update(&be);
    for (int i = count + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++)
        if (g_r->nodes[i].text[0]) { snprintf(ctext, sizeof(ctext), "%s", g_r->nodes[i].text); break; }
    printf("count text=[%s]\n", ctext);
    bad += (strcmp(ctext, "Clicks: 0") != 0);
    /* after #reset, #out's text must be back to the body color (0xf8fafc)
       - the reset listener assigns style.cssText too, so the propagation
       runs in both directions. */
    {
        unsigned body_c = rgba(248, 250, 252, 255);
        int ti;
        for (ti = out + 1; ti < g_r->node_count && !g_r->nodes[ti].is_box; ti++) {
            if (g_r->nodes[ti].text[0]) break;
        }
        if (ti < g_r->node_count && !g_r->nodes[ti].is_box &&
            g_r->nodes[ti].text[0]) {
            printf("out text color=%08x (want %08x)\n", g_r->nodes[ti].color, body_c);
            bad += (g_r->nodes[ti].color != body_c);
        }
    }

    /* ==== set_theme regression (the "green for less than a second" bug) ==
     * main.c calls apply_runtime_overrides() every frame, which calls
     * cwv_host_set_theme() -> cwv_backend_set_theme().  A no-op
     * theme call used to re-propagate default text colors and revert JS
     * style changes within one frame.  Re-run the #inc listener, then
     * hammer set_theme: same-value (per-frame) and a real switch. */
    for (int t = 0; t < 3; t++) {
        g_r->focus_node = inc;
        ops->focus_activate(&be);
        ops->update(&be);
    }
    {
        unsigned green = rgba(52, 211, 153, 255);
        int ti;
        for (ti = out + 1; ti < g_r->node_count && !g_r->nodes[ti].is_box; ti++)
            if (g_r->nodes[ti].text[0]) break;
        if (ti >= g_r->node_count || g_r->nodes[ti].is_box) { printf("MISSING #out text\n"); return 1; }
        /* per-frame no-op call: color must survive */
        for (int t = 0; t < 5; t++) (void)cwv_backend_set_theme(&be, 0);
        printf("after 5x same-theme: %08x\n", g_r->nodes[ti].color);
        bad += (g_r->nodes[ti].color != green);
        /* a REAL theme switch must not revert an inline style either
           (the text node is color_explicit after style_apply_inline) */
        (void)cwv_backend_set_theme(&be, 1);
        printf("after real switch: %08x\n", g_r->nodes[ti].color);
        bad += (g_r->nodes[ti].color != green);
        (void)cwv_backend_set_theme(&be, 0);
        bad += (g_r->nodes[ti].color != green);
    }

    /* ==== ES6 SELF-TEST PAGE (testdata/es6_selftest.html) ================
     * A brand-new page, standard DOM only, that exercises the transformer
     * and the prelude library end-to-end.  Proves the JS/ES6 surface is
     * engine-level (any page in the web dir gets it), not per-page. */
    rc = ops->load(&be, "file:///app0/es6_selftest.html");
    printf("SELFTEST LOAD rc=%d\n", rc);
    if (rc != 0) return 1;
    ops->update(&be);
    js_console_dump(g_r, log, sizeof(log));
    printf("--- selftest console ---\n%s--------------------\n", log);
    bad += check(log, "log: es6-selftest 23 passed 0 failed");
    {
        int si = -1, ok_rows = 0;
        for (int i = 0; i < g_r->node_count; i++)
            if (!strcmp(g_r->nodes[i].id, "summary")) si = i;
        if (si < 0) { printf("MISSING #summary\n"); return 1; }
        for (int i = si + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++) {
            if (g_r->nodes[i].text[0]) {
                printf("summary=[%s]\n", g_r->nodes[i].text);
                ok_rows += (strcmp(g_r->nodes[i].text, "ES6 self-test: 23 passed, 0 failed") == 0);
                bad += (g_r->nodes[i].color != rgba(52, 211, 153, 255));
                break;
            }
        }
        for (const char *p = log; (p = strstr(p, "PASS ")) != NULL; p++) ok_rows++;
        bad += (ok_rows < 24);   /* summary + all 23 PASS rows */
    }

    ops->shutdown(&be);

    /* ==== ES7+ SELF-TEST PAGE (testdata/es7_selftest.html) =============
     * The 2026-09-26 (48) transformer additions: spread, destructuring,
     * for-of patterns, pattern/default/rest parameters.  Same standard
     * t() harness as the ES6 page; summary must be exact + green. */
    ops->initialize(&be, 480, 272);
    g_r = (CwvRenderer *)be.userdata;
    js_set_source_transform(g_r, transform_hook, NULL);
    js_set_prelude_hook(g_r, prelude_hook, NULL);
    rc = ops->load(&be, "file:///app0/es7_selftest.html");
    printf("ES7SELF LOAD rc=%d\n", rc);
    if (rc != 0) return 1;
    ops->update(&be);
    js_console_dump(g_r, log, sizeof(log));
    printf("--- es7 selftest console ---\n%s--------------------\n", log);
    bad += check(log, "es7-selftest 21 passed 0 failed");
    {
        int si = -1, ok_rows = 0;
        for (int i = 0; i < g_r->node_count; i++)
            if (!strcmp(g_r->nodes[i].id, "summary")) si = i;
        if (si < 0) { printf("MISSING #summary (es7)\n"); return 1; }
        for (int i = si + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++) {
            if (g_r->nodes[i].text[0]) {
                printf("es7summary=[%s]\n", g_r->nodes[i].text);
                ok_rows += (strcmp(g_r->nodes[i].text, "ES7 self-test: 21 passed, 0 failed") == 0);
                bad += (g_r->nodes[i].color != rgba(52, 211, 153, 255));
                break;
            }
        }
        for (const char *p = log; (p = strstr(p, "PASS ")) != NULL; p++) ok_rows++;
        bad += (ok_rows < 22);   /* summary + all 21 PASS rows */
        for (const char *p = log; (p = strstr(p, "FAIL ")) != NULL; p++)
            { printf("ES7 FAILURE ROW: %.80s\n", p); bad++; }
    }

    ops->shutdown(&be);

    /* Class page: exercise the real load/transform/execute/DOM path. */
    ops->initialize(&be, 480, 272);
    g_r = (CwvRenderer *)be.userdata;
    js_set_source_transform(g_r, transform_hook, NULL);
    js_set_prelude_hook(g_r, prelude_hook, NULL);
    rc = ops->load(&be, "file:///app0/es_class_selftest.html");
    printf("CLASSSELF LOAD rc=%d\n", rc);
    if (rc != 0) return 1;
    ops->update(&be);
    js_console_dump(g_r, log, sizeof(log));
    printf("--- class selftest console ---\n%s--------------------\n", log);
    bad += check(log, "class-selftest 22 passed 0 failed");
    {
        int si = -1, ok_rows = 0;
        for (int i = 0; i < g_r->node_count; i++)
            if (!strcmp(g_r->nodes[i].id, "summary")) si = i;
        if (si < 0) { printf("MISSING #summary (class)\n"); return 1; }
        for (int i = si + 1; i < g_r->node_count && !g_r->nodes[i].is_box; i++) {
            if (g_r->nodes[i].text[0]) {
                printf("classsummary=[%s]\n", g_r->nodes[i].text);
                ok_rows += (strcmp(g_r->nodes[i].text, "Class self-test: 22 passed, 0 failed") == 0);
                bad += (g_r->nodes[i].color != rgba(52, 211, 153, 255));
                break;
            }
        }
        for (const char *p = log; (p = strstr(p, "PASS ")) != NULL; p++) ok_rows++;
        bad += (ok_rows < 23);   /* summary + all 22 PASS rows */
        for (const char *p = log; (p = strstr(p, "FAIL ")) != NULL; p++)
            { printf("CLASS FAILURE ROW: %.80s\n", p); bad++; }
    }

    ops->shutdown(&be);

    /* ---------------------------------------------------------------
     * ESM SELF-TEST REGRESSION (pins the "silently stubbed module" bug:
     * on Vita3K a suprx whose NIDs did not resolve gets return-0 stubs,
     * which looks like "module enabled" while scripts run raw ES6).
     * --------------------------------------------------------------- */
    {
        char rep[160];

        /* Case A: real transform + real prelude -> full PASS.  Uses the
         * same registration calls host.c makes after module load. */
        ops->initialize(&be, 480, 272);
        g_r = (CwvRenderer *)be.userdata;
        js_set_source_transform(g_r, transform_hook, NULL);
        js_set_prelude_hook(g_r, prelude_hook, NULL);
        int st = cwv_backend_esm_selftest(&be, rep, sizeof(rep));
        printf("selftest A: rc=%d rep=[%s]\n", st, rep);
        bad += (st != 1) + check(rep, "transform=PASS")
             + check(rep, "compile=PASS") + check(rep, "prelude=PASS");

        /* Case B: the Vita3K stub signature - a transform that answers
         * 0 ("nothing to transform") for a real ES6 probe.  MUST fail. */
        js_set_source_transform(g_r, stub_transform_returning_zero, NULL);
        st = cwv_backend_esm_selftest(&be, rep, sizeof(rep));
        printf("selftest B: rc=%d rep=[%s]\n", st, rep);
        bad += (st != 0) + check(rep, "transform=FAIL");

        /* Case C: no transform registered at all.  MUST fail loudly. */
        js_set_source_transform(g_r, NULL, NULL);
        st = cwv_backend_esm_selftest(&be, rep, sizeof(rep));
        printf("selftest C: rc=%d rep=[%s]\n", st, rep);
        bad += (st != 0) + check(rep, "transform=NONE");

        /* Case D - the BOOT FALLBACK FLOW main.c now runs at startup:
         * module loaded but its exports were stubbed (Case B) -> self-test
         * FAIL -> switch to the built-in fallback (the same core, compiled
         * into the app) -> self-test must PASS again, and a real ES6 page
         * must load with zero errors right after the switch.  This is the
         * exact recovery a Vita3K user gets, pinned end-to-end. */
        js_set_source_transform(g_r, stub_transform_returning_zero, NULL);
        st = cwv_backend_esm_selftest(&be, rep, sizeof(rep));
        printf("selftest D1 (stubbed module): rc=%d rep=[%s]\n", st, rep);
        bad += (st != 0) + check(rep, "transform=FAIL");
        /* "fallback": the same registration enable_es6_builtin performs */
        js_set_source_transform(g_r, transform_hook, NULL);
        js_set_prelude_hook(g_r, prelude_hook, NULL);
        st = cwv_backend_esm_selftest(&be, rep, sizeof(rep));
        printf("selftest D2 (built-in fallback): rc=%d rep=[%s]\n", st, rep);
        bad += (st != 1) + check(rep, "transform=PASS")
             + check(rep, "compile=PASS") + check(rep, "prelude=PASS");
        /* and the page still works: reload js3.html end-to-end */
        rc = ops->load(&be, "file:///app0/js3.html");
        js_console_dump(g_r, log, sizeof(log));
        printf("--- console (after fallback) ---\n%s--------------------------------\n", log);
        bad += (rc != 0) + check(log, "log: total is 6")
             + check(log, "log: map ok 2");

        ops->shutdown(&be);
    }

    printf(bad ? "ES6 TEST FAILED (%d)\n" : "ES6 TEST PASSED\n", bad);
    return bad ? 1 : 0;
}
