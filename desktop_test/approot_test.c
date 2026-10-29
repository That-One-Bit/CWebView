/* approot_test.c - unit gate for the per-console app content root
 * (cwv_platform_set_app_root / cwv_platform_url_root /
 *  cwv_platform_path_root, declared in cwv_platform.h).
 *
 * The engine's URL space was hardcoded to the Vita layout
 * (file:///app0 <-> app0:) until the user direction of 2026-09-23 (33):
 * app0: is a REAL Vita device and stays Vita-only, while the Wii and
 * Wii U load their content from the folder the PACKAGE sits in
 * (.../exampledir/s.rpx -> .../exampledir/web/).  This gate proves:
 *   1. the defaults keep the exact Vita behavior (nothing regressed),
 *   2. a custom root loads its own URLs AND rejects app0-style ones,
 *   3. site-rooted, relative, query-carrying and traversal hrefs stay
 *      inside the configured root,
 *   4. switching back restores the Vita layout.
 * The stub platform maps both "app0:/" and the test's Wii-style root
 * onto ./testdata, so both roots open the same fixture files.
 */
#include <stdio.h>
#include <string.h>

#include "core/cwv_backend.h"
#include "core/cwv_renderer.h"
#include "core/cwv_platform.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok: %s\n", (msg)); \
    else { printf("FAIL: %s\n", (msg)); fails++; } \
} while (0)

static int load_url(CwvBackend *b, const char *url)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", url);
    return b->ops->load(b, buf);
}

int main(void)
{
    CwvBackend b;
    char out[1024];

    /* ---- 1. defaults = the Vita layout, unchanged ------------------ */
    CHECK(strcmp(cwv_platform_url_root(), "file:///app0") == 0,
          "default url root is file:///app0");
    CHECK(strcmp(cwv_platform_path_root(), "app0:") == 0,
          "default path root is app0:");

    b.ops = cwv_backend_default_ops();
    CHECK(b.ops->initialize(&b, 960, 544) == 0, "backend initializes");
    CHECK(load_url(&b, "file:///app0/basic.html") == 0,
          "default root loads file:///app0/basic.html");

    /* ---- 2. the Wii-style root: folder the package sits in ---------- */
    /* .../exampledir/s.rpx -> .../exampledir/web/: the URL names the
     * folder, the physical path is the same folder. */
    cwv_platform_set_app_root("file:///sd:/apps/cwebview", "sd:/apps/cwebview");
    CHECK(strcmp(cwv_platform_url_root(), "file:///sd:/apps/cwebview") == 0,
          "custom url root active");
    CHECK(strcmp(cwv_platform_path_root(), "sd:/apps/cwebview") == 0,
          "custom path root active");

    CHECK(load_url(&b, "file:///sd:/apps/cwebview/basic.html") == 0,
          "package-folder root loads its own web content");
    CHECK(load_url(&b, "file:///app0/basic.html") ==
          CWV_BACKEND_NOT_IMPLEMENTED,
          "app0-style URLs are rejected outside Vita (app0 is Vita-only)");

    /* ---- 3. href resolution stays inside the configured root --------- */
    resolve_url("file:///sd:/apps/cwebview/web/page.html", "/style.css",
                out, sizeof(out));
    CHECK(strcmp(out, "file:///sd:/apps/cwebview/web/style.css") == 0,
          "site-rooted href resolves to <root>/web");
    resolve_url("file:///sd:/apps/cwebview/web/page.html", "img/a.png",
                out, sizeof(out));
    CHECK(strcmp(out, "file:///sd:/apps/cwebview/web/img/a.png") == 0,
          "relative href resolves against the page directory");
    resolve_url("file:///sd:/apps/cwebview/web/page.html", "a.png?v=2",
                out, sizeof(out));
    CHECK(strcmp(out, "file:///sd:/apps/cwebview/web/a.png") == 0,
          "query strings never reach the file layer");
    resolve_url("file:///sd:/apps/cwebview/web/page.html", "../outside.css",
                out, sizeof(out));
    CHECK(strcmp(out, "file:///sd:/apps/cwebview/outside.css") == 0,
          "../ climbs to the package folder but never out of it");

    /* ---- 4. switching back restores the Vita layout ------------------ */
    cwv_platform_set_app_root("file:///app0", "app0:");
    CHECK(load_url(&b, "file:///app0/basic.html") == 0,
          "restoring the default roots brings app0 loading back");
    CHECK(load_url(&b, "file:///sd:/apps/cwebview/basic.html") ==
          CWV_BACKEND_NOT_IMPLEMENTED,
          "the Vita layout rejects the Wii-style root");

    printf(fails ? "APPROOT GATE FAILED (%d)\n" : "APPROOT GATE PASSED\n",
           fails);
    return fails ? 1 : 0;
}
