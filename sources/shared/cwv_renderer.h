/*
 * ============================================================================
 *  Vita WebView - renderer data model + shared declarations
 *  (cwv_renderer.h - INTERNAL header, shared by the renderer files)
 * ============================================================================
 *
 *  The native renderer is split by responsibility:
 *
 *    cwv_backend.c   core infra: text helpers, resource loading (files,
 *                         curl, images, fonts), drawing, input, scrolling,
 *                         IME, and the public CwvBackendOps API
 *    cwv_css.c       the CSS side: value parsing, the CSS_PROPERTIES[]
 *                         registry, rules and selector matching
 *    cwv_html.c      the HTML side: the TAG_TABLE[] registry, the
 *                         recursive parser and the layout engine
 *    cwv_js.c        the JavaScript side: the Duktape engine glue, the
 *                         JS_GLOBALS[] / document / element method registries,
 *                         <script> harvesting, timers and events
 *
 *  This header defines what they share: the data model (what a page is made
 *  of, in three structs) and the functions that cross file boundaries.
 *  Everything else in the .c files is declared `static` (= private to that
 *  file), so this header is also the table of contents: if you want to know
 *  what one part of the renderer offers to another, read it top to bottom.
 *
 *  Read docs/EXTENDING_THE_RENDERER.md for how to add CSS properties, HTML
 *  tags and JS globals with one table row each.
 * ============================================================================
 */

#ifndef CWV_RENDERER_H
#define CWV_RENDERER_H

#include "cwv_backend.h"

#include <stdio.h>
#include "cwv_platform.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---------------- Hard limits (fixed-size buffers, see file banners) ----- */
#define VW_MAX_HTML  262144   /* largest HTML file we will read (256 KB) */
#define VW_MAX_TEXT  4096     /* longest single text run in one node */
#define VW_MAX_NODES 512      /* max layout nodes per page */
#define VW_MAX_CSS   131072   /* largest CSS buffer */
#define VW_MAX_RULES 256      /* max CSS rules per page */
#define VW_MAX_IMAGES 64
#define VW_MAX_FONTS  32

#define BODY_FONT_SIZE 0.75f  /* the "1em" body font size, in renderer units */

/* Keep the document inside a small browser-like safe area.  This is a
   renderer viewport inset, not CSS margin: it must remain present even when
   the page itself has no body margin.  The scrollbars stay on the physical
   screen edge, like a normal browser's scrollbar chrome. */
#define RENDERER_VIEW_INSET 12.0f

/* ============================================================================
 * SECTION 1: DATA MODEL
 * ============================================================================
 * Everything the renderer knows about one page lives in these four structs:
 *
 *   CwvCssRule  - one CSS rule ("selector { declarations }")
 *   CwvFont     - one loaded @font-face font family
 *   CwvNode     - one drawable element or text run (the "DOM")
 *   CwvRenderer - the whole page state (nodes, rules, scroll, focus, ...)
 * ==========================================================================*/

typedef struct {
    char selector[128];       /* e.g. ".card, .note" (as written, unexpanded) */
    char declarations[2048]; /* e.g. "color: red; margin: 4px" */
} CwvCssRule;

typedef struct {
    char family[96];          /* CSS font-family name to match */
    char src[1024];           /* resolved URL the font was loaded from */
    int weight;
    int italic;
    void *font;        /* loaded vita2d TTF/OTF handle (NULL if broken) */
} CwvFont;

/* ---- CSS cursor keywords (2026-09-26 (45)) ---------------------------
 * The parser stores the keyword, the backend hit-test resolves the
 * effective style under the pointer (inheritance is baked into child
 * nodes at parse time like color), and the port maps the style onto
 * the cursor art pack:  <pkg>/assets/cursors/<name>.png
 *   default.png pointer.png text.png wait.png crosshair.png help.png
 *   move.png grab.png grabbing.png not-allowed.png zoom-in.png
 *   zoom-out.png            (missing art falls back to the console art)
 * CWV_CSTYLE_URL carries custom art (cursor: url(...), local or
 * internet) loaded by the engine; the hotspot comes from the CSS
 * "url(x.png) hx hy" form.  AUTO resolves UA defaults: links/buttons
 * get POINTER, textfields get TEXT - like a browser. */
enum {
    CWV_CSTYLE_AUTO = 0, CWV_CSTYLE_DEFAULT, CWV_CSTYLE_NONE,
    CWV_CSTYLE_POINTER, CWV_CSTYLE_TEXT, CWV_CSTYLE_WAIT,
    CWV_CSTYLE_CROSSHAIR, CWV_CSTYLE_HELP, CWV_CSTYLE_MOVE,
    CWV_CSTYLE_GRAB, CWV_CSTYLE_GRABBING, CWV_CSTYLE_NOT_ALLOWED,
    CWV_CSTYLE_ZOOM_IN, CWV_CSTYLE_ZOOM_OUT, CWV_CSTYLE_URL
};

/* What cwv_host_cursor_at() hands the port for the point under the
 * pointer.  img is an ENGINE-owned texture (URL cursors only); the
 * port draws it through the normal cursor path and never frees it. */
/* (named struct: cwv_host.h / cwv_backend.h forward-declare the
 * typedef so they stay self-contained like the ES6 hooks) */
typedef struct CwvCursorInfo {
    int style;              /* CWV_CSTYLE_* */
    void *img;              /* URL cursor art, NULL otherwise */
    int hot_x, hot_y;       /* URL cursor hotspot within the art */
} CwvCursorInfo;

/* One drawable box: an element (<div>, <p>, <img>, <button>, ...) or one run
 * of text. The parser produces these; the drawing code paints them.
 * Groups of fields are separated by short comments. */
typedef struct {
    /* identity + content */
    char tag[16];             /* lower-case tag name, or "p" for bare text */
    char text[VW_MAX_TEXT];   /* visible text (already entity-decoded) */
    char src[1024];           /* img src / font src, resolved */

    /* geometry (document space, before zoom/scroll) */
    float x, y, w, h;
    /* CSS width bookkeeping: w_pct holds an unresolved percentage width
     * (0 = none) until the block enters the flow and it is resolved
     * against the containing block; has_w marks that CSS set a width
     * at all (width:0 collapses the box instead of reading as auto). */
    float w_pct;
    int has_w;

    /* text color */
    unsigned color;
    int color_explicit;       /* set when CSS/UA gave an explicit color */
    /* set when this node's color was INHERITED from an ancestor whose
       color was explicit (page CSS), not from the theme default.  Theme
       re-tint passes must never touch these nodes: a browser's dark/light
       mode never overrules the page's own cascade, and re-tinting them
       was what made es6_test/es6_demo text "flash to black" when a theme
       pass ran (2026-09-21 (29)). */
    int color_inherited_explicit;
    unsigned background;      /* RGBA8 fill color */
    int has_background;

    /* CSS opacity + visibility (2026-10-05 (65)).  The engine composites
       opacity by mixing each painted color toward the solid backdrop
       behind it (baked at parse time like color inheritance), because
       every console draws solid rects/text; images and tiled backgrounds
       stay opaque (documented limitation).  eff_opacity is the ACCUMULATED
       product down the ancestor chain - group opacity multiplies. */
    float   eff_opacity;     /* 1.0 default; parent product * own opacity */
    unsigned paint_backdrop; /* solid color behind this node's own
                                background - what opacity mixes against */
    int     visibility_hidden;  /* visibility:hidden - keep the layout
                                   space, skip paint and hit tests */

    /* border */
    int has_border;
    float border_width;
    unsigned border_color;
    int border_style;         /* 0=none, 1=solid, 2=dashed, 3=dotted, 4=double */

    /* typography */
    float font_size;
    int bold;
    int centered;             /* text-align: center */
    /* CSS typography batch (2026-10-05 (65)).  All of these are baked
       down at parse time (node_from_parent / wrap_and_add) so the draw
       path stays flat and per-platform code never changes. */
    int   deco;              /* text-decoration bitmask: 1 underline,
                                2 line-through, 4 overline (inherited -
                                it propagates to inline descendants) */
    int   text_transform;    /* 0 none, 1 uppercase, 2 lowercase,
                                3 capitalize (applied in wrap_and_add) */
    float letter_spacing;    /* px between characters, 0 = none */
    float word_spacing;      /* extra px after each space, 0 = none */
    float line_height_mul;   /* CSS line-height factor on the 28px line
                                box (1.0 = default, inherited) */
    int   white_space;       /* 0 normal, 1 nowrap, 2 pre (nowrap + no
                                trimming of line edges, keeps spaces) */
    int display_none;         /* display:none - skip painting and hit tests */
    int display_explicit;     /* display was set via CSS (vs UA default) */

    /* CSS box model */
    float margin_top, margin_right, margin_bottom, margin_left;
    float padding_left, padding_right, padding_top, padding_bottom;
    /* min/max-width/box-sizing (blocks): resolved into w once, right
       before the children are parsed (see parse_html_range).  The engine
       paints the border box for an explicit width - i.e. box-sizing
       border-box is the native model; content-box WIDENS it. */
    float min_w, max_w;      /* px clamps, 0 = unset */
    int   box_sizing;        /* 0 = border-box (engine default), 1 = content-box */

    /* image support */
    int is_image;
    void *image;
    char alt[256];             /* img alt text, drawn on a broken image */
    int object_fit;            /* 0=fill (stretch), 1=contain, 2=cover */
    int image_rendering;       /* CSS image-rendering: 0=auto (linear
                                  sampling), 1=pixelated/crisp-edges
                                  (nearest - chunky retro pixels) */
    int image_filter_state;   /* filter last pushed to the texture, so the
                                  draw path only calls set_filters on change */
    int list_style;           /* CSS list-style-type on this element.
                                  0 = unset (the enclosing list decides),
                                  1 disc, 2 circle, 3 square, 4 decimal,
                                  9 none */
    int list_marker;          /* <li> only: the RESOLVED marker kind (same
                                  values as list_style) */
    int list_number;          /* <li> only: item number when decimal */
    void *bg_image;  /* CSS background-image, loaded at parse time */
    char bg_image_src[512];    /* the raw url() value from CSS (unresolved) */
    /* CSS background layer keys (2026-10-05 (68)): repeat / position /
       size / attachment / clip.  Position/size store a px value plus a
       pct flag (0 = px, 1 = percent, resolved against box-minus-image
       at draw time, exactly like Chrome). */
    int   bg_repeat;      /* 0 repeat, 1 no-repeat, 2 repeat-x, 3 repeat-y */
    float bg_pos_x, bg_pos_y;  int bg_pos_x_pct, bg_pos_y_pct;
    float bg_size_w, bg_size_h; int bg_size_w_pct, bg_size_h_pct;
    int   bg_size_cover, bg_size_contain;   /* cover/contain keywords */
    int   bg_attachment_fixed;   /* background-attachment: fixed */
    int   bg_clip;         /* 0 border-box, 1 padding-box, 2 content-box */

    /* CSS position (2026-10-05 (70)): static is the flow model this engine
       has always used.  relative shifts the painted box (and its
       children) without touching the flow; absolute/fixed pull the box
       out of the flow and pin it at top/left offsets (fixed also stops
       scrolling with the page); sticky is painted as relative (the
       scroll-stick of the browser is not implemented, documented). */
    int   pos_kind;        /* 0 static, 1 relative, 2 absolute, 3 fixed,
                              4 sticky (renders as relative) */
    float pos_top, pos_right, pos_bottom, pos_left;   /* px offsets */
    int   pos_top_set, pos_right_set, pos_bottom_set, pos_left_set;

    /* inline SVG subset (2026-10-05 (67)): shape nodes carry baked,
       box-relative px points in the renderer's point arena; the <svg>
       root is an ordinary box node whose children are shape records. */
    int   svg_shape;     /* 0 none, 1 rect, 2 circle, 3 ellipse, 4 line,
                            5 polyline, 6 polygon, 7 path, 8 text */
    int   svg_fill_on, svg_stroke_on;
    unsigned svg_fill, svg_stroke;
    float svg_sw;        /* stroke width, px */
    int   svg_pt_off, svg_pt_n;    /* float PAIRS in r->svg_pts */
    int   svg_closed;    /* Z-closed path / polygon: fillable */

    /* fonts */
    void *font;        /* resolved @font-face font, may be NULL */
    char font_family[96];     /* CSS font-family value (may be unset) */

    /* CSS cursor: url(...)/keyword resolved at parse time.
       cursor_img loads through the SAME machinery as background-image
       (load_image: local file or the fetch hook for http/https), so
       internet cursors work like local ones (2026-09-26 (45)). */
    int cursor_style;        /* CWV_CSTYLE_* keyword, 0 = auto */
    char cursor_url[512];    /* raw url() value (unresolved) */
    void *cursor_img;        /* loaded url() art, NULL when load failed */
    int cursor_fallback;     /* keyword after the url() (CSS fallback) */
    int cursor_hot_x, cursor_hot_y;  /* url(x.png) hx hy hotspot */

    /* links */
    int is_link;
    char href[1024];
    char link_target[32];     /* "app" default, or cwv-target value */

    /* interactive controls */
    int is_button;
    int button_pressed;
    int focusable_self;       /* this exact element can take DPAD focus */
    int disabled;
    int tabindex;
    int is_select;
    int is_textfield;
    int z_index;                /* CSS z-index; paint order sorts on this */
    int textfield_password;
    int textfield_multiline;
    char placeholder[256];
    int is_option;
    int option_selected;
    int select_open;
    int select_index;
    int select_hover_index;
    int select_touch_started_on_control;
    int select_touch_moved_to_option;

    /* CSS overflow policy: 0=auto, 1=visible, 2=hidden, 3=scroll. */
    int overflow_x;
    int overflow_y;
    /* overflow was set EXPLICITLY on this element (vs the inherited
     * "auto" default) - only such boxes can become scroll regions
     * (2026-10-06 (75)); the value still inherits for page clamping. */
    int overflow_explicit;

    /* Per-element scroll regions (2026-10-06 (75)).  A box whose content
     * overflows with overflow:auto/scroll becomes a SCROLL REGION: it
     * joins the DPAD/Tab focus chain like any control, and while focused
     * it consumes the platform scroll input (stick, keys, wheel) before
     * the page - leftover delta past the region's bounds falls through
     * to the page exactly like nested scroll containers in a browser.
     *
     * Region fields (scroll_region == 1) hold the live offsets and the
     * clamped scrollable extents.  The two offsets + clip window below
     * are BAKED per node by renderer_update_scroll_regions(): the
     * accumulated ancestor scroll offsets, and the document-space
     * visibility window the node paints/hit-tests inside (finite only
     * when an ancestor region clips it). */
    float scroll_top, scroll_left;      /* live offsets */
    float scroll_max_y, scroll_max_x;   /* scrollable extent, 0 = pinned */
    int   scroll_region;                /* 1 = focusable scrollable box */
    float scroll_off_x, scroll_off_y;   /* accumulated ancestor offsets */
    float clip_x0, clip_y0, clip_x1, clip_y1;  /* visibility window */
    int   has_clip;                     /* window is finite */

    int button_group;         /* id shared by a button and its label nodes */

    /* CSS display: 0=block, 1=inline, 2=inline-block. */
    int display_mode;
    int is_box;               /* this node is a painted box (bg/border owner) */
    /* DOM nesting depth (0 = top of the parsed document).  The parser keeps
     * it in sync with the element recursion; layout uses it to tell a box's
     * DIRECT children (depth + 1) from deeper descendants, which is what
     * CSS margin containment needs. */
    int depth;

    /* Vita control modal (VCM) attributes */
    char vcm[24];             /* vita-control binding name, e.g. "cross" */
    char id[64];              /* HTML id attribute */
    char cls[128];            /* HTML class attribute */
    int vcm_active;           /* persistent active state for the VCM binding */
    int control_modal;        /* shows the native modal when VCM is active */
    char control_modal_title[96];

} CwvNode;

/* The JavaScript runtime state (defined in cwv_js.c).  The renderer
 * only holds a pointer: pages without <script> keep it NULL and every JS
 * entry point below is a cheap NULL check, so the legacy no-JS behavior is
 * untouched. */
typedef struct CwvJs CwvJs;

/* ES6 extension hook types (CwvSourceTransformFn / CwvJsPreludeFn)
 * come from cwv_backend.h, included above. */
/* Whole-page state. One of these is allocated per loaded page (backend_init). */
typedef struct {
    int width, height, loaded;
    char url[1024];
    char base_url[1024];      /* directory part of the URL, for relative refs */
    char *html;               /* the whole HTML file text (heap) */

    CwvNode nodes[VW_MAX_NODES];  /* the flattened "DOM" in paint order */
    /* (75) active scroll-region clip pushed through the port seam, so
     * identical rects do not re-issue scissor state every node. */
    int   clip_active;
    float clip_cx0, clip_cy0, clip_cx1, clip_cy1;
    int node_count;
    int parse_depth;             /* current element nesting while parsing */
    /* inline-SVG point arena + viewBox scaling stack (2026-10-05 (67)).
       Reset with the page; shape nodes append baked points at parse
       time and reference them by svg_pt_off/svg_pt_n. */
#define VW_SVG_MAX_PTS 4096
    float   svg_pts[VW_SVG_MAX_PTS];
    int     svg_pts_used;
    struct { int depth; float scale, minx, miny; } svg_vb[4];
    int     svg_vb_n;
    /* Page insets for the flow origin.  12px horizontal safe area by
     * default (Vita rounded screen edges); CSS margin+padding on <body>
     * replaces it, clamped to the 12px minimum.  page_top is the body's
     * margin+padding top with no clamp (Chrome-accurate). */
    float page_left, page_right;
    float page_top;

    CwvCssRule rules[VW_MAX_RULES];
    int rule_count;

    CwvFont fonts[VW_MAX_FONTS];
    int font_count;

    unsigned body_background, body_color;
    int body_background_explicit, body_color_explicit;
    int theme_dark;

    int scrollbars_visible;
    float cursor_y;           /* layout cursor: where the next node goes */
    float scroll_y;
    float scroll_x;
    float zoom;
    int overflow_x;           /* page-wide overflow from <html>/<body> */
    int overflow_y;
    int focus_node;           /* index of the DPAD-focused node, or -1 */
    int select_focus_node;
    float cursor_x;           /* inline layout cursor (same line) */
    float line_height_current;/* height of the line being built */
    void *system_font; /* unused (kept for API symmetry) */
    void *system_pgf;   /* the Vita system bitmap font */
    char pending_navigation[1024];
    char pending_target[32];
    int control_mode;         /* 0=None, 1=Browser, 2=Standalone, 3=Touch */
    char button_wait[32];     /* optional cwv-button-wait policy; empty = default */

    /* native IME (on-screen keyboard) state */
    int ime_active;
    int ime_node;
    cwv_utf16 ime_initial[CWV_IME_MAX_TEXT];
    cwv_utf16 ime_buffer[CWV_IME_MAX_TEXT];

    /* Decoded-image cache, owned by the renderer (2026-09-21 (29)).
       Every <img> and CSS background-image texture lives HERE, not in the
       node: multiple tags with the same URL decode once (img_demo.html
       loads pixel.png six times -> one GPU texture), and page navigation
       frees the whole table, so GPU/texture memory no longer accumulates
       page over page (the Vita GXM heap exhaustion that made later
       images come up as the broken-placeholder after browsing around).
       Entries are per loaded page: backend_load clears the table before
       re-parsing, backend_shutdown frees it for good. */
#define CWV_IMG_CACHE 24
struct {
    char url[192];
    void *tex;
} img_cache[CWV_IMG_CACHE];
int img_cache_count;

    /* JavaScript runtime (NULL when the page has no <script> and no eval) */
    CwvJs *js;

    /* ES6->ES5 source transformer (NULL = run sources untransformed) */
    CwvSourceTransformFn source_transform;
    void *source_transform_userdata;
    /* Optional TypeScript stripper: applied FIRST, before the ES6
     * transform, to scripts the page marked as TypeScript. */
    CwvSourceTransformFn ts_transform;
    void *ts_transform_userdata;

    /* ES6 library prelude (NULL = no library injection) */
    CwvJsPreludeFn prelude_hook;
    void *prelude_userdata;

    /* Runtime log hook (per-app CWV.log under the app data namespace
 * on device).  Image-load
     * failures are mirrored here because the page console lives inside
     * the JS context: a page WITHOUT scripts has no context, so its
     * console lines (including every image diagnostic) would otherwise
     * be silently dropped and DevTools shows "(no js context)" with
     * nothing to show.  NULL = diagnostics stay console-only. */
    void (*runtime_log)(const char *fmt, ...);

    /* Network fetch hook for the page's fetch() (NULL = fetch fails
     * gracefully with status -1; the app still cannot crash). */
    CwvFetchHook fetch_hook;
    void *fetch_userdata;

    /* id -> node map for INLINE elements.  Box elements (blocks, controls,
     * anything with background/border) keep their id on their own node, but
     * a bare inline <a id="x"> creates no node - only its text children
     * survive.  This map lets JS still find such elements; it points at the
     * element's first text child.  Filled during parsing; not painted. */
    struct {
        char id[64];
        int node_index;
    } id_map[64];
    int id_map_count;
} CwvRenderer;

/* ----------------------------------------------------------------------------
 * Shared small text/color helpers (defined in cwv_backend.c)
 * -------------------------------------------------------------------------- */
unsigned rgba(unsigned r, unsigned g, unsigned b, unsigned a);
/* CSS opacity compositing: linear blend of fg toward bg by t (0..1).
 * t=1 returns fg unchanged.  The engine draws solid rects/text, so
 * opacity is approximated by mixing against the solid backdrop behind
 * the element (baked at parse time); see CwvNode.eff_opacity. */
unsigned mix_rgba(unsigned bg, unsigned fg, float t);
int ci_eq(const char *a, const char *b);                     /* case-insens == */
int ci_ncmp(const char *a, const char *b, size_t n);         /* case-insens cmp */
void trim(char *s);                                          /* strip spaces */
void append_text(char *out, size_t cap, const char *src, size_t len); /* +entities */
unsigned parse_color(const char *s, unsigned fallback);       /* CSS color -> RGBA8 */
const char *find_attr(const char *start, const char *end, const char *name,
                      char *out, size_t cap);                /* attribute value */

/* ----------------------------------------------------------------------------
 * CSS side (defined in cwv_css.c)
 * -------------------------------------------------------------------------- */
float css_length_px(const char *value, float font_size);      /* "12px"/"1.5em" -> px */
void cwv_css_set_viewport(float w, float h);  /* publish the viewport for
                                                 vw/vh/vmin/vmax and the
                                                 svh/dvh families (69) */
void parse_css(CwvRenderer *r, const char *css);        /* collect rules */
/* Project scroll:off (cwv_project.h): append AFTER the page's own rules
 * so overflow: hidden wins and scrolling clamps to 0. */
void css_project_force_no_scroll(CwvRenderer *r);
void style_apply(CwvNode *n, const char *style);          /* "k: v; k2: v2" -> node */

/* JS-facing variant: applies the style, then re-propagates the four
 * INHERITED properties (color/bold/centered/font_size) into the subtree,
 * because inheritance was baked in at parse time (see cwv_css.c). */
void style_apply_inline(CwvRenderer *r, CwvNode *root, const char *style);
void apply_rules(CwvRenderer *r, CwvNode *n, const char *id,
                 const char *cls, int active);               /* match+apply all rules */
int selector_matches(const char *sel, const char *tag, const char *id,
                     const char *cls, int active);            /* one selector list */

/* ----------------------------------------------------------------------------
 * HTML side (defined in cwv_html.c)
 * -------------------------------------------------------------------------- */
/* What kind of tag is this? (bit flags, combined with |) */
#define TAGF_VOID         (1u << 0)  /* no closing tag, no children (<img>, <br>) */
#define TAGF_IGNORED      (1u << 1)  /* skip the whole subtree (<script>, ...) */
#define TAGF_HEADING      (1u << 2)  /* h1..h6: keep own size when inheriting */
#define TAGF_INLINE       (1u << 3)  /* default display:inline (like <span>) */
#define TAGF_INLINE_BLOCK (1u << 4)  /* default display:inline-block (like <button>) */
#define TAGF_LIST         (1u << 5)  /* <ul>/<ol>: a list container (marker stack) */

/* Context handed to the parse/setup hooks in cwv_html.c. */
typedef struct TagCtx {
    CwvRenderer   *r;      /* the page being parsed                */
    const char        *tag;    /* lower-case tag name                  */
    const char        *q;      /* start of the tag's text (name+attrs) */
    const char        *gt;     /* pointer to the '>' of the open tag   */
    const char        *end;    /* end of the HTML range being parsed   */
    const CwvNode *parent;  /* parent node (style inheritance)     */
    const char        *next;   /* parse_full hooks: where to continue  */
} TagCtx;

/* Context handed to draw hooks in cwv_backend.c. */
typedef struct TagDrawCtx {
    CwvRenderer *r;
    CwvNode      *n;          /* node being painted (screen coords below) */
    float sx, sy;                 /* top-left of the box on screen           */
    float sw, sh;                 /* box size on screen (zoom applied)       */
    float z;                      /* current zoom factor                     */
    float baseline;               /* suggested text baseline y               */
    unsigned text_color;          /* resolved text color                     */
} TagDrawCtx;

typedef void (*TagParseFn)(TagCtx *ctx);                    /* full custom parse */
typedef void (*TagSetupFn)(TagCtx *ctx, CwvNode *n);     /* node customization */
typedef void (*TagDrawFn)(TagDrawCtx *ctx);                  /* custom painting */

typedef struct TagInfo TagInfo;

/* One row of TAG_TABLE[] - see the registry banner in cwv_html.c.
 * This is the HTML-side extension point: add one row to teach the renderer
 * a new tag, plus (optionally) small hooks for parsing and drawing. */
struct TagInfo {
    const char  *name;
    unsigned    flags;
    float       font_scale_em;     /* 1.0 = body size; h1 = 2.0, ... */
    const char *margin_top_em;    /* e.g. "1em"; NULL = no default margin */
    const char *margin_bottom_em;
    float       margin_right_px;   /* e.g. buttons get 8px of space */
    TagParseFn  parse_full;        /* optional; replaces normal handling */
    TagSetupFn  setup;             /* optional; runs during parsing */
    TagDrawFn   draw;              /* optional; runs during painting */
};

const TagInfo *tag_info(const char *tag);   /* TAG_TABLE[] lookup (NULL=unknown) */
float measure_text_width(CwvRenderer *r, const CwvNode *n, const char *text);
float line_height(const CwvNode *n);
void parse_html(CwvRenderer *r);        /* reset + parse r->html into nodes */
void renderer_map_inline_id(CwvRenderer *r, const char *id, int first_child);

/* ----------------------------------------------------------------------------
 * Resource loading (defined in cwv_backend.c)
 * -------------------------------------------------------------------------- */
void resolve_url(const char *base, const char *ref, char *out, size_t cap);

/* Load any resource the renderer supports: file:/// inside the VPK, or
 * http(s):// via curl with the ux0: cache (see cwv_backend.c).
 * Returns 0 and fills *data and *size (malloc'd, caller frees) or -1. */
int load_resource(CwvRenderer *r, const char *url,
                  unsigned char **data, size_t *size);

/* ---- ES6 source transform hook ------------------------------------------
 * The renderer itself runs ES5.1 only.  The built-in ESM provider
 * (cwv_host_enable_es_builtin, shared by every port) registers an
 * ES6->ES5 transformer here; every script (inline, external, import,
 * eval()) goes through it before execution.  Without a registered hook the
 * renderer behaves exactly like before. */
/* Register the TypeScript stripper (applies to scripts with
 * type="text/typescript" / "application/typescript", or src="*.ts"). */
void js_set_ts_transform(CwvRenderer *r, CwvSourceTransformFn fn,
                        void *userdata);
void js_set_source_transform(CwvRenderer *r, CwvSourceTransformFn fn,
                             void *userdata);

/* ---- Network fetch hook ---------------------------------------------------
 * Pages get a global fetch(url, callback) (see cwv_js.c).  The renderer
 * has no network stack of its own: the host (cwv_host.c on the Vita)
 * registers a hook here that performs the HTTP GET via sceHttp and returns
 * the HTTP status code, with the NUL-terminated body copied into `body`.
 * Return values:  >= 0 = HTTP status (callback receives it);  -1 = failure
 * (no network, bad URL, connection error, timeout) - the callback then gets
 * (-1, "") and the app keeps running.  Returning -1 must be the ONLY
 * observable effect of "no internet". */
/* CwvFetchHook itself is typedef'd in cwv_backend.h, like the
 * other hook types. */
void js_set_fetch_hook(CwvRenderer *r, CwvFetchHook fn, void *userdata);

/* ---- JS prelude hook ------------------------------------------------------
 * The ESM provider also registers a PRELUDE: a piece of ES5.1 source
 * evaluated once, right after the JS context is created on a page, that
 * installs the ES6 LIBRARY directly into the engine (Array.prototype.find,
 * String.prototype.padStart, Object.assign, Map, Set, ...).  That is
 * "direct support, not conversion": these are real functions provided by
 * the prelude, not source rewrites.  Returns NULL = no prelude. */
void js_set_prelude_hook(CwvRenderer *r, CwvJsPreludeFn fn,
                         void *userdata);
void *load_image(CwvRenderer *r, const char *url);
void *font_for(CwvRenderer *r, const char *family);

/* ----------------------------------------------------------------------------
 * JavaScript side (defined in cwv_js.c)
 *
 * Every entry point is safe to call on any renderer: when the page has no
 * <script> and no eval, they are no-ops (the JS context stays NULL).
 * -------------------------------------------------------------------------- */
int  js_is_active(const CwvRenderer *r);            /* JS engine exists? */
void js_harvest_and_run(CwvRenderer *r);            /* after parse_html() */
void js_tick(CwvRenderer *r);                        /* timers, once/frame */
int  js_eval(CwvRenderer *r, const char *javascript);/* run source now */
int  js_has_listener(const CwvRenderer *r, int node_index);
/* Fire the element's click listeners (each gets a small event object).
 * Returns 1 when a listener called event.preventDefault(). */
int  js_notify_activate(CwvRenderer *r, int node_index);
int  js_console_dump(const CwvRenderer *r, char *out, size_t cap); /* logs */
void js_console_stats(const CwvRenderer *r, int *errors, int *warns, int *lines);
/* Add a line to the page console from C (used by the image loader to say
 * WHY an image fell back to its alt-text placeholder).  level: log/info/
 * warn/error/hint - same strings the JS console methods use. */
void js_console_add_line(CwvRenderer *r, const char *level, const char *msg);
const char *js_console_last_error(const CwvRenderer *r);  /* devtools row */
int  js_has_exit_listener(const CwvRenderer *r);
int  js_notify_exit(CwvRenderer *r);                   /* returns 1=handled */
void js_shutdown(CwvRenderer *r);                   /* free the engine */

#endif /* CWV_RENDERER_H */
