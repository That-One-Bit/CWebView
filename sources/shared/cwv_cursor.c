/* cwv_cursor.c - see cwv_cursor.h for the art contract.  Console-neutral:
 * everything goes through the platform seam so the same file builds on
 * Vita, Wii U and Wii without a port header. */
#include <string.h>
#include <stdio.h>

#include "cwv_cursor.h"
#include "cwv_platform.h"
#include "cwv_renderer.h"   /* CWV_CSTYLE_*, CwvCursorInfo */

/* Pack file names, indexed by CWV_CSTYLE_* (0=auto -> unused, and the
 * URL style never uses the pack - the engine hands over loaded art). */
static const char *const PACK_NAMES[16] = {
    NULL,           /* AUTO    */
    "default",      /* DEFAULT */
    "none",         /* NONE (never loaded; hidden, not art) */
    "pointer",      /* POINTER */
    "text",         /* TEXT */
    "wait",         /* WAIT */
    "crosshair",    /* CROSSHAIR */
    "help",         /* HELP */
    "move",         /* MOVE */
    "grab",         /* GRAB */
    "grabbing",     /* GRABBING */
    "not-allowed",  /* NOT_ALLOWED */
    "zoom-in",      /* ZOOM_IN */
    "zoom-out",     /* ZOOM_OUT */
    NULL,           /* URL (engine-provided art) */
    NULL
};

int cwv_cursor_init(CwvCursor *c, const char *console_art_path)
{
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    /* Owner preference (2026-09-26 (45)): center-anchored art, so
     * round cursors are exact.  cwv_cursor_set_hotspot(TIP) restores
     * the old top-left contract for tip-shaped art. */
    c->hotspot = CWV_CURSOR_CENTER;
    if (console_art_path && *console_art_path) {
        c->art = cwv_tex_load_png_file(console_art_path);
        if (c->art) {
            c->art_w = cwv_tex_width(c->art);
            c->art_h = cwv_tex_height(c->art);
            if (c->art_w > 0 && c->art_h > 0) return 0;
            cwv_tex_free(c->art);
            c->art = NULL;
        }
    }
    /* No second, generic path: "app0:" is Vita-only (user direction
     * (33)/(38) - shared code never hardcodes it).  Missing art means
     * the code-drawn arrow below. */
    return -1;
}

void cwv_cursor_destroy(CwvCursor *c)
{
    int i;
    if (!c) return;
    if (c->art) cwv_tex_free(c->art);
    /* Pack art is ours; the URL style art belongs to the ENGINE and is
     * never freed here (style_art only aliases it while active). */
    for (i = 0; i < 16; i++)
        if (c->pack_art[i]) cwv_tex_free(c->pack_art[i]);
    memset(c, 0, sizeof(*c));
}

void cwv_cursor_set_hotspot(CwvCursor *c, int hotspot)
{
    if (c) c->hotspot = (hotspot == CWV_CURSOR_TIP) ? CWV_CURSOR_TIP
                                                    : CWV_CURSOR_CENTER;
}

void cwv_cursor_set_style_dir(CwvCursor *c, const char *dir)
{
    if (!c) return;
    snprintf(c->style_dir, sizeof(c->style_dir), "%s",
             (dir && *dir) ? dir : "");
}

/* Lazily load one pack entry; returns the texture or NULL. */
static void *pack_load(CwvCursor *c, int style)
{
    char path[300];
    void *tex;
    if (style <= CWV_CSTYLE_AUTO || style >= CWV_CSTYLE_URL) return NULL;
    if (!c->style_dir[0] || !PACK_NAMES[style]) return NULL;
    /* Failure caching (2026-09-26): a missing pack file must not turn
     * into per-frame disk I/O - remember attempted styles. */
    if (c->pack_tried & (1u << style)) return NULL;
    snprintf(path, sizeof(path), "%s/%s.png", c->style_dir, PACK_NAMES[style]);
    c->pack_tried |= (1u << style);
    tex = cwv_tex_load_png_file(path);
    if (tex && cwv_tex_width(tex) > 0 && cwv_tex_height(tex) > 0)
        return tex;
    if (tex) { cwv_tex_free(tex); tex = NULL; }
    return NULL;
}

void cwv_cursor_set_style(CwvCursor *c, const void *info_in)
{
    const CwvCursorInfo *info = (const CwvCursorInfo *)info_in;
    int style;

    if (!c) return;
    style = info ? info->style : CWV_CSTYLE_AUTO;
    if (style < CWV_CSTYLE_AUTO || style > CWV_CSTYLE_URL)
        style = CWV_CSTYLE_AUTO;
    if (style == CWV_CSTYLE_DEFAULT) style = CWV_CSTYLE_AUTO;

    c->style_hidden = (style == CWV_CSTYLE_NONE);
    c->style_art = NULL;
    c->style_w = c->style_h = 0;
    c->style_hot_x = c->style_hot_y = 0;
    c->style = style;

    if (style == CWV_CSTYLE_AUTO || style == CWV_CSTYLE_NONE)
        return;

    if (style == CWV_CSTYLE_URL) {
        /* Engine-owned art + its CSS hotspot; draw it, never free it. */
        if (info && info->img) {
            c->style_art = info->img;
            c->style_w = cwv_tex_width(info->img);
            c->style_h = cwv_tex_height(info->img);
            c->style_hot_x = info->hot_x;
            c->style_hot_y = info->hot_y;
            if (c->style_hot_x < 0) c->style_hot_x = 0;
            if (c->style_hot_y < 0) c->style_hot_y = 0;
        }
        /* img == NULL (load failed) -> falls through to the default
         * console art below, like a browser ignoring a broken cursor */
        if (!c->style_art) { c->style = CWV_CSTYLE_AUTO; }
        return;
    }

    /* keyword: pack art, cached; missing art falls back to the console
     * default art / arrow */
    if (!c->pack_art[style])
        c->pack_art[style] = pack_load(c, style);
    if (c->pack_art[style]) {
        c->style_art = c->pack_art[style];
        c->style_w = cwv_tex_width(c->style_art);
        c->style_h = cwv_tex_height(c->style_art);
    } else {
        c->style = CWV_CSTYLE_AUTO;   /* missing pack art -> default */
    }
}

void cwv_cursor_place(CwvCursor *c, float x, float y, int screen_w, int screen_h)
{
    if (!c) return;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (screen_w > 0 && x > (float)screen_w - 1.0f) x = (float)screen_w - 1.0f;
    if (screen_h > 0 && y > (float)screen_h - 1.0f) y = (float)screen_h - 1.0f;
    c->x = x;
    c->y = y;
}

void cwv_cursor_show(CwvCursor *c, int visible)
{
    if (c) c->visible = visible ? 1 : 0;
}

/* Code-drawn fallback arrow: a white triangle with a dark edge, tip at
 * (x, y).  Built from the rect primitive only - every port has it. */
static void cursor_arrow(const CwvCursor *c, unsigned fill, unsigned edge,
                         float grow)
{
    int j;
    for (j = 0; j < 13; j++) {
        float top   = c->y + 2.0f * (float)j - grow;
        float left  = c->x + 0.4f * (float)j - grow;
        float width = 13.0f - 0.5f * (float)j + grow * 2.0f;
        if (width <= 0.0f) break;
        cwv_draw_rectangle(left, top, width, 2.0f + grow * 2.0f, edge);
    }
    for (j = 0; j < 13; j++) {
        float top   = c->y + 2.0f * (float)j;
        float left  = c->x + 0.4f * (float)j;
        float width = 13.0f - 0.5f * (float)j;
        if (width <= 0.0f) break;
        cwv_draw_rectangle(left, top, width, 2.0f, fill);
    }
}

void cwv_cursor_draw(const CwvCursor *c)
{
    const void *art;
    int w, h, hx, hy;

    if (!c || !c->visible) return;
    /* cursor: none - the pointer stays functional but invisible,
     * exactly like a browser. */
    if (c->style_hidden) return;

    art = c->style_art ? c->style_art : c->art;
    if (art) {
        w = c->style_art ? c->style_w : c->art_w;
        h = c->style_art ? c->style_h : c->art_h;
        if (c->style == CWV_CSTYLE_URL) {
            /* custom art anchors at its CSS hotspot */
            hx = c->style_hot_x; hy = c->style_hot_y;
        } else if (c->hotspot == CWV_CURSOR_CENTER) {
            hx = w / 2; hy = h / 2;
        } else {
            hx = 0; hy = 0;
        }
        cwv_draw_texture_scale((void *)art, c->x - (float)hx,
                               c->y - (float)hy, 1.0f, 1.0f);
    } else {
        /* no art anywhere: the code arrow IS the pointer - its tip is
         * always the exact pointer position, hotspot-independent. */
        cursor_arrow(c, RGBA8(250, 250, 250, 255), RGBA8(20, 22, 28, 235), 1.0f);
    }
}

int cwv_cursor_position(const CwvCursor *c, int *x, int *y)
{
    if (!c || !c->visible) return 0;
    if (x) *x = (int)(c->x + 0.5f);
    if (y) *y = (int)(c->y + 0.5f);
    return 1;
}
