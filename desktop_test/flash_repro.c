/* Color-flash repro: load es6_demo.html, click #inc, then simulate the
 * Vita main loop (per-frame set_theme + update) and watch #out's color. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#include "core/cwv_renderer.h"
#include "web/js/modern/cwv_es2015_core.h"
#include "web/js/modern/cwv_es_libs.h"
#include "core/cwv_backend.h"

static CwvRenderer *g_r;

static int hook_resolve(void *ud, const char *base, const char *spec,
                        char *out, size_t cap) {
    (void)ud;
    if (!base[0]) base = g_r->base_url;
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
static const char *prelude_hook(void *ud) { (void)ud; return cwv_es_editions_prelude(); }
static int transform_hook(const char *in, char *out, size_t cap, void *ud) {
    VwEsHooks hooks;
    hooks.userdata = ud;
    hooks.resolve_url = hook_resolve;
    hooks.load_file = hook_load_file;
    hooks.evaluate = hook_evaluate;
    return vw_es2015_transform(in, out, cap, &hooks);
}

static void dump_nodes(const char *when) {
    for (int i = 0; i < g_r->node_count; i++) {
        CwvNode *n = &g_r->nodes[i];
        if (n->id[0] && (strcmp(n->id, "out") == 0 || strcmp(n->id, "count") == 0)) {
            printf("  [%s] node %d <%s id=%s> color=#%08x explicit=%d text=[%.30s]\n",
                   when, i, n->tag, n->id, n->color, n->color_explicit, n->text);
        }
    }
    for (int i = 0; i < g_r->node_count; i++) {
        CwvNode *n = &g_r->nodes[i];
        if (!n->is_box && n->text[0] && strstr(n->text, "output") == n->text) {
            printf("  [%s] textrun %d color=#%08x explicit=%d text=[%.30s]\n",
                   when, i, n->color, n->color_explicit, n->text);
        }
        if (!n->is_box && n->text[0] && strstr(n->text, "Clicks") == n->text) {
            printf("  [%s] textrun %d color=#%08x explicit=%d text=[%.30s]\n",
                   when, i, n->color, n->color_explicit, n->text);
        }
    }
}

int main(void) {
    stub_set_log(stdout);
    CwvBackend b;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    memset(&b, 0, sizeof(b));
    ops->initialize(&b, 960, 544);
    g_r = (CwvRenderer *)b.userdata;
    js_set_source_transform(g_r, transform_hook, NULL);
    js_set_prelude_hook(g_r, prelude_hook, NULL);

    int rc = ops->load(&b, "file:///app0/es6_demo.html");
    printf("LOAD rc=%d nodes=%d theme_dark=%d\n", rc, g_r->node_count, g_r->theme_dark);
    dump_nodes("after load");

    cwv_backend_set_theme(&b, 1);
    printf("theme pass(dark=1) done\n");
    dump_nodes("after theme pass");

    for (int i = 0; i < 12; i++) {
        ops->focus_cycle(&b, 1);
        if (g_r->focus_node >= 0) {
            CwvNode *fn = &g_r->nodes[g_r->focus_node];
            printf("focus %d: tag=%s id=%s text=[%.20s]\n", i, fn->tag, fn->id, fn->text);
            if (fn->id[0] && strcmp(fn->id, "inc") == 0) break;
        }
    }
    ops->focus_activate(&b);
    dump_nodes("after click #inc");

    for (int f = 0; f < 5; f++) {
        cwv_backend_set_theme(&b, 1);
        ops->update(&b);
        ops->draw(&b);
        char when[32];
        snprintf(when, sizeof(when), "frame %d", f);
        dump_nodes(when);
    }

    cwv_backend_set_theme(&b, 0);
    dump_nodes("after theme flip to light");
    cwv_backend_set_theme(&b, 0);
    dump_nodes("frame with light theme");

    /* ---- assertions (2026-09-21 (29)) ----------------------------------
     * A theme flip must NEVER touch page-owned colors: every node that
     * inherited the page's explicit body color keeps #f8fafc, and the
     * JS-styled #out keeps its own explicit green.  The pre-fix pass
     * repainted inherited text to the light default rgba(25,25,30) -
     * invisible on the dark es6 page. */
    int bad = 0, kept = 0;
    for (int i = 0; i < g_r->node_count; i++) {
        CwvNode *n = &g_r->nodes[i];
        /* Only PURE inherited text matters: buttons keep the UA black
           label (their own correct look), and explicit nodes keep
           whatever CSS/JS set.  A plain inherited node must still be
           the page's #f8fafc after every flip. */
        if (n->color_inherited_explicit && !n->is_button && !n->color_explicit) {
            /* JS may recolor a parent, in which case its children inherit
             * THAT explicit page color, not the original body color. */
            int valid = (n->color == 0xfffcfaf8);
            kept++;
            for (int j=i-1;j>=0 && !valid;j--)
                if(g_r->nodes[j].depth < n->depth &&
                   g_r->nodes[j].color_explicit &&
                   g_r->nodes[j].color==n->color) valid=1;
            if (!valid) {
                bad++;
                printf("    mis-tinted: node %d <%s id=%s> color=#%08x explicit=%d\n",
                       i, n->tag, n->id, n->color, n->color_explicit);
            }
        }
        if (n->id[0] && strcmp(n->id, "out") == 0 && n->color != 0xff99d334) bad++;
    }
    printf("theme-color check: inherited=%d mis-tinted=%d\n", kept, bad);
    if (bad || kept == 0) {
        printf("THEME REPRO: FAILURES (inherited colors re-tinted)\n");
        ops->shutdown(&b);
        return 1;
    }

    /* Repeated JS cssText writes to a parent must update inherited text
     * every time, without making the child itself explicit.  Child CSS
     * declarations, even when they match the old parent color, stay put. */
    for (int i = 0; i + 1 < g_r->node_count; i++) {
        CwvNode *parent=&g_r->nodes[i], *child=&g_r->nodes[i+1];
        if (strcmp(parent->id,"count") || child->depth <= parent->depth) continue;
        unsigned old=child->color;
        int explicit_before=child->color_explicit;
        style_apply_inline(g_r,parent,"padding: 2px");
        if (child->color!=old || child->color_explicit!=explicit_before) bad++;
        style_apply_inline(g_r,parent,"color: #ff0000");
        if (child->color!=0xff0000ffu || child->color_explicit) bad++;
        style_apply_inline(g_r,parent,"color: #00ff00");
        if (child->color!=0xff00ff00u || child->color_explicit) bad++;
        cwv_backend_set_theme(&b,1);
        if (child->color!=0xff00ff00u) bad++;
        printf("JS inherited color cycling: %s\n",bad?"FAIL":"PASS");
        break;
    }
    if (bad) { ops->shutdown(&b); return 1; }
    ops->shutdown(&b);
    printf("THEME REPRO: ALL PASS\n");
    return 0;
}
