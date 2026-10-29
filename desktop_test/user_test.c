/* Full-flow repro of the user's es2015_test.html + app.js (CRLF) through the
 * real backend + transformer, with the same hooks main.c uses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#include "core/cwv_renderer.h"
#include "web/js/modern/cwv_es2015_core.h"
#include "web/js/modern/cwv_es_libs.h"

static CwvRenderer *g_r;

static int hook_resolve(void *ud, const char *base, const char *spec,
                        char *out, size_t cap) {
    (void)ud;
    if (!base[0]) base = g_r->base_url;
    resolve_url(base, spec, out, cap);
    return 0;
}
static int hook_load_file(void *ud, const char *url, char *out, size_t cap) {
    unsigned char *d = NULL; size_t s = 0; (void)ud;
    if (load_resource(g_r, url, &d, &s) < 0) return -1;
    size_t n = s < cap - 1 ? s : cap - 1;
    memcpy(out, d, n); out[n] = 0; free(d);
    return (int)n;
}
static int hook_evaluate(void *ud, const char *js) { (void)ud; return js_eval(g_r, js); }
static const char *prelude_hook(void *ud) { (void)ud; return cwv_es_editions_prelude(); }
static int transform_hook(const char *in, char *out, size_t cap, void *ud) {
    VwEsHooks hooks;
    hooks.userdata = ud;
    hooks.resolve_url = hook_resolve;
    hooks.load_file = hook_load_file;
    hooks.evaluate = hook_evaluate;
    return vw_es2015_transform(in, out, cap, &hooks);
}

int main(void) {
    stub_set_log(stdout);
    CwvBackend be;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    memset(&be, 0, sizeof(be));
    ops->initialize(&be, 480, 272);
    g_r = (CwvRenderer *)be.userdata;
    js_set_source_transform(g_r, transform_hook, NULL);
    js_set_prelude_hook(g_r, prelude_hook, NULL);

    int rc = ops->load(&be, "file:///app0/es2015_test.html");
    printf("LOAD rc=%d\n", rc);

    char log[16384];
    js_console_dump(g_r, log, sizeof(log));
    printf("--- console ---\n%s----------------\n", log);
    printf("js_active=%d\n", cwv_backend_js_active(&be));
    const char *err = cwv_backend_js_last_error(&be);
    printf("last_error=[%s]\n", err ? err : "(none)");
    return 0;
}
