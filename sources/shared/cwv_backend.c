/* ============================================================================
 * FILE ORIENTATION: the painting + integration half of the web backend.
 * THIS is where draw functions live: draw_node walks the node tree built by
 * cwv_html.c and calls each tag's draw hook (registered in TAG_TABLE
 * over there); ALL text ends up on screen through the shared text painter
 * in this file - including button labels (the quarter-line pen offset that
 * keeps them vertically centered).  Input hit-testing, DPAD focus cycling
 * and the TAB-STOP REGISTRY are here too.  To change how a tag paints,
 * write the draw function HERE and point the tag's row at it.
 * ============================================================================ */

/* ============================================================================
 *  Vita WebView - renderer core + public API (cwv_backend.c)
 * ============================================================================
 *  The shared infrastructure of the native renderer:
 *
 *    SECTION A  small text & attribute helpers (shared with all files)
 *    SECTION B  resource loading: VPK files, curl, images, @font-face fonts,
 *              <link>/<style> CSS collection
 *    SECTION C  pointer input + DPAD focus + the <select> picker state
 *    SECTION D  viewport, scrolling, scrollbars
 *    SECTION E  the Vita IME (on-screen keyboard) + focus activation
 *    SECTION F  drawing: node chrome/content, select popups, scrollbars,
 *              the VCM modal, and the tag draw-hook dispatch
 *    SECTION G  the public CwvBackendOps API (what main.c calls)
 *
 *  The CSS engine lives in cwv_css.c, the HTML parser in
 *  cwv_html.c, and the JavaScript runtime in cwv_js.c.
 *  The shared data model lives in cwv_renderer.h.
 * ==========================================================================*/

#include "cwv_renderer.h"
#include "cwv_project.h"
#include "cwv_platform.h"
#include "cwv_cwi.h"

/* duktape: used only by the ESM self-test compile probe below. */
#include "sources/web/js/duktape.h"

/* ---- app content root (the engine's URL space; see cwv_platform.h) --- */
/* Defaults keep the Vita layout (app0:), so any build that never calls
 * cwv_platform_set_app_root behaves exactly as before. */
static char g_app_url_root[256] = "file:///app0";
static char g_app_path_root[256] = "app0:";

void cwv_platform_set_app_root(const char *url_root, const char *path_root)
{
    if (!url_root || !path_root || !*url_root || !*path_root) return;
    snprintf(g_app_url_root, sizeof(g_app_url_root), "%s", url_root);
    snprintf(g_app_path_root, sizeof(g_app_path_root), "%s", path_root);
}
const char *cwv_platform_url_root(void)   { return g_app_url_root; }
const char *cwv_platform_path_root(void) { return g_app_path_root; }

/* URL under the app root -> physical path under the path root
 * (query/fragment kept; callers that must not carry one strip it).
 * 0 = converted, -1 = outside the root or too long. */
static int app_url_to_path(const char *url, char *out, size_t cap)
{
    const char *url_root = cwv_platform_url_root();
    size_t rl = strlen(url_root);
    if (!url || strncmp(url, url_root, rl) != 0 || url[rl] != '/') return -1;
    if (snprintf(out, cap, "%s%s", cwv_platform_path_root(), url + rl) >= (int)cap)
        return -1;
    /* Percent-decode the local path like a browser (Chrome on a file:
       URL): "my%20icon.png" opens "my icon.png".  Only valid %XX escapes
       are rewritten; a stray '%' is copied verbatim (2026-10-05 (66)). */
    {
        static const char hexdig[] = "0123456789ABCDEF";
        char *w = out;
        for (const char *p = out; *p; p++) {
            if (p[0] == '%' && p[1] && p[2]) {
                const char *hi = strchr(hexdig, toupper((unsigned char)p[1]));
                const char *lo = strchr(hexdig, toupper((unsigned char)p[2]));
                if (hi && lo && w - out < (ptrdiff_t)cap - 1) {
                    *w++ = (char)(((hi - hexdig) << 4) | (lo - hexdig));
                    p += 2;
                    continue;
                }
            }
            if (w - out < (ptrdiff_t)cap - 1) *w++ = *p;
        }
        *w = 0;
    }
    return 0;
}


/* Cross-section forward declarations for statics used before their
 * definition below (C requires declaration before first use). */
static float renderer_content_origin_x(const CwvRenderer *r);
static float renderer_content_origin_y(const CwvRenderer *r);
static int renderer_textfield_open(CwvRenderer *r, int node_index);
static int select_option_count(const CwvRenderer *r, const CwvNode *select);
static CwvNode *select_option_at(CwvRenderer *r, CwvNode *select, int wanted);
static void select_set_index(CwvRenderer *r, CwvNode *select, int index);


/* ============================================================================
 * SECTION 2: SMALL TEXT & ATTRIBUTE HELPERS
 * ============================================================================
 * Low-level string utilities: case-insensitive compares, trimming, HTML
 * entity decoding and attribute lookups. These are used by both the CSS and
 * the HTML side of the parser.
 * ==========================================================================*/

/* Pack r,g,b (0-255) and a (0-255 alpha) into one 32-bit RGBA value. */
unsigned rgba(unsigned r, unsigned g, unsigned b, unsigned a) {
    return RGBA8(r, g, b, a);
}

/* CSS opacity compositing: blend fg toward the solid backdrop bg by
 * t (0..1).  t >= 1 returns fg EXACTLY, so the opacity code paths are
 * zero-cost for the default fully-opaque page.  The alpha byte comes
 * from fg (engine colors are opaque in practice). */
unsigned mix_rgba(unsigned bg, unsigned fg, float t) {
    unsigned br, bgg, bb, fr, fg2, fb;
    if (t >= 1.0f) return fg;
    if (t < 0.0f) t = 0.0f;
    /* RGBA8 layout: r = low byte, a = high byte (see cwv_platform.h) */
    br = bg & 255;          bgg = (bg >> 8) & 255;  bb = (bg >> 16) & 255;
    fr = fg & 255;          fg2 = (fg >> 8) & 255;  fb = (fg >> 16) & 255;
    return rgba((unsigned)(br + (fr - br) * t + 0.5f),
                (unsigned)(bgg + (fg2 - bgg) * t + 0.5f),
                (unsigned)(bb + (fb - bb) * t + 0.5f),
                (fg >> 24) & 255);
}

/* Case-insensitive string equality. Python: a.lower() == b.lower(). */
int ci_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Case-insensitive compare of the first n characters (like strncasecmp). */
int ci_ncmp(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char x = tolower((unsigned char)a[i]);
        unsigned char y = tolower((unsigned char)b[i]);
        if (x != y) return (int)x - (int)y;
        if (!x) return 0;
    }
    return 0;
}

/* One hex digit to 0-15, or -1 if not a hex digit. */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a CSS color string (#rgb, #rrggbb, or a small set of names) into an
 * RGBA8 value. Anything unparseable returns `fallback` unchanged. */
unsigned parse_color(const char *s, unsigned fallback) {
    int h[6];
    if (!s) return fallback;
    while (isspace((unsigned char)*s)) s++;

    if (*s == '#') {
        s++;
        if (strlen(s) >= 6) {
            int i;
            for (i = 0; i < 6; i++) h[i] = hex_digit(s[i]);
            if (h[0] >= 0 && h[1] >= 0 && h[2] >= 0 &&
                h[3] >= 0 && h[4] >= 0 && h[5] >= 0)
                return rgba(h[0]*16+h[1], h[2]*16+h[3], h[4]*16+h[5], 255);
        }
        if (strlen(s) >= 3) {
            int i;
            for (i = 0; i < 3; i++) h[i] = hex_digit(s[i]);
            if (h[0] >= 0 && h[1] >= 0 && h[2] >= 0)
                return rgba(h[0]*17, h[1]*17, h[2]*17, 255);
        }
    }

    /* Named colors (the small set this renderer supports). */
    if (ci_ncmp(s, "white", 5) == 0)        return rgba(255, 255, 255, 255);
    if (ci_ncmp(s, "black", 5) == 0)        return rgba(0, 0, 0, 255);
    if (ci_ncmp(s, "red", 3) == 0)          return rgba(255, 0, 0, 255);
    if (ci_ncmp(s, "green", 5) == 0)        return rgba(0, 128, 0, 255);
    if (ci_ncmp(s, "blue", 4) == 0)         return rgba(0, 0, 255, 255);
    if (ci_ncmp(s, "yellow", 6) == 0)       return rgba(255, 255, 0, 255);
    if (ci_ncmp(s, "orange", 6) == 0)       return rgba(255, 165, 0, 255);
    if (ci_ncmp(s, "purple", 6) == 0)       return rgba(128, 0, 128, 255);
    if (ci_ncmp(s, "gray", 4) == 0 || ci_ncmp(s, "grey", 4) == 0)
                                           return rgba(128, 128, 128, 255);
    if (ci_ncmp(s, "transparent", 11) == 0) return rgba(0, 0, 0, 0);
    return fallback;
}

/* Find an attribute inside one tag, e.g. find_attr(q, gt, "href", buf, size)
 * looks for href="..." / href='...' / href=bare within [start, end).
 * Returns NULL when the attribute is not present. */
const char *find_attr(const char *start, const char *end,
                              const char *name, char *out, size_t cap) {
    const char *p = start;
    size_t n = strlen(name);
    if (!out || !cap) return NULL;
    out[0] = 0;
    while (p && p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        /* Not at our attribute name? Jump to the next whitespace. */
        if (p + n > end || ci_ncmp(p, name, n) != 0) {
            while (p < end && !isspace((unsigned char)*p)) p++;
            continue;
        }
        p += n;
        /* The name must END here: a search for "href" must not stop at
           "hreflang" (and must keep scanning, not give up - the real
           href may come later in the same tag). */
        if (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' ||
                        *p == ':')) {
            while (p < end && !isspace((unsigned char)*p)) p++;
            continue;
        }
        while (p < end && isspace((unsigned char)*p)) p++;
        /* Boolean attributes ("selected", "disabled", ...) are present with
           no value at all.  Return the (empty) buffer so callers can tell
           "present" from "absent", exactly like HTML does. */
        if (p >= end || *p != '=') return out;
        p++;
        while (p < end && isspace((unsigned char)*p)) p++;
        /* Value may be quoted with " or ', or run to the next whitespace/>. */
        char qchar = (*p == '"' || *p == '\'') ? *p++ : 0;
        const char *q = p;
        while (q < end && ((qchar && *q != qchar) ||
                           (!qchar && !isspace((unsigned char)*q) && *q != '>')))
            q++;
        size_t len = (size_t)(q - p);
        if (len >= cap) len = cap - 1;
        memcpy(out, p, len);
        out[len] = 0;
        return out;
    }
    return NULL;
}

/* Strip whitespace from both ends of a NUL-terminated string, in place. */
void trim(char *s) {
    char *p = s;
    size_t n;
    if (!s) return;
    while (isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

/* Append `len` raw characters from the HTML source to `out`, decoding the
 * few HTML entities we support and normalizing whitespace. This is what
 * turns "a &amp; b\ntab" into "a & b tab" in node text. */
void append_text(char *out, size_t cap, const char *src, size_t len) {
    size_t u = strlen(out), i;
    for (i = 0; i < len && u + 1 < cap; i++) {
        unsigned char c = src[i];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c == '&') {
            if (i + 4 < len && !strncmp(src + i, "&lt;", 4))   { c = '<'; i += 3; }
            else if (i + 4 < len && !strncmp(src + i, "&gt;", 4))   { c = '>'; i += 3; }
            else if (i + 5 < len && !strncmp(src + i, "&amp;", 5))  { c = '&'; i += 4; }
            else if (i + 6 < len && !strncmp(src + i, "&quot;", 6)) { c = '"'; i += 5; }
        }
        out[u++] = (char)c;
    }
    out[u] = 0;
}

/* ============================================================================
 * SECTION 6: RESOURCES - FILES, URLS, IMAGES, FONTS
 * ============================================================================
 * Everything that gets the page's secondary data: reading files from the
 * app root (app0:/ on Vita; the package folder on Wii U and Wii),
 * resolving relative URLs against the page URL, optional
 * HTTP(S) fetches via libcurl, and loading images / @font-face fonts.
 * ==========================================================================*/

/* Read a whole file from app0:/ (the files inside the VPK). Returns 0 and a
 * heap buffer in *out on success; negative on error. */
static int load_file(const char *path, char **out) {
    long size = cwv_file_size(path);
    if (size < 0 || size > VW_MAX_HTML - 1) return -1;
    int fd = cwv_file_open_read(path);
    if (fd < 0) return -2;
    char *buf = malloc(size + 1);
    if (!buf) { cwv_file_close(fd); return -3; }
    int got = cwv_file_read(fd, buf, (int)size);
    cwv_file_close(fd);
    if (got < 0) { free(buf); return -4; }
    buf[got] = 0;
    *out = buf;
    return 0;
}

static int is_url(const char *s) {
    return s && (ci_ncmp(s, "http://", 7) == 0 || ci_ncmp(s, "https://", 8) == 0);
}

/* "file:///app0/dir/page.html" -> "file:///app0/dir/" (directory part) */
static void base_dir(const char *url, char *out, size_t cap) {
    /* Stop at a query OR a fragment: neither belongs to the file path,
     * and a page loaded as ".../page.html#top" must still resolve its
     * images against ".../", not against ".../page.html#top/" (a real
     * bug: every image on a fragment-URL page previously failed). */
    const char *q = strchr(url, '?');
    const char *h = strchr(url, '#');
    const char *cut = (q && (!h || q < h)) ? q : h;
    size_t n = cut ? (size_t)(cut - url) : strlen(url);
    if (n >= cap) n = cap - 1;
    memcpy(out, url, n);
    out[n] = 0;
    char *slash = strrchr(out, '/');
    if (slash) slash[1] = 0;
    else out[0] = 0;
}

/* Resolve a possibly-relative URL (href/src) against the page's base URL. */
void resolve_url(const char *base, const char *ref, char *out, size_t cap) {
    if (!ref || !*ref) { out[0] = 0; return; }
    if (is_url(ref)) { snprintf(out, cap, "%s", ref); return; }
    /* data: URLs are self-contained - never joined against the page
       directory (Chrome parity, 2026-10-05 (66)). */
    if (ci_ncmp(ref, "data:", 5) == 0) { snprintf(out, cap, "%s", ref); return; }
    if (ref[0] == '/')
        /* Site-rooted path: the site root is <app root>/web on every
         * console (app0:/web on Vita, the web/ folder next to the
         * package on Wii U and Wii). */
        snprintf(out, cap, "%s/web%s", cwv_platform_url_root(), ref);
    else {
        char dir[1024];
        base_dir(base, dir, sizeof(dir));
        size_t dl = strlen(dir);
        if (dl && dir[dl - 1] == '/') dir[--dl] = 0;    /* drop trailing / */
        const char *p = ref;
        /* Resolve ./ and ../ segments like a browser: ./ stays in the
           page's directory, ../ climbs one directory per segment and can
           never climb out of the site root. */
        for (;;) {
            if (p[0] == '.' && p[1] == '/') { p += 2; continue; }
            if (!(p[0] == '.' && p[1] == '.' &&
                  (p[2] == '/' || p[2] == 0)))
                break;                                  /* not a .. segment */
            char *colon = strstr(dir, "://");
            char *slash = strrchr(dir, '/');
            if (!colon || !slash || slash <= colon + 2) break;  /* at root */
            *slash = 0;
            p += 3;
        }
        snprintf(out, cap, "%s/%s", dir, p);             /* relative path */
    }
    /* Local file URLs never carry a query or fragment to the loader:
     * "pixel.png?v=2" and "cat.png#icon" read the plain file, exactly
     * like Chrome on a file: URL.  (Remote http(s) refs returned above
     * keep theirs - the query is part of the request there.) */
    if (ci_ncmp(out, "file:///", 8) == 0) {
        char *cut = strpbrk(out, "?#");
        if (cut) *cut = 0;
    }
}

/* Remote resources (images, @font-face fonts, linked CSS) are capped -
 * the console-native fetchers behind the seam need a caller-supplied
 * buffer up front, unlike libcurl's old realloc-as-you-go approach.
 * 4 MB matches the local-file image cap (VW_MAX_IMAGE_BYTES below). */
#define CWV_MAX_REMOTE_RESOURCE_BYTES (4 * 1024 * 1024)

/* Load any resource: app0:/ files first, then http(s) via the
 * platform seam (cwv_console_fetch_bin - see cwv_platform.h). */
int load_resource(CwvRenderer *r, const char *url,
                         unsigned char **data, size_t *size) {
    (void)r;
    if (!url) return -1;
    {
        char path[1024];
        if (app_url_to_path(url, path, sizeof(path)) == 0) {
        char *p = NULL;
        int rc = load_file(path, &p);
        if (rc < 0) return rc;
        *data = (unsigned char *)p;
        *size = (size_t)cwv_file_size(path);
        return 0;
        }
    }
    if (is_url(url)) {
        /* +1 for a trailing NUL: some callers (parse_css/parse_font_faces)
         * treat this buffer as C-string text, exactly as the old curl
         * path did by always NUL-terminating after the last byte written. */
        unsigned char *buf = malloc(CWV_MAX_REMOTE_RESOURCE_BYTES + 1);
        if (!buf) return -10;
        size_t got = 0;
        if (cwv_console_fetch_bin(url, buf, CWV_MAX_REMOTE_RESOURCE_BYTES,
                                  &got) < 0) {
            free(buf);
            return -11;
        }
        buf[got] = 0;
        *data = buf;
        *size = got;
        return 0;
    }
    return -12;
}

/* Load an image from any supported URL; format is picked by extension.
 * Returns NULL when the image cannot be loaded (the node keeps default size). */
/* Say WHY an image fell back to its alt-text placeholder, in the page
 * console (DevTools > Console) - this is how remote load failures become
 * diagnosable on hardware instead of a silent empty box. */
static void img_diag(CwvRenderer *r, const char *url, const char *why,
                     const unsigned char *data, size_t size) {
    char buf[420];   /* sized so the worst URL + reason never truncates */
    if (data && size >= 3) {
        /* Vita's printf lacks %zu: it printed the literal "zu" and
         * shifted every later argument, so the magic bytes read wrong
         * (user-reported 2026-10-01). Cast to unsigned long everywhere
         * a size_t reaches a log line on any port. */
        snprintf(buf, sizeof(buf), "image %.200s: %s (%lu bytes, magic %02x %02x %02x)",
                 url, why, (unsigned long)size, data[0], data[1], data[2]);
    } else {
        /* Local file failures say WHERE the loader actually looked, next
         * to the directory of the page doing the loading - one console
         * line answers "is the path wrong, or is the file missing from
         * the page's directory?" without any guesswork.  Both are shown
         * as Vita device paths (app0:/...). */
        /* "file:///app0/" is 13 characters: index 12 is the slash
         * itself, so slicing at +12 doubled it ("app0://file"). */
        char ppath[512], page[512];
        if (url && app_url_to_path(url, ppath, sizeof(ppath)) == 0) {
            snprintf(page, sizeof(page), "%s", "(none)");
            if (r && r->base_url[0] &&
                app_url_to_path(r->base_url, page, sizeof(page)) != 0)
                snprintf(page, sizeof(page), "%s", "(none)");
            snprintf(buf, sizeof(buf),
                     "image %.260s: %s [page dir: %.110s]",
                     ppath, why, page);
        } else {
            snprintf(buf, sizeof(buf), "image %.230s: %s", url, why);
        }
    }
    js_console_add_line(r, "error", buf);
    /* The console lives in the JS context; script-free pages have none,
     * so EVERY image diagnostic would be dropped on exactly the pages
     * most likely to be plain HTML with pictures.  Mirror to the app
     * runtime log (VWWV.log) - always, context or not. */
    if (r && r->runtime_log)
        r->runtime_log("IMG: %.380s", buf);
}

/* Images get their OWN file reader: pages cap at VW_MAX_HTML (256 KB, the
 * parser's contract) but a photo is often bigger, and load_file's cap made
 * every large local image silently fail.  Binary-exact read, 4 MB cap. */
#define VW_MAX_IMAGE_BYTES (4 * 1024 * 1024)
static int load_file_bin(const char *path, unsigned char **out, size_t *size) {
    long fsize = cwv_file_size(path);
    int fd;
    int got;
    if (fsize < 0) return -1;
    if (fsize <= 0 || (size_t)fsize > VW_MAX_IMAGE_BYTES) return -2;
    fd = cwv_file_open_read(path);
    if (fd < 0) return -3;
    unsigned char *buf = malloc((size_t)fsize);
    if (!buf) { cwv_file_close(fd); return -4; }
    got = cwv_file_read(fd, buf, (int)fsize);
    cwv_file_close(fd);
    if (got != (int)fsize) { free(buf); return -5; }
    *out = buf;
    *size = (size_t)got;
    return 0;
}

/* The bundled broken-image glyph, compiled directly into the binary
 * (see cwv_broken_image.c) so a failed <img> always has a REAL icon
 * to show - no dependency on the user's web/ directory. */
extern const unsigned char VW_BROKEN_IMAGE_PNG[];
extern const size_t VW_BROKEN_IMAGE_PNG_LEN;

/* Decoded once, lazily, the first time any image fails to load; reused
 * for every failure after that (one small texture, never freed - it
 * lives for the process, like the other engine-owned resources). */
static void *g_broken_image_tex;
static int g_broken_image_tried;
static void *broken_image_texture(void) {
    if (!g_broken_image_tex && !g_broken_image_tried) {
        g_broken_image_tried = 1;
        g_broken_image_tex = cwv_tex_load_png_buffer(VW_BROKEN_IMAGE_PNG,
                                                    VW_BROKEN_IMAGE_PNG_LEN);
    }
    return g_broken_image_tex;
}

/* Preflight PNG container boundaries before the native buffer loader.
 * This does not validate compressed pixels or guarantee GPU allocation. */
static unsigned long image_be32(const unsigned char *p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}
static const char *png_preflight(const unsigned char *data, size_t size,
                                 unsigned long *width, unsigned long *height) {
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    size_t pos = 8;
    int saw_idat = 0;
    if (size < 33 || memcmp(data, sig, 8) != 0)
        return "short or invalid PNG signature/header";
    if (image_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) != 0)
        return "invalid PNG IHDR";
    *width = image_be32(data + 16);
    *height = image_be32(data + 20);
    if (!*width || !*height || *width > 4096 || *height > 4096)
        return "PNG dimensions outside 1..4096 (rejected before decode)";
    while (pos < size) {
        unsigned long n;
        const unsigned char *type;
        if (size - pos < 12) return "truncated PNG chunk header/CRC";
        n = image_be32(data + pos);
        if (n > size - pos - 12) return "PNG chunk extends beyond input";
        type = data + pos + 4;
        if (pos != 8 && memcmp(type, "IHDR", 4) == 0)
            return "duplicate PNG IHDR";
        if (memcmp(type, "IDAT", 4) == 0) saw_idat = 1;
        if (memcmp(type, "IEND", 4) == 0)
            return n == 0 && saw_idat ? NULL : "invalid PNG IEND/missing IDAT";
        pos += (size_t)n + 12;
    }
    return "PNG missing IEND";
}

/* Decode a base64 payload in place.  Returns the decoded length or -1.
 * Whitespace is skipped, '=' padding ends the stream, and any byte outside
 * the alphabet aborts (fail the image rather than show garbage). */
static int base64_decode(const char *src, size_t len,
                         unsigned char *out, size_t cap) {
    static signed char tab[256];   /* 0 = unset: filled on first use */
    static int inited = 0;
    size_t o = 0;
    int acc = 0, bits = 0;
    if (!inited) {
        /* 0 = unset is ambiguous ('A' decodes to 0), so mark unused
           entries with -1 on the first call and never touch them again. */
        memset(tab, -1, sizeof(tab));
        const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) tab[(unsigned char)alpha[i]] = (signed char)i;
        inited = 1;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        int v;
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        v = tab[c];
        if (v < 0) return -1;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    return (int)o;
}

/* Parse a "data:[<mediatype>][;base64],<payload>" URL into a fresh buffer.
 * Only base64 payloads are supported (the toDataURL/inline-icon form);
 * returns 0 with *data and *size set, -1 on malformed input. */
static int data_url_decode(const char *url, unsigned char **data, size_t *size) {
    const char *p = url + 5;                     /* skip "data:" */
    int b64 = 0;
    while (*p && *p != ',') {
        if (ci_ncmp(p, ";base64", 7) == 0) { b64 = 1; p += 7; continue; }
        p++;
    }
    if (*p != ',') return -1;
    p++;                                         /* the payload */
    if (!b64) return -1;                         /* percent-encoded form: rare, unsupported */
    size_t len = strlen(p), cap = len / 4 * 3 + 4;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return -1;
    int n = base64_decode(p, len, buf, cap);
    if (n < 0) { free(buf); return -1; }
    *data = buf;
    *size = (size_t)n;
    return 0;
}

void *load_image(CwvRenderer *r, const char *url) {
    /* The renderer owns the decoded textures (see CwvRenderer.img_cache):
       a repeated URL within the page returns the SAME texture instead of
       re-decoding and re-uploading it (img_demo.html loads pixel.png six
       times -> one GPU texture), and every entry is freed as a group when
       the page is left (backend_load) or the backend shuts down. */
    if (!r || !url) return NULL;
    for (int i = 0; i < r->img_cache_count; i++)
        if (strcmp(r->img_cache[i].url, url) == 0)
            return r->img_cache[i].tex;

    unsigned char *data = NULL;
    size_t size = 0;
    /* Inline data: URLs (canvas toDataURL, hand-inlined icons) decode
       from their base64 payload - no file or network involved
       (Chrome parity, 2026-10-05 (66)). */
    if (ci_ncmp(url, "data:", 5) == 0) {
        if (data_url_decode(url, &data, &size) != 0) {
            img_diag(r, url, "malformed data: URL (base64 payloads supported)", NULL, 0);
            return NULL;
        }
    }
    /* Local files under the app root go through the image-sized binary
       reader; remote URLs keep the fetch path in load_resource. */
    else {
        char path[1024];
        if (app_url_to_path(url, path, sizeof(path)) == 0) {
            int rc = load_file_bin(path, &data, &size);
            if (rc < 0) {
                img_diag(r, url, rc == -2 ? "file over the 4 MB image cap"
                                          : "file read failed", NULL, 0);
                return NULL;
            }
        } else if (load_resource(r, url, &data, &size) < 0 || !data || size < 8) {
            img_diag(r, url, "fetch failed (missing file or bad URL)", NULL, 0);
            free(data);
            return NULL;
        }
    }
    /* Local input needs the same minimum-length guard as remote input. */
    if (!data || size < 8) {
        img_diag(r, url, "image shorter than 8 bytes", data, size);
        free(data);
        return NULL;
    }
    /* Refuse before decoding, not after an unnecessary GPU allocation. */
    if (r->img_cache_count >= CWV_IMG_CACHE) {
        img_diag(r, url, "over the per-page unique-image limit", NULL, 0);
        free(data);
        return NULL;
    }
    /* Pick the decoder from the file's MAGIC BYTES, not the URL extension:
       real web image URLs are often extensionless or carry a query string
       (.../logo?size=x), and a PNG mislabeled .jpg would fail to decode. */
    void *t = NULL;
    const char *raw_fmt = NULL;   /* (77) set when a non-CWI format decodes */
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        raw_fmt = "JPEG";
        t = cwv_tex_load_jpeg_buffer(data, (unsigned long)size);
    }
    else if (data[0] == 'B' && data[1] == 'M') {
        raw_fmt = "BMP";
        t = cwv_tex_load_bmp_buffer(data, (unsigned long)size);
    }
    else if (cwv_cwi_is(data, size)) {
        /* CWI, the lossless native container (2026-10-05 (71)): decoded
           by the shared decoder on every port - same magic-sniff rule
           as PNG/JPEG/BMP, and data: URIs flow through here too
           (data:image/x-cwi;base64,...). */
        if (r->runtime_log)
            r->runtime_log("IMG CWI decode begin: %.200s encoded=%lu",
                           url, (unsigned long)size);
        t = cwv_tex_load_cwi_buffer(data, (unsigned long)size);
        if (r->runtime_log)
            r->runtime_log("IMG CWI decode end: %.200s %s", url,
                            t ? "ok" : "failed");
    }
    else if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
             data[3] == 'G') {
        unsigned long width = 0, height = 0;
        raw_fmt = "PNG";
        const char *why = png_preflight(data, size, &width, &height);
        if (why) {
            img_diag(r, url, why, data, size);
            free(data);
            return NULL;
        }
        if (r->runtime_log)
            r->runtime_log("IMG PNG decode begin: %.200s %lux%lu encoded=%lu",
                           url, width, height, (unsigned long)size);
        t = cwv_tex_load_png_buffer(data, (unsigned long)size);
        /* The native API receives a complete PNG. Do not retry with its
         * signature removed: that is not a bounded or portable contract. */
        if (r->runtime_log)
            r->runtime_log("IMG PNG decode end: %.200s %s", url,
                           t ? "texture returned" : "failed");
    }
    /* (77) Compatibility note: raw formats stay fully supported on every
       port, but a console pays for the decode at page load. Say so once
       per image (the cache dedupes by URL): a console WARN (DevTools
       shows it where web devs look) mirrored to the runtime log for
       script-free pages, exactly the img_diag dual-surface pattern. */
    if (t && raw_fmt) {
        char nbuf[360];
        snprintf(nbuf, sizeof(nbuf),
                 "image %.200s decoded from %s at runtime - supported for "
                 "compatibility, but pack it as .cwi for pre-decoded "
                 "loads (tools/cwi_pack.py)", url, raw_fmt);
        js_console_add_line(r, "warn", nbuf);
        if (r->runtime_log)
            r->runtime_log("IMG NOTE: %.340s", nbuf);
    }
    if (t && (cwv_tex_width(t) > 4096 ||
              cwv_tex_height(t) > 4096)) {
        /* GXM textures cap at 4096px; the JPEG loader downscales itself,
           PNG does not - a huge PNG would upload garbage on hardware. */
        cwv_tex_free(t);
        t = NULL;
        img_diag(r, url, "over the 4096px GPU texture limit", data, size);
    }
    if (!t) {
        img_diag(r, url, "decode/upload failed (bad data, or GPU memory full)",
                 data, size);
        free(data);
        return NULL;   /* not cached: a retry (reload) gets a fresh decode */
    }
    free(data);
    /* Cache the decoded texture under the renderer's ownership.  The table
       holds 24 unique images per page - far beyond any authored page here -
       and a FULL table simply refuses the new decode (broken placeholder,
       img_diag logs it) instead of evicting: an evicted entry could still
       be referenced by an earlier <img> node, and freeing it would leave
       that node drawing through a dangling texture pointer.  Refusing is
       visible-but-safe; eviction after a page keeps GPU memory bounded
       either way. */
    if (r->img_cache_count >= CWV_IMG_CACHE) {
        img_diag(r, url, "over the per-page unique-image limit", NULL, 0);
        cwv_tex_free(t);
        return NULL;
    }
    int slot = r->img_cache_count++;
    snprintf(r->img_cache[slot].url, sizeof(r->img_cache[slot].url), "%s", url);
    r->img_cache[slot].tex = t;
    return t;
}

/* Load one @font-face family. Remote fonts are cached to a console-native
 * writable directory (see cwv_console_cache_dir) because some font
 * loaders can only open a real file, not an in-memory buffer. */
static int load_font(CwvRenderer *r, const char *family, const char *src) {
    if (r->font_count >= VW_MAX_FONTS) return -1;
    char resolved[1024];
    resolve_url(r->base_url, src, resolved, sizeof(resolved));
    unsigned char *data = NULL;
    size_t size = 0;
    if (load_resource(r, resolved, &data, &size) < 0) return -2;
    void *font = NULL;

    /* file:/// resources are inside the VPK. HTTP(S) fonts are cached
       first so both local and remote fonts use cwv_font_load_file. */
    char font_path[1024];
    if (is_url(resolved)) {
        unsigned hash = 2166136261u;
        for (const char *h = resolved; *h; h++) hash = (hash ^ (unsigned char)*h) * 16777619u;
        char cache_dir[256];
        if (cwv_console_cache_dir(cache_dir, sizeof(cache_dir)) < 0) { free(data); return -3; }
        snprintf(font_path, sizeof(font_path), "%s/font-%08X.ttf", cache_dir, hash);
        if (cwv_file_write_all(font_path, data, size) != 0) { free(data); return -3; }
        free(data);
    } else {
        snprintf(font_path, sizeof(font_path), "app0:/%s", resolved + 12);
        free(data);
    }
    font = cwv_font_load_file(font_path);
    if (!font) return -3;
    CwvFont *f = &r->fonts[r->font_count++];
    snprintf(f->family, sizeof(f->family), "%s", family);
    snprintf(f->src, sizeof(f->src), "%s", resolved);
    f->font = font;
    return 0;
}

/* Look up a loaded @font-face font by family name. */
void *font_for(CwvRenderer *r, const char *family) {
    int i;
    if (family && *family)
        for (i = 0; i < r->font_count; i++)
            if (ci_eq(r->fonts[i].family, family)) return r->fonts[i].font;
    return r->system_font;
}

/* Collect @font-face blocks from CSS text and load each font.
 * This is a tiny hand parser for "font-family: X; src: url(Y)" pairs - it is
 * deliberately not a general CSS parser. */
static void parse_font_faces(CwvRenderer *r, const char *css) {
    const char *p = css;
    while ((p = strstr(p, "@font-face"))) {
        const char *a = strchr(p, '{'), *b = a ? strchr(a, '}') : NULL;
        if (!a || !b) break;
        char block[2048];
        size_t n = (size_t)(b - a - 1);
        if (n >= sizeof(block)) n = sizeof(block) - 1;
        memcpy(block, a + 1, n);
        block[n] = 0;

        /* font-family: quoted-or-bare-name (stop at ';') */
        char fam[96] = {0}, src[1024] = {0};
        char *q = strstr(block, "font-family");
        if (q) {
            q = strchr(q, ':');
            if (q) {
                q++;
                while (isspace((unsigned char)*q)) q++;
                if (*q == '"' || *q == '\'') {
                    char qc = *q++;
                    char *e = strchr(q, qc);
                    if (e) {
                        size_t l = (size_t)(e - q);
                        if (l >= sizeof(fam)) l = sizeof(fam) - 1;
                        memcpy(fam, q, l);
                        fam[l] = 0;
                    }
                } else {
                    sscanf(q, "%95[^;]", fam);
                }
                trim(fam);
            }
        }
        /* src: url(...) - first url wins, quotes/space trimmed. */
        q = strstr(block, "src");
        if (q) {
            q = strchr(q, ':');
            if (q) {
                q++;
                char *u = strstr(q, "url(");
                if (u) {
                    u += 4;
                    while (*u == ' ' || *u == '\'' || *u == '"') u++;
                    char *e = u;
                    while (*e && *e != ')' && *e != '\'' && *e != '"') e++;
                    size_t l = (size_t)(e - u);
                    if (l >= sizeof(src)) l = sizeof(src) - 1;
                    memcpy(src, u, l);
                    src[l] = 0;
                }
            }
        }
        if (fam[0] && src[0]) load_font(r, fam, src);
        p = b + 1;
    }
}

/* Find <link rel="stylesheet" href="..."> tags in the HTML and load each
 * referenced CSS file (fonts first, then rules). */
static void load_external_css(CwvRenderer *r, const char *html) {
    const char *p = html;
    while ((p = strstr(p, "<link"))) {
        const char *gt = strchr(p, '>');
        if (!gt) break;
        char rel[64], href[1024];
        if (find_attr(p + 5, gt, "rel", rel, sizeof(rel)) &&
            ci_ncmp(rel, "stylesheet", 10) == 0 &&
            find_attr(p + 5, gt, "href", href, sizeof(href))) {
            char u[1024];
            resolve_url(r->base_url, href, u, sizeof(u));
            unsigned char *d = NULL;
            size_t sz = 0;
            if (load_resource(r, u, &d, &sz) == 0) {
                parse_font_faces(r, (char *)d);
                parse_css(r, (char *)d);
                free(d);
            }
        }
        p = gt + 1;
    }
}

/* Find <style>...</style> blocks in the HTML and parse each one's CSS. */
static void parse_inline_styles(CwvRenderer *r, const char *html) {
    const char *p = html;
    while ((p = strstr(p, "<style"))) {
        const char *gt = strchr(p, '>');
        if (!gt) break;
        const char *e = strstr(gt + 1, "</style>");
        if (!e) break;
        size_t n = (size_t)(e - gt - 1);
        char *css = malloc(n + 1);
        if (!css) break;
        memcpy(css, gt + 1, n);
        css[n] = 0;
        parse_font_faces(r, css);
        parse_css(r, css);
        free(css);
        p = e + 8;
    }
}

/* ============================================================================
 * SECTION 9: POINTER INPUT AND FOCUS
 * ============================================================================
 * Touch/pointer handling (tapping links, buttons, textfields and the native
 * <select> picker) plus DPAD focus cycling with browser-like tabindex order.
 * ==========================================================================*/

/* ---- per-element scroll regions (2026-10-06 (75)) ----------------------
 * A box with EXPLICIT overflow:auto/scroll whose content extends past
 * its borders becomes a scroll REGION: it joins the DPAD/Tab focus
 * chain, and while focused it consumes the platform scroll input
 * (stick, keys, wheel) before the page.  Leftover delta past the
 * region's bounds falls through to the page, exactly like nested
 * scroll containers in a browser.
 *
 * This one pass bakes everything paint and input need, and is cheap
 * enough (two linear sweeps) to run at draw/input/focus time:
 *   1. subtree extents (reversed depth-stack sweep, O(n));
 *   2. region classification + live-offset clamping;
 *   3. per node: accumulated ancestor scroll offsets and the
 *      document-space visibility window (finite only inside regions).
 * ------------------------------------------------------------------------ */
#define CWV_REGION_STACK 64

static void renderer_update_scroll_regions(CwvRenderer *r) {
    if (!r || r->node_count <= 0) return;
    int n = r->node_count, i;

    /* --- 1: subtree extents (reversed pre-order sweep) -------------------
     * The flat node list is a pre-order traversal, so a node's subtree
     * is the contiguous run of deeper nodes that follow it.  Popping
     * every deeper stack entry at each node yields its descendants'
     * combined extent in one pass. */
    float ext_x1[VW_MAX_NODES], ext_y1[VW_MAX_NODES];
    struct { int depth; float x1, y1; } st[CWV_REGION_STACK];
    int st_n = 0;
    for (i = n - 1; i >= 0; i--) {
        CwvNode *nd = &r->nodes[i];
        float x1 = nd->x + nd->w, y1 = nd->y + nd->h;
        while (st_n > 0 && st[st_n - 1].depth > nd->depth) {
            if (st[st_n - 1].x1 > x1) x1 = st[st_n - 1].x1;
            if (st[st_n - 1].y1 > y1) y1 = st[st_n - 1].y1;
            st_n--;
        }
        ext_x1[i] = x1; ext_y1[i] = y1;
        if (st_n < CWV_REGION_STACK) {
            st[st_n].depth = nd->depth;
            st[st_n].x1 = x1; st[st_n].y1 = y1;
            st_n++;
        }
    }

    /* --- 2: region classification + clamping ----------------------------
     * Only EXPLICIT overflow:auto/scroll boxes count - the inherited
     * "auto" default (which is what makes the PAGE scrollable) must not
     * turn every box into a region.  A region that cannot actually
     * scroll on any axis stays out of the focus chain. */
    for (i = 0; i < n; i++) {
        CwvNode *nd = &r->nodes[i];
        nd->scroll_region = 0;
        if (!nd->overflow_explicit || nd->display_none) continue;
        if (nd->overflow_y != 0 && nd->overflow_y != 3 &&
            nd->overflow_x != 0 && nd->overflow_x != 3) continue;
        float max_y = (nd->overflow_y == 0 || nd->overflow_y == 3)
            ? ext_y1[i] - (nd->y + nd->h) : 0.0f;
        float max_x = (nd->overflow_x == 0 || nd->overflow_x == 3)
            ? ext_x1[i] - (nd->x + nd->w) : 0.0f;
        if (max_y < 0.0f) max_y = 0.0f;
        if (max_x < 0.0f) max_x = 0.0f;
        nd->scroll_max_y = max_y;
        nd->scroll_max_x = max_x;
        nd->scroll_region = (max_y > 0.5f || max_x > 0.5f) ? 1 : 0;
        if (!nd->scroll_region) {
            nd->scroll_top = 0.0f;
            nd->scroll_left = 0.0f;
        } else {
            if (nd->scroll_top > max_y) nd->scroll_top = max_y;
            if (nd->scroll_left > max_x) nd->scroll_left = max_x;
        }
    }

    /* --- 3: bake offsets + visibility windows (document order) ----------
     * The window is expressed in the child's own document coordinates:
     * a child c paints at  (c.x - origin)*z - page_scroll - off  where
     * off already contains every ancestor region's live scroll offset,
     * and it is visible inside region R iff  c.x >= R.x + off  (the
     * region's box stays put while its content shifts). */
    struct { int depth, has_clip;
             float offx, offy, wx0, wy0, wx1, wy1; } st2[CWV_REGION_STACK];
    int st2_n = 0;
    st2[0].depth = -1; st2[0].has_clip = 0;
    st2[0].offx = 0.0f; st2[0].offy = 0.0f;
    st2_n = 1;
    for (i = 0; i < n; i++) {
        CwvNode *nd = &r->nodes[i];
        while (st2_n > 1 && st2[st2_n - 1].depth >= nd->depth) st2_n--;
        int top = st2_n - 1;
        nd->scroll_off_x = st2[top].offx;
        nd->scroll_off_y = st2[top].offy;
        nd->has_clip = st2[top].has_clip;
        nd->clip_x0 = st2[top].wx0; nd->clip_y0 = st2[top].wy0;
        nd->clip_x1 = st2[top].wx1; nd->clip_y1 = st2[top].wy1;
        if (nd->scroll_region && st2_n < CWV_REGION_STACK) {
            float offx = st2[top].offx + nd->scroll_left;
            float offy = st2[top].offy + nd->scroll_top;
            float wx0 = nd->x + offx, wy0 = nd->y + offy;
            float wx1 = wx0 + nd->w, wy1 = wy0 + nd->h;
            if (st2[top].has_clip) {
                if (wx0 < st2[top].wx0) wx0 = st2[top].wx0;
                if (wy0 < st2[top].wy0) wy0 = st2[top].wy0;
                if (wx1 > st2[top].wx1) wx1 = st2[top].wx1;
                if (wy1 > st2[top].wy1) wy1 = st2[top].wy1;
            }
            st2[st2_n].depth = nd->depth;
            st2[st2_n].has_clip = 1;
            st2[st2_n].offx = offx; st2[st2_n].offy = offy;
            st2[st2_n].wx0 = wx0; st2[st2_n].wy0 = wy0;
            st2[st2_n].wx1 = wx1; st2[st2_n].wy1 = wy1;
            st2_n++;
        }
    }
}

/* Reveal the node at `idx` inside its ancestor scroll regions (Chrome
 * scrolls a focused control into view within its scroll container; the
 * PAGE itself keeps its existing non-revealing behaviour).  Call after
 * focus changes. */
static void renderer_reveal_in_regions(CwvRenderer *r, int idx) {
    if (!r || idx < 0 || idx >= r->node_count) return;
    int depth = r->nodes[idx].depth;
    float top = r->nodes[idx].y, bot = top + r->nodes[idx].h;
    int j;
    for (j = idx - 1; j >= 0 && depth > 0; j--) {
        if (r->nodes[j].depth >= depth) continue;
        depth = r->nodes[j].depth;      /* nodes[j] is an ancestor */
        CwvNode *a = &r->nodes[j];
        if (!a->scroll_region) continue;
        /* child coord c is visible vertically iff
           c.y >= a->y + a->scroll_top && c.y + h <= a->y + st + a->h */
        float wy0 = a->y + a->scroll_top, wy1 = wy0 + a->h;
        if (top < wy0) {
            a->scroll_top = top - a->y;
        } else if (bot > wy1) {
            a->scroll_top = bot - a->y - a->h;
        }
        if (a->scroll_top < 0.0f) a->scroll_top = 0.0f;
        if (a->scroll_top > a->scroll_max_y) a->scroll_top = a->scroll_max_y;
    }
}

/* (84) Chrome-style scroll dispatch: the NEAREST scroll region in the
 * ancestor chain of `idx` (the region itself when idx is one) absorbs the
 * delta first, then each outer region; only the leftover reaches the page.
 * A selected element never drags the whole page along before its own
 * container is fully scrolled (user report 84: the page used to scroll
 * immediately whenever focus sat on a child inside the region). */
static float renderer_regions_absorb_y(CwvRenderer *r, int idx, float delta) {
    int depth, j;
    if (!r || idx < 0 || idx >= r->node_count) return delta;
    depth = r->nodes[idx].depth;
    /* the node itself, when it is a region, consumes before its ancestors */
    if (r->nodes[idx].scroll_region && r->nodes[idx].scroll_max_y > 0.5f) {
        CwvNode *f = &r->nodes[idx];
        float before = f->scroll_top, t = before + delta;
        if (t < 0.0f) t = 0.0f;
        if (t > f->scroll_max_y) t = f->scroll_max_y;
        f->scroll_top = t;
        delta -= (t - before);
        if (delta == 0.0f) return 0.0f;
    }
    for (j = idx - 1; j >= 0 && depth > 0; j--) {
        if (r->nodes[j].depth >= depth) continue;
        depth = r->nodes[j].depth;      /* nodes[j] is an ancestor */
        CwvNode *a = &r->nodes[j];
        if (!a->scroll_region || a->scroll_max_y <= 0.5f) continue;
        float before = a->scroll_top, t = before + delta;
        if (t < 0.0f) t = 0.0f;
        if (t > a->scroll_max_y) t = a->scroll_max_y;
        a->scroll_top = t;
        delta -= (t - before);
        if (delta == 0.0f) return 0.0f;
    }
    return delta;
}

/* Same chain on the horizontal axis (mirrors renderer_regions_absorb_y). */
static float renderer_regions_absorb_x(CwvRenderer *r, int idx, float delta) {
    int depth, j;
    if (!r || idx < 0 || idx >= r->node_count) return delta;
    depth = r->nodes[idx].depth;
    if (r->nodes[idx].scroll_region && r->nodes[idx].scroll_max_x > 0.5f) {
        CwvNode *f = &r->nodes[idx];
        float before = f->scroll_left, t = before + delta;
        if (t < 0.0f) t = 0.0f;
        if (t > f->scroll_max_x) t = f->scroll_max_x;
        f->scroll_left = t;
        delta -= (t - before);
        if (delta == 0.0f) return 0.0f;
    }
    for (j = idx - 1; j >= 0 && depth > 0; j--) {
        if (r->nodes[j].depth >= depth) continue;
        depth = r->nodes[j].depth;
        CwvNode *a = &r->nodes[j];
        if (!a->scroll_region || a->scroll_max_x <= 0.5f) continue;
        float before = a->scroll_left, t = before + delta;
        if (t < 0.0f) t = 0.0f;
        if (t > a->scroll_max_x) t = a->scroll_max_x;
        a->scroll_left = t;
        delta -= (t - before);
        if (delta == 0.0f) return 0.0f;
    }
    return delta;
}

/* (84) deepest scroll region whose on-screen box contains the point
 * (touch/pointer drag dispatch: the region under the finger scrolls
 * first, the page only takes the leftover). */
static int renderer_region_at(CwvRenderer *r, int x, int y) {
    int i, best = -1, best_depth = -1;
    if (!r) return -1;
    float z = r->zoom > 0.01f ? r->zoom : 1.0f;
    float ox = renderer_content_origin_x(r), oy = renderer_content_origin_y(r);
    for (i = 0; i < r->node_count; i++) {
        CwvNode *n = &r->nodes[i];
        if (!n->scroll_region || n->display_none || n->visibility_hidden) continue;
        float sx = (n->x - ox) * z - r->scroll_x - n->scroll_off_x * z;
        float sy = (n->y - oy) * z - r->scroll_y - n->scroll_off_y * z;
        if ((float)x < sx || (float)x > sx + n->w * z) continue;
        if ((float)y < sy || (float)y > sy + n->h * z) continue;
        if (n->depth > best_depth) { best = i; best_depth = n->depth; }
    }
    return best;
}

/* Is the screen point (x,y) inside this node's box?  Accounts for zoom and
 * scroll: converts a screen point into document coordinates first. */
static int node_hit(const CwvRenderer *r, const CwvNode *n, int x, int y) {
    if (!r || !n || n->display_none || n->visibility_hidden) return 0;
    float z = r->zoom > 0.01f ? r->zoom : 1.0f;
    float origin_x = renderer_content_origin_x(r), origin_y = renderer_content_origin_y(r);
    /* (75) per-element scroll regions: nodes inside a region are shifted
     * by the accumulated ancestor offsets, and a point outside the
     * region's visibility window cannot hit scrolled-away content.
     * position:fixed ignores both, exactly like it ignores the page. */
    if (n->pos_kind == 3) {
        float ux = origin_x + (float)x / z;
        float uy = origin_y + (float)y / z;
        return ux >= n->x && ux <= n->x + n->w && uy >= n->y && uy <= n->y + n->h;
    }
    float ux = origin_x + (r->scroll_x + (float)x) / z + n->scroll_off_x;
    float uy = origin_y + (r->scroll_y + (float)y) / z + n->scroll_off_y;
    if (n->has_clip &&
        (ux < n->clip_x0 || ux > n->clip_x1 ||
         uy < n->clip_y0 || uy > n->clip_y1)) return 0;
    return ux >= n->x && ux <= n->x + n->w && uy >= n->y && uy <= n->y + n->h;
}

/* CSS cursor lookup (2026-09-26 (45)): what cursor style belongs at
 * the screen point (x,y)?  Walks the node list topmost-first (the same
 * reverse-document order the tap hit-tests use), takes the first hit
 * node, and resolves its cursor:
 *   - an explicit CSS cursor (inherited values are baked into child
 *     nodes at parse time, exactly like color) wins;
 *   - AUTO falls back to the browser UA defaults: links, buttons and
 *     selects get POINTER, textfields get TEXT;
 *   - URL styles carry the engine-loaded art + its CSS hotspot.
 * Returns 1 with *out filled when a style applies, 0 for plain AUTO. */
static int backend_cursor_at(CwvBackend *backend, int x, int y,
                             CwvCursorInfo *out)
{
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    int i;
    renderer_update_scroll_regions(r);   /* (75): hit tests need offsets */
    if (out) { out->style = CWV_CSTYLE_AUTO; out->img = NULL;
               out->hot_x = 0; out->hot_y = 0; }
    if (!r || !out) return 0;
    for (i = r->node_count - 1; i >= 0; --i) {
        CwvNode *n = &r->nodes[i];
        if (!node_hit(r, n, x, y)) continue;
        if (n->cursor_style == CWV_CSTYLE_URL && n->cursor_img) {
            out->style = CWV_CSTYLE_URL;
            out->img = n->cursor_img;
            out->hot_x = n->cursor_hot_x;
            out->hot_y = n->cursor_hot_y;
            return 1;
        }
        if (n->cursor_style != CWV_CSTYLE_AUTO) {
            out->style = n->cursor_style;
            return 1;
        }
        /* UA defaults, like a browser: pointer over links/buttons/
           selects, text beam over textfields. */
        if (n->is_link || n->is_button || n->is_select) {
            out->style = CWV_CSTYLE_POINTER;
            return 1;
        }
        if (n->is_textfield) {
            out->style = CWV_CSTYLE_TEXT;
            return 1;
        }
        /* topmost hit node decides, like a browser */
        return 0;
    }
    return 0;
}

/* One pointer sample (down=1 press, down=0 release).  Returns 1 when the
 * event was consumed by a control. */
static int backend_pointer_event(CwvBackend *backend, int x, int y, int down) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return 0;
    renderer_update_scroll_regions(r);   /* (75): hit tests need offsets */

    if (down) {
        /* An opened select owns pointer input until an option is chosen. */
        for (int si = 0; si < r->node_count; si++) {
            CwvNode *sel = &r->nodes[si];
            if (!sel->is_select || !sel->select_open) continue;
            int count = select_option_count(r, sel);
            if (count <= 0) { sel->select_open = 0; break; }
            float z = r->zoom > 0.01f ? r->zoom : 1.0f;
            float control_x = (sel->x - renderer_content_origin_x(r)) * z - r->scroll_x;
            float control_y = (sel->y - renderer_content_origin_y(r)) * z - r->scroll_y;
            float sx = control_x;
            float sw = sel->w * z; if (sw < 140.0f) sw = 140.0f;
            float row = 28.0f;
            float sh = row * (count > 8 ? 8 : count);
            float below_y = control_y + sel->h * z;
            float above_y = control_y - sh;
            float sy = (below_y + sh <= r->height - 4.0f) ? below_y : above_y;
            if (sx < 4.0f) sx = 4.0f;
            if (sx + sw > r->width - 4.0f) sx = (float)r->width - sw - 4.0f;
            if (sy < 4.0f) sy = 4.0f;
            if (x >= sx && x <= sx + sw && y >= sy && y <= sy + sh) {
                int idx = (int)((y - sy) / row);
                if (idx < 0) idx = 0;
                if (idx >= count) idx = count - 1;
                /* Pointer samples are also used for touch-move updates.  Keep
                   the picker open while the finger is down and only commit on
                   release once the finger has actually entered an option. */
                sel->select_hover_index = idx;
                sel->select_touch_moved_to_option = 1;
                sel->select_touch_started_on_control = 0;
                r->focus_node = si;
                return 1;
            }
            /* If the finger started on the closed select, the first release is
               the equivalent of a normal tap-to-open.  Do not immediately
               select the current row and close the popup.  Once the popup is
               already open, tapping outside it dismisses it. */
            if (sel->select_touch_started_on_control) {
                return 1;
            }
            return 1;
        }
        /* Tap on a textfield opens the Vita IME (on-screen keyboard). */
        for (int i = r->node_count - 1; i >= 0; --i) {
            CwvNode *n = &r->nodes[i];
            if (n->is_textfield && node_hit(r, n, x, y)) {
                r->focus_node = i;
                renderer_reveal_in_regions(r, i);   /* (75) */
                renderer_textfield_open(r, i);
                return 1;
            }
        }
        /* Tap on a closed select opens the native picker. */
        for (int i = r->node_count - 1; i >= 0; --i) {
            CwvNode *n = &r->nodes[i];
            if (n->is_select && node_hit(r, n, x, y)) {
                r->focus_node = i;
                renderer_reveal_in_regions(r, i);   /* (75) */
                n->select_open = 1;
                n->select_hover_index = n->select_index;
                n->select_touch_started_on_control = 1;
                n->select_touch_moved_to_option = 0;
                return 1;
            }
        }
        /* Buttons press down as a group (the element + its label nodes). */
        int group = 0;
        for (int i = r->node_count - 1; i >= 0; --i) {
            CwvNode *n = &r->nodes[i];
            if (n->is_button && n->has_background && node_hit(r, n, x, y)) {
                group = n->button_group;
                break;
            }
        }
        if (!group) {
            for (int i = r->node_count - 1; i >= 0; --i) {
                CwvNode *n = &r->nodes[i];
                if (n->is_button && node_hit(r, n, x, y)) { group = n->button_group; break; }
            }
        }
        if (group) {
            for (int i = 0; i < r->node_count; i++)
                if (r->nodes[i].is_button && r->nodes[i].button_group == group)
                    r->nodes[i].button_pressed = 1;
            return 1;
        }
        return 0;
    }

    /* Commit an opened select on touch release.  The hover row is deliberately
       separate from select_index so the option can show its darkened hover
       state while the finger moves over it. */
    for (int si = 0; si < r->node_count; si++) {
        CwvNode *sel = &r->nodes[si];
        if (!sel->is_select || !sel->select_open) continue;
        if (!down) {
            int count = select_option_count(r, sel);
            if (sel->select_touch_started_on_control) {
                /* First tap opens the picker and intentionally leaves it open. */
                sel->select_touch_started_on_control = 0;
                sel->select_touch_moved_to_option = 0;
                r->focus_node = si;
                return 1;
            }
            if (sel->select_touch_moved_to_option && count > 0 &&
                sel->select_hover_index >= 0 && sel->select_hover_index < count)
                select_set_index(r, sel, sel->select_hover_index);
            sel->select_open = 0;
            sel->select_touch_moved_to_option = 0;
            r->focus_node = si;
            return 1;
        }
        return 1;
    }

    /* Release on a pressed button: navigate when the button is also a link. */
    for (int i = r->node_count - 1; i >= 0; --i) {
        CwvNode *n = &r->nodes[i];
        if (!n->is_button || !n->button_pressed) continue;
        int group = n->button_group;
        int inside = 0;
        for (int j = 0; j < r->node_count; j++) {
            CwvNode *b = &r->nodes[j];
            if (b->is_button && b->button_group == group && b->has_background &&
                node_hit(r, b, x, y)) {
                inside = 1;
                break;
            }
        }
        for (int j = 0; j < r->node_count; j++)
            if (r->nodes[j].is_button && r->nodes[j].button_group == group)
                r->nodes[j].button_pressed = 0;
        if (inside && js_has_listener(r, i)) {
            /* Chrome order: listeners run first, and the default action
               (navigation) still happens unless the handler called
               event.preventDefault(). */
            if (js_notify_activate(r, i)) return 1;
        }
        if (inside && n->is_link && n->href[0]) {
            snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", n->href);
            snprintf(r->pending_target, sizeof(r->pending_target), "%s",
                     n->link_target[0] ? n->link_target : "app");
            return 1;
        }
        return 1;
    }

    /* Release on a plain link navigates. */
    for (int i = r->node_count - 1; i >= 0; --i) {
        CwvNode *n = &r->nodes[i];
        if (!n->is_link || n->is_button || !n->href[0]) continue;
        if (node_hit(r, n, x, y)) {
            /* Chrome order: click listeners run, THEN navigation happens -
               a listener only cancels it with event.preventDefault(). */
            if (js_notify_activate(r, i)) return 1;
            if (n->href[0] == '#') return 1;   /* fragment: click only */
            snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", n->href);
            snprintf(r->pending_target, sizeof(r->pending_target), "%s",
                     n->link_target[0] ? n->link_target : "app");
            return 1;
        }
    }
    /* Chrome fires "click" on ANY element with a listener - <div>, <span>,
       <img> widgets (color pickers, canvas tools) depend on it.  Buttons
       and links were handled above; this pass covers everything else. */
    for (int i = r->node_count - 1; i >= 0; --i) {
        CwvNode *n = &r->nodes[i];
        if (n->is_button || n->is_link || n->display_none) continue;
        if (!node_hit(r, n, x, y)) continue;
        if (js_has_listener(r, i)) {
            js_notify_activate(r, i);
            return 1;
        }
    }
    return 0;
}

/* Cancel all transient input state (finger lifted off everything etc.). */
static void backend_pointer_cancel(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return;
    for (int i = 0; i < r->node_count; i++) {
        r->nodes[i].button_pressed = 0;
        r->nodes[i].select_touch_started_on_control = 0;
        r->nodes[i].select_touch_moved_to_option = 0;
    }
    r->pending_navigation[0] = 0;
    r->pending_target[0] = 0;
}

/* ==========================================================================
 * THE TAB-STOP REGISTRY (the input contract, in source form)
 * ==========================================================================
 * The single list of which elements are DPAD tab stops and what X does to
 * them.  docs/EXTENDING_THE_RENDERER.md keeps a human table in sync with
 * this.  To make a new element selectable:
 *   1. add a row below (tag name exactly as the parser stores it);
 *   2. make the parser set focusable_self on the node - only the
 *      interactive element itself gets it, never a descendant that merely
 *      inherits hit-testing (cwv_html.c, SECTION 8);
 *   3. give X a behavior in backend_pointer_tap / focus_activate.
 * button/select/a are recognized through the parser's is_* flags (the
 * flag rows below document that); the tag-keyed rows are what
 * renderer_is_focusable actually matches on. */
typedef struct {
    const char *tag;        /* HTML tag, lowercase, as stored in node->tag */
    int  via_flag;          /* 1 = recognized via a parser is_* flag, not here */
    int  is_stop_by_default;/* 1 = always a stop; 0 = only with explicit tabindex */
    const char *x_does;     /* what CROSS does - doubles as the doc table */
} VwFocusRule;
static const VwFocusRule VW_FOCUS_RULES[] = {
    { "button",  1, 1, "fires JS click listeners + pressed visual" },
    { "select",  1, 1, "opens the native picker" },
    { "a",       1, 1, "fires click listeners, then navigates; "
                        "event.preventDefault() cancels; '#frag' never reloads" },
    { "input",   0, 1, "opens the IME keyboard (type text/password)" },
    { "textarea",0, 1, "opens the IME keyboard" },
    { "img",     0, 0, "fires its click listeners; a stop only with explicit "
                        "tabindex" },
    { NULL, 0, 0, NULL }
};

/* Which elements can take DPAD focus? Only the actual interactive element,
 * not the descendants that merely inherit its hit-testing flags. */
static int renderer_is_focusable(const CwvNode *n) {
    int i;
    if (!n || n->display_none || n->disabled || n->tabindex < 0)
        return 0;
    /* (75) per-element scroll regions: an overflow box that actually
     * scrolls is a tab stop like any control (Chrome parity: scrollable
     * elements take keyboard focus for arrow scrolling).  tabindex=-1
     * above keeps one out of the cycle. */
    if (n->scroll_region) return 1;
    if (!n->focusable_self) return 0;
    /* tabindex=-1 removes an element from the cycle; the parser's is_*
       flags carry the first four registry rows even when the tag spelling
       differs (e.g. an <a> wrapped around a block). */
    if (n->is_button || n->is_link || n->is_select || n->is_textfield)
        return 1;
    /* tag-keyed registry rows (input/textarea/img) */
    if (n->tag[0]) {
        for (i = 0; VW_FOCUS_RULES[i].tag; i++) {
            if (strcmp(n->tag, VW_FOCUS_RULES[i].tag) != 0) continue;
            if (VW_FOCUS_RULES[i].is_stop_by_default) return 1;
            return n->is_image && n->tabindex >= 0;  /* img: explicit tabindex */
        }
    }
    return 0;
}

static int focus_tab_index(const CwvNode *n) {
    if (!n) return -1;
    return n->tabindex;
}

/* Browser-like keyboard/focus order:
 *   1. positive tabindex controls, ascending;
 *   2. implicit tabindex=0 controls, DOM order.
 * Descendants which merely inherit link/button hit-testing are not focusable
 * because focusable_self is only set on the actual interactive element.
 * This prevents a <p> inside a <div>/<button>/<a> from becoming a second
 * DPAD stop.  Direction +1 = forward, -1 = backward, 0 = no-op. */
static int renderer_focus_cycle(CwvRenderer *r, int direction) {
    if (!r || direction == 0) return -1;
    renderer_update_scroll_regions(r);   /* (75): regions are tab stops */

    int count = 0;
    int current_tab = 0;
    int current_node = r->focus_node;
    for (int i = 0; i < r->node_count; i++) {
        if (!renderer_is_focusable(&r->nodes[i])) continue;
        if (i == current_node) { current_tab = focus_tab_index(&r->nodes[i]); }
        count++;
    }
    if (count <= 0) return 0;

    int found = -1;
    if (direction > 0) {
        /* Find the next candidate after the current one according to
           tabindex/DOM order.  Positive tabindex values are ordered first. */
        int best_tab = 1000000, best_dom = 1000000;
        if (current_node < 0) {
            /* Positive tabindex values form the first focus ring.  If none
               exist, start with the first implicit tabindex=0 control. */
            int have_positive = 0;
            for (int i = 0; i < r->node_count; i++)
                if (renderer_is_focusable(&r->nodes[i]) && focus_tab_index(&r->nodes[i]) > 0) {
                    have_positive = 1;
                    break;
                }
            best_tab = have_positive ? 1000000 : 0;
            best_dom = 1000000;
            for (int i = 0; i < r->node_count; i++)
                if (renderer_is_focusable(&r->nodes[i])) {
                    int t = focus_tab_index(&r->nodes[i]);
                    if ((have_positive ? t > 0 : t == 0) &&
                        (t < best_tab || (t == best_tab && i < best_dom))) {
                        best_tab = t;
                        best_dom = i;
                        found = i;
                    }
                }
        } else {
            int cur_tab = current_tab;
            for (int i = 0; i < r->node_count; i++)
                if (renderer_is_focusable(&r->nodes[i])) {
                    int t = focus_tab_index(&r->nodes[i]);
                    if ((t > cur_tab || (t == cur_tab && i > current_node)) &&
                        (t < best_tab || (t == best_tab && i < best_dom))) {
                        best_tab = t;
                        best_dom = i;
                        found = i;
                    }
                }
            if (found < 0) {
                /* Wrap to the first focusable candidate. */
                best_tab = 1000000;
                best_dom = 1000000;
                for (int i = 0; i < r->node_count; i++)
                    if (renderer_is_focusable(&r->nodes[i])) {
                        int t = focus_tab_index(&r->nodes[i]);
                        if (t < best_tab || (t == best_tab && i < best_dom)) {
                            best_tab = t;
                            best_dom = i;
                            found = i;
                        }
                    }
            }
        }
    } else {
        int best_tab = -1, best_dom = -1;
        if (current_node < 0) {
            for (int i = 0; i < r->node_count; i++)
                if (renderer_is_focusable(&r->nodes[i])) {
                    int t = focus_tab_index(&r->nodes[i]);
                    if (t > best_tab || (t == best_tab && i > best_dom)) {
                        best_tab = t;
                        best_dom = i;
                        found = i;
                    }
                }
        } else {
            int cur_tab = current_tab;
            for (int i = 0; i < r->node_count; i++)
                if (renderer_is_focusable(&r->nodes[i])) {
                    int t = focus_tab_index(&r->nodes[i]);
                    if ((t < cur_tab || (t == cur_tab && i < current_node)) &&
                        (t > best_tab || (t == best_tab && i > best_dom))) {
                        best_tab = t;
                        best_dom = i;
                        found = i;
                    }
                }
            if (found < 0) {
                best_tab = -1;
                best_dom = -1;
                for (int i = 0; i < r->node_count; i++)
                    if (renderer_is_focusable(&r->nodes[i])) {
                        int t = focus_tab_index(&r->nodes[i]);
                        if (t > best_tab || (t == best_tab && i > best_dom)) {
                            best_tab = t;
                            best_dom = i;
                            found = i;
                        }
                    }
            }
        }
    }
    if (found < 0) return 0;
    r->focus_node = found;
    renderer_reveal_in_regions(r, found);   /* (75) scroll it into view */
    return 1;
}

/* How many <option> nodes belong to this <select> (the nodes between it and
 * the next <select> in the flat node list). */
static int select_option_count(const CwvRenderer *r, const CwvNode *select) {
    if (!r || !select) return 0;
    int count = 0, idx = (int)(select - r->nodes);
    for (int i = idx + 1; i < r->node_count; i++) {
        if (r->nodes[i].is_select && i != idx) break;
        if (r->nodes[i].is_option) count++;
    }
    return count;
}

/* The Nth <option> node of a select, or NULL. */
static CwvNode *select_option_at(CwvRenderer *r, CwvNode *select, int wanted) {
    int idx = (int)(select - r->nodes), n = 0;
    for (int i = idx + 1; i < r->node_count; i++) {
        if (r->nodes[i].is_select && i != idx) break;
        if (r->nodes[i].is_option) { if (n++ == wanted) return &r->nodes[i]; }
    }
    return NULL;
}

/* Select an option (wraps around at both ends) and sync the visible label. */
static void select_set_index(CwvRenderer *r, CwvNode *select, int index) {
    int count = select_option_count(r, select);
    if (count <= 0) return;
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;
    CwvNode *opt = select_option_at(r, select, index);
    if (!opt) return;
    select->select_index = index;
    select->select_hover_index = index;
    /* Keep the selected label on the actual <select> node.  The option
       subtree is hidden from the normal paint pass, so this is the only label
       used while the picker is closed. */
    snprintf(select->text, sizeof(select->text), "%s", opt->text);
    for (int i = (int)(select - r->nodes) + 1; i < r->node_count; i++) {
        if (r->nodes[i].is_select && &r->nodes[i] != select) break;
        if (r->nodes[i].is_option) r->nodes[i].option_selected = (&r->nodes[i] == opt);
    }
}

/* ============================================================================
 * SECTION 10: VIEWPORT, SCROLLING AND THE PUBLIC SCROLL API
 * ============================================================================
 * The renderer keeps a small browser-like safe area inset (see
 * RENDERER_VIEW_INSET) and computes the page's scrollable extent from the
 * laid-out nodes.  Scrollbar visibility mirrors a browser: hidden when the
 * content fits, aware of overflow policies set on <html>/<body>.
 * ==========================================================================*/

static float renderer_content_origin_y(const CwvRenderer *r) {
    (void)r;
    return 0.0f;
}

static float renderer_content_origin_x(const CwvRenderer *r) {
    (void)r;
    return 0.0f;
}

/* Total document height (at current zoom), never smaller than the viewport. */
static float renderer_content_height(const CwvRenderer *r) {
    if (!r) return 0.0f;
    const float inset = RENDERER_VIEW_INSET;
    float top = inset;
    float bottom = (float)r->height - inset;
    for (int i = 0; i < r->node_count; i++) {
        const CwvNode *n = &r->nodes[i];
        if (n->display_none) continue;
        float b = n->y + n->h + n->margin_bottom;
        /* (75) browser parity: content clipped by a scroll region must
         * not extend the PAGE's scrollable area past the region box. */
        if (n->has_clip && b > n->clip_y1) b = n->clip_y1;
        if (b > bottom) bottom = b;
    }
    float extent = (bottom > inset ? bottom : inset);
    float z = r->zoom > 0.01f ? r->zoom : 1.0f;
    float scaled = (extent - top) * z;
    float viewport = (float)r->height - 2.0f * inset;
    return scaled > viewport ? scaled : viewport;
}

/* Total document width (at current zoom), never smaller than the viewport. */
static float renderer_content_width(const CwvRenderer *r) {
    if (!r) return 0.0f;
    const float inset = RENDERER_VIEW_INSET;
    float left = inset;
    float right = (float)r->width - inset;
    for (int i = 0; i < r->node_count; i++) {
        const CwvNode *n = &r->nodes[i];
        if (n->display_none) continue;
        float rr = n->x + n->w + n->margin_right;
        /* (75) browser parity: region-clipped content never widens the
         * page. */
        if (n->has_clip && rr > n->clip_x1) rr = n->clip_x1;
        if (rr > right) right = rr;
    }
    float extent = (right > inset ? right : inset);
    float z = r->zoom > 0.01f ? r->zoom : 1.0f;
    float scaled = (extent - left) * z;
    float viewport = (float)r->width - 2.0f * inset;
    return scaled > viewport ? scaled : viewport;
}

/* Should scrollbars be shown, and how large is the viewport after reserving
 * their space? (A horizontal bar eats vertical space and vice versa, hence
 * the second pass.) */
static void renderer_scrollbar_state(const CwvRenderer *r, int *vertical,
                                     int *horizontal, float *viewport_w,
                                     float *viewport_h) {
    const float sb = 8.0f;
    float cw = renderer_content_width(r);
    float ch = renderer_content_height(r);
    int v = 0, h = 0;

    if (r->overflow_y != 1 && r->overflow_y != 2 &&
        ch > (float)r->height - 2.0f * RENDERER_VIEW_INSET) v = 1;
    if (r->overflow_x != 1 && r->overflow_x != 2 &&
        cw > (float)r->width - 2.0f * RENDERER_VIEW_INSET) h = 1;

    /* A horizontal scrollbar consumes vertical viewport space and can itself
       create vertical overflow; likewise for the vertical scrollbar.  A
       second pass resolves that interaction without introducing a layout
       dependency or moving the document origin. */
    if (h && r->overflow_y != 1 && r->overflow_y != 2 &&
        ch > (float)r->height - 2.0f * RENDERER_VIEW_INSET - sb) v = 1;
    if (v && r->overflow_x != 1 && r->overflow_x != 2 &&
        cw > (float)r->width - 2.0f * RENDERER_VIEW_INSET - sb) h = 1;

    if (vertical)   *vertical = v;
    if (horizontal) *horizontal = h;
    if (viewport_w) *viewport_w = (float)r->width - 2.0f * RENDERER_VIEW_INSET - (v ? sb : 0.0f);
    if (viewport_h) *viewport_h = (float)r->height - 2.0f * RENDERER_VIEW_INSET - (h ? sb : 0.0f);
}

/* Largest allowed horizontal scroll offset (0 = not scrollable). */
static float renderer_max_scroll_x(const CwvRenderer *r) {
    if (r->overflow_x == 2 || r->overflow_x == 1) return 0.0f;
    float vw = 0.0f;
    renderer_scrollbar_state(r, NULL, NULL, &vw, NULL);
    float max = renderer_content_width(r) - vw;
    return max > 0.0f ? max : 0.0f;
}

/* Largest allowed vertical scroll offset (0 = not scrollable). */
static float renderer_max_scroll(const CwvRenderer *r) {
    if (r->overflow_y == 2 || r->overflow_y == 1) return 0.0f;
    float vh = 0.0f;
    renderer_scrollbar_state(r, NULL, NULL, NULL, &vh);
    float max = renderer_content_height(r) - vh;
    return max > 0.0f ? max : 0.0f;
}

static int backend_scroll_x_by(CwvBackend *backend, float delta) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    /* (75,84) horizontal axis: the region chain of the focused node
     * consumes the delta first (same semantics as backend_scroll_by). */
    renderer_update_scroll_regions(r);
    if (r->focus_node >= 0 && r->focus_node < r->node_count) {
        delta = renderer_regions_absorb_x(r, r->focus_node, delta);
        if (delta == 0.0f) return 0;
    }
    float target = r->scroll_x + delta;
    float max = renderer_max_scroll_x(r);
    if (target < 0.0f) target = 0.0f;
    if (target > max) target = max;
    r->scroll_x = target;
    return 0;
}

static float backend_scroll_x_offset(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? r->scroll_x : 0.0f;
}

static float backend_content_width(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? renderer_content_width(r) : 0.0f;
}

static int backend_scroll_by(CwvBackend *backend, float delta) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    /* (75,84) per-element scroll: the region chain of the focused node
     * (the region itself, or the container of a selected child) consumes
     * the delta first; whatever the bounds cannot absorb reaches the
     * page, like nested scroll containers in a browser. */
    renderer_update_scroll_regions(r);
    if (r->focus_node >= 0 && r->focus_node < r->node_count) {
        delta = renderer_regions_absorb_y(r, r->focus_node, delta);
        if (delta == 0.0f) return 0;
    }
    float target = r->scroll_y + delta;
    float max = renderer_max_scroll(r);
    if (target < 0.0f) target = 0.0f;
    if (target > max) target = max;
    r->scroll_y = target;
    return 0;
}

static int backend_scroll_to(CwvBackend *backend, float offset) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    float max = renderer_max_scroll(r);
    if (offset < 0.0f) offset = 0.0f;
    if (offset > max) offset = max;
    r->scroll_y = offset;
    return 0;
}

/* (84) scroll at a SCREEN POINT (touch/pointer drag dispatch): the
 * deepest scroll region under the point consumes the delta through its
 * ancestor chain; the leftover reaches the page.  With no region under
 * the point this is exactly backend_scroll_by. */
static int backend_scroll_at(CwvBackend *backend, float delta, int x, int y) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    renderer_update_scroll_regions(r);
    int reg = renderer_region_at(r, x, y);
    if (reg >= 0) {
        delta = renderer_regions_absorb_y(r, reg, delta);
        if (delta == 0.0f) return 0;
    }
    float target = r->scroll_y + delta;
    float max = renderer_max_scroll(r);
    if (target < 0.0f) target = 0.0f;
    if (target > max) target = max;
    r->scroll_y = target;
    return 0;
}

/* (84) PAGE-only horizontal scroll: the page scrollbar is explicit page
 * chrome - the focused node's region chain must not eat its delta. */
static int backend_scroll_page_x_by(CwvBackend *backend, float delta) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    float target = r->scroll_x + delta;
    float max = renderer_max_scroll_x(r);
    if (target < 0.0f) target = 0.0f;
    if (target > max) target = max;
    r->scroll_x = target;
    return 0;
}

/* (84) horizontal axis of backend_scroll_at. */
static int backend_scroll_x_at(CwvBackend *backend, float delta, int x, int y) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    renderer_update_scroll_regions(r);
    int reg = renderer_region_at(r, x, y);
    if (reg >= 0) {
        delta = renderer_regions_absorb_x(r, reg, delta);
        if (delta == 0.0f) return 0;
    }
    float target = r->scroll_x + delta;
    float max = renderer_max_scroll_x(r);
    if (target < 0.0f) target = 0.0f;
    if (target > max) target = max;
    r->scroll_x = target;
    return 0;
}

static float backend_scroll_offset(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? r->scroll_y : 0.0f;
}

static float backend_content_height(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? renderer_content_height(r) : 0.0f;
}

/* ============================================================================
 * SECTION 11: TEXT INPUT (VITA IME) AND FOCUS ACTIVATION
 * ============================================================================
 * The Vita's on-screen keyboard (IME dialog) is the only way to type on the
 * device, so <textfield> editing runs through it.  These helpers convert
 * between UTF-8 (the HTML side) and UTF-16 (the IME side), open the dialog
 * and apply its result back into the node.  Activation of the focused
 * control (Cross/Enter) lives here too.
 * ==========================================================================*/

/* UTF-8 -> UTF-16 (with surrogate pairs for code points above 0xFFFF). */
static void utf8_to_utf16_simple(const char *src, cwv_utf16 *dst, size_t cap) {
    size_t di = 0;
    const unsigned char *p = (const unsigned char *)(src ? src : "");
    if (!dst || cap == 0) return;
    while (*p && di + 1 < cap) {
        unsigned cp = 0;
        int n = 1;
        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((unsigned)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            n = 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            n = 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((unsigned)(p[0] & 7) << 18) | ((unsigned)(p[1] & 0x3F) << 12) |
                 ((unsigned)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            n = 4;
        } else {
            cp = 0xFFFD;   /* invalid byte: replacement character */
        }
        p += n;
        if (cp <= 0xFFFF) {
            dst[di++] = (cwv_utf16)cp;
        } else if (cp <= 0x10FFFF && di + 2 < cap) {
            cp -= 0x10000;
            dst[di++] = (cwv_utf16)(0xD800 + (cp >> 10));
            dst[di++] = (cwv_utf16)(0xDC00 + (cp & 0x3FF));
        }
    }
    dst[di] = 0;
}

/* UTF-16 -> UTF-8 (surrogate-aware). */
static void utf16_to_utf8_simple(const cwv_utf16 *src, char *dst, size_t cap) {
    size_t oi = 0;
    if (!dst || cap == 0) return;
    dst[0] = 0;
    for (size_t i = 0; src && src[i] && oi + 1 < cap; i++) {
        unsigned cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (src[++i] - 0xDC00);
        }
        if (cp < 0x80) {
            if (oi + 1 >= cap) break;
            dst[oi++] = (char)cp;
        } else if (cp < 0x800) {
            if (oi + 2 >= cap) break;
            dst[oi++] = (char)(0xC0 | (cp >> 6));
            dst[oi++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            if (oi + 3 >= cap) break;
            dst[oi++] = (char)(0xE0 | (cp >> 12));
            dst[oi++] = (char)(0x80 | ((cp >> 6) & 63));
            dst[oi++] = (char)(0x80 | (cp & 63));
        } else {
            if (oi + 4 >= cap) break;
            dst[oi++] = (char)(0xF0 | (cp >> 18));
            dst[oi++] = (char)(0x80 | ((cp >> 12) & 63));
            dst[oi++] = (char)(0x80 | ((cp >> 6) & 63));
            dst[oi++] = (char)(0x80 | (cp & 63));
        }
    }
    dst[oi] = 0;
}

/* Open the native IME for a textfield node (pre-filled with its value). */
static int renderer_textfield_open(CwvRenderer *r, int node_index) {
    if (!r || node_index < 0 || node_index >= r->node_count) return -1;
    CwvNode *n = &r->nodes[node_index];
    if (!n->is_textfield || n->disabled) return 0;
    if (r->ime_active) return 1;
    utf8_to_utf16_simple(n->text, r->ime_initial, CWV_IME_MAX_TEXT);
    memcpy(r->ime_buffer, r->ime_initial, sizeof(r->ime_buffer));

    if (cwv_ime_open(u"CWebView", r->ime_initial, n->textfield_password,
                     n->textfield_multiline,
                     r->ime_buffer, CWV_IME_MAX_TEXT) < 0) return -1;
    r->ime_node = node_index;
    r->ime_active = 1;
    r->focus_node = node_index;
    return 1;
}

/* Poll the IME (called every frame from backend_update): when the user is
 * done, copy the typed text back into the node. */
static void renderer_textfield_update(CwvRenderer *r) {
    if (!r || !r->ime_active) return;
    int st = cwv_ime_poll(r->ime_buffer, CWV_IME_MAX_TEXT);
    if (st == 0) return;
    if (st == 1 && r->ime_node >= 0 && r->ime_node < r->node_count)
        utf16_to_utf8_simple(r->ime_buffer, r->nodes[r->ime_node].text,
                             sizeof(r->nodes[r->ime_node].text));
    r->ime_active = 0;
    r->ime_node = -1;
}

/* Activate the focused control (Cross/Enter): open pickers, press buttons,
 * follow links. */
static int renderer_focus_activate(CwvRenderer *r) {
    if (!r || r->focus_node < 0 || r->focus_node >= r->node_count) return 0;
    CwvNode *n = &r->nodes[r->focus_node];
    if (n->is_select) {
        n->select_open = !n->select_open;
        n->select_hover_index = n->select_index;
        return 1;
    }
    if (n->is_textfield) return renderer_textfield_open(r, r->focus_node);
    if (n->is_button) {
        n->button_pressed = 1;
        /* A JS "activate" listener on a button replaces nothing (buttons do
           not navigate); it just runs after the press. */
        js_notify_activate(r, r->focus_node);
        return 1;
    }
    if (n->is_link && n->href[0]) {
        /* Chrome order: run the click listeners FIRST, then navigate.  A
           listener only cancels the navigation by calling
           event.preventDefault().  A pure-fragment href ("#top") fires
           the click but never reloads the page - this engine has no
           anchor scrolling, and reloading would lose page state. */
        if (js_notify_activate(r, r->focus_node)) return 1;
        if (n->href[0] == '#') return 1;
        snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", n->href);
        snprintf(r->pending_target, sizeof(r->pending_target), "%s",
                 n->link_target[0] ? n->link_target : "app");
        return 1;
    }
    return 0;
}

/* ============================================================================
 * SECTION 12: DRAWING
 * ============================================================================
 * draw_node() paints one node at a time:
 *   1. images (custom path, done)
 *   2. the box "chrome": background fill, border (solid/double/dashed/dotted),
 *      focus ring
 *   3. the node's CONTENT, dispatched like this:
 *        - is_textfield   -> native text field content (value, caret, dots)
 *        - is_select      -> centered label + disclosure arrow
 *        - TAG_TABLE draw hook (if the tag registered one)   <-- extension
 *        - otherwise      -> plain text with the system font
 *
 * backend_draw() then paints the whole frame: page background, every node,
 * any opened <select> popup, the scrollbars and the optional VCM modal.
 * ==========================================================================*/

/* Draw a solid 1-stroke border (style=1) or the dashed/dotted patterns. */
static void draw_node_border(CwvNode *n, float sx, float sy,
                             float sw, float sh, float z) {
    float bw = n->border_width * z;
    if (n->border_style == 1) {
        cwv_draw_rectangle(sx, sy, sw, bw, n->border_color);
        cwv_draw_rectangle(sx, sy + sh - bw, sw, bw, n->border_color);
        cwv_draw_rectangle(sx, sy, bw, sh, n->border_color);
        cwv_draw_rectangle(sx + sw - bw, sy, bw, sh, n->border_color);
    } else if (n->border_style == 4) {
        /* CSS double border: two distinct strokes separated by a gap.
           On the Vita's pixel-sized raster, a 1px border must still
           produce two 1px strokes rather than collapsing into one. */
        float q = bw / 3.0f; if (q < 1.0f) q = 1.0f;
        float inner = 2.0f * q;
        float inner_w = sw - 4.0f * q;
        float inner_h = sh - 4.0f * q;
        if (inner_w > 0.0f && inner_h > 0.0f) {
            cwv_draw_rectangle(sx, sy, sw, q, n->border_color);
            cwv_draw_rectangle(sx, sy + sh - q, sw, q, n->border_color);
            cwv_draw_rectangle(sx, sy, q, sh, n->border_color);
            cwv_draw_rectangle(sx + sw - q, sy, q, sh, n->border_color);
            cwv_draw_rectangle(sx + inner, sy + inner, inner_w, q, n->border_color);
            cwv_draw_rectangle(sx + inner, sy + sh - inner - q, inner_w, q, n->border_color);
            cwv_draw_rectangle(sx + inner, sy + inner, q, inner_h, n->border_color);
            cwv_draw_rectangle(sx + sw - inner - q, sy + inner, q, inner_h, n->border_color);
        } else {
            /* Extremely small boxes cannot physically contain two
               separated strokes; keep a valid outer border instead. */
            cwv_draw_rectangle(sx, sy, sw, q, n->border_color);
            cwv_draw_rectangle(sx, sy + sh - q, sw, q, n->border_color);
            cwv_draw_rectangle(sx, sy, q, sh, n->border_color);
            cwv_draw_rectangle(sx + sw - q, sy, q, sh, n->border_color);
        }
    } else {
        /* Dashed/dotted: a repeating dash pattern on each side. */
        float dash = bw * 4.0f; if (dash < 4) dash = 4;
        for (float xx = sx; xx < sx + sw; xx += dash * 2)
            cwv_draw_rectangle(xx, sy, fminf(dash, sx + sw - xx), bw, n->border_color);
        for (float xx = sx; xx < sx + sw; xx += dash * 2)
            cwv_draw_rectangle(xx, sy + sh - bw, fminf(dash, sx + sw - xx), bw, n->border_color);
        for (float yy = sy; yy < sy + sh; yy += dash * 2)
            cwv_draw_rectangle(sx, yy, bw, fminf(dash, sy + sh - yy), n->border_color);
        for (float yy = sy; yy < sy + sh; yy += dash * 2)
            cwv_draw_rectangle(sx + sw - bw, yy, bw, fminf(dash, sy + sh - yy), n->border_color);
    }
}

/* The <textfield> content: value or placeholder text, a password mask, and
 * a caret when focused. */
static void draw_textfield_content(CwvRenderer *r, CwvNode *n,
                                   float sx, float sy, float sw, float sh,
                                   float z, unsigned text_color) {
    /* the uniform draw signature carries sw/sh, but text fields clip to the
     * box they already laid out, so they are unused here */
    (void)sw; (void)sh;
    float inner_x = sx + (n->padding_left + n->border_width) * z;
    float inner_y = sy + (n->padding_top + n->border_width) * z;
    float text_metric_tf = line_height(n) * z;
    float text_y = inner_y +
        (sh - (n->padding_top + n->padding_bottom + 2.0f * n->border_width) * z +
         text_metric_tf) * 0.5f - 2.0f * z;
    const char *shown = n->text[0] ? n->text : n->placeholder;
    unsigned tc = n->text[0] ? text_color
        : (r->theme_dark ? rgba(150, 156, 166, 255) : rgba(120, 124, 132, 255));
    char mask[256];
    if (n->textfield_password && n->text[0]) {
        size_t m = strlen(n->text);
        if (m >= sizeof(mask)) m = sizeof(mask) - 1;
        for (size_t mi = 0; mi < m; mi++) mask[mi] = '*';
        mask[m] = 0;
        shown = mask;
    }
    if (n->font)
        cwv_font_draw_text(n->font, inner_x, text_y, tc,
                              (unsigned)(n->font_size * 32 * z), shown);
    else if (r->system_pgf)
        cwv_pgf_draw_text(r->system_pgf, inner_x, text_y, tc, n->font_size * z, shown);
    if (r->focus_node == (int)(n - r->nodes) && !r->ime_active) {
        float cw = measure_text_width(r, n, n->text) * z;
        float cy = inner_y;
        float ch = sh - (n->padding_top + n->padding_bottom + 2.0f * n->border_width) * z;
        cwv_draw_rectangle(inner_x + cw + 1.0f * z, cy, 1.5f * z, ch, tc);
    }
}

/* The closed <select> content: the chosen label centered in the control plus
 * a native disclosure arrow (drawn as stacked rectangles because vita2d has
 * no public triangle primitive on all SDK versions). */
static void draw_select_content(CwvRenderer *r, CwvNode *n,
                                float sx, float sy, float sw, float sh,
                                float z, float baseline, unsigned text_color) {
    (void)sw; (void)sh;   /* same clipping story as text fields */
    float inner_x = sx + (n->padding_left + n->border_width) * z;
    float inner_w = (n->w - n->padding_left - n->padding_right - 2.0f * n->border_width) * z;
    float tw = measure_text_width(r, n, n->text) * z;
    float text_metric = line_height(n) * z;
    float label_x = inner_x + (inner_w - tw) * 0.5f;
    if (label_x < inner_x) label_x = inner_x;
    /* Vita2D font baselines include glyph ascent/leading differently from
       the CSS line-box midpoint.  Lift the baseline slightly so the visible
       glyphs, rather than the font box, are vertically centered. */
    baseline = sy + (sh + text_metric) * 0.5f - 3.0f * z;
    if (n->font)
        cwv_font_draw_text(n->font, label_x, baseline, text_color,
                              (unsigned)(n->font_size * 32 * z), n->text);
    else if (r->system_pgf)
        cwv_pgf_draw_text(r->system_pgf, label_x, baseline, text_color,
                             n->font_size * z, n->text);

    /* A small native disclosure arrow makes it clear that this is a picker
       even when the original WebKit select chrome is unavailable. */
    float arrow_x = label_x + (tw > inner_w ? inner_w : tw) + 10.0f * z;
    float arrow_cx = (n->x + n->w - n->padding_right - n->border_width) * z -
                     r->scroll_x - 6.0f * z;
    if (arrow_x > arrow_cx - 8.0f * z) arrow_x = arrow_cx - 8.0f * z;
    float arrow_y = sy + sh * 0.5f;
    const float aw = 5.0f * z;
    const float ah = 6.0f * z;
    const float ay = arrow_y - 2.0f * z;
    for (int row = 0; row < 6; ++row) {
        float half = aw * (1.0f - (float)row / 6.0f);
        float rw = half * 2.0f;
        float rh = ah / 6.0f + 0.35f * z;
        cwv_draw_rectangle(arrow_cx - half, ay + (float)row * (ah / 6.0f), rw, rh,
                              text_color);
    }
}

/* CSS letter-spacing/word-spacing (2026-10-05 (65)): draw the run one
 * UTF-8 rune at a time, advancing by each rune's measured width plus the
 * letter gap (and the word gap after a space).  Only taken when the page
 * actually asks for spacing - the normal single-draw fast path is kept
 * byte-identical for every existing page. */
static void draw_text_spaced(CwvRenderer *r, CwvNode *n, float x,
                             float baseline, unsigned color, float z) {
    const char *p = n->text;
    float pen = x;
    while (p && *p) {
        char rune[8];
        int len = 0;
        float rw;
        rune[len++] = *p++;
        while (*p && ((((unsigned char)*p) & 0xC0) == 0x80) && len < 7)
            rune[len++] = *p++;
        rune[len] = 0;
        if (n->font) {
            unsigned sz = (unsigned)(n->font_size * 32.0f * z);
            if (sz < 1) sz = 1;
            rw = (float)cwv_font_text_width(n->font, sz, rune);
            cwv_font_draw_text(n->font, pen, baseline, color, sz, rune);
        } else if (r->system_pgf) {
            rw = (float)cwv_pgf_text_width(r->system_pgf, n->font_size * z, rune);
            cwv_pgf_draw_text(r->system_pgf, pen, baseline, color,
                              n->font_size * z, rune);
        } else {
            rw = (float)len * (n->font_size * 12.0f);
        }
        pen += rw + n->letter_spacing * z;
        if (rune[0] == ' ') pen += n->word_spacing * z;
    }
}

/* CSS text-decoration (2026-10-05 (65)): underline / line-through /
 * overline as solid text-colored rects across the run, Chrome-style. */
static void draw_text_decoration(CwvRenderer *r, CwvNode *n,
                                 float sx, float baseline,
                                 unsigned color, float z) {
    float lw, th;
    if (!n->deco || !n->text[0]) return;
    lw = measure_text_width(r, n, n->text) * z;
    th = z > 0.8f ? 1.5f : 1.0f;
    if (n->deco & 1)   /* underline */
        cwv_draw_rectangle(sx, baseline + 3.0f * z, lw, th, color);
    if (n->deco & 2)   /* line-through: mid-line of the glyph box */
        cwv_draw_rectangle(sx, baseline - 7.0f * z, lw, th, color);
    if (n->deco & 4)   /* overline: top of the line box */
        cwv_draw_rectangle(sx, baseline - 18.0f * z, lw, th, color);
}

static void draw_node(CwvRenderer *r, CwvNode *n) {
    if (n->display_none) return;

    /*
     * VCM activation is a persistent page state, not the browser's transient
     * :active mouse/keyboard state.  Apply matching :active CSS to a temporary
     * node so switching VCM controls never permanently mutates the base style.
     */
    CwvNode active_node;
    if (n->vcm_active) {
        active_node = *n;
        apply_rules(r, &active_node, n->id, n->cls, 1);
        n = &active_node;
    }

    /* CSS visibility:hidden (2026-10-05 (65)): the layout SPACE stays
       (unlike display:none), but nothing paints and hit tests skip it -
       the check runs after the :active re-style so a rule can toggle
       visibility live. */
    if (n->visibility_hidden) return;

    /* CSS opacity (2026-10-05 (65)).  eff_opacity is the accumulated
       ancestor product, paint_backdrop the solid color behind this
       node's own background; every color this node paints is mixed
       toward the backdrop by the factor.  eff == 1 keeps every color
       byte-identical, so pages that use no opacity pay nothing. */
    float eff = n->eff_opacity;
    if (eff < 0.0f) eff = 0.0f;
    if (eff > 1.0f) eff = 1.0f;
    /* surface = what the node's own content sits ON (own bg included,
       already faded).  bg_backdrop = what the node's background sits on. */
    unsigned surface = n->has_background
        ? mix_rgba(n->paint_backdrop, n->background, eff)
        : n->paint_backdrop;

    /* Document -> screen coordinates (zoom + scroll). */
    float z = r->zoom > 0.01f ? r->zoom : 1.0f;
    float origin_x = renderer_content_origin_x(r);
    float origin_y = renderer_content_origin_y(r);
    /* position: fixed (2026-10-05 (70)): pinned to the viewport - the
       page scroll does not apply, exactly like the browser. */
    /* (75) per-element scroll regions: content inside a region shifts by
     * the accumulated ancestor scroll offsets (position:fixed ignores
     * them, exactly like it ignores the page scroll). */
    float sx = (n->pos_kind == 3 ? (n->x - origin_x) * z
                                 : (n->x - origin_x) * z - r->scroll_x
                                   - n->scroll_off_x * z);
    float sy = (n->pos_kind == 3 ? (n->y - origin_y) * z
                                 : (n->y - origin_y) * z - r->scroll_y
                                   - n->scroll_off_y * z);
    float sw = n->w * z, sh = n->h * z;

    /* Offscreen cull - the cheapest big win in the whole draw path.  A
       node entirely outside the viewport paints nothing, so scrolling a
       long page only draws the nodes actually on screen instead of the
       entire document.  Nodes that are even PARTIALLY visible still
       paint, so nothing pops in or out at the edges. */
    if (sx >= (float)r->width || sy >= (float)r->height ||
        sx + sw <= 0.0f || sy + sh <= 0.0f) return;

    /* (75) scroll-region clipping: nodes inside a region paint (and
     * hit-test) only inside the region's box.  The engine culls nodes
     * that fall entirely outside the window on every port, and pushes
     * the pixel-exact rect through the port's clip seam so text and
     * textures clip where the platform supports it (vita2d scissor,
     * SDL2 clip rect, GDI clip; the pure-software Wii port clips rects
     * and relies on this cull for the rest - documented). */
    if (n->has_clip && n->pos_kind != 3) {
        float cx0 = (n->clip_x0 - origin_x) * z - r->scroll_x - n->scroll_off_x * z;
        float cy0 = (n->clip_y0 - origin_y) * z - r->scroll_y - n->scroll_off_y * z;
        float cx1 = (n->clip_x1 - origin_x) * z - r->scroll_x - n->scroll_off_x * z;
        float cy1 = (n->clip_y1 - origin_y) * z - r->scroll_y - n->scroll_off_y * z;
        if (sx >= cx1 || sy >= cy1 || sx + sw <= cx0 || sy + sh <= cy0) {
            return;   /* entirely scrolled out of its region */
        }
        /* only emit seam calls when the desired clip changes */
        if (!r->clip_active || r->clip_cx0 != cx0 || r->clip_cy0 != cy0 ||
            r->clip_cx1 != cx1 || r->clip_cy1 != cy1) {
            cwv_set_clip(cx0, cy0, cx1 - cx0, cy1 - cy0);
            r->clip_active = 1;
            r->clip_cx0 = cx0; r->clip_cy0 = cy0;
            r->clip_cx1 = cx1; r->clip_cy1 = cy1;
        }
    } else if (r->clip_active) {
        cwv_clip_disable();
        r->clip_active = 0;
    }

    /* Background fill (buttons darken while pressed). */
    if (n->has_background) {
        unsigned bg = n->background;
        if (n->is_button && n->button_pressed) {
            unsigned a = (bg >> 24) & 255, rr = (bg >> 16) & 255,
                     gg = (bg >> 8) & 255, bb = bg & 255;
            bg = rgba((unsigned)(rr * 0.72f), (unsigned)(gg * 0.72f),
                      (unsigned)(bb * 0.72f), a);
        }
        if (eff < 1.0f) bg = mix_rgba(n->paint_backdrop, bg, eff);
        cwv_draw_rectangle(sx, sy, sw, sh, bg);
    }

    /* CSS background-image: tiled from the box's top-left, Chrome's
       default background-repeat: repeat.  Edge tiles draw as texture
       SUB-REGIONS (the visible slice converted back to texture pixels),
       so the pattern clips exactly at the box with no scissor - and it
       tracks zoom like every other paint. */
    if (n->bg_image && sw > 0.5f && sh > 0.5f) {
        float iw = (float)cwv_tex_width(n->bg_image);
        float ih = (float)cwv_tex_height(n->bg_image);
        if (iw > 0.5f && ih > 0.5f) {
            float tw = iw * z, th = ih * z;
            for (float ty = sy; ty < sy + sh; ty += th) {
                for (float tx = sx; tx < sx + sw; tx += tw) {
                    float vw = tw, vh = th;
                    if (tx + vw > sx + sw) vw = sx + sw - tx;
                    if (ty + vh > sy + sh) vh = sy + sh - ty;
                    cwv_draw_texture_part(n->bg_image, tx, ty,
                                             0.0f, 0.0f, vw / z, vh / z);
                }
            }
        }
    }

    /* Images: paint the texture (object-fit aware) or the Chrome-style
       broken-image box, then fall through to border + focus ring so an
       <img> with CSS borders or tabindex paints like Chrome. */
    if (n->is_image) {
        if (n->image) {
            float tw = (float)cwv_tex_width(n->image);
            float th = (float)cwv_tex_height(n->image);
            /* CSS image-rendering: pixelated = nearest sampling, auto =
               linear.  The filter only goes to the texture when the CSS
               value CHANGES, so steady-state frames stay untouched. */
            if (n->image_filter_state != n->image_rendering) {
                int f = n->image_rendering
                    ? CWV_TEX_FILTER_POINT
                    : CWV_TEX_FILTER_LINEAR;
                cwv_tex_set_filter(n->image, f, f);
                n->image_filter_state = n->image_rendering;
            }
            if (tw > 0.5f && th > 0.5f) {
                if (n->object_fit == 1) {          /* contain: letterbox */
                    float dw = sw, dh = sw * th / tw;
                    if (dh > sh) { dh = sh; dw = sh * tw / th; }
                    float s = dw / tw;
                    cwv_draw_texture_part_scale(n->image,
                        sx + (sw - dw) * 0.5f, sy + (sh - dh) * 0.5f,
                        0.0f, 0.0f, tw, th, s, s);
                } else if (n->object_fit == 2) {    /* cover: crop to fill */
                    float pw = tw, ph = th;
                    if (tw / th > sw / sh) pw = th * (sw / sh);
                    else                   ph = tw * (sh / sw);
                    cwv_draw_texture_part_scale(n->image, sx, sy,
                        (tw - pw) * 0.5f, (th - ph) * 0.5f, pw, ph,
                        sw / pw, sh / ph);
                } else {                           /* fill: stretch (default) */
                    cwv_draw_texture_part_scale(n->image, sx, sy,
                        0.0f, 0.0f, tw, th, sw / tw, sh / th);
                }
            }
        } else if (sw > 0.5f && sh > 0.5f) {
            /* Chrome-style broken image: light placeholder box, thin gray
               border, and the alt text drawn in the corner. */
            cwv_draw_rectangle(sx, sy, sw, sh, rgba(238, 238, 238, 255));
            unsigned bc = rgba(120, 120, 120, 255);
            cwv_draw_rectangle(sx, sy, sw, 1.0f, bc);
            cwv_draw_rectangle(sx, sy + sh - 1.0f, sw, 1.0f, bc);
            cwv_draw_rectangle(sx, sy, 1.0f, sh, bc);
            cwv_draw_rectangle(sx + sw - 1.0f, sy, 1.0f, sh, bc);
            /* The default "image failed" glyph: the engine's bundled
               broken-image icon (a real PNG asset, compiled in - see
               broken_image_texture() above), drawn at its native pixel
               size scaled by zoom, like Chrome's small fixed-size glyph
               in the corner of the placeholder box.  Falls back to
               drawing NOTHING extra (just the box) if the tiny decode
               ever fails - the box and border alone still mark the
               spot.
               DIAGNOSTIC: if this glyph shows on device, layout and
               painting are fine and the failure is in the path/load; if
               not even the box appears, the <img> element itself never
               made it into the layout. */
            {
                void *bt = broken_image_texture();
                if (bt) {
                    float bw = (float)cwv_tex_width(bt) * z;
                    float bh = (float)cwv_tex_height(bt) * z;
                    cwv_draw_texture_scale(bt, sx + 4.0f * z, sy + 4.0f * z,
                                              z, z);
                    (void)bw; (void)bh;
                }
            }
            if (n->alt[0]) {
                float ty = sy + n->font_size * 21.0f * z;
                unsigned ac = rgba(90, 90, 90, 255);
                if (n->font)
                    cwv_font_draw_text(n->font, sx + 4.0f * z, ty, ac,
                                          (unsigned)(n->font_size * 32 * z),
                                          n->alt);
                else if (r->system_pgf)
                    cwv_pgf_draw_text(r->system_pgf, sx + 4.0f * z, ty, ac,
                                         n->font_size * z, n->alt);
            }
        }
    }

    /* Border, if any. */
    if (n->has_border && n->border_width > 0 && n->border_style != 0) {
        if (eff < 1.0f) {
            CwvNode bnode = *n;
            bnode.border_color = mix_rgba(surface, n->border_color, eff);
            draw_node_border(&bnode, sx, sy, sw, sh, z);
        } else {
            draw_node_border(n, sx, sy, sw, sh, z);
        }
    }

    /* (The DPAD focus ring is painted by a dedicated pass in backend_draw.
       It used to be drawn here, mid document order, so any element that
       painted later covered the focused link's ring - only the LAST link
       on the page showed its bottom edge.) */

    /* Images have no text pass of their own; everything they paint is
       already done by the texture block above. */
    if (n->is_image) return;

    /* Resolve the text metrics and color used by every content style. */
    float text_metric = line_height(n) * z;
    float baseline = sy + n->font_size * 21.0f * z;
    if (n->is_button) {
        float content_top = sy + (n->padding_top + n->border_width) * z;
        float content_h = (n->h - n->padding_top - n->padding_bottom -
                           n->border_width * 2.0f) * z;
        if (content_h < text_metric) content_h = text_metric;
        /* Chrome centers the LINE BOX in the content box, then drops the
           pen one ASCENT from its top.  The PGF/TTF pen is a baseline: glyph
           pixels extend about 75% of the line box above it (font_size*21 of
           the font_size*28 line height), so the pen sits only a QUARTER line
           below the box middle - not half a line, which visibly pushed every
           button label too far down. */
        baseline = content_top + (content_h - text_metric) * 0.5f
                 + text_metric * 0.75f;
    }
    unsigned text_color = n->color;
    /* Button labels inherit the button's UA text color.  Keep this decision
       at draw time as well as during theme propagation so a button can never
       flash black when switching between light/dark defaults.  Explicit CSS
       color still wins. */
    if (n->is_button && !n->color_explicit)
        text_color = rgba(0, 0, 0, 255);
    if (eff < 1.0f)
        text_color = mix_rgba(surface, text_color, eff);

    /* Nothing textual left to paint?  Only a registered draw hook may still
       produce visuals here (extension point, see SECTION 7). */
    const TagInfo *info = tag_info(n->tag);
    if (!n->text[0] && !n->is_textfield) {
        if (info && info->draw) {
            TagDrawCtx dc = { r, n, sx, sy, sw, sh, z, baseline, text_color };
            info->draw(&dc);
        }
        return;
    }

    if (n->is_textfield) {
        /* Keep the closed <select> label visible even while the native picker
           is open. The picker is rendered after the page and is positioned
           outside the control, so the selected label must remain part of the
           control chrome rather than disappearing while the menu is open. */
        draw_textfield_content(r, n, sx, sy, sw, sh, z, text_color);
    } else if (n->is_select) {
        draw_select_content(r, n, sx, sy, sw, sh, z, baseline, text_color);
    } else if (info && info->draw) {
        /* Custom per-tag painting registered in TAG_TABLE. */
        TagDrawCtx dc = { r, n, sx, sy, sw, sh, z, baseline, text_color };
        info->draw(&dc);
    } else {
        /* Default: draw the text with the resolved font.  Spacing CSS
           takes the per-rune path; everything else keeps the single
           fast draw (2026-10-05 (65)). */
        if (n->letter_spacing != 0.0f || n->word_spacing != 0.0f) {
            draw_text_spaced(r, n, sx, baseline, text_color, z);
        } else if (n->font)
            cwv_font_draw_text(n->font, sx, baseline, text_color,
                                  (unsigned)(n->font_size * 32 * z), n->text);
        else if (r->system_pgf)
            cwv_pgf_draw_text(r->system_pgf, sx, baseline, text_color,
                                 n->font_size * z, n->text);
        draw_text_decoration(r, n, sx, baseline, text_color, z);
    }
}

/* ---- native <select> popup (rendered after the page like real chrome) --- */

static void draw_select_popups(CwvRenderer *r) {
    for (int i = 0; i < r->node_count; i++) {
        CwvNode *sel = &r->nodes[i];
        if (!sel->is_select || !sel->select_open) continue;
        int count = select_option_count(r, sel);
        if (count <= 0) continue;
        float z = r->zoom > 0.01f ? r->zoom : 1.0f;
        float control_x = (sel->x - renderer_content_origin_x(r)) * z
                          - r->scroll_x - sel->scroll_off_x * z;  /* (75,84) */
        float control_y = (sel->y - renderer_content_origin_y(r)) * z
                          - r->scroll_y - sel->scroll_off_y * z;  /* (75,84) */
        float sx = control_x;
        float sw = sel->w * z; if (sw < 140.0f) sw = 140.0f;
        float row = 28.0f;
        float sh = row * (count > 8 ? 8 : count);
        /* Anchor the popup to the actual rendered select box.  Some HTML
           selects have no explicit CSS height, so use the renderer's measured
           line/padding height instead of a zero n->h.  A zero height used to
           make below_y equal control_y and caused the popup to paint directly
           over the select label. */
        float control_h = sel->h;
        if (control_h < 4.0f) {
            float lh = line_height(sel);
            control_h = lh + sel->padding_top + sel->padding_bottom + 2.0f * sel->border_width;
        }
        if (control_h < 28.0f) control_h = 28.0f;
        float below_y = control_y + control_h * z;
        float above_y = control_y - sh;
        float sy;
        if (below_y + sh <= r->height - 4.0f) {
            sy = below_y;
        } else if (above_y >= 4.0f) {
            sy = above_y;
        } else {
            /* If neither side has enough room, keep the popup attached to
               the control without covering its center; clamp the popup to
               the viewport and let the visible option count remain capped. */
            sy = (float)r->height - 4.0f - sh;
            if (sy < 4.0f) sy = 4.0f;
            if (sy + sh > control_y) sy = control_y - sh;
            if (sy < 4.0f) sy = 4.0f;
        }
        if (sx < 4.0f) sx = 4.0f;
        if (sx + sw > r->width - 4.0f) sx = (float)r->width - sw - 4.0f;
        if (sy < 4.0f) sy = 4.0f;
        unsigned popup_bg = r->theme_dark ? rgba(0, 0, 0, 255) : rgba(250, 250, 250, 255);
        unsigned popup_text = r->theme_dark ? rgba(245, 245, 245, 255) : rgba(20, 20, 20, 255);
        cwv_draw_rectangle(sx, sy, sw, sh, popup_bg);
        for (int j = 0; j < count; j++) {
            CwvNode *opt = select_option_at(r, sel, j);
            if (!opt) continue;
            int selected = (j == sel->select_index);
            int hovered = (j == sel->select_hover_index);
            if (selected)
                cwv_draw_rectangle(sx, sy + j * row, sw, row, rgba(70, 105, 150, 255));
            if (hovered) {
                /* Touch hover and DPAD focus both use the same subtle
                   darkening treatment.  When the hovered row is already the
                   selected row, darken the selected blue instead of replacing
                   it with a neutral gray. */
                unsigned hc = selected
                    ? (r->theme_dark ? rgba(52, 78, 108, 255) : rgba(58, 88, 126, 255))
                    : (r->theme_dark ? rgba(42, 42, 42, 255) : rgba(226, 226, 226, 255));
                cwv_draw_rectangle(sx, sy + j * row, sw, row, hc);
            }
            if (opt->text[0]) {
                unsigned tc = selected ? rgba(255, 255, 255, 255) : popup_text;
                float tw = measure_text_width(r, opt, opt->text);
                float tx = sx + (sw - tw) * 0.5f;
                if (tx < sx + 8) tx = sx + 8;
                float base = sy + j * row + (row + line_height(opt) * 0.78f) * 0.5f;
                cwv_pgf_draw_text(r->system_pgf, tx, base, tc, 0.75f, opt->text);
            }
        }
    }
}

/* Minimal WebKit-like scrollbars.  They occupy the right/bottom edge of the
 * viewport, and when both axes overflow they meet cleanly at the corner
 * instead of drawing over one another. */
static void draw_scrollbars(CwvRenderer *r) {
    int sb_vertical = 0, sb_horizontal = 0;
    float viewport_w = (float)r->width, viewport_h = (float)r->height;
    renderer_scrollbar_state(r, &sb_vertical, &sb_horizontal, &viewport_w, &viewport_h);
    float max_y = renderer_max_scroll(r);
    float max_x = renderer_max_scroll_x(r);
    const float sb = 8.0f;
    const unsigned track = rgba(238, 238, 238, 220);
    const unsigned thumb = rgba(128, 128, 128, 230);
    if (r->scrollbars_visible && sb_vertical && max_y > 0.0f) {
        float track_h = (float)r->height - (sb_horizontal ? sb : 0.0f);
        float content_h = renderer_content_height(r);
        float thumb_h = track_h * (viewport_h / content_h);
        if (thumb_h < 24.0f) thumb_h = 24.0f;
        if (thumb_h > track_h) thumb_h = track_h;
        float ty = (track_h - thumb_h) * (r->scroll_y / max_y);
        float track_x = (float)r->width - sb;
        cwv_draw_rectangle(track_x, 0, sb, track_h, track);
        cwv_draw_rectangle(track_x, ty, sb, thumb_h, thumb);
    }
    if (r->scrollbars_visible && sb_horizontal && max_x > 0.0f) {
        float track_w = (float)r->width - (sb_vertical ? sb : 0.0f);
        float content_w = renderer_content_width(r);
        float thumb_w = track_w * (viewport_w / content_w);
        if (thumb_w < 24.0f) thumb_w = 24.0f;
        if (thumb_w > track_w) thumb_w = track_w;
        float tx = (track_w - thumb_w) * (r->scroll_x / max_x);
        float track_y = (float)r->height - sb;
        cwv_draw_rectangle(0, track_y, track_w, sb, track);
        cwv_draw_rectangle(tx, track_y, thumb_w, sb, thumb);
    }

    /* (75) per-region scrollbars: thin bars inside each scroll region's
     * box, drawn AFTER the page chrome so they always sit on top of
     * the region content (page scrolling is never affected by them).
     * They appear only while the region can actually scroll on an
     * axis, mirroring overflow: auto. */
    if (!r->scrollbars_visible) return;
    for (int i = 0; i < r->node_count; i++) {
        CwvNode *n = &r->nodes[i];
        if (!n->scroll_region) continue;
        float z = r->zoom > 0.01f ? r->zoom : 1.0f;
        float origin_x = renderer_content_origin_x(r);
        float origin_y = renderer_content_origin_y(r);
        float sx = (n->x - origin_x) * z - r->scroll_x - n->scroll_off_x * z;
        float sy = (n->y - origin_y) * z - r->scroll_y - n->scroll_off_y * z;
        float sw = n->w * z, sh = n->h * z;
        const float rsb = 4.0f;
        const unsigned rtrack = rgba(238, 238, 238, 140);
        const unsigned rthumb = rgba(128, 128, 128, 200);
        if (n->scroll_max_y > 0.5f) {
            float content_h = n->h + n->scroll_max_y;
            float thumb_h = sh * (n->h / content_h);
            if (thumb_h < 16.0f) thumb_h = 16.0f;
            if (thumb_h > sh) thumb_h = sh;
            float ty = sy + (sh - thumb_h) * (n->scroll_top / n->scroll_max_y);
            float track_x = sx + sw - rsb;
            cwv_draw_rectangle(track_x, sy, rsb, sh, rtrack);
            cwv_draw_rectangle(track_x, ty, rsb, thumb_h, rthumb);
        }
        if (n->scroll_max_x > 0.5f) {
            float content_w = n->w + n->scroll_max_x;
            float thumb_w = sw * (n->w / content_w);
            if (thumb_w < 16.0f) thumb_w = 16.0f;
            if (thumb_w > sw) thumb_w = sw;
            float tx = sx + (sw - thumb_w) * (n->scroll_left / n->scroll_max_x);
            float track_y = sy + sh - rsb;
            cwv_draw_rectangle(sx, track_y, sw, rsb, rtrack);
            cwv_draw_rectangle(tx, track_y, thumb_w, rsb, rthumb);
        }
    }
}

/*
 * Optional CWV control modal.  This is deliberately native chrome: the page
 * only opts in with cwv-control-modal="Title" on a VCM element.  The modal
 * follows the same persistent VCM active state as :active CSS.
 */
static void draw_control_modal(CwvRenderer *r) {
    for (int i = 0; i < r->node_count; i++) {
        CwvNode *modal = &r->nodes[i];
        if (!modal->vcm_active || !modal->control_modal || !modal->vcm[0]) continue;
        float mw = 330.0f, mh = 68.0f;
        float mx = ((float)r->width - mw) * 0.5f;
        float my = (float)r->height - mh - 18.0f;
        if (my < 8.0f) my = 8.0f;
        unsigned mbg = r->theme_dark ? rgba(24, 27, 31, 245) : rgba(250, 250, 250, 245);
        unsigned mfg = r->theme_dark ? rgba(245, 247, 250, 255) : rgba(25, 28, 32, 255);
        unsigned maccent = rgba(70, 105, 150, 255);
        cwv_draw_rectangle(mx, my, mw, mh, mbg);
        cwv_draw_rectangle(mx, my, mw, 3.0f, maccent);
        cwv_pgf_draw_text(r->system_pgf, mx + 14.0f, my + 25.0f, mfg, 0.72f,
                             modal->control_modal_title[0] ? modal->control_modal_title
                                                           : "Vita Control");
        char hint[96];
        snprintf(hint, sizeof(hint), "VCM: %s", modal->vcm);
        cwv_pgf_draw_text(r->system_pgf, mx + 14.0f, my + 49.0f,
                             r->theme_dark ? rgba(190, 198, 208, 255)
                                           : rgba(80, 86, 94, 255),
                             0.58f, hint);
        break;
    }
}

/* Paint the whole frame. */
/* DPAD focus ring, painted as the LAST page pass so nothing can cover
 * it (siblings used to paint over the focused link's bottom edge). */
static void draw_focus_ring(CwvRenderer *r) {
    CwvNode *n;
    float z, sx, sy, sw, sh, ff;
    unsigned fc;
    if (r->focus_node < 0 || r->focus_node >= r->node_count) return;
    n = &r->nodes[r->focus_node];
    if (n->display_none) return;
    z = r->zoom > 0.01f ? r->zoom : 1.0f;
    /* (75) the ring follows the focused node through its ancestors'
     * scroll offsets; fixed nodes keep viewport pinning. */
    sx = (n->pos_kind == 3 ? (n->x - renderer_content_origin_x(r)) * z
                           : (n->x - renderer_content_origin_x(r)) * z
                             - r->scroll_x - n->scroll_off_x * z);
    sy = (n->pos_kind == 3 ? (n->y - renderer_content_origin_y(r)) * z
                           : (n->y - renderer_content_origin_y(r)) * z
                             - r->scroll_y - n->scroll_off_y * z);
    sw = n->w * z; sh = n->h * z;
    fc = rgba(255, 220, 70, 255);
    ff = 2.0f * z;
    cwv_draw_rectangle(sx - ff, sy - ff, sw + 2.0f * ff, ff, fc);
    cwv_draw_rectangle(sx - ff, sy + sh, sw + 2.0f * ff, ff, fc);
    cwv_draw_rectangle(sx - ff, sy, ff, sh, fc);
    cwv_draw_rectangle(sx + sw, sy, ff, sh, fc);
}

static int backend_draw(CwvBackend *backend) {
    CwvRenderer *r = backend->userdata;
    if (!r) return -1;
    renderer_update_scroll_regions(r);   /* (75): bake region state */
    cwv_clear_screen();
    cwv_draw_rectangle(0, 0, r->width, r->height, r->body_background);
    if (!r->loaded) return 0;
    /* Paint order: DOM order unless CSS z-index explicitly stacks.  A
       stable insertion sort keeps equal z-index nodes in document order,
       matching the browser's stacking behaviour. */
    int order[VW_MAX_NODES];
    int order_count = 0;
    for (int i = 0; i < r->node_count && i < VW_MAX_NODES; i++)
        order[order_count++] = i;
    for (int i = 1; i < order_count; i++) {
        int key = order[i], j = i - 1;
        while (j >= 0 && r->nodes[order[j]].z_index > r->nodes[key].z_index) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (int i = 0; i < order_count; i++)
        draw_node(r, &r->nodes[order[i]]);

    /* Chrome (focus ring, popups, scrollbars) never clips to a region. */
    if (r->clip_active) {
        cwv_clip_disable();
        r->clip_active = 0;
    }

    /* Focus ring LAST: nothing that painted after the focused element can
       hide it anymore. */
    draw_focus_ring(r);

    /* Lightweight select picker. It is rendered as native chrome rather than
       changing the page layout, like a browser's opened <select>. */
    draw_select_popups(r);
    draw_scrollbars(r);
    draw_control_modal(r);
    return 0;
}

/* ============================================================================
 * SECTION 13: THE BACKEND API (cwv_backend.h implementation)
 * ============================================================================
 * These are the functions main.c actually calls, through the
 * CwvBackendOps vtable (see the bottom of this file).  Everything above
 * was internal machinery; this is the public contract.
 * ==========================================================================*/

static int backend_init(CwvBackend *backend, int width, int height) {
    CwvRenderer *r = calloc(1, sizeof(*r));
    if (!r) return -1;
    r->width = width;
    r->height = height;
    r->zoom = 1.0f;
    r->overflow_x = 0;
    r->overflow_y = 0;
    r->theme_dark = 0;
    r->scrollbars_visible = 1;
    r->body_background = rgba(245, 245, 245, 255);
    r->body_color = rgba(25, 25, 30, 255);
    r->system_pgf = cwv_pgf_load_default();
    /* Custom TTF/OTF fonts use the console font handle; the system PGF remains the
       fallback. */
    backend->userdata = r;
    return 0;
}

/* Load a page. Only URLs under the app root (cwv_platform_url_root()) are
 * supported by this backend; anything else returns "not implemented" so
 * the host can fall back to another engine. */
static int backend_load(CwvBackend *backend, const char *url) {
    CwvRenderer *r = backend->userdata;
    if (!r || !url) return -1;
    r->pending_navigation[0] = 0;
    r->pending_target[0] = 0;

    /* Local pages live under the app root (app0: on Vita; the folder
       the package sits in on Wii U and Wii - see cwv_platform.h).
       Full-prefix compare with a fresh slice: keeping the separator
       slash would double it (the app0://<page> bug fixed in (27)). */
    const char *url_root = cwv_platform_url_root();
    size_t rl = strlen(url_root);
    if (strncmp(url, url_root, rl) != 0 || url[rl] != '/')
        return CWV_BACKEND_NOT_IMPLEMENTED;

    const char *resource = url + rl + 1;   /* after the root's slash */
    char path[1024];
    const char *end = strpbrk(resource, "?#");
    size_t len = end ? (size_t)(end - resource) : strlen(resource);
    size_t pl = strlen(cwv_platform_path_root());
    if (len + pl + 1 >= sizeof(path)) return -2;
    memcpy(path, cwv_platform_path_root(), pl);
    path[pl] = '/';
    memcpy(path + pl + 1, resource, len);
    path[len + pl + 1] = 0;

    free(r->html);
    r->html = NULL;
    js_shutdown(r);   /* a new page starts with a fresh JS context */
    /* The PREVIOUS page's decoded images and @font-face webfonts are
       page-scoped resources: free them before re-parsing so browsing
       cannot accumulate GPU/texture memory page over page (the Vita
       GXM heap exhaustion behind "images stop loading after
       navigating around", 2026-09-21 (29)).  The system pgf/font
       singletons are NOT owned by a page and stay open. */
    for (int i = 0; i < r->img_cache_count; i++)
        if (r->img_cache[i].tex) cwv_tex_free(r->img_cache[i].tex);
    r->img_cache_count = 0;
    for (int i = 0; i < r->font_count; i++)
        if (r->fonts[i].font) cwv_font_free(r->fonts[i].font);
    if (load_file(path, &r->html) < 0) return -3;
    snprintf(r->url, sizeof(r->url), "%s", url);
    base_dir(url, r->base_url, sizeof(r->base_url));
    r->rule_count = 0;
    r->font_count = 0;
    r->scroll_y = 0.0f;
    r->scroll_x = 0.0f;
    r->focus_node = -1;

    /* Collect CSS first, then build the node tree (which uses the rules). */
    load_external_css(r, r->html);
    parse_inline_styles(r, r->html);
#if !CWV_SCROLL_ENABLED
    /* Project scroll:off (package.json <console>.scroll=false): this rule
     * goes in AFTER the page's own sheets, so overflow: hidden wins and
     * every scroll input clamps to 0 (2026-09-23 (39)). */
    css_project_force_no_scroll(r);
#endif
    cwv_css_set_viewport((float)r->width, (float)r->height); /* vw/vh (69) */
    parse_html(r);
    /* Finally run the page's <script> blocks (no-op for script-free pages:
       the JS context is only created when there is actually code to run). */
    js_harvest_and_run(r);
    r->loaded = 1;
    return 0;
}

/* Evaluate JavaScript in the page's JS context (Duktape, via
 * cwv_js.c).  Creates the context on first use if needed. */
static int backend_eval(CwvBackend *backend, const char *javascript) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (!r || !javascript) return -1;
    return js_eval(r, javascript);
}

/* Called every frame: poll the IME keyboard when it is open. */
static int backend_update(CwvBackend *backend) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (r) {
        renderer_textfield_update(r);
        js_tick(r);   /* run due setTimeout/setInterval callbacks, if any */
    }
    return 0;
}

/* DPAD focus cycling; when a <select> is open, the cycle moves the selection
 * instead of moving focus. */
static int backend_focus_cycle(CwvBackend *backend, int direction) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    if (r->focus_node >= 0 && r->focus_node < r->node_count) {
        CwvNode *n = &r->nodes[r->focus_node];
        if (n->is_select && n->select_open) {
            select_set_index(r, n, n->select_index + (direction > 0 ? 1 : -1));
            return 1;
        }
    }
    return renderer_focus_cycle(r, direction);
}

static int backend_focus_clear(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r) return -1;
    r->focus_node = -1;
    r->select_focus_node = -1;
    return 0;
}

static int backend_focus_activate(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return renderer_focus_activate(r);
}

static int backend_control_mode(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? r->control_mode : 0;
}

/* Activate a Vita Control Modal (VCM) binding by name, e.g. "cross".
 * A VCM is a single active page control: activating one removes :active from
 * the previous one. */
static int backend_vcm_activate(CwvBackend *backend, const char *control) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r || !control || !*control) return 0;
    int matched = -1;
    for (int i = 0; i < r->node_count; i++) {
        CwvNode *n = &r->nodes[i];
        if (n->vcm[0] && ci_eq(n->vcm, control)) { matched = i; break; }
    }
    if (matched < 0) return 0;

    for (int i = 0; i < r->node_count; i++) r->nodes[i].vcm_active = 0;
    CwvNode *n = &r->nodes[matched];
    n->vcm_active = 1;
    r->focus_node = matched;

    if (n->is_select) { n->select_open = !n->select_open; return 1; }
    if (n->is_textfield) { return renderer_textfield_open(r, matched); }
    /* A JS "activate" listener on the bound element runs (this is how a
       Vita button combo can trigger page script); without listeners the
       behavior is the unchanged default. */
    if (n->is_button) { n->button_pressed = 1; js_notify_activate(r, matched); return 1; }
    if (n->is_link && n->href[0]) {
        if (js_has_listener(r, matched)) {
            js_notify_activate(r, matched);
            return 1;
        }
        snprintf(r->pending_navigation, sizeof(r->pending_navigation), "%s", n->href);
        snprintf(r->pending_target, sizeof(r->pending_target), "%s",
                 n->link_target[0] ? n->link_target : "app");
        return 1;
    }
    /* Non-button, non-link elements (e.g. a plain vcm-bound <div>) can still
       carry a JS listener for the combo. */
    js_notify_activate(r, matched);
    return 1;
}

static const char *backend_navigation_url(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? r->pending_navigation : "";
}

static const char *backend_navigation_target(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    return r ? r->pending_target : "";
}

int cwv_backend_clear_focus(CwvBackend *backend) {
    return backend_focus_clear(backend);
}

/* Switch the light/dark theme and re-propagate the page's default text
 * colors (explicit CSS colors are kept). */
int cwv_backend_set_theme(CwvBackend *backend, int dark) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    int want, changed;
    if (!r) return -1;
    want = dark ? 1 : 0;
    changed = (r->theme_dark != want);   /* no-op calls must not re-stomp */
    r->theme_dark = want;
    /* On unchanged per-frame calls, do not reapply even body defaults:
     * script/page-owned colors must stay stable between renders.  HTML
     * parsing itself restores defaults when navigating to a new page. */
    if (changed && !r->body_background_explicit)
        r->body_background = r->theme_dark ? rgba(0, 0, 0, 255) : rgba(245, 245, 245, 255);
    if (changed && !r->body_color_explicit)
        r->body_color = r->theme_dark ? rgba(235, 238, 242, 255) : rgba(25, 25, 30, 255);
    /* Only a REAL theme change re-propagates the default text colors.
     * main.c calls set_theme every frame (apply_runtime_overrides), and
     * re-running this pass on an unchanged theme would revert every JS
     * style change (el.style.cssText) within one frame - the "green for
     * less than a second" bug. */
    if (changed) {
        unsigned text_color = r->theme_dark ? rgba(235, 238, 242, 255) : rgba(25, 25, 30, 255);
        for (int i = 0; i < r->node_count; i++) {
            CwvNode *n = &r->nodes[i];
            /* A browser never lets the app theme overrule the page's own
               cascade.  Nodes that carry an EXPLICIT color (CSS rule,
               style attribute, JS cssText) are skipped - and so are nodes
               that INHERITED an explicit color (body { color:#f8fafc }
               flows into every child at parse time; those children are
               page-colored, not theme-colored).  Re-tinting them was the
               es6 "green text flashes to black" bug (2026-09-21 (29)):
               a light-mode pass repainted inherited #f8fafc text to
               rgba(25,25,30) - invisible on the dark page. */
            if (n->is_button && !n->color_explicit && !n->color_inherited_explicit)
                n->color = rgba(0, 0, 0, 255);
            else if (!n->color_explicit && !n->color_inherited_explicit)
                n->color = text_color;
        }
    }
    return 0;
}

int cwv_backend_set_scrollbars(CwvBackend *backend, int visible) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (!r) return -1;
    r->scrollbars_visible = visible ? 1 : 0;
    return 0;
}

static void backend_shutdown(CwvBackend *backend) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (!r) return;
    /* All decoded image textures are owned by the renderer's image cache
       (2026-09-21 (29)); node.image pointers borrow from it, so the table
       is the one place to free them.  bg_image textures borrow from it
       the same way. */
    for (int i = 0; i < r->img_cache_count; i++)
        if (r->img_cache[i].tex) cwv_tex_free(r->img_cache[i].tex);
    r->img_cache_count = 0;
    for (int i = 0; i < r->font_count; i++) {
        if (r->fonts[i].font) cwv_font_free(r->fonts[i].font);
    }
    if (r->system_pgf) cwv_pgf_free(r->system_pgf);
    js_shutdown(r);
    free(r->html);
    free(r);
    backend->userdata = NULL;
}

/* Zoom in the 0.75x..2x range, anchored at the viewport centre so the area
 * under the user's view stays stable. */
int cwv_backend_set_zoom(CwvBackend *backend, float zoom) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (!r) return -1;
    if (zoom < 0.75f) zoom = 0.75f;
    if (zoom > 2.0f) zoom = 2.0f;
    float old = r->zoom > 0.01f ? r->zoom : 1.0f;
    if (fabsf(zoom - old) > 0.0001f) {
        /* Zoom around the viewport centre instead of the page's top-left.
           This keeps the area under the user's view stable and makes the
           already-visible left side reachable again after zooming. */
        float ox = renderer_content_origin_x(r);
        float oy = renderer_content_origin_y(r);
        float center_doc_x = ox + (r->scroll_x + (float)r->width * 0.5f) / old;
        float center_doc_y = oy + (r->scroll_y + (float)r->height * 0.5f) / old;
        r->zoom = zoom;
        r->scroll_x = (center_doc_x - ox) * zoom - (float)r->width * 0.5f;
        r->scroll_y = (center_doc_y - oy) * zoom - (float)r->height * 0.5f;
    } else {
        r->zoom = zoom;
    }
    float max = renderer_max_scroll(r);
    if (r->scroll_y < 0.0f) r->scroll_y = 0.0f;
    if (r->scroll_y > max) r->scroll_y = max;
    float max_x = renderer_max_scroll_x(r);
    if (r->scroll_x < 0.0f) r->scroll_x = 0.0f;
    if (r->scroll_x > max_x) r->scroll_x = max_x;
    return 0;
}

float cwv_backend_get_zoom(CwvBackend *backend) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    return r ? (r->zoom > 0.01f ? r->zoom : 1.0f) : 1.0f;
}

static const char *backend_button_wait(CwvBackend *backend) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    if (!r || !r->button_wait[0]) return "";
    return r->button_wait;
}

/* The vtable main.c receives via cwv_backend_default_ops().  The ABI is
 * intentionally isolated from CwvHost: the initial implementation can be
 * replaced wholesale without changing the wrapper. */
static const CwvBackendOps g_default_ops = {
    backend_init,
    backend_load,
    backend_eval,
    backend_update,
    backend_draw,
    backend_pointer_event,
    backend_cursor_at,
    backend_pointer_cancel,
    backend_scroll_by,
    backend_scroll_to,
    backend_scroll_offset,
    backend_scroll_x_by,
    backend_scroll_at,
    backend_scroll_x_at,
    backend_scroll_page_x_by,
    backend_scroll_x_offset,
    backend_content_width,
    backend_content_height,
    backend_focus_cycle,
    backend_focus_activate,
    backend_vcm_activate,
    backend_control_mode,
    backend_button_wait,
    backend_navigation_url,
    backend_navigation_target,
    backend_shutdown,
};

/* ---- JavaScript support (public API; see cwv_backend.h) ------------- */

void cwv_backend_console_stats(CwvBackend *backend, int *errors,
                                    int *warns, int *lines) {
    CwvRenderer *r = (CwvRenderer *)backend->userdata;
    js_console_stats(r, errors, warns, lines);
}

int cwv_backend_console_dump(CwvBackend *backend, char *out, size_t cap) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    return js_console_dump(r, out, cap);
}

int cwv_backend_js_active(CwvBackend *backend) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    return js_is_active(r);
}

const char *cwv_backend_js_last_error(CwvBackend *backend) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    return js_console_last_error(r);
}

void cwv_backend_set_ts_transform(CwvBackend *backend,
                                  int (*transform)(const char *in, char *out,
                                                   size_t cap, void *userdata),
                                  void *userdata) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    js_set_ts_transform(r, transform, userdata);
}

void cwv_backend_set_source_transform(CwvBackend *backend,
                                           CwvSourceTransformFn fn,
                                           void *userdata) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    js_set_source_transform(r, fn, userdata);
}

void cwv_backend_set_runtime_log(CwvBackend *backend,
                                      void (*fn)(const char *fmt, ...)) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    if (r) r->runtime_log = fn;
}

void cwv_backend_set_js_prelude(CwvBackend *backend,
                                     CwvJsPreludeFn fn, void *userdata) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    js_set_prelude_hook(r, fn, userdata);
}

/* ESM SELF-TEST (see the header comment).  A tiny ES6 probe is pushed
 * through the SAME hooks the renderer uses for real page scripts, so the
 * test exercises the identical call path - if the module's exports were
 * silently stubbed (Vita3K), step 1 fails immediately and the caller can
 * report it loudly instead of letting every page run raw ES6. */
int cwv_backend_esm_selftest(CwvBackend *backend, char *report, size_t cap)
{
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    static const char probe[] =
        "let vw_esm_probe = (a) => { return a + 1; };"
        "`probe ${vw_esm_probe(1)}`;";
    char es5[2048];
    size_t pos = 0;
    int ok = 1, trc;

    if (report && cap) report[0] = 0;

#define VW_ADD(...) do { \
        if (report && cap > pos) { \
            int n_ = snprintf(report + pos, cap - pos, __VA_ARGS__); \
            if (n_ > 0 && (size_t)n_ < cap - pos) pos += (size_t)n_; \
        } \
    } while (0)

    /* 1. transform probe: the module MUST report a real rewrite. */
    if (!r || !r->source_transform) {
        VW_ADD("transform=NONE(module not registered)");
        return 0;
    }
    trc = r->source_transform(probe, es5, sizeof(es5),
                              r->source_transform_userdata);
    if (trc == 1 && strstr(es5, "function") &&
        !strstr(es5, "let ") && !strstr(es5, "=>")) {
        VW_ADD("transform=PASS");
    } else {
        ok = 0;
        VW_ADD("transform=FAIL(rc=%d%s%s)", trc,
               trc == 0 ? " module answered 'nothing to transform'" : "",
               trc == 1 ? " module returned unconverted output" : "");
    }

    /* 2. compile probe: the rewritten source must be valid ES5.1. */
    {
        duk_context *ctx = duk_create_heap_default();
        duk_push_string(ctx, trc == 1 ? es5 : probe);
        duk_push_string(ctx, "esm-selftest.js");
        if (duk_pcompile(ctx, 0) == 0) {
            VW_ADD(" compile=PASS");
        } else {
            ok = 0;
            VW_ADD(" compile=FAIL(%.80s)", duk_safe_to_string(ctx, -1));
        }
        duk_destroy_heap(ctx);
    }

    /* 3. prelude probe: the ES6 library must be installable. */
    if (r->prelude_hook) {
        const char *p = r->prelude_hook(r->prelude_userdata);
        if (p && p[0] && strstr(p, "padStart") && strstr(p, "Map"))
            VW_ADD(" prelude=PASS");
        else { ok = 0; VW_ADD(" prelude=FAIL"); }
    } else {
        ok = 0;
        VW_ADD(" prelude=NONE");
    }

#undef VW_ADD
    return ok;
}

void cwv_backend_set_fetch_hook(CwvBackend *backend,
                                     CwvFetchHook fn, void *userdata) {
    CwvRenderer *r = backend ? (CwvRenderer *)backend->userdata : NULL;
    js_set_fetch_hook(r, fn, userdata);
}

const CwvBackendOps *cwv_backend_default_ops(void) {
    return &g_default_ops;
}
