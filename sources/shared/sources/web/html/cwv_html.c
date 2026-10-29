/* ============================================================================
 * FILE ORIENTATION - read this first
 * ============================================================================
 * WHAT THIS FILE IS: the HTML half of the web backend.  It turns raw page
 * text into a tree of CwvNode boxes with positions and sizes.  It never
 * draws a pixel and it never runs JavaScript.
 *
 * THE PIPELINE (who owns which stage):
 *   1. PARSE  - HERE, SECTION 7/8.  Raw HTML becomes node objects.  TAG_TABLE
 *               (SECTION 7) is the registry of known tags - ADD NEW HTML TAGS
 *               THERE (one row, no other edit needed for a plain block).
 *   2. STYLE  - cwv_css.c.  CSS rules + inline styles fill node fields
 *               (color, background, borders, font weight...).
 *   3. LAYOUT - HERE (layout_block / layout_inline_block and friends):
 *               positions and sizes, Chrome-style flow.
 *   4. PAINT  - cwv_backend.c.  draw_node walks the tree and calls each
 *               tag's draw hook from TAG_TABLE.  DRAW FUNCTIONS LIVE THERE,
 *               NOT IN THIS FILE.  TEXT IS DRAWN THERE TOO (the shared text
 *               painter and the button-label centering math).  To change what
 *               a tag looks like on screen: point its TAG_TABLE row at a draw
 *               function in cwv_backend.c.
 *   5. SCRIPT - cwv_js.c.  The DOM that page JavaScript sees (Duktape).
 *
 * Rule of thumb: WHERE something is = this file.  What it LOOKS like =
 * css.c.  Pixels on screen = backend.c.  Scripting = js.c.
 *
 * Everything below is still driven by the tables - extend the tables, not
 * the switches.
 * ============================================================================ */

/* ============================================================================
 *  Vita WebView - the HTML parser + layout engine (cwv_html.c)
 * ============================================================================
 *  Everything on the HTML side of the renderer:
 *
 *    SECTION A  THE TAG REGISTRY  (extension point #2)
 *              TAG_TABLE[] with per-tag flags, defaults and hooks
 *    SECTION B  node construction (defaults, style inheritance)
 *    SECTION C  the recursive parser: text, comments, close-tag matching,
 *              and the per-tag parse/setup hooks
 *    SECTION D  layout: the CSS display modes block / inline-block / inline
 *
 *  "Layout" happens during parsing: the renderer keeps a text cursor
 *  (r->cursor_x / r->cursor_y) and advances it like a typewriter - inline
 *  content moves along the line, block content starts a new line. That is
 *  why nodes are already positioned when parsing finishes.
 *  See docs/EXTENDING_THE_RENDERER.md for the "add one table row" recipe.
 * ==========================================================================*/

#include "core/cwv_renderer.h"


/* ============================================================================
 * SECTION 7: THE TAG REGISTRY  (extension point #2)
 * ============================================================================
 * One row per known HTML tag. The parser (SECTION 8) looks every tag up in
 * TAG_TABLE[] and uses the row to decide:
 *
 *   flags           - what KIND of tag it is (see TAGF_* below)
 *   font_scale_em   - heading font scale relative to the body font
 *   margin_*        - the browser-like default margins (UA stylesheet)
 *   parse_full      - optional: a hook that replaces the whole parse step
 *                     (used by <html>, <body>, <img> which need custom flow)
 *   setup           - optional: a hook that customizes the node AFTER common
 *                     attributes/CSS are applied but BEFORE children are
 *                     parsed (used by <a>, <button>, <select>, ...)
 *   draw            - optional: a hook that replaces the default text
 *                     painting for the tag (see draw_node in SECTION 12)
 *
 * >>> TO ADD A NEW HTML TAG <<<
 *   1. Add one row to TAG_TABLE[] below, e.g.
 *        { "marquee", 0, 1.0f, NULL, NULL, 0.0f, NULL, tag_setup_marquee, NULL },
 *   2. If it needs special parsing, write a small setup hook:
 *        static void tag_setup_marquee(TagCtx *c, CwvNode *n) { ... }
 *   3. If it needs custom visuals, write a draw hook and set the `draw` field.
 *   4. Rebuild. Unknown attributes/CSS on the tag are already ignored.
 * Tags NOT in the table still render as generic block/inline containers.
 * ==========================================================================*/

/* Forward declarations: the parse/setup hooks and the text wrapper are
 * defined further down in this file; the registry table below references
 * the hooks, and the parser calls the wrapper before its definition. */
static void parse_html_range(CwvRenderer *r, const char *start,
                            const char *end, const CwvNode *parent);
static void tag_parse_html(TagCtx *ctx);
static void tag_parse_body(TagCtx *ctx);
static void tag_parse_img(TagCtx *ctx);
static void tag_setup_a(TagCtx *ctx, CwvNode *n);
static void tag_setup_select(TagCtx *ctx, CwvNode *n);
static void tag_setup_option(TagCtx *ctx, CwvNode *n);
static void tag_setup_button(TagCtx *ctx, CwvNode *n);
static void tag_parse_input(TagCtx *ctx);
static void layout_inline_block(CwvRenderer *r, CwvNode *n, int child_start,
                                float box_y, int box_index,
                                float saved_x, float saved_y, float flow_lh);
static void tag_setup_input(TagCtx *ctx, CwvNode *n);
static void tag_setup_ul(TagCtx *ctx, CwvNode *n);
static void tag_setup_ol(TagCtx *ctx, CwvNode *n);
static void tag_setup_li(TagCtx *ctx, CwvNode *n);
static void tag_setup_blockquote(TagCtx *ctx, CwvNode *n);
static void tag_draw_li(TagDrawCtx *ctx);
static void tag_setup_u(TagCtx *ctx, CwvNode *n);
static void tag_setup_strike(TagCtx *ctx, CwvNode *n);
static void tag_setup_mark(TagCtx *ctx, CwvNode *n);
static void wrap_and_add(CwvRenderer *r, CwvNode *n);


/* ======================================================================
 * INLINE SVG SUBSET (2026-10-05 (67))
 * A static, Chrome-shaped subset: <svg width height viewBox> with the
 * common shape elements.  Shape tags are TAGF_VOID records - no flow,
 * no children - whose points are BAKED (viewBox-scaled, origin-shifted)
 * into r->svg_pts at parse time; the draw hook in cwv_backend.c paints
 * them with portable primitives (rects / scanlines), so every console
 * renders the same icons with zero per-port code.
 * Out of scope (documented): CSS-styled SVG internals, <text>, SMIL,
 * transforms, gradients, masks, external .svg files, JS createElementNS.
 * ====================================================================== */
static void tag_setup_svg(TagCtx *ctx, CwvNode *n);
static void tag_parse_svg_shape(TagCtx *ctx);
static void tag_draw_svg_shape(TagDrawCtx *ctx);

/* Baked-coordinate clamp (76): the <img> decoder fuzz taught us that
 * hostile geometry must fail FAST and BOUNDED, not hang the scanline
 * loops or blow up the page's scrollable extent.  NaN fails every
 * comparison, so the !(...) form is the only one that catches it. */
#define SVG_COORD_MAX 16384.0f
static float svg_clamp_coord(float v) {
    if (!(v > -SVG_COORD_MAX)) v = -SVG_COORD_MAX;
    if (!(v <  SVG_COORD_MAX)) v =  SVG_COORD_MAX;
    return v;
}

/* append one baked point pair; returns 0 when the arena is full.
 * Coordinates are clamped into the safe drawing range (76). */
static int svg_push_pt(CwvRenderer *r, float x, float y) {
    if (r->svg_pts_used + 2 > VW_SVG_MAX_PTS) return 0;
    r->svg_pts[r->svg_pts_used++] = svg_clamp_coord(x);
    r->svg_pts[r->svg_pts_used++] = svg_clamp_coord(y);
    return 1;
}

/* viewBox scaling context: tag_setup_svg pushes {children depth, scale};
 * shape parses look up the top entry for their depth.  Sibling <svg>s
 * replace each other's entry, nested ones stack (cap 4). */
static void node_from_parent(CwvRenderer *r, CwvNode *n, const char *tag,
                               const CwvNode *parent);

static void svg_vb_push(CwvRenderer *r, int depth, float scale,
                        float minx, float miny) {
    int i;
    for (i = r->svg_vb_n - 1; i >= 0; i--)
        if (r->svg_vb[i].depth >= depth) r->svg_vb_n = i;   /* replace */
    if (r->svg_vb_n >= 4) r->svg_vb_n = 3;
    r->svg_vb[r->svg_vb_n].depth = depth;
    r->svg_vb[r->svg_vb_n].scale = scale;
    r->svg_vb[r->svg_vb_n].minx  = minx;
    r->svg_vb[r->svg_vb_n].miny  = miny;
    r->svg_vb_n++;
}
static int svg_vb_lookup(CwvRenderer *r, float *scale, float *minx,
                         float *miny) {
    int i;
    for (i = r->svg_vb_n - 1; i >= 0; i--)
        if (r->svg_vb[i].depth <= r->parse_depth) {
            *scale = r->svg_vb[i].scale; *minx = r->svg_vb[i].minx;
            *miny = r->svg_vb[i].miny;
            return 1;
        }
    *scale = 1.0f; *minx = *miny = 0.0f;
    return 0;
}

static void tag_setup_svg(TagCtx *ctx, CwvNode *n) {
    CwvRenderer *r = ctx->r;
    char a[128];
    float w = 300.0f, h = 150.0f;          /* Chrome's replaced-element default */
    float scale = 1.0f, minx = 0.0f, miny = 0.0f;
    if (find_attr(ctx->q, ctx->gt, "width", a, sizeof(a))) w = (float)atof(a);
    if (find_attr(ctx->q, ctx->gt, "height", a, sizeof(a))) h = (float)atof(a);
    /* (76) NaN passes "w < 1" (all comparisons are false), so reject with
     * the negated form and cap the box like the img element's intrinsic
     * size guard: a giant/NaN svg box must not corrupt the page extent. */
    if (!(w >= 1.0f) || !(w <= 1e6f)) w = 300.0f;   /* inf/NaN -> auto */
    if (!(h >= 1.0f) || !(h <= 1e6f)) h = 150.0f;
    if (w > 4096.0f) w = 4096.0f;
    if (h > 4096.0f) h = 4096.0f;
    n->w = w; n->h = h;
    if (find_attr(ctx->q, ctx->gt, "viewBox", a, sizeof(a))) {
        float vw = 0.0f, vh = 0.0f;
        sscanf(a, "%f %f %f %f", &minx, &miny, &vw, &vh);
        if (vw > 0.5f && vh > 0.5f) {
            /* preserveAspectRatio="none"-style fill of the box: what
               icons in practice rely on. */
            float sx = w / vw, sy = h / vh;
            scale = sx < sy ? sx : sy;
        }
    }
    /* children parse one level deeper (flow_context bumps parse_depth) */
    svg_vb_push(r, r->parse_depth + 1, scale, minx, miny);
}

/* attribute float with a default */
static float svg_attr_f(const char *q, const char *gt, const char *name,
                        float def) {
    char a[64];
    if (!find_attr(q, gt, name, a, sizeof(a))) return def;
    if (!a[0]) return def;
    {
        float v = (float)atof(a);
        /* (76) NaN/inf attributes parse cleanly out of atof and then
           sail past every "<=" sanity check - the same class of hole the
           image fuzz found in the size math. */
        if (!(v >= -1e6f) || !(v <= 1e6f)) return def;
        return v;
    }
}

/* fill/stroke presentation attributes (fill="red", stroke="#333",
   stroke-width="2"); "none"/absent disables the layer. */
static void svg_paint_attrs(const char *q, const char *gt, CwvNode *n) {
    char a[64];
    if (find_attr(q, gt, "fill", a, sizeof(a)) && a[0]) {
        if (ci_eq(a, "none")) n->svg_fill_on = 0;
        else { n->svg_fill = parse_color(a, n->svg_fill); n->svg_fill_on = 1; }
    }
    if (find_attr(q, gt, "stroke", a, sizeof(a)) && a[0]) {
        if (ci_eq(a, "none")) n->svg_stroke_on = 0;
        else { n->svg_stroke = parse_color(a, n->svg_stroke); n->svg_stroke_on = 1; }
    }
    n->svg_sw = svg_attr_f(q, gt, "stroke-width", 1.0f);
}

/* number list "12,34 56,78 ..." -> baked point pairs */
static int svg_parse_points(CwvRenderer *r, const char *v, float scale,
                            float minx, float miny) {
    int n = 0;
    const char *p = v;
    while (*p) {
        float x, y;
        char *e1, *e2;
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' ||
               *p == '\r') p++;
        if (!*p) break;
        x = (float)strtod(p, &e1);
        if (e1 == p) break;
        p = e1;
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        y = (float)strtod(p, &e2);
        if (e2 == p) break;
        p = e2;
        /* (76) the CWI fuzz lesson: data that no longer fits must be
           REJECTED, never silently accepted as a truncated shape. */
        if (!svg_push_pt(r, (x - minx) * scale, (y - miny) * scale))
            return -1;
        n++;
    }
    return n;
}

/* SVG path data: M m L l H h V v C c S s Q q T t A a Z z.  Curves are
 * flattened into polylines at parse time (cubic 8 segments, quad 6);
 * arcs (A/a) draw as a straight chord to their endpoint (documented
 * approximation); Z marks the path fillable. */
static int svg_parse_path(CwvRenderer *r, const char *d, float scale,
                          float minx, float miny, int *closed) {
    int n = 0, sub_start = r->svg_pts_used / 2;
    float cx = 0.0f, cy = 0.0f;       /* current point */
    float lcx = 0.0f, lcy = 0.0f;      /* last cubic control (for S) */
    float lqx = 0.0f, lqy = 0.0f;      /* last quad control (for T) */
    char last_cmd = 0;
    const char *p = d;
    *closed = 0;

#define SVG_X(x)   ((x - minx) * scale)
#define SVG_Y(y)   ((y - miny) * scale)
#define SVG_EAT_NUM(var) do {                                     \
        while (*p == ' ' || *p == ',' || *p == '\t' ||             \
               *p == '\n' || *p == '\r') p++;                       \
        (var) = (float)strtod(p, &e);                              \
        if (e == p) return n;                                      \
        p = e;                                                    \
    } while (0)

    while (*p) {
        char cmd;
        char *e;
        float x1, y1, x2, y2, x3, y3;
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' ||
               *p == '\r') p++;
        if (!*p) break;
        if (isalpha((unsigned char)*p)) { cmd = *p++; last_cmd = cmd; }
        else cmd = last_cmd;      /* implicit repeat of the last command */
        switch (cmd) {
        case 'M': case 'm':
            SVG_EAT_NUM(x1); SVG_EAT_NUM(y1);
            if (cmd == 'm') { x1 += cx; y1 += cy; }
            cx = x1; cy = y1;
            if (!svg_push_pt(r, SVG_X(cx), SVG_Y(cy))) { return -1; } n++;
            last_cmd = (cmd == 'M') ? 'L' : 'l';   /* M chains as L */
            break;
        case 'L': case 'l':
            SVG_EAT_NUM(x1); SVG_EAT_NUM(y1);
            if (cmd == 'l') { x1 += cx; y1 += cy; }
            cx = x1; cy = y1;
            if (!svg_push_pt(r, SVG_X(cx), SVG_Y(cy))) { return -1; } n++;
            break;
        case 'H': case 'h':
            SVG_EAT_NUM(x1);
            if (cmd == 'h') x1 += cx;
            cx = x1;
            if (!svg_push_pt(r, SVG_X(cx), SVG_Y(cy))) { return -1; } n++;
            break;
        case 'V': case 'v':
            SVG_EAT_NUM(y1);
            if (cmd == 'v') y1 += cy;
            cy = y1;
            if (!svg_push_pt(r, SVG_X(cx), SVG_Y(cy))) { return -1; } n++;
            break;
        case 'C': case 'c': {
            /* x1 y1 x2 y2 x3 y3: control1, control2, end */
            int i;
            SVG_EAT_NUM(x1); SVG_EAT_NUM(y1); SVG_EAT_NUM(x2);
            SVG_EAT_NUM(y2); SVG_EAT_NUM(x3); SVG_EAT_NUM(y3);
            if (cmd == 'c') { x1 += cx; y1 += cy; x2 += cx; y2 += cy;
                              x3 += cx; y3 += cy; }
            /* flatten the cubic in 8 segments */
            for (i = 1; i <= 8; i++) {
                float t = (float)i / 8.0f, u = 1.0f - t;
                float fx = u*u*u*cx + 3.0f*u*u*t*x1
                         + 3.0f*u*t*t*x2 + t*t*t*x3;
                float fy = u*u*u*cy + 3.0f*u*u*t*y1
                         + 3.0f*u*t*t*y2 + t*t*t*y3;
                if (!svg_push_pt(r, SVG_X(fx), SVG_Y(fy))) { return -1; } n++;
            }
            lcx = x2; lcy = y2;         /* S reflection source */
            cx = x3; cy = y3;
            break;
        }
        case 'S': case 's': {
            float rx, ry, ex, ey;
            SVG_EAT_NUM(x2); SVG_EAT_NUM(y2); SVG_EAT_NUM(ex); SVG_EAT_NUM(ey);
            if (cmd == 's') { x2 += cx; y2 += cy; ex += cx; ey += cy; }
            rx = 2.0f * cx - lcx; ry = 2.0f * cy - lcy;
            {
                int i;
                for (i = 1; i <= 8; i++) {
                    float t = (float)i / 8.0f, u = 1.0f - t;
                    float fx = u*u*u*cx + 3.0f*u*u*t*rx
                             + 3.0f*u*t*t*x2 + t*t*t*ex;
                    float fy = u*u*u*cy + 3.0f*u*u*t*ry
                             + 3.0f*u*t*t*y2 + t*t*t*ey;
                    if (!svg_push_pt(r, SVG_X(fx), SVG_Y(fy))) { return -1; } n++;
                }
            }
            lcx = x2; lcy = y2;
            cx = ex; cy = ey;
            break;
        }
        case 'Q': case 'q': {
            float ex, ey;
            int i;
            SVG_EAT_NUM(x1); SVG_EAT_NUM(y1); SVG_EAT_NUM(ex); SVG_EAT_NUM(ey);
            if (cmd == 'q') { x1 += cx; y1 += cy; ex += cx; ey += cy; }
            for (i = 1; i <= 6; i++) {
                float t = (float)i / 6.0f, u = 1.0f - t;
                float fx = u*u*cx + 2.0f*u*t*x1 + t*t*ex;
                float fy = u*u*cy + 2.0f*u*t*y1 + t*t*ey;
                if (!svg_push_pt(r, SVG_X(fx), SVG_Y(fy))) { return -1; } n++;
            }
            lqx = x1; lqy = y1;
            cx = ex; cy = ey;
            break;
        }
        case 'T': case 't': {
            float ex, ey, qx, qy;
            int i;
            SVG_EAT_NUM(ex); SVG_EAT_NUM(ey);
            if (cmd == 't') { ex += cx; ey += cy; }
            qx = 2.0f * cx - lqx; qy = 2.0f * cy - lqy;
            for (i = 1; i <= 6; i++) {
                float t = (float)i / 6.0f, u = 1.0f - t;
                float fx = u*u*cx + 2.0f*u*t*qx + t*t*ex;
                float fy = u*u*cy + 2.0f*u*t*qy + t*t*ey;
                if (!svg_push_pt(r, SVG_X(fx), SVG_Y(fy))) { return -1; } n++;
            }
            lqx = qx; lqy = qy;
            cx = ex; cy = ey;
            break;
        }
        case 'A': case 'a': {
            float ex, ey;
            /* rx ry rot la sf x y: the arc geometry is consumed, the
               chord is drawn (v1 approximation, documented). */
            SVG_EAT_NUM(x1); SVG_EAT_NUM(y1); SVG_EAT_NUM(x2);
            SVG_EAT_NUM(y2); SVG_EAT_NUM(x3); SVG_EAT_NUM(y3);
            ex = x3; ey = y3;
            if (cmd == 'a') { ex += cx; ey += cy; }
            cx = ex; cy = ey;
            if (!svg_push_pt(r, SVG_X(cx), SVG_Y(cy))) { return -1; } n++;
            break;
        }
        case 'Z': case 'z':
            *closed = 1;
            /* jump back to the subpath start like a real Z */
            if (sub_start * 2 < r->svg_pts_used) {
                cx = r->svg_pts[sub_start * 2] ;
                cy = r->svg_pts[sub_start * 2 + 1];
                /* re-emit the start point so the fill closes visually
                   and relative commands continue from the right place */
                if (!svg_push_pt(r, cx, cy)) { return -1; } n++;
            }
            break;
        default:
            /* unknown command: skip one char to guarantee progress */
            p++;
            break;
        }
    }
#undef SVG_EAT_NUM
#undef SVG_X
#undef SVG_Y
    return n;
}

/* shared parse_full for the shape tags: rect, circle, ellipse, line,
 * polyline, polygon, path.  The node records the shape's points (baked
 * against the enclosing <svg>'s viewBox) and its static flow position;
 * the draw hook paints it. */
static void tag_parse_svg_shape(TagCtx *ctx) {
    CwvRenderer *r = ctx->r;
    const char *q = ctx->q, *gt = ctx->gt;
    CwvNode n;
    float scale = 1.0f, minx = 0.0f, miny = 0.0f;
    int shape = 0, pts = 0, closed = 0;
    float bbox_minx = 1e9f, bbox_miny = 1e9f, bbox_maxx = -1e9f, bbox_maxy = -1e9f;
    char a[2048];
    int i;

    svg_vb_lookup(r, &scale, &minx, &miny);

    if      (ci_eq(ctx->tag, "rect"))      shape = 1;
    else if (ci_eq(ctx->tag, "circle"))   shape = 2;
    else if (ci_eq(ctx->tag, "ellipse"))  shape = 3;
    else if (ci_eq(ctx->tag, "line"))     shape = 4;
    else if (ci_eq(ctx->tag, "polyline")) shape = 5;
    else if (ci_eq(ctx->tag, "polygon"))  shape = 6;
    else if (ci_eq(ctx->tag, "path"))     shape = 7;
    else if (ci_eq(ctx->tag, "text"))     shape = 8;
    else { ctx->next = gt + 1; return; }

    if (r->node_count >= VW_MAX_NODES ||
        r->svg_pts_used >= VW_SVG_MAX_PTS - 2) { ctx->next = gt + 1; return; }

    int arena_save = r->svg_pts_used;   /* (76) rollback point */
    node_from_parent(r, &n, ctx->tag, ctx->parent);
    n.svg_shape  = shape;
    n.svg_pt_off = r->svg_pts_used;
    n.x = r->cursor_x;            /* the <svg> content origin */
    n.y = r->cursor_y;
    n.w = 0.0f; n.h = 0.0f;
    svg_paint_attrs(q, gt, &n);
    if (shape == 5 || shape == 6) n.svg_stroke_on = n.svg_stroke_on || n.svg_fill_on ? n.svg_stroke_on : 0;  /* keep */

    switch (shape) {
    case 1: {   /* rect: x y width height */
        float x = svg_attr_f(q, gt, "x", 0.0f), y = svg_attr_f(q, gt, "y", 0.0f);
        float w = svg_attr_f(q, gt, "width", 0.0f), h = svg_attr_f(q, gt, "height", 0.0f);
        if (w <= 0 || h <= 0) { ctx->next = gt + 1; return; }
        svg_push_pt(r, (x - minx) * scale, (y - miny) * scale);
        svg_push_pt(r, (x - minx + w) * scale, (y - miny + h) * scale);
        pts = 2;
        break;
    }
    case 2: {   /* circle: cx cy r */
        float cx = svg_attr_f(q, gt, "cx", 0.0f), cy = svg_attr_f(q, gt, "cy", 0.0f);
        float rad = svg_attr_f(q, gt, "r", 0.0f);
        if (rad <= 0) { ctx->next = gt + 1; return; }
        svg_push_pt(r, (cx - minx) * scale, (cy - miny) * scale);
        svg_push_pt(r, rad * scale, rad * scale);   /* rx, ry */
        pts = 2;
        break;
    }
    case 3: {   /* ellipse: cx cy rx ry */
        float cx = svg_attr_f(q, gt, "cx", 0.0f), cy = svg_attr_f(q, gt, "cy", 0.0f);
        float rx = svg_attr_f(q, gt, "rx", 0.0f), ry = svg_attr_f(q, gt, "ry", 0.0f);
        if (rx <= 0 || ry <= 0) { ctx->next = gt + 1; return; }
        svg_push_pt(r, (cx - minx) * scale, (cy - miny) * scale);
        svg_push_pt(r, rx * scale, ry * scale);
        pts = 2;
        break;
    }
    case 4: {   /* line: x1 y1 x2 y2 */
        float x1 = svg_attr_f(q, gt, "x1", 0.0f), y1 = svg_attr_f(q, gt, "y1", 0.0f);
        float x2 = svg_attr_f(q, gt, "x2", 0.0f), y2 = svg_attr_f(q, gt, "y2", 0.0f);
        svg_push_pt(r, (x1 - minx) * scale, (y1 - miny) * scale);
        svg_push_pt(r, (x2 - minx) * scale, (y2 - miny) * scale);
        pts = 2;
        break;
    }
    case 5: case 6:   /* polyline / polygon: points */
        if (find_attr(q, gt, "points", a, sizeof(a)))
            pts = svg_parse_points(r, a, scale, minx, miny);
        if (shape == 6) n.svg_closed = 1;
        break;
    case 7:            /* path: d */
        if (find_attr(q, gt, "d", a, sizeof(a)))
            pts = svg_parse_path(r, a, scale, minx, miny, &closed);
        n.svg_closed = closed;
        break;
    case 8: {   /* text: x y baseline point; content runs inside the
                   <svg> are folded into this node at text-creation time
                   (flow_parse, (73)) - no post-parse harvest pass.  Chrome
                   defaults: black fill, 16px font. */
        float x = svg_attr_f(q, gt, "x", 0.0f), y = svg_attr_f(q, gt, "y", 0.0f);
        float fs = svg_attr_f(q, gt, "font-size", 16.0f);
        if (!svg_push_pt(r, (x - minx) * scale, (y - miny) * scale)) {
            pts = -1;   /* (76) arena full: reject, never draw stale pts */
            break;
        }
        pts = 1;
        n.font_size = (fs > 0.5f && fs <= 200.0f) ? fs / 16.0f : 1.0f; /* em */
        if (!n.svg_fill_on) { n.svg_fill = 0xFF000000u; n.svg_fill_on = 1; }
        n.svg_stroke_on = 0;   /* stroked text is not in the subset */
        break;
    }
    }
    /* (76) rejected (overflow) shapes restore the arena: no partial
     * geometry may leak into the next shape's point list. */
    if (pts < 0) { r->svg_pts_used = arena_save; ctx->next = gt + 1; return; }
    if (pts < 1) { r->svg_pts_used = arena_save; ctx->next = gt + 1; return; }
    n.svg_pt_n = pts;

    /* bbox -> node box (so scroll bounds, hit tests and debug dumps
       see the shape's real extent) */
    for (i = 0; i < pts * 2; i += 2) {
        float px = r->svg_pts[n.svg_pt_off + i];
        float py = r->svg_pts[n.svg_pt_off + i + 1];
        if (shape == 2 || shape == 3) break;   /* param form: below */
        if (px < bbox_minx) bbox_minx = px;
        if (px > bbox_maxx) bbox_maxx = px;
        if (py < bbox_miny) bbox_miny = py;
        if (py > bbox_maxy) bbox_maxy = py;
    }
    if (shape == 2 || shape == 3) {
        bbox_minx = r->svg_pts[n.svg_pt_off] - r->svg_pts[n.svg_pt_off + 2];
        bbox_maxx = r->svg_pts[n.svg_pt_off] + r->svg_pts[n.svg_pt_off + 2];
        bbox_miny = r->svg_pts[n.svg_pt_off + 1] - r->svg_pts[n.svg_pt_off + 3];
        bbox_maxy = r->svg_pts[n.svg_pt_off + 1] + r->svg_pts[n.svg_pt_off + 3];
    } else if (pts == 2) {   /* rect corners / line endpoints: pair form */
        float x0 = r->svg_pts[n.svg_pt_off], y0 = r->svg_pts[n.svg_pt_off + 1];
        float x1v = r->svg_pts[n.svg_pt_off + 2], y1v = r->svg_pts[n.svg_pt_off + 3];
        bbox_minx = x0 < x1v ? x0 : x1v; bbox_maxx = x0 > x1v ? x0 : x1v;
        bbox_miny = y0 < y1v ? y0 : y1v; bbox_maxy = y0 > y1v ? y0 : y1v;
    }
    n.w = bbox_maxx - bbox_minx;
    n.h = bbox_maxy - bbox_miny;
    if (n.w < 1.0f) n.w = 1.0f;
    if (n.h < 1.0f) n.h = 1.0f;

    r->nodes[r->node_count++] = n;
    ctx->next = gt + 1;
}

/* ---- THE TABLE: one row per known tag ------------------------------- */
static const TagInfo TAG_TABLE[] = {
    /* tag        flags                          scale  margin-top  margin-bot  mr    parse_full       setup      draw */
    { "html",     0,                             1.0f,  NULL,       NULL,       0.0f, tag_parse_html,  NULL, NULL },
    { "body",     0,                             1.0f,  NULL,       NULL,       0.0f, tag_parse_body,  NULL, NULL },

    /* images are void and fully custom (no children, own layout flow) */
    { "img",      TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_img,   NULL, NULL },
    /* Canvas is a normal layout box with an opt-in raster backing store. */
    { "canvas",   TAGF_INLINE_BLOCK,              1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },

    /* heading scale and browser-like default margins (an em string is
       resolved against the tag's own font size, like a real UA sheet) */
    { "h1",       TAGF_HEADING,                  2.00f, "0.67em",   "0.67em",   0.0f, NULL, NULL, NULL },
    { "h2",       TAGF_HEADING,                  1.50f, "0.83em",   "0.83em",   0.0f, NULL, NULL, NULL },
    { "h3",       TAGF_HEADING,                  1.17f, "1em",      "1em",      0.0f, NULL, NULL, NULL },
    { "h4",       TAGF_HEADING,                  1.00f, "1.33em",   "1.33em",   0.0f, NULL, NULL, NULL },
    { "h5",       TAGF_HEADING,                  0.83f, "1.33em",   "1.33em",   0.0f, NULL, NULL, NULL },
    { "h6",       TAGF_HEADING,                  0.67f, "1.33em",   "1.33em",   0.0f, NULL, NULL, NULL },
    { "p",        0,                             1.0f,  "1em",      "1em",      0.0f, NULL, NULL, NULL },

    /* inline text containers */
    { "span",     TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "a",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_a, NULL },
    { "strong",   TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "em",       TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "b",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "i",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "small",    TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "label",    TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    /* text semantics (2026-10-05 (65)): Chrome UA text-decoration /
       highlight defaults, overridable by CSS */
    { "u",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_u,       NULL },
    { "ins",      TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_u,       NULL },
    { "s",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_strike,   NULL },
    { "strike",   TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_strike,   NULL },
    { "del",      TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_strike,   NULL },
    { "mark",     TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_mark,     NULL },

    /* inline SVG (2026-10-05 (67)): the <svg> root is an ordinary box
       (width/height/viewBox attributes); shapes are void records whose
       points are baked at parse time and painted by the shared draw
       hook.  <g> is a boxless passthrough; <defs>/<title>/<desc>/
       <metadata> are ignored with their subtree, exactly like a
       browser's non-rendered SVG metadata. */
    { "svg",      0,                             1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_svg, NULL },
    { "g",        TAGF_INLINE,                   1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "rect",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "circle",   TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "ellipse",  TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "line",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "polyline", TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "polygon",  TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "path",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "text",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, tag_parse_svg_shape, NULL, tag_draw_svg_shape },
    { "defs",     TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },

    /* interactive controls (inline-block by default) */
    /* lists: <ul>/<ol> reserve 24px of marker room and keep a marker/
       counter stack; <li> resolves its marker at parse time and paints it
       with a draw hook.  blockquote = the Chrome inset (1em, 40px). */
    { "ul",       TAGF_LIST,                     1.0f,  "0.5em",     "0.5em",   0.0f, NULL, tag_setup_ul, NULL },
    { "ol",       TAGF_LIST,                     1.0f,  "0.5em",     "0.5em",   0.0f, NULL, tag_setup_ol, NULL },
    { "li",       0,                             1.0f,  NULL,        NULL,      0.0f, NULL, tag_setup_li, tag_draw_li },
    { "blockquote",0,                            1.0f,  "1em",       "1em",      0.0f, NULL, tag_setup_blockquote, NULL },

    { "button",   TAGF_INLINE_BLOCK,             1.0f,  NULL,       NULL,       8.0f, NULL, tag_setup_button, NULL },
    { "input",    TAGF_INLINE_BLOCK | TAGF_VOID, 1.0f,  NULL,       NULL,       0.0f, tag_parse_input, tag_setup_input, NULL },
    { "textarea", TAGF_INLINE_BLOCK,             1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_input, NULL },
    { "select",   TAGF_INLINE_BLOCK,             1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_select, NULL },
    { "option",   0,                             1.0f,  NULL,       NULL,       0.0f, NULL, tag_setup_option, NULL },

    /* void tags: no closing tag, no children */
    { "area",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "base",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "br",       TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "col",      TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "embed",    TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "hr",       TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "link",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "meta",     TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "param",    TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "source",   TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "track",    TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "wbr",      TAGF_VOID,                     1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },

    /* invisible containers: skip the whole subtree */
    { "script",   TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "style",    TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "head",     TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "title",    TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "noscript", TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
    { "template", TAGF_IGNORED,                  1.0f,  NULL,       NULL,       0.0f, NULL, NULL, NULL },
};

/* Look up one tag by (already lower-cased) name. NULL for unknown tags. */
const TagInfo *tag_info(const char *tag) {
    size_t i;
    if (!tag) return NULL;
    for (i = 0; i < sizeof(TAG_TABLE) / sizeof(TAG_TABLE[0]); i++)
        if (!strcmp(TAG_TABLE[i].name, tag)) return &TAG_TABLE[i];
    return NULL;
}

static int is_void_tag(const char *tag) {
    const TagInfo *t = tag_info(tag);
    return t && (t->flags & TAGF_VOID);
}

static int is_heading_tag(const char *tag) {
    const TagInfo *t = tag_info(tag);
    return t && (t->flags & TAGF_HEADING);
}

/* Fill a brand-new node with the tag's defaults from the registry: the
 * user-agent stylesheet of this tiny renderer. */
static void node_defaults(CwvRenderer *r, CwvNode *n, const char *tag) {
    const TagInfo *t = tag_info(tag);

    memset(n, 0, sizeof(*n));
    /* -1 = "never pushed": the first paint of an image must sync the
     * texture filter with the CSS image-rendering value (auto->LINEAR,
     * pixelated->POINT), even though the fields both read 0 until then.
     * Ports whose textures default to POINT (the Windows software blit)
     * would otherwise nearest-sample "auto" images forever. */
    n->image_filter_state = -1;
    snprintf(n->tag, sizeof(n->tag), "%s", tag ? tag : "");   /* (75): NULL-tag guard */
    n->color = r->body_color;
    n->color_inherited_explicit = r->body_color_explicit;
    n->font_size = BODY_FONT_SIZE * (t ? t->font_scale_em : 1.0f);
    n->background = rgba(0, 0, 0, 0);
    n->box_sizing = 1; /* CSS initial value is content-box */
    n->border_color = rgba(0, 0, 0, 255);
    n->border_style = 0;
    n->overflow_x = 0;
    n->overflow_y = 0;
    n->tabindex = 0;
    n->line_height_mul = 1.0f;   /* CSS line-height: normal */
    n->eff_opacity = 1.0f;       /* opacity accumulates down the chain */

    if (t) {
        /* Default display mode, like a browser's UA stylesheet. */
        if (t->flags & TAGF_INLINE)        n->display_mode = 1;
        else if (t->flags & TAGF_INLINE_BLOCK) n->display_mode = 2;
        else                              n->display_mode = 0;

        /* Headings are bold (size already came from font_scale_em). */
        if (t->flags & TAGF_HEADING) n->bold = 1;

        /* Small browser-like user-agent margins.  These are intentionally
           kept separate from padding: margins affect flow outside the painted
           box, while padding belongs inside the element background. */
        if (t->margin_top_em)
            n->margin_top = css_length_px(t->margin_top_em, n->font_size);
        if (t->margin_bottom_em)
            n->margin_bottom = css_length_px(t->margin_bottom_em, n->font_size);
        if (t->margin_right_px > 0.0f)
            n->margin_right = t->margin_right_px;
    } else {
        n->display_mode = 0;   /* unknown tags behave like a plain block */
    }
}

/* Create a node for `tag` that inherits inheritable styles from `parent`. */
static void node_from_parent(CwvRenderer *r, CwvNode *n,
                             const char *tag, const CwvNode *parent) {
    node_defaults(r, n, tag);
    if (parent) {
        n->overflow_x = parent->overflow_x;
        n->overflow_y = parent->overflow_y;
        /* Inherit the properties that CSS actually inherits, but keep the
           renderer's built-in heading defaults.  The old implementation
           copied the parent's font-size and bold state onto every child,
           which meant <h1>/<h2>/... became the body's normal text size. */
        n->color = parent->color;
        /* A child of an explicitly-colored element keeps that page color
           across theme changes: inherited page colors must never be
           re-tinted by the theme pass (2026-09-21 (29)). */
        n->color_inherited_explicit =
            parent->color_explicit || parent->color_inherited_explicit;
        /* text-align is inherited in CSS.  Preserve the parent's explicit
           centering state so descendants of a centered block are centered
           without making ordinary elements centered by default. */
        n->centered = parent->centered;
        /* CSS typography batch (2026-10-05 (65)): text-decoration,
         * text-transform, letter/word-spacing, line-height and
         * white-space are inherited in CSS, so bake the parent's
         * already-applied value down (the parent's rules ran before its
         * children are parsed, exactly like color).
         * Opacity is a GROUP: the accumulated product is copied so a
         * child can only ever fade FURTHER, never un-fade its ancestor;
         * visibility:hidden also inherits until something resets it.
         * paint_backdrop carries the solid color behind this node's own
         * background (the parent's background faded through its own
         * opacity) so the draw path can mix opacity without a tree
         * walk. */
        n->deco = parent->deco;
        n->text_transform = parent->text_transform;
        n->letter_spacing = parent->letter_spacing;
        n->word_spacing = parent->word_spacing;
        n->line_height_mul = parent->line_height_mul;
        n->white_space = parent->white_space;
        n->visibility_hidden = parent->visibility_hidden;
        n->eff_opacity = parent->eff_opacity;
        n->paint_backdrop = parent->has_background
            ? mix_rgba(parent->paint_backdrop, parent->background,
                       parent->eff_opacity)
            : parent->paint_backdrop;
        /* cursor is inherited in CSS (2026-09-26 (45)); like color, the
           parent's rule already ran when its children are parsed, so
           baking the value down gives every descendant the right style
           with no runtime tree walk. */
        n->cursor_style = parent->cursor_style;
        n->cursor_hot_x = parent->cursor_hot_x;
        n->cursor_hot_y = parent->cursor_hot_y;
        if (parent->cursor_img) {
            n->cursor_img = parent->cursor_img;
        } else if (parent->cursor_url[0]) {
            snprintf(n->cursor_url, sizeof(n->cursor_url), "%s",
                     parent->cursor_url);
        }
        if (!is_heading_tag(tag)) {
            n->font_size = parent->font_size;
            n->bold = parent->bold;
        }
        n->font = parent->font;
        n->font_family[0] = 0;
        if (parent->font_family[0])
            snprintf(n->font_family, sizeof(n->font_family), "%s", parent->font_family);

        /* Links are inherited by descendants for hit-testing.  An <a> may
           contain a block such as <div>, so the drawable box and all text
           inside that block must retain the anchor's destination. */
        if (parent->is_link) {
            n->is_link = 1;
            snprintf(n->href, sizeof(n->href), "%s", parent->href);
            snprintf(n->link_target, sizeof(n->link_target), "%s", parent->link_target);
        }
        if (parent->is_button) { n->is_button = 1; n->button_pressed = parent->button_pressed; }
    }
}

/* Find the end of a start/end tag without treating a quoted '>' as the
 * terminator. HTML pages commonly use comparisons in attributes, e.g.
 * onclick="if (a > b) ...". Keep this bounded by the current parse range. */
static const char *html_find_tag_end(const char *lt, const char *end) {
    const char *p = lt;
    char quote = 0;
    int after_equal = 0;
    while (p < end && *p) {
        char ch = *p++;
        if (quote) {
            if (ch == quote) quote = 0;
        } else if (ch == '=' ) {
            after_equal = 1;
        } else if ((ch == '\'' || ch == '"') && after_equal) {
            quote = ch;
            after_equal = 0;
        } else if (ch == '>') {
            return p - 1;
        } else if (!isspace((unsigned char)ch)) {
            /* Apostrophes/quotes inside an unquoted value are not delimiters. */
            after_equal = 0;
        }
    }
    return NULL;
}

/* Raw-text elements do not parse ordinary opening tags in their contents.
 * Searching only for their closing delimiter is both more correct for script
 * strings containing "<div>" and cheaper than tokenizing the entire payload. */
static int find_raw_text_close(const char *start, const char *end,
                               const char *tag, const char **close_lt,
                               const char **close_gt) {
    size_t tag_len = strlen(tag);
    const char *p = start;
    while (p < end && *p) {
        const char *lt = strchr(p, '<');
        if (!lt || lt >= end) return 0;
        if (lt + 2 + tag_len < end && lt[1] == '/') {
            size_t i;
            for (i = 0; i < tag_len; ++i)
                if (tolower((unsigned char)lt[2 + i]) !=
                    tolower((unsigned char)tag[i])) break;
            if (i == tag_len && (lt[2 + tag_len] == '>' ||
                isspace((unsigned char)lt[2 + tag_len]))) {
                const char *gt = html_find_tag_end(lt, end);
                if (!gt || gt >= end) return 0;
                *close_lt = lt;
                *close_gt = gt;
                return 1;
            }
        }
        p = lt + 1;
    }
    return 0;
}

/* Scan forward from just after an open tag to find the matching close tag,
 * counting nested same-name opens. Comments are skipped so markup-looking
 * text inside comments cannot corrupt nesting. */
static int find_matching_close(const char *open_gt, const char *end,
                               const char *tag, const char **close_lt,
                               const char **close_gt) {
    const char *p = open_gt + 1;
    int depth = 1;
    if (!strcmp(tag, "script") || !strcmp(tag, "style") ||
        !strcmp(tag, "title") || !strcmp(tag, "textarea"))
        return find_raw_text_close(p, end, tag, close_lt, close_gt);
    while (p < end && *p) {
        const char *lt = strchr(p, '<');
        if (!lt || lt >= end) break;
        if (lt + 3 < end && !strncmp(lt, "<!--", 4)) {
            const char *comment_end = strstr(lt + 4, "-->");
            p = (comment_end && comment_end < end) ? comment_end + 3 : end;
            continue;
        }
        const char *gt = html_find_tag_end(lt, end);
        if (!gt || gt >= end) break;
        if (lt[1] == '/') {
            /* a closing tag: does it close OUR tag? */
            char t[16] = {0};
            size_t n = 0;
            const char *q = lt + 2;
            while (q < gt && !isspace((unsigned char)*q) && *q != '>' && n < sizeof(t) - 1)
                t[n++] = (char)tolower((unsigned char)*q++);
            t[n] = 0;
            if (ci_eq(t, tag)) {
                depth--;
                if (depth == 0) { *close_lt = lt; *close_gt = gt; return 1; }
            }
        } else if (lt[1] != '!' && lt[1] != '?') {
            /* an opening tag: does it re-open OUR tag? (voids do not nest) */
            char t[16] = {0};
            size_t n = 0;
            const char *q = lt + 1;
            while (q < gt && !isspace((unsigned char)*q) && *q != '/' && n < sizeof(t) - 1)
                t[n++] = (char)tolower((unsigned char)*q++);
            t[n] = 0;
            if (!strcmp(t, "script") || !strcmp(t, "style") ||
                !strcmp(t, "title") || !strcmp(t, "textarea")) {
                const char *raw_lt = NULL, *raw_gt = NULL;
                if (find_raw_text_close(gt + 1, end, t, &raw_lt, &raw_gt)) {
                    p = raw_gt + 1;
                    continue;
                }
                /* An unterminated raw-text element consumes the remaining
                   range, as HTML parsing does for these simple containers. */
                return 0;
            }
            if (ci_eq(t, tag) && !is_void_tag(t)) depth++;
        }
        p = gt + 1;
    }
    return 0;
}

/* Create a text node for a run of characters between tags. `style` is the
 * containing element whose inherited styles apply (link/button state etc.). */
static void add_text_node(CwvRenderer *r, const char *tag,
                          const char *text, size_t len, const CwvNode *style) {
    if (r->node_count >= VW_MAX_NODES || !text || !len) return;
    CwvNode n;
    node_from_parent(r, &n, tag, style);
    n.depth = r->parse_depth;
    n.margin_top = 0.0f;
    n.margin_bottom = 0.0f;
    if (style && style->is_link) {
        n.is_link = 1;
        snprintf(n.href, sizeof(n.href), "%s", style->href);
        snprintf(n.link_target, sizeof(n.link_target), "%s", style->link_target);
    }
    if (style && style->is_button) { n.is_button = 1; n.button_pressed = style->button_pressed; }
    /* Text participates in the parent's inline formatting context.  It is
       not itself a block merely because its parent is a <div>/<p>/<header>.
       This is important for both browser-like inline flow and correct block
       height calculation. */
    /* Raw text inside <svg> (or <g>) is SVG <text> CONTENT, not page
       text (2026-10-06 (73)).  Fold it DIRECTLY into the most recent
       <text> shape node here, at creation time: a staged flow node would
       advance the layout cursor before a post-parse pass could hide it
       - the exact silent-layout-drift class of bug <img> suffered with
       hidden-but-flowing nodes.  Orphan runs (before any <text>) are
       dropped, like Chrome drops stray svg text. */
    if (style && (ci_eq(style->tag, "svg") || ci_eq(style->tag, "g"))) {
        for (int k = r->node_count - 1; k >= 0; k--) {
            /* Never fold past the CURRENT <svg>: without this guard a
               run before the first <text> of a SECOND svg would land in
               the first svg's last <text> node. */
            if (r->nodes[k].svg_shape == 0 &&
                ci_eq(r->nodes[k].tag, "svg"))
                break;
            if (r->nodes[k].svg_shape != 8) continue;
            append_text(r->nodes[k].text, sizeof(r->nodes[k].text),
                        text, len);
            trim(r->nodes[k].text);
            return;
        }
        return;   /* no <text> open: drop the run */
    }
    append_text(n.text, sizeof(n.text), text, len);
    trim(n.text);
    if (!n.text[0]) return;
    n.display_mode = 1;
    n.w = 0.0f;
    n.x = r->cursor_x;
    n.y = r->cursor_y;
    if (style && style->is_button) {
        n.display_mode = 1;
    }
    if (n.font_family[0]) n.font = font_for(r, n.font_family);
    wrap_and_add(r, &n);
}

/* Measure how wide a text run will be, in renderer pixels. Uses the loaded
 * @font-face font when present, else the Vita system PGF font. */
/* Count UTF-8 code points (letter-spacing is per CHARACTER, not per
 * byte - a 3-byte CJK rune must not be spaced three times) and ASCII
 * spaces (what word-spacing attaches to). */
static int utf8_rune_count(const char *s, int *spaces) {
    int runes = 0;
    if (spaces) *spaces = 0;
    while (s && *s) {
        unsigned char c = (unsigned char)*s;
        if (spaces && c == ' ') (*spaces)++;
        /* advance one rune: skip continuation bytes 10xxxxxx */
        s++;
        while (s && *s && (((unsigned char)*s) & 0xC0) == 0x80) s++;
        runes++;
    }
    return runes;
}

float measure_text_width(CwvRenderer *r, const CwvNode *n,
                                const char *text) {
    if (!text || !*text) return 0.0f;
    float w;
    if (n->font) {
        unsigned sz = (unsigned)(n->font_size * 32.0f);
        if (sz < 1) sz = 1;
        w = (float)cwv_font_text_width(n->font, sz, text);
    } else if (r->system_pgf) {
        w = (float)cwv_pgf_text_width(r->system_pgf, n->font_size, text);
    } else {
        w = (float)strlen(text) * (n->font_size * 12.0f);
    }
    /* CSS letter-spacing/word-spacing (2026-10-05 (65)): spacing is added
       BETWEEN characters and AFTER spaces, so a run of N runes gains
       (N-1) letter gaps and one word gap per space.  Both default 0,
       so this block is a pure read when the page uses neither. */
    if (n->letter_spacing != 0.0f || n->word_spacing != 0.0f) {
        int spaces = 0;
        int runes = utf8_rune_count(text, &spaces);
        w += n->letter_spacing * (float)(runes > 0 ? runes - 1 : 0);
        w += n->word_spacing * (float)spaces;
    }
    return w;
}

/* The height of one line of text at this node's font size. */
float line_height(const CwvNode *n) {
    /* Keep a little leading around the glyphs, like the browser's normal
       line box.  The baseline is positioned separately in draw_node(); it
       must not sit on the very bottom edge of the line box.
       CSS line-height (2026-10-05 (65)) scales the whole line box by the
       baked-down factor (1.0 = normal), so wrapping, node heights and
       the baseline all follow it consistently. */
    return n->font_size * 28.0f * n->line_height_mul;
}

/* ============================================================================
 * SECTION 8: HTML PARSING AND LAYOUT
 * ============================================================================
 * parse_html_range() is the heart of the renderer. It walks the HTML text
 * between `start` and `end`, one tag at a time:
 *
 *   - text between tags becomes inline text nodes (wrap_and_add)
 *   - <html>/<body>/<img> are handled by their full-parse hooks
 *   - every other element becomes a node with inherited styles; its setup
 *     hook (from TAG_TABLE) customizes it; its children are then parsed
 *     recursively between its open and matching close tag
 *
 * "Layout" happens along the way: the renderer keeps a text cursor
 * (r->cursor_x / r->cursor_y) and advances it like a typewriter - inline
 * content moves along the line, block content starts a new line. That is
 * why nodes are already positioned when parsing finishes. The three
 * layout_*() helpers below implement the CSS display modes block /
 * inline-block / inline.
 * ==========================================================================*/

/* ---- full-parse hooks: tags with a custom flow ------------------------- */

/* <html> attributes configure the whole renderer (control mode, button wait
 * policy, page overflow) and style the document root for inheritance. */
static void tag_parse_html(TagCtx *c) {
    CwvRenderer *r = c->r;
    const char *q = c->q, *gt = c->gt, *end = c->end, *tag = c->tag;

    CwvNode html_node;
    node_from_parent(r, &html_node, "html", c->parent);
    char html_style[2048], html_id[64] = {0}, html_cls[128] = {0};
    char control_mode[32] = {0}, button_wait[32] = {0};
    if (find_attr(q, gt, "style", html_style, sizeof(html_style)))
        style_apply(&html_node, html_style);
    find_attr(q, gt, "id", html_id, sizeof(html_id));
    find_attr(q, gt, "class", html_cls, sizeof(html_cls));
    if (find_attr(q, gt, "cwv-button-wait", button_wait, sizeof(button_wait))) {
        trim(button_wait);
        snprintf(r->button_wait, sizeof(r->button_wait), "%s", button_wait);
    } else {
        r->button_wait[0] = 0;
    }
    if (find_attr(q, gt, "cwv-control-mode", control_mode, sizeof(control_mode))) {
        trim(control_mode);
        if (ci_eq(control_mode, "browser"))       r->control_mode = 1;
        else if (ci_eq(control_mode, "standalone")) r->control_mode = 2;
        else if (ci_eq(control_mode, "touch"))      r->control_mode = 3;
        else if (ci_eq(control_mode, "cursor"))     r->control_mode = 4;
        else                                       r->control_mode = 0;
    } else {
        r->control_mode = 0;
    }
    apply_rules(r, &html_node, html_id, html_cls, 0);
    r->overflow_x = html_node.overflow_x;
    r->overflow_y = html_node.overflow_y;

    const char *clt = NULL, *cgt = NULL;
    if (find_matching_close(gt, end, tag, &clt, &cgt)) {
        parse_html_range(r, gt + 1, clt, &html_node);
        c->next = cgt + 1;
    } else {
        parse_html_range(r, gt + 1, end, &html_node);
        c->next = end;
    }
}

/* <body> sets the page-wide colors/overflow and is the style parent of all
 * visible content. */
static void tag_parse_body(TagCtx *c) {
    CwvRenderer *r = c->r;
    const char *q = c->q, *gt = c->gt, *end = c->end, *tag = c->tag;

    CwvNode body;
    node_from_parent(r, &body, "body", c->parent);
    char style[2048], id[64] = {0}, cls[128] = {0};
    if (find_attr(q, gt, "style", style, sizeof(style))) style_apply(&body, style);
    find_attr(q, gt, "id", id, sizeof(id));
    find_attr(q, gt, "class", cls, sizeof(cls));
    apply_rules(r, &body, id, cls, 0);
    r->body_color = body.color;
    r->body_color_explicit = body.color_explicit;
    r->overflow_x = body.overflow_x;
    r->overflow_y = body.overflow_y;
    r->body_background_explicit = body.has_background;
    if (body.has_background) r->body_background = body.background;
    /* The opacity compositing root: everything inside <body> mixes
       against the solid body background (faded through the body's own
       opacity, which node_from_parent bakes into the children). */
    body.paint_backdrop = r->body_background;
    /* body.eff_opacity keeps the ACCUMULATED product (node_defaults set
       1.0, a body opacity rule multiplied it) so children fade too. */

    /* Chrome honors <body> margin + padding as the page inset.  Clamp the
       horizontal inset to the 12px Vita safe area (rounded screen edges);
       the top offset is applied as written.  <body> is the first thing
       laid out, so this is also where the flow origin is set. */
    r->page_left = body.margin_left + body.padding_left;
    if (r->page_left < 12.0f) r->page_left = 12.0f;
    r->page_right = body.margin_right + body.padding_right;
    if (r->page_right < 12.0f) r->page_right = 12.0f;
    r->page_top = body.margin_top + body.padding_top;
    r->cursor_x = r->page_left;
    r->cursor_y = r->page_top;

    const char *clt = NULL, *cgt = NULL;
    if (find_matching_close(gt, end, tag, &clt, &cgt)) {
        parse_html_range(r, gt + 1, clt, &body);
        c->next = cgt + 1;
    } else {
        parse_html_range(r, gt + 1, end, &body);
        c->next = end;
    }
}

/* <img> is a leaf node: fixed default size, overridable by width/height
 * attributes or CSS; it never parses children. */
static void tag_parse_img(TagCtx *c) {
    CwvRenderer *r = c->r;
    const char *q = c->q, *gt = c->gt;

    char src[1024] = {0};
    if (!find_attr(q, gt, "src", src, sizeof(src)) || !src[0]) {
        /* The one FULLY silent image failure: no src means no load is
           ever attempted, so no diagnostic can fire anywhere.  Chrome
           renders nothing but still tells DevTools - so do we. */
        js_console_add_line(r, "error", "image: <img> without a usable src");
        if (r->runtime_log)
            r->runtime_log("IMG: <img> without a usable src");
        c->next = gt + 1;
        return;
    }
    if (r->node_count < VW_MAX_NODES) {
        CwvNode n;
        node_from_parent(r, &n, "img", c->parent);
        n.is_image = 1;
        snprintf(n.src, sizeof(n.src), "%s", src);
        char u[1024];
        resolve_url(r->base_url, src, u, sizeof(u));
        n.image = load_image(r, u);
        char alt[256] = {0};
        if (find_attr(q, gt, "alt", alt, sizeof(alt)))
            snprintf(n.alt, sizeof(n.alt), "%s", alt);
        /* -1 = "no size given yet": width/height attributes, then CSS, then
           the image's own aspect decide the final box (see below). */
        n.w = -1.0f;
        n.h = -1.0f;
        char w[64], h[64];
        if (find_attr(q, gt, "width", w, sizeof(w)))   n.w = (float)atof(w);
        if (find_attr(q, gt, "height", h, sizeof(h))) n.h = (float)atof(h);
        char style[2048], id[64] = {0}, cls[128] = {0};
        if (find_attr(q, gt, "style", style, sizeof(style))) style_apply(&n, style);
        find_attr(q, gt, "id", id, sizeof(id));
        find_attr(q, gt, "class", cls, sizeof(cls));
        apply_rules(r, &n, id, cls, 0);
        /* Chrome sizing: an image with no width/height at all uses its
           INTRINSIC (real pixel) size; with only one side given the other
           side is derived so the aspect ratio survives. */
        float tw = 0.0f, th = 0.0f;
        if (n.image) {
            tw = (float)cwv_tex_width(n.image);
            th = (float)cwv_tex_height(n.image);
        }
        if (tw > 0.5f && th > 0.5f) {
            if (n.w < 0 && n.h >= 0)      n.w = n.h * tw / th;
            else if (n.h < 0 && n.w >= 0) n.h = n.w * th / tw;
            else if (n.w < 0 && n.h < 0) { n.w = tw; n.h = th; }
        } else {
            /* Broken image (could not load/decode): a modest placeholder.
               With alt text it is text-box shaped, without it a small
               tile, like the browser's broken-image box. */
            if (n.w < 0) n.w = n.alt[0] ? 220.0f : 48.0f;
            if (n.h < 0) n.h = n.alt[0] ? 96.0f  : 48.0f;
        }
        /* Chrome never lets an image overflow the content area: an
           oversized image shrinks to the content width, keeping its ratio.
           The content width is the flow width (viewport minus the page
           margins) minus the image's own padding.  The old code computed
           page_right - page_left, which is "distance from the right
           margin" minus "distance from the left margin" ~= 0, so every
           oversized image silently clamped to the 320px fallback instead
           of the real 900+px flow (found by the full <img> audit,
           2026-10-05 (66)). */
        float content_w = (float)r->width - r->page_left - r->page_right
                        - n.padding_left - n.padding_right;
        if (content_w < 1.0f) content_w = 320.0f;
        /* CSS width:<percent> on an image is resolved against the same
           content width, exactly like the block flow (Chrome: an
           icon at 50% fills half the column). */
        if (n.w_pct > 0.0f) {
            n.w = content_w * n.w_pct / 100.0f;
            n.w_pct = 0.0f;
        }
        if (n.w > content_w) {
            if (n.h > 0.0f) n.h = n.h * content_w / n.w;
            n.w = content_w;
        }
        if (n.w < 1.0f) n.w = 1.0f;
        if (n.h < 1.0f) n.h = 1.0f;
        /* An image is a keyboard stop ONLY with an explicit tabindex
           (Chrome ignores an img without one), so default it to -1. */
        n.tabindex = -1;
        char tab[32] = {0};
        if (find_attr(q, gt, "tabindex", tab, sizeof(tab))) {
            n.tabindex = atoi(tab);
            n.focusable_self = (n.tabindex >= 0);
        }
        n.x = r->page_left + n.padding_left;
        n.y = r->cursor_y + n.margin_top;
        n.depth = r->parse_depth;
        r->nodes[r->node_count++] = n;
        r->cursor_y = n.y + n.h + n.margin_bottom;
    }
    c->next = gt + 1;
}

/* ---- setup hooks: customize the node before its children parse --------- */

/* <a href="..." cwv-target="..."> becomes a clickable link. */
static void tag_setup_a(TagCtx *c, CwvNode *n) {
    char href[1024] = {0}, target[32] = {0};
    n->is_link = 1;
    n->focusable_self = 1;
    if (find_attr(c->q, c->gt, "href", href, sizeof(href)))
        snprintf(n->href, sizeof(n->href), "%s", href);
    if (find_attr(c->q, c->gt, "cwv-target", target, sizeof(target)))
        snprintf(n->link_target, sizeof(n->link_target), "%s", target);
    else
        snprintf(n->link_target, sizeof(n->link_target), "app");
}

static void tag_setup_select(TagCtx *c, CwvNode *n) {
    CwvRenderer *r = c->r;
    (void)c;
    n->is_select = 1;
    n->focusable_self = 1;
    n->centered = 1;
    if (!n->display_explicit) n->display_mode = 2;
    if (!n->has_background) {
        n->background = r->theme_dark ? rgba(0, 0, 0, 255) : rgba(245, 245, 245, 255);
        n->has_background = 1;
    }
    if (!n->has_border) {
        n->border_color = rgba(128, 128, 128, 255);
        n->border_width = 1.0f;
        n->border_style = 1;
        n->has_border = 1;
    }
    if (n->padding_left <= 0)   n->padding_left = 9.0f;
    if (n->padding_right <= 0)  n->padding_right = 28.0f;
    if (n->padding_top <= 0)    n->padding_top = 5.0f;
    if (n->padding_bottom <= 0) n->padding_bottom = 5.0f;
}

/* <option> hides inside the picker; only its text label is harvested. */
static void tag_setup_option(TagCtx *c, CwvNode *n) {
    char selected[16] = {0};
    n->is_option = 1;
    /* Boolean HTML attributes such as selected may appear with or
       without a value.  Presence is what matters. */
    if (find_attr(c->q, c->gt, "selected", selected, sizeof(selected)))
        n->option_selected = 1;
}

/* <button> gets native control chrome and shares a "group" id with its
 * label text nodes so they press/highlight together. */
static void tag_setup_button(TagCtx *c, CwvNode *n) {
    CwvRenderer *r = c->r;
    (void)c;
    n->is_button = 1;
    n->focusable_self = 1;
    if (!n->display_explicit) n->display_mode = 2;
    n->centered = 1;
    n->button_group = r->node_count + 1;
    /* Native HTML button defaults: black text on a light gray control. */
    if (!n->color_explicit) n->color = rgba(0, 0, 0, 255);
    if (!n->has_background) {
        n->background = rgba(224, 224, 224, 255);
        n->has_background = 1;
    }
    if (!n->has_border) {
        n->border_color = rgba(128, 128, 128, 255);
        n->border_width = 1.0f;
        n->border_style = 1;
        n->has_border = 1;
    }
    /* Chromium's compact native button defaults use 2px vertical and 6px
       horizontal padding with a 1px border.  Keep the inset symmetrical;
       explicit CSS padding still wins because these are defaults only. */
    if (n->padding_left <= 0)   n->padding_left = 6.0f;
    if (n->padding_right <= 0)  n->padding_right = 6.0f;
    if (n->padding_top <= 0)    n->padding_top = 2.0f;
    if (n->padding_bottom <= 0) n->padding_bottom = 2.0f;
}

/* <input> is a VOID element: no close tag, no children.  The generic
 * branch parses an element's children and waits for its close tag, so it
 * cannot host <input>; like <img>, this tag gets its own parse path.
 * It builds the node, applies the textfield chrome via tag_setup_input(),
 * then places the control in the flow itself - inline-block next to the
 * surrounding content, or on its own line when CSS makes it display:block.
 */
static void tag_parse_input(TagCtx *c) {
    CwvRenderer *r = c->r;
    const char *q = c->q, *gt = c->gt;
    c->next = gt + 1;
    if (r->node_count >= VW_MAX_NODES) return;

    CwvNode n;
    node_from_parent(r, &n, "input", c->parent);
    n.depth = r->parse_depth;
    char style[2048], id[64] = {0}, cls[128] = {0}, vcm[24] = {0}, control_modal[96] = {0};
    if (find_attr(q, gt, "style", style, sizeof(style))) style_apply(&n, style);
    find_attr(q, gt, "id", id, sizeof(id));
    find_attr(q, gt, "class", cls, sizeof(cls));
    snprintf(n.id, sizeof(n.id), "%s", id);
    snprintf(n.cls, sizeof(n.cls), "%s", cls);
    {
        char tabindex_value[32] = {0}, disabled_value[16] = {0};
        if (find_attr(q, gt, "tabindex", tabindex_value, sizeof(tabindex_value)))
            n.tabindex = atoi(tabindex_value);
        if (find_attr(q, gt, "disabled", disabled_value, sizeof(disabled_value)))
            n.disabled = 1;
    }
    if (find_attr(q, gt, "vcm", vcm, sizeof(vcm))) {
        trim(vcm);
        snprintf(n.vcm, sizeof(n.vcm), "%s", vcm);
    }
    if (find_attr(q, gt, "cwv-control-modal", control_modal, sizeof(control_modal))) {
        trim(control_modal);
        n.control_modal = 1;
        snprintf(n.control_modal_title, sizeof(n.control_modal_title), "%s", control_modal);
    }
    apply_rules(r, &n, id, cls, 0);
    if (n.font_family[0]) n.font = font_for(r, n.font_family);

    /* textfield chrome: is_textfield, value/placeholder, native colors */
    TagCtx sc;
    sc.r = r; sc.tag = "input"; sc.q = q; sc.gt = gt;
    sc.end = c->end; sc.parent = c->parent; sc.next = NULL;
    tag_setup_input(&sc, &n);
    if (n.display_none) return;   /* type="hidden" */

    /* Chrome's default text input width (size=20 chars). */
    if (n.w <= 0.0f) n.w = 173.0f;

    int box_index = -1;
    if (n.display_mode == 0) {
        /* display:block input: own line, like a block box.  (CSS margin
           collapsing against the neighbours is approximated: the margins
           simply add, which is what a page of stacked form rows shows.) */
        if (r->cursor_x > r->page_left && r->line_height_current > 0.0f)
            r->cursor_y += r->line_height_current;
        r->cursor_x = r->page_left;
        r->line_height_current = 0.0f;
        float y = r->cursor_y + n.margin_top;
        float h = line_height(&n) + n.padding_top + n.padding_bottom
                  + n.border_width * 2.0f;
        if (n.h > 0.0f) h = n.h;
        n.x = r->page_left;
        n.y = y;
        n.h = h;
        box_index = r->node_count++;
        r->nodes[box_index] = n;
        r->nodes[box_index].is_box = 1;
        r->nodes[box_index].text[0] = 0;
        r->cursor_y = y + h + n.margin_bottom;
        r->cursor_x = r->page_left;
        return;
    }

    /* default: inline-block control on the current line */
    float flow_x = r->cursor_x, flow_y = r->cursor_y, flow_lh = r->line_height_current;
    box_index = r->node_count++;
    r->nodes[box_index] = n;
    r->nodes[box_index].is_box = 1;
    r->nodes[box_index].x = r->page_left;
    r->nodes[box_index].y = flow_y;
    r->nodes[box_index].w = n.w;
    r->nodes[box_index].h = 0.0f;
    r->nodes[box_index].text[0] = 0;
    /* child_start == node_count: an input has no children, so the
       shrink-to-fit pass below sees only the control itself. */
    layout_inline_block(r, &n, r->node_count, flow_y, box_index,
                        flow_x, flow_y, flow_lh);
}

/* <input> is the standard HTML text input.  All of the spec's single-line
 * types (text, password, search, email, number, tel, url, ...) render the
 * same single-line field on a real device (the Vita IME dialog), so every
 * type except "hidden" becomes a text field; type="password" masks it.
 * No custom <textfield> element - standard tags only. */
static void tag_setup_input(TagCtx *c, CwvNode *n) {
    CwvRenderer *r = c->r;
    char type[32] = {0};
    char value[VW_MAX_TEXT] = {0};
    char placeholder[256] = {0};
    n->focusable_self = 1;
    if (!n->display_explicit) n->display_mode = 2;
    if (find_attr(c->q, c->gt, "type", type, sizeof(type)) && ci_eq(type, "hidden")) {
        n->display_none = 1;
        return;
    }
    n->is_textfield = 1;
    if (type[0] && ci_eq(type, "password")) n->textfield_password = 1;
    if (find_attr(c->q, c->gt, "value", value, sizeof(value)))
        snprintf(n->text, sizeof(n->text), "%s", value);
    if (find_attr(c->q, c->gt, "placeholder", placeholder, sizeof(placeholder)))
        snprintf(n->placeholder, sizeof(n->placeholder), "%s", placeholder);
    /* Native control chrome unless CSS overrode it. */
    if (!n->has_background) {
        n->background = r->theme_dark ? rgba(28, 30, 34, 255) : rgba(255, 255, 255, 255);
        n->has_background = 1;
    }
    if (!n->has_border) {
        n->border_color = rgba(128, 128, 128, 255);
        n->border_width = 1.0f;
        n->border_style = 1;
        n->has_border = 1;
    }
    if (n->padding_left <= 0)   n->padding_left = 9.0f;
    if (n->padding_right <= 0)  n->padding_right = 9.0f;
    if (n->padding_top <= 0)    n->padding_top = 5.0f;
    if (n->padding_bottom <= 0) n->padding_bottom = 5.0f;
}

/* ---- Lists (<ul>, <ol>, <li>) ----------------------------------------
 * Parsing keeps a tiny stack of open lists so <ol> numbers its items and
 * nested lists get their own counters.  The stack lives at file scope
 * (parsing is single-pass and only one document parses at a time) and is
 * reset at the start of every parse_html(). */
static int g_list_style[8];   /* marker kind per open list (values: see
                                 CwvNode.list_style) */
static int g_list_count[8];   /* items seen so far in each open <ol> */
static int g_list_depth;

/* ---- Text semantics (2026-10-05 (65)) ---------------------------------
 * The HTML elements whose Chrome default is a text-decoration or a
 * highlight: <u>/<ins> underline, <s>/<strike>/<del> line-through, and
 * <mark> with the UA yellow.  They ride on the CSS text-decoration
 * machinery, so CSS can still override them (a { text-decoration: none }
 * on an <ins> wins, like in a browser). */
static void tag_setup_u(TagCtx *ctx, CwvNode *n) {
    (void)ctx;
    n->deco |= 1;
}

static void tag_setup_strike(TagCtx *ctx, CwvNode *n) {
    (void)ctx;
    n->deco |= 2;
}

static void tag_setup_mark(TagCtx *ctx, CwvNode *n) {
    (void)ctx;
    /* Chrome's UA sheet: mark { background-color: yellow }.  Page CSS
       can still override it (has_background is only set when CSS did
       not). */
    if (!n->has_background) {
        n->background = rgba(255, 255, 0, 255);
        n->has_background = 1;
    }
}

static void tag_setup_list_container(TagCtx *ctx, CwvNode *n, int is_ol) {
    (void)ctx;
    /* 24px of marker room.  Because block containers narrow the content
       flow by their padding, each NESTED list indents another level,
       exactly like the browser's nested-list inset. */
    if (n->padding_left < 24.0f) n->padding_left = 24.0f;
    if (g_list_depth < 8) {
        /* CSS list-style-type on the <ul>/<ol> wins; else the tag default:
           disc bullets for <ul>, decimal numbering for <ol>. */
        g_list_style[g_list_depth] = n->list_style ? n->list_style
                                                    : (is_ol ? 4 : 1);
        g_list_count[g_list_depth] = 0;
        g_list_depth++;
    }
}

static void tag_setup_ul(TagCtx *ctx, CwvNode *n) {
    tag_setup_list_container(ctx, n, 0);
}

static void tag_setup_ol(TagCtx *ctx, CwvNode *n) {
    tag_setup_list_container(ctx, n, 1);
}

static void tag_setup_li(TagCtx *ctx, CwvNode *n) {
    (void)ctx;   /* li setup needs no tag context (kept for table parity) */
    /* The item takes its marker from the innermost open list; an inline
       list-style-type on the <li> itself overrides it (CSS on the item
       beats the list's choice, like Chrome). */
    if (n->list_style)
        n->list_marker = n->list_style;
    else if (g_list_depth > 0)
        n->list_marker = g_list_style[g_list_depth - 1];
    if (n->list_marker == 4 && g_list_depth > 0)
        n->list_number = ++g_list_count[g_list_depth - 1];
}

static void tag_setup_blockquote(TagCtx *ctx, CwvNode *n) {
    (void)ctx;
    /* Chrome's user-agent style: blockquote { margin: 1em 40px } - we do
       the 40px left inset as padding so the text visibly indents (the
       engine narrows the flow by a block's padding). */
    if (n->padding_left < 40.0f) n->padding_left = 40.0f;
}

/* Paint one <li> marker: bullets live in the list's 24px gutter, just
 * left of the item text, centered on the first text line.  Numbers are
 * right-aligned so multi-digit lists keep a clean edge. */
static void tag_draw_li(TagDrawCtx *ctx) {
    CwvRenderer *r = ctx->r;
    CwvNode *n = ctx->n;
    if (!n->list_marker || n->list_marker == 9) return;  /* unset / none */
    float mx = ctx->sx + 8.0f * ctx->z;
    float my = ctx->baseline - 3.0f * ctx->z;
    if (n->list_marker == 4) {                    /* decimal: "N." */
        char num[12];
        snprintf(num, sizeof(num), "%d.", n->list_number);
        float w = cwv_pgf_text_width(r->system_pgf, 0.75f, num);
        cwv_pgf_draw_text(r->system_pgf, mx + 8.0f * ctx->z - w, ctx->baseline,
                             ctx->text_color, 0.75f, num);
    } else if (n->list_marker == 3) {             /* square */
        cwv_draw_rectangle(mx - 2.0f * ctx->z, my - 2.0f * ctx->z,
                              4.0f * ctx->z, 4.0f * ctx->z, ctx->text_color);
    } else {                                      /* disc / circle */
        cwv_draw_fill_circle(mx, my,
            (n->list_marker == 2 ? 2.2f : 3.4f) * ctx->z, ctx->text_color);
    }
}


/* ---- inline SVG painting (2026-10-05 (67)) -----------------------------
 * Portable primitives only: filled rects, scanline spans (drawn as
 * 1px rects) and the platform's fill_circle.  Everything is baked into
 * r->svg_pts at parse time, so the per-frame cost is a small walk -
 * icon-sized shapes stay cheap even on the Wii. */
static void svg_paint_line(float x1, float y1, float x2, float y2,
                           unsigned color, float w) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = dx * dx + dy * dy;
    int steps, i;
    float thick = w < 1.0f ? 1.0f : w;
    if (len < 0.01f) return;
    len = sqrtf(len);
    steps = (int)(len + 0.5f);
    if (steps < 1) steps = 1;
    if (steps > 4096) steps = 4096;
    for (i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        cwv_draw_rectangle(x1 + dx * t - thick * 0.5f,
                           y1 + dy * t - thick * 0.5f,
                           thick, thick, color);
    }
}

/* even-odd scanline fill of a point list (screen space, zoom baked in) */
static void svg_fill_polygon(const float *p, int npts, unsigned color) {
    int i, y;
    float miny = 1e9f, maxy = -1e9f;
    float xs[VW_SVG_MAX_PTS / 2];
    if (npts < 3) return;
    for (i = 0; i < npts; i++) {
        if (p[i * 2 + 1] < miny) miny = p[i * 2 + 1];
        if (p[i * 2 + 1] > maxy) maxy = p[i * 2 + 1];
    }
    for (y = (int)miny; y <= (int)maxy; y++) {
        int cnt = 0;
        float sy = (float)y + 0.5f;
        for (i = 0; i < npts; i++) {
            int j = (i + 1) % npts;
            float y0 = p[i * 2 + 1], y1v = p[j * 2 + 1];
            if ((y0 <= sy && y1v > sy) || (y1v <= sy && y0 > sy)) {
                float t = (sy - y0) / (y1v - y0);
                xs[cnt++] = p[i * 2] + (p[j * 2] - p[i * 2]) * t;
            }
        }
        /* insertion sort: cnt is tiny (icon edges) */
        for (i = 1; i < cnt; i++) {
            float v = xs[i];
            int k = i;
            while (k > 0 && xs[k - 1] > v) { xs[k] = xs[k - 1]; k--; }
            xs[k] = v;
        }
        for (i = 0; i + 1 < cnt; i += 2) {
            float w = xs[i + 1] - xs[i];
            if (w > 0.05f)
                cwv_draw_rectangle(xs[i], (float)y, w, 1.0f, color);
        }
    }
}

static void tag_draw_svg_shape(TagDrawCtx *ctx) {
    CwvRenderer *r = ctx->r;
    CwvNode *n = ctx->n;
    float z = ctx->z;
    float ox = ctx->sx, oy = ctx->sy;      /* the <svg> content origin */
    const float *p = r->svg_pts + n->svg_pt_off;
    float sw = n->svg_sw * z;
    int i, npts = n->svg_pt_n;
    if (!n->svg_shape || npts <= 0) return;

    switch (n->svg_shape) {
    case 1: {   /* rect: two baked corners */
        float x0 = ox + p[0] * z, y0 = oy + p[1] * z;
        float x1 = ox + p[2] * z, y1 = oy + p[3] * z;
        float w = x1 - x0, h = y1 - y0;
        if (n->svg_fill_on && w > 0.05f && h > 0.05f)
            cwv_draw_rectangle(x0, y0, w, h, n->svg_fill);
        if (n->svg_stroke_on && sw > 0.0f) {
            svg_paint_line(x0, y0, x1, y0, n->svg_stroke, sw);
            svg_paint_line(x1, y0, x1, y1, n->svg_stroke, sw);
            svg_paint_line(x1, y1, x0, y1, n->svg_stroke, sw);
            svg_paint_line(x0, y1, x0, y0, n->svg_stroke, sw);
        }
        break;
    }
    case 2:     /* circle: center + radius pair */
    case 3: {   /* ellipse: center + rx, ry */
        float cx = ox + p[0] * z, cy = oy + p[1] * z;
        float rx = p[2] * z, ry = (n->svg_shape == 2 ? p[2] : p[3]) * z;
        if (n->svg_fill_on && rx > 0.05f && ry > 0.05f) {
            if (n->svg_shape == 2)
                cwv_draw_fill_circle(cx, cy, rx, n->svg_fill);
            else {
                /* scanline ellipse: spans of 1px rects */
                int y;
                for (y = 0; y < (int)(ry * 2.0f); y++) {
                    float dy = (float)y - ry + 0.5f;
                    float t = 1.0f - (dy * dy) / (ry * ry);
                    float dx;
                    if (t <= 0.0f) continue;
                    dx = rx * sqrtf(t);
                    cwv_draw_rectangle(cx - dx, cy - ry + (float)y,
                                      dx * 2.0f, 1.0f, n->svg_fill);
                }
            }
        }
        if (n->svg_stroke_on && sw > 0.0f) {
            int steps = 32, i2;
            float px = 0.0f, py = 0.0f;
            for (i2 = 0; i2 <= steps; i2++) {
                float a = (float)i2 / (float)steps * 6.283185f;
                float ex = cx + rx * cosf(a), ey = cy + ry * sinf(a);
                if (i2 > 0) svg_paint_line(px, py, ex, ey, n->svg_stroke, sw);
                px = ex; py = ey;
            }
        }
        break;
    }
    case 8: {   /* text: one baked baseline point + harvested content */
        /* The SVG y attribute is the BASELINE, which is exactly what the
           font draw ops take, so no ascent correction is needed. */
        if (n->text[0]) {
            float tx = ox + p[0] * z, ty = oy + p[1] * z;
            if (n->font)
                cwv_font_draw_text(n->font, tx, ty, n->svg_fill,
                                   (unsigned)(n->font_size * 32.0f * z),
                                   n->text);
            else if (r->system_pgf)
                cwv_pgf_draw_text(r->system_pgf, tx, ty, n->svg_fill,
                                  n->font_size * z, n->text);
        }
        break;
    }
    default: {  /* line / polyline / polygon / path: point list */
        /* bake screen points into a local scratch (icon-sized by design) */
        float sp[VW_SVG_MAX_PTS];
        if (npts * 2 > VW_SVG_MAX_PTS) npts = VW_SVG_MAX_PTS / 2;
        for (i = 0; i < npts; i++) {
            sp[i * 2]     = ox + p[i * 2] * z;
            sp[i * 2 + 1] = oy + p[i * 2 + 1] * z;
        }
        if (n->svg_fill_on && n->svg_closed && npts >= 3)
            svg_fill_polygon(sp, npts, n->svg_fill);
        if (n->svg_stroke_on && sw > 0.0f)
            for (i = 0; i + 1 < npts; i++)
                svg_paint_line(sp[i * 2], sp[i * 2 + 1],
                               sp[i * 2 + 2], sp[i * 2 + 3],
                               n->svg_stroke, sw);
        break;
    }
    }
}

/* ---- post-parse: harvest <option> labels into the <select> ------------- */

/* After a <select>'s children are parsed, walk the option nodes: copy each
 * option's first text child into the option itself (the popup label), then
 * hide the whole option subtree from the normal paint pass.  The closed
 * control's label becomes the chosen option's text. */
static void select_harvest(CwvRenderer *r, CwvNode *n,
                           int child_start, int box_index) {
    int chosen = -1, first = -1, option_count = 0;
    for (int i = child_start; i < r->node_count; i++) {
        CwvNode *child = &r->nodes[i];
        if (!child->is_option) continue;
        if (first < 0) first = i;
        if (child->option_selected) chosen = i;
        option_count++;

        for (int j = i + 1; j < r->node_count; j++) {
            CwvNode *label = &r->nodes[j];
            if (label->is_option) break;
            if (label->text[0] && !child->text[0])
                snprintf(child->text, sizeof(child->text), "%s", label->text);
            label->display_none = 1;
        }
        child->display_none = 1;
    }
    if (chosen < 0) chosen = first;
    n->select_index = 0;
    n->select_hover_index = 0;
    if (chosen >= 0) {
        int idx = 0;
        for (int i = child_start; i < r->node_count; i++) {
            if (r->nodes[i].is_option) {
                if (i == chosen) { n->select_index = idx; n->select_hover_index = idx; }
                idx++;
            }
        }
        if (box_index >= 0 && r->focus_node == box_index)
            r->select_focus_node = box_index;
        if (r->nodes[chosen].text[0])
            snprintf(n->text, sizeof(n->text), "%s", r->nodes[chosen].text);
        if (!n->text[0] && first >= 0)
            snprintf(n->text, sizeof(n->text), "%s", r->nodes[first].text);
        if (box_index >= 0)
            snprintf(r->nodes[box_index].text, sizeof(r->nodes[box_index].text), "%s", n->text);
    }
    (void)option_count;
}

/* ---- layout: the three CSS display modes ------------------------------- */

/* Inline-block controls (especially <button>) participate in the current
 * line. They are shrink-to-fit unless CSS supplied a width.  Children were
 * parsed only to determine the intrinsic size. */
static void layout_inline_block(CwvRenderer *r, CwvNode *n, int child_start,
                                float box_y, int box_index,
                                float saved_x, float saved_y, float flow_lh) {
    /* Parsing the children may advance cursor_x, so restore the line origin
       while measuring and then position the children inside the finished
       control. */
    float content_w = 0.0f, content_h = 0.0f;
    int i, last_direct = -1;
    for (i = child_start; i < r->node_count; i++) {
        CwvNode *child = &r->nodes[i];
        if (child->display_none) continue;
        if (child->depth == n->depth + 1 &&
            (child->is_box || child->display_mode == 0)) last_direct = i;
        if (child->text[0]) {
            float tw = measure_text_width(r, child, child->text);
            if (tw > content_w) content_w = tw;
        }
        /* A BLOCK child box (e.g. a <p> inside a <button>) carries the
           full available width from the block flow, not its intrinsic
           width; counting it here made controls overflow the screen.
           Its real content is measured through its text nodes, which are
           in this same range.  Images and nested inline-blocks keep their
           own measured width. */
        if (child->is_image || (child->is_box && child->display_mode != 0)) {
            if (child->w > content_w) content_w = child->w;
        }
        float bottom = (child->y + child->h) - box_y;
        if (bottom > content_h) content_h = bottom;
    }
    if (n->w <= 0) {
        /* CSS inline-block controls are shrink-to-fit.  Do not use the
           parent's available width here: that is what caused <button> to
           become a full-page-width control. */
        if (content_w < 1.0f) content_w = 32.0f;
        n->w = content_w + n->padding_left + n->padding_right + n->border_width * 2.0f;
    }
    if (n->h <= 0) {
        float natural_h = (content_h > 0.0f ? content_h : line_height(n)) +
                           n->padding_top + n->padding_bottom + n->border_width * 2.0f;
        /* Same margin-containment rule as layout_block(): when the control
           has padding or a border, the last DIRECT child's bottom margin
           is contained inside it instead of collapsing out. */
        if ((n->padding_bottom > 0.0f || n->border_width > 0.0f) && last_direct >= 0)
            natural_h += r->nodes[last_direct].margin_bottom;
        /* Inline controls align on their own compact line box.  Do not add
           another font line-height on top of measured text. */
        n->h = natural_h;
    }
    /* Wrap the control to the next line only when it no longer fits. */
    if (saved_x > r->page_left && saved_x + n->margin_left + n->w + n->margin_right >
        (float)r->width - r->page_right) {
        r->cursor_y = saved_y + (flow_lh > 0.0f ? flow_lh : 0.0f);
        r->cursor_x = r->page_left;
        r->line_height_current = 0.0f;
        box_y = r->cursor_y;
    } else {
        r->cursor_y = saved_y;
        r->cursor_x = saved_x;
    }
    n->x = r->cursor_x + n->margin_left;
    n->y = box_y;
    if (box_index >= 0) {
        r->nodes[box_index].x = n->x;
        r->nodes[box_index].y = n->y;
        r->nodes[box_index].w = n->w;
        r->nodes[box_index].h = n->h;
    }
    /* Place the parsed children inside the control's content box.
       Inline children share one line; a block child box opens a new
       line, so <button><span>Label</span><p>Detail</p></button> stacks
       two lines the way a browser does instead of painting them over
       each other. */
    float inner_x = n->x + n->padding_left + n->border_width;
    float inner_w = n->w - n->padding_left - n->padding_right - n->border_width * 2.0f;
    if (inner_w < 0.0f) inner_w = 0.0f;
    float cy = n->y + n->padding_top + n->border_width;
    float line_h = 0.0f;
    for (i = child_start; i < r->node_count; i++) {
        CwvNode *child = &r->nodes[i];
        if (child->display_none) continue;
        if (child->is_box && child->display_mode == 0) {
            cy += line_h;            /* block child: start a new line */
            line_h = 0.0f;
        }
        cy += child->margin_top;
        child->y = cy;
        if (child->is_box) {
            child->x = inner_x;
            child->w = inner_w;
        } else if (n->is_button && child->text[0]) {
            float tw = measure_text_width(r, child, child->text);
            child->x = inner_x + (inner_w - tw) * 0.5f;
            if (child->x < inner_x) child->x = inner_x;
        } else if (n->centered && child->text[0]) {
            float tw = measure_text_width(r, child, child->text);
            child->x = inner_x + (inner_w - tw) * 0.5f;
            if (child->x < inner_x) child->x = inner_x;
        } else {
            child->x = inner_x;
        }
        if (child->h > line_h) line_h = child->h;
    }
    r->cursor_x = n->x + n->w + n->margin_right;
    float control_lh = n->h + n->margin_top + n->margin_bottom;
    if (control_lh > r->line_height_current) r->line_height_current = control_lh;
}

/* Block formatting: the element's height is exactly the content extent plus
 * its own padding/border. Do NOT add a synthetic line-height here: a <div>
 * with no padding therefore ends exactly at the bottom of its last child. */
static void layout_block(CwvRenderer *r, CwvNode *n, int child_start,
                         float box_y, int box_index,
                         float *pending_block_margin_bottom,
                         int *have_pending_block_margin) {
    float content_h = 0.0f;
    int first_child = -1, last_child = -1;
    int i;
    for (i = child_start; i < r->node_count; i++) {
        CwvNode *child = &r->nodes[i];
        if (child->display_none) continue;
        /* content_h covers every descendant (grandchildren included: a
           child box's own layout has already sized it around them).  But
           for CSS margins only DIRECT children (depth + 1) count: the last
           node in flat order is often a text node or a grandchild whose
           margin belongs to an inner box, not to this one. */
        if (child->depth == n->depth + 1) {
            if (first_child < 0) first_child = i;
            /* Margin collapse-through only happens through block-level
               boxes.  Trailing INLINE runs (e.g. a <span> line after the
               last <p>) have no bottom margin and must not displace the
               <p> as "last child" - that lost its contained margin and
               shrank containers by their real margin. */
            if (child->is_box || child->display_mode == 0) last_child = i;
        }
        float bottom = (child->y + child->h) - box_y;
        if (bottom > content_h) content_h = bottom;
    }
    if (content_h < 0.0f) content_h = 0.0f;

    /* CSS margin collapse: when a block has no padding/border, its first and
       last in-flow child's vertical margins can collapse through the parent.
       The collapsed margin must not be painted as part of the parent's
       background.  Shift the border box to the first child's content
       position and trim the trailing margin from the painted height. */
    float collapsed_top = 0.0f, collapsed_bottom = 0.0f;
    if (first_child >= 0 && n->padding_top <= 0.0f && n->border_width <= 0.0f)
        collapsed_top = r->nodes[first_child].margin_top;
    if (last_child >= 0 && n->padding_bottom <= 0.0f && n->border_width <= 0.0f)
        collapsed_bottom = r->nodes[last_child].margin_bottom;
    if (collapsed_top > 0.0f) {
        box_y += collapsed_top;
        content_h -= collapsed_top;
        if (content_h < 0.0f) content_h = 0.0f;
    }
    /* A collapsed top margin is outside the painted border box.  The box
       node was allocated before child parsing, so move it now; otherwise the
       background begins at the pre-collapse position and creates the visible
       strip above <h1>/<h2>/<p> content. */
    if (box_index >= 0) r->nodes[box_index].y = box_y;

    /* Auto height of a block, the way a real browser computes it.
     *
     * content_h is measured from the box top to the deepest child bottom,
     * using the children's ABSOLUTE positions - and children are parsed
     * below (box top + top border + top padding), so content_h already
     * contains the top padding/border.  Only the bottom padding/border is
     * added on top of it.  (The previous formula added the TOP padding a
     * second time, which painted a phantom strip of background BELOW the
     * last child - the "divs look connected / too tall" bug.)
     *
     * The last child's margin-bottom: when the bottom padding or a border
     * stops it from collapsing out of the box, it stays INSIDE the parent
     * and is part of the painted height (CSS 2.1 8.3.1; this is why a
     * padded <div> in Chrome keeps the last <p>'s margin inside its
     * background).  With no bottom padding/border the margin escapes and
     * is handled by the pending-margin collapse logic instead. */
    float own_h;
    if (first_child >= 0) {
        own_h = content_h + n->padding_bottom + n->border_width;
        if ((n->padding_bottom > 0.0f || n->border_width > 0.0f) && last_child >= 0)
            own_h += r->nodes[last_child].margin_bottom;
    } else {
        /* an empty box paints just its padding + border */
        own_h = n->padding_top + n->padding_bottom + n->border_width * 2.0f;
    }
    if (n->h > 0.0f) own_h = n->h;

    /* Buttons default to inline-block and shrink-to-fit their label. When
       CSS explicitly sets display:block, however, the control follows normal
       block sizing and fills the containing content width unless width is
       specified. Keep this separate from the inline-block sizing path. */
    if (n->is_button && n->display_mode != 0 && n->w <= 0.0f) {
        float fit_w = 0.0f;
        for (i = child_start; i < r->node_count; i++) {
            CwvNode *child = &r->nodes[i];
            if (child->display_none) continue;
            if (child->text[0]) {
                float tw = measure_text_width(r, child, child->text);
                if (tw > fit_w) fit_w = tw;
            } else if (child->is_image ||
                       (child->is_box && child->display_mode != 0)) {
                if (child->w > fit_w) fit_w = child->w;
            }
        }
        if (fit_w < 1.0f) fit_w = 32.0f;
        n->w = fit_w + n->padding_left + n->padding_right + n->border_width * 2.0f;
    }
    /* The label of a block button is centered inside the button itself.
       Do not let the generic block text layout decide the label position
       against the page. */
    if (n->is_button) {
        /* Anchor the label to this button's own border box, not the page's
           left edge. Block buttons may be centered, positioned, or nested. */
        float inner_x = n->x + n->padding_left + n->border_width;
        float inner_w = (n->w > 0.0f ? n->w : (float)r->width - r->page_left - r->page_right) -
                        n->padding_left - n->padding_right - n->border_width * 2.0f;
        if (inner_w < 0.0f) inner_w = 0.0f;
        /* A button can contain several text nodes (for example a <span>
           followed by a nested <p>).  They are descendants of the same focus
           target, but they must still occupy separate line boxes instead of
           being painted at the same y-position.  Stack their boxes
           vertically and grow an auto-height button to contain them. */
        float child_flow_h = 0.0f;
        int visible_children = 0;
        for (i = child_start; i < r->node_count; i++) {
            CwvNode *child = &r->nodes[i];
            if (child->display_none) continue;
            float ch = child->h > 0.0f ? child->h : line_height(child);
            child_flow_h += child->margin_top + ch + child->margin_bottom;
            visible_children++;
        }
        if (visible_children > 0 && n->h <= 0.0f)
            own_h = child_flow_h + n->padding_top + n->padding_bottom + n->border_width * 2.0f;
        float child_y = box_y + n->padding_top + n->border_width;
        for (i = child_start; i < r->node_count; i++) {
            CwvNode *child = &r->nodes[i];
            if (child->display_none) continue;
            child_y += child->margin_top;
            child->y = child_y;
            if (child->text[0]) {
                float tw = measure_text_width(r, child, child->text);
                child->x = inner_x + (inner_w - tw) * 0.5f;
                if (child->x < inner_x) child->x = inner_x;
            } else if (child->is_box) {
                child->x = inner_x;
                child->w = inner_w;
            }
            child_y += (child->h > 0.0f ? child->h : line_height(child)) + child->margin_bottom;
        }
    }
    if (box_index >= 0) {

        r->nodes[box_index].w = (n->w > 0.0f ? n->w :
                                 (float)r->width - r->page_left - r->page_right);
        r->nodes[box_index].h = own_h;
    }
    /* Keep the trailing margin pending so a following block can collapse it
       with its own top margin.  If this is the last child in a parent, its
       margin is considered by the parent's normal last-child collapse logic
       instead of being painted. */
    *pending_block_margin_bottom = n->margin_bottom;
    if (collapsed_bottom > *pending_block_margin_bottom)
        *pending_block_margin_bottom = collapsed_bottom;
    *have_pending_block_margin = 1;
    r->cursor_y = box_y + own_h;
    r->cursor_x = r->page_left;
    r->line_height_current = 0.0f;
}

/* Inline containers do not create a new line.  Explicit display:inline on a
 * button is still an intrinsic-size control; generic inline boxes just get
 * their painted box sized to the current line. */
static void layout_inline(CwvRenderer *r, CwvNode *n, int child_start,
                          float flow_x, float flow_y, int box_index) {
    if (n->is_button) {
        /* It must not collapse to the width of its containing block or use
           the generic zero-width inline box path. */
        float content_w = 0.0f, content_h = 0.0f;
        int i;
        for (i = child_start; i < r->node_count; i++) {
            CwvNode *child = &r->nodes[i];
            if (child->display_none) continue;
            if (child->text[0]) {
                float tw = measure_text_width(r, child, child->text);
                if (tw > content_w) content_w = tw;
            }
            if (child->h > content_h) content_h = child->h;
        }
        if (n->w <= 0.0f)
            n->w = content_w + n->padding_left + n->padding_right + n->border_width * 2.0f;
        if (n->h <= 0.0f)
            n->h = (content_h > 0.0f ? content_h : line_height(n)) +
                   n->padding_top + n->padding_bottom + n->border_width * 2.0f;
        n->x = flow_x;
        n->y = flow_y;
        if (n->x > r->page_left && n->x + n->w > (float)r->width - r->page_right) {
            r->cursor_y = flow_y + (r->line_height_current > 0.0f ? r->line_height_current : 0.0f);
            r->cursor_x = r->page_left;
            n->x = r->page_left;
            n->y = r->cursor_y;
        }
        float inner_x = n->x + n->padding_left + n->border_width;
        float inner_w = n->w - n->padding_left - n->padding_right - n->border_width * 2.0f;
        if (inner_w < 0.0f) inner_w = 0.0f;
        for (i = child_start; i < r->node_count; i++) {
            CwvNode *child = &r->nodes[i];
            if (child->display_none) continue;
            child->y = n->y + n->padding_top + n->border_width + child->margin_top;
            if (child->text[0]) {
                if (n->centered) {
                    float tw = measure_text_width(r, child, child->text);
                    child->x = inner_x + (inner_w - tw) * 0.5f;
                    if (child->x < inner_x) child->x = inner_x;
                } else {
                    child->x = inner_x;
                }
            } else {
                child->x = inner_x;
            }
        }
        if (box_index >= 0) {
            r->nodes[box_index].x = n->x; r->nodes[box_index].y = n->y;
            r->nodes[box_index].w = n->w; r->nodes[box_index].h = n->h;
        }
        r->cursor_x = n->x + n->w;
        if (n->h > r->line_height_current) r->line_height_current = n->h;
    } else if (box_index >= 0) {
        /* Inline containers do not create a new line. */
        float content_h = r->line_height_current;
        if (content_h < 1.0f) content_h = n->h;
        r->nodes[box_index].h = content_h;
        r->nodes[box_index].x = r->page_left;
    }
}

/* ---- the main recursive parser ----------------------------------------- */

static void parse_html_range(CwvRenderer *r, const char *start,
                             const char *end, const CwvNode *parent) {
    const char *p = start;
    /* Vertical margins between blocks collapse; a margin left over at the
       end of this range (no following sibling) is applied once, here. */
    float pending_block_margin_bottom = 0.0f;
    int have_pending_block_margin = 0;

    while (p < end && *p && r->node_count < VW_MAX_NODES) {
        /* --- 1. any text before the next tag becomes a text node --- */
        const char *lt = strchr(p, '<');
        if (!lt || lt >= end) {
            if (p < end)
                add_text_node(r, parent ? parent->tag : "p", p, (size_t)(end - p), parent);
            break;
        }
        if (lt > p)
            add_text_node(r, parent ? parent->tag : "p", p, (size_t)(lt - p), parent);

        /* --- 2. comments and non-element tags are skipped --- */
        if (lt + 3 < end && !strncmp(lt, "<!--", 4)) {
            const char *e = strstr(lt + 4, "-->");
            p = (e && e < end) ? e + 3 : end;
            continue;
        }
        const char *gt = html_find_tag_end(lt, end);
        if (!gt || gt >= end) break;
        if (lt[1] == '/' || lt[1] == '!' || lt[1] == '?') {  /* close/doctype/PI */
            p = gt + 1;
            continue;
        }

        /* --- 3. read the tag name (lower-cased) --- */
        char tag[16] = {0};
        size_t ti = 0;
        const char *q = lt + 1;
        while (q < gt && !isspace((unsigned char)*q) && *q != '/' && ti < sizeof(tag) - 1)
            tag[ti++] = (char)tolower((unsigned char)*q++);
        tag[ti] = 0;
        if (!tag[0]) { p = gt + 1; continue; }

        /* --- 4. look the tag up in the registry --- */
        const TagInfo *info = tag_info(tag);

        /* Invisible containers (<script>, <style>, <head>...): skip the
           whole subtree so its text never reaches the screen. */
        if (info && (info->flags & TAGF_IGNORED)) {
            const char *clt = NULL, *cgt = NULL;
            if (find_matching_close(gt, end, tag, &clt, &cgt)) p = cgt + 1;
            else p = gt + 1;
            continue;
        }

        /* --- 5. tags with a fully custom parse path --- */
        if (info && info->parse_full) {
            TagCtx ctx;
            ctx.r = r;
            ctx.tag = tag;
            ctx.q = q;
            ctx.gt = gt;
            ctx.end = end;
            ctx.parent = parent;
            ctx.next = gt + 1;         /* fallback if the hook forgets */
            info->parse_full(&ctx);
            p = ctx.next;
            continue;
        }

        /* --- 6. generic element: build the node, apply style --- */
        CwvNode n;
        node_from_parent(r, &n, tag, parent);
        n.depth = r->parse_depth;      /* see the depth comment in renderer.h */
        if (!strcmp(tag, "canvas")) {
            char cw[32] = {0}, ch[32] = {0};
            n.is_canvas = 1;
            n.is_box = 1; /* empty canvas still owns a drawable DOM box */
            n.canvas_width = 300; n.canvas_height = 150;
            if (find_attr(q, gt, "width", cw, sizeof(cw)) && atoi(cw) > 0) n.canvas_width = atoi(cw);
            if (find_attr(q, gt, "height", ch, sizeof(ch)) && atoi(ch) > 0) n.canvas_height = atoi(ch);
            if (n.canvas_width > 1024) n.canvas_width = 1024;
            if (n.canvas_height > 768) n.canvas_height = 768;
            n.w = (float)n.canvas_width; n.h = (float)n.canvas_height;
        }
        char style[2048], id[64] = {0}, cls[128] = {0}, vcm[24] = {0}, control_modal[96] = {0};
        if (find_attr(q, gt, "style", style, sizeof(style))) style_apply(&n, style);
        find_attr(q, gt, "id", id, sizeof(id));
        find_attr(q, gt, "class", cls, sizeof(cls));
        snprintf(n.id, sizeof(n.id), "%s", id);
        snprintf(n.cls, sizeof(n.cls), "%s", cls);
        {
            char tabindex_value[32] = {0}, disabled_value[16] = {0};
            if (find_attr(q, gt, "tabindex", tabindex_value, sizeof(tabindex_value)))
                n.tabindex = atoi(tabindex_value);
            if (find_attr(q, gt, "disabled", disabled_value, sizeof(disabled_value)))
                n.disabled = 1;
        }
        if (find_attr(q, gt, "vcm", vcm, sizeof(vcm))) {
            trim(vcm);
            snprintf(n.vcm, sizeof(n.vcm), "%s", vcm);
        }
        if (find_attr(q, gt, "cwv-control-modal", control_modal, sizeof(control_modal))) {
            trim(control_modal);
            n.control_modal = 1;
            snprintf(n.control_modal_title, sizeof(n.control_modal_title), "%s", control_modal);
        }
        apply_rules(r, &n, id, cls, 0);
        if (n.font_family[0]) n.font = font_for(r, n.font_family);
        /* CSS background-image: resolve against the page URL and load the
           tile now (parse time, same point as <img>).  A failed load logs
           the reason to the console via load_image's diagnostics; the
           solid background color, if the page set one, shows through. */
        if (n.bg_image_src[0]) {
            char bu[1024];
            resolve_url(r->base_url, n.bg_image_src, bu, sizeof(bu));
            n.bg_image = load_image(r, bu);
        }
        /* CSS cursor: url(...) art - resolve against the page URL and
           load now, the same point as background-image (2026-09-26
           (45)).  Local files and http/https URLs both work (fetch
           hook).  A failed load honors the trailing keyword fallback,
           else falls back to auto. */
        if (n.cursor_url[0] && n.cursor_style == CWV_CSTYLE_URL) {
            char cu[1024];
            resolve_url(r->base_url, n.cursor_url, cu, sizeof(cu));
            n.cursor_img = load_image(r, cu);
            if (!n.cursor_img) {
                n.cursor_style = n.cursor_fallback != CWV_CSTYLE_AUTO
                                 ? n.cursor_fallback : CWV_CSTYLE_AUTO;
                n.cursor_url[0] = 0;
            }
        }

        /* --- 7. tag-specific customization (registry setup hook) --- */
        if (info && info->setup) {
            TagCtx ctx;
            ctx.r = r;
            ctx.tag = tag;
            ctx.q = q;
            ctx.gt = gt;
            ctx.end = end;
            ctx.parent = parent;
            ctx.next = NULL;
            info->setup(&ctx, &n);
        }

        /* --- 8. void tags are done: no children, no close tag --- */
        if (info && (info->flags & TAGF_VOID)) {
            p = gt + 1;
            continue;
        }

        /* --- 9. find the matching close tag, then parse children --- */
        const char *clt = NULL, *cgt = NULL;
        int has_close = find_matching_close(gt, end, tag, &clt, &cgt);
        const char *child_end = has_close ? clt : end;
        int block = (n.display_mode == 0);

        /* Preserve the parent's flow position before child parsing. Child
           text must never move an inline-block control's own origin. */
        float flow_x = r->cursor_x;
        float flow_y = r->cursor_y;
        float flow_lh = r->line_height_current;

        /* A block starts a new formatting line. Finish any preceding inline
           content before placing its box. */
        if (block) {
            if (r->cursor_x > r->page_left && r->line_height_current > 0.0f)
                r->cursor_y += r->line_height_current;
            r->cursor_x = r->page_left;
            r->line_height_current = 0.0f;
            /* Adjacent block vertical margins collapse: use the larger of
               the previous bottom margin and this element's top margin, not
               the sum. */
            if (have_pending_block_margin) {
                r->cursor_y += (pending_block_margin_bottom > n.margin_top ?
                                pending_block_margin_bottom : n.margin_top);
                pending_block_margin_bottom = 0.0f;
                have_pending_block_margin = 0;
            } else {
                r->cursor_y += n.margin_top;
            }
        }
        float box_y = r->cursor_y;
        int box_index = -1;
        int child_start;

        /* Absolute/fixed offsets are applied after measuring the complete
           subtree below. Keep normal-flow coordinates during measurement so
           right/bottom can anchor against the rendered size. */

        /* A parent without padding/border participates in CSS margin
           collapsing with its first in-flow child.  We resolve the collapse
           after parsing the children below, because that is the first point
           where the actual child margin is known. */

        /* Block boxes use the available width by default, exactly as a
           browser's normal block formatting context does. Inline-block
           controls (notably <button>) are different: they are shrink-to-fit
           unless CSS explicitly supplies a width.
           Padding and borders establish the content origin.  The previous
           renderer painted the padding but parsed children from the outer
           box origin, which made backgrounds and nested content disagree. */
        if (block) {
            r->cursor_y = box_y + n.padding_top + n.border_width;
            r->cursor_x = r->page_left + n.padding_left + n.border_width;
            r->line_height_current = 0.0f;
        }
        /* Save the PARENT flow BEFORE the CSS width resolution below
           narrows it: the resolution shrinks page_right for the element's
           children (their text wraps at the element's width), and the
           restore after parse_html_range must put back the values the
           PARENT flow had, not the already-narrowed ones.  Saving after
           the narrowing made a "width:60px" div permanently narrow the
           flow for every sibling that followed (the last <img> on
           bgimage_test.html clamped to 60px, found by the reference
           gate, 2026-10-05 (66)). */
        float saved_page_left = r->page_left, saved_page_right = r->page_right;
        /* CSS width resolution (2026-10-05 (65)): runs BEFORE the box
           node is reserved so the painted box width and the narrowed
           content flow can never disagree (that mismatch was the old
           "text overflows its width:80% panel" bug).
           - a percentage width is resolved against the containing block's
             content width now that it is finally known (Chrome parity:
             width:80% is 80% of the parent, not 80px, 2026-10-05 (66));
           - an explicit width sets the border box (the engine's native
             model, i.e. box-sizing: border-box);
           - box-sizing: content-box widens it by padding+border;
           - min/max-width clamp it (max-width also clamps an auto-width
             full block, exactly like CSS).
           The resolved pixel width is written back into n.w so every
           later consumer (box painter, button label math) sees the
           final value, and the flow is narrowed to it so the block's
           TEXT wraps inside its box. */
        if (block) {
            float avail = (float)r->width - r->page_left - r->page_right;
            if (n.has_w || n.max_w > 0.0f || n.min_w > 0.0f) {
                float bw;
                if (n.w_pct > 0.0f)
                    bw = avail * n.w_pct / 100.0f;
                else if (n.has_w && n.w >= 0.0f)
                    bw = n.w;
                else
                    bw = avail;
                if (n.box_sizing == 1 && (n.has_w || n.w_pct > 0.0f))
                    bw += n.padding_left + n.padding_right
                          + n.border_width * 2.0f;
                if (n.max_w > 0.0f && bw > n.max_w) bw = n.max_w;
                if (n.min_w > 0.0f && bw < n.min_w) bw = n.min_w;
                n.w = bw;
                n.w_pct = 0.0f;
                if (bw < avail) r->page_right += avail - bw;
            }
        }
        /* Reserve a painted box node for blocks or anything with visible
           chrome (background/border) or VCM bindings. */
        if (block || n.has_background || n.has_border || n.vcm[0] || n.control_modal || n.is_canvas) {
            if (r->node_count < VW_MAX_NODES) {
                box_index = r->node_count++;
                r->nodes[box_index] = n;
                r->nodes[box_index].is_box = 1;
                r->nodes[box_index].x = r->page_left;
                /* Match the 0.1.0 renderer's visual box origin: the box
                   slightly surrounds the text rather than treating the
                   font's line-height as visible content. */
                r->nodes[box_index].y = box_y;
                if (n.has_w) r->nodes[box_index].w = n.w;   /* width:0 collapses */
                else if (n.w > 0) r->nodes[box_index].w = n.w;
                else if (n.display_mode == 0)
                    r->nodes[box_index].w = (float)r->width - r->page_left - r->page_right;
                else r->nodes[box_index].w = 1.0f;
                r->nodes[box_index].h = (n.h > 0 ? n.h : 0.0f);
                r->nodes[box_index].text[0] = 0;
            }
        }
        /* The generated border/background box is not one of this element's
           children. Start measuring margins and content after it. */
        child_start = r->node_count;

        /* One level deeper for this element's content - but only if the
           element actually creates a flow context (a block or a painted
           box).  Box-less inline elements (a plain <a>, <b>, <span>)
           leave their children in the parent's flow, exactly like CSS:
           their content is still a DIRECT child of the surrounding block. */
        int flow_context = (block || box_index >= 0);
        if (flow_context) r->parse_depth++;
        /* Children flow inside the parent's CONTENT box, not the page box:
           a block with padding/border narrows the flow for everything
           inside it, the way Chrome insets nested content by the parent's
           padding (a padded <div>'s children no longer run edge to edge).
           Save, shrink, parse, restore. */
        if (block) {
            r->page_left += n.padding_left + n.border_width;
            /* page_right is a distance FROM the right edge, so a parent's
               right padding makes the child's inset LARGER, not smaller. */
            r->page_right += n.padding_right + n.border_width;
        }
        parse_html_range(r, gt + 1, child_end, &n);
        /* Restore the parent-level insets right after the children are
           parsed: children (and their layouts) run INSIDE the call above
           and need the narrowed content box, while THIS element's own
           layout_block below must size it against the parent's box. */
        r->page_left = saved_page_left;
        r->page_right = saved_page_right;
        if (flow_context) r->parse_depth--;

        /* A list container ends here: pop its marker/counter so an outer
           <ol> resumes numbering where it left off (nested lists inside
           an <li> pushed/popped inside the recursive call above). */
        if (info && (info->flags & TAGF_LIST) && g_list_depth > 0)
            g_list_depth--;

        /* Carry the VCM/id/class bindings onto the painted box node. */
        if (box_index >= 0) {
            r->nodes[box_index].vcm_active = n.vcm_active;
            r->nodes[box_index].control_modal = n.control_modal;
            snprintf(r->nodes[box_index].control_modal_title,
                     sizeof(r->nodes[box_index].control_modal_title), "%s", n.control_modal_title);
            snprintf(r->nodes[box_index].vcm, sizeof(r->nodes[box_index].vcm), "%s", n.vcm);
            snprintf(r->nodes[box_index].id, sizeof(r->nodes[box_index].id), "%s", n.id);
            snprintf(r->nodes[box_index].cls, sizeof(r->nodes[box_index].cls), "%s", n.cls);
        }

        /* A <select>'s parsed children are only labels for the picker. */
        if (n.is_select)
            select_harvest(r, &n, child_start, box_index);


        /* A box-less inline <a> reserves no node of its own, so the DPAD
           focus stop is its FIRST child node (a text run, or a wrapped
           block), exactly like Chrome gives ONE tab stop per link.  Links
           without an href are not stops, like the browser. */
        if (n.is_link && n.href[0] && box_index < 0) {
            int k;
            for (k = child_start; k < r->node_count; k++)
                if (r->nodes[k].is_link && !r->nodes[k].is_button) {
                    r->nodes[k].focusable_self = 1;
                    r->nodes[k].tabindex = n.tabindex;
                    break;
                }
        }

        /* Inline elements leave no node of their own (only their text
           children survive), so remember their id -> first text child for
           the JS layer and querySelector. */
        if (n.id[0] && !block && box_index < 0) {
            int k;
            for (k = child_start; k < r->node_count; k++)
                if (r->nodes[k].text[0]) { renderer_map_inline_id(r, n.id, k); break; }
        }

        /* --- 10. layout by display mode --- */
        if (n.display_mode == 2) {
            layout_inline_block(r, &n, child_start, box_y, box_index,
                               flow_x, flow_y, flow_lh);
        } else if (block) {
            layout_block(r, &n, child_start, box_y, box_index,
                         &pending_block_margin_bottom, &have_pending_block_margin);
        } else {
            layout_inline(r, &n, child_start, flow_x, flow_y, box_index);
        }

        /* CSS position (2026-10-05 (70)) - the element is fully laid out
           now, so this is the moment to finish the positioning. */
        if (n.pos_kind == 2 || n.pos_kind == 3) {
            /* absolute/fixed: OUT of the flow.  right/bottom are resolved
               after measuring the box, because only then is its rendered
               size known.  The current engine approximates the containing
               block with the viewport; this is more useful than silently
               ignoring the two CSS offsets. */
            int pos_end = r->node_count;
            float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
            int first = box_index >= 0 ? box_index : child_start;
            int si;
            int have_bounds = 0;
            for (si = first; si < pos_end && si < VW_MAX_NODES; si++) {
                CwvNode *pn = &r->nodes[si];
                float x1 = pn->x, y1 = pn->y;
                float x2 = pn->x + (pn->w > 0.0f ? pn->w : 0.0f);
                float y2 = pn->y + (pn->h > 0.0f ? pn->h : 0.0f);
                if (!have_bounds) {
                    min_x = x1; min_y = y1; max_x = x2; max_y = y2;
                    have_bounds = 1;
                } else {
                    if (x1 < min_x) min_x = x1;
                    if (y1 < min_y) min_y = y1;
                    if (x2 > max_x) max_x = x2;
                    if (y2 > max_y) max_y = y2;
                }
            }
            if (have_bounds && pos_end > first) {
                float dx = 0.0f, dy = 0.0f;
                if (n.pos_left_set)
                    dx = r->page_left + n.pos_left - min_x;
                else if (n.pos_right_set)
                    dx = cwv_css_viewport_width() - n.pos_right - max_x;
                if (n.pos_top_set)
                    dy = n.pos_top - min_y;
                else if (n.pos_bottom_set)
                    dy = cwv_css_viewport_height() - n.pos_bottom - max_y;
                if (dx != 0.0f || dy != 0.0f) {
                    for (si = first; si < pos_end && si < VW_MAX_NODES; si++) {
                        r->nodes[si].x += dx;
                        r->nodes[si].y += dy;
                    }
                }
            }
            /* Restore the cursor the parent had, so later siblings lay out
               as if this box never existed - and its margin does not fire. */
            r->cursor_x = flow_x;
            r->cursor_y = flow_y;
            r->line_height_current = flow_lh;
        } else if (n.pos_kind == 1 || n.pos_kind == 4) {
            /* relative (and sticky-as-relative): the flow keeps the box's
               SPACE; the painted box and its children shift by the given
               offsets without moving anything else, like Chrome. */
            float dx = (n.pos_left_set ? n.pos_left : 0.0f)
                     - (n.pos_right_set ? n.pos_right : 0.0f);
            float dy = (n.pos_top_set ? n.pos_top : 0.0f)
                     - (n.pos_bottom_set ? n.pos_bottom : 0.0f);
            if (dx != 0.0f || dy != 0.0f) {
                int from = box_index >= 0 ? box_index : child_start;
                int si;
                for (si = from; si < r->node_count && si < VW_MAX_NODES; si++) {
                    r->nodes[si].x += dx;
                    r->nodes[si].y += dy;
                }
            }
        }

        p = has_close ? cgt + 1 : end;
    }
    if (have_pending_block_margin)
        r->cursor_y += pending_block_margin_bottom;
}

/* Record an inline element's id for the JS layer (see CwvRenderer). */
void renderer_map_inline_id(CwvRenderer *r, const char *id, int first_child) {
    int i;
    if (!r || !id || !id[0]) return;
    /* update if the id is already mapped (e.g. re-parsed element) */
    for (i = 0; i < r->id_map_count; i++)
        if (!strcmp(r->id_map[i].id, id)) { r->id_map[i].node_index = first_child; return; }
    if (r->id_map_count >= 64) return;
    snprintf(r->id_map[r->id_map_count].id, sizeof(r->id_map[0].id), "%s", id);
    r->id_map[r->id_map_count].node_index = first_child;
    r->id_map_count++;
}

/* Take a wrapped line of text and add it as a node, splitting the node's
 * text at word boundaries to fit the available line width.  One node may
 * therefore become several nodes, one per visual line. */
/* CSS text-transform (2026-10-05 (65)): applied in place, in-place
 * per word for capitalize (the first ALPHANUMERIC of each word grows
 * upper-case, like Chrome - leading punctuation stays small). */
static void transform_text(CwvNode *n) {
    char *q;
    int word_start = 1;
    if (!n->text_transform || !n->text[0]) return;
    for (q = n->text; *q; q++) {
        if (n->text_transform == 1)
            *q = (char)toupper((unsigned char)*q);
        else if (n->text_transform == 2)
            *q = (char)tolower((unsigned char)*q);
        else {   /* capitalize */
            if (isalnum((unsigned char)*q)) {
                if (word_start) *q = (char)toupper((unsigned char)*q);
                word_start = 0;
            } else if (isspace((unsigned char)*q)) {
                word_start = 1;
            }
        }
    }
}

static void css_normalize_whitespace(char *text, int mode) {
    char *src = text, *dst = text;
    int in_space = 0;
    /* normal/nowrap collapse all ASCII whitespace; pre-line collapses spaces
       and tabs but preserves explicit newlines. pre/pre-wrap stay untouched. */
    if (mode != 0 && mode != 1 && mode != 4) return;
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (mode == 4 && c == '\n') {
            while (dst > text && dst[-1] == ' ') dst--;
            *dst++ = '\n';
            in_space = 0;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            in_space = 1;
            continue;
        }
        if (in_space && dst > text && dst[-1] != '\n') *dst++ = ' ';
        *dst++ = (char)c;
        in_space = 0;
    }
    while (dst > text && (dst[-1] == ' ' || dst[-1] == '\n')) dst--;
    *dst = 0;
}

static void wrap_and_add(CwvRenderer *r, CwvNode *n) {
    if (n->display_none || !n->text[0]) return;

    /* Inline text uses the remaining line width.  Block text gets the full
       content width so text-align:center can be resolved against the block,
       rather than against the current cursor position. */
    float available;
    if ((n->centered || n->right_aligned) && n->display_mode == 1) {
        available = (float)r->width - r->page_left - r->page_right
                    - n->padding_left - n->padding_right;
    } else {
        available = (float)r->width - r->page_right - r->cursor_x
                    - n->padding_left - n->padding_right;
    }
    if (available < 1.0f) available = 1.0f;

    char src[VW_MAX_TEXT];
    /* CSS text-transform runs before wrapping so the measured width
       matches what is painted (2026-10-05 (65)). */
    transform_text(n);
    snprintf(src, sizeof(src), "%s", n->text);
    css_normalize_whitespace(src, n->white_space);
    char *p = src;
    while (*p && r->node_count < VW_MAX_NODES) {
        /* Gather characters until the line is full or a newline appears.
           white-space:nowrap/pre (2026-10-05 (65)): no width wrapping at
           all - the run keeps whole words on one line and only a literal
           newline breaks it, so <pre> blocks and nowrap chips behave
           like Chrome's no-wrap (overflow shows instead of re-flowing). */
        int no_wrap = (n->white_space == 1 || n->white_space == 2);
        char line[VW_MAX_TEXT] = {0};
        int count = 0;
        int last_space_idx = -1;   /* index into line[] / src[] alike */
        while (*p && count < (int)sizeof(line) - 1) {
            if (*p == '\n') { p++; break; }
            line[count++] = *p++;
            if (line[count - 1] == ' ') last_space_idx = count - 1;
            if (!no_wrap &&
                measure_text_width(r, n, line) > available && count > 1) {
                /* Back up to the last space so words stay whole.  The
                   line buffer mirrors src 1:1 (line[i] == src[base+i]
                   with base = p-src-count), so the continuation pointer
                   is derived in SRC.  The old code did p = last_space+1
                   with last_space INSIDE line[], the loop's next
                   iteration re-zeroed that buffer and the walk stopped
                   at its NUL - silently dropping the whole rest of the
                   paragraph on the first space wrap (found by the
                   width-clamp regression trace, 2026-10-05 (66)). */
                if (last_space_idx >= 0) {
                    line[last_space_idx] = 0;
                    p = src + ((p - src) - count + last_space_idx + 1);
                    while (*p == ' ') p++;
                } else {
                    line[--count] = 0;
                    p--;
                }
                break;
            }
        }
        line[count] = 0;
        /* pre and pre-wrap preserve authored edge spaces; other modes
           collapse/trim them according to their whitespace policy. */
        if (n->white_space != 2 && n->white_space != 3) trim(line);
        if (!*line) continue;

        CwvNode *x = &r->nodes[r->node_count++];
        *x = *n;
        snprintf(x->text, sizeof(x->text), "%s", line);
        float tw = measure_text_width(r, x, line);
        x->w = tw + n->padding_left + n->padding_right + n->border_width * 2.0f;
        x->y = r->cursor_y;
        x->h = line_height(x);

        if ((n->centered || n->right_aligned) && n->display_mode == 1) {
            float content_left = r->page_left + n->padding_left;
            float content_width = (float)r->width - r->page_left - r->page_right
                                  - n->padding_left - n->padding_right;
            if (n->centered)
                x->x = content_left + (content_width - tw) * 0.5f;
            else
                x->x = content_left + content_width - tw;
            if (x->x < content_left) x->x = content_left;
        } else {
            x->x = r->cursor_x + n->padding_left;
            if (r->cursor_x > r->page_left && x->x + x->w > (float)r->width - r->page_right) {
                r->cursor_y += r->line_height_current > 0.0f ? r->line_height_current : line_height(x);
                r->cursor_x = r->page_left;
                x->x = r->cursor_x + n->padding_left;
                x->y = r->cursor_y;
            }
        }

        float lh = x->h;
        if (lh > r->line_height_current) r->line_height_current = lh;
        if ((n->centered || n->right_aligned) && n->display_mode == 1) {
            /* Centered text in a block must not advance the inline cursor by
               its painted width. The containing block owns the line. */
            r->cursor_x = (float)r->width - r->page_right;
        } else {
            r->cursor_x = x->x + x->w;
        }
    }
}

/*
 * Reset the renderer and parse the whole document from r->html.
 *
 * The 0.1.0 renderer worked because each visible element was turned into a
 * drawable node. The later linear compatibility parser tried to recover that
 * behavior by finding the first </...> after an opening tag. That is not safe
 * for nested HTML: <header><h1>...</h1></header> made the header consume the
 * h1 as literal text, which is exactly why tags were appearing on screen.
 *
 * Use the recursive tree-aware parser below instead. It finds the matching
 * closing tag, applies inherited styles to descendants, and creates drawable
 * text/background nodes for the actual elements.
 */
void parse_html(CwvRenderer *r) {
    g_list_depth = 0;   /* fresh list stack per document */
    CwvNode root;

    r->node_count = 0;
    r->parse_depth = 0;
    r->svg_pts_used = 0;      /* fresh SVG point arena per page (67) */
    r->svg_vb_n = 0;
    r->page_left = 12.0f;
    r->page_right = 12.0f;
    r->page_top = 0.0f;
    r->control_mode = 0;
    /* Start the document at the top of the viewport.  The previous build
       introduced a document-top inset here, but that was not intended to be
       part of the page's CSS layout.  Keep the 24px horizontal Vita safe-area
       inset in the element positioning code; do not add vertical page chrome. */
    r->cursor_y = r->page_top;
    r->cursor_x = r->page_left;
    r->line_height_current = 0.0f;
    r->body_background = r->theme_dark ? rgba(0, 0, 0, 255) : rgba(245, 245, 245, 255);
    r->body_color = r->theme_dark ? rgba(235, 238, 242, 255) : rgba(25, 25, 30, 255);
    r->body_background_explicit = 0;
    r->body_color_explicit = 0;
    r->overflow_x = 0;
    r->overflow_y = 0;
    /* The recursive parser is the authoritative path.  It preserves the
       element tree, so tags inside containers remain elements rather than
       becoming literal text such as "<h1>...". */
    node_defaults(r, &root, "body");
    root.color = r->body_color;
    root.background = r->body_background;
    root.has_background = 0;
    parse_html_range(r, r->html, r->html + strlen(r->html), &root);
    if (!r->body_background_explicit)
        r->body_background = r->theme_dark ? rgba(0, 0, 0, 255) : rgba(245, 245, 245, 255);
    if (!r->body_color_explicit)
        r->body_color = r->theme_dark ? rgba(235, 238, 242, 255) : rgba(25, 25, 30, 255);
}
