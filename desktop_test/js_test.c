/* JS feature test: exercises the JavaScript runtime end-to-end.
 * Builds only against the CURRENT (split) renderer - the original has no JS.
 * Exit code 0 = every expected console line was observed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#include "core/cwv_renderer.h"

static int check(const char *hay, const char *needle) {
    if (!strstr(hay, needle)) {
        printf("MISSING: %s\n", needle);
        return 1;
    }
    return 0;
}

int main(void) {
    stub_set_log(stdout);
    CwvBackend be;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    memset(&be, 0, sizeof(be));
    ops->initialize(&be, 480, 272);
    int rc = ops->load(&be, "file:///app0/js.html");
    printf("LOAD rc=%d\n", rc);
    if (rc != 0) return 1;

    /* timers need a frame tick */
    for (int i = 0; i < 3; i++) ops->update(&be);

    /* console + mutations */
    char log[8192];
    ops->eval(&be, "console.log('navigator-platform=' + navigator.platform + ';window=' + window.navigator.platform)");
    js_console_dump((CwvRenderer *)be.userdata, log, sizeof(log));

    /* --- console repeat badge: identical consecutive lines collapse ---
     * three identical errors must produce ONE line with a [x3] marker. */
    ops->eval(&be, "console.error('dup error'); console.error('dup error'); console.error('dup error');");
    char log2[8192];
    int bad = 0;
    js_console_dump((CwvRenderer *)be.userdata, log2, sizeof(log2));
    printf("--- dup console ---\n%s----------------\n", log2);
    bad += (strstr(log2, "error: dup error") == NULL);
    bad += (strstr(log2, "[x3]") == NULL);
    /* a DIFFERENT message must still get its own row */
    ops->eval(&be, "console.error('other error');");
    js_console_dump((CwvRenderer *)be.userdata, log2, sizeof(log2));
    bad += (strstr(log2, "error: other error") == NULL);

    /* --- compile-failure hints name the unsupported construct ---
     * (object shorthand {a, b} is NOT here: Duktape 2.7 parses it
     * natively, so it is not a compile failure on this engine) */
    ops->eval(&be, "var a = [1, ...xs];");
    js_console_dump((CwvRenderer *)be.userdata, log2, sizeof(log2));
    bad += (strstr(log2, "spread/rest") == NULL);
    ops->eval(&be, "class C {}");
    js_console_dump((CwvRenderer *)be.userdata, log2, sizeof(log2));
    bad += (strstr(log2, "'class'") == NULL);
    /* console stats counters (debug overlay source) */
    {
        int errs = 0, wrns = 0, lines = 0;
        js_console_stats((CwvRenderer *)be.userdata, &errs, &wrns, &lines);
        printf("stats: errors=%d warns=%d lines=%d\n", errs, wrns, lines);
        if (errs < 3) { printf("BAD stats error count\n"); bad++; }
        if (lines <= 0) { printf("BAD stats line count\n"); bad++; }
    }

    printf("--- console ---\n%s----------------\n", log);

    bad += check(log, "log: script start");
    bad += check(log, "log: done");
    bad += check(log, "navigator-platform=PlayStation Vita (test stub);window=PlayStation Vita (test stub)");
    bad += check(log, "log: timer fired");
    bad += check(log, "log: q count");

    /* node mutations visible in the renderer state */
    CwvRenderer *r = (CwvRenderer *)be.userdata;
    int out_i = -1, box_i = -1, go_i = -1, lnk_i = -1;
    for (int i = 0; i < r->node_count; i++) {
        if (r->nodes[i].id[0]) {
            if (!strcmp(r->nodes[i].id, "out") && out_i < 0) out_i = i;
            if (!strcmp(r->nodes[i].id, "box") && box_i < 0) box_i = i;
            if (!strcmp(r->nodes[i].id, "go") && go_i < 0) go_i = i;
        }
    }
    /* the link is INLINE: its id lives in the id map, not on a node */
    for (int i = 0; i < r->id_map_count; i++)
        if (!strcmp(r->id_map[i].id, "lnk")) lnk_i = r->id_map[i].node_index;
    printf("ids: out=%d box=%d go=%d lnk(text)=%d\n", out_i, box_i, go_i, lnk_i);
    if (out_i < 0 || box_i < 0 || go_i < 0 || lnk_i < 0) { printf("MISSING id node\n"); return 1; }

    /* the 'out' box: its first text child must read "after" (js_text_owner) */
    int owner = -1;
    for (int i = out_i + 1; i < r->node_count && !r->nodes[i].is_box; i++)
        if (r->nodes[i].text[0]) { owner = i; break; }
    printf("out text=[%s]\n", owner >= 0 ? r->nodes[owner].text : "(none)");
    bad += (owner >= 0 && strcmp(r->nodes[owner].text, "after") != 0);

    /* box element: CSS via the registry (double border 4px, bg, color, bold) */
    CwvNode *box = &r->nodes[box_i];
    printf("box bstyle=%d bw=%.2f bg=%08X col=%08X bold=%d\n",
           box->border_style, box->border_width, box->background, box->color, box->bold);
    bad += !(box->border_style == 4 && box->border_width >= 3.99f);
    bad += !(box->background == 0xFF281810);
    bad += !(box->bold == 1);

    /* activate the button via the keyboard/focus path */
    r->focus_node = go_i;
    ops->focus_activate(&be);
    ops->update(&be);
    ops->pointer_cancel(&be);   /* clear the visual button press state */
    js_console_dump(r, log, sizeof(log));
    bad += check(log, "log: button fired");

    /* activate the link by TAPPING it; the JS click listener runs, and its
       event.preventDefault() CANCELS the navigation (Chrome semantics:
       a listener that does not cancel still lets the href navigate) */
    {
        CwvNode *lnk = &r->nodes[lnk_i];
        int px = (int)(lnk->x + lnk->w * 0.5f), py = (int)(lnk->y + lnk->h * 0.5f);
        ops->pointer_event(&be, px, py, 1);
        ops->pointer_event(&be, px, py, 0);
        ops->update(&be);
    }
    printf("nav=[%s]\n", ops->navigation_url(&be));
    js_console_dump(r, log, sizeof(log));
    bad += check(log, "log: link intercepted");
    bad += (strlen(ops->navigation_url(&be)) != 0);   /* preventDefault worked */

    /* plain link (no listener): click->navigate order must still navigate */
    {
        int lnk2_i = -1;
        CwvNode *lnk2;
        int px, py;
        for (int i = 0; i < r->id_map_count; i++)
            if (!strcmp(r->id_map[i].id, "lnk2")) lnk2_i = r->id_map[i].node_index;
        if (lnk2_i < 0) { printf("MISSING lnk2 id\n"); return 1; }
        lnk2 = &r->nodes[lnk2_i];
        px = (int)(lnk2->x + lnk2->w * 0.5f); py = (int)(lnk2->y + lnk2->h * 0.5f);
        ops->pointer_event(&be, px, py, 1);
        ops->pointer_event(&be, px, py, 0);
        ops->update(&be);
    }
    printf("nav2=[%s]\n", ops->navigation_url(&be));
    bad += (strcmp(ops->navigation_url(&be), "second.html") != 0);   /* navigates */

    /* eval() */
    rc = ops->eval(&be, "console.log('eval works ' + (2 + 3))");
    js_console_dump(r, log, sizeof(log));
    printf("eval rc=%d\n", rc);
    bad += check(log, "log: eval works 5");

    /* exit redirect: no handler on a fresh page, handler on this page */
    ops->load(&be, "file:///app0/basic.html");
    /* vcm combo -> :active + JS listener (js2.html) */
    ops->load(&be, "file:///app0/js2.html");
    {
        CwvRenderer *r2 = (CwvRenderer *)be.userdata;
        int vcmb = -1;
        for (int i = 0; i < r2->node_count; i++)
            if (r2->nodes[i].vcm[0] && !strcmp(r2->nodes[i].vcm, "cross")) { vcmb = i; break; }
        printf("vcm box node=%d\n", vcmb);
        if (vcmb < 0) { printf("MISSING vcm box\n"); bad++; }
        else {
            CwvNode *v = &r2->nodes[vcmb];
            /* Native activation: sets the state and fires the page's
               standard addEventListener("click", ...) listener.  The
               :active PAINT is done by draw_node() through a temporary
               node copy, so the stored node keeps its base style
               (rgba(0,0,0,0) here - only the :active rule is red). */
            ops->vcm_activate(&be, "cross");
            printf("vcm active=%d bg=%08X focus=%d\n",
                   v->vcm_active, v->background, r2->focus_node);
            bad += !(v->vcm_active == 1);
            bad += !(r2->focus_node == vcmb);   /* combo focuses the node */
            ops->update(&be);
            js_console_dump(r2, log, sizeof(log));
            bad += check(log, "log: cross combo fired");
        }

        /* fetch() with NO backend registered: graceful failure only */
        {
            int frc = ops->load(&be, "file:///app0/fetch.html");
            char flog[4096];
            printf("FETCH LOAD rc=%d\n", frc);
            if (frc != 0) { printf("MISSING fetch.html load\n"); return 1; }
            for (int i = 0; i < 2; i++) ops->update(&be);
            js_console_dump((CwvRenderer *)be.userdata, flog, sizeof(flog));
            printf("--- fetch console ---\n%s----------------\n", flog);
            bad += check(flog, "cb status=-1 len=0");
            bad += check(flog, "fetch returned false");
            bad += check(flog, "no network backend");
            bad += check(flog, "boom");       /* throwing callback: no crash */
            bad += check(flog, "still alive");
        }

    }

    ops->shutdown(&be);
    printf(bad ? "JS TEST FAILED (%d)\n" : "JS TEST PASSED\n", bad);
    return bad ? 1 : 0;
}
