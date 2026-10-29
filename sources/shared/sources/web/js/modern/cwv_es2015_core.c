/* cwv_es2015_core.c - modern-JS handling pipeline. See cwv_es2015_core.h
 * for the feature list and the rules.
 *
 * READING GUIDE (this is the only tricky file in the module):
 *
 * The transformer is a single forward pass with a tiny state machine.
 * The golden rule of every JS minifier/transpiler applies: never rewrite
 * anything that lives inside a string, a comment, a template literal or a
 * regex.  So the pass tracks which of those contexts it is in (skip_text()),
 * and only rewrites tokens seen in "normal code" mode.
 *
 * Three things need lookahead, and each has a small trick:
 *   - `(a, b) => ...`: by the time we see "=>" the "(a, b)" is already in
 *     the output buffer.  Solution: remember where each "(" landed in the
 *     OUTPUT; when the matching ")" is followed by "=>", squeeze the word
 *     "function " in front of that recorded position.
 *   - `a => ...`: same idea, but the backtrack finds an identifier.
 *   - template literals: converted by their own helper so `${...}` pieces
 *     can be recursively transformed (an arrow can hide inside ${...}).
 */

#include "web/js/modern/cwv_es2015_core.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* --- output buffer -------------------------------------------------------- */

typedef struct {
    char  *p;
    size_t cap, len;
    int    overflow;
} Buf;

static void bput(Buf *b, char c) {
    if (b->len + 1 >= b->cap) { b->overflow = 1; return; }
    b->p[b->len++] = c;
}

static void bputs(Buf *b, const char *s) {
    while (*s) bput(b, *s++);
}

static void bputn(Buf *b, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) bput(b, s[i]);
}

/* Insert text at an earlier position, shifting the rest right. */
static int binsert(Buf *b, size_t at, const char *text) {
    size_t tl = strlen(text), need = b->len + tl;
    if (need + 1 >= b->cap) { b->overflow = 1; return -1; }
    memmove(b->p + at + tl, b->p + at, b->len - at);
    memcpy(b->p + at, text, tl);
    b->len += tl;
    return 0;
}

/* Truncate the buffer back to a shorter length. */
static void btruncate(Buf *b, size_t len) {
    if (len <= b->len) b->len = len;
}

/* --- import tracking (cycle guard) ---------------------------------------- */

#define MAX_LOADING 8
static char g_loading[MAX_LOADING][256];
static int  g_loading_count;
static int  g_depth;              /* > 0 while inside a nested transform */

/* --- scanning helpers ----------------------------------------------------- */

static int is_ident_char(int c) { return isalnum((unsigned char)c) || c == '_' || c == '$'; }
static int is_ident_start(int c) { return isalpha((unsigned char)c) || c == '_' || c == '$'; }

/* Copy a string/comment/template/regex literal verbatim from p, honoring
 * escapes.  `q` is the opening delimiter ( ' " / ` ), `raw` ends it ( ' " /
 * ` ).  Returns the position after the closing delimiter, or NULL on EOL
 * error (unterminated literal: just stop).  Template literals are handled
 * by the caller (convert_template), NOT here. */
static const char *skip_literal(const char *p, char open, char close,
                                int allow_newline) {
    (void)open;   /* not needed: the caller already verified the opener */
    p++;                                    /* past the opening delimiter */
    while (*p) {
        if (*p == '\\') {
            p++;                      /* past the backslash ...        */
            if (!*p) break;          /* ...unless it ended the source */
            p++;                     /* skip the escaped character    */
            continue;
        }
        if (*p == close) return p + 1;
        if (!allow_newline && (*p == '\n' || *p == '\r')) break;
        p++;
    }
    return p;
}

/* Copy a regex literal "/.../flags" starting at p. */
static const char *skip_regex(const char *p) {
    p++;
    while (*p) {
        if (*p == '\\') {
            p++;                      /* past the backslash ...        */
            if (!*p) break;          /* ...unless it ended the source */
            p++;                     /* skip the escaped character    */
            continue;
        }
        if (*p == '\n' || *p == '\r') break;   /* not a regex after all */
        if (*p == '/') {                      /* closing slash */
            p++;
            while (is_ident_char((unsigned char)*p)) p++;   /* flags */
            return p;
        }
        p++;
    }
    return p;
}

/* Would a "/" at this point start a regex (vs. division)?  Standard
 * heuristic: regexes follow operators, keywords and openers. */
static int regex_position(Buf *b) {
    size_t i = b->len;
    char last = 0;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i == 0) return 1;
    last = b->p[i - 1];
    if (strchr("(,=:[!&|?{};+-*%~^<>\n", last)) return 1;
    if (isalpha((unsigned char)last) || last == '_') {
        /* keyword before the slash? ("return", "typeof", "case", ...) */
        size_t j = i;
        while (j > 0 && is_ident_char((unsigned char)b->p[j - 1])) j--;
        static const char *kw[] = { "return", "typeof", "case", "in", "of",
                                    "new", "delete", "void", "do", "else", 0 };
        int k;
        for (k = 0; kw[k]; k++) {
            size_t kl = strlen(kw[k]);
            if (i - j == kl && !strncmp(b->p + j, kw[k], kl)) return 1;
        }
    }
    return 0;
}

/* --- template literal -> string concatenation ----------------------------- */

/* Recursive lexical pass (forward declaration; the arrow logic uses it for
 * ${...} pieces so an arrow inside a template still gets converted). */
static const char *lex_pass(const char *p, Buf *b, const VwEsHooks *hooks,
                            int depth);

/* Convert one `...${expr}...` template.  The opening backtick is at p[-1];
 * p points AT the backtick (call: p = convert_template(p, b, hooks)). */
static const char *convert_template(const char *p, Buf *b,
                                    const VwEsHooks *hooks, int depth) {
    (void)depth;  /* recursion depth is capped by the caller, not here */
    bputs(b, "(\"");
    p++;                                       /* past the backtick */
    while (*p && *p != '`') {
        if (*p == '\\') {                      /* escape: keep, but make the
                                                 char inside-safe for " " */
            char c = p[1];
            if (c == '"') bputs(b, "\\\"");
            else if (c == '\n') bputs(b, "\\n");
            else if (c == '\r') { }
            else if (c == '`') bputs(b, "`");
            else { bput(b, '\\'); if (c) { bput(b, c); p++; } }
            p += 2;
            continue;
        }
        if (*p == '\n') { bputs(b, "\\n"); p++; continue; }
        if (*p == '\r') { p++; continue; }
        if (*p == '"') { bputs(b, "\\\""); p++; continue; }
        if (*p == '$' && p[1] == '{') {
            /* --- substitution: recursively lex the inner expression --- */
            const char *q = p + 2;
            int nest = 1;
            char *expr;
            size_t n;
            while (*q && nest > 0) {
                if (*q == '{' || *q == '(' || *q == '[') nest++;
                else if (*q == '}' || *q == ')' || *q == ']') {
                    if (*q == '}' && nest == 1) break;
                    nest--;
                } else if (*q == '\'' || *q == '"')
                    q = skip_literal(q, *q, *q, 0) - 1;
                else if (*q == '/' && 0) { } /* regex in templates: skip v1 */
                q++;
            }
            n = (size_t)(q - (p + 2));
            expr = (char *)malloc(n + 1);
            if (!expr) { b->overflow = 1; return p; }
            memcpy(expr, p + 2, n);
            expr[n] = 0;
            /* convert the inner expression through the same pass */
            {
                char *out = (char *)malloc(n * 6 + 256);
                if (out) {
                    out[0] = 0;
                    if (vw_es2015_transform(expr, out, n * 6 + 256, hooks) == 1)
                        ;
                    else
                        snprintf(out, n * 6 + 256, "%s", expr);
                    bputs(b, "\" + (");
                    bputs(b, out);
                    bputs(b, ") + \"");
                    free(out);
                } else b->overflow = 1;
            }
            free(expr);
            p = (*q == '}') ? q + 1 : q;
            continue;
        }
        bput(b, *p++);
    }
    if (*p == '`') p++;
    bputs(b, "\")");
    return p;
}


/* --- for..of -> indexed loop ---------------------------------------------- */

/* Skip a template literal (backtick ... backtick) in a source scan. */
static const char *skip_template_raw(const char *p) {
    p++;                                       /* past the backtick */
    while (*p) {
        if (*p == '\\') { p++; if (*p) p++; continue; }
        if (*p == '`') return p + 1;
        p++;
    }
    return p;
}

/* Scan p forward to the ")" that closes the current group (nest starts at
 * 0, so the first ")" found at depth 0 ends it).  String/comment/template
 * aware.  Returns the position of that ")" (or end of source). */
static const char *scan_to_close_paren(const char *p) {
    int nest = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = skip_literal(p, *p, *p, 0); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) { p += 2; }   /* past the closing "/"        */
            continue;
        }
        if (*p == '`') { p = skip_template_raw(p); continue; }
        if (*p == '(' || *p == '[' || *p == '{') nest++;
        else if (*p == ')') { if (nest == 0) return p; nest--; }
        else if (*p == ']' || *p == '}') { if (nest > 0) nest--; }
        p++;
    }
    return p;
}

/* Scan past a balanced "{ ... }" block (p AT the "{").  Returns the
 * position after the matching "}", or end of source. */
static const char *scan_brace_block(const char *p) {
    int nest = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = skip_literal(p, *p, *p, 0); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) { p += 2; }   /* past the closing "/"        */
            continue;
        }
        if (*p == '`') { p = skip_template_raw(p); continue; }
        if (*p == '{') nest++;
        else if (*p == '}') { nest--; if (nest == 0) return p + 1; }
        p++;
    }
    return p;
}

/* `for (const x of LIST) BODY`  (also `let x`, `var x`, or no declaration)
 * becomes:
 *   for (var __vw_s<N> = LIST, __vw_i<N> = 0;
 *        __vw_i<N> < __vw_s<N>.length; __vw_i<N>++) {
 *     var x = __vw_s<N>[__vw_i<N>]; BODY
 *   }
 * The iterable is captured ONCE (real for..of semantics) and the body is
 * recursively transformed, so arrows inside it still convert.  Works for
 * arrays and strings; returns 0 when the output tail is not a for-header
 * (so `of` used as a plain word is never touched).
 * Unique variable suffixes make nested for..of loops independent. */
static int try_for_of(Buf *b, const char **pp, const VwEsHooks *hooks,
                      int depth) {
    const char *p = *pp;
    size_t e, s, hs, fpos;
    char ident[128];
    int has_decl = 0;
    const char *q, *bs, *be;
    size_t n;
    char *iter, *iter_es5, *body, *body_es5;
    int braced;
    static int seq = 0;
    char sv[32], iv[32];
    int my_seq;

    /* 1. The output tail must be:  for ( [var] IDENT <ws>            */
    e = b->len;
    while (e > 0 && isspace((unsigned char)b->p[e - 1])) e--;
    s = e;
    while (s > 0 && is_ident_char((unsigned char)b->p[s - 1])) s--;
    if (s == e || e - s >= sizeof(ident)) return 0;      /* no identifier */
    memcpy(ident, b->p + s, e - s);
    ident[e - s] = 0;
    hs = s;
    while (hs > 0 && isspace((unsigned char)b->p[hs - 1])) hs--;
    if (hs >= 3 && !strncmp(b->p + hs - 3, "var", 3) &&
        (hs - 3 == 0 || !is_ident_char((unsigned char)b->p[hs - 4]))) {
        has_decl = 1;
        hs -= 3;
        while (hs > 0 && isspace((unsigned char)b->p[hs - 1])) hs--;
    }
    if (hs == 0 || b->p[hs - 1] != '(') return 0;
    hs--;
    while (hs > 0 && isspace((unsigned char)b->p[hs - 1])) hs--;
    if (hs < 3 || strncmp(b->p + hs - 3, "for", 3) != 0) return 0;
    if (hs - 3 > 0 && is_ident_char((unsigned char)b->p[hs - 4])) return 0;
    fpos = hs - 3;

    /* 2. The source at p is:  of ITER ) BODY                        */
    if (!(p[0] == 'o' && p[1] == 'f')) return 0;
    p += 2;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    q = scan_to_close_paren(p);
    if (*q != ')') return 0;
    n = (size_t)(q - p);
    iter = (char *)malloc(n + 1);
    if (!iter) return 0;
    memcpy(iter, p, n);
    iter[n] = 0;

    /* 3. Body: braced block or single statement up to ";"            */
    bs = q + 1;
    while (*bs == ' ' || *bs == '\t' || *bs == '\n' || *bs == '\r') bs++;
    braced = (*bs == '{');
    if (braced) {
        be = scan_brace_block(bs);
    } else {
        /* single statement: to the next ";" at depth 0 */
        int nest2 = 0;
        const char *r = bs;
        while (*r) {
            if (*r == '\'' || *r == '"') { r = skip_literal(r, *r, *r, 0); continue; }
            if (r[0] == '/' && r[1] == '/') { while (*r && *r != '\n') r++; continue; }
            if (r[0] == '/' && r[1] == '*') {
                r += 2; while (*r && !(r[0] == '*' && r[1] == '/')) r++;
                if (*r) { r += 2; }
                continue;
            }
            if (*r == '`') { r = skip_template_raw(r); continue; }
            if (*r == '(' || *r == '[' || *r == '{') nest2++;
            else if (*r == ')' || *r == ']' || *r == '}') { if (nest2 == 0) break; nest2--; }
            else if (nest2 == 0 && *r == ';') { r++; break; }
            r++;
        }
        be = r;
    }
    n = (size_t)(be - bs);
    body = (char *)malloc(n + 1);
    if (!body) { free((void *)iter); return 0; }
    memcpy(body, bs, n);
    body[n] = 0;

    /* 4. Recursively transform ITER and BODY (arrows may hide inside) */
    iter_es5 = (char *)malloc(n * 6 + 4096);   /* reuses n; generous */
    body_es5 = (char *)malloc(strlen(body) * 6 + 4096);
    if (!iter_es5 || !body_es5) {
        free((void *)iter); free((void *)body); free(iter_es5); free(body_es5);
        return 0;
    }
    if (vw_es2015_transform(iter, iter_es5, (size_t)(q - p) * 6 + 4096,
                         hooks) != 1)
        snprintf(iter_es5, (size_t)(q - p) * 6 + 4096, "%s", iter);
    if (vw_es2015_transform(body, body_es5, strlen(body) * 6 + 4096,
                         hooks) != 1)
        snprintf(body_es5, strlen(body) * 6 + 4096, "%s", body);
    free((void *)iter);
    free((void *)body);

    /* 5. Rewrite: chop the old "for (var x " and emit the ES5 loop    */
    my_seq = seq++;
    snprintf(sv, sizeof(sv), "__vw_s%d", my_seq);
    snprintf(iv, sizeof(iv), "__vw_i%d", my_seq);
    btruncate(b, fpos);
    bputs(b, "for (var ");
    bputs(b, sv);
    bputs(b, " = ");
    bputs(b, iter_es5);
    bputs(b, ", ");
    bputs(b, iv);
    bputs(b, " = 0; ");
    bputs(b, iv);
    bputs(b, " < ");
    bputs(b, sv);
    bputs(b, ".length; ");
    bputs(b, iv);
    bputs(b, "++) { ");
    if (has_decl) bputs(b, "var ");
    bputs(b, ident);
    bputs(b, " = ");
    bputs(b, sv);
    bputs(b, "[");
    bputs(b, iv);
    bputs(b, "]; ");
    bputs(b, body_es5);
    bputs(b, braced ? " }" : "; }");

    free(iter_es5);
    free(body_es5);
    *pp = be;
    (void)depth;
    return 1;
}


/* ====================================================================== */
/* ES6+ COMPLETION (2026-09-26 (47)): class, async/await, destructuring,   */
/* spread/rest, default params, method shorthand, **, ?., ??, logical     */
/* assignment, optional catch.  Everything here feeds the SAME output      */
/* buffer as the original pass; features that need lookahead backtrack     */
/* into the output exactly like try_for_of does.  Anything the code        */
/* cannot prove it handles is copied VERBATIM (and produces an engine      */
/* syntax error) rather than mis-compiled - the policy the whole          */
/* transformer has always followed.                                       */
/* ====================================================================== */

/* --- small utilities -------------------------------------------------- */

static int g_temp_seq;          /* unique __vw_* temp names, reset per call */

static char *dupn(const char *a, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, a, n);
    d[n] = 0;
    return d;
}

/* Recursive transform of a source piece with verbatim fallback:
   the module's standard policy (see try_for_of / arrow_body_emit). */
static void xform_piece(const char *src, Buf *b, const VwEsHooks *hooks) {
    size_t n = strlen(src);
    char *dst = (char *)malloc(n * 6 + 4096);
    if (!dst) { bputs(b, src); return; }
    if (vw_es2015_transform(src, dst, n * 6 + 4096, hooks) != 1)
        snprintf(dst, n * 6 + 4096, "%s", src);
    bputs(b, dst);
    free(dst);
}

/* String/comment/template-aware scan from p (AT an opening bracket) to
 * the position AFTER the matching closing bracket, or end of source. */
static const char *scan_balanced(const char *p) {
    char open = *p, close = (open == '{') ? '}' : (open == '[') ? ']' : ')';
    int nest = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = skip_literal(p, *p, *p, 0); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (*p == '`') { p = skip_template_raw(p); continue; }
        if (*p == open) nest++;
        else if (*p == close) { nest--; if (nest == 0) return p + 1; }
        p++;
    }
    return p;
}

/* Skip whitespace in a source scan. */
static const char *skipws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Skip an identifier; returns p unchanged if there is none. */
static const char *skip_ident(const char *p) {
    if (!is_ident_start((unsigned char)*p)) return p;
    while (is_ident_char((unsigned char)*p)) p++;
    return p;
}

/* Is the source at p the given word, with a non-ident char after? */
static int src_word(const char *p, const char *w) {
    size_t n = strlen(w);
    if (strncmp(p, w, n) != 0) return 0;
    return !is_ident_char((unsigned char)p[n]);
}

/* --- operand backtrack (for **, ??, ?.) -------------------------------- */

/* Find the start of the trailing "primary expression" in the output:
   ident / member chains, numeric literals, and balanced groups.  Used
   to grab the LEFT operand of **, ?? and ?. which we must wrap. */
static size_t operand_start(Buf *b) {
    size_t i = b->len;
    int guard = 64;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    while (i > 0 && guard-- > 0) {
        char c = b->p[i - 1];
        if (is_ident_char((unsigned char)c)) {
            while (i > 0 && is_ident_char((unsigned char)b->p[i - 1])) i--;
            if (i > 0 && b->p[i - 1] == '.') { i--; continue; }   /* member */
            return i;
        }
        if (c >= '0' && c <= '9') {
            while (i > 0 && (isalnum((unsigned char)b->p[i - 1]) ||
                             b->p[i - 1] == '.')) i--;
            return i;
        }
        if (c == ')' || c == ']') {
            char close = c, open = (c == ')') ? '(' : '[';
            int nest = 0;
            while (i > 0) {
                char d = b->p[i - 1];
                if (d == close) nest++;
                else if (d == open) { nest--; if (nest == 0) { i--; break; } }
                else if (d == '\'' || d == '"') {
                    char q = d;
                    i--;
                    while (i > 0 && b->p[i - 1] != q) i--;
                    if (i > 0) i--;
                    continue;
                }
                i--;
            }
            /* the group may be a call/index: keep the ident before it */
            if (i > 0 && is_ident_char((unsigned char)b->p[i - 1])) continue;
            return i;
        }
        break;      /* anything else: operand starts here */
    }
    return i;
}

/* --- destructuring pattern compiler ----------------------------------- */

/* Emits assignment statements that destructure `access` (a C expression
 * string producing the value: a temp name) into the pattern at *pp.
 * Returns 0 on success, -1 if the pattern uses something unsupported
 * (computed keys, junk).  Targets: identifiers and simple member chains.
 * Supported: nesting, defaults, array rest (...r), object rest {...r}.
 * NO computed keys - they make the whole destructure fail (verbatim). */
static int destructure_emit(const char **pp, const char *access,
                            Buf *out, const VwEsHooks *hooks, int decl) {
    const char *p = *pp;
    char tmp[32];
    int n;
    if (*p == '[') {
        const char *end = scan_balanced(p);
        const char *q = p + 1;
        int idx = 0;
        if (end[-1] != ']' && end[-1] != '}' && end[-1] != ')') return -1;
        while (q < end - 1) {
            char acc[32];
            q = skipws(q);
            if (q >= end - 1) break;
            if (*q == ',') {                          /* elision hole */
                q++; idx++;
                continue;
            }
            if (!strncmp(q, "...", 3)) {              /* array rest */
                const char *r = skipws(q + 3);
                const char *re = skip_ident(r);
                if (re == r) return -1;
                bputs(out, "var ");
                bputn(out, r, (size_t)(re - r));
                bputs(out, " = __vw_aread(");
                bputs(out, access);
                snprintf(acc, sizeof(acc), ", %d); ", idx);
                bputs(out, acc);
                q = re;
                break;
            }
            if (*q == '[' || *q == '{') {           /* nested pattern */
                const char *ne = scan_balanced(q);
                if (ne[-1] != ']' && ne[-1] != '}' && ne[-1] != ')') return -1;
                n = g_temp_seq++;
                snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
                bputs(out, "var ");
                bputs(out, tmp);
                bputs(out, " = ");
                bputs(out, access);
                snprintf(acc, sizeof(acc), "[%d]; ", idx);
                bputs(out, acc);
                {
                    const char *sub = dupn(q, (size_t)(ne - q));
                    const char *sq = sub;
                    int rc;
                    if (!sub) return -1;
                    rc = destructure_emit(&sq, tmp, out, hooks, 1);
                    free((void *)sub);
                    if (rc != 0) return -1;
                }
                q = ne;
            } else {                                /* target [= default] */
                const char *te = skip_ident(q);
                const char *te2;
                while (te < end - 1 && *te == '.' &&
                       is_ident_start((unsigned char)te[1])) {
                    te++; te = skip_ident(te);
                }
                if (te == q) return -1;
                n = g_temp_seq++;
                snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
                bputs(out, "var ");
                bputs(out, tmp);
                bputs(out, " = ");
                bputs(out, access);
                snprintf(acc, sizeof(acc), "[%d]; ", idx);
                bputs(out, acc);
                te2 = te;
                {
                    const char *dq = skipws(te);
                    if (dq < end - 1 && dq[0] == '=' && dq[1] != '=' &&
                        dq[1] != '>') {
                        /* default: to the next top-level "," (or end) */
                        const char *de = dq + 1;
                        int nest2 = 0;
                        while (de < end - 1) {
                            char c = *de;
                            if (c == '\'' || c == '"') { de = skip_literal(de, c, c, 0); continue; }
                            if (c == '(' || c == '[' || c == '{') nest2++;
                            else if (c == ')' || c == ']' || c == '}') nest2--;
                            else if (c == ',' && nest2 == 0) break;
                            de++;
                        }
                        {
                            const char *dsrc = dupn(dq + 1, (size_t)(de - (dq + 1)));
                            if (!dsrc) return -1;
                            bputs(out, "if (");
                            bputs(out, tmp);
                            bputs(out, " === undefined) ");
                            bputs(out, tmp);
                            bputs(out, " = ");
                            xform_piece(dsrc, out, hooks);
                            free((void *)dsrc);
                            bputs(out, "; ");
                        }
                        te2 = de;    /* resume AFTER the default value */
                    }
                }
                if (decl) bputs(out, "var ");
                bputn(out, q, (size_t)(te - q));
                bputs(out, " = ");
                bputs(out, tmp);
                bputs(out, "; ");
                q = te2;
            }
            q = skipws(q);
            if (q < end - 1 && *q == ',') { q++; idx++; continue; }
            if (q >= end - 1) break;
            return -1;
        }
        *pp = end;
        return 0;
    }
    if (*p == '{') {
        const char *end = scan_balanced(p);
        const char *q = p + 1;
        char keys[2048];
        size_t keys_len = 0;
        if (*end == 0) return -1;
        keys[0] = 0;
        while (q < end - 1) {
            char acc[512];
            q = skipws(q);
            if (q >= end - 1) break;
            if (!strncmp(q, "...", 3)) {              /* object rest */
                const char *r = skipws(q + 3);
                const char *re = skip_ident(r);
                if (re == r) return -1;
                bputs(out, "var ");
                bputn(out, r, (size_t)(re - r));
                bputs(out, " = __vw_oread(");
                bputs(out, access);
                bputs(out, ", [");
                bputs(out, keys);
                bputs(out, "]); ");
                q = re;
                break;
            }
            {
                const char *ke;
                char key[256];
                size_t kl;
                if (*q == '\'' || *q == '"') {
                    ke = skip_literal(q, *q, *q, 1);
                    if (ke == q || *ke == 0) return -1;
                    kl = (size_t)(ke - 2 - (q + 1));
                    if (kl >= sizeof(key)) return -1;
                    memcpy(key, q + 1, kl);
                    key[kl] = 0;
                } else if (is_ident_start((unsigned char)*q)) {
                    ke = skip_ident(q);
                    if (ke == q) return -1;
                    kl = (size_t)(ke - q);
                    if (kl >= sizeof(key)) return -1;
                    memcpy(key, q, kl);
                    key[kl] = 0;
                } else if (*q == '[') {
                    return -1;      /* computed key: unsupported */
                } else return -1;
                /* record for the rest-copier */
                if (keys_len + kl + 4 < sizeof(keys)) {
                    if (keys_len) keys[keys_len++] = ',';
                    keys[keys_len++] = '"';
                    memcpy(keys + keys_len, key, kl);
                    keys_len += kl;
                    keys[keys_len++] = '"';
                    keys[keys_len] = 0;
                }
                {
                    const char *cq = skipws(ke);
                    if (cq < end - 1 && *cq == ':') {
                        const char *vq = skipws(cq + 1);
                        if (*vq == '{' || *vq == '[') {
                            const char *ne = scan_balanced(vq);
                            if (ne[-1] != ']' && ne[-1] != '}' && ne[-1] != ')') return -1;
                            n = g_temp_seq++;
                            snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
                            bputs(out, "var ");
                            bputs(out, tmp);
                            bputs(out, " = ");
                            bputs(out, access);
                            bputs(out, "[\"");
                            bputs(out, key);
                            bputs(out, "\"]; ");
                            {
                                const char *sub = dupn(vq, (size_t)(ne - vq));
                                const char *sq = sub;
                                int rc;
                                if (!sub) return -1;
                                rc = destructure_emit(&sq, tmp, out, hooks, 1);
                                free((void *)sub);
                                if (rc != 0) return -1;
                            }
                            q = ne;
                        } else {
                            const char *te = skip_ident(vq);
                            while (te < end - 1 && *te == '.' &&
                                   is_ident_start((unsigned char)te[1])) {
                                te++; te = skip_ident(te);
                            }
                            if (te == vq) return -1;
                            snprintf(acc, sizeof(acc), "%s[\"%s\"]",
                                     access, key);
                            if (decl) bputs(out, "var ");
                            bputn(out, vq, (size_t)(te - vq));
                            bputs(out, " = ");
                            bputs(out, acc);
                            bputs(out, "; ");
                            q = te;
                        }
                    } else {
                        /* shorthand {a} or {a = def} */
                        snprintf(acc, sizeof(acc), "%s[\"%s\"]", access, key);
                        n = g_temp_seq++;
                        snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
                        bputs(out, "var ");
                        bputs(out, tmp);
                        bputs(out, " = ");
                        bputs(out, acc);
                        bputs(out, "; ");
                        if (cq < end - 1 && cq[0] == '=' && cq[1] != '=' &&
                            cq[1] != '>') {
                            const char *de = cq + 1;
                            int nest2 = 0;
                            while (de < end - 1) {
                                char c = *de;
                                if (c == '\'' || c == '"') { de = skip_literal(de, c, c, 0); continue; }
                                if (c == '(' || c == '[' || c == '{') nest2++;
                                else if (c == ')' || c == ']' || c == '}') nest2--;
                                else if (c == ',' && nest2 == 0) break;
                                de++;
                            }
                            {
                                const char *dsrc = dupn(cq + 1, (size_t)(de - (cq + 1)));
                                if (!dsrc) return -1;
                                bputs(out, "if (");
                                bputs(out, tmp);
                                bputs(out, " === undefined) ");
                                bputs(out, tmp);
                                bputs(out, " = ");
                                xform_piece(dsrc, out, hooks);
                                free((void *)dsrc);
                                bputs(out, "; ");
                            }
                            cq = de;
                        }
                        if (decl) bputs(out, "var ");
                        bputs(out, key);
                        bputs(out, " = ");
                        bputs(out, tmp);
                        bputs(out, "; ");
                        q = cq;
                    }
                }
            }
            q = skipws(q);
            if (q < end - 1 && *q == ',') { q++; continue; }
            if (q >= end - 1) break;
            return -1;
        }
        *pp = end;
        return 0;
    }
    return -1;
}

/* --- the main lexical pass ------------------------------------------------- */

/* Remember where each "(" started in the OUTPUT so a later "=>" can turn
 * the whole group into "function (...)". */
typedef struct {
    size_t dst_at;        /* buffer position of the "(" */
    const char *src_at;   /* source position after "(" (for src checks) */
} ParenMark;

#define MAX_PARENS 48
static ParenMark g_parens[MAX_PARENS];
static int       g_paren_top;

/* Emit an arrow EXPRESSION body as "<open>return <body> ; }".  The body
 * is recursively transformed first: arrows can NEST inside an expression
 * body (e.g. `() => [1].map(x => x * 2)`), and copying it verbatim - as
 * this used to do - left the inner arrow un-converted and produced a
 * SyntaxError at runtime.  On transform failure fall back to verbatim
 * (same policy as try_for_of). */
static void arrow_body_emit(const char *body, size_t len, Buf *b,
                            const VwEsHooks *hooks,
                            const char *open, const char *close) {
    char *src = malloc(len + 1);
    char *dst = malloc(len * 6 + 4096);
    if (src) { memcpy(src, body, len); src[len] = 0; }
    if (src && dst && vw_es2015_transform(src, dst, len * 6 + 4096, hooks) == 1) {
        bputs(b, open);
        bputs(b, dst);
        bputs(b, close);
    } else {                                 /* out of memory: keep verbatim */
        bputs(b, open);
        bputn(b, body, len);
        bputs(b, close);
    }
    free(src);
    free(dst);
}

/* Find the end of an arrow EXPRESSION body: stops at a depth-0 ",", ";",
 * ")" , "]" or "}".  Returns the position of that terminator (it is NOT
 * consumed). */
static const char *arrow_body_end(const char *p) {
    int nest = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = skip_literal(p, *p, *p, 0); continue; }
        if (*p == '/' && p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (*p == '/' && p[0] == '*' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
        if (*p == '(' || *p == '[' || *p == '{') nest++;
        else if (*p == ')' || *p == ']' || *p == '}') {
            if (nest == 0 && (*p == ')' || *p == ']' || *p == '}')) return p;
            nest--;
        } else if (nest == 0 && (*p == ',' || *p == ';')) return p;
        p++;
    }
    return p;
}


/* --- clean parameter builder (defaults / rest / patterns) --------------- */

/* Parses one raw parameter-list (WITHOUT the parens) and produces:
 *   clean   - a plain ES5 parameter list for the function header
 *   prologue- statements to inject at the top of the body (defaults,
 *             rest -> arguments slice, destructuring)
 * Returns 0 ok, -1 unsupported (caller keeps the source verbatim). */
static int build_params(const char *raw, Buf *clean, Buf *prologue,
                        const VwEsHooks *hooks) {
    const char *p = skipws(raw);
    int pos = 0;
    while (*p && *p != ')') {
        char tmp[32];
        int n;
        /* piece ends at a depth-0 "," or the end */
        const char *e = p;
        int nest = 0;
        while (*e) {
            char c = *e;
            if (c == '\'' || c == '"') { e = skip_literal(e, c, c, 0); continue; }
            if (c == '`') { e = skip_template_raw(e); continue; }
            if (c == '(' || c == '[' || c == '{') nest++;
            else if (c == ')' || c == ']' || c == '}') {
                if (nest == 0 && c == ')') break;
                nest--;
            } else if (c == ',' && nest == 0) break;
            e++;
        }
        {
            const char *piece = dupn(p, (size_t)(e - p));
            const char *q = piece ? skipws(piece) : NULL;
            if (!piece) return -1;
            if (*q == 0) { free((void *)piece); p = e; goto next_piece; }
            if (!strncmp(q, "...", 3)) {
                const char *r = skipws(q + 3);
                const char *re = skip_ident(r);
                char rest[128];
                if (re == r || *skipws(re) != 0) { free((void *)piece); return -1; }
                if ((size_t)(re - r) >= sizeof(rest)) { free((void *)piece); return -1; }
                memcpy(rest, r, (size_t)(re - r));
                rest[re - r] = 0;
                bputs(prologue, "var ");
                bputs(prologue, rest);
                bputs(prologue, " = __vw_aread(arguments, ");
                {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", pos);
                    bputs(prologue, num);
                }
                bputs(prologue, "); ");
            } else if (*q == '{' || *q == '[') {
                const char *pe = scan_balanced(q);
                const char *dq;
                if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') { free((void *)piece); return -1; }
                n = g_temp_seq++;
                snprintf(tmp, sizeof(tmp), "__vw_p%d", n);
                dq = skipws(pe);
                if (dq[0] == '=' && dq[1] != '=' && dq[1] != '>') {
                    /* default runs to the piece end */
                    {
                        const char *dsrc = dupn(dq + 1, strlen(dq + 1));
                        if (!dsrc) { free((void *)piece); return -1; }
                        bputs(prologue, "if (");
                        bputs(prologue, tmp);
                        bputs(prologue, " === undefined) ");
                        bputs(prologue, tmp);
                        bputs(prologue, " = ");
                        xform_piece(dsrc, prologue, hooks);
                        free((void *)dsrc);
                        bputs(prologue, "; ");
                    }
                }
                if (clean->len) bputs(clean, ", ");
                bputs(clean, tmp);
                {
                    /* the object branch of destructure_emit rejects a
                       pattern whose closing brace is followed by a NUL
                       (*end == 0 guard), so append a non-NUL char */
                    char *psrc = (char *)malloc((size_t)(pe - q) + 8);
                    int prc;
                    if (!psrc) { free((void *)piece); return -1; }
                    memcpy(psrc, q, (size_t)(pe - q));
                    psrc[pe - q] = ';';
                    psrc[pe - q + 1] = 0;
                    {
                        const char *sq = psrc;
                        prc = destructure_emit(&sq, tmp, prologue, hooks, 1);
                    }
                    free(psrc);
                    if (prc != 0) {
                        free((void *)piece);
                        return -1;
                    }
                }
                pos++;
            } else if (is_ident_start((unsigned char)*q)) {
                const char *ne = skip_ident(q);
                const char *dq = skipws(ne);
                if (dq[0] == '=' && dq[1] != '=' && dq[1] != '>') {
                    /* default parameter */
                    const char *dsrc = dupn(dq + 1, strlen(dq + 1));
                    if (!dsrc) { free((void *)piece); return -1; }
                    if (clean->len) bputs(clean, ", ");
                    bputn(clean, q, (size_t)(ne - q));
                    bputs(prologue, "if (");
                    bputn(prologue, q, (size_t)(ne - q));
                    bputs(prologue, " === undefined) ");
                    bputn(prologue, q, (size_t)(ne - q));
                    bputs(prologue, " = ");
                    xform_piece(dsrc, prologue, hooks);
                    free((void *)dsrc);
                    bputs(prologue, "; ");
                } else if (*dq == 0) {
                    if (clean->len) bputs(clean, ", ");
                    bputn(clean, q, (size_t)(ne - q));
                } else {
                    free((void *)piece);
                    return -1;      /* junk after the parameter */
                }
                pos++;
            } else {
                free((void *)piece);
                return -1;
            }
            free((void *)piece);
        }
      next_piece:
        p = e;
        if (*p == ',') { p++; p = skipws(p); }
    }
    return 0;
}

/* Does a raw parameter list need the rewrite at all?  (cheap check for
 * the hot path: most parameter lists are plain) */
static int params_need_rewrite(const char *raw, size_t n) {
    size_t i;
    int nest = 0;
    for (i = 0; i < n; i++) {
        char c = raw[i];
        if (c == '\'' || c == '"') {
            const char *q = skip_literal(raw + i, c, c, 0);
            i += (size_t)(q - (raw + i)) - 1;
            continue;
        }
        if (nest == 0 && (c == '{' || c == '[')) {
            /* top-level pattern param: the char is also counted below,
               so test BEFORE the increment */
            return 1;
        }
        if (c == '(' || c == '[' || c == '{') nest++;
        else if (c == ')' || c == ']' || c == '}') nest--;
        else if (nest == 0 && (c == '.' || c == '=' || c == '?')) {
            /* '.' of "...", '{'/'[' patterns, '=' defaults; '?' is the
               only way a '?' can appear (arrow pure check ran first) */
            return 1;
        }
    }
    return 0;
}

/* Is the group before this "(" a function's parameter list?  Returns
 * the position of the "function" keyword in the OUTPUT, or (size_t)-1. */
static size_t output_params_group(Buf *b, size_t paren_at) {
    size_t j = paren_at;
    while (j > 0 && isspace((unsigned char)b->p[j - 1])) j--;
    if (j > 0 && is_ident_char((unsigned char)b->p[j - 1])) {
        /* function NAME ( - ident directly before the group */
        size_t wStart = j;
        while (wStart > 0 && is_ident_char((unsigned char)b->p[wStart - 1])) wStart--;
        {
            size_t k = wStart;
            while (k > 0 && isspace((unsigned char)b->p[k - 1])) k--;
            if (k >= 8 && !strncmp(b->p + k - 8, "function", 8) &&
                (k - 8 == 0 || !is_ident_char((unsigned char)b->p[k - 9])))
                return k - 8;
        }
        return (size_t)-1;
    }
    if (j >= 8 && !strncmp(b->p + j - 8, "function", 8) &&
        (j - 8 == 0 || !is_ident_char((unsigned char)b->p[j - 9])))
        return j - 8;
    return (size_t)-1;
}

/* --- destructuring declarations / assignments --------------------------- */

/* Statement-start in the output buffer?  (for assignment destructure:
   after ';' '{' '}' or at the very beginning - never after ')' which
   could still be a controlled statement we cannot reliably classify.) */
static int output_stmt_start(Buf *b) {
    size_t i = b->len;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i == 0) return 1;
    {
        char c = b->p[i - 1];
        return c == ';' || c == '{' || c == '}';
    }
}

/* Try destructuring at a `var` declaration: output ends with the word
 * "var" and the source holds `[pat] = init;` / `{pat} = init;` (or the
 * same inside a for(..of..) header).  p points at the `[` or `{`.
 * Returns 1 when the source was rewritten (p advanced), 0 otherwise. */
static int try_destructure(Buf *b, const char **pp, const VwEsHooks *hooks) {
    const char *p = *pp;
    size_t e = b->len;
    size_t var_at;
    const char *pend, *q;
    while (e > 0 && isspace((unsigned char)b->p[e - 1])) e--;
    if (e < 3 || strncmp(b->p + e - 3, "var", 3) != 0) return 0;
    if (e - 3 > 0 && is_ident_char((unsigned char)b->p[e - 4])) return 0;
    var_at = e - 3;
    pend = scan_balanced(p);              /* past the pattern */
    if (pend[-1] != ']' && pend[-1] != '}' && pend[-1] != ')') return 0;
    q = skipws(pend);
    if (*q != '=') return 0;              /* not a destructure */
    q = skipws(q + 1);
    /* init runs to a depth-0 ',' or ';' */
    {
        const char *ie = q;
        int nest = 0;
        while (*ie) {
            char c = *ie;
            if (c == '\'' || c == '"') { ie = skip_literal(ie, c, c, 0); continue; }
            if (c == '`') { ie = skip_template_raw(ie); continue; }
            if (ie[0] == '/' && ie[1] == '/') { while (*ie && *ie != '\n') ie++; continue; }
            if (c == '(' || c == '[' || c == '{') nest++;
            else if (c == ')' || c == ']' || c == '}') nest--;
            else if (nest == 0 && (c == ',' || c == ';')) break;
            ie++;
        }
        if (*ie != ',' && *ie != ';') return 0;
        {
            const char *init = dupn(q, (size_t)(ie - q));
            char tmp[32];
            int n = g_temp_seq++;
            Buf pro;
            char *pro_buf;
            const char *pat;
            int rc;
            if (!init) return 0;
            snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
            pro_buf = (char *)malloc(strlen(init) + 4096);
            if (!pro_buf) { free((void *)init); return 0; }
            pro.p = pro_buf; pro.cap = strlen(init) + 4096;
            pro.len = 0; pro.overflow = 0;
            pat = p;      /* pattern starts at `[` or `{` */
            rc = destructure_emit(&pat, tmp, &pro, hooks, 1);
            free((void *)init);
            if (rc != 0 || pro.overflow) { free(pro_buf); return 0; }
            btruncate(b, var_at);
            bputs(b, "var ");
            bputs(b, tmp);
            bputs(b, " = ");
            /* init: recursively transformed (templates/arrows inside) */
            {
                const char *init2 = dupn(q, (size_t)(ie - q));
                xform_piece(init2 ? init2 : "", b, hooks);
                free((void *)init2);
            }
            bputs(b, "; ");
            if (pro.len) bputn(b, pro_buf, pro.len);
            free(pro_buf);
            if (*ie == ',') bputs(b, " var ");   /* continue the var list */
            *pp = ie;                       /* caller sees ',' or ';' */
            return 1;
        }
    }
}

/* for-of with a destructuring pattern in the output tail:
   `for (var [a, b] of ITER)` / `for (var {a} of ITER)`.  Detected when
   try_for_of's plain-ident probe fails; the pattern text sits in the
   OUTPUT buffer, truncated from the position of its opening bracket. */
static int try_for_of_pattern(Buf *b, const char **pp,
                              const VwEsHooks *hooks) {
    const char *p = *pp;
    size_t e = b->len;
    size_t close_at, open_at, var_at;
    const char *q;
    char close_c, open_c;
    int nest;
    const char *bs, *be;
    const char *iter, *body, *pat;
    char *iter_es5, *body_es5;
    int braced;
    static int seq2 = 0;
    char sv[32], iv[32], ev[32];
    int my_seq;
    size_t fpos;

    /* tail: balanced [...] or {...} before "of" */
    while (e > 0 && isspace((unsigned char)b->p[e - 1])) e--;
    if (e == 0) return 0;
    close_c = b->p[e - 1];
    if (close_c != ']' && close_c != '}') return 0;
    open_c = (close_c == ']') ? '[' : '{';
    nest = 0;
    open_at = e;
    while (open_at > 0) {
        char d = b->p[open_at - 1];
        if (d == close_c) nest++;
        else if (d == open_c) { nest--; if (nest == 0) { open_at--; break; } }
        open_at--;
    }
    if (open_at == 0) return 0;
    close_at = e;
    var_at = open_at;
    while (var_at > 0 && isspace((unsigned char)b->p[var_at - 1])) var_at--;
    if (var_at < 3 || strncmp(b->p + var_at - 3, "var", 3) != 0 ||
        (var_at - 3 > 0 && is_ident_char((unsigned char)b->p[var_at - 4])))
        return 0;
    var_at -= 3;
    while (var_at > 0 && isspace((unsigned char)b->p[var_at - 1])) var_at--;
    if (var_at == 0 || b->p[var_at - 1] != '(') return 0;
    var_at--;
    while (var_at > 0 && isspace((unsigned char)b->p[var_at - 1])) var_at--;
    if (var_at < 3 || strncmp(b->p + var_at - 3, "for", 3) != 0) return 0;
    if (var_at - 3 > 0 && is_ident_char((unsigned char)b->p[var_at - 4])) return 0;
    fpos = var_at - 3;

    /* source: of ITER ) BODY */
    if (p[0] != 'o' || p[1] != 'f') return 0;
    p += 2;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    q = scan_to_close_paren(p);
    if (*q != ')') return 0;
    iter = dupn(p, (size_t)(q - p));
    if (!iter) return 0;

    bs = q + 1;
    while (*bs == ' ' || *bs == '\t' || *bs == '\n' || *bs == '\r') bs++;
    braced = (*bs == '{');
    if (braced) {
        be = scan_brace_block(bs);
    } else {
        int nest2 = 0;
        const char *r = bs;
        while (*r) {
            if (*r == '\'' || *r == '"') { r = skip_literal(r, *r, *r, 0); continue; }
            if (r[0] == '/' && r[1] == '/') { while (*r && *r != '\n') r++; continue; }
            if (r[0] == '/' && r[1] == '*') {
                r += 2; while (*r && !(r[0] == '*' && r[1] == '/')) r++;
                if (*r) { r += 2; }
                continue;
            }
            if (*r == '`') { r = skip_template_raw(r); continue; }
            if (*r == '(' || *r == '[' || *r == '{') nest2++;
            else if (*r == ')' || *r == ']' || *r == '}') { if (nest2 == 0) break; nest2--; }
            else if (nest2 == 0 && *r == ';') { r++; break; }
            r++;
        }
        be = r;
    }
    body = dupn(bs, (size_t)(be - bs));
    if (!body) { free((void *)iter); return 0; }

    iter_es5 = (char *)malloc(strlen(iter) * 6 + 4096);
    body_es5 = (char *)malloc(strlen(body) * 6 + 4096);
    if (!iter_es5 || !body_es5) {
        free((void *)iter); free((void *)body); free(iter_es5); free(body_es5);
        return 0;
    }
    if (vw_es2015_transform(iter, iter_es5, strlen(iter) * 6 + 4096, hooks) != 1)
        snprintf(iter_es5, strlen(iter) * 6 + 4096, "%s", iter);
    if (vw_es2015_transform(body, body_es5, strlen(body) * 6 + 4096, hooks) != 1)
        snprintf(body_es5, strlen(body) * 6 + 4096, "%s", body);
    free((void *)iter); free((void *)body);

    /* the pattern: copy out of the output before truncating */
    pat = dupn(b->p + open_at, close_at - open_at);
    if (!pat) { free(iter_es5); free(body_es5); return 0; }
    {
        /* same *end == 0 guard as build_params: append a non-NUL char */
        size_t pl = strlen(pat);
        char *p2 = (char *)realloc((void *)pat, pl + 2);
        if (!p2) { free((void *)pat); free(iter_es5); free(body_es5); return 0; }
        p2[pl] = ';';
        p2[pl + 1] = 0;
        pat = p2;
    }

    my_seq = seq2++;
    snprintf(sv, sizeof(sv), "__vw_s%d", my_seq);
    snprintf(iv, sizeof(iv), "__vw_i%d", my_seq);
    snprintf(ev, sizeof(ev), "__vw_e%d", my_seq);

    btruncate(b, fpos);
    bputs(b, "for (var ");
    bputs(b, sv);
    bputs(b, " = ");
    bputs(b, iter_es5);
    bputs(b, ", ");
    bputs(b, iv);
    bputs(b, " = 0; ");
    bputs(b, iv);
    bputs(b, " < ");
    bputs(b, sv);
    bputs(b, ".length; ");
    bputs(b, iv);
    bputs(b, "++) { var ");
    bputs(b, ev);
    bputs(b, " = ");
    bputs(b, sv);
    bputs(b, "[");
    bputs(b, iv);
    bputs(b, "]; ");
    {
        char *pro_buf = (char *)malloc(strlen(pat) * 6 + 4096);
        if (pro_buf) {
            Buf pro;
            const char *pq = pat;
            pro.p = pro_buf; pro.cap = strlen(pat) * 6 + 4096;
            pro.len = 0; pro.overflow = 0;
            if (destructure_emit(&pq, ev, &pro, hooks, 1) == 0 && !pro.overflow)
                bputn(b, pro_buf, pro.len);
            free(pro_buf);
        }
    }
    bputs(b, body_es5);
    bputs(b, braced ? " }" : "; }");
    free((void *)pat);
    free(iter_es5);
    free(body_es5);
    *pp = be;
    return 1;
}

/* forward decls from the object-literal section below */
static int output_expr_position(Buf *b);
static int output_stmt_start(Buf *b);

/* --- ES7+ operator / spread / destructure handlers (2026-09-26 (47)) ----- */

/* Scan an expression in the source to the next depth-0 terminator
 * (, ; ) ] }) for right operands.  Returns the terminator position. */
static const char *scan_expr_end(const char *p) {
    int nest = 0;
    while (*p) {
        char c = *p;
        if (c == '\'' || c == '"') { p = skip_literal(p, c, c, 0); continue; }
        if (c == '`') { p = skip_template_raw(p); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') nest++;
        else if (c == ')' || c == ']' || c == '}') { if (nest == 0) break; nest--; }
        else if (nest == 0 && (c == ',' || c == ';')) break;
        p++;
    }
    return p;
}

/* Is this a plain assignment target (ident or member chain)? */
static int is_assign_target(const char *t, size_t n) {
    size_t i;
    if (n == 0 || !is_ident_start((unsigned char)t[0])) return 0;
    for (i = 0; i < n; i++)
        if (!is_ident_char((unsigned char)t[i]) && t[i] != '.') return 0;
    return 1;
}

/* ** and **=  ->  Math.pow().  Triggered at the first `*` of `**`. */
static int try_exponent(Buf *b, const char **pp, const VwEsHooks *hooks) {
    const char *p = *pp;
    size_t ls, ll;
    char *L, *R;
    const char *r;
    int assign = (p[2] == '=');
    ls = operand_start(b);
    ll = b->len - ls;
    while (ll > 0 && isspace((unsigned char)b->p[ls + ll - 1])) ll--;
    if (assign) {
        if (!is_assign_target(b->p + ls, ll)) return 0;
    } else if (ll == 0 || ll > 512) return 0;
    L = dupn(b->p + ls, ll);
    if (!L) return 0;
    p += assign ? 3 : 2;
    r = scan_expr_end(p);
    if (r == p) { free(L); return 0; }
    R = dupn(p, (size_t)(r - p));
    if (!R) { free(L); return 0; }
    btruncate(b, ls);
    if (assign) {
        bputs(b, L);
        bputs(b, " = Math.pow(");
        bputs(b, L);
        bputs(b, ", ");
        xform_piece(R, b, hooks);
        bputs(b, ")");
    } else {
        bputs(b, "Math.pow(");
        bputs(b, L);
        bputs(b, ", ");
        xform_piece(R, b, hooks);
        bputs(b, ")");
    }
    free(L); free((void *)R);
    *pp = r;
    return 1;
}

/* ?? (nullish coalescing) and ??=.  Triggered at the first `?` of `??`. */
static int try_coalesce(Buf *b, const char **pp, const VwEsHooks *hooks) {
    const char *p = *pp;
    size_t ls, ll;
    char *L, *R;
    const char *r;
    int assign = (p[2] == '=');
    char t[32];
    int n = g_temp_seq++;
    snprintf(t, sizeof(t), "__vw_q%d", n);
    ls = operand_start(b);
    ll = b->len - ls;
    while (ll > 0 && isspace((unsigned char)b->p[ls + ll - 1])) ll--;
    if (assign) {
        if (!is_assign_target(b->p + ls, ll)) return 0;
    } else if (ll == 0 || ll > 2048) return 0;
    L = dupn(b->p + ls, ll);
    if (!L) return 0;
    p += assign ? 3 : 2;
    r = scan_expr_end(p);
    if (r == p) { free(L); return 0; }
    R = dupn(p, (size_t)(r - p));
    if (!R) { free(L); return 0; }
    btruncate(b, ls);
    bputs(b, "((");
    bputs(b, t);
    bputs(b, " = (");
    bputs(b, L);
    bputs(b, ")) == null ? ");
    if (assign) {
        bputs(b, "((");
        bputs(b, L);
        bputs(b, ") = ");
        xform_piece(R, b, hooks);
        bputs(b, ") : ");
        bputs(b, t);
    } else {
        bputs(b, "(");
        xform_piece(R, b, hooks);
        bputs(b, ") : ");
        bputs(b, t);
    }
    bputs(b, ")");
    free(L); free((void *)R);
    *pp = r;
    return 1;
}

/* ?. optional chaining.  Triggered at the `?` of `?.` (not `?.5`).  The
 * whole postfix chain is guarded - a null anywhere yields undefined. */
static int try_optchain(Buf *b, const char **pp, const VwEsHooks *hooks) {
    (void)hooks;
    const char *p = *pp + 2;               /* past "?." */
    size_t ls = operand_start(b);
    size_t ll = b->len - ls;
    char *L;
    char t[8][32];
    const char *link[16];
    size_t linklen[16];
    int nlink = 0, i;
    if (ll == 0 || ll > 2048) return 0;
    /* first link: the one right after "?." */
    while (nlink < 16) {
        const char *le;
        if (is_ident_start((unsigned char)*p)) {
            le = skip_ident(p);
        } else if (*p == '(') {
            le = scan_balanced(p);
        } else if (*p == '[') {
            le = scan_balanced(p);
        } else return 0;
        link[nlink] = p;
        linklen[nlink] = (size_t)(le - p);
        nlink++;
        p = le;
        /* continue the chain? */
        if (p[0] == '.' && is_ident_start((unsigned char)p[1])) {
            p++;                            /* link starts at the ident */
        } else if (p[0] == '?' && p[1] == '.' &&
                   (is_ident_start((unsigned char)p[2]) || p[2] == '[' ||
                    p[2] == '(')) {
            p += 2;
        } else if (p[0] == '[' || p[0] == '(') {
            /* plain continue */
        } else break;
    }
    L = dupn(b->p + ls, ll);
    if (!L) return 0;
    btruncate(b, ls);
    bputs(b, "(");
    snprintf(t[0], sizeof(t[0]), "__vw_o%d", g_temp_seq++);
    bputs(b, t[0]);
    bputs(b, " = (");
    bputs(b, L);
    bputs(b, ")");
    for (i = 0; i < nlink; i++) {
        snprintf(t[(i + 1) % 8], sizeof(t[0]), "__vw_o%d", g_temp_seq++);
        bputs(b, ", ");
        bputs(b, t[(i + 1) % 8]);
        bputs(b, " = (");
        bputs(b, t[i % 8]);
        bputs(b, " == null ? void 0 : ");
        bputs(b, t[i % 8]);
        if (linklen[i] > 0 && is_ident_start((unsigned char)link[i][0]))
            bputs(b, ".");
        bputn(b, link[i], linklen[i]);
        bputs(b, ")");
    }
    bputs(b, ", ");
    bputs(b, t[nlink % 8]);
    bputs(b, ")");
    free(L);
    *pp = p;
    return 1;
}

/* &&= and ||= logical assignment. */
static int try_logical_assign(Buf *b, const char **pp,
                              const VwEsHooks *hooks, int and_op) {
    const char *p = *pp;
    size_t ls = operand_start(b);
    size_t ll = b->len - ls;
    char *L, *R;
    const char *r;
    while (ll > 0 && isspace((unsigned char)b->p[ls + ll - 1])) ll--;
    if (!is_assign_target(b->p + ls, ll)) return 0;
    L = dupn(b->p + ls, ll);
    if (!L) return 0;
    p += 3;
    r = scan_expr_end(p);
    if (r == p) { free(L); return 0; }
    R = dupn(p, (size_t)(r - p));
    if (!R) { free(L); return 0; }
    btruncate(b, ls);
    bputs(b, "(");
    bputs(b, L);
    bputs(b, and_op ? " && (" : " || (");
    bputs(b, L);
    bputs(b, " = ");
    xform_piece(R, b, hooks);
    bputs(b, "))");
    free(L); free((void *)R);
    *pp = r;
    return 1;
}

/* Optional catch binding: catch { ... } -> catch (__vw_cN) { ... } */
static int try_optional_catch(Buf *b, const char **pp) {
    const char *p = *pp;
    const char *q;
    size_t i = b->len;
    char prev = 0;
    static int seq3 = 0;
    char t[32];
    if (!src_word(p, "catch")) return 0;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i > 0) prev = b->p[i - 1];
    if (is_ident_char((unsigned char)prev) || prev == '.') return 0;
    q = skipws(p + 5);
    if (*q != '{') return 0;               /* normal catch (e) */
    snprintf(t, sizeof(t), "catch (__vw_c%d)", seq3++);
    bputs(b, t);
    *pp = p + 5;                            /* the "{" flows normally */
    return 1;
}

/* Spread in a CALL: f(a, ...b, c) -> f.apply(null, [].concat([a],
 * __vw_spread(b), [c])).  Triggered at a "..." inside a call group. */
static int try_call_spread(Buf *b, const char **pp,
                           const VwEsHooks *hooks) {
    ParenMark mark;
    size_t i;
    const char *close;
    const char *q;
    if (g_paren_top == 0) return 0;
    mark = g_parens[g_paren_top - 1];
    /* function parameter group? (defaults/rest handled at ")") */
    if (output_params_group(b, mark.dst_at) != (size_t)-1) return 0;
    /* group prefix: ident call / member call only */
    i = mark.dst_at;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    {
        char prev = 0;
        size_t chain_end = i;
        size_t chain_start = i;
        static const char *kw[] = { "if", "while", "for", "switch", "catch",
                                    "with", "return", "typeof", "new", "void",
                                    "function", "in", "of", "do", "else", 0 };
        if (i == 0) return 0;
        prev = b->p[i - 1];
        if (prev == '.') {
            /* member call: backtrack the chain */
            while (chain_start > 0 &&
                   (is_ident_char((unsigned char)b->p[chain_start - 1]) ||
                    b->p[chain_start - 1] == '.'))
                chain_start--;
            if (chain_start == 0 || b->p[chain_start] == '.') return 0;
        } else if (is_ident_char((unsigned char)prev)) {
            while (chain_start > 0 &&
                   is_ident_char((unsigned char)b->p[chain_start - 1]))
                chain_start--;
            /* keyword call? (if/while/...) */
            {
                int k;
                size_t wl = chain_end - chain_start;
                for (k = 0; kw[k]; k++)
                    if (strlen(kw[k]) == wl &&
                        !strncmp(b->p + chain_start, kw[k], wl)) return 0;
            }
        } else return 0;
        /* split object / method for .apply() */
        close = scan_to_close_paren(mark.src_at);
        if (*close != ')') return 0;
        /* the "..." must be inside THIS group (it is: mark is innermost) */
        {
            char *callee = dupn(b->p + chain_start, chain_end - chain_start);
            char *obj = NULL;
            char *arr;
            Buf ab;
            int any_spread = 0;
            if (!callee) return 0;
            if (strchr(callee, '.')) {
                char *dot = strrchr(callee, '.');
                *dot = 0;
                obj = dupn(callee, strlen(callee));
                *dot = '.';
                if (!obj) { free(callee); return 0; }
            }
            arr = (char *)malloc((size_t)(close - mark.src_at) * 8 + 1024);
            if (!arr) { free(callee); free(obj); return 0; }
            ab.p = arr; ab.cap = (size_t)(close - mark.src_at) * 8 + 1024;
            ab.len = 0; ab.overflow = 0;
            bputs(&ab, "[].concat(");
            q = mark.src_at;
            while (q < close) {
                int nest = 0;
                const char *e = q;
                const char *tq, *te;
                while (e < close) {
                    char c = *e;
                    if (c == '\'' || c == '"') { e = skip_literal(e, c, c, 0); if (e > close) e = close; continue; }
                    if (c == '`') { e = skip_template_raw(e); if (e > close) e = close; continue; }
                    if (c == '(' || c == '[' || c == '{') nest++;
                    else if (c == ')' || c == ']' || c == '}') nest--;
                    else if (c == ',' && nest == 0) break;
                    e++;
                }
                tq = q;
                while (tq < e && isspace((unsigned char)*tq)) tq++;
                te = e;
                while (te > tq && isspace((unsigned char)te[-1])) te--;
                if (te > tq) {
                    if ((size_t)(te - tq) > 3 && !strncmp(tq, "...", 3)) {
                        char *sp = dupn(tq + 3, (size_t)(te - tq - 3));
                        if (!sp) { free(arr); free(callee); free(obj); return 0; }
                        bputs(&ab, any_spread ? ", " : "");
                        bputs(&ab, "__vw_spread(");
                        xform_piece(sp, &ab, hooks);
                        bputs(&ab, ")");
                        free(sp);
                        any_spread = 1;
                    } else {
                        char *nm = dupn(tq, (size_t)(te - tq));
                        if (!nm) { free(arr); free(callee); free(obj); return 0; }
                        bputs(&ab, any_spread ? ", " : "");
                        bputs(&ab, "[");
                        xform_piece(nm, &ab, hooks);
                        bputs(&ab, "]");
                        free(nm);
                        any_spread = 1;
                    }
                }
                q = (e < close) ? e + 1 : close;
            }
            bputs(&ab, ")");
            if (!any_spread || ab.overflow) {
                free(arr); free(callee); free(obj);
                return 0;
            }
            btruncate(b, chain_start);
            bputs(b, callee);
            bputs(b, ".apply(");
            bputs(b, obj ? obj : "null");
            bputs(b, ", ");
            bputn(b, arr, ab.len);
            bputs(b, ")");
            free(arr); free(callee); free(obj);
            *pp = close + 1;
            return 1;
        }
    }
}

/* Statement-start parenthesized assignment destructure:
   ({ a } = obj);  ->  var __vw_dN = obj; a = __vw_dN.a;  */
static int try_paren_destructure(Buf *b, const char **pp,
                                 const VwEsHooks *hooks) {
    const char *p = *pp;
    const char *q, *end, *q2, *q3, *cl, *pq;
    char *init;
    char tmp[32];
    Buf pro;
    char *pro_buf;
    int n = g_temp_seq++;
    if (!output_stmt_start(b)) return 0;
    q = skipws(p + 1);
    if (*q != '{') return 0;
    end = scan_balanced(q);
    if (end[-1] != ']' && end[-1] != '}' && end[-1] != ')') return 0;
    q2 = skipws(end);
    if (q2[0] != '=' || q2[1] == '=') return 0;
    q3 = skipws(q2 + 1);
    cl = scan_to_close_paren(q3);
    if (*cl != ')') return 0;
    init = dupn(q3, (size_t)(cl - q3));
    if (!init) return 0;
    snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
    pro_buf = (char *)malloc((size_t)(end - q) * 6 + 4096);
    if (!pro_buf) { free(init); return 0; }
    pro.p = pro_buf; pro.cap = (size_t)(end - q) * 6 + 4096;
    pro.len = 0; pro.overflow = 0;
    pq = q;   /* SOURCE pointer: the object branch of destructure_emit
                 requires a non-NUL char past the closing brace (its
                 *end == 0 guard), so a dupn'd pattern would be rejected */
    if (destructure_emit(&pq, tmp, &pro, hooks, 0) != 0 || pro.overflow) {
        free(pro_buf); free(init);
        return 0;
    }
    bputs(b, "var ");
    bputs(b, tmp);
    bputs(b, " = ");
    xform_piece(init, b, hooks);
    bputs(b, "; ");
    if (pro.len) bputn(b, pro_buf, pro.len);
    free(pro_buf); free(init);
    *pp = cl + 1;
    return 1;
}

/* Statement-start array assignment destructure:
   [a, b] = f();  ->  var __vw_dN = f(); a = __vw_dN[0]; b = __vw_dN[1]; */
static int try_assign_destructure_array(Buf *b, const char **pp,
                                        const VwEsHooks *hooks) {
    const char *p = *pp;
    const char *end, *q, *ie;
    char *pat, *init;
    char tmp[32];
    Buf pro;
    char *pro_buf;
    const char *pq;
    int n = g_temp_seq++;
    if (!output_stmt_start(b)) return 0;
    end = scan_balanced(p);
    if (end[-1] != ']' && end[-1] != '}' && end[-1] != ')') return 0;
    q = skipws(end);
    if (q[0] != '=' || q[1] == '=' || q[1] == '>') return 0;
    q = skipws(q + 1);
    ie = scan_expr_end(q);                 /* , or ; at depth 0 */
    if (*ie != ',' && *ie != ';') return 0;
    pat = dupn(p, (size_t)(end - p));
    init = dupn(q, (size_t)(ie - q));
    if (!pat || !init) { free(pat); free(init); return 0; }
    snprintf(tmp, sizeof(tmp), "__vw_d%d", n);
    pro_buf = (char *)malloc(strlen(pat) * 6 + 4096);
    if (!pro_buf) { free(pat); free(init); return 0; }
    pro.p = pro_buf; pro.cap = strlen(pat) * 6 + 4096;
    pro.len = 0; pro.overflow = 0;
    pq = pat;
    if (destructure_emit(&pq, tmp, &pro, hooks, 0) != 0 || pro.overflow) {
        free(pro_buf); free(pat); free(init);
        return 0;
    }
    bputs(b, "var ");
    bputs(b, tmp);
    bputs(b, " = ");
    xform_piece(init, b, hooks);
    bputs(b, "; ");
    if (pro.len) bputn(b, pro_buf, pro.len);
    free(pro_buf); free(pat); free(init);
    *pp = ie;                             /* ',' or ';' stays in the stream */
    return 1;
}

/* Array literal with spread: [a, ...b, c] -> [].concat([a],
 * __vw_spread(b), [c]).  Only in expression position, only when a
 * depth-0 "..." is present. */
static int try_array_spread(Buf *b, const char **pp,
                            const VwEsHooks *hooks) {
    const char *p = *pp;
    const char *end, *q;
    int nest = 0, found = 0, first = 1;
    char *arr;
    Buf ab;
    /* output_expr_position refuses an EMPTY output, but the recursive
     * arrow-body pass (arrow_body_emit) starts from scratch: a body
     * that BEGINS with "[" (e.g. `() => [...a, ...b].join()`) is an
     * array literal, not a statement.  accept a start-of-output "[";
     * statement-start patterns still lose to try_assign_destructure_
     * array, which runs first (2026-09-26 (48) gate find). */
    {
        size_t i = b->len;
        while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
        if (i != 0 && !output_expr_position(b)) return 0;
    }
    end = scan_balanced(p);
    if (end[-1] != ']') return 0;
    /* any depth-0 "..."? */
    for (q = p + 1; q < end - 1; q++) {
        char c = *q;
        if (c == '\'' || c == '"') { q = skip_literal(q, c, c, 0); continue; }
        if (c == '`') { q = skip_template_raw(q); continue; }
        if (c == '(' || c == '[' || c == '{') nest++;
        else if (c == ')' || c == ']' || c == '}') nest--;
        else if (nest == 0 && c == '.' && !strncmp(q, "...", 3)) { found = 1; break; }
    }
    if (!found) return 0;
    arr = (char *)malloc((size_t)(end - p) * 8 + 1024);
    if (!arr) return 0;
    ab.p = arr; ab.cap = (size_t)(end - p) * 8 + 1024;
    ab.len = 0; ab.overflow = 0;
    bputs(&ab, "[].concat(");
    q = p + 1;
    while (q < end - 1) {
        const char *e = q;
        const char *tq, *te;
        int n2 = 0;
        while (e < end - 1) {
            char c = *e;
            if (c == '\'' || c == '"') { e = skip_literal(e, c, c, 0); if (e > end - 1) e = end - 1; continue; }
            if (c == '`') { e = skip_template_raw(e); if (e > end - 1) e = end - 1; continue; }
            if (c == '(' || c == '[' || c == '{') n2++;
            else if (c == ')' || c == ']' || c == '}') n2--;
            else if (c == ',' && n2 == 0) break;
            e++;
        }
        tq = q;
        while (tq < e && isspace((unsigned char)*tq)) tq++;
        te = e;
        while (te > tq && isspace((unsigned char)te[-1])) te--;
        if (te > tq) {
            char *piece;
            if ((size_t)(te - tq) > 3 && !strncmp(tq, "...", 3)) {
                piece = dupn(tq + 3, (size_t)(te - tq - 3));
                if (piece) {
                    if (!first) bputs(&ab, ", ");
                    bputs(&ab, "__vw_spread(");
                    xform_piece(piece, &ab, hooks);
                    bputs(&ab, ")");
                    free(piece);
                }
            } else {
                piece = dupn(tq, (size_t)(te - tq));
                if (piece) {
                    if (!first) bputs(&ab, ", ");
                    bputs(&ab, "[");
                    xform_piece(piece, &ab, hooks);
                    bputs(&ab, "]");
                    free(piece);
                }
            }
            first = 0;
        }
        q = (e < end - 1) ? e + 1 : end - 1;
    }
    bputs(&ab, ")");
    if (ab.overflow) { free(arr); return 0; }
    bputn(b, arr, ab.len);
    free(arr);
    *pp = end;
    return 1;
}

/* --- object literal rewrite (shorthand / method shorthand / spread) ----- */

/* async machinery: build the Promise-chain body for an async function
 * (params already cleaned; body text raw, NUL-terminated).  Writes ES5
 * into out.  0 ok, -1 unsupported await form (caller leaves verbatim).
 * Defined after lex_pass; forward-declared here. */
static int async_chain_body(const char *params_clean, const char *body,
                            char *out, size_t out_cap,
                            const VwEsHooks *hooks);

/* Expression position? (is a `{`/`[` here an object/array literal?) */
static int output_expr_position(Buf *b) {
    size_t i = b->len;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i == 0) return 0;
    {
        char c = b->p[i - 1];
        if (strchr("=,:([?!&|+-*/%<>^~", c)) return 1;
        if (is_ident_char((unsigned char)c)) {
            size_t wStart = i;
            while (wStart > 0 && is_ident_char((unsigned char)b->p[wStart - 1])) wStart--;
            {
                char w[16];
                size_t wl = i - wStart;
                if (wl < sizeof(w)) {
                    memcpy(w, b->p + wStart, wl);
                    w[wl] = 0;
                    if (!strcmp(w, "return") || !strcmp(w, "case") ||
                        !strcmp(w, "new") || !strcmp(w, "typeof") ||
                        !strcmp(w, "delete") || !strcmp(w, "void") ||
                        !strcmp(w, "in") || !strcmp(w, "of") ||
                        !strcmp(w, "instanceof"))
                        return 1;
                }
            }
        }
    }
    return 0;
}

/* One member of an object literal. */
typedef struct {
    int kind;            /* 0=verbatim  1=shorthand  2=method  3=accessor
                             4=spread  5=async-method */
    char name[128];
    const char *text;    /* slice into the source literal */
    size_t tlen;
    char params[1024];   /* raw params (methods) */
    char body[16384];    /* body text (methods), NUL-terminated */
    char clean[512];     /* cleaned params */
    char prol[16384];    /* param prologue (methods) */
} ObjMember;

/* Split the literal's inner text into depth-0 comma pieces and classify
 * each member.  Returns a malloc'd array, NULL on unsupported content. */
static ObjMember *obj_parse_members(const char *in, size_t n, int *count) {
    ObjMember *m = (ObjMember *)calloc(16, sizeof(ObjMember));
    int cap = 16, cnt = 0;
    const char *p = in, *end = in + n;
    if (!m) return NULL;
    while (p < end) {
        const char *e = p;
        int nest = 0;
        const char *q, *e2;
        size_t plen;
        while (e < end) {
            char c = *e;
            if (c == '\'' || c == '"') { e = skip_literal(e, c, c, 0); if (e > end) e = end; continue; }
            if (c == '`') { e = skip_template_raw(e); if (e > end) e = end; continue; }
            if (e[0] == '/' && e[1] == '/' && nest == 0) {
                while (e < end && *e != '\n') e++;
                continue;
            }
            if (c == '(' || c == '[' || c == '{') nest++;
            else if (c == ')' || c == ']' || c == '}') nest--;
            else if (c == ',' && nest == 0) break;
            e++;
        }
        q = p;
        while (q < e && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
        e2 = e;
        while (e2 > q && (e2[-1] == ' ' || e2[-1] == '\t' ||
                          e2[-1] == '\n' || e2[-1] == '\r')) e2--;
        plen = (size_t)(e2 - q);
        if (plen == 0) {                    /* empty / trailing comma */
            if (e >= end) break;
            p = e + 1;
            continue;
        }
        if (cnt >= cap) {
            ObjMember *nm;
            cap += 8;
            nm = (ObjMember *)realloc(m, (size_t)cap * sizeof(ObjMember));
            if (!nm) { free(m); return NULL; }
            m = nm;
            memset(m + cnt, 0, (size_t)(cap - cnt) * sizeof(ObjMember));
        }
        m[cnt].text = q;
        m[cnt].tlen = plen;
        if (plen > 3 && !strncmp(q, "...", 3)) {     /* spread piece */
            m[cnt].kind = 4;
            m[cnt].text = q + 3;
            m[cnt].tlen = plen - 3;
            cnt++;
            if (e >= end) break;
            p = e + 1;
            continue;
        }
        {
            const char *w = q;   /* method name start (set for kinds 2/5) */
            int kw = 0;
            if (src_word(q, "get") || src_word(q, "set")) {
                const char *n2 = skipws(q + 3);
                const char *ne = skip_ident(n2);
                const char *po = skipws(ne);
                if (ne > n2 && *po == '(' && ne < q + plen) {
                    m[cnt].kind = 3;
                    snprintf(m[cnt].name, sizeof(m[cnt].name), "%.*s",
                             (int)(ne - n2), n2);
                    kw = 1;
                }
            }
            if (!kw && src_word(q, "async")) {
                const char *n2 = skipws(q + 5);
                const char *ne = skip_ident(n2);
                const char *po = skipws(ne);
                if (ne > n2 && *po == '(' && ne < q + plen) {
                    m[cnt].kind = 5;
                    snprintf(m[cnt].name, sizeof(m[cnt].name), "%.*s",
                             (int)(ne - n2), n2);
                    w = n2;
                    kw = 1;
                }
            }
            if (!kw) {
                const char *we = skip_ident(w);
                if (we > w && is_ident_start((unsigned char)w[0])) {
                    const char *aq = skipws(we);
                    if (aq < q + plen && *aq == '(') {
                        m[cnt].kind = 2;    /* method shorthand */
                        snprintf(m[cnt].name, sizeof(m[cnt].name), "%.*s",
                                 (int)(we - w), w);
                    } else if (aq >= q + plen) {
                        m[cnt].kind = 1;    /* shorthand {a} */
                        snprintf(m[cnt].name, sizeof(m[cnt].name), "%.*s",
                                 (int)(we - w), w);
                    } else if (*aq == ':') {
                        m[cnt].kind = 0;    /* key: value */
                    } else {
                        free(m); return NULL;
                    }
                } else if (*w == '\'' || *w == '"') {
                    const char *ke = skip_literal(w, *w, *w, 1);
                    const char *aq = skipws(ke);
                    if (ke <= q + plen && aq < q + plen && *aq == ':')
                        m[cnt].kind = 0;
                    else { free(m); return NULL; }
                } else {
                    free(m); return NULL;  /* computed key, number key, junk */
                }
            }
        /* methods: extract params + body (w points at the method name) */
        if (m[cnt].kind == 2 || m[cnt].kind == 5) {
            const char *po = skipws(skip_ident(w));
            const char *pe, *bo, *be;
            if (*po != '(') { free(m); return NULL; }
            pe = scan_balanced(po);
            if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') { free(m); return NULL; }
            {
                size_t pn = (size_t)(pe - po - 1);
                if (pn >= sizeof(m[cnt].params)) { free(m); return NULL; }
                memcpy(m[cnt].params, po + 1, pn);
                m[cnt].params[pn] = 0;
            }
            bo = skipws(pe);
            if (*bo != '{') { free(m); return NULL; }
            be = scan_balanced(bo);
            if (be[-1] != ']' && be[-1] != '}' && be[-1] != ')') { free(m); return NULL; }
            {
                size_t bn = (size_t)(be - bo - 2);
                if (bn >= sizeof(m[cnt].body)) { free(m); return NULL; }
                memcpy(m[cnt].body, bo + 1, bn);
                m[cnt].body[bn] = 0;
            }
        }
        }
        cnt++;
        if (e >= end) break;
        p = e + 1;
    }
    *count = cnt;
    return m;
}

/* Pre-build cleaned params + prologues for all method members.
 * 0 ok, -1 unsupported (caller leaves the literal verbatim). */
static int obj_prep_methods(ObjMember *m, int count, const VwEsHooks *hooks) {
    int i;
    for (i = 0; i < count; i++) {
        if (m[i].kind != 2 && m[i].kind != 5) continue;
        {
            Buf clean, prol;
            clean.p = m[i].clean; clean.cap = sizeof(m[i].clean);
            clean.len = 0; clean.overflow = 0;
            prol.p = m[i].prol; prol.cap = sizeof(m[i].prol);
            prol.len = 0; prol.overflow = 0;
            if (build_params(m[i].params, &clean, &prol, hooks) != 0)
                return -1;
            if (clean.overflow || prol.overflow) return -1;
            m[i].clean[clean.len] = 0;
            m[i].prol[prol.len] = 0;
        }
    }
    return 0;
}

/* Emit one member (already prepped). */
static void obj_emit_member(Buf *b, ObjMember *m, const VwEsHooks *hooks) {
    switch (m->kind) {
    case 0:
    case 3: {
        const char *t = dupn(m->text, m->tlen);
        if (t) { xform_piece(t, b, hooks); free((void *)t); }
        break;
    }
    case 1:
        bputs(b, m->name);
        bputs(b, ": ");
        bputs(b, m->name);
        break;
    case 2:
        bputs(b, m->name);
        bputs(b, ": function (");
        bputs(b, m->clean);
        bputs(b, ") { ");
        bputs(b, m->prol);
        xform_piece(m->body, b, hooks);
        bputs(b, " }");
        break;
    case 5: {
        char *chain = (char *)malloc(strlen(m->body) * 8 + 4096);
        if (!chain) break;
        chain[0] = 0;
        bputs(b, m->name);
        bputs(b, ": function (");
        bputs(b, m->clean);
        bputs(b, ") { ");
        bputs(b, m->prol);
        if (async_chain_body(m->clean, m->body, chain,
                              strlen(m->body) * 8 + 4096, hooks) == 0)
            bputs(b, chain);
        else
            xform_piece(m->body, b, hooks);
        bputs(b, " }");
        free(chain);
        break;
    }
    case 4: {   /* spread: text already stripped of "..." by the parser */
        const char *t = dupn(m->text, m->tlen);
        if (t) { xform_piece(t, b, hooks); free((void *)t); }
        break;
    }
    }
}

/* Try the object-literal rewrite at the `{` the source points at.
 * Returns 1 when rewritten (p advanced past the literal), 0 verbatim.
 * The literal is (re)emitted parenthesized, so a statement-position
 * shorthand (`{a, b}`) still parses as an expression statement. */
static int try_object_literal(Buf *b, const char **pp,
                              const VwEsHooks *hooks) {
    const char *p = *pp;
    const char *end = scan_balanced(p);
    ObjMember *m;
    int count = 0, i;
    int features = 0, spread = 0;
    if (end[-1] != '}') return 0;
    /* parameter group context? patterns must NOT become literals */
    if (g_paren_top > 0 &&
        output_params_group(b, g_parens[g_paren_top - 1].dst_at) != (size_t)-1)
        return 0;
    m = obj_parse_members(p + 1, (size_t)(end - p - 2), &count);
    if (!m) return 0;
    for (i = 0; i < count; i++) {
        if (m[i].kind != 0 && m[i].kind != 3) features = 1;
        if (m[i].kind == 4) spread = 1;
    }
    if (!features) { free(m); return 0; }
    if (obj_prep_methods(m, count, hooks) != 0) { free(m); return 0; }
    if (spread) {
        /* accessors cannot merge through Object.assign */
        for (i = 0; i < count; i++)
            if (m[i].kind == 3) { free(m); return 0; }
    }
    bputs(b, "(");
    if (spread) {
        bputs(b, "Object.assign({}");
        for (i = 0; i < count; i++) {
            if (m[i].kind == 4) {
                bputs(b, ", ");
                obj_emit_member(b, &m[i], hooks);
            } else {
                bputs(b, ", { ");
                obj_emit_member(b, &m[i], hooks);
                bputs(b, " }");
            }
        }
        bputs(b, "))");
    } else {
        bputs(b, "{ ");
        for (i = 0; i < count; i++) {
            if (i) bputs(b, ", ");
            obj_emit_member(b, &m[i], hooks);
        }
        bputs(b, " })");
    }
    free(m);
    *pp = end;
    return 1;
}

/* Output-buffer position just past the last copied comment.  Comments are
 * copied VERBATIM into the output, so the let/const word-boundary check
 * below would otherwise see the comment's trailing letters ("DOM
 * selections\nconst x") as an adjacent identifier and silently skip the
 * conversion.  A declaration that follows a comment (with only whitespace
 * in between) is always a word boundary in real JS. */
static size_t g_comment_end = (size_t)-1;

static const char *lex_pass(const char *p, Buf *b, const VwEsHooks *hooks,
                            int depth) {
    if (depth > 8) { while (*p) bput(b, *p++); return p; }   /* safety valve */

    while (*p && !b->overflow) {
        /* ---- literals and comments: copy verbatim ------------------------- */
        if (p[0] == '/' && p[1] == '/') {                 /* line comment */
            while (*p && *p != '\n') bput(b, *p++);
            g_comment_end = b->len;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {                  /* block comment */
            bputn(b, p, 2); p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) bput(b, *p++);
            if (p[0] == '*') { bputn(b, p, 2); p += 2; }
            g_comment_end = b->len;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            const char *q = skip_literal(p, *p, *p, 0);
            bputn(b, p, (size_t)(q - p));
            p = q;
            continue;
        }
        if (*p == '`') {                                   /* template */
            p = convert_template(p, b, hooks, depth + 1);
            continue;
        }
        if (*p == '/') {                                   /* / or regex */
            if (regex_position(b)) {
                const char *q = skip_regex(p);
                bputn(b, p, (size_t)(q - p));
                p = q;
            } else bput(b, *p++);
            continue;
        }

        /* ---- ES7+ operators (47) ----------------------------------------- */
        if (p[0] == '*' && p[1] == '*') {
            if (try_exponent(b, &p, hooks)) continue;
        }
        if (p[0] == '&' && p[1] == '&' && p[2] == '=') {
            if (try_logical_assign(b, &p, hooks, 1)) continue;
        }
        if (p[0] == '|' && p[1] == '|' && p[2] == '=') {
            if (try_logical_assign(b, &p, hooks, 0)) continue;
        }
        if (p[0] == '?' && p[1] == '?') {
            if (try_coalesce(b, &p, hooks)) continue;
        }
        if (p[0] == '?' && p[1] == '.' && !isdigit((unsigned char)p[2])) {
            if (try_optchain(b, &p, hooks)) continue;
        }
        if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
            if (try_call_spread(b, &p, hooks)) {
                /* NOTE: if it returned 0 the "..." was NOT consumed; the
                   "." falls through to the plain-char path below, which
                   copies it and the next loop iteration handles ".." - to
                   avoid that, copy all three dots verbatim when the
                   spread rewrite did not apply (e.g. arrow params, where
                   the ")" handler rebuilds the list anyway). */
                bputn(b, p, 3);
                p += 3;
                continue;
            }
        }
        if (p[0] == 'c' && p[1] == 'a' && p[2] == 't' && p[3] == 'c' &&
            p[4] == 'h') {
            if (try_optional_catch(b, &p)) continue;
        }

        /* ---- "[" array literal / destructuring (47) --------------------- */
        if (*p == '[') {
            if (try_destructure(b, &p, hooks)) continue;
            if (try_assign_destructure_array(b, &p, hooks)) continue;
            if (try_array_spread(b, &p, hooks)) continue;
        }

        /* ---- "{" object literal / destructuring (47) -------------------- */
        if (*p == '{') {
            if (try_destructure(b, &p, hooks)) continue;
            if (try_object_literal(b, &p, hooks)) continue;
        }

        /* ---- "(" group tracking (for later "(a) =>" detection) ----------- */
        if (*p == '(') {
            if (try_paren_destructure(b, &p, hooks)) continue;
            if (g_paren_top < MAX_PARENS) {
                g_parens[g_paren_top].dst_at = b->len;
                g_parens[g_paren_top].src_at = p + 1;
                g_paren_top++;
            }
            bput(b, *p++);
            continue;
        }
        if (*p == ')') {
            ParenMark mark = {0, 0};
            int is_top = 0;
            if (g_paren_top > 0) { g_paren_top--; mark = g_parens[g_paren_top]; is_top = 1; }
            bput(b, *p++);
            if (is_top) {
                /* peek: "=>" right after ")" ? */
                const char *q = p;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                /* ES7+ (47): complex parameter list (defaults, rest,
                   patterns)?  then rebuild it - for both function
                   headers and "(...) =>" arrows.  Plain lists fall
                   through to the original arrow logic. */
                if (params_need_rewrite(mark.src_at,
                                        (size_t)((p - 1) - mark.src_at))) {
                    size_t fnpos = output_params_group(b, mark.dst_at);
                    int is_arrow = (q[0] == '=' && q[1] == '>');
                    if (fnpos != (size_t)-1 || is_arrow) {
                        Buf clean, prol;
                        char clean_buf[1024], prol_buf[16384];
                        clean.p = clean_buf; clean.cap = sizeof(clean_buf);
                        clean.len = 0; clean.overflow = 0;
                        prol.p = prol_buf; prol.cap = sizeof(prol_buf);
                        prol.len = 0; prol.overflow = 0;
                        if (build_params(mark.src_at, &clean, &prol, hooks) == 0 &&
                            !clean.overflow && !prol.overflow) {
                            btruncate(b, mark.dst_at + 1);  /* keep "(" */
                            /* an ARROW has no "function" keyword in the
                             * source: the rebuilt group needs one, or the
                             * output is "(__vw_pN) { ... }" - a syntax
                             * error the 2026-09-26 (48) es7 gate caught
                             * (function headers already have it).  The
                             * insert must come AFTER the truncate: binsert
                             * shifts positions, so doing it first moved
                             * the "(" and the truncate ate it. */
                            if (is_arrow && binsert(b, mark.dst_at, "function ") != 0) {
                                /* insert failed (no room): bail out of
                                 * the rewrite, keep the original group */
                                bputn(b, clean_buf, clean.len);
                                bputs(b, ")");
                                continue;
                            }
                            bputn(b, clean_buf, clean.len);
                            bputs(b, ")");
                            if (is_arrow) {
                                p = q + 2;                  /* past "=>" */
                                while (*p == ' ' || *p == '\t' ||
                                       *p == '\n' || *p == '\r') p++;
                                if (*p == '{') {
                                    bputs(b, " { ");
                                    bputn(b, prol_buf, prol.len);
                                    p++;
                                } else {
                                    const char *e = arrow_body_end(p);
                                    bputs(b, " { ");
                                    bputn(b, prol_buf, prol.len);
                                    arrow_body_emit(p, (size_t)(e - p), b,
                                                    hooks, " return ", "; }");
                                    p = e;
                                }
                            } else {
                                /* function header: inject the prologue
                                   after the "{" that must follow */
                                const char *h = p;
                                while (*h == ' ' || *h == '\t' ||
                                       *h == '\n' || *h == '\r') h++;
                                if (*h == '{') {
                                    bputs(b, " { ");
                                    bputn(b, prol_buf, prol.len);
                                    p = h + 1;
                                }
                                /* else: leave as-is (engine will error) */
                            }
                            continue;
                        }
                    }
                }
                if (q[0] == '=' && q[1] == '>') {
                    size_t k;
                    /* is this group a plain parameter list?
                       (only idents, commas, ws, and one nesting level) */
                    int plain = 1;
                    int nest = 0;
                    for (k = 0; k < (size_t)(p - 1 - mark.src_at); k++) {
                        char c = mark.src_at[k];
                        if (c == '(' || c == '[') nest++;
                        else if (c == ')' || c == ']') nest--;
                        else if (nest == 0 && !(is_ident_char((unsigned char)c)
                                                || c == ',' || c == ' '
                                                || c == '\t' || c == '\n'
                                                || c == '\r')) { plain = 0; break; }
                    }
                    if (plain && binsert(b, mark.dst_at, "function ") == 0) {
                        p = q + 2;               /* past "=>" */
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p == '{') {
                            /* block body: keep it, close later normally */
                            bputs(b, " {");
                            p++;
                        } else {
                            /* expression body: wrap in "return ... ; }",
                               RECURSIVELY transformed (nested arrows) */
                            const char *e = arrow_body_end(p);
                            arrow_body_emit(p, (size_t)(e - p), b, hooks,
                                            " { return ", "; }");
                            p = e;
                        }
                        continue;
                    }
                }
            }
            continue;
        }

        /* ---- "=>" after a single identifier:  x => ... ------------------- */
        if (p[0] == '=' && p[1] == '>') {
            /* backtrack in the OUTPUT over exactly one identifier */
            size_t i = b->len;
            while (i > 0 && (b->p[i - 1] == ' ' || b->p[i - 1] == '\t'
                             || b->p[i - 1] == '\n' || b->p[i - 1] == '\r'))
                i--;
            size_t e = i;
            while (i > 0 && is_ident_char((unsigned char)b->p[i - 1])) i--;
            if (i < e) {                                    /* found an ident */
                /* boundary before it? (not obj.x =>, not a.b c =>) */
                size_t wl = e - i;
                size_t j = i;
                while (j > 0 && (b->p[j - 1] == ' ' || b->p[j - 1] == '\t'
                                 || b->p[j - 1] == '\n' || b->p[j - 1] == '\r'))
                    j--;
                if (j > 0) {
                    char prev = b->p[j - 1];
                    if (is_ident_char((unsigned char)prev) || prev == '.') {
                        /* part of a larger expression: leave alone */
                        bputs(b, "=>");
                        p += 2;
                        continue;
                    }
                }
                /* rewrite: "function (" + ident, drop the " =>" */
                char id[64];
                if (wl < sizeof(id)) {
                    memcpy(id, b->p + i, wl);
                    id[wl] = 0;
                    btruncate(b, i);
                    bputs(b, "function (");
                    bputs(b, id);
                    p += 2;                                  /* past "=>" */
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '{') { bputs(b, ") {"); p++; }
                    else {
                        const char *e2 = arrow_body_end(p);
                        arrow_body_emit(p, (size_t)(e2 - p), b, hooks,
                                        ") { return ", "; }");
                        p = e2;
                    }
                    continue;
                }
            }
            bputs(b, "=>");
            p += 2;
            continue;
        }

        /* ---- for..of -> indexed loop ------------------------------------- */
        if (p[0] == 'o' && p[1] == 'f' &&
            (p[2] == ' ' || p[2] == '\t' || p[2] == '\n' || p[2] == '\r')) {
            if (try_for_of(b, &p, hooks, depth)) continue;
            if (try_for_of_pattern(b, &p, hooks)) continue;
        }

        /* ---- let / const -> var ------------------------------------------ */
        if ((p[0] == 'l' && p[1] == 'e' && p[2] == 't' &&
             (p[3] == ' ' || p[3] == '\t' || p[3] == '\n' || p[3] == '\r'
              || p[3] == '(')) ||
            (p[0] == 'c' && p[1] == 'o' && p[2] == 'n' && p[3] == 's' &&
             p[4] == 't' &&
             (p[5] == ' ' || p[5] == '\t' || p[5] == '\n' || p[5] == '\r'))) {
            /* word boundary in front? (not "infolet", not ".let") */
            size_t i = b->len;
            char prev = 0;
            while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
            /* only whitespace between the last comment and here? then the
               comment's letters cannot be an adjacent identifier */
            int after_comment = (i == g_comment_end) ||
                (i + 1 == g_comment_end && b->p[i] == '\r');
            if (i > 0) prev = b->p[i - 1];
            if (after_comment ||
                (!is_ident_char((unsigned char)prev) && prev != '.')) {
                /* and the next non-ws char is an identifier start? */
                const char *q = p + (p[0] == 'l' ? 3 : 5);
                if (*q == '(') {             /* for (let i=... */
                    while (*q != ' ') q++;
                } else {
                    while (*q == ' ' || *q == '\t' || *q == '\n'
                           || *q == '\r') q++;
                }
                if (is_ident_start((unsigned char)*q) || *q == '[' ||
                    *q == '{') {
                    bputs(b, "var");
                    p += (p[0] == 'l' ? 3 : 5);
                    continue;
                }
            }
        }

        /* ---- plain character --------------------------------------------- */
        bput(b, *p++);
    }
    return p;
}

/* --- import / export handling --------------------------------------------- */

/* Does the line starting at p begin (after whitespace) with `word`? */
static int line_starts_with(const char *p, const char *word) {
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, word, strlen(word)) != 0) return 0;
    p += strlen(word);
    return (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\'' || *p == '{');
}

/* Extract the "specifier" (the quoted path) from an import line. */
static int import_specifier(const char *line, char *out, size_t cap) {
    const char *s = strchr(line, '\'');
    const char *d = strchr(line, '"');
    const char *q = NULL;
    if (s && (!d || s < d)) q = s; else if (d) q = d;
    if (!q) return 0;
    q++;
    {
        size_t n = 0;
        while (q[n] && q[n] != '\'' && q[n] != '"' && n < cap - 1) { out[n] = q[n]; n++; }
        out[n] = 0;
        return n > 0;
    }
}

/* Run an imported file (load -> transform -> evaluate), with cycle guard. */
static void run_import(const char *spec, const VwEsHooks *hooks) {
    char url[1024], *body;
    int i;
    if (!hooks || !hooks->resolve_url || !hooks->load_file || !hooks->evaluate)
        return;
    if (hooks->resolve_url(hooks->userdata, "", spec, url, sizeof(url)) != 0)
        return;
    for (i = 0; i < g_loading_count; i++)
        if (!strcmp(g_loading[i], url)) return;      /* cycle: already busy */
    if (g_loading_count >= MAX_LOADING) return;
    body = (char *)malloc(256 * 1024);
    if (!body) return;
    if (hooks->load_file(hooks->userdata, url, body, 256 * 1024) >= 0) {
        char es5[256 * 1024];
        snprintf(g_loading[g_loading_count], 256, "%.255s", url);
        g_loading_count++;
        es5[0] = 0;
        if (vw_es2015_transform(body, es5, sizeof(es5), hooks) != 1)
            snprintf(es5, sizeof(es5), "%s", body);
        hooks->evaluate(hooks->userdata, es5);
        g_loading_count--;
    }
    free((void *)body);
}

/* Phase 1: line-oriented import/export handling.  Writes into dst. */
static int import_export_pass(const char *src, Buf *b,
                              const VwEsHooks *hooks) {
    const char *line = src;
    int changed = 0;
    while (*line && !b->overflow) {
        const char *nl = line;
        while (*nl && *nl != '\n') nl++;
        size_t len = (size_t)(nl - line);
        if (line_starts_with(line, "import")) {
            char spec[512];
            if (import_specifier(line, spec, sizeof(spec)))
                run_import(spec, hooks);
            bputs(b, "\n");                    /* replace with a blank line */
            changed = 1;
        } else if (line_starts_with(line, "export")) {
            const char *q = line;
            while (*q == ' ' || *q == '\t') q++;
            q += 6;                            /* past "export" */
            while (*q == ' ') q++;
            if (!strncmp(q, "default", 7) && (q[7] == ' ' || q[7] == '\n')) {
                q += 7;
                while (*q == ' ') q++;
                bputn(b, q, len - (size_t)(q - line));   /* keep the rest */
            } else if (!strncmp(q, "{", 1)) {
                bputs(b, "\n");                          /* export {a,b}: drop */
            } else {
                bputn(b, q, len - (size_t)(q - line));   /* keep declaration */
            }
            changed = 1;
        } else {
            bputn(b, line, len);
        }
        if (*nl == '\n') bput(b, '\n');
        line = nl + (*nl == '\n');
    }
    return changed;
}


/* --- public entry ----------------------------------------------------------- */

static int async_chain_body(const char *params_clean, const char *body,
                            char *out, size_t out_cap,
                            const VwEsHooks *hooks) {
    size_t bl = strlen(body);
    char *sb;
    char *hoisted[256];
    size_t hcount = 0;
    Buf ob;
    const char *p;
    size_t seg_off;
    int naw = 0;
    size_t i;
    (void)params_clean;
    ob.p = out; ob.cap = out_cap; ob.len = 0; ob.overflow = 0;
    out[0] = 0;
    sb = (char *)malloc(bl + 16);
    if (!sb) return -1;

    /* --- pass 1: strip var/let/const at ident boundaries, hoist names.
       Patterns (let [a,b]) after the keyword make the whole async
       function unsupported (return -1). --- */
    {
        const char *r = body;
        char *w = sb;
        while (*r) {
            if (*r == '\'' || *r == '"') {
                const char *e = skip_literal(r, *r, *r, 0);
                while (r < e) *w++ = *r++;
                continue;
            }
            if (*r == '`') {
                const char *e = skip_template_raw(r);
                while (r < e) *w++ = *r++;
                continue;
            }
            if ((src_word(r, "var") || src_word(r, "let") ||
                 src_word(r, "const"))) {
                /* boundary: output tail before the keyword */
                size_t k = (size_t)(w - sb);
                char prev = 0;
                while (k > 0 && isspace((unsigned char)sb[k - 1])) k--;
                if (k > 0) prev = sb[k - 1];
                if (!is_ident_char((unsigned char)prev) && prev != '.') {
                    const char *q2 = skipws(r + (src_word(r, "var") ? 3 :
                                                 src_word(r, "let") ? 3 : 5));
                    if (is_ident_start((unsigned char)*q2)) {
                        /* copy the declarator list, dropping the keyword */
                        /* names: ident at list start and after depth-0 , */
                        r = q2;
                        while (*r && *r != ';') {
                            char c = *r;
                            if (c == '\'' || c == '"') {
                                const char *e = skip_literal(r, c, c, 0);
                                while (r < e) *w++ = *r++;
                                continue;
                            }
                            if (c == '(' || c == '[' || c == '{') {
                                const char *e = scan_balanced(r);
                                while (r < e) *w++ = *r++;
                                continue;
                            }
                            if (c == ',' ) {
                                /* look ahead: next declarator name */
                                const char *nq = skipws(r + 1);
                                if (is_ident_start((unsigned char)*nq) &&
                                    hcount < 256) {
                                    const char *ne = skip_ident(nq);
                                    hoisted[hcount++] = dupn(nq, (size_t)(ne - nq));
                                }
                                *w++ = *r++;
                                continue;
                            }
                            *w++ = *r++;
                        }
                        continue;
                    } else if (*q2 == '[' || *q2 == '{') {
                        free(sb);
                        for (i = 0; i < hcount; i++) free(hoisted[i]);
                        return -1;      /* pattern decl in async: unsupported */
                    }
                }
            }
            *w++ = *r++;
        }
        *w = 0;
        /* first declarator name of each stripped decl was missed: we
           skipped it above; recover by scanning for the "= " we just
           copied?  Simpler: record happened only after commas.  Redo:
           scan sb for bare "NAME = " at stmt starts is unreliable, so
           instead re-walk: after the keyword we land ON the first name -
           record it there.  Handled below in a second mini-scan. */
    }
    /* recover first declarator names: find "= " preceded by a name whose
       preceding sig char is ';' '{' '}' or start.  Cheap heuristic that
       catches the common "x = 1" statements the stripping produced. */
    {
        const char *r = sb;
        while (*r) {
            if (*r == '\'' || *r == '"') { r = skip_literal(r, *r, *r, 0); continue; }
            if (*r == '`') { r = skip_template_raw(r); continue; }
            if (r[0] == '/' && r[1] == '/') { while (*r && *r != '\n') r++; continue; }
            if (is_ident_start((unsigned char)*r)) {
                const char *ne = skip_ident(r);
                const char *aq = skipws(ne);
                if (*aq == '=' && aq[1] != '=' && aq[1] != '>') {
                    size_t k = (size_t)(r - sb);
                    while (k > 0 && isspace((unsigned char)sb[k - 1])) k--;
                    if (k == 0 || sb[k - 1] == ';' || sb[k - 1] == '{' ||
                        sb[k - 1] == '}') {
                        int dup = 0;
                        for (i = 0; i < hcount; i++)
                            if (hoisted[i] && !strcmp(hoisted[i], r) &&
                                strlen(hoisted[i]) == (size_t)(ne - r)) dup = 1;
                        if (!dup && hcount < 256) {
                            char *hh = dupn(r, (size_t)(ne - r));
                            if (hh) hoisted[hcount++] = hh;
                        }
                    }
                }
                r = ne;
                continue;
            }
            r++;
        }
    }

    /* --- pass 2: count top-level awaits; reject braced awaits --- */
    {
        int nest = 0, brace = 0;
        p = sb;
        while (*p) {
            char c = *p;
            if (c == '\'' || c == '"') { p = skip_literal(p, c, c, 0); continue; }
            if (c == '`') { p = skip_template_raw(p); continue; }
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
            if (p[0] == '/' && p[1] == '*') {
                p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
                if (*p) p += 2;
                continue;
            }
            if (c == '(' || c == '[') nest++;
            else if (c == ')' || c == ']') nest--;
            else if (c == '{') brace++;
            else if (c == '}') brace--;
            else if (nest == 0 && src_word(p, "await")) {
                if (brace > 0) {
                    /* await inside try/catch/loop blocks cannot be
                       naively statement-split: whole fn unsupported */
                    free(sb);
                    for (i = 0; i < hcount; i++) free(hoisted[i]);
                    return -1;
                }
                naw++;
            }
            p++;
        }
    }

    if (naw == 0) {
        bputs(&ob, "return Promise.resolve().then(function () { ");
        xform_piece(body, &ob, hooks);          /* raw body (no stripping) */
        bputs(&ob, " });");
        if (ob.overflow) { free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]); return -1; }
        out[ob.len] = 0;
        free(sb);
        for (i = 0; i < hcount; i++) free(hoisted[i]);
        return 0;
    }

    /* --- pass 3: split at the awaits --- */
    bputs(&ob, "var ");
    if (hcount == 0) bputs(&ob, "__vw_h0");
    for (i = 0; i < hcount; i++) {
        if (i) bputs(&ob, ", ");
        bputs(&ob, hoisted[i]);
    }
    bputs(&ob, "; return Promise.resolve().then(function () { ");
    seg_off = 0;
    {
        int nest = 0;
        p = sb;
        while (*p) {
            char c = *p;
            if (c == '\'' || c == '"') { p = skip_literal(p, c, c, 0); continue; }
            if (c == '`') { p = skip_template_raw(p); continue; }
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
            if (p[0] == '/' && p[1] == '*') {
                p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
                if (*p) p += 2;
                continue;
            }
            if (c == '(' || c == '[') { nest++; p++; continue; }
            if (c == ')' || c == ']') { nest--; p++; continue; }
            if (nest != 0 || !src_word(p, "await")) { p++; continue; }

            /* await at depth 0: classify */
            {
                const char *aw = p;
                const char *ae, *ee;
                const char *j = aw;
                size_t k;
                char prev = 0;
                while (j > sb && isspace((unsigned char)j[-1])) j--;
                k = (size_t)(j - sb);
                if (k > 0) prev = sb[k - 1];

                /* expression end: to ';' at depth 0 */
                ae = skipws(aw + 5);
                ee = scan_expr_end(ae);
                if (*ee != ';') {
                    free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]);
                    return -1;          /* await inside an expression */
                }
                if (k >= 6 && !strncmp(sb + k - 6, "return", 6) &&
                    (k - 6 == 0 || !is_ident_char((unsigned char)sb[k - 7]))) {
                    /* return await E;  ->  return (E); */
                    {
                        char *seg = dupn(sb + seg_off, (size_t)(aw - sb) - seg_off);
                        if (!seg) { free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]); return -1; }
                        xform_piece(seg, &ob, hooks);
                        free(seg);
                    }
                    {
                        char *ex = dupn(ae, (size_t)(ee - ae));
                        bputs(&ob, " (");
                        if (ex) { xform_piece(ex, &ob, hooks); free(ex); }
                        bputs(&ob, "); ");
                    }
                    seg_off = (size_t)(ee - sb) + 1;
                    p = ee + 1;
                    continue;
                }
                if (prev == ';'|| prev == '{' || prev == '}' ||
                    (size_t)(j - sb) == 0) {
                    /* statement await:  await E;  */
                    {
                        char *seg = dupn(sb + seg_off, (size_t)(aw - sb) - seg_off);
                        if (!seg) { free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]); return -1; }
                        xform_piece(seg, &ob, hooks);
                        free(seg);
                    }
                    bputs(&ob, "return (");
                    {
                        char *ex = dupn(ae, (size_t)(ee - ae));
                        if (ex) { xform_piece(ex, &ob, hooks); free(ex); }
                    }
                    bputs(&ob, "); }).then(function () { ");
                    seg_off = (size_t)(ee - sb) + 1;
                    p = ee + 1;
                    continue;
                }
                /* target = await E;  ? */
                {
                    const char *te = j;          /* just before "await" */
                    const char *ts = j;
                    const char *eq;
                    int ok = 0;
                    while (te > sb && isspace((unsigned char)te[-1])) te--;
                    eq = te;
                    if (eq > sb && eq[-1] == '=' &&
                        (eq - 1 == sb ||
                         (sb[eq - sb - 2] != '=' && sb[eq - sb - 2] != '!' &&
                          sb[eq - sb - 2] != '<' && sb[eq - sb - 2] != '>' &&
                          sb[eq - sb - 2] != '+' && sb[eq - sb - 2] != '-' &&
                          sb[eq - sb - 2] != '*' && sb[eq - sb - 2] != '/' &&
                          sb[eq - sb - 2] != '%' && sb[eq - sb - 2] != '&' &&
                          sb[eq - sb - 2] != '|' && sb[eq - sb - 2] != '?'))) {
                        te = eq - 1;             /* end of the target text */
                        while (te > sb && isspace((unsigned char)te[-1])) te--;
                        ts = te;
                        if (ts >= sb &&
                            (ts == sb ||
                             is_ident_char((unsigned char)sb[ts - sb - 1]))) {
                            while (ts > sb &&
                                   is_ident_char((unsigned char)ts[-1])) ts--;
                            while (ts > sb && ts[-1] == '.' && ts - 1 > sb &&
                                   is_ident_char((unsigned char)ts[-2])) {
                                ts -= 2;
                                while (ts > sb &&
                                       is_ident_char((unsigned char)ts[-1])) ts--;
                            }
                            {
                                size_t k2 = (size_t)(ts - sb);
                                while (k2 > 0 &&
                                       isspace((unsigned char)sb[k2 - 1])) k2--;
                                if ((k2 == 0 && ts > sb) || sb[k2 - 1] == ';' ||
                                    sb[k2 - 1] == '{' || sb[k2 - 1] == '}')
                                    ok = 1;
                            }
                        }
                    }
                    if (ok && ts < te) {
                        /* target = await E; */
                        char rname[32];
                        char *tgt = dupn(ts, (size_t)(te - ts));
                        char *seg;
                        if (!tgt) { free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]); return -1; }
                        seg = dupn(sb + seg_off, (size_t)(ts - sb) - seg_off);
                        if (!seg) { free(tgt); free(sb); for (i = 0; i < hcount; i++) free(hoisted[i]); return -1; }
                        xform_piece(seg, &ob, hooks);
                        free(seg);
                        bputs(&ob, "return (");
                        {
                            char *ex = dupn(ae, (size_t)(ee - ae));
                            if (ex) { xform_piece(ex, &ob, hooks); free(ex); }
                        }
                        snprintf(rname, sizeof(rname), "__vw_r%d", g_temp_seq++);
                        bputs(&ob, "); }).then(function (");
                        bputs(&ob, rname);
                        bputs(&ob, ") { ");
                        bputs(&ob, tgt);
                        bputs(&ob, " = ");
                        bputs(&ob, rname);
                        bputs(&ob, "; ");
                        free(tgt);
                        seg_off = (size_t)(ee - sb) + 1;
                        p = ee + 1;
                        continue;
                    }
                }
                /* unsupported await placement */
                free(sb);
                for (i = 0; i < hcount; i++) free(hoisted[i]);
                return -1;
            }
        }
        /* tail segment */
        {
            const char *seg = sb + seg_off;
            xform_piece(seg, &ob, hooks);
            bputs(&ob, " });");
        }
    }
    if (ob.overflow) {
        free(sb);
        for (i = 0; i < hcount; i++) free(hoisted[i]);
        return -1;
    }
    out[ob.len] = 0;
    free(sb);
    for (i = 0; i < hcount; i++) free(hoisted[i]);
    return 0;
}

/* --- async function / async arrow rewrite ------------------------------- */

/* At `async`: either `async function NAME?(params) { body }` or
   `async (params) => body` / `async ident => body`. */
static int try_async(Buf *b, const char **pp, const VwEsHooks *hooks) {
    const char *p = *pp;
    const char *q;
    size_t i = b->len;
    char prev = 0;
    char *params_raw = NULL, *body_raw = NULL;
    char clean_buf[1024], prol_buf[16384], chain_buf[65536];
    Buf clean, prol;
    const char *body_start;

    if (!src_word(p, "async")) return 0;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i > 0) prev = b->p[i - 1];
    if (is_ident_char((unsigned char)prev) || prev == '.') return 0;
    q = skipws(p + 5);

    if (src_word(q, "function")) {
        const char *n, *ne, *po, *pe, *bo, *be;
        char name[128] = "";
        n = skipws(q + 8);
        ne = skip_ident(n);
        po = (ne > n) ? skipws(ne) : n;
        if (ne == n) po = n;
        if (*po != '(') return 0;
        pe = scan_balanced(po);
        if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') return 0;
        bo = skipws(pe);
        if (*bo != '{') return 0;
        be = scan_balanced(bo);
        if (be[-1] != ']' && be[-1] != '}' && be[-1] != ')') return 0;
        if (ne > n && (size_t)(ne - n) < sizeof(name))
            { memcpy(name, n, (size_t)(ne - n)); name[ne - n] = 0; }
        params_raw = dupn(po + 1, (size_t)(pe - po - 1));
        body_raw = dupn(bo + 1, (size_t)(be - bo - 2));
        (void)body_start;
        if (!params_raw || !body_raw) {
            free(params_raw); free(body_raw);
            return 0;
        }
        /* emit */
        clean.p = clean_buf; clean.cap = sizeof(clean_buf);
        clean.len = 0; clean.overflow = 0;
        prol.p = prol_buf; prol.cap = sizeof(prol_buf);
        prol.len = 0; prol.overflow = 0;
        if (build_params(params_raw, &clean, &prol, hooks) != 0 ||
            clean.overflow || prol.overflow) {
            free(params_raw); free(body_raw);
            return 0;
        }
        if (async_chain_body(clean_buf, body_raw, chain_buf,
                             sizeof(chain_buf), hooks) != 0) {
            free(params_raw); free(body_raw);
            return 0;
        }
        bputs(b, "function ");
        bputs(b, name);
        bputs(b, " (");
        bputn(b, clean_buf, clean.len);
        bputs(b, ") { ");
        bputn(b, prol_buf, prol.len);
        bputs(b, chain_buf);
        bputs(b, " }");
        free(params_raw); free(body_raw);
        *pp = be;
        return 1;
    }
    if (*q == '(') {
        /* async (params) => body */
        const char *pe = scan_balanced(q);
        const char *aq;
        if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') return 0;
        aq = skipws(pe);
        if (!(aq[0] == '=' && aq[1] == '>')) return 0;
        params_raw = dupn(q + 1, (size_t)(pe - q - 1));
        if (!params_raw) return 0;
        body_start = skipws(aq + 2);
    } else if (is_ident_start((unsigned char)*q)) {
        /* async ident => body */
        const char *ne = skip_ident(q);
        const char *aq = skipws(ne);
        if (!(aq[0] == '=' && aq[1] == '>')) return 0;
        /* Allocate and terminate the single-identifier parameter copy. */
        params_raw = (char *)malloc((size_t)(ne - q) + 1);
        if (!params_raw) return 0;
        memcpy(params_raw, q, (size_t)(ne - q));
        params_raw[ne - q] = 0;
        body_start = skipws(aq + 2);
    } else return 0;

    /* arrow body */
    if (*body_start == '{') {
        const char *be = scan_balanced(body_start);
        if (be[-1] != ']' && be[-1] != '}' && be[-1] != ')') { free(params_raw); return 0; }
        body_raw = dupn(body_start + 1, (size_t)(be - body_start - 2));
        *pp = be;
    } else {
        const char *e = arrow_body_end(body_start);
        body_raw = (char *)malloc((size_t)(e - body_start) + 64);
        if (!body_raw) { free(params_raw); return 0; }
        memcpy(body_raw, "return ", 7);
        memcpy(body_raw + 7, body_start, (size_t)(e - body_start));
        body_raw[7 + (e - body_start)] = 0;
        *pp = e;
    }
    if (!body_raw) { free(params_raw); return 0; }
    clean.p = clean_buf; clean.cap = sizeof(clean_buf);
    clean.len = 0; clean.overflow = 0;
    prol.p = prol_buf; prol.cap = sizeof(prol_buf);
    prol.len = 0; prol.overflow = 0;
    if (build_params(params_raw, &clean, &prol, hooks) != 0 ||
        clean.overflow || prol.overflow ||
        async_chain_body(clean_buf, body_raw, chain_buf,
                         sizeof(chain_buf), hooks) != 0) {
        free(params_raw); free(body_raw);
        return 0;
    }
    bputs(b, "function (");
    bputn(b, clean_buf, clean.len);
    bputs(b, ") { ");
    bputn(b, prol_buf, prol.len);
    bputs(b, chain_buf);
    bputs(b, " }");
    free(params_raw); free(body_raw);
    return 1;
}


/* 2026-09-26 (50): private members.  Real WeakMap-per-field privacy is
 * out (Duktape 2.7 has no WeakMap), so a class's "#x" members become
 * "__x" - private FIELDS become ordinary own properties, private
 * METHODS become prototype methods.  Privacy is not ENFORCED (outside
 * access reads instead of throwing), but code written against private
 * members runs; that is the goal ("handle the JS", Piskel-class apps).
 * hash_rewrite() rewrites access positions "obj.#x" -> "obj.__x"; the
 * member parser rewrites the DECLARATION names.  Literals, comments and
 * template pieces are skipped so "#" inside strings is untouched. */
static char *hash_rewrite(const char *in) {
    size_t n = strlen(in), cap = n * 2 + 64;
    char *out = (char *)malloc(cap);
    const char *p = in;
    char *w;
    if (!out) return NULL;
    w = out;
    while (*p) {
        if (*p == '\'' || *p == '"') {
            const char *e = skip_literal(p, *p, *p, 0);
            while (p < e) *w++ = *p++;
            continue;
        }
        if (*p == '`') {
            const char *e = skip_template_raw(p);
            while (p < e) *w++ = *p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') *w++ = *p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            *w++ = *p++; *w++ = *p++;
            while (*p && !(p[0] == '*' && p[1] == '/')) *w++ = *p++;
            if (p[0] == '*') { *w++ = *p++; *w++ = *p++; }
            continue;
        }
        if (p[0] == '.' && p[1] == '#') {
            *w++ = '.'; *w++ = '_'; *w++ = '_';
            p += 2;                      /* past ".#" */
            continue;
        }
        *w++ = *p++;
    }
    *w = 0;
    return out;
}

static char *super_rewrite(const char *in, const char *parent) {
    size_t n = strlen(in), cap = n * 4 + strlen(parent) * 8 + 1024;
    char *out = (char *)malloc(cap);
    const char *p = in;
    char *w;
    if (!out) return NULL;
    w = out;
    while (*p) {
        if (*p == '\'' || *p == '"') {
            const char *e = skip_literal(p, *p, *p, 0);
            while (p < e) *w++ = *p++;
            continue;
        }
        if (*p == '`') {
            const char *e = skip_template_raw(p);
            while (p < e) *w++ = *p++;
            continue;
        }
        if (src_word(p, "super")) {
            /* boundary */
            if (w == out || (!is_ident_char((unsigned char)w[-1]) &&
                             w[-1] != '.')) {
                const char *aq = p + 5;
                if (*aq == '(') {
                    /* (50) was p += 5: the "(" we just emitted was
                     * copied again -> "(this,(".  Also skip the ","
                     * for an EMPTY arg list - "super()" emitted
                     * ".call(this,)" (trailing comma = syntax error). */
                    const char *an = aq + 1;
                    while (*an == ' ' || *an == '\t' || *an == '\n' || *an == '\r') an++;
                    if (*an == ')') w += sprintf(w, "%s.call(this", parent);
                    else w += sprintf(w, "%s.call(this,", parent);
                    p = aq + 1;
                    continue;
                }
                if (*aq == '.') {
                    const char *me = skip_ident(aq + 1);
                    if (me > aq + 1 && *me == '(') {
                        w += sprintf(w, "%s.prototype.", parent);
                        p = aq + 1;
                        /* copy "m" then emit ".call(this," before the "(" */
                        while (p < me) *w++ = *p++;
                        {   /* (50) past "(" - was p = me, same
                             * double-paren bug; empty args also must not
                             * emit the trailing "," */
                            const char *an = me + 1;
                            while (*an == ' ' || *an == '\t' || *an == '\n' || *an == '\r') an++;
                            if (*an == ')') w += sprintf(w, ".call(this");
                            else w += sprintf(w, ".call(this,");
                        }
                        p = me + 1;
                        continue;
                    }
                    w += sprintf(w, "%s.prototype.", parent);
                    p = aq + 1;
                    continue;
                }
            }
        }
        *w++ = *p++;
    }
    *w = 0;
    return out;
}


/* (50) find a depth-0 "super(" in a ctor body (literals skipped).
 * Returns the position of the "super" keyword, or NULL. */
static const char *find_super_call(const char *p) {
    int nest = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = skip_literal(p, *p, *p, 0); continue; }
        if (*p == '`') { p = skip_template_raw(p); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (p[0] == '*') p += 2;
            continue;
        }
        if (*p == '(' || *p == '[' || *p == '{') nest++;
        else if (*p == ')' || *p == ']' || *p == '}') {
            if (nest == 0) return NULL;
            nest--;
        }
        else if (nest == 0 && src_word(p, "super")) {
            const char *aq = p + 5;
            while (*aq == ' ' || *aq == '\t') aq++;
            if (*aq == '(') return p;
            p += 5; continue;
        }
        p++;
    }
    return NULL;
}

/* (50) full class-body rewrite: optional #private pass, optional super
 * pass, then the recursive ES6 transform (nested arrows/templates still
 * convert).  On transform failure the REWRITTEN text is kept verbatim -
 * at least #private/super are handled.  With no rewrite needed this is
 * the old fast path (xform_piece), which keeps golden output stable. */
static void class_body_emit(const char *body, const char *parent,
                            int do_hash, Buf *b,
                            const VwEsHooks *hooks) {
    char *cur = NULL;
    if (do_hash) { cur = hash_rewrite(body); if (cur) { body = cur; } }
    if (parent && parent[0]) {
        char *sr = super_rewrite(body, parent);
        /* Preserve the hash-rewritten allocation when super allocation
         * fails. Otherwise body would point to memory just freed below. */
        if (sr) {
            free(cur);
            cur = sr;
            body = sr;
        }
    }
    if (cur) {
        size_t n = strlen(cur), cap = n * 6 + 4096;
        char *dst = (char *)malloc(cap);
        if (dst && vw_es2015_transform(cur, dst, cap, hooks) == 1)
            bputs(b, dst);
        else
            bputs(b, cur);   /* keep the #private/super rewrite at least */
        free(dst);
        free(cur);
    } else {
        xform_piece(body, b, hooks);
    }
}

/* --- class rewrite (parse-then-emit; no partial output on bail) ---------- */

typedef struct {
    int kind;          /* 1=ctor 2=method 3=field 4=accessor */
    int is_static, is_async, acc_get, is_priv;
    char name[128];
    char params[2048];
    char body[65536];
    char value[8192];
} ClsMember;

/* (50) emit the instance-field assignments "this.x = v; ..." */
static void emit_fields(Buf *b, ClsMember *mem, int nmem,
                        int saw_hash, const VwEsHooks *hooks) {
    int m;
    for (m = 0; m < nmem; m++)
        if (mem[m].kind == 3 && !mem[m].is_static) {
            bputs(b, " this.");
            bputs(b, mem[m].name);
            bputs(b, " = ");
            if (mem[m].value[0])
                class_body_emit(mem[m].value, NULL, saw_hash, b, hooks);
            else bputs(b, "undefined");
            bputs(b, ";");
        }
}

static int try_class(Buf *b, const char **pp, const VwEsHooks *hooks) {
    const char *p = *pp;
    size_t i = b->len;
    char prev = 0;
    const char *q;
    char name[128] = "";
    char parent[512] = "";
    int is_decl;
    const char *body, *bodyEnd;
    char inner[128] = "";
    static int cseq = 0;
    ClsMember mem[64];
    int nmem = 0;
    int unsupported = 0;
    int saw_hash = 0;      /* (50) class uses "#private" members */
    int m;

    if (!src_word(p, "class")) return 0;
    while (i > 0 && isspace((unsigned char)b->p[i - 1])) i--;
    if (i > 0) prev = b->p[i - 1];
    if (is_ident_char((unsigned char)prev) || prev == '.') return 0;
    is_decl = !output_expr_position(b);

    q = skipws(p + 5);
    if (is_ident_start((unsigned char)*q)) {
        const char *ne = skip_ident(q);
        if ((size_t)(ne - q) >= sizeof(name)) return 0;
        memcpy(name, q, (size_t)(ne - q));
        name[ne - q] = 0;
        q = skipws(ne);
    }
    if (src_word(q, "extends")) {
        const char *e = skipws(q + 7);
        const char *brace = e;
        int nest = 0;
        while (*brace) {
            char c = *brace;
            if (c == '\'' || c == '"') { brace = skip_literal(brace, c, c, 0); continue; }
            if (c == '`') { brace = skip_template_raw(brace); continue; }
            if (c == '(' || c == '[') nest++;
            else if (c == ')' || c == ']') nest--;
            else if (c == '{' && nest == 0) break;
            brace++;
        }
        if (*brace != '{') return 0;
        if ((size_t)(brace - e) >= sizeof(parent)) return 0;
        memcpy(parent, e, (size_t)(brace - e));
        parent[brace - e] = 0;
        {   /* (50) trim trailing ws: "class X extends A { ..." copied
             * "A " and emitted "A .prototype" - works, but ugly */
            size_t pl = strlen(parent);
            while (pl > 0 && isspace((unsigned char)parent[pl - 1])) pl--;
            parent[pl] = 0;
        }
        q = brace;
    }
    if (*q != '{') return 0;
    body = q;
    bodyEnd = scan_balanced(q);
    if (bodyEnd[-1] != ']' && bodyEnd[-1] != '}' && bodyEnd[-1] != ')') return 0;
    if (is_decl && !name[0]) return 0;
    if (name[0]) snprintf(inner, sizeof(inner), "%s", name);
    else snprintf(inner, sizeof(inner), "__vw_C%d", cseq++);

    /* --- parse members (no output!) ------------------------------------- */
    {
        const char *mm = body + 1;
        const char *mend = bodyEnd - 1;
        while (mm < mend && !unsupported) {
            int is_static = 0, is_async = 0, is_priv = 0;
            const char *k, *ke;
            char mname[128];
            mm = skipws(mm);
            if (mm >= mend) break;
            if (mm[0] == '/' && mm[1] == '/') {
                while (mm < mend && *mm != '\n') mm++;
                continue;
            }
            if (mm[0] == '/' && mm[1] == '*') {
                mm += 2;
                while (mm + 1 < mend && !(mm[0] == '*' && mm[1] == '/')) mm++;
                if (mm + 1 < mend) mm += 2;
                continue;
            }
            if (*mm == ';') { mm++; continue; }
            if (*mm == '*') { unsupported = 1; break; }
            if (src_word(mm, "static")) { is_static = 1; mm = skipws(mm + 6); }
            if (src_word(mm, "async")) { is_async = 1; mm = skipws(mm + 5); }
            if (*mm == '#') { is_priv = 1; mm++; }  /* (50) private member */
            k = mm;
            ke = skip_ident(k);
            if (ke == k) { unsupported = 1; break; }
            if ((size_t)(ke - k) >= sizeof(mname) - 3) { unsupported = 1; break; }
            if (is_priv) { mname[0] = '_'; mname[1] = '_';
                memcpy(mname + 2, k, (size_t)(ke - k));
                mname[ke - k + 2] = 0; }
            else { memcpy(mname, k, (size_t)(ke - k)); mname[ke - k] = 0; }
            saw_hash |= is_priv;
            mm = skipws(ke);
            if (nmem >= 64) { unsupported = 1; break; }
            memset(&mem[nmem], 0, sizeof(ClsMember));

            if ((!strcmp(mname, "get") || !strcmp(mname, "set")) &&
                is_ident_start((unsigned char)*mm)) {
                /* accessor: the REAL name follows.  A bare "get()"
                 * (nothing or "(" follows) is a METHOD named get -
                 * ct4's class had one and the whole class bailed (50) */
                const char *ke2 = skip_ident(mm);
                const char *po, *pe, *bo, *be;
                if (ke2 == mm) { unsupported = 1; break; }
                po = skipws(ke2);
                if (*po != '(') { unsupported = 1; break; }
                pe = scan_balanced(po);
                if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') { unsupported = 1; break; }
                bo = skipws(pe);
                if (*bo != '{') { unsupported = 1; break; }
                be = scan_balanced(bo);
                if (be[-1] != ']' && be[-1] != '}' && be[-1] != ')') { unsupported = 1; break; }
                mem[nmem].kind = 4;
                mem[nmem].is_static = is_static;
                mem[nmem].acc_get = (mname[0] == 'g');
                if (is_priv) { unsupported = 1; break; }  /* "get #x()" */
                mem[nmem].is_priv = 0;
                if ((size_t)(ke2 - mm) >= sizeof(mem[nmem].name)) { unsupported = 1; break; }
                memcpy(mem[nmem].name, mm, (size_t)(ke2 - mm));
                {
                    /* interior is (po+1 .. pe-1): length pe - po - 2.
                     * The old -1 copy also grabbed the closing ")" -
                     * "get x()" produced params ")" and the accessor
                     * emitted "function ())" (2026-09-26 (50) gate find). */
                    size_t pn = (size_t)(pe - po - 2);
                    size_t bn = (size_t)(be - bo - 2);
                    if (pn >= sizeof(mem[nmem].params) ||
                        bn >= sizeof(mem[nmem].body)) { unsupported = 1; break; }
                    memcpy(mem[nmem].params, po + 1, pn);
                    mem[nmem].params[pn] = 0;
                    memcpy(mem[nmem].body, bo + 1, bn);
                    mem[nmem].body[bn] = 0;
                }
                nmem++;
                mm = be;
                continue;
            }
            if (*mm == '(') {
                /* ctor or method */
                const char *pe = scan_balanced(mm);
                const char *bo, *be;
                if (pe[-1] != ']' && pe[-1] != '}' && pe[-1] != ')') { unsupported = 1; break; }
                bo = skipws(pe);
                if (*bo != '{') { unsupported = 1; break; }
                be = scan_balanced(bo);
                if (be[-1] != ']' && be[-1] != '}' && be[-1] != ')') { unsupported = 1; break; }
                {
                    /* interior (mm+1 .. pe-1): pe - mm - 2 (was -1, see
                     * the accessor note above - an empty "()" param list
                     * became ")" and broke build_params) */
                    size_t pn = (size_t)(pe - mm - 2);
                    size_t bn = (size_t)(be - bo - 2);
                    if (pn >= sizeof(mem[nmem].params) ||
                        bn >= sizeof(mem[nmem].body)) { unsupported = 1; break; }
                    memcpy(mem[nmem].params, mm + 1, pn);
                    mem[nmem].params[pn] = 0;
                    memcpy(mem[nmem].body, bo + 1, bn);
                    mem[nmem].body[bn] = 0;
                }
                mem[nmem].kind = (!is_static && !is_async && !is_priv &&
                                  !strcmp(mname, "constructor")) ? 1 : 2;
                mem[nmem].is_static = is_static;
                mem[nmem].is_async = is_async;
                mem[nmem].is_priv = is_priv;
                strcpy(mem[nmem].name, mname);
                if (mem[nmem].kind == 1) {
                    int j;
                    for (j = 0; j < nmem; j++)
                        if (mem[j].kind == 1) { unsupported = 1; break; }
                    if (unsupported) break;
                }
                nmem++;
                mm = be;
                continue;
            }
            if (*mm == '=' || *mm == ';') {
                /* field */
                const char *ve = NULL;
                if (*mm == '=') {
                    const char *vs = skipws(mm + 1);
                    const char *e = vs;
                    int nest2 = 0;
                    while (*e) {
                        char c = *e;
                        if (c == '\'' || c == '"') { e = skip_literal(e, c, c, 0); continue; }
                        if (c == '`') { e = skip_template_raw(e); continue; }
                        if (c == '(' || c == '[' || c == '{') nest2++;
                        else if (c == ')' || c == ']' || c == '}') { if (nest2 == 0) break; nest2--; }
                        else if (c == ';' && nest2 == 0) break;
                        e++;
                    }
                    if (*e != ';') { unsupported = 1; break; }
                    if ((size_t)(e - vs) >= sizeof(mem[nmem].value)) { unsupported = 1; break; }
                    memcpy(mem[nmem].value, vs, (size_t)(e - vs));
                    mem[nmem].value[e - vs] = 0;
                    ve = e;
                } else {
                    ve = mm;
                    mem[nmem].value[0] = 0;
                }
                mem[nmem].kind = 3;
                mem[nmem].is_static = is_static;
                mem[nmem].is_priv = is_priv;
                strcpy(mem[nmem].name, mname);
                nmem++;
                mm = ve + 1;
                continue;
            }
            unsupported = 1;
            break;
        }
    }
    if (unsupported) return 0;      /* verbatim: nothing was emitted */

    /* --- prep cleaned params for every ctor/method ------------------------ */
    for (m = 0; m < nmem; m++) {
        if (mem[m].kind == 1 || mem[m].kind == 2) {
            Buf clean, prol;
            char clean_buf[1024], prol_buf[16384];
            clean.p = clean_buf; clean.cap = sizeof(clean_buf);
            clean.len = 0; clean.overflow = 0;
            prol.p = prol_buf; prol.cap = sizeof(prol_buf);
            prol.len = 0; prol.overflow = 0;
            if (build_params(mem[m].params, &clean, &prol, hooks) != 0 ||
                clean.overflow || prol.overflow)
                return 0;          /* verbatim */
            mem[m].value[0] = 0;   /* reuse value[] to stash clean params */
            if (clean.len >= sizeof(mem[m].value)) return 0;
            memcpy(mem[m].value, clean_buf, clean.len);
            mem[m].value[clean.len] = 0;
            /* stash prologue in params[] (already copied into clean) */
            if (prol.len >= sizeof(mem[m].params)) return 0;
            memcpy(mem[m].params, prol_buf, prol.len);
            mem[m].params[prol.len] = 0;
        }
    }

    /* --- emit ------------------------------------------------------------- */
    if (is_decl) {
        bputs(b, "var ");
        bputs(b, name);
        bputs(b, " = ");
    }
    bputs(b, "(function () { function ");
    bputs(b, inner);
    bputs(b, "(");
    for (m = 0; m < nmem; m++)
        if (mem[m].kind == 1) { bputs(b, mem[m].value); break; }
    bputs(b, ") { ");
    {
        int has_ctor = 0;
        for (m = 0; m < nmem; m++)
            if (mem[m].kind == 1) { has_ctor = 1; break; }
        if (!has_ctor && parent[0])
            bputs(b, parent), bputs(b, ".apply(this, arguments); ");
        if (!has_ctor)
            emit_fields(b, mem, nmem, saw_hash, hooks);   /* (50) order */
    }
    /* (50) FIELD-INITIALIZER ORDER.  Spec: instance fields initialize
     * BEFORE the ctor body (base) / right AFTER super() returns
     * (derived) - not after the whole body.  A ctor that reads `this.x`
     * relied on the field value and the old after-body emit gave it
     * undefined. */
    {
        int has_ctor = 0;
        for (m = 0; m < nmem; m++)
            if (mem[m].kind == 1) { has_ctor = 1; break; }
        if (!has_ctor) {
            /* default ctor: fields already emitted above (50) */
        } else {
        for (m = 0; m < nmem; m++)
            if (mem[m].kind == 1) {
                char *ctb = NULL;
                const char *btext = mem[m].body;
                const char *sup = NULL;
                bputn(b, mem[m].params, strlen(mem[m].params)); /* prologue */
                if (saw_hash) {
                    ctb = hash_rewrite(mem[m].body);
                    if (ctb) btext = ctb;
                }
                if (parent[0]) sup = find_super_call(btext);
                if (sup) {
                    /* seg1 ; super(...) ; FIELDS ; rest-of-body */
                    const char *aq = sup + 5;
                    const char *close;
                    char *seg;
                    while (*aq == ' ' || *aq == '\t') aq++;
                    close = scan_balanced(aq);
                    seg = dupn(btext, (size_t)(sup - btext));
                    if (seg) {
                        class_body_emit(seg, NULL, 0, b, hooks);
                        free(seg);
                    }
                    seg = dupn(sup, (size_t)(close - sup));
                    if (seg) {
                        char *rw = super_rewrite(seg, parent);
                        if (rw) { bputs(b, rw); free(rw); }
                        else bputs(b, seg);
                        free(seg);
                    }
                    bputs(b, "; ");
                    emit_fields(b, mem, nmem, saw_hash, hooks);
                    class_body_emit(close, parent, 0, b, hooks);
                } else if (parent[0]) {
                    /* derived ctor WITHOUT super(): emit as before, keep
                     * fields last (engine will throw on this-misuse the
                     * way real JS would, only later) */
                    char *rw = super_rewrite(btext, parent);
                    if (rw) { bputs(b, rw); free(rw); }
                    else bputs(b, btext);
                    emit_fields(b, mem, nmem, saw_hash, hooks);
                } else {
                    /* base: fields FIRST, then the body */
                    emit_fields(b, mem, nmem, saw_hash, hooks);
                    class_body_emit(btext, NULL, 0, b, hooks);
                }
                free(ctb);
                break;
            }
        }          /* close has_ctor else */
    }
    bputs(b, " } ");
    bputs(b, inner);
    bputs(b, ".prototype = Object.create(");
    if (parent[0]) { bputs(b, parent); bputs(b, ".prototype"); }
    else bputs(b, "Object.prototype");
    bputs(b, "); ");
    bputs(b, inner);
    bputs(b, ".prototype.constructor = ");
    bputs(b, inner);
    bputs(b, "; ");
    for (m = 0; m < nmem; m++) {
        if (mem[m].kind == 2) {
            char target[256];
            if (mem[m].is_static)
                snprintf(target, sizeof(target), "%s.%s", inner, mem[m].name);
            else
                snprintf(target, sizeof(target), "%s.prototype.%s",
                         inner, mem[m].name);
            bputs(b, target);
            bputs(b, " = function (");
            bputs(b, mem[m].value);            /* clean params */
            bputs(b, ") { ");
            bputn(b, mem[m].params, strlen(mem[m].params));
            if (mem[m].is_async) {
                char chain[131072];
                if (async_chain_body(mem[m].value, mem[m].body,
                                     chain, sizeof(chain), hooks) != 0)
                    bputs(b, " /* async unsupported */ ");
                else
                    bputs(b, chain);
            } else {
                /* (50) #private + super + nested-arrow transform */
                class_body_emit(mem[m].body, parent, saw_hash, b, hooks);
            }
            bputs(b, " }; ");
        } else if (mem[m].kind == 3 && mem[m].is_static) {
            bputs(b, inner);
            bputs(b, ".");
            bputs(b, mem[m].name);
            bputs(b, " = ");
            if (mem[m].value[0])
                class_body_emit(mem[m].value, NULL, saw_hash, b, hooks);
            else bputs(b, "undefined");
            bputs(b, "; ");
        } else if (mem[m].kind == 4) {
            bputs(b, "Object.defineProperty(");
            bputs(b, mem[m].is_static ? inner : "");
            if (!mem[m].is_static) {
                bputs(b, inner);
                bputs(b, ".prototype");
            }
            bputs(b, ", \"");
            bputs(b, mem[m].name);
            bputs(b, "\", { ");
            bputs(b, mem[m].acc_get ? "get" : "set");
            bputs(b, ": function (");
            bputs(b, mem[m].params);
            bputs(b, ") { ");
            class_body_emit(mem[m].body, parent, saw_hash, b, hooks);
            bputs(b, " }, configurable: true }); ");
        }
    }
    if (parent[0]) {
        /* (50) static inheritance: Child.staticM must see Parent's
         * statics (spec: constructor functions chain).  __proto__ on a
         * function is writable in Duktape. */
        bputs(b, inner);
        bputs(b, ".__proto__ = ");
        bputs(b, parent);
        bputs(b, "; ");
    }
    bputs(b, "return ");
    bputs(b, inner);
    bputs(b, "; })()");
    /* A declaration originally needs no terminator, even when the next
     * statement is on the same line. Its replacement is a var assignment. */
    if (is_decl) bputs(b, ";");
    *pp = bodyEnd;
    return 1;
}

/* --- phase-A driver ------------------------------------------------------- */

static int block_pass(const char *src, Buf *b, const VwEsHooks *hooks) {
    const char *p = src;
    int changed = 0;
    while (*p && !b->overflow) {
        if (p[0] == '/' && p[1] == '/') {                      /* comment */
            while (*p && *p != '\n') bput(b, *p++);
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            bputn(b, p, 2); p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) bput(b, *p++);
            if (p[0] == '*') { bputn(b, p, 2); p += 2; }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            const char *e = skip_literal(p, *p, *p, 0);
            bputn(b, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (*p == '`') {
            const char *e = skip_template_raw(p);
            bputn(b, p, (size_t)(e - p));
            p = e;
            continue;
        }
        if (*p == '/') {
            if (regex_position(b)) {
                const char *e = skip_regex(p);
                bputn(b, p, (size_t)(e - p));
                p = e;
            } else bput(b, *p++);
            continue;
        }
        if (p[0] == 'c' && src_word(p, "class")) {
            if (try_class(b, &p, hooks)) { changed = 1; continue; }
        }
        if (p[0] == 'a' && src_word(p, "async")) {
            if (try_async(b, &p, hooks)) { changed = 1; continue; }
        }
        bput(b, *p++);
    }
    return changed;
}

int vw_es2015_transform(const char *src, char *dst, size_t cap,
                     const VwEsHooks *hooks) {
    Buf b = { dst, cap, 0, 0 };
    char *phase1 = NULL;
    const char *input = src;
    int changed = 0;

    if (!src || !dst || cap < 4) return -1;
    dst[0] = 0;
    if (!src[0]) return 0;

    /* Fast paths (ESM round, 2026-10-03).  Each phase below scans the
       whole script and works on a heap copy of it - phase 2 alone
       mallocs EIGHT times the source length.  Every console paid that
       for every script, even plain ES5 pages (the Wii most: 8x a
       200 KB script is 1.6 MB of the ~24 MB it has).  A phase is now
       skipped when the keywords it rewrites are not even present.
       The checks are plain substring searches: a keyword that only
       appears inside a string or a comment makes us run the pass
       exactly as before (harmless), while the reverse cannot happen -
       every construct these passes rewrite contains one of the
       markers below.  The lexical pass has no such marker (it also
       rewrites destructuring and default parameters, whose shapes
       are not keyword-detectable), so it always runs. */

    /* Phase 1: import/export - only when the host can load/evaluate
       and one of the two keywords appears in the source. */
    if (hooks && hooks->resolve_url && hooks->load_file && hooks->evaluate
        && (strstr(src, "import") || strstr(src, "export"))) {
        Buf p1;
        phase1 = (char *)malloc(strlen(src) * 2 + 4096);
        if (!phase1) return -1;
        p1.p = phase1; p1.cap = strlen(src) * 2 + 4096; p1.len = 0; p1.overflow = 0;
        if (import_export_pass(src, &p1, hooks)) {
            phase1[p1.len] = 0;
            input = phase1;
            changed = 1;
        }
    }

    /* Phase 2: block-level rewrites (class, async) - ES7+ (47).  Runs
       before the lexical pass so classes become plain functions and
       async functions become Promise chains the lexer never sees.  The
       transform counters are kept in sync even when the pass is
       skipped, so nested/dynamic calls see the same state as before. */
    {
        char *phase2 = NULL;
        g_paren_top = 0;
        if (!g_depth) { g_loading_count = 0; g_temp_seq = 0; }
        g_depth++;
        if (strstr(input, "class") || strstr(input, "async")
            || strstr(input, "await")) {
            phase2 = (char *)malloc(strlen(input) * 8 + 4096);
            if (phase2) {
                Buf p2;
                p2.p = phase2; p2.cap = strlen(input) * 8 + 4096;
                p2.len = 0; p2.overflow = 0;
                if (block_pass(input, &p2, hooks) && !p2.overflow) {
                    phase2[p2.len] = 0;
                    input = phase2;
                    changed = 1;
                }
                if (p2.overflow) { input = src; }
            }
        }
        /* Phase 3: lexical pass (let/const, arrows, templates). */
        lex_pass(input, &b, hooks, 0);
        g_depth--;
        free(phase2);
    }

    free(phase1);
    if (b.overflow) { dst[0] = 0; return -1; }
    b.p[b.len] = 0;
    if (b.len == strlen(src) && !strcmp(dst, src)) return changed ? 1 : 0;
    return 1;
}
