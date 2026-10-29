/* Page-scoped image cache regression (2026-09-21 (29)):
 * 1. img_test.html's repeated pixel.png <img> tags -> shared decode,
 *    cache holds only the unique resolvable URLs.
 * 2. Navigating away frees the previous page's textures (no
 *    page-over-page GPU accumulation - the Vita GXM exhaustion).
 * 3. Shutdown frees the last page's cache: allocs == frees at exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int g_stub_tex_allocs, g_stub_tex_frees;
#include "core/cwv_renderer.h"
#include "core/cwv_backend.h"

static int fails = 0;
static void check(int cond, const char *what) {
    printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}

/* Invalid resources must never reach a native decoder/upload. */
static void reject_image(CwvRenderer *r, const unsigned char *data, size_t size,
                          const char *label) {
    const char *path = "testdata/image_invalid_fixture.png";
    FILE *f = fopen(path, "wb");
    if (!f) { check(0, "create invalid fixture"); return; }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if (written != size) { remove(path); check(0, "write fixture"); return; }
    int before = g_stub_tex_allocs;
    void *tex = load_image(r, "file:///app0/image_invalid_fixture.png");
    check(tex == NULL && g_stub_tex_allocs == before, label);
    remove(path);
}
static void invalid_image_tests(CwvRenderer *r) {
    unsigned char tiny[7] = {137,80,78,71,13,10,26};
    reject_image(r, tiny, 1, "one-byte local image rejected before decoder");
    reject_image(r, tiny, 7, "seven-byte local image rejected before decoder");
    unsigned char png[4096];
    FILE *f = fopen("testdata/pixel.png", "rb");
    if (!f) { check(0, "read PNG fixture"); return; }
    size_t n = fread(png, 1, sizeof(png), f);
    fclose(f);
    if (n < 45) { check(0, "complete PNG fixture"); return; }
    reject_image(r, png, 24, "truncated PNG IHDR rejected before decoder");
    reject_image(r, png, n - 5, "truncated PNG tail rejected before decoder");
    png[16] = 0; png[17] = 0; png[18] = 0x20; png[19] = 0;
    reject_image(r, png, n, "8192px PNG rejected before decode/upload");
    png[16] = 0; png[17] = 0; png[18] = 0; png[19] = 1;
    png[33] = 0x7f; png[34] = 0xff; png[35] = 0xff; png[36] = 0xff;
    reject_image(r, png, n, "out-of-bounds PNG chunk rejected before decoder");
}

int main(void) {
    CwvBackend b;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    memset(&b, 0, sizeof(b));
    ops->initialize(&b, 960, 544);
    CwvRenderer *r = (CwvRenderer *)b.userdata;

    invalid_image_tests(r);

    /* ---- 1. eleven <img> tags, three distinct URL spellings ---------- */
    int rc = ops->load(&b, "file:///app0/img_test.html");
    CwvNode *imgs[16]; int n_img = 0;
    for (int i = 0; i < r->node_count; i++)
        if (r->nodes[i].is_image) imgs[n_img++] = &r->nodes[i];
    printf("img_test: rc=%d img nodes=%d cache=%d allocs=%d\n",
           rc, n_img, r->img_cache_count, g_stub_tex_allocs);
    check(rc == 0, "img_test loads");
    check(n_img == 11, "all eleven <img> nodes parsed");
    /* URL-keyed like a per-document cache: pixel.png, ./pixel.png and
       pixel.png?pin=1 are three STRINGS -> up to 3 local entries; the
       http:// one fails in the stub (no network), missing.png fails. */
    check(r->img_cache_count >= 1 && r->img_cache_count <= 4,
          "cache holds only the unique resolvable URLs");
    int allocs_here = g_stub_tex_allocs;
    check(allocs_here >= 1 && allocs_here <= 4, "one decode per unique URL, not per tag");

    /* the plain pixel.png spellings must share ONE texture */
    void *plain = NULL; int shared_count = 0;
    for (int i = 0; i < n_img; i++) {
        if (strstr(r->nodes[i].src, "pixel.png") &&
            !strstr(r->nodes[i].src, "./") && !strstr(r->nodes[i].src, "?")) {
            if (!plain) plain = imgs[i]->image;
            if (imgs[i]->image == plain) shared_count++;
        }
    }
    check(shared_count >= 6, "identical-URL nodes share one texture");

    /* ---- 2. navigating away frees the page's textures ---------------- */
    ops->load(&b, "file:///app0/index.html");
    check(g_stub_tex_frees == allocs_here, "previous page's textures freed at navigation");
    printf("  (after nav: cache=%d allocs=%d frees=%d)\n",
           r->img_cache_count, g_stub_tex_allocs, g_stub_tex_frees);

    /* ---- 2b. repeated browsing cannot accumulate ---------------------- */
    for (int round = 0; round < 4; round++) {
        ops->load(&b, "file:///app0/img_test.html");
        ops->load(&b, "file:///app0/bgimage_test.html");
    }
    int net = g_stub_tex_allocs - g_stub_tex_frees;
    printf("  after 8 more navigations: allocs=%d frees=%d net-live=%d\n",
           g_stub_tex_allocs, g_stub_tex_frees, net);
    check(net <= 4, "browsing leaves no accumulating live textures");

    /* ---- 3. shutdown frees the rest ----------------------------------- */
    ops->shutdown(&b);
    printf("  after shutdown: allocs=%d frees=%d\n", g_stub_tex_allocs, g_stub_tex_frees);
    check(g_stub_tex_allocs == g_stub_tex_frees, "shutdown frees every texture (allocs == frees)");

    printf("%s\n", fails ? "IMG CACHE TEST: FAILURES" : "IMG CACHE TEST: ALL PASS");
    return fails ? 1 : 0;
}
