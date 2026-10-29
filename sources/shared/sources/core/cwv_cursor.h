#ifndef CWV_CURSOR_H
#define CWV_CURSOR_H

/* Shared pointer cursor for the three console ports (Vita back touchpad,
 * Wii U Wiimote IR, Wii Wiimote IR).
 *
 * Art contract - the owner creates the art; until it exists every port
 * falls back to a code-drawn arrow so the feature works out of the box:
 *   <package>/assets/cursor-<console>.png   console-specific art
 *
 * The build stages that path from (in order): the package.json
 * "cursor" key of the console section (webapp-relative, like "icon"),
 * then the repo's shared assets/cursor-<console>.png.  Missing art
 * falls back to the code-drawn arrow below.
 *
 * HOTSPOT (2026-09-26 (45), owner preference): art is CENTER-anchored -
 * the pointer position is the CENTER of the art, so round cursors are
 * exact (a small circle can be the whole cursor and still hit what you
 * see).  cwv_cursor_set_hotspot(CWV_CURSOR_TIP) restores the old
 * top-left-tip contract for tip-shaped art.  The code-drawn fallback
 * arrow is always tip-anchored: its tip IS the pointer position.
 * The default art is drawn 1:1 in virtual-canvas pixels, so ship it at
 * on-screen size (~24-48px tall).
 *
 * CSS cursor pack (2026-09-26 (45)): the port points the cursor at
 *   <package>/assets/cursors/    (staged by the build scripts)
 * and per frame applies what the page asked for via
 * cwv_cursor_set_style() with the CwvCursorInfo the engine resolved:
 *   default.png pointer.png text.png wait.png crosshair.png help.png
 *   move.png grab.png grabbing.png not-allowed.png zoom-in.png
 *   zoom-out.png
 * Missing pack art falls back to the console art, then the arrow.
 * Pages can also ship custom art: `cursor: url(...)` (local file or
 * http/https) is loaded by the ENGINE and arrives as info->img with an
 * optional CSS hotspot `url(x.png) hx hy`. */

/* Where the art's anchor sits relative to the pointer position. */
enum { CWV_CURSOR_TIP = 0, CWV_CURSOR_CENTER = 1 };

typedef struct CwvCursor {
    float x, y;        /* position in virtual-canvas pixels */
    int   visible;     /* only drawn (and only taps) when nonzero */
    void *art;         /* loaded texture handle; NULL = code arrow */
    int   art_w, art_h;
    int   hotspot;     /* CWV_CURSOR_TIP or CWV_CURSOR_CENTER */

    /* CSS cursor state (2026-09-26 (45)) */
    int   style;         /* CWV_CSTYLE_* currently applied */
    int   style_hidden;  /* cursor: none - invisible but still taps */
    void *style_art;     /* pack/custom art for the current style */
    int   style_w, style_h;
    int   style_hot_x, style_hot_y;  /* URL-cursor CSS hotspot */
    void *pack_art[16];  /* cache: pack_art[CWV_CSTYLE_*], NULL = try load */
    unsigned pack_tried; /* bitmask: styles already attempted (failed loads) */
    char  style_dir[256];  /* <package>/assets/cursors (native path) */
} CwvCursor;

/* Loads the console-specific art and sets the CENTER hotspot (owner
 * preference (45)); CWV_CURSOR_TIP restores the old contract.  Returns
 * 0 when art loaded, -1 otherwise (code-drawn arrow). */
int  cwv_cursor_init(CwvCursor *c, const char *console_art_path);
void cwv_cursor_destroy(CwvCursor *c);

/* Tip (top-left) or center anchoring for the loaded art.  Changing this
 * does not move the pointer position, only where art is drawn around
 * it. */
void cwv_cursor_set_hotspot(CwvCursor *c, int hotspot);

/* Points the style loader at the cursor art pack directory, e.g.
 * "<app root>/assets/cursors" (Wii U/Wii) or "app0:/assets/cursors"
 * (Vita).  Files are <keyword>.png, loaded lazily and cached. */
void cwv_cursor_set_style_dir(CwvCursor *c, const char *dir);

/* Applies the engine-resolved CSS cursor for this frame.  info == NULL
 * or style AUTO/DEFAULT restores the console default art (or arrow).
 * Keyword styles lazily load <style_dir>/<name>.png; URL styles use
 * the engine-owned info->img (drawn with its CSS hotspot) and are
 * never freed here.  When the port's "CSS cursors" setting is off, the
 * port simply always passes NULL. */
void cwv_cursor_set_style(CwvCursor *c, const void *info);

void cwv_cursor_place(CwvCursor *c, float x, float y, int screen_w, int screen_h);
void cwv_cursor_show(CwvCursor *c, int visible);

/* Draws the active style/art (or the fallback arrow) LAST in a frame so
 * it sits above the page and any open menu. */
void cwv_cursor_draw(const CwvCursor *c);

/* Rounded tap position; returns 0 (and leaves x/y alone) when hidden. */
int  cwv_cursor_position(const CwvCursor *c, int *x, int *y);

#endif
