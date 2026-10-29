/* ============================================================================
 * FILE ORIENTATION: the CSS half of the web backend.  parse_css reads a
 * stylesheet into a rule registry; the property table maps declarations to
 * node fields.  It never parses HTML and never draws - layout lives in
 * cwv_html.c, painting in cwv_backend.c.  To add a CSS property,
 * add one row to the property table (see docs/EXTENDING_THE_RENDERER.md,
 * Recipe 1).
 * ============================================================================ */

/* ============================================================================
 *  Vita WebView - the CSS engine (cwv_css.c)
 * ============================================================================
 *  Everything on the CSS side of the renderer:
 *
 *    SECTION A  CSS value parsing ("12px", "1.5em", "#a0b1c2" -> numbers)
 *    SECTION B  THE CSS PROPERTY REGISTRY  (extension point #1)
 *              css_prop_*() handlers + the CSS_PROPERTIES[] table
 *    SECTION C  rules, selectors and selector matching
 *
 *  style_apply() is the door everything else uses (style="" attributes, CSS
 *  rules, and JS el.setStyle() all funnel through the same table).
 *  See docs/EXTENDING_THE_RENDERER.md for the "add one table row" recipe.
 * ==========================================================================*/

#include "core/cwv_renderer.h"


/* ============================================================================
 * SECTION 3: CSS VALUE PARSING HELPERS
 * ============================================================================
 * These turn CSS *values* (the right-hand side of "key: value") into numbers
 * the renderer can use. They are used by the property handlers in SECTION 4.
 * ==========================================================================*/

/* Convert a CSS length ("12px", "1.5em", "2rem", "40") to renderer pixels.
 *   px / unitless : the number as-is
 *   em            : the number scaled by the current font size
 *   rem           : the number scaled by the 16px root font
 * The magic 16/0.75 factor converts the CSS world (1em = 16px) into this
 * renderer's units (1em = BODY_FONT_SIZE = 0.75). */
/* Viewport for vw/vh/vmin/vmax and the small/large/dynamic families
 * (2026-10-05 (69)).  The backend publishes the REAL viewport at load
 * time so every port resolves units against its own screen.  On the
 * consoles there is no browser chrome, so small/large/dynamic are all
 * the full viewport - the distinction only matters on desktop windows. */
static float g_css_view_w = 960.0f, g_css_view_h = 544.0f;
void cwv_css_set_viewport(float w, float h) {
    if (w > 1.0f) g_css_view_w = w;
    if (h > 1.0f) g_css_view_h = h;
}
float cwv_css_viewport_width(void) { return g_css_view_w; }
float cwv_css_viewport_height(void) { return g_css_view_h; }

float css_length_px(const char *value, float font_size) {
    char buf[64];
    char *end;
    double number;
    if (!value) return 0.0f;
    snprintf(buf, sizeof(buf), "%s", value);
    trim(buf);
    number = strtod(buf, &end);
    if (end == buf) return 0.0f;           /* not a number at all */
    while (isspace((unsigned char)*end)) end++;
    /* relative font units (check the LONGEST names first so "em" is not
       a prefix mis-hit: these are matched from the full remainder) */
    if (ci_ncmp(end, "rem", 3) == 0)
        return (float)(number * 0.75);        /* 1rem = root font (BODY_FONT_SIZE) */
    if (ci_ncmp(end, "em", 2) == 0)
        return (float)(number * font_size);   /* 1em = current font size */
    /* viewport units: the small/large/dynamic viewport families
       (svh/svw, lvh/lvw, dvh/dvw) all resolve to the full viewport
       here - a console has no shrinking browser chrome, so Chrome's
       s/l/d distinctions are all the same value by definition */
    if (ci_ncmp(end, "svh", 3) == 0 || ci_ncmp(end, "lvh", 3) == 0 ||
        ci_ncmp(end, "dvh", 3) == 0)
        return (float)(number * g_css_view_h / 100.0);
    if (ci_ncmp(end, "svw", 3) == 0 || ci_ncmp(end, "lvw", 3) == 0 ||
        ci_ncmp(end, "dvw", 3) == 0)
        return (float)(number * g_css_view_w / 100.0);
    if (ci_ncmp(end, "vmin", 4) == 0) {
        float m = g_css_view_w < g_css_view_h ? g_css_view_w : g_css_view_h;
        return (float)(number * m / 100.0);
    }
    if (ci_ncmp(end, "vmax", 4) == 0) {
        float m = g_css_view_w > g_css_view_h ? g_css_view_w : g_css_view_h;
        return (float)(number * m / 100.0);
    }
    if (ci_ncmp(end, "vw", 2) == 0)
        return (float)(number * g_css_view_w / 100.0);
    if (ci_ncmp(end, "vh", 2) == 0)
        return (float)(number * g_css_view_h / 100.0);
    /* absolute physical units - CSS reference pixel = 96 per inch */
    if (ci_ncmp(end, "in", 2) == 0)
        return (float)(number * 96.0);
    if (ci_ncmp(end, "cm", 2) == 0)
        return (float)(number * 37.79527559055);   /* 96 / 2.54 */
    if (ci_ncmp(end, "mm", 2) == 0)
        return (float)(number * 3.779527559055);
    if (ci_ncmp(end, "q", 1) == 0)
        return (float)(number * 0.94488188976);    /* quarter-millimeter */
    if (ci_ncmp(end, "pc", 2) == 0)
        return (float)(number * 16.0);              /* 12pt pica */
    if (ci_ncmp(end, "pt", 2) == 0)
        return (float)(number * 96.0 / 72.0);       /* 1pt = 1/72 inch */
    if (ci_ncmp(end, "px", 2) == 0 || *end == 0)
        return (float)number;
    return (float)number;
}

/* Convert a font-size value ("12px", "1.2em", "1.5rem", "24") to renderer
 * font units. Result is constrained by the caller, not here. */
static float css_font_size(const char *value, float parent_size) {
    char buf[64];
    char *end;
    double number;
    if (!value) return parent_size;
    snprintf(buf, sizeof(buf), "%s", value);
    trim(buf);
    number = strtod(buf, &end);
    if (end == buf) return parent_size;
    while (isspace((unsigned char)*end)) end++;
    if (ci_ncmp(end, "em", 2) == 0)
        return (float)(parent_size * number);
    if (ci_ncmp(end, "rem", 3) == 0)
        return (float)(0.75 * number);
    /* Bare numbers and px are treated as pixel sizes; the renderer measures
     * fonts at 32 px per unit (see measure_text_width / draw calls). */
    return (float)(number / 32.0);
}

static float css_length_pct(const char *value);

/* Split a 1-4 item length list ("4px", "1px 2px", "1 2 3", "1 2 3 4") into
 * up to four pixel values, CSS-shorthand style. Returns how many values were
 * found; missing entries are set to 0. */
static int parse_lengths(const char *value, float font_size,
                         float *a, float *b, float *c, float *d) {
    char work[256], *tok;
    float vals[4];
    int count = 0;
    if (!value) return 0;
    snprintf(work, sizeof(work), "%s", value);
    tok = strtok(work, " \t\r\n");
    while (tok && count < 4) {
        float pct = css_length_pct(tok);
        vals[count++] = pct >= 0.0f
            ? g_css_view_w * pct / 100.0f
            : css_length_px(tok, font_size);
        tok = strtok(NULL, " \t\r\n");
    }
    if (a) *a = count > 0 ? vals[0] : 0.0f;
    if (b) *b = count > 1 ? vals[1] : 0.0f;
    if (c) *c = count > 2 ? vals[2] : 0.0f;
    if (d) *d = count > 3 ? vals[3] : 0.0f;
    return count;
}

/* Map an overflow keyword to the renderer's policy code.
 * 0=auto, 1=visible, 2=hidden, 3=scroll. Unknown values stay "auto". */
static int css_overflow_value(const char *v) {
    char b[32];
    if (!v) return 0;
    snprintf(b, sizeof(b), "%s", v);
    trim(b);
    if (ci_eq(b, "hidden"))  return 2;
    if (ci_eq(b, "visible")) return 1;
    if (ci_eq(b, "scroll"))  return 3;
    return 0; /* auto and unknown values use the renderer's automatic policy. */
}

/* Map a border-style keyword to the renderer's style code.
 * 0=none, 1=solid, 2=dashed, 3=dotted, 4=double. */
static int css_border_style_value(const char *v) {
    char b[32];
    if (!v) return 0;
    snprintf(b, sizeof(b), "%s", v);
    trim(b);
    if (ci_eq(b, "none") || ci_eq(b, "hidden")) return 0;
    if (ci_eq(b, "solid"))  return 1;
    if (ci_eq(b, "dashed")) return 2;
    if (ci_eq(b, "dotted")) return 3;
    if (ci_eq(b, "double")) return 4;
    /* Vita2D's primitive renderer cannot reproduce groove/ridge/inset/outset
       faithfully yet; use solid as the closest low-cost representation. */
    if (ci_eq(b, "groove") || ci_eq(b, "ridge") ||
        ci_eq(b, "inset") || ci_eq(b, "outset"))
        return 1;
    return 0;
}

/* Parse the `border` shorthand ("1px solid black"): walks the tokens and
 * classifies each as a style keyword, a width, or a color. */
static int parse_border_shorthand(CwvNode *n, const char *v) {
    char work[256];
    char *tok;
    int found_style = 0, width_seen = 0;
    float width = 0.0f;
    if (!n || !v) return 0;
    snprintf(work, sizeof(work), "%s", v);
    tok = strtok(work, " \t\r\n");
    while (tok) {
        int bs = css_border_style_value(tok);
        if (bs) {
            n->border_style = bs;
            found_style = 1;
        } else {
            char *end = NULL;
            strtod(tok, &end);
            if (end != tok) {
                while (isspace((unsigned char)*end)) end++;
                if (ci_ncmp(end, "px", 2) == 0 || ci_ncmp(end, "em", 2) == 0 ||
                    ci_ncmp(end, "rem", 3) == 0 || *end == 0) {
                    width = css_length_px(tok, n->font_size);
                    width_seen = 1;
                }
            }
            /* A token that parses as a color wins only if it actually changed
               the value (parse_color falls back to the old value otherwise). */
            unsigned old = n->border_color, parsed = parse_color(tok, old);
            if (parsed != old ||
                ci_eq(tok, "black") || ci_eq(tok, "white") || ci_eq(tok, "red") ||
                ci_eq(tok, "green") || ci_eq(tok, "blue") || ci_eq(tok, "orange") ||
                ci_eq(tok, "purple"))
                n->border_color = parsed;
        }
        tok = strtok(NULL, " \t\r\n");
    }
    /* Chrome: an explicit width ALWAYS applies - "border: 0" removes an
       existing border instead of being ignored. */
    if (width_seen) {
        n->border_width = width;
        n->has_border = (width > 0.0f && n->border_style != 0);
    }
    if (found_style) {
        n->has_border = (n->border_style != 0);
        if (n->has_border && n->border_width <= 0) n->border_width = 1.0f;
    }
    return found_style || width_seen;
}

/* ============================================================================
 * SECTION 4: CSS PROPERTY REGISTRY  (extension point #1)
 * ============================================================================
 * style_apply() splits a style string into "key: value" pairs and looks each
 * key up in the CSS_PROPERTIES table below. Each entry points at a small
 * function that knows how to apply that one property to a node.
 *
 * >>> TO ADD A NEW CSS PROPERTY <<<
 *   1. Write a handler:  static void css_prop_myprop(CwvNode *n,
 *                                                     const char *value)
 *      (`n` is the element being styled, `value` is the raw CSS text).
 *   2. Add one line to CSS_PROPERTIES:  { "myprop", css_prop_myprop },
 * Unknown properties are silently ignored, so partial support is fine.
 * ==========================================================================*/

static void css_prop_color(CwvNode *n, const char *v) {
    n->color = parse_color(v, n->color);
    n->color_explicit = 1;
}



/* Percentages for inset offsets resolve against the containing block in CSS.
 * This renderer's current absolute/fixed layout uses the viewport as its
 * containing-block approximation, so use the corresponding viewport axis
 * rather than incorrectly treating "25%" as 25 pixels. */
static float css_position_length(const char *value, float font_size, int vertical) {
    float pct = css_length_pct(value);
    if (pct >= 0.0f)
        return (vertical ? g_css_view_h : g_css_view_w) * pct / 100.0f;
    return css_length_px(value, font_size);
}

/* ---- CSS position keys (2026-10-05 (70)) ------------------------------- */
static void css_prop_position(CwvNode *n, const char *v) {
    if (ci_eq(v, "relative"))       n->pos_kind = 1;
    else if (ci_eq(v, "absolute"))  n->pos_kind = 2;
    else if (ci_eq(v, "fixed"))      n->pos_kind = 3;
    else if (ci_eq(v, "sticky"))     n->pos_kind = 4;   /* renders relative */
    else                             n->pos_kind = 0;   /* static */
}

/* top/right/bottom/left: px or % (percent resolves against the viewport,
 * the engine's approximation of the containing block - documented). */
static void css_prop_pos_top(CwvNode *n, const char *v) {
    n->pos_top = css_position_length(v, n->font_size, 1);
    n->pos_top_set = 1;
}
static void css_prop_pos_right(CwvNode *n, const char *v) {
    n->pos_right = css_position_length(v, n->font_size, 0);
    n->pos_right_set = 1;
}
static void css_prop_pos_bottom(CwvNode *n, const char *v) {
    n->pos_bottom = css_position_length(v, n->font_size, 1);
    n->pos_bottom_set = 1;
}
static void css_prop_pos_left(CwvNode *n, const char *v) {
    n->pos_left = css_position_length(v, n->font_size, 0);
    n->pos_left_set = 1;
}

/* ---- CSS background layer keys (2026-10-05 (68)) -----------------------
 * repeat / position / size / attachment / clip, plus the same keywords
 * picked out of the shorthand.  Position and size keep a px value plus a
 * percent flag: a percent is resolved against (box - image) at DRAW time
 * like Chrome, where 50% centers the image. */
static void css_bg_pos_component(const char *v, float *px, int *pct) {
    if (ci_eq(v, "left") || ci_eq(v, "top"))       { *px = 0.0f; *pct = 1; return; }
    if (ci_eq(v, "center"))                        { *px = 50.0f; *pct = 1; return; }
    if (ci_eq(v, "right") || ci_eq(v, "bottom"))    { *px = 100.0f; *pct = 1; return; }
    if (css_length_pct(v) >= 0.0f) {
        *px = (float)atof(v);
        *pct = 1;
        return;
    }
    *px = css_length_px(v, 1.0f);
    *pct = 0;
}

static void css_prop_background_repeat(CwvNode *n, const char *v) {
    if (ci_eq(v, "no-repeat"))       n->bg_repeat = 1;
    else if (ci_eq(v, "repeat-x"))   n->bg_repeat = 2;
    else if (ci_eq(v, "repeat-y"))   n->bg_repeat = 3;
    else                              n->bg_repeat = 0;  /* repeat, space,
                                       round -> tiled repeat (documented) */
}

static void css_prop_background_position(CwvNode *n, const char *v) {
    char work[128], *tok;
    float x = 0.0f, y = 0.0f;
    int xp = 1, yp = 1, got = 0;
    snprintf(work, sizeof(work), "%s", v);
    tok = strtok(work, " \t");
    while (tok && got < 2) {
        if (got == 0) css_bg_pos_component(tok, &x, &xp);
        else          css_bg_pos_component(tok, &y, &yp);
        got++;
        tok = strtok(NULL, " \t");
    }
    if (got == 1) {
        /* One-value syntax positions the image horizontally, except top and
           bottom which are vertical keywords. CSS defaults the other axis
           to center. */
        if (ci_eq(work, "top") || ci_eq(work, "bottom")) {
            x = 50.0f; xp = 1;
        } else {
            y = 50.0f; yp = 1;
        }
    }
    n->bg_pos_x = x; n->bg_pos_x_pct = xp;
    n->bg_pos_y = y; n->bg_pos_y_pct = yp;
}

static void css_prop_background_size(CwvNode *n, const char *v) {
    char work[128], *tok;
    n->bg_size_cover = n->bg_size_contain = 0;
    n->bg_size_w = n->bg_size_h = 0.0f;
    n->bg_size_w_pct = n->bg_size_h_pct = 0;
    if (ci_eq(v, "cover"))  { n->bg_size_cover = 1; return; }
    if (ci_eq(v, "contain")) { n->bg_size_contain = 1; return; }
    snprintf(work, sizeof(work), "%s", v);
    tok = strtok(work, " \t");
    if (tok) {
        if (css_length_pct(tok) >= 0.0f) {
            n->bg_size_w = (float)atof(tok); n->bg_size_w_pct = 1;
        } else { n->bg_size_w = css_length_px(tok, 1.0f); n->bg_size_w_pct = 0; }
        tok = strtok(NULL, " \t");
        if (tok) {
            if (css_length_pct(tok) >= 0.0f) {
                n->bg_size_h = (float)atof(tok); n->bg_size_h_pct = 1;
            } else { n->bg_size_h = css_length_px(tok, 1.0f); n->bg_size_h_pct = 0; }
        }
    }
}

static void css_prop_background_attachment(CwvNode *n, const char *v) {
    n->bg_attachment_fixed = ci_eq(v, "fixed") ? 1 : 0;
}

static void css_prop_background_clip(CwvNode *n, const char *v) {
    if (ci_eq(v, "padding-box"))       n->bg_clip = 1;
    else if (ci_eq(v, "content-box"))  n->bg_clip = 2;
    else if (ci_eq(v, "border-box"))   n->bg_clip = 0;
}

static void css_prop_background_origin(CwvNode *n, const char *v) {
    if (ci_eq(v, "border-box"))       n->bg_origin = 1;
    else if (ci_eq(v, "content-box")) n->bg_origin = 2;
    else if (ci_eq(v, "padding-box")) n->bg_origin = 0;
}

/* the shorthand "background:" also recognizes the layer keywords:
 * background: url(t.png) no-repeat center / cover #ccc */
static void css_bg_shorthand_layers(CwvNode *n, const char *v) {
    /* 2026-10-06 (75): strtok_r was an implicit declaration on the Vita
     * toolchain (it is POSIX, not C99) - split tokens by hand instead,
     * which is portable everywhere the engine builds. */
    char work[256], *tok;
    char *cur = work;
    snprintf(work, sizeof(work), "%s", v);
    while ((tok = cur) != NULL) {
        while (*tok == ' ' || *tok == '\t') tok++;
        cur = tok;
        while (*cur && *cur != ' ' && *cur != '\t') cur++;
        if (*cur) { *cur = 0; cur++; }
        if (!*tok) { if (!*cur) break; continue; }
        if (strstr(tok, "url(") || *tok == '#' || *tok == '"') continue;
        if (ci_eq(tok, "no-repeat") || ci_eq(tok, "repeat") ||
            ci_eq(tok, "repeat-x") || ci_eq(tok, "repeat-y"))
            css_prop_background_repeat(n, tok);
        else if (ci_eq(tok, "cover") || ci_eq(tok, "contain"))
            css_prop_background_size(n, tok);
        else if (ci_eq(tok, "fixed") || ci_eq(tok, "scroll"))
            css_prop_background_attachment(n, tok);
        else if (ci_eq(tok, "left") || ci_eq(tok, "center") ||
                 ci_eq(tok, "right") || ci_eq(tok, "top") ||
                 ci_eq(tok, "bottom") || *tok == '-' ||
                 (isdigit((unsigned char)*tok) && !strstr(tok, "px")))
            /* "center / cover" and "10px 20px": position word(s) */
            css_prop_background_position(n, tok);
    }
}

/* Extract url(...) from a background value into the node's raw
 * bg_image_src.  The URL may be quoted (single or double) or bare; it is
 * stored UNRESOLVED - the HTML parse step resolves it against the page
 * URL and loads the texture, so CSS stays renderer-free. */
static void css_bg_url(CwvNode *n, const char *v) {
    const char *u = strstr(v, "url(");
    size_t i = 0;
    if (!u) return;
    u += 4;
    while (*u == ' ') u++;
    char q = (*u == '"' || *u == '\'') ? *u++ : 0;
    while (*u && (q ? *u != q : *u != ')') &&
           i < sizeof(n->bg_image_src) - 1)
        n->bg_image_src[i++] = *u++;
    n->bg_image_src[i] = 0;
}

static void css_prop_background_image(CwvNode *n, const char *v) {
    /* background-image: url(...).  The color side of the background is
       untouched; if the load fails, load_image's diagnostic lands in the
       console and whatever solid color was set shows through, exactly
       like Chrome's transparent-over-color layering. */
    n->bg_image_src[0] = 0;
    css_bg_url(n, v);
}

static void css_prop_background(CwvNode *n, const char *v) {
    /* A url() inside the shorthand (background: url(x) #color) is the
       image layer; the rest of the value is the color layer. */
    css_bg_url(n, v);
    /* layer keywords in the shorthand (2026-10-05 (68)): repeat /
       attachment / cover-contain / position words */
    css_bg_shorthand_layers(n, v);
    /* For now the native backend treats a background declaration as a solid
       color for the COLOR layer. Ignore gradient portions instead of losing
       a valid color entirely. */
    char bg[128];
    /* Skip the url(...) portion: with "url(x) #cfe8ff" the color lives in
       the REMAINDER, so chop the url out before taking the color word. */
    const char *cu = strstr(v, "url(");
    const char *cend = cu ? strchr(cu, ')') : NULL;
    const char *rest = cend ? cend + 1 : v;
    while (*rest == ' ' || *rest == '\t') rest++;
    snprintf(bg, sizeof(bg), "%s", cu && *rest ? rest : v);
    char *sp = strpbrk(bg, " \t");
    if (sp) *sp = 0;
    unsigned parsed = parse_color(bg, n->background);
    if (parsed != n->background || ci_eq(bg, "transparent")) {
        n->background = parsed;
        n->has_background = 1;
    }
}

static void css_prop_border(CwvNode *n, const char *v) {
    parse_border_shorthand(n, v);
}

static void css_prop_border_width(CwvNode *n, const char *v) {
    float x = css_length_px(v, n->font_size);
    if (x < 0 || x >= 128) return;
    n->border_width = x;
    if (x > 0) {
        n->has_border = 1;
        if (n->border_style == 0) n->border_style = 1;
    } else {
        n->has_border = 0;   /* Chrome: border-width: 0 hides the border */
    }
}

static void css_prop_border_style(CwvNode *n, const char *v) {
    int bs = css_border_style_value(v);
    n->border_style = bs;
    n->has_border = (bs != 0);
    if (bs && n->border_width <= 0) n->border_width = 1.0f;
}

static void css_prop_border_color(CwvNode *n, const char *v) {
    n->border_color = parse_color(v, n->border_color);
    n->has_border = 1;
    if (n->border_style == 0) n->border_style = 1;
}

static void css_prop_font_weight(CwvNode *n, const char *v) {
    n->bold = ci_ncmp(v, "bold", 4) == 0 || atoi(v) >= 600;
}

static void css_prop_text_align(CwvNode *n, const char *v) {
    n->centered = ci_eq(v, "center");
    n->right_aligned = ci_eq(v, "right") || ci_eq(v, "end");
    /* left/start are the initial LTR behavior. Full justify spacing and RTL
       direction are not implemented by this renderer. */
}

static void css_prop_overflow(CwvNode *n, const char *v) {
    int ov = css_overflow_value(v);
    n->overflow_x = ov;
    n->overflow_y = ov;
    n->overflow_explicit = 1;   /* (75): explicit boxes may scroll */
}

static void css_prop_overflow_x(CwvNode *n, const char *v) {
    n->overflow_x = css_overflow_value(v);
    n->overflow_explicit = 1;   /* (75) */
}

static void css_prop_overflow_y(CwvNode *n, const char *v) {
    n->overflow_y = css_overflow_value(v);
    n->overflow_explicit = 1;   /* (75) */
}

static void css_prop_display(CwvNode *n, const char *v) {
    n->display_explicit = 1;
    if (ci_ncmp(v, "none", 4) == 0) {
        n->display_none = 1;
    } else if (ci_ncmp(v, "inline-block", 12) == 0) {
        n->display_none = 0; n->display_mode = 2;
    } else if (ci_ncmp(v, "inline", 6) == 0) {
        n->display_none = 0; n->display_mode = 1;
    } else if (ci_ncmp(v, "block", 5) == 0) {
        n->display_none = 0; n->display_mode = 0;
    }
}

/* Percentage length ("80%" -> 80.0, "0%" -> 0.0, no '%' -> -1).
 * Percentages cannot be resolved at declaration time (the containing
 * block is not known yet), so the raw number is kept and the block
 * entry code (parse_html_range) turns it into pixels. */
static float css_length_pct(const char *value) {
    char buf[64];
    char *end;
    double number;
    if (!value) return -1.0f;
    snprintf(buf, sizeof(buf), "%s", value);
    trim(buf);
    number = strtod(buf, &end);
    if (end == buf) return -1.0f;
    while (isspace((unsigned char)*end)) end++;
    if (*end == '%' && (end[1] == 0 || isspace((unsigned char)end[1])))
        return (float)number;
    return -1.0f;
}

static float css_box_length(const char *value, float font_size, int vertical) {
    float pct = css_length_pct(value);
    if (pct >= 0.0f)
        return (vertical ? g_css_view_h : g_css_view_w) * pct / 100.0f;
    return css_length_px(value, font_size);
}

static void css_prop_width(CwvNode *n, const char *v) {
    float pct = css_length_pct(v);
    if (pct >= 0.0f) {                 /* "80%" (0% is a valid collapse) */
        n->w_pct = pct;
        n->w = 0.0f;
        n->has_w = 1;
        return;
    }
    float x = css_length_px(v, n->font_size);
    if (x >= 0 && x < 4096) {          /* width: 0 is valid (collapses) */
        n->w = x;
        n->w_pct = 0.0f;
        n->has_w = 1;
    }
}

static void css_prop_height(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 1);
    if (x >= 0 && x < 4096) n->h = x;
}

static void css_prop_margin_top(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x > -1024 && x < 1024) n->margin_top = x;   /* negatives are legal */
}

static void css_prop_margin_left(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x > -1024 && x < 1024) n->margin_left = x;
}

static void css_prop_margin_right(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x > -1024 && x < 1024) n->margin_right = x;
}

static void css_prop_margin_bottom(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x > -1024 && x < 1024) n->margin_bottom = x;
}

static void css_prop_padding_left(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x >= 0 && x < 1024) n->padding_left = x;
}

static void css_prop_padding_right(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x >= 0 && x < 1024) n->padding_right = x;
}

static void css_prop_padding_top(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x >= 0 && x < 1024) n->padding_top = x;
}

static void css_prop_padding_bottom(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x >= 0 && x < 1024) n->padding_bottom = x;
}

/* The 1-4 value margin shorthand, Chrome order: top right bottom left.
 * Negative margins are legal and the layout already models left/right. */
static void css_prop_margin(CwvNode *n, const char *v) {
    float a = 0, b = 0, c = 0, d = 0;
    int ok_a, ok_b, ok_c, ok_d;
    int cnt = parse_lengths(v, n->font_size, &a, &b, &c, &d);
    if (cnt < 1) return;
    ok_a = a > -1024 && a < 1024; ok_b = b > -1024 && b < 1024;
    ok_c = c > -1024 && c < 1024; ok_d = d > -1024 && d < 1024;
    if (cnt == 1) {
        if (ok_a) {
            n->margin_top = n->margin_bottom = a;
            n->margin_left = n->margin_right = a;
        }
    } else {
        if (ok_a) n->margin_top = a;
        if (ok_c) n->margin_bottom = c; else if (ok_a) n->margin_bottom = a;
        if (ok_b) n->margin_right = b; else if (ok_a) n->margin_right = a;
        if (ok_d) n->margin_left = d; else if (ok_b) n->margin_left = b;
    }
}

/* The 1-4 value padding shorthand (top right bottom left). */
static void css_prop_padding(CwvNode *n, const char *v) {
    float a = 0, b = 0, c = 0, d = 0;
    int cnt = parse_lengths(v, n->font_size, &a, &b, &c, &d);
    if (cnt == 1) {
        if (a >= 0 && a < 1024)
            n->padding_left = n->padding_right = n->padding_top = n->padding_bottom = a;
    } else if (cnt == 2) {
        if (a >= 0 && a < 1024) n->padding_top = n->padding_bottom = a;
        if (b >= 0 && b < 1024) n->padding_left = n->padding_right = b;
    } else if (cnt == 3) {
        if (a >= 0 && a < 1024) n->padding_top = a;
        if (b >= 0 && b < 1024) n->padding_left = n->padding_right = b;
        if (c >= 0 && c < 1024) n->padding_bottom = c;
    } else if (cnt >= 4) {
        if (a >= 0 && a < 1024) n->padding_top = a;
        if (b >= 0 && b < 1024) n->padding_right = b;
        if (c >= 0 && c < 1024) n->padding_bottom = c;
        if (d >= 0 && d < 1024) n->padding_left = d;
    }
}

static void css_prop_z_index(CwvNode *n, const char *v) {
    /* Chrome stacks z-index at paint time; the renderer collects nodes in
       DOM order and stable-sorts on this field while drawing. */
    int z = atoi(v);
    if (z >= -1000 && z <= 1000) n->z_index = z;
}

static void css_prop_image_rendering(CwvNode *n, const char *v) {
    /* Chrome's two useful values: auto = smooth linear sampling when the
       image is scaled, pixelated = hard nearest-neighbor pixels (retro
       art, pixel fonts).  crisp-edges is the CSS alias for the same idea. */
    if (!strcmp(v, "pixelated") || !strcmp(v, "crisp-edges"))
        n->image_rendering = 1;
    else
        n->image_rendering = 0;   /* auto + anything unknown */
}

static void css_prop_list_style(CwvNode *n, const char *v) {
    /* Marker kinds.  0 stays "unset" so an <li> without its own rule
       inherits the enclosing list's choice instead of a hard default. */
    if (!strcmp(v, "disc")) n->list_style = 1;
    else if (!strcmp(v, "circle")) n->list_style = 2;
    else if (!strcmp(v, "square")) n->list_style = 3;
    else if (!strcmp(v, "decimal")) n->list_style = 4;
    else if (!strcmp(v, "none")) n->list_style = 9;
}

static void css_prop_object_fit(CwvNode *n, const char *v) {
    /* fill (default) stretches the texture to the box like <img> does in
       Chrome; contain letterboxes and cover crops, both keeping the image
       aspect ratio.  Anything unrecognised behaves as fill. */
    if (!strcmp(v, "contain")) n->object_fit = 1;
    else if (!strcmp(v, "cover")) n->object_fit = 2;
    else n->object_fit = 0;
}

static void css_prop_font_size(CwvNode *n, const char *v) {
    float x = css_font_size(v, n->font_size);
    if (x > 0.1f && x < 8.0f) n->font_size = x;
}

static void css_prop_font_family(CwvNode *n, const char *v) {
    /* Strip the quotes CSS allows around family names. */
    char *q = (char *)v;
    while (*q == '"' || *q == '\'' || isspace((unsigned char)*q)) q++;
    char *e = q + strlen(q);
    while (e > q && (isspace((unsigned char)e[-1]) || e[-1] == '"' || e[-1] == '\'')) e--;
    size_t l = (size_t)(e - q);
    if (l >= sizeof(n->font_family)) l = sizeof(n->font_family) - 1;
    memcpy(n->font_family, q, l);
    n->font_family[l] = 0;
}

/* CSS cursor (2026-09-26 (45)): a keyword, a url() list, or a url()
 * followed by a hotspot.  Keywords map to the CWV_CSTYLE_* enum; the
 * pack art lives at <pkg>/assets/cursors/<name>.png on the port side.
 * url() art loads through the SAME machinery as background-image
 * (local file or the fetch hook for http/https), so internet cursors
 * behave like local ones.  Multiple values are allowed by CSS
 * (fallbacks): the FIRST url() that loads wins for drawing, but a
 * trailing keyword (e.g. "url(x.cur), pointer") is honored when the
 * url() fails to load at parse time - that fallback keyword is kept
 * in cursor_url style fashion: we store it as cursor_style and mark
 * the URL.  Simplification kept: first url() + optional "hx hy". */
static void css_prop_cursor(CwvNode *n, const char *v) {
    static const struct { const char *name; int style; } map[] = {
        { "auto",         CWV_CSTYLE_AUTO },
        { "default",      CWV_CSTYLE_DEFAULT },
        { "none",         CWV_CSTYLE_NONE },
        { "pointer",      CWV_CSTYLE_POINTER },
        { "text",         CWV_CSTYLE_TEXT },
        { "wait",         CWV_CSTYLE_WAIT },
        { "crosshair",    CWV_CSTYLE_CROSSHAIR },
        { "help",         CWV_CSTYLE_HELP },
        { "move",         CWV_CSTYLE_MOVE },
        { "grab",         CWV_CSTYLE_GRAB },
        { "grabbing",     CWV_CSTYLE_GRABBING },
        { "not-allowed",  CWV_CSTYLE_NOT_ALLOWED },
        { "zoom-in",      CWV_CSTYLE_ZOOM_IN },
        { "zoom-out",     CWV_CSTYLE_ZOOM_OUT },
    };
    const char *u = strstr(v, "url(");
    int hx = 0, hy = 0, have_hot = 0;
    /* trailing fallback keyword after the url(): "url(a.cur), pointer" */
    const char *fb = u ? strchr(u, ')') : NULL;
    size_t i;

    n->cursor_url[0] = 0;
    n->cursor_img = NULL;
    n->cursor_fallback = CWV_CSTYLE_AUTO;
    n->cursor_style = CWV_CSTYLE_AUTO;
    if (u) {
        const char *p = u + 4;
        char q = 0;
        size_t j = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"' || *p == '\'') q = *p++;
        while (*p && (q ? *p != q : *p != ')') &&
               j < sizeof(n->cursor_url) - 1)
            n->cursor_url[j++] = *p++;
        n->cursor_url[j] = 0;
        /* CSS allows "url(x.png) hx hy" - the hotspot INSIDE the art
           that anchors to the pointer position (default 0,0 = top-left). */
        if (p) {
            const char *after = q ? p + 1 : p;
            if (after && *after == ')') after++;
            while (after && (*after == ' ' || *after == ',' ||
                             *after == '\t')) after++;
            if (after && (sscanf(after, "%d %d", &hx, &hy) == 2 ||
                          sscanf(after, "%d,%d", &hx, &hy) == 2))
                have_hot = 1;
        }
        n->cursor_style = CWV_CSTYLE_URL;
        n->cursor_hot_x = have_hot ? hx : 0;
        n->cursor_hot_y = have_hot ? hy : 0;
        /* a later bare keyword in the same declaration is the fallback */
        if (fb) {
            char rest[128];
            snprintf(rest, sizeof(rest), "%s", fb + 1);
            for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
                if (strstr(rest, map[i].name)) {
                    n->cursor_fallback = map[i].style;
                    break;
                }
        }
        return;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (!strcmp(v, map[i].name)) {
            n->cursor_style = map[i].style;
            return;
        }
    }
    /* unknown value: leave AUTO (a browser falls back to auto) */
}

/* ---- CSS typography batch (2026-10-05 (65)) --------------------------
 * text-decoration / text-transform / letter-spacing / word-spacing /
 * line-height / white-space / opacity / visibility / min-width /
 * max-width / box-sizing - the most-used Chrome properties the engine
 * did not support yet.  Values are baked onto the node at parse time. */

/* text-decoration: none | underline | overline | line-through (the CSS
 * shorthand also carries colors/styles - ignored, engine draws text
 * colored lines).  Keywords may combine; a bare "none" clears. */
static void css_prop_text_decoration(CwvNode *n, const char *v) {
    if (ci_ncmp(v, "none", 4) == 0) { n->deco = 0; return; }
    {
        int d = 0;
        if (strstr(v, "underline"))     d |= 1;
        if (strstr(v, "line-through"))   d |= 2;
        if (strstr(v, "overline"))       d |= 4;
        n->deco = d;
    }
}

/* text-transform: none | uppercase | lowercase | capitalize */
static void css_prop_text_transform(CwvNode *n, const char *v) {
    if (ci_ncmp(v, "uppercase", 9) == 0)      n->text_transform = 1;
    else if (ci_ncmp(v, "lowercase", 9) == 0) n->text_transform = 2;
    else if (ci_ncmp(v, "capitalize", 10) == 0) n->text_transform = 3;
    else                                      n->text_transform = 0;
}

/* letter-spacing / word-spacing: normal | <length> (negative is legal) */
static void css_prop_letter_spacing(CwvNode *n, const char *v) {
    if (ci_ncmp(v, "normal", 6) == 0) { n->letter_spacing = 0.0f; return; }
    {
        float x = css_length_px(v, n->font_size);
        if (x > -8.0f && x < 64.0f) n->letter_spacing = x;
    }
}

static void css_prop_word_spacing(CwvNode *n, const char *v) {
    if (ci_ncmp(v, "normal", 6) == 0) { n->word_spacing = 0.0f; return; }
    {
        float x = css_length_px(v, n->font_size);
        if (x > -32.0f && x < 128.0f) n->word_spacing = x;
    }
}

/* line-height: normal | <number> | <length> | <percentage>.
 * The engine's line box is font_size * 28 renderer px, so the factor
 * scales THAT - a factor of 1.5 gives the familiar 1.5 line box. */
static void css_prop_line_height(CwvNode *n, const char *v) {
    char buf[64];
    char *end;
    double number;
    if (ci_ncmp(v, "normal", 6) == 0) { n->line_height_mul = 1.0f; return; }
    snprintf(buf, sizeof(buf), "%s", v);
    trim(buf);
    number = strtod(buf, &end);
    if (end == buf) return;
    while (isspace((unsigned char)*end)) end++;
    if (ci_ncmp(end, "%", 1) == 0)
        n->line_height_mul = (float)(number / 100.0);
    else if (ci_ncmp(end, "px", 2) == 0) {
        /* absolute: convert to a factor of this node's line box */
        float box = n->font_size * 28.0f;
        n->line_height_mul = box > 0.5f ? (float)(number / box) : 1.0f;
    }
    else /* unitless number and em both act as the factor (CSS numbers
            are the recommended form and the most common by far) */
        n->line_height_mul = (float)number;
    if (n->line_height_mul < 0.1f) n->line_height_mul = 0.1f;
    if (n->line_height_mul > 8.0f) n->line_height_mul = 8.0f;
}

/* white-space: normal | nowrap | pre (pre-wrap/pre-line are accepted and
 * mapped to their nearest supported cousin). */
static void css_prop_white_space(CwvNode *n, const char *v) {
    if (ci_eq(v, "nowrap"))      n->white_space = 1;
    else if (ci_eq(v, "pre"))    n->white_space = 2;
    else if (ci_eq(v, "pre-wrap")) n->white_space = 3;
    else if (ci_eq(v, "pre-line")) n->white_space = 4;
    else                           n->white_space = 0; /* normal */
}

/* opacity: <number 0..1> | <percentage>.  NOT inherited as a value - the
 * GROUP multiplies, so the handler multiplies the already-accumulated
 * ancestor product (node_from_parent copies eff_opacity down). */
static void css_prop_opacity(CwvNode *n, const char *v) {
    char buf[64];
    char *end;
    double number;
    snprintf(buf, sizeof(buf), "%s", v);
    trim(buf);
    number = strtod(buf, &end);
    if (end == buf) return;
    while (isspace((unsigned char)*end)) end++;
    if (ci_ncmp(end, "%", 1) == 0) number /= 100.0;
    if (number < 0.0) number = 0.0;
    if (number > 1.0) number = 1.0;
    n->eff_opacity *= (float)number;
}

/* visibility: visible | hidden | collapse (== hidden for boxes) | inherit */
static void css_prop_visibility(CwvNode *n, const char *v) {
    if (ci_eq(v, "inherit")) return; /* inherited state was copied at node creation */
    if (ci_eq(v, "hidden") || ci_eq(v, "collapse"))
        n->visibility_hidden = 1;
    else if (ci_eq(v, "visible"))
        n->visibility_hidden = 0;
}

/* min-width / max-width: block-level clamps resolved against the flow in
 * parse_html_range (they also apply to an auto-width block, like CSS). */
static void css_prop_min_width(CwvNode *n, const char *v) {
    float x = css_box_length(v, n->font_size, 0);
    if (x >= 0 && x < 4096) n->min_w = x;
}

static void css_prop_max_width(CwvNode *n, const char *v) {
    if (ci_ncmp(v, "none", 4) == 0) { n->max_w = 0.0f; return; }
    {
        float x = css_box_length(v, n->font_size, 0);
        if (x > 0 && x < 4096) n->max_w = x;
    }
}

/* box-sizing: border-box is the ENGINE's native model (an explicit width
 * paints the border box); content-box opts into the CSS default where
 * the width is the CONTENT box and padding/border widen the paint. */
static void css_prop_box_sizing(CwvNode *n, const char *v) {
    if (ci_eq(v, "content-box")) n->box_sizing = 1;
    else if (ci_eq(v, "border-box")) n->box_sizing = 0;
}

/* ---- THE TABLE: add one line per supported CSS property -------------- */
typedef void (*CssPropFn)(CwvNode *n, const char *value);

typedef struct {
    const char *name;   /* lower-case CSS property name (keys arrive lowered) */
    CssPropFn apply;    /* function that applies the value to the node */
} CssPropertyMap;

static const CssPropertyMap CSS_PROPERTIES[] = {
    { "color",           css_prop_color },
    { "cursor",          css_prop_cursor },
    { "background",      css_prop_background },
    { "position", css_prop_position },
    { "top",      css_prop_pos_top },
    { "right",    css_prop_pos_right },
    { "bottom",   css_prop_pos_bottom },
    { "left",     css_prop_pos_left },
    { "background-repeat",     css_prop_background_repeat },
    { "background-position",   css_prop_background_position },
    { "background-size",      css_prop_background_size },
    { "background-attachment",css_prop_background_attachment },
    { "background-clip",      css_prop_background_clip },
    { "background-origin",    css_prop_background_origin },
    { "background-color", css_prop_background },
    { "background-image", css_prop_background_image },
    { "border",          css_prop_border },
    { "border-width",    css_prop_border_width },
    { "margin-left",     css_prop_margin_left },
    { "margin-right",    css_prop_margin_right },
    { "border-style",    css_prop_border_style },
    { "border-color",    css_prop_border_color },
    { "font-weight",     css_prop_font_weight },
    { "text-align",      css_prop_text_align },
    { "overflow",        css_prop_overflow },
    { "overflow-x",      css_prop_overflow_x },
    { "overflow-y",      css_prop_overflow_y },
    { "display",         css_prop_display },
    { "width",           css_prop_width },
    { "height",          css_prop_height },
    { "margin-top",      css_prop_margin_top },
    { "margin-bottom",   css_prop_margin_bottom },
    { "margin",          css_prop_margin },
    { "padding-left",    css_prop_padding_left },
    { "list-style-type",  css_prop_list_style },
    { "list-style",       css_prop_list_style },   /* shorthand alias */
    { "padding-right",   css_prop_padding_right },
    { "padding-top",     css_prop_padding_top },
    { "padding-bottom",  css_prop_padding_bottom },
    { "padding",         css_prop_padding },
    { "font-size",       css_prop_font_size },
    { "font-family",     css_prop_font_family },
    { "z-index",         css_prop_z_index },
    { "object-fit",      css_prop_object_fit },
    { "image-rendering", css_prop_image_rendering },
    { "text-decoration", css_prop_text_decoration },
    { "text-transform",  css_prop_text_transform },
    { "letter-spacing",  css_prop_letter_spacing },
    { "word-spacing",    css_prop_word_spacing },
    { "line-height",     css_prop_line_height },
    { "white-space",     css_prop_white_space },
    { "opacity",         css_prop_opacity },
    { "visibility",      css_prop_visibility },
    { "min-width",       css_prop_min_width },
    { "max-width",       css_prop_max_width },
    { "box-sizing",      css_prop_box_sizing },
};

/* Linear lookup: property lists are tiny, so a scan is cheaper than a hash. */
static const CssPropertyMap *css_property_find(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(CSS_PROPERTIES) / sizeof(CSS_PROPERTIES[0]); i++)
        if (!strcmp(CSS_PROPERTIES[i].name, name)) return &CSS_PROPERTIES[i];
    return NULL;
}

/* Apply a style="" string or a rule body ("color: red; margin: 4px") to a
 * node: split on ';', split each item on ':', look the key up in the table. */
void style_apply(CwvNode *n, const char *style) {
    char work[2048];
    char *p, *item;
    snprintf(work, sizeof(work), "%s", style);
    p = work;
    while (p) {
        item = p;
        char *semi = strchr(p, ';');
        if (semi) { *semi = 0; p = semi + 1; } else p = NULL;
        char *colon = strchr(item, ':');
        if (!colon) continue;
        *colon = 0;
        char *k = item, *v = colon + 1;
        trim(k); trim(v);
        /* CSS property names are case-insensitive: normalize to lower-case. */
        for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q);
        const CssPropertyMap *m = css_property_find(k);
        if (m) m->apply(n, v);
    }
}

/* style_apply_inline(): the JS-facing entry (setStyle / style.cssText).
 * This engine bakes inherited styles into child nodes at PARSE time
 * (node creation copies the parent's color/bold/align/font_size), so a
 * later `el.style.cssText = "color: green"` on a container is stale: the
 * container's own node repaints but its text children keep the old
 * color.  Real CSS re-resolves inheritance, so after applying the
 * inline style we walk the container's subtree and push the four
 * inherited properties down.  A descendant only takes the new value
 * while it still carries exactly the value it originally inherited
 * (the container's PRE-style value): anything else was set by the
 * descendant's own rule or style attribute and must win, like CSS. */
void style_apply_inline(CwvRenderer *r, CwvNode *root, const char *style)
{
    unsigned pre_color;
    int pre_color_explicit;
    int pre_bold, pre_centered, pre_right_aligned;
    float pre_size;
    int root_i, i;

    if (!root) return;
    pre_color   = root->color;
    pre_color_explicit = root->color_explicit;
    pre_bold    = root->bold;
    pre_centered= root->centered;
    pre_right_aligned = root->right_aligned;
    pre_size    = root->font_size;

    style_apply(root, style);

    if (!r || !r->node_count) return;
    if (root < r->nodes || root >= r->nodes + r->node_count) return;
    root_i = (int)(root - r->nodes);

    for (i = root_i + 1; i < r->node_count; i++) {
        CwvNode *n = &r->nodes[i];
        if (n->depth <= root->depth) break;   /* walked past the subtree */
        if ((root->color != pre_color || root->color_explicit != pre_color_explicit) &&
            !n->color_explicit && n->color == pre_color) {
            n->color = root->color;
            /* The descendant inherits the ancestor's explicit JS color.
             * It is NOT itself explicitly styled: subsequent JS updates
             * to the parent must continue to propagate without a flash. */
            n->color_inherited_explicit =
                root->color_explicit || root->color_inherited_explicit;
        }
        if (n->bold     == pre_bold)      n->bold      = root->bold;
        if (n->centered == pre_centered)  n->centered  = root->centered;
        if (n->right_aligned == pre_right_aligned) n->right_aligned = root->right_aligned;
        if (n->font_size== pre_size)      n->font_size = root->font_size;
    }
}

/* ============================================================================
 * SECTION 5: CSS RULES AND SELECTOR MATCHING
 * ============================================================================
 * A "rule" is one selector + its declarations, exactly as written in the CSS.
 * When an element is parsed, apply_rules() walks every collected rule and
 * applies the ones whose selector matches the element.
 *
 * Supported selector forms (per comma-separated part):
 *   tag            div
 *   .class         .card
 *   #id            #hero
 *   tag.class      span.bad
 *   tag#id         div#hero
 *   tag#id.class   combinations of the above
 *   :active        matches only when the VCM control is active; rules
 *                  WITHOUT :active are skipped while styling the active state
 * ==========================================================================*/

static void add_css_rule(CwvRenderer *r, const char *sel, const char *decl) {
    if (r->rule_count >= VW_MAX_RULES) return;
    snprintf(r->rules[r->rule_count].selector, sizeof(r->rules[0].selector), "%s", sel);
    snprintf(r->rules[r->rule_count].declarations, sizeof(r->rules[0].declarations), "%s", decl);
    trim(r->rules[r->rule_count].selector);
    r->rule_count++;
}

/* Project scroll:off (cwv_project.h, 2026-09-23 (39)): force overflow
 * hidden on the document root.  The backend appends this AFTER the page's
 * own sheets and inline styles, so it wins and every scroll input clamps
 * to 0 - scrolling is impossible, the way a project with "scroll":false
 * wants it.  No-op when the project has scrolling enabled. */
void css_project_force_no_scroll(CwvRenderer *r) {
    add_css_rule(r, "html", "overflow: hidden");
    add_css_rule(r, "body", "overflow: hidden");
}


/* Scan a whole stylesheet's text and collect rules into r->rules.
 * This is a flat brace scanner: find '{', find '}', store the text between. */
/* Strip CSS block comments (and the legacy <!-- --> guards some editors
 * leave inside <style>) into a fresh buffer.  A browser ignores comments
 * ANYWHERE in a sheet - before a selector, between declarations, or at
 * the end.  Without this, a comment before a selector swallowed the whole
 * rule (the selector started with the comment text and never matched). */
static char *css_strip_comments(const char *css) {
    size_t n = strlen(css);
    char *out = malloc(n + 1);
    const char *p = css;
    size_t i = 0;
    if (!out) return NULL;
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
        } else if (strncmp(p, "<!--", 4) == 0) {
            p += 4;
        } else if (strncmp(p, "-->", 3) == 0) {
            p += 3;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = 0;
    return out;
}

void parse_css(CwvRenderer *r, const char *css) {
    char *clean = css_strip_comments(css);
    const char *p;
    if (!clean) return;
    p = clean;
    while (p && *p && r->rule_count < VW_MAX_RULES) {
        const char *a = strchr(p, '{');
        if (!a) break;
        const char *b = strchr(a + 1, '}');
        if (!b) break;

        char sel[128], decl[2048];
        size_t sl = (size_t)(a - p);
        if (sl >= sizeof(sel)) sl = sizeof(sel) - 1;
        memcpy(sel, p, sl);
        sel[sl] = 0;
        size_t dl = (size_t)(b - a - 1);
        if (dl >= sizeof(decl)) dl = sizeof(decl) - 1;
        memcpy(decl, a + 1, dl);
        decl[dl] = 0;

        /* Strip comments. */
        char *c = strstr(sel, "/*");
        if (c) *c = 0;
        trim(sel);
        if (*sel) add_css_rule(r, sel, decl);
        p = b + 1;
    }
    free(clean);
}

/* Does one comma-free selector match this element? (see SECTION 5 header)
 *
 * `active` selects WHICH rules are considered: when styling a VCM control's
 * pressed state (active=1) only :active rules apply; when styling the normal
 * state (active=0) only rules without :active apply. */
static int selector_matches_one(const char *sel, const char *tag,
                                const char *id, const char *cls, int active) {
    /* Most rules are tag-qualified and most nodes do not match that tag.
     * Reject those cheaply before copying/tokenizing the selector. This is
     * especially useful on Vita/Wii, where each node is checked against every
     * collected rule and heap allocation is deliberately avoided. */
    const char *fast = sel;
    size_t fast_len = 0;
    while (*fast && isspace((unsigned char)*fast)) fast++;
    if (*fast && *fast != '.' && *fast != '#') {
        while (fast[fast_len] && fast[fast_len] != '.' &&
               fast[fast_len] != '#' && fast[fast_len] != ':' &&
               !isspace((unsigned char)fast[fast_len])) fast_len++;
        if (fast_len) {
            const char *actual = tag ? tag : "";
            size_t actual_len = strlen(actual), j;
            if (actual_len != fast_len) return 0;
            for (j = 0; j < fast_len; ++j)
                if (tolower((unsigned char)fast[j]) !=
                    tolower((unsigned char)actual[j])) return 0;
        }
    }

    char s[256];
    snprintf(s, sizeof(s), "%s", sel);
    trim(s);

    /* Peel off and validate the :active pseudo-class, if present. */
    int requires_active = 0;
    char *pseudo = strstr(s, ":active");
    if (pseudo) {
        char after[64] = {0};
        snprintf(after, sizeof(after), "%s", pseudo + 7);
        /* ":active" must be the last part (nothing but .class/#id may follow
           and only then via another token - any other trailing text means the
           selector is more complex than this renderer supports). */
        if (after[0] && after[0] != '.' && after[0] != '#' &&
            !isspace((unsigned char)after[0]))
            return 0;
        *pseudo = 0;
        trim(s);
        requires_active = 1;
    }
    if (requires_active && !active) return 0;
    if (!requires_active && active) return 0;

    /* Parse the rest: an optional leading tag name, then .class / #id parts.
     * The LAST .class wins (wanted_cls is overwritten) and the LAST #id wins. */
    char wanted_tag[32] = {0}, wanted_id[64] = {0}, wanted_cls[64] = {0};
    const char *p = s;
    if (*p != '.' && *p != '#') {
        size_t n = 0;
        while (p[n] && p[n] != '.' && p[n] != '#' &&
               !isspace((unsigned char)p[n]) && n < 31) n++;
        memcpy(wanted_tag, p, n);
        wanted_tag[n] = 0;
        p += n;
    }
    while (*p) {
        char kind = *p++;
        char buf[64];
        size_t n = 0;
        while (*p && !isspace((unsigned char)*p) && *p != '.' && *p != '#' && n < 63)
            buf[n++] = *p++;
        buf[n] = 0;
        if (kind == '.')      snprintf(wanted_cls, sizeof(wanted_cls), "%s", buf);
        else if (kind == '#') snprintf(wanted_id, sizeof(wanted_id), "%s", buf);
        else return 0;   /* stray character we do not understand */
    }

    /* All requested parts must match the element. */
    if (wanted_tag[0] && !ci_eq(wanted_tag, tag)) return 0;
    if (wanted_id[0] && strcmp(wanted_id, id ? id : "") != 0) return 0;
    if (wanted_cls[0]) {
        if (!cls) return 0;
        /* The class attribute is a space-separated list; every class must be
           one exact token of it (no prefix matching). */
        char classes[128];
        snprintf(classes, sizeof(classes), "%s", cls);
        char *tok = strtok(classes, " \t\r\n");
        int found = 0;
        while (tok) {
            if (strcmp(tok, wanted_cls) == 0) { found = 1; break; }
            tok = strtok(NULL, " \t\r\n");
        }
        if (!found) return 0;
    }
    return 1;
}

/* A selector list ("h1, .card") matches when any one part matches. */
int selector_matches(const char *sel, const char *tag,
                            const char *id, const char *cls, int active) {
    char list[256];
    snprintf(list, sizeof(list), "%s", sel);
    char *p = list;
    while (p) {
        char *comma = strchr(p, ',');
        if (comma) *comma = 0;
        trim(p);
        if (*p && selector_matches_one(p, tag, id, cls, active)) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

/* Apply every matching rule (in source order) to the node. */
void apply_rules(CwvRenderer *r, CwvNode *n,
                        const char *id, const char *cls, int active) {
    int i;
    for (i = 0; i < r->rule_count; i++)
        if (selector_matches(r->rules[i].selector, n->tag, id, cls, active))
            style_apply(n, r->rules[i].declarations);
}
