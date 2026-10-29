#ifndef CWV_ARGS_H
#define CWV_ARGS_H

/* Optional experimental runtime capabilities requested by package.json.
 * The parser is deliberately tiny and allocation-free: CWebView targets
 * consoles where a heavyweight JSON dependency would be undesirable.
 *
 * This is NOT a general JSON parser. It finds the exact root-level key
 * "cwv-args" and accepts a quoted, space-separated string. Keep values
 * simple, for example: "beta-enable-canvas beta-enable-webgl".
 * Unknown switches are ignored by the parser and can be diagnosed by the
 * caller. A parsed switch is only a request; it does not imply that the
 * corresponding browser API has been implemented.
 */
#include <stddef.h>
#include <string.h>
#include <ctype.h>

#define CWV_ARG_CANVAS (1u << 0)
#define CWV_ARG_WEBGL  (1u << 1)
#define CWV_ARG_KNOWN (CWV_ARG_CANVAS | CWV_ARG_WEBGL)

static unsigned cwv_args_parse(const char *value)
{
    unsigned flags = 0;
    const char *p = value;
    if (!p) return 0;
    while (*p) {
        const char *start;
        size_t len;
        while (*p && isspace((unsigned char)*p)) p++;
        start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        len = (size_t)(p - start);
        if (len == sizeof("beta-enable-canvas") - 1 &&
            memcmp(start, "beta-enable-canvas", len) == 0)
            flags |= CWV_ARG_CANVAS;
        else if (len == sizeof("beta-enable-webgl") - 1 &&
                 memcmp(start, "beta-enable-webgl", len) == 0)
            flags |= CWV_ARG_WEBGL;
    }
    return flags;
}

/* Extract the value of the exact root-level cwv-args property from a
 * manifest. This intentionally handles only a plain quoted string; escaped
 * JSON strings are rejected rather than partially interpreted. Returns 1
 * when found, 0 when absent/invalid. */
static int cwv_args_manifest_value(const char *json, char *out, size_t cap)
{
    const char *p, *key = "\"cwv-args\"";
    size_t key_len = sizeof("\"cwv-args\"") - 1;
    if (!json || !out || cap == 0) return 0;
    out[0] = '\0';
    p = json;
    while ((p = strstr(p, key)) != NULL) {
        const char *q = p + key_len;
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q++ != ':') { p += key_len; continue; }
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q++ != '"') return 0;
        {
            size_t n = 0;
            while (*q && *q != '"') {
                if (*q == '\\' || n + 1 >= cap) return 0;
                out[n++] = *q++;
            }
            if (*q != '"') return 0;
            out[n] = '\0';
            return 1;
        }
    }
    return 0;
}

#endif /* CWV_ARGS_H */
