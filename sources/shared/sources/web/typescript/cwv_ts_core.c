/* cwv_ts_core.c - TypeScript -> JavaScript type stripper (2026-10-01).
 *
 * Architecture (public contract in cwv_ts_core.h): ONE scanner, ONE
 * output, a small context stack. The default action is to copy the
 * source verbatim; a context is pushed only where the TS grammar
 * allows type syntax, so object literals, ternaries, labels and
 * comparisons are preserved by construction:
 *
 *   CX_PARAMS - a parameter list (function/method/constructor decl,
 *               arrow params, catch clauses). Strips annotations, `?`/
 *               `!` markers, visibility modifiers, `this` params;
 *               collects constructor param properties.
 *   CX_DECL   - a let/const/var declarator list: one annotation per
 *               declared name; initializers flow through the normal
 *               expression machinery.
 *   CX_CLASS  - a class body at a known brace depth: members start at
 *               statement boundaries; modifiers are dropped, overload
 *               signatures and index signatures are removed via
 *               non-emitting lookahead, field annotations are skipped.
 *   CX_BLOCK  - any brace block opened inside an expression (method
 *               and arrow bodies, object literals in initializers):
 *               contents are processed with full normal machinery.
 *
 * Return annotations ride a small pending stack: a function-ish param
 * list pushes one, and the very next significant char consumes it
 * (`:` = the annotation) or clears it (`{`, `;`, `=>` or anything
 * unexpected = no annotation).
 *
 * Strings, template literals, regex literals and comments are copied
 * verbatim and never scanned for syntax. Constructs the subset cannot
 * express (namespaces, decorators, ambient modules, .tsx) return -2
 * so the host can surface an honest hint instead of broken JavaScript.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "web/typescript/cwv_ts_core.h"

typedef enum { CX_NORMAL, CX_PARAMS, CX_DECL, CX_CLASS, CX_BLOCK } CxKind;

typedef struct {
    CxKind kind;
    int arrow;      /* CX_PARAMS: arrow params (return annotation ends at =>) */
    int fnret;      /* CX_PARAMS: a return annotation may follow */
    int ctor;       /* CX_PARAMS: constructor (param properties) */
    int pstate;     /* CX_PARAMS: 0 name expected, 1 after name, 2 default */
    int pdepth;     /* CX_PARAMS: bracket depth inside the list */
    int thisdrop;   /* CX_PARAMS: a `this` param was dropped (eat 1 comma) */
    int dstate;     /* CX_DECL:   0 name expected, 1 after name/annot, 2 init */
    int ddepth;     /* CX_DECL:   bracket depth inside the list */
    int cdepth;     /* CX_CLASS:  brace depth of the class body */
    int mstart;     /* CX_CLASS:  at a member start position */
    int bdepth;     /* CX_BLOCK:  brace depth inside the block */
    char props[4][64]; int propcount;
} Cx;

typedef struct {
    char *buf; size_t cap, len; int overflow;
    int saw_ts;
    int last_sig;
    int last_nws;         /* last non-whitespace significant char */
    int kw_regex;         /* the last word allows a regex literal next */
    Cx cx[24]; int cx_top;
    int ret[8]; int ret_top;
    int brace_depth;
    int pending_class;    /* the next `{` opens a class body */
    int pending_fn;       /* the next `(` is a parameter list */
    int pending_fnbody;   /* the next `{` is a function body block */
    char inject[512];     /* ctor param properties, emitted after `{` */
} Ts;

static char s_reason[96];

/* ---- emitter ---------------------------------------------------------- */

static void outc(Ts *t, char c)
{
    if (t->len + 1 < t->cap) {
        t->buf[t->len++] = c;
        t->buf[t->len] = 0;
    } else
        t->overflow = 1;
    t->last_sig = (unsigned char)c;
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        t->last_nws = (unsigned char)c;
    t->kw_regex = 0;
}

static void outs(Ts *t, const char *s)
{ while (*s) outc(t, *s++); }

static void out_range(Ts *t, const char *s, const char *e)
{ while (s < e) outc(t, *s++); }

/* ---- lexemes ---------------------------------------------------------- */

static int is_idstart(int c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
static int is_idchar(int c)
{ return is_idstart(c) || (c >= '0' && c <= '9'); }

static int kw_at(const char *p, const char *w)
{
    size_t n = strlen(w);
    return strncmp(p, w, n) == 0 && !is_idchar((unsigned char)p[n]);
}

static const char *skip_ws(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        return p;
    }
}

static const char *scan_quote(const char *q)
{
    char open = q[0];
    const char *p = q + 1;
    while (*p && *p != open) {
        if (*p == '\\') p++;
        if (!*p) break;
        p++;
    }
    return *p ? p + 1 : p;
}

static const char *scan_template(const char *q)
{
    const char *p = q + 1;
    while (*p && *p != '`') {
        if (*p == '\\') { p += 2; continue; }
        if (p[0] == '$' && p[1] == '{') {
            int depth = 1;
            p += 2;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                else if (*p == '\'' || *p == '"') p = scan_quote(p) - 1;
                else if (*p == '`') p = scan_template(p) - 1;
                else if (p[0] == '/' && p[1] == '*') p = skip_ws(p) - 1;
                if (*p) p++;
            }
            continue;
        }
        p++;
    }
    return *p ? p + 1 : p;
}

static const char *match_close(const char *p)
{
    char open = p[0], close = (open == '(') ? ')' : (open == '{') ? '}' : ']';
    int depth = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = scan_quote(p); continue; }
        if (*p == '`') { p = scan_template(p); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') { p = skip_ws(p); continue; }
        if (*p == open) depth++;
        else if (*p == close) {
            depth--;
            if (depth == 0) return p;
        }
        p++;
    }
    return p;
}

static const char *skip_type(Ts *t, const char *p, int end_arrow, int end_brace);

static const char *match_angle(const char *lt)
{
    const char *p = lt + 1;
    int depth = 1;
    while (*p) {
        if (*p == '\'' || *p == '"') { p = scan_quote(p); continue; }
        if (*p == '`') { p = scan_template(p); continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') { p = skip_ws(p); continue; }
        if (*p == '<') depth++;
        else if (*p == '>') {
            depth--;
            if (depth == 0) return p;
        } else if (*p == '(' || *p == '{' || *p == '[') {
            p = match_close(p);
            continue;
        } else if (*p == ')' || *p == '}' || *p == ']' ||
                   *p == ';' || *p == '=') {
            return NULL;
        }
        p++;
    }
    return NULL;
}

static int is_arrow_params(Ts *t, const char *open)
{
    const char *close = match_close(open);
    const char *q;
    int saved;
    if (*close != ')') return 0;
    q = skip_ws(close + 1);
    if (*q == ':') {                    /* possible return annotation */
        saved = t->saw_ts;
        q = skip_type(t, q + 1, 1, 0);
        t->saw_ts = saved;              /* lookahead: no mark yet */
        q = skip_ws(q);
    }
    return q[0] == '=' && q[1] == '>';
}

static int looks_like_type_args(const char *lt, const char *gt)
{
    const char *p = lt + 1;
    while (p < gt) {
        if (*p == '\'' || *p == '"') {
            const char *q = scan_quote(p);
            if (q > gt) return 0;
            p = q;
            continue;
        }
        if (is_idstart((unsigned char)*p)) {
            while (p < gt && is_idchar((unsigned char)*p)) p++;
            continue;
        }
        if (*p == '.' || *p == ',' || *p == ' ' || *p == '\t' ||
            *p == '\n' || *p == '\r' || *p == '[' || *p == ']' ||
            *p == '<' || *p == '>' || *p == '|')
            { p++; continue; }
        return 0;
    }
    return 1;
}

/* Skip a type expression (emits nothing). */
static const char *skip_type(Ts *t, const char *p, int end_arrow, int end_brace)
{
    int groups = 0;
    t->saw_ts = 1;
    p = skip_ws(p);
    while (*p) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { p++; continue; }
        if (c == '\'' || c == '"') { p = scan_quote(p); groups = 1; continue; }
        if (c == '`') { p = scan_template(p); groups = 1; continue; }
        if (c == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (c == '/' && p[1] == '*') { p = skip_ws(p); continue; }
        if (c == '=' && p[1] == '>') {
            if (end_arrow) break;
            p += 2; groups = 1; continue;
        }
        if (c == '<') {
            const char *q = match_angle(p);
            if (!q) break;
            p = q + 1; groups = 1; continue;
        }
        if (c == '(' || c == '{' || c == '[') {
            const char *q = match_close(p);
            if (!*q) break;
            if (end_brace && c == '{' && groups) break;
            p = q + 1; groups = 1; continue;
        }
        if (c == ';' || c == ',' || c == ')' || c == ']' || c == '}' ||
            c == '?' || c == ':' || c == '=')
            break;
        if (c == '!' || c == '+' || c == '-' || c == '*' || c == '/' ||
            c == '%' || c == '^' || c == '~' || c == '>')
            break;
        if (is_idstart((unsigned char)c)) {
            while (is_idchar((unsigned char)*p)) p++;
            groups = 1;
            continue;
        }
        if (c == '.' || c == '|' || c == '&') { p++; continue; }
        break;
    }
    return p;
}


/* Emit the trailing whitespace of an annotation region [colon, end):
 * skip_type already consumed it, but the token that follows (= , ) {)
 * needs its spacing back so the output stays readable. */
static void emit_trailing_ws(Ts *t, const char *colon, const char *end)
{
    const char *ws = end;
    while (ws > colon + 1 &&
           (ws[-1] == ' ' || ws[-1] == '\t' || ws[-1] == '\n' || ws[-1] == '\r'))
        ws--;
    out_range(t, ws, end);
}

/* ---- verbatim regions --------------------------------------------------- */

static const char *copy_verbatim(Ts *t, const char *p, int regex_ok)
{
    if (*p == '\'' || *p == '"') {
        const char *q = scan_quote(p);
        out_range(t, p, q);
        return q;
    }
    if (*p == '`') {
        const char *q = scan_template(p);
        out_range(t, p, q);
        return q;
    }
    if (p[0] == '/' && p[1] == '/') {
        const char *q = p;
        while (*q && *q != '\n') q++;
        out_range(t, p, q);
        return q;
    }
    if (p[0] == '/' && p[1] == '*') {
        const char *q = skip_ws(p);
        out_range(t, p, q);
        return q;
    }
    if (regex_ok && *p == '/') {
        const char *q = scan_quote(p);
        out_range(t, p, q);
        return q;
    }
    return NULL;
}

static int regex_position(const Ts *t)
{
    int c = t->last_sig;
    return t->kw_regex || c == 0 || c == '(' || c == ',' || c == '=' ||
           c == ':' || c == '[' || c == '!' || c == '&' || c == '|' ||
           c == '?' || c == '{' || c == ';' || c == '}' || c == '\n' ||
           c == '>' || c == '<' || c == '+' || c == '-' || c == '*' ||
           c == '%' || c == '^' || c == '~';
}

/* ---- contexts ------------------------------------------------------------ */

static Cx *cx_top(Ts *t)
{ return t->cx_top > 0 ? &t->cx[t->cx_top - 1] : NULL; }

static void push_cx(Ts *t, CxKind kind)
{
    Cx *cx;
    if (t->cx_top >= (int)(sizeof(t->cx)/sizeof(t->cx[0]))) return;
    cx = &t->cx[t->cx_top++];
    memset(cx, 0, sizeof(*cx));
    cx->kind = kind;
}

static void push_params(Ts *t, int arrow, int fnret, int ctor)
{
    push_cx(t, CX_PARAMS);
    {
        Cx *cx = cx_top(t);
        cx->arrow = arrow;
        cx->fnret = fnret;
        cx->ctor = ctor;
    }
}

static void ret_push(Ts *t, int mode)
{ if (t->ret_top < (int)(sizeof(t->ret)/sizeof(t->ret[0]))) t->ret[t->ret_top++] = mode; }

static void ret_pop(Ts *t)
{ if (t->ret_top > 0) t->ret_top--; }

/* ---- statement-level drops (non-emitting) -------------------------------- */

static const char *drop_interface(Ts *t, const char *p)
{
    const char *q = skip_ws(p + 9);
    while (is_idchar((unsigned char)*q) || *q == '.') q++;
    q = skip_ws(q);
    if (*q == '<') {
        const char *g = match_angle(q);
        if (g) q = skip_ws(g + 1);
    }
    while (kw_at(q, "extends")) {
        q = skip_ws(q + 7);
        for (;;) {
            while (is_idchar((unsigned char)*q) || *q == '.') q++;
            q = skip_ws(q);
            if (*q == '<') {
                const char *g = match_angle(q);
                if (g) { q = skip_ws(g + 1); continue; }
            }
            if (*q == ',') { q = skip_ws(q + 1); continue; }
            break;
        }
    }
    if (*q == '{') q = match_close(q);
    if (*q == '}') q++;
    t->saw_ts = 1;
    return q;
}

static const char *drop_type_alias(Ts *t, const char *p)
{
    const char *q = skip_ws(p + 4);
    int d = 0;
    while (*q) {
        if (*q == '\'' || *q == '"') { q = scan_quote(q); continue; }
        if (*q == '`') { q = scan_template(q); continue; }
        if (*q == '(' || *q == '{' || *q == '[') d++;
        else if (*q == ')' || *q == '}' || *q == ']') d--;
        else if (*q == ';' && d == 0) { q++; break; }
        q++;
    }
    t->saw_ts = 1;
    return q;
}

static const char *drop_declare(Ts *t, const char *p)
{
    const char *q = skip_ws(p + 7);
    const char *e = q;
    while (is_idchar((unsigned char)*e)) e++;
    if ((size_t)(e - q) == 6 && !strncmp(q, "module", 6)) {
        snprintf(s_reason, sizeof(s_reason), "ambient module blocks");
        return NULL;
    }
    if ((size_t)(e - q) == 9 && !strncmp(q, "namespace", 9)) {
        snprintf(s_reason, sizeof(s_reason), "ambient namespace blocks");
        return NULL;
    }
    while (*q) {
        if (*q == '\'' || *q == '"') { q = scan_quote(q); continue; }
        if (*q == '`') { q = scan_template(q); continue; }
        if (*q == ';') { q++; break; }
        if (*q == '{') {
            q = match_close(q);
            if (*q == '}') { q++; if (*q == ';') q++; break; }
            continue;
        }
        q++;
    }
    t->saw_ts = 1;
    return q;
}

/* drop an `import type ...;` / `export type ...;` statement */
static const char *drop_import_export_type(Ts *t, const char *p)
{
    const char *q = p;
    while (*q) {
        if (*q == '\'' || *q == '"') { q = scan_quote(q); continue; }
        if (*q == '`') { q = scan_template(q); continue; }
        if (*q == ';') { q++; break; }
        if (*q == '{') {
            q = match_close(q);
            if (*q == '}') q++;
            continue;
        }
        q++;
    }
    t->saw_ts = 1;
    return q;
}

/* ---- enum codegen --------------------------------------------------------- */

static const char *emit_enum(Ts *t, const char *body_open, const char *name,
                             const char *export_kw)
{
    const char *p = body_open + 1;
    long auto_val = 0;
    char member[160], init[512];
    outs(t, export_kw);
    outs(t, "var ");
    outs(t, name);
    outs(t, ";(function(");
    outs(t, name);
    outs(t, "){");
    for (;;) {
        char c;
        p = skip_ws(p);
        c = *p;
        if (!c || c == '}') break;
        if (c == '\'' || c == '"') {
            const char *q = scan_quote(p);
            size_t n = (size_t)(q - p);
            if (n >= sizeof(member)) n = sizeof(member) - 1;
            memcpy(member, p, n); member[n] = 0;
            p = q;
        } else {
            const char *w = p;
            size_t n;
            while (is_idchar((unsigned char)*p)) p++;
            if (p == w) { p++; continue; }
            n = (size_t)(p - w);
            if (n >= sizeof(member)) n = sizeof(member) - 1;
            memcpy(member, w, n); member[n] = 0;
        }
        init[0] = 0;
        p = skip_ws(p);
        if (*p == '=') {
            const char *s = skip_ws(++p);
            p = s;
            int d = 0;
            while (*p) {
                if (*p == '\'' || *p == '"') { p = scan_quote(p); continue; }
                if (*p == '(' || *p == '{' || *p == '[') d++;
                else if (*p == ')' || *p == '}' || *p == ']') {
                    if (d == 0) break;
                    d--;
                } else if (*p == ',' && d == 0) break;
                else if (p[0] == '/' && p[1] == '/') break;
                p++;
            }
            while (p > s && (p[-1] == ' ' || p[-1] == '\t' ||
                             p[-1] == '\n' || p[-1] == '\r'))
                p--;                             /* trim trailing ws */
            {
                size_t n = (size_t)(p - s);
                if (n >= sizeof(init)) n = sizeof(init) - 1;
                memcpy(init, s, n); init[n] = 0;
            }
        }
        if (!init[0]) {
            char line[480];
            snprintf(line, sizeof(line), "%s[%s[\"%s\"] = %ld] = \"%s\";",
                     name, name, member, auto_val, member);
            outs(t, line);
            auto_val++;
        } else {
            char line[900];
            snprintf(line, sizeof(line), "%s[\"%s\"] = %s;", name, member, init);
            outs(t, line);
            if ((init[0] >= '0' && init[0] <= '9') ||
                (init[0] == '-' && init[1] >= '0' && init[1] <= '9')) {
                char rev[480];
                snprintf(rev, sizeof(rev), "%s[%s[\"%s\"]] = \"%s\";",
                         name, name, member, member);
                outs(t, rev);
                auto_val = strtol(init, NULL, 10) + 1;
            }
        }
        p = skip_ws(p);
        if (*p == ',') p++;
    }
    p = match_close(body_open);
    if (*p == '}') p++;
    {
        char tail[160];
        snprintf(tail, sizeof(tail), "})(%s || (%s = {}));", name, name);
        outs(t, tail);
    }
    t->saw_ts = 1;
    return p;
}

/* ---- class-member lookahead ----------------------------------------------- */

/* Is the member at p a bare overload signature, index signature or
 * `declare` member that must be dropped whole? Returns the position
 * after it, or NULL if it is a real member. */
static const char *member_drop_pos(Ts *t, const char *p)
{
    const char *q = p;
    for (;;) {
        size_t n;
        q = skip_ws(q);
        if (!is_idstart((unsigned char)*q)) break;
        n = 0;
        while (is_idchar((unsigned char)q[n])) n++;
        if ((n == 6 && !strncmp(q, "public", 6)) ||
            (n == 7 && !strncmp(q, "private", 7)) ||
            (n == 9 && !strncmp(q, "protected", 9)) ||
            (n == 8 && !strncmp(q, "readonly", 8)) ||
            (n == 8 && !strncmp(q, "abstract", 8)) ||
            (n == 8 && !strncmp(q, "override", 8)) ||
            (n == 7 && !strncmp(q, "declare", 7)))
            { q += n; continue; }
        if ((n == 3 && !strncmp(q, "get", 3)) || (n == 3 && !strncmp(q, "set", 3))) {
            const char *r = skip_ws(q + n);
            if (is_idstart((unsigned char)*r)) { q += n; continue; }
        }
        break;
    }
    if (*q == '[') {                              /* index signature? */
        const char *close = match_close(q);
        const char *scan = q + 1;
        while (scan < close && *scan != ':' && *scan) scan++;
        if (scan < close && *close == ']') {
            q = skip_ws(close + 1);
            if (*q == ':') q = skip_type(t, q + 1, 0, 0);
            q = skip_ws(q);
            if (*q == ';') return q + 1;
            return q;
        }
        return NULL;                              /* computed name: keep */
    }
    if (*q == '\'' || *q == '"') q = scan_quote(q);
    else if (is_idstart((unsigned char)*q)) while (is_idchar((unsigned char)*q)) q++;
    else if (*q == '*') q++;
    else return NULL;
    q = skip_ws(q);
    if (*q == '?' || *q == '!') {
        const char *r = skip_ws(q + 1);
        if (*r == '(' || *r == ':' || *r == '=') q = r;
    }
    if (*q == '<') {
        const char *g = match_angle(q);
        if (g) q = skip_ws(g + 1);
    }
    if (*q != '(') return NULL;
    {
        const char *close = match_close(q);
        const char *r;
        if (*close != ')') return NULL;
        r = skip_ws(close + 1);
        if (*r == ':') {
            r = skip_type(t, r + 1, 0, 1);
            r = skip_ws(r);
        }
        if (*r == ';') return r + 1;              /* overload signature */
    }
    return NULL;
}

/* ---- one scanner step ------------------------------------------------------ */

static int stmt_boundary(const Ts *t)
{
    int c = t->last_nws;
    return c == 0 || c == ';' || c == '{' || c == '}';
}

static const char *step(Ts *t, const char *p)
{
    char c = *p;
    Cx *cx = cx_top(t);
    const char *v;

    /* verbatim regions */
    v = copy_verbatim(t, p, regex_position(t));
    if (v) return v;

    /* pending return annotations */
    if (t->ret_top > 0 && (!cx || cx->kind != CX_PARAMS)) {
        const char *n = skip_ws(p);
        if (*n == ':') {
            int mode = t->ret[t->ret_top - 1];
            const char *e = skip_type(t, n + 1, mode == 2, mode == 1);
            emit_trailing_ws(t, n, e);
            ret_pop(t);
            if (*skip_ws(e) == '{') t->pending_fnbody = 1;
            return e;
        }
        if (*n == '{') {
            ret_pop(t);
            t->pending_fnbody = 1;
        } else if (*n == ';' || *n == '}' ||
                   (*n == '=' && n[1] == '>') || *n) {
            ret_pop(t);
        }
    }

    /* ---------------- identifiers ---------------- */
    if (is_idstart((unsigned char)c)) {
        size_t n = 0;
        while (is_idchar((unsigned char)p[n])) n++;
        int stmt = stmt_boundary(t);

        /* --- parameter list context --- */
        if (cx && cx->kind == CX_PARAMS) {
            if (cx->pdepth == 0 && cx->pstate == 0) {
                /* `this: T` parameter: drop everything */
                if (n == 4 && !strncmp(p, "this", 4) &&
                    skip_ws(p + n)[0] == ':') {
                    const char *e;
                    t->saw_ts = 1;
                    e = skip_type(t, skip_ws(p + n) + 1, 0, 0);
                    emit_trailing_ws(t, p, e);
                    cx->thisdrop = 1;
                    return e;
                }
                /* visibility modifiers / param properties */
                if ((n == 6 && !strncmp(p, "public", 6)) ||
                    (n == 7 && !strncmp(p, "private", 7)) ||
                    (n == 9 && !strncmp(p, "protected", 9)) ||
                    (n == 8 && !strncmp(p, "readonly", 8))) {
                    const char *r = skip_ws(p + n);
                    if (is_idstart((unsigned char)*r)) {
                        t->saw_ts = 1;
                        if (cx->ctor && cx->propcount < 4) {
                            size_t ln = 0;
                            while (is_idchar((unsigned char)r[ln])) ln++;
                            if (ln > 63) ln = 63;
                            memcpy(cx->props[cx->propcount], r, ln);
                            cx->props[cx->propcount][ln] = 0;
                            cx->propcount++;
                        }
                        return r;
                    }
                }
                out_range(t, p, p + n);
                p += n;
                cx->pstate = 1;
                if (*p == '?' || *p == '!') {
                    const char *r = skip_ws(p + 1);
                    if (*r == ':' || *r == ',' || *r == ')' || *r == '=') {
                        t->saw_ts = 1;
                        return p + 1;
                    }
                }
                return p;
            }
            /* expression position inside the list: fall through to the
             * general identifier handling below */
        }

        /* --- declarator list context --- */
        else if (cx && cx->kind == CX_DECL) {
            if (cx->ddepth == 0 && cx->dstate == 0) {
                out_range(t, p, p + n);
                p += n;
                cx->dstate = 1;
                /* definite assignment `let x!: T` */
                if (*p == '!') {
                    const char *r = skip_ws(p + 1);
                    if (*r == ':') {
                        t->saw_ts = 1;
                        return r;
                    }
                }
                return p;
            }
            /* initializer expression: general handling below */
        }

        /* --- class member start --- */
        else if (cx && cx->kind == CX_CLASS && cx->mstart) {
            const char *d = member_drop_pos(t, p);
            if (d) return d;                     /* dropped member */
            /* modifiers */
            if ((n == 6 && !strncmp(p, "public", 6)) ||
                (n == 7 && !strncmp(p, "private", 7)) ||
                (n == 9 && !strncmp(p, "protected", 9)) ||
                (n == 8 && !strncmp(p, "readonly", 8)) ||
                (n == 8 && !strncmp(p, "abstract", 8)) ||
                (n == 8 && !strncmp(p, "override", 8))) {
                t->saw_ts = 1;
                return skip_ws(p + n);
            }
            /* accessors and static members: the NEXT word is the name */
            if (((n == 3 && !strncmp(p, "get", 3)) ||
                 (n == 3 && !strncmp(p, "set", 3)) ||
                 (n == 6 && !strncmp(p, "static", 6))) &&
                is_idstart((unsigned char)*skip_ws(p + n))) {
                out_range(t, p, p + n);
                return p + n;                    /* mstart stays 1 */
            }
            /* static block: `static { ... }` - treat as a plain block */
            if (n == 6 && !strncmp(p, "static", 6) &&
                skip_ws(p + n)[0] == '{') {
                const char *r = skip_ws(p + n);
                out_range(t, p, r);
                outc(t, '{');
                t->brace_depth++;
                p = r + 1;
                push_cx(t, CX_BLOCK);
                cx_top(t)->bdepth = t->brace_depth;
                cx->mstart = 0;
                return p;
            }
            out_range(t, p, p + n);
            p += n;
            {
                const char *r = p;
                if (*r == '?' || *r == '!') {     /* optional/definite field */
                    const char *s2 = skip_ws(r + 1);
                    if (*s2 == ':' || *s2 == '=' || *s2 == ';') {
                        t->saw_ts = 1;
                        p = r + 1;
                        r = p;
                    }
                }
                r = skip_ws(p);
                if (*r == '<') {                  /* method generics */
                    const char *g = match_angle(r);
                    if (g && looks_like_type_args(r, g)) {
                        t->saw_ts = 1;
                        out_range(t, p, r);
                        p = skip_ws(g + 1);
                        r = p;
                    }
                }
                if (*r == '(') {
                    cx->mstart = 0;               /* method: params next */
                    out_range(t, p, r);          /* ws before the list */
                    outc(t, '(');
                    push_params(t, 0, 1, n == 11 && !strncmp(p - n, "constructor", 11));
                    return r + 1;
                }
            }
            /* field: mstart stays 1 so the `:` branch skips the type */
            return p;
        }

        /* --- statement-level TS constructs (any normal context) --- */
        if (stmt && kw_at(p, "interface")) {
            return drop_interface(t, p);
        }
        if (stmt && kw_at(p, "type") &&
            is_idstart((unsigned char)*skip_ws(p + 4))) {
            const char *e = skip_ws(p + 4);
            while (is_idchar((unsigned char)*e)) e++;
            e = skip_ws(e);
            if (*e == '<') {
                const char *g = match_angle(e);
                if (g) e = skip_ws(g + 1);
            }
            if (*e == '=' || *e == '{')
                return drop_type_alias(t, p);
        }
        if (stmt && kw_at(p, "declare")) {
            const char *d = drop_declare(t, p);
            if (!d) return NULL;
            return d;
        }
        if (stmt && kw_at(p, "namespace")) {
            snprintf(s_reason, sizeof(s_reason), "namespaces");
            return NULL;
        }
        if (stmt && kw_at(p, "import") && kw_at(skip_ws(p + 6), "type"))
            return drop_import_export_type(t, skip_ws(p + 6) + 4);
        if (stmt && kw_at(p, "export")) {
            const char *e = skip_ws(p + 6);
            if (kw_at(e, "interface")) return drop_interface(t, e);
            if (kw_at(e, "type") || kw_at(e, "declare"))
                return drop_import_export_type(t, e);
            if (kw_at(e, "namespace")) {
                snprintf(s_reason, sizeof(s_reason), "namespaces");
                return NULL;
            }
            if (kw_at(e, "enum") || (kw_at(e, "const") &&
                                     kw_at(skip_ws(e + 5), "enum"))) {
                int cenum = kw_at(e, "const");
                const char *r = cenum ? skip_ws(e + 5) : e;
                r = skip_ws(r + 4);
                if (is_idstart((unsigned char)*r)) {
                    char name[96];
                    const char *n2 = r;
                    size_t ln;
                    while (is_idchar((unsigned char)*r)) r++;
                    ln = (size_t)(r - n2);
                    if (ln >= sizeof(name)) ln = sizeof(name) - 1;
                    memcpy(name, n2, ln); name[ln] = 0;
                    r = skip_ws(r);
                    if (*r == '{')
                        return emit_enum(t, r, name, "export ");
                }
            }
            out_range(t, p, p + n);
            return p + n;
        }
        if (stmt && kw_at(p, "enum")) {
            const char *r = skip_ws(p + 4);
            if (is_idstart((unsigned char)*r)) {
                char name[96];
                const char *n2 = r;
                size_t ln;
                while (is_idchar((unsigned char)*r)) r++;
                ln = (size_t)(r - n2);
                if (ln >= sizeof(name)) ln = sizeof(name) - 1;
                memcpy(name, n2, ln); name[ln] = 0;
                r = skip_ws(r);
                if (*r == '{')
                    return emit_enum(t, r, name, "");
            }
            out_range(t, p, p + n);
            return p + n;
        }
        if (kw_at(p, "const") && kw_at(skip_ws(p + 5), "enum")) {
            const char *r = skip_ws(p + 5);
            r = skip_ws(r + 4);
            if (is_idstart((unsigned char)*r)) {
                char name[96];
                const char *n2 = r;
                size_t ln;
                while (is_idchar((unsigned char)*r)) r++;
                ln = (size_t)(r - n2);
                if (ln >= sizeof(name)) ln = sizeof(name) - 1;
                memcpy(name, n2, ln); name[ln] = 0;
                r = skip_ws(r);
                if (*r == '{')
                    return emit_enum(t, r, name, "");
            }
            /* plain const: fall through to the declarator branch */
        }
        if (kw_at(p, "abstract") && kw_at(skip_ws(p + 8), "class")) {
            t->saw_ts = 1;
            return skip_ws(p + 8);                 /* drop `abstract` */
        }
        if (c == '@') {
            snprintf(s_reason, sizeof(s_reason), "decorators");
            return NULL;
        }
        if (kw_at(p, "class")) {
            const char *r;
            out_range(t, p, p + 5);
            p += 5;
            r = skip_ws(p);
            if (is_idstart((unsigned char)*r)) {
                while (is_idchar((unsigned char)*r)) r++;
                out_range(t, p, r);              /* " Name" */
                p = r;
            }
            if (*skip_ws(p) == '<') {             /* class generics */
                const char *g = match_angle(skip_ws(p));
                if (g) { t->saw_ts = 1; p = g + 1; }
            }
            if (kw_at(skip_ws(p), "extends")) {
                const char *w = p;                /* keep the spacing */
                const char *e = skip_ws(skip_ws(p) + 7); /* past "extends" */
                while (is_idchar((unsigned char)*e)) e++;
                out_range(t, w, e);              /* " extends Name" */
                p = e;
                if (*skip_ws(p) == '<') {
                    const char *g = match_angle(skip_ws(p));
                    if (g) { t->saw_ts = 1; p = g + 1; }
                }
            }
            while (kw_at(skip_ws(p), "implements")) {
                const char *e = skip_ws(skip_ws(p) + 10);
                while (*e) {
                    if (*e == '{' || *e == ';') break;
                    e++;
                }
                t->saw_ts = 1;
                p = e;
                outc(t, ' ');                     /* keep the body spacing */
            }
            t->pending_class = 1;
            return p;
        }
        if (kw_at(p, "function") &&
            (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r' ||
             p[8] == '*' || p[8] == '(')) {
            /* overload lookahead */
            const char *r = skip_ws(p + 8);
            if (*r == '*') r = skip_ws(r + 1);
            if (is_idstart((unsigned char)*r))
                while (is_idchar((unsigned char)*r)) r++;
            r = skip_ws(r);
            if (*r == '<') {
                const char *g = match_angle(r);
                if (g) r = skip_ws(g + 1);
            }
            if (*r == '(') {
                const char *close = match_close(r);
                const char *r2 = skip_ws(close + 1);
                if (*r2 == ':') r2 = skip_type(t, r2 + 1, 0, 1);
                r2 = skip_ws(r2);
                if (*r2 == ';') {                  /* overload: drop */
                    t->saw_ts = 1;
                    return r2 + 1;
                }
            }
            /* real declaration */
            out_range(t, p, p + 8);
            p += 8;
            if (*p == '*') { outc(t, '*'); p = skip_ws(p + 1); }
            {
                const char *w = p;              /* keep the spacing */
                if (is_idstart((unsigned char)*skip_ws(p))) {
                    while (is_idchar((unsigned char)*p)) p++;
                    if (p == w) p = skip_ws(p);  /* just ws: copy later */
                    out_range(t, w, p);
                }
            }
            p = skip_ws(p);
            if (*p == '<') {
                const char *g = match_angle(p);
                if (g) { t->saw_ts = 1; p = skip_ws(g + 1); }
            }
            t->pending_fn = 1;
            return p;
        }
        if (stmt && (kw_at(p, "let") || kw_at(p, "var")) &&
            (p[3] == ' ' || p[3] == '\t' || p[3] == '\n' || p[3] == '\r')) {
            out_range(t, p, p + 3);
            push_cx(t, CX_DECL);
            return p + 3;
        }
        if (stmt && kw_at(p, "const") &&
            (p[5] == ' ' || p[5] == '\t' || p[5] == '\n' || p[5] == '\r')) {
            out_range(t, p, p + 5);
            push_cx(t, CX_DECL);
            return p + 5;
        }
        /* casts */
        if ((kw_at(p, "as") || kw_at(p, "satisfies")) && t->last_sig != '.') {
            const char *r = skip_ws(p + n);
            if (is_idstart((unsigned char)*r) || *r == '(' || *r == '{' ||
                *r == '[' || *r == '\'' || *r == '"' || *r == '`')
                return skip_type(t, r, 0, 0);
        }
        /* ordinary identifier */
        out_range(t, p, p + n);
        p += n;
        {
            const char *q = p;
            if (q[0] == '!' && q[1] != '=') {      /* non-null assertion */
                char nx = q[1];
                if (nx == '.' || nx == '(' || nx == '[' || nx == ';' ||
                    nx == ',' || nx == ')' || nx == ']' || nx == '}' ||
                    nx == '+' || nx == '-' || nx == '*' || nx == '%' ||
                    nx == '?' || nx == ':' || nx == '=' || nx == 0 ||
                    ((nx == ' ' || nx == '\n' || nx == '\r' || nx == '\t') &&
                     *skip_ws(q + 1) == '.')) {
                    t->saw_ts = 1;
                    return q + 1;
                }
            }
            if (q[0] == '<') {                      /* generic call site */
                const char *g = match_angle(q);
                if (g && looks_like_type_args(q, g) &&
                    skip_ws(g + 1)[0] == '(') {
                    t->saw_ts = 1;
                    return skip_ws(g + 1);
                }
            }
            if (n == 8 && !strncmp(p - n, "function", 8))
                t->pending_fn = 1;
            if (n == 5 && !strncmp(p - n, "catch", 5))
                t->pending_fn = 1;                 /* catch (e: T) */
            if ((n == 6 && !strncmp(p - n, "return", 6)) ||
                (n == 5 && !strncmp(p - n, "typeof", 5)) ||
                (n == 4 && !strncmp(p - n, "case", 4)) ||
                (n == 6 && !strncmp(p - n, "delete", 6)) ||
                (n == 4 && !strncmp(p - n, "void", 4)) ||
                (n == 3 && !strncmp(p - n, "new", 3)) ||
                (n == 10 && !strncmp(p - n, "instanceof", 10)) ||
                (n == 2 && !strncmp(p - n, "in", 2)) ||
                (n == 2 && !strncmp(p - n, "of", 2)))
                t->kw_regex = 1;
            return p;
        }
    }

    /* ---------------- punctuation ---------------- */
    if (c == '@' && stmt_boundary(t)) {
        snprintf(s_reason, sizeof(s_reason), "decorators");
        return NULL;
    }

    if (c == '[' && cx && cx->kind == CX_CLASS && cx->mstart) {
        const char *close = match_close(p);
        const char *scan = p + 1;
        while (scan < close && *scan != ':' && *scan) scan++;
        if (scan < close && *close == ']' &&
            skip_ws(close + 1)[0] == ':') {
            /* index signature: drop through the member's `;` */
            const char *q = skip_ws(close + 1);
            t->saw_ts = 1;
            q = skip_type(t, q + 1, 0, 0);
            q = skip_ws(q);
            if (*q == ';') q++;
            return q;
        }
        /* computed member name: copy it, the method params after it */
        {
            const char *q = close + 1;
            outc(t, '[');
            out_range(t, p + 1, close);
            outc(t, ']');
            q = skip_ws(q);
            if (*q == '(') {
                out_range(t, close + 1, q);
                outc(t, '(');
                cx->mstart = 0;
                push_params(t, 0, 1, 0);
                return q + 1;
            }
            cx->mstart = 0;
            return q;
        }
    }

    if (c == '(') {
        if (t->pending_fn) {
            t->pending_fn = 0;
            outc(t, '(');
            push_params(t, 0, 1, 0);
            return p + 1;
        }
        if (is_arrow_params(t, p)) {
            outc(t, '(');
            push_params(t, 1, 1, 0);
            return p + 1;
        }
        if (cx && cx->kind == CX_CLASS && cx->mstart) {
            /* quoted/computed-name method: params are a param list */
            outc(t, '(');
            push_params(t, 0, 1, 0);
            return p + 1;
        }
        outc(t, '(');
        if (cx && cx->kind == CX_PARAMS) cx->pdepth++;
        if (cx && cx->kind == CX_DECL) cx->ddepth++;
        return p + 1;
    }

    if (c == ')' ) {
        outc(t, ')');
        if (cx && cx->kind == CX_PARAMS) {
            if (cx->pdepth > 0) {
                cx->pdepth--;
                return p + 1;
            }
            /* pop the parameter list */
            {
                int arrow = cx->arrow, fnret = cx->fnret, ctor = cx->ctor;
                int i, cnt = cx->propcount;
                char props[4][64];
                memcpy(props, cx->props, sizeof(props));
                t->cx_top--;
                if (arrow || fnret) ret_push(t, arrow ? 2 : 1);
                if (ctor && cnt) {
                    size_t il = 0;
                    t->inject[0] = 0;
                    for (i = 0; i < cnt; i++)
                        il += (size_t)snprintf(t->inject + il,
                                               sizeof(t->inject) - il,
                                               "this.%s=%s;", props[i], props[i]);
                    t->saw_ts = 1;
                }
            }
            return p + 1;
        }
        if (cx && cx->kind == CX_DECL && cx->ddepth > 0)
            cx->ddepth--;
        return p + 1;
    }

    if (c == ']' ) {
        outc(t, ']');
        if (cx && cx->kind == CX_PARAMS && cx->pdepth > 0) cx->pdepth--;
        if (cx && cx->kind == CX_DECL && cx->ddepth > 0) cx->ddepth--;
        return p + 1;
    }

    if (c == '}') {
        outc(t, '}');
        t->brace_depth--;
        if (cx && cx->kind == CX_BLOCK && t->brace_depth < cx->bdepth) {
            t->cx_top--;                          /* leaving a nested block */
            cx = cx_top(t);                       /* recompute for CLASS */
        }
        if (cx && cx->kind == CX_CLASS) {
            if (t->brace_depth < cx->cdepth) {    /* the class body closes */
                t->cx_top--;
                return p + 1;
            }
            if (t->brace_depth == cx->cdepth)
                cx->mstart = 1;                   /* back at member level */
            return p + 1;
        }
        if (cx && cx->kind == CX_DECL && cx->ddepth == 0) {
            t->cx_top--;
            return p + 1;
        }
        return p + 1;
    }

    if (c == '{') {
        outc(t, '{');
        t->brace_depth++;
        if (t->inject[0]) {
            outs(t, t->inject);
            t->inject[0] = 0;
        }
        if (t->pending_class) {
            t->pending_class = 0;
            t->pending_fnbody = 0;
            push_cx(t, CX_CLASS);
            {
                Cx *d = cx_top(t);
                d->cdepth = t->brace_depth;
                d->mstart = 1;
            }
            return p + 1;
        }
        if (t->pending_fnbody) {
            t->pending_fnbody = 0;
            push_cx(t, CX_BLOCK);
            cx_top(t)->bdepth = t->brace_depth;
            return p + 1;
        }
        if (cx && (cx->kind == CX_PARAMS || cx->kind == CX_DECL ||
                   (cx->kind == CX_CLASS && !cx->mstart))) {
            /* a block opened inside an expression */
            push_cx(t, CX_BLOCK);
            cx_top(t)->bdepth = t->brace_depth;
            return p + 1;
        }
        return p + 1;
    }

    if (c == ';') {
        outc(t, ';');
        if (cx && cx->kind == CX_CLASS) {
            if (t->brace_depth == cx->cdepth) cx->mstart = 1;
            return p + 1;
        }
        if (cx && cx->kind == CX_DECL && cx->ddepth == 0) {
            t->cx_top--;
            return p + 1;
        }
        return p + 1;
    }

    if (c == ',' && cx && cx->kind == CX_PARAMS && cx->pdepth == 0) {
        if (cx->thisdrop) {                        /* drop the comma after
                                                     * a `this` param */
            cx->thisdrop = 0;
            t->saw_ts = 1;
            return p + 1;
        }
        outc(t, ',');
        cx->pstate = 0;
        return p + 1;
    }

    if (c == ':' && cx && cx->kind == CX_PARAMS && cx->pdepth == 0) {
        if (cx->pstate == 2 && t->ret_top > 0) {   /* nested return annot */
            int mode = t->ret[t->ret_top - 1];
            const char *e = skip_type(t, p + 1, mode == 2, mode == 1);
            emit_trailing_ws(t, p, e);
            ret_pop(t);
            if (*skip_ws(e) == '{') t->pending_fnbody = 1;
            return e;
        }
        if (cx->pstate <= 1) {
            const char *e;
            cx->pstate = 2;
            e = skip_type(t, p + 1, 0, 0);
            emit_trailing_ws(t, p, e);
            return e;
        }
        outc(t, ':');                              /* ternary in a default */
        return p + 1;
    }

    if (c == '=' && cx && cx->kind == CX_PARAMS && cx->pdepth == 0) {
        outc(t, '=');
        cx->pstate = 2;
        return p + 1;
    }

    if (c == ':' && cx && cx->kind == CX_DECL && cx->ddepth == 0 &&
        cx->dstate == 1) {
        const char *e;
        cx->dstate = 2;
        e = skip_type(t, p + 1, 0, 0);
        emit_trailing_ws(t, p, e);
        return e;
    }
    if (c == '=' && cx && cx->kind == CX_DECL && cx->ddepth == 0 &&
        cx->dstate != 2) {
        outc(t, '=');
        cx->dstate = 2;
        return p + 1;
    }
    if (c == ',' && cx && cx->kind == CX_DECL && cx->ddepth == 0 &&
        cx->dstate == 2) {
        outc(t, ',');
        cx->dstate = 0;
        return p + 1;
    }

    if (c == ':' && cx && cx->kind == CX_CLASS && cx->mstart) {
        const char *e;
        cx->mstart = 0;                            /* field annotation */
        e = skip_type(t, p + 1, 0, 0);
        emit_trailing_ws(t, p, e);
        return e;
    }
    if (c == '=' && cx && cx->kind == CX_CLASS && cx->mstart) {
        outc(t, '=');
        cx->mstart = 0;                            /* field initializer */
        return p + 1;
    }

    /* whitespace */
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        outc(t, c);
        /* outc clobbered last_sig; restore boundary info is not needed
         * because stmt_boundary() only tests 0/;/{/} which are not
         * whitespace. */
        return p + 1;
    }

    outc(t, c);
    return p + 1;
}

int vw_ts_strip(const char *src, char *dst, size_t cap)
{
    Ts t;
    const char *p;
    if (!src || !dst || cap < 4) return -1;
    memset(&t, 0, sizeof(t));
    t.buf = dst;
    t.cap = cap;
    dst[0] = 0;
    s_reason[0] = 0;
    p = src;
    while (*p && !t.overflow) {
        p = step(&t, p);
        if (!p) return -2;
    }
    if (t.overflow) return -1;
    return t.saw_ts ? 1 : 0;
}

const char *vw_ts_unsupported_reason(void)
{
    return s_reason;
}
