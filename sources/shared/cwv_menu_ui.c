/* cwv_menu_ui.c - the platform-neutral CWMenu shell implementation.
 *
 * Ported from the Vita shell (sources/vita/main.c, 2026-09-21) so the
 * Wii U renders the bundled menu system with the same template geometry.  The drawing layer
 * is a 4-function vtable (CwvMenuPaint); all state, layout, input
 * semantics and action dispatch are shared.  Geometry and colors come
 * from the loaded .cwm script (CWMenu 2), with the same C fallbacks the
 * Vita shell had, so a script-less host still renders.
 *
 * The settings + page-operations menus use the exact devtools visual
 * grammar now (user request 2026-09-21): text tab strip at y=58, the
 * content panel at 20/106/700/270, a 42px row rhythm, and the shared
 * footer-separator + action row at y=382/392.
 */
#include "cwv_menu_ui.h"
#include "cwv_project.h"
#include "cwv_version.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* forward: shared control-mode display name (backend id numbering) */
static const char *cwv_control_mode_name(int mode);


#define RGBA8(r,g,b,a) (((unsigned)(a)<<24)|((unsigned)(b)<<16)|((unsigned)(g)<<8)|(unsigned)(r))

/* vita2d-compatible ABGR, kept byte-identical to the Vita shell so
 * both ports paint the same numbers through their own vtable. */
static unsigned rgba8_channel(unsigned c, int shift)
{
    return (c >> shift) & 0xff;
}

/* ------------------------------------------------------------------ */
/* paint wrappers                                                      */
/* ------------------------------------------------------------------ */

static float menu_scale(const CwvMenuUi *ui)
{
    return ui->scale > 0.0f ? ui->scale : 1.0f;
}
static void paint_rect(const CwvMenuUi *ui, float x, float y, float w, float h,
                       unsigned color)
{
    if (ui->paint && ui->paint->fill_rect) {
        float s = menu_scale(ui);
        ui->paint->fill_rect(x * s, y * s, w * s, h * s, color, ui->paint->ud);
    }
}

static void paint_circle(const CwvMenuUi *ui, float cx, float cy, float r,
                         unsigned color)
{
    if (ui->paint && ui->paint->fill_circle)
        ui->paint->fill_circle(cx, cy, r, color, ui->paint->ud);
}

static void paint_text(const CwvMenuUi *ui, float x, float y, unsigned color,
                       float scale, const char *text)
{
    if (ui->paint && ui->paint->draw_text && text && text[0]) {
        float s = menu_scale(ui);
        ui->paint->draw_text(x * s, y * s, color, scale * s, text,
                             ui->paint->ud);
    }
}

static float paint_text_width(const CwvMenuUi *ui, float scale, const char *text)
{
    if (ui->paint && ui->paint->text_width && text && text[0])
        return ui->paint->text_width(scale * menu_scale(ui), text,
                                     ui->paint->ud);
    return (float)((text ? (int)strlen(text) : 0)) * 8.0f * scale *
           menu_scale(ui);
}

/* Rounded rectangle built from the vtable's fill_rect, matching the
 * Vita's arcless approximation: corner rects carved out of the edge
 * bands, so a port never needs a rounded primitive. */
static void paint_rounded_rect(const CwvMenuUi *ui, float x, float y,
                               float w, float h, float r, unsigned color)
{
    if (r < 2.0f) {
        paint_rect(ui, x, y, w, h, color);
        return;
    }
    if (r * 2.0f > w) r = w * 0.5f;
    if (r * 2.0f > h) r = h * 0.5f;
    paint_rect(ui, x + r, y, w - 2.0f * r, h, color);          /* center band */
    paint_rect(ui, x, y + r, r, h - 2.0f * r, color);         /* left band  */
    paint_rect(ui, x + w - r, y + r, r, h - 2.0f * r, color); /* right band */
    paint_rect(ui, x + r * 0.3f, y, r * 1.4f, r, color);       /* TL -> TR blend */
    paint_rect(ui, x, y + r * 0.3f, r, r * 1.4f, color);
    paint_rect(ui, x + w - r * 1.7f, y, r * 1.4f, r, color);
    paint_rect(ui, x + w - r, y + r * 0.3f, r, r * 1.4f, color);
    paint_rect(ui, x + r * 0.3f, y + h - r, r * 1.4f, r, color);
    paint_rect(ui, x, y + h - r * 1.7f, r, r * 1.4f, color);
    paint_rect(ui, x + w - r * 1.7f, y + h - r, r * 1.4f, r, color);
    paint_rect(ui, x + w - r, y + h - r * 1.7f, r, r * 1.4f, color);
}

/* ------------------------------------------------------------------ */
/* CWMenu theme / template helpers (Vita-parity)                       */
/* ------------------------------------------------------------------ */

static const VitaMenuTemplate *tpl_find(const VitaMenuTemplate *list,
                                       const char *name)
{
    if (!list || !name) return NULL;
    for (const VitaMenuTemplate *t = list; t->name; t++)
        if (!strcmp(t->name, name)) return t;
    return NULL;
}

static int rect_hit(float x, float y, float w, float h, int tx, int ty)
{
    return tx >= (int)x && tx <= (int)(x + w) &&
           ty >= (int)y && ty <= (int)(y + h);
}

static void menu_accent_rgb(int accent, int *rr, int *gg, int *bb)
{
    /* C built-ins are the FALLBACK since CWMenu 2: the `theme` block in
     * the .cwm script owns these via accent0..accent5. */
    static const unsigned char colors[][3] = {
        {70,105,150}, {180,55,60}, {205,112,45},
        {55,145,85}, {45,155,175}, {92,55,135}
    };
    char slot[16];
    int i = (accent < 0 || accent > 5) ? 0 : accent;
    snprintf(slot, sizeof(slot), "accent%d", i);
    if (cwm_theme_get_rgb(slot, rr, gg, bb) == 0) return;
    *rr = colors[i][0]; *gg = colors[i][1]; *bb = colors[i][2];
}

static unsigned menu_theme_color(int accent, float modifier)
{
    int rr, gg, bb;
    menu_accent_rgb(accent, &rr, &gg, &bb);
    if (modifier < 0.0f) modifier = 0.0f;
    if (modifier > 1.0f) modifier = 1.0f;
    return RGBA8((unsigned)(rr * modifier), (unsigned)(gg * modifier),
                 (unsigned)(bb * modifier), 255);
}

static float template_brightness(const VitaMenuTemplate *t)
{
    float b = 0.0f, v;
    if (!t) return 1.0f;
    if (t->brightness_var && cwm_var_get_number(t->brightness_var, &v) == 0) b = v;
    else if (t->brightness > 0.0f) b = t->brightness;
    else {
        switch (t->style) {
            case VITA_MENU_STYLE_LIGHT: b = 1.16f; break;
            case VITA_MENU_STYLE_DIM:   b = 0.62f; break;
            default:                    b = 1.0f;  break;
        }
    }
    if (b < 0.25f) b = 0.25f;
    if (b > 2.0f) b = 2.0f;
    return b;
}

static float template_tint(const VitaMenuTemplate *t)
{
    float tint = 1.0f, v;
    if (!t) return 1.0f;
    if (t->tint_var && cwm_var_get_number(t->tint_var, &v) == 0) tint = v;
    else if (t->tint >= 0.0f) tint = t->tint;
    if (tint < 0.0f) tint = 0.0f;
    if (tint > 1.0f) tint = 1.0f;
    return tint;
}

int cwv_menu_ui_accent_count(void) { return 6; }

const char *cwv_menu_ui_accent_name(int accent)
{
    /* label0..label5 in the CWMenu theme block override the C names. */
    static const char *names[] = {
        "Blue", "Red", "Orange", "Green", "Cyan", "Dark Purple"
    };
    int i = (accent < 0 || accent > 5) ? 0 : accent;
    const char *themed = cwm_theme_get_label(i);
    return themed ? themed : names[i];
}

const char *cwv_menu_ui_scroll_speed_name(int speed)
{
    return speed == 0 ? "Slow" : speed == 1 ? "Normal" : "Fast";
}

/* Theme color of the ACTIVE accent (override-aware brightness/tint). */
static unsigned style_theme_color(const CwvMenuUi *ui,
                                   const VitaMenuTemplate *t, float modifier)
{
    if (!t) return menu_theme_color(ui->settings.accent, modifier);
    modifier *= template_brightness(t);
    if (modifier > 1.0f) modifier = 1.0f;
    if (modifier < 0.0f) modifier = 0.0f;
    return menu_theme_color(ui->settings.accent, modifier);
}

static unsigned style_accent_color(const CwvMenuUi *ui,
                                   const VitaMenuTemplate *t, int active)
{
    float modifier = (active ? 0.82f : 0.34f);
    float tint = template_tint(t);
    unsigned accent, neutral;
    unsigned ar, ag, ab, nr, ng, nb;
    if (t) modifier *= template_brightness(t);
    if (modifier > 1.0f) modifier = 1.0f;
    accent = menu_theme_color(ui->settings.accent, modifier);
    if (tint >= 0.999f) return accent;
    {
        int r = 150, g = 160, b = 175; /* CWMenu theme `neutral` */
        (void)cwm_theme_get_rgb("neutral", &r, &g, &b);
        neutral = RGBA8((unsigned)((float)r * modifier),
                        (unsigned)((float)g * modifier),
                        (unsigned)((float)b * modifier), 255);
    }
    ar = rgba8_channel(accent, 0); ag = rgba8_channel(accent, 8); ab = rgba8_channel(accent, 16);
    nr = rgba8_channel(neutral, 0); ng = rgba8_channel(neutral, 8); nb = rgba8_channel(neutral, 16);
    return RGBA8((unsigned)(nr + (ar - nr) * tint),
                 (unsigned)(ng + (ag - ng) * tint),
                 (unsigned)(nb + (ab - nb) * tint), 255);
}

static unsigned style_text_color(const VitaMenuTemplate *t)
{
    int r, g, b;
    if (!t || t->style == VITA_MENU_STYLE_NORMAL) {
        if (cwm_theme_get_rgb("text-normal", &r, &g, &b) == 0) return RGBA8(r, g, b, 255);
        return RGBA8(235, 240, 250, 255);
    }
    if (t->style == VITA_MENU_STYLE_LIGHT) {
        if (cwm_theme_get_rgb("text-light", &r, &g, &b) == 0) return RGBA8(r, g, b, 255);
        return RGBA8(255, 248, 238, 255);
    }
    if (cwm_theme_get_rgb("text-dim", &r, &g, &b) == 0) return RGBA8(r, g, b, 255);
    return RGBA8(155, 165, 180, 255);
}

static int template_stops(const VitaMenuTemplate *t)
{
    float v;
    int stops = t ? t->stops : 0;
    if (t && t->stops_var && cwm_var_get_number(t->stops_var, &v) == 0)
        stops = (int)(v + 0.5f);
    if (stops <= 0) stops = 3;
    if (stops > 6) stops = 6;
    if (stops < 2) stops = 2;
    return stops;
}

/* Active template for the open mode (Vita registry order preserved). */
static const VitaMenuTemplate *active_template(const CwvMenuUi *ui)
{
    size_t count = 0;
    const char *name = "main";
    if(ui->mode == CWV_MENU_CUSTOM && ui->custom_name[0])
        return vita_native_menu_find(ui->custom_name,&count);
    switch (ui->mode) {
        case CWV_MENU_MAIN:     name = "main"; break;
        case CWV_MENU_PAGEOPS:  name = "page_operations"; break;
        case CWV_MENU_SETTINGS: name = ui->settings_tab == 0
                                     ? "settings_appearance"
                                     : ui->settings_tab == 1
                                     ? "settings_controls"
                                     : "settings_preferences"; break;
        case CWV_MENU_DEVTOOLS: switch (ui->dev_section) {
                    case 0: name = "devtools_performance"; break;
                    case 1: name = "devtools_renderer"; break;
                    case 2: name = "devtools_input"; break;
                    case 3: name = "devtools_page"; break;
                    case 4: name = "devtools_overrides"; break;
                    case 5: name = "devtools_console"; break;
                    case 6: name = "devtools_platform"; break;
                    default: name = "devtools_close"; break;
                } break;
        default: break;
    }
    {
        const VitaMenuTemplate *items = vita_native_menu_find(name, &count);
        if (items) return items;
    }
    return vita_native_menu_find("main", &count);
}

/* ---- Project feature gating (cwv_project.h, 2026-09-23 (39)) -------------
 * A disabled project feature removes its menu rows through BOTH channels:
 * the CWMenu 3 `when <expr>` conditional drops them from a loaded script,
 * and these gates cover the no-script C fallback templates, touch input
 * and action dispatch.  When a feature is enabled the #if bodies compile
 * away entirely. */
static int menu_action_hidden(const char *action)
{
    if (!action || !action[0]) return 0;
#if !CWV_CURSOR_ENABLED
    if (!strcmp(action, "cycle_pointer")) return 1;
#endif
#if !CWV_ZOOM_ENABLED
    if (!strcmp(action, "set_zoom") || !strcmp(action, "cycle_zoom_input") ||
        !strcmp(action, "zoom-in") || !strcmp(action, "zoom-out")) return 1;
#endif
#if !CWV_SCROLL_ENABLED
    if (!strcmp(action, "cycle_scroll_input") ||
        !strcmp(action, "set_scroll_speed")) return 1;
#endif
#if !CWV_DEVTOOLS_ENABLED
    if (!strcmp(action, "devtools")) return 1;
#endif
#if !CWV_HISTORY_ENABLED
    if (!strcmp(action, "history-back") || !strcmp(action, "forward")) return 1;
#endif
    return 0;
}

/* Row-level variant: additionally honors the Vita-only cursor_toggle rule
 * (the shared shell never shows the back-touchpad row on other ports). */
static int menu_row_hidden(const CwvMenuUi *ui, const char *action)
{
    if (action && !strcmp(action, "cycle_pointer") && !ui->cursor_toggle)
        return 1;
    return menu_action_hidden(action);
}

/* Settings rows a project feature can hide live in content-row-N elements;
 * navigation walks only over rows that exist AND are shown. */
static int settings_row_base(int tab)
{
    /* appearance 4 (theme, accent, zoom, CSS cursors (45)),
       controls 4, preferences 5 */
    return tab == 1 ? 4 : tab == 2 ? 5 : 4;
}

static int settings_row_visible(const CwvMenuUi *ui, int tab, int idx)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    char row_name[32];
    const VitaMenuTemplate *t;
    if (idx < 0 || idx >= settings_row_base(tab)) return 0;
    snprintf(row_name, sizeof(row_name), "content-row-%d", idx);
    t = tpl_find(tpl, row_name);
    return t && !menu_row_hidden(ui, t->action);
}

static int settings_row_last_visible(const CwvMenuUi *ui, int tab)
{
    int i, last = -1;
    for (i = 0; i < settings_row_base(tab); i++)
        if (settings_row_visible(ui, tab, i)) last = i;
    return last;
}

/* Window rect from the CWMenu 2 window keys; legacy scripts render at
 * the Vita base anchor 110/54, scale 1.0, no crop. */
static void menu_frame(const VitaMenuTemplate *tpl, float *x, float *y,
                       float *w, float *h)
{
    const VitaMenuTemplate *window = tpl_find(tpl, "window");
    if (window) {
        float scale = window->win_scale > 0.0f ? window->win_scale : 1.0f;
        float cl = window->crop_l, ct = window->crop_t;
        float cr = window->crop_r, cb = window->crop_b;
        if (scale < 0.75f) scale = 0.75f;
        if (scale > 1.25f) scale = 1.25f;
        if (cl < 0.0f) cl = 0.0f;
        if (ct < 0.0f) ct = 0.0f;
        if (cr < 0.0f) cr = 0.0f;
        if (cb < 0.0f) cb = 0.0f;
        *x = (window->win_anchor_set ? window->win_anchor_x : 110.0f)
             + window->x + cl * scale;
        *y = (window->win_anchor_set ? window->win_anchor_y : 54.0f)
             + window->y + ct * scale;
        *w = (window->w - cl - cr) * scale;
        *h = (window->h - ct - cb) * scale;
        if (*w < 40.0f) *w = 40.0f;
        if (*h < 40.0f) *h = 40.0f;
    } else {
        *x = 110.0f; *y = 54.0f; *w = 740.0f; *h = 436.0f;
    }
}

/* Scrollbar element owned by a group ("controls" / "content"). */
static const VitaMenuTemplate *scrollbar_find(const VitaMenuTemplate *tpl,
                                              const char *owner)
{
    const VitaMenuTemplate *legacy = NULL;
    char want[64];
    if (!tpl || !owner) return NULL;
    snprintf(want, sizeof(want), "%s", owner);
    for (const VitaMenuTemplate *t = tpl; t->name; t++) {
        if (t->kind != VITA_MENU_SCROLLBAR) continue;
        if (!strcmp(t->name, want)) return t;
        if (!legacy) legacy = t; /* first scrollbar = fallback */
    }
    return legacy;
}

static void draw_scrollbar(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                           float x, float y, float content_size,
                           float viewport_size, float offset)
{
    CWMenuMenuScrollbar bar;
    unsigned track, thumb;
    float thumb_h, thumb_y;
    if (!t || t->kind != VITA_MENU_SCROLLBAR) return;
    cwm_scrollbar_init(&bar, 24.0f);
    cwm_scrollbar_set(&bar, content_size, viewport_size, offset, t->h);
    if (!cwm_scrollbar_visible(&bar)) return;
    track = style_theme_color(ui, t, 0.16f);
    thumb = style_accent_color(ui, t, 1);
    thumb_h = cwm_scrollbar_thumb_size(&bar);
    thumb_y = y + t->y + cwm_scrollbar_thumb_position(&bar);
    paint_rect(ui, x + t->x, y + t->y, t->w, t->h, track);
    paint_rect(ui, x + t->x, thumb_y, t->w, thumb_h, thumb);
}

static void ui_log(const CwvMenuUi *ui, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (!ui->ops || !ui->ops->log) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui->ops->log(buf, ui->ops->ud);
}

/* ------------------------------------------------------------------ */
/* shared chrome pieces                                                */
/* ------------------------------------------------------------------ */

/* The CWMenu 2 `border` key on text elements (devtools rows): plain
 * text by default; only the sides the script asked for are drawn. */
static void row_borders(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                        float bx, float by, float y_scroll)
{
    if (!t || !t->border_flags) return;
    if (t->border_flags & 16)
        paint_rect(ui, bx, by, t->w, t->h, style_theme_color(ui, t, 0.12f));
    if (t->border_flags & 1)
        paint_rect(ui, bx, by, t->w, 1.0f, style_theme_color(ui, t, 0.55f));
    if (t->border_flags & 4)
        paint_rect(ui, bx, by + t->h - 1.0f, t->w, 1.0f, RGBA8(60, 70, 88, 255));
    if (t->border_flags & 8)
        paint_rect(ui, bx, by, 4.0f, t->h, style_theme_color(ui, t, 0.55f));
    if (t->border_flags & 2)
        paint_rect(ui, bx + t->w - 1.0f, by, 1.0f, t->h, style_theme_color(ui, t, 0.55f));
    (void)y_scroll;
}

/* Underline / separator element. */
static void draw_separator(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                           float x, float y)
{
    if (!t) return;
    paint_rect(ui, x + t->x, y + t->y, t->w, 1.0f,
               t->kind == VITA_MENU_UNDERLINE ? RGBA8(60, 70, 88, 255)
               : style_theme_color(ui, t, 0.4f));
}

/* The devtools content card: panel + top/left accent edges, used by
 * page operations, settings AND devtools - the unified look. */
static void draw_content_panel(const CwvMenuUi *ui,
                               const VitaMenuTemplate *panel,
                               float x, float y)
{
    float cx, cy, cw, ch;
    if (!panel) return;
    cx = x + panel->x; cy = y + panel->y;
    cw = panel->w; ch = panel->h;
    paint_rect(ui, cx, cy, cw, ch, style_theme_color(ui, panel, 0.12f));
    paint_rect(ui, cx, cy, cw, 2.0f, style_theme_color(ui, panel, 0.75f));
    paint_rect(ui, cx, cy, 2.0f, ch, style_theme_color(ui, panel, 0.75f));
}

/* The section-accent decoration (3px accent strip under the tabs). */
static void draw_section_accent(const CwvMenuUi *ui,
                                const VitaMenuTemplate *tpl, float x, float y)
{
    const VitaMenuTemplate *t = tpl_find(tpl, "section-accent");
    if (!t || t->kind != VITA_MENU_DECORATION) return;
    paint_rect(ui, x + t->x, y + t->y, t->w, t->h,
               style_accent_color(ui, t, 1));
}

/* Text tab strip (settings + devtools): plain-text tabs, the active one
 * bright with an accent underline, inactive muted. */
static void draw_tab_strip(const CwvMenuUi *ui, const VitaMenuTemplate *tpl,
                           float x, float y, int tab_count,
                           const char *const *tab_names, int active_tab)
{
    int i;
    for (i = 0; i < tab_count; i++) {
        const VitaMenuTemplate *t = tpl_find(tpl, tab_names[i]);
        float xx, yy, tw;
        if (!t) continue;
        xx = x + t->x; yy = y + t->y;
        tw = paint_text_width(ui, 0.72f, t->label ? t->label : "");
        paint_text(ui, xx + (t->w - tw) * 0.5f, yy + 24.0f,
                   active_tab == i ? RGBA8(255, 255, 255, 255)
                                   : RGBA8(150, 160, 175, 255),
                   0.72f, t->label ? t->label : "");
        if (active_tab == i)
            paint_rect(ui, xx + (t->w - tw) * 0.5f, yy + t->h - 3.0f, tw, 2.0f,
                       style_accent_color(ui, t, 1));
    }
}

/* Devtools-grammar action row: footer-separator + template-positioned
 * buttons with centered labels.  names/actions are parallel arrays. */
static void draw_action_row(const CwvMenuUi *ui, const VitaMenuTemplate *tpl,
                            float x, float y, int count,
                            const char *const *names, int active_index)
{
    int i;
    const VitaMenuTemplate *sep = tpl_find(tpl, "footer-separator");
    draw_separator(ui, sep, x, y);
    for (i = 0; i < count; i++) {
        const VitaMenuTemplate *t = tpl_find(tpl, names[i]);
        float ax, ay, tw;
        if (!t) continue;
        ax = x + t->x; ay = y + t->y;
        /* Unified back-button grammar (2026-10-05): the same accent fill
         * (faded when inactive) + theme 0.92 top/left edges the Vita
         * Settings and Page Operations back buttons use. */
        if (active_index == i) {
            paint_rect(ui, ax, ay, t->w, t->h, style_accent_color(ui, t, 1));
            paint_rect(ui, ax, ay, t->w, 2.0f, style_theme_color(ui, t, 0.92f));
            paint_rect(ui, ax, ay, 2.0f, t->h, style_theme_color(ui, t, 0.92f));
        } else {
            paint_rect(ui, ax, ay, t->w, t->h, style_accent_color(ui, t, 0));
            paint_rect(ui, ax, ay, t->w, 2.0f, style_theme_color(ui, t, 0.92f));
            paint_rect(ui, ax, ay, 2.0f, t->h, style_theme_color(ui, t, 0.92f));
        }
        tw = paint_text_width(ui, 0.68f, t->label ? t->label : "");
        paint_text(ui, ax + (t->w - tw) * 0.5f, ay + 26.0f,
                   style_text_color(t), 0.68f, t->label ? t->label : "");
    }
}

/* ------------------------------------------------------------------ */
/* main menu                                                           */
/* ------------------------------------------------------------------ */

static void draw_main_menu(CwvMenuUi *ui, CwvHost *host, float x, float y)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    const VitaMenuTemplate *t;
    static const char *rows[4] = {"page-operations", "open-url", "settings",
                                  "more"};
    int i;

    /* DevTools chip, top right (removed entirely when the project
     * disables devtools: the script drops it, the fallback gate here). */
    t = tpl_find(tpl, "devtools");
    if (t && !menu_action_hidden(t->action)) {
        float bx = x + t->x, by = y + t->y;
        paint_rect(ui, bx, by, t->w, t->h, style_theme_color(ui, t, 0.34f));
        paint_rect(ui, bx, by, t->w, 2.0f, style_theme_color(ui, t, 0.92f));
        paint_rect(ui, bx, by, 2.0f, t->h, style_theme_color(ui, t, 0.92f));
        paint_text(ui, bx + 12.0f, by + 21.0f, style_text_color(t), 0.70f,
                   t->label ? t->label : "DevTools");
    }

    paint_text(ui, x + 20.0f, y + 82.0f, RGBA8(190, 210, 235, 255), 0.76f, "Page");
    /* page identity card */
    paint_rect(ui, x + 20.0f, y + 94.0f, 70.0f, 70.0f,
               style_theme_color(ui, NULL, 0.25f));
    paint_rect(ui, x + 20.0f, y + 94.0f, 70.0f, 2.0f,
               style_accent_color(ui, NULL, 1));
    if (host && ui->paint && ui->paint->favicon) {
        const char *favicon=cwv_host_favicon_url(host);
        ui->paint->favicon(x+24.0f,y+98.0f,62.0f,62.0f,
                           favicon?favicon:"",ui->paint->ud);
    }
    {
        const char *pt = host ? cwv_host_page_title(host) : "";
        const char *pu = host ? cwv_host_current_url(host) : "";
        char mode_line[64];
        paint_text(ui, x + 104.0f, y + 117.0f, RGBA8(245, 245, 250, 255), 0.84f,
                   (pt && *pt) ? pt : "Untitled");
        paint_text(ui, x + 104.0f, y + 141.0f, RGBA8(185, 195, 210, 255), 0.66f,
                   (pu && *pu) ? pu : "about:blank");
        snprintf(mode_line, sizeof(mode_line), "Control mode: %s",
                 cwv_control_mode_name(host ? cwv_host_control_mode(host) : 0));
        paint_text(ui, x + 104.0f, y + 162.0f, RGBA8(185, 195, 210, 255), 0.66f,
                   mode_line);
        /* (81) internal CWebView version + the app's own manifest version
         * beside it in bright white (Vita main-menu parity). */
        snprintf(mode_line, sizeof(mode_line), "Version: %s", CWV_VERSION_DISPLAY);
        paint_text(ui, x + 420.0f, y + 162.0f, RGBA8(155, 165, 180, 255), 0.62f,
                   mode_line);
        {
            char app_line[96];
            float vw = paint_text_width(ui, 0.62f, mode_line);
            snprintf(app_line, sizeof(app_line), "  App: %s", CWV_APP_VERSION);
            paint_text(ui, x + 420.0f + vw, y + 162.0f,
                       RGBA8(255, 255, 255, 255), 0.62f, app_line);
        }
    }

    draw_separator(ui, tpl_find(tpl, "page-separator"), x, y);

    /* four large action rows (More = CWMenu 4 linked submenu) */
    for (i = 0; i < 4; i++) {
        const VitaMenuTemplate *rt = tpl_find(tpl, rows[i]);
        int active;
        float rx, ry;
        if (!rt) continue;
        if (menu_row_hidden(ui, rt->action)) continue;   /* project-gated */
        active = (ui->main_selected == i);
        rx = x + rt->x; ry = y + rt->y;
        /* Unified row grammar (2026-10-05): identical chrome to the
         * Settings/DevTools and Page Operations rows - selected row gets
         * theme fill + 4px accent bar, every row a 1px separator. */
        if (active) {
            paint_rect(ui, rx, ry, rt->w, rt->h, style_theme_color(ui, rt, 0.23f));
            paint_rect(ui, rx, ry, 4.0f, rt->h, style_accent_color(ui, rt, 1));
        }
        paint_rect(ui, rx, ry + rt->h - 1.0f, rt->w, 1.0f, RGBA8(60, 70, 88, 255));
        paint_text(ui, rx + 16.0f, ry + 25.0f, style_text_color(rt), 0.72f,
                   rt->label ? rt->label : "");
    }

    draw_separator(ui, tpl_find(tpl, "footer-separator"), x, y);
    t = tpl_find(tpl, "back");
    if (t) {
        int active = (ui->main_selected == 4);
        float bw;
        paint_rect(ui, x + t->x, y + t->y, t->w, t->h,
                   style_accent_color(ui, t, active));
        paint_rect(ui, x + t->x, y + t->y, t->w, 2.0f, style_theme_color(ui, t, 0.92f));
        paint_rect(ui, x + t->x, y + t->y, 2.0f, t->h, style_theme_color(ui, t, 0.92f));
        bw = paint_text_width(ui, 0.68f, t->label ? t->label : "");
        paint_text(ui, x + t->x + (t->w - bw) * 0.5f, y + t->y + 25.0f,
                   RGBA8(255, 255, 255, 255), 0.68f, t->label ? t->label : "");
    }
    t = tpl_find(tpl, "footer");
    if (t) {
        float fy = y + t->y;
        paint_text(ui, x + t->x + 24.0f, fy + 25.0f, RGBA8(245, 245, 250, 255), 0.66f, "A Select");
        paint_text(ui, x + t->x + 180.0f, fy + 25.0f, RGBA8(245, 245, 250, 255), 0.66f, "B Close");
        if (ui->script_error[0])
            paint_text(ui, x + t->x + 330.0f, fy + 25.0f, RGBA8(232, 132, 112, 255), 0.66f,
                       ui->script_error);
        else
            paint_text(ui, x + t->x + 330.0f, fy + 25.0f, RGBA8(245, 245, 250, 255), 0.66f, "Top-right DevTools");
    }
}

/* ------------------------------------------------------------------ */
/* page operations (devtools grammar, template-driven)                  */
/* ------------------------------------------------------------------ */

static void draw_pageops(CwvMenuUi *ui, CwvHost *host, float x, float y)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    static const char *rows[3] = {"history-back", "forward", "reload"};
    const VitaMenuTemplate *panel = tpl_find(tpl, "content-panel");
    const VitaMenuTemplate *title = tpl_find(tpl, "content-title");
    int i;
    (void)host;

    draw_content_panel(ui, panel, x, y);
    if (title)
        paint_text(ui, x + title->x, y + title->y + 20.0f, style_text_color(title),
                   0.82f, title->label ? title->label : "");
    /* (42) modernized page ops: the live page URL sits right-aligned on
     * the title line (context, like the settings rows' values) ... */
    if (title && host) {
        const char *pu = cwv_host_current_url(host);
        if (pu && *pu) {
            float tw = paint_text_width(ui, 0.54f, pu);
            paint_text(ui, x + title->x + title->w - tw, y + title->y + 20.0f,
                       RGBA8(185, 195, 210, 255), 0.54f, pu);
        }
    }
    draw_separator(ui, tpl_find(tpl, "content-separator"), x, y);
    draw_section_accent(ui, tpl, x, y);

    for (i = 0; i < 3; i++) {
        const VitaMenuTemplate *rt = tpl_find(tpl, rows[i]);
        const char *value = NULL;
        float ry;
        if (!rt) continue;
        /* ... and each row carries a right-aligned state value, so the
         * menu reads like the settings tabs instead of bare labels. */
        if (!strcmp(rows[i], "history-back"))
            value = ui->history_index > 0 ? "available" : "oldest page";
        else if (!strcmp(rows[i], "forward"))
            value = (ui->history_count > 0 &&
                     ui->history_index < ui->history_count - 1) ? "available" : "newest page";
        ry = y + rt->y;
        if (ui->pageops_selected == i) {
            paint_rect(ui, x + rt->x, ry, rt->w, rt->h, style_theme_color(ui, rt, 0.23f));
            paint_rect(ui, x + rt->x, ry, 4.0f, rt->h, style_accent_color(ui, rt, 1));
        }
        paint_rect(ui, x + rt->x, ry + rt->h - 1.0f, rt->w, 1.0f, RGBA8(60, 70, 88, 255));
        paint_text(ui, x + rt->x + 16.0f, ry + 25.0f, style_text_color(rt), 0.72f,
                   rt->label ? rt->label : "");
        if (value) {
            float vw = paint_text_width(ui, 0.54f, value);
            paint_text(ui, x + rt->x + rt->w - 16.0f - vw, ry + 24.0f,
                       RGBA8(185, 195, 210, 255), 0.54f, value);
        }
    }
    {
        static const char *back[1] = {"menu-back"};
        draw_action_row(ui, tpl, x, y, 1, back, ui->pageops_selected == 3 ? 0 : -1);
    }
    {
        const VitaMenuTemplate *footer = tpl_find(tpl, "footer");
        if (footer)
            paint_text(ui, x + footer->x, y + footer->y + 25.0f,
                       RGBA8(145, 155, 170, 255), 0.60f,
                       "D-pad: select   CROSS/A: activate   CIRCLE/B: back");
    }
}

/* ------------------------------------------------------------------ */
/* settings (devtools grammar, 3 tabs, live values)                     */
/* ------------------------------------------------------------------ */

/* Value strings for the three tabs (Vita parity). */
static void settings_row_value(const CwvMenuUi *ui, int tab, int row,
                               char *out, size_t cap)
{
    switch (tab) {
        case 0: /* appearance */
            if (row == 0) snprintf(out, cap, "%s", ui->settings.dark_mode ? "Dark" : "Light");
            else if (row == 1) snprintf(out, cap, "%s", cwv_menu_ui_accent_name(ui->settings.accent));
            else if (row == 2) snprintf(out, cap, "%.2fx", ui->settings.zoom_level);
            else snprintf(out, cap, "%s", ui->settings.css_cursors ? "On" : "Off");
            break;
        case 1: /* controls */
            if (row == 0) snprintf(out, cap, "%s",
                ui->settings.scroll_mode == 0 ? "R Analog" :
                ui->settings.scroll_mode == 1 ? "L Analog" : "D-pad");
            else if (row == 1) snprintf(out, cap, "%s",
                ui->settings.zoom_mode == 0 ? "R Analog" :
                ui->settings.zoom_mode == 1 ? "L Analog" : "Off");
            else if (row == 2) snprintf(out, cap, "%s",
                cwv_menu_ui_scroll_speed_name(ui->settings.scroll_speed));
            else snprintf(out, cap, "%s", "Browser-style");
            break;
        default: /* preferences */
            if (row == 0) snprintf(out, cap, "%s", "(per app)");
            else if (row == 1) snprintf(out, cap, "%s",
                ui->settings_status == CWV_MENU_STATUS_CACHE_CLEARED
                ? "Cleared" : "Font/resource cache");
            else if (row == 2) snprintf(out, cap, "%s",
                ui->settings_status == CWV_MENU_STATUS_PREFS_RESET
                ? "Reset to defaults" : "Restore defaults");
            else if (row == 3) snprintf(out, cap, "%s",
                ui->fixed_hotkey ? "HOME (fixed)"
                : ui->settings.menu_hotkey == 1 ? "L+R+START"
                : ui->settings.menu_hotkey == 2 ? "Either combo" : "L+R+SELECT");
            else snprintf(out, cap, "%s",
                ui->settings.show_exit_app ? "On" : "Off");
            break;
    }
}

/* Slider draw (Vita parity): label left, track + stop ticks, value right. */
static void draw_slider_row(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                            float x, float y_scroll, int value)
{
    int stops = template_stops(t);
    int v = value;
    float sx, sy, sw, step;
    unsigned track, active;
    const char *label = "Slider";
    char dyn[48];
    float tw;

    if (v < 0) v = 0;
    if (v >= stops) v = stops - 1;
    sx = x + t->x + 190.0f;
    sy = (y_scroll + t->y) + 27.0f;
    sw = t->w - 220.0f;
    track = style_theme_color(ui, t, 0.28f);
    active = style_accent_color(ui, t, 1);
    step = stops > 1 ? sw / (float)(stops - 1) : 0.0f;

    if (t->action && !strcmp(t->action, "set_scroll_speed")) {
        label = cwv_menu_ui_scroll_speed_name(v);
    } else if (t->action && !strcmp(t->action, "set_zoom")) {
        /* Zoom slider: 6 stops span 0.75x..2.00x (step 0.25). */
        float z = 0.75f + (1.25f * (float)v / (float)(stops > 1 ? stops - 1 : 1));
        snprintf(dyn, sizeof(dyn), "%.2fx", z);
        label = dyn;
    } else if (t->action && !strcmp(t->action, "set_menu_brightness")) {
        float b = 0.25f + (1.75f * (float)v / (float)(stops - 1));
        snprintf(dyn, sizeof(dyn), "%.2fx", b);
        label = dyn;
    } else if (t->action && !strcmp(t->action, "set_menu_tint")) {
        static const char *tint_labels[] = {
            "Neutral", "Subtle", "Soft", "Medium", "Strong", "Full"
        };
        label = tint_labels[v < 6 ? v : 5];
    } else if (t->action && !strcmp(t->action, "set_pitch_stops")) {
        snprintf(dyn, sizeof(dyn), "%d stops", v + 2);
        label = dyn;
    } else {
        static const char *pitch[] = {
            "Very Low", "Low", "Normal", "High", "Very High", "Maximum"
        };
        label = pitch[v];
    }

    paint_text(ui, x + t->x + 14.0f, (y_scroll + t->y) + 22.0f,
               style_text_color(t), 0.66f, t->label ? t->label : "Slider");
    paint_rect(ui, sx, sy - 2.0f, sw, 4.0f, track);
    {
        int i;
        for (i = 0; i < stops; i++) {
            float px = sx + step * (float)i;
            paint_rect(ui, px - 3.0f, sy - 6.0f, 6.0f, 12.0f,
                       i == v ? active : track);
        }
    }
    {
        float px = sx + step * (float)v;
        paint_rect(ui, px - 7.0f, sy - 7.0f, 14.0f, 14.0f, active);
    }
    tw = paint_text_width(ui, 0.60f, label);
    paint_text(ui, sx + sw - tw, (y_scroll + t->y) + 22.0f,
               RGBA8(170, 185, 205, 255), 0.60f, label);
}

/* Zoom slider stop index for the current zoom level. */
static int zoom_stop_index(const CwvMenuUi *ui, const VitaMenuTemplate *t)
{
    int stops = template_stops(t);
    float span = 1.25f / (float)(stops > 1 ? stops - 1 : 1);
    int v = (int)((ui->settings.zoom_level - 0.75f) / span + 0.5f);
    if (v < 0) v = 0;
    if (v >= stops) v = stops - 1;
    return v;
}

static void draw_settings(CwvMenuUi *ui, float x, float y)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    static const char *tabs[3] = {"appearance-tab", "controls-tab", "preferences-tab"};
    const VitaMenuTemplate *panel = tpl_find(tpl, "content-panel");
    const VitaMenuTemplate *title = tpl_find(tpl, "content-title");
    const VitaMenuTemplate *status = tpl_find(tpl, "content-status");
    const VitaMenuTemplate *sep = tpl_find(tpl, "content-separator");
    int row_count = ui->settings_tab == 1 ? 4 : ui->settings_tab == 2 ? 5 : 4;
    int i;

    draw_tab_strip(ui, tpl, x, y, 3, tabs, ui->settings_tab);
    draw_content_panel(ui, panel, x, y);
    if (title)
        paint_text(ui, x + title->x, y + title->y + 20.0f,
                   RGBA8(190, 210, 235, 255), 0.72f,
                   title->label ? title->label : "");
    draw_separator(ui, sep, x, y);

    for (i = 0; i < row_count; i++) {
        char row_name[32];
        const VitaMenuTemplate *rt;
        char value[96];
        float yy;
        int active;
        snprintf(row_name, sizeof(row_name), "content-row-%d", i);
        rt = tpl_find(tpl, row_name);
        if (!rt) continue;
        if (menu_row_hidden(ui, rt->action)) continue;   /* project-gated */
        yy = y + rt->y - ui->settings_scroll;
        active = (ui->settings_action < 0 && ui->settings_item == i);
        if (active) {
            paint_rect(ui, x + rt->x, yy, rt->w, rt->h, style_theme_color(ui, rt, 0.23f));
            paint_rect(ui, x + rt->x, yy, 4.0f, rt->h, style_accent_color(ui, rt, 1));
        }
        row_borders(ui, rt, x + rt->x, yy, ui->settings_scroll);
        if (rt->kind == VITA_MENU_SLIDER) {
            int v = (rt->action && !strcmp(rt->action, "set_zoom"))
                    ? zoom_stop_index(ui, rt)
                    : ui->settings.scroll_speed;
            draw_slider_row(ui, rt, x, y - ui->settings_scroll, v);
        } else {
            settings_row_value(ui, ui->settings_tab, i, value, sizeof(value));
            paint_text(ui, x + rt->x + 14.0f, yy + 25.0f,
                       RGBA8(190, 210, 235, 255), 0.68f,
                       rt->label ? rt->label : "");
            /* 2026-09-26 (44): values right-align inside the row so a
             * long label ("D-pad focus", "Scroll input") never runs into
             * its value, and the eye scans one clean value column. */
            {
                float vw = paint_text_width(ui, 0.68f, value);
                paint_text(ui, x + rt->x + rt->w - vw - 14.0f, yy + 25.0f,
                           RGBA8(250, 250, 255, 255), 0.68f, value);
            }
            if (ui->settings_tab == 2 && i > 0 && i < 3)
                paint_text(ui, x + rt->x + rt->w - 25.0f, yy + 25.0f,
                           style_accent_color(ui, NULL, 1), 0.82f, ">");
        }
    }

    if (status && ui->settings_tab != 1) {
        const char *msg = ui->settings.dirty ? "Unsaved changes"
                        : "Changes are saved per application";
        paint_text(ui, x + status->x, y + status->y + 15.0f,
                   ui->settings.dirty ? RGBA8(235, 190, 120, 255)
                                     : RGBA8(145, 155, 170, 255),
                   0.60f, msg);
    }

    {
        static const char *actions[3] = {"undo", "save", "exit"};
        draw_action_row(ui, tpl, x, y, 3, actions, ui->settings_action);
    }
    paint_text(ui, x + 20.0f, y + 425.0f, RGBA8(145, 155, 170, 255), 0.60f,
               "L/R: tab   D-pad: item   LEFT/RIGHT: change   A: select");
}

/* ------------------------------------------------------------------ */
/* devtools                                                            */
/* ------------------------------------------------------------------ */

#define DEV_CONSOLE_ROW_H 22.0f

/* Wrap length: the longest prefix of seg[0..rem) whose advance fits
 * avail, mirroring the Vita's per-glyph-free prefix measure. */
static int console_wrap_len(const CwvMenuUi *ui, const char *seg, int rem,
                            float avail, float scale)
{
    int lo = 0, hi = rem;
    char probe[258];
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int n = mid > 256 ? 256 : mid;
        memcpy(probe, seg, (size_t)n);
        probe[n] = 0;
        if (paint_text_width(ui, scale, probe) <= avail) lo = mid;
        else hi = mid - 1;
    }
    return lo > 0 ? lo : 1;
}

/* Rebuild the wrapped console cache when the ring changed. */
static void console_cache_rebuild(CwvMenuUi *ui, CwvHost *host)
{
    int lines;
    int errs = 0, warns = 0;
    if (!host) {
        ui->console_row_count = 0;
        return;
    }
    cwv_host_console_stats(host, &errs, &warns, &lines);
    if (lines == ui->console_lines_last && errs == ui->console_errors_last &&
        warns == ui->console_warns_last && ui->console_cache_age < 30) {
        ui->console_cache_age++;
        return;
    }
    /* The ring can wrap at its line limit with the same counts. Refresh
     * periodically so a busy console never gets stuck on a stale dump. */
    ui->console_cache_age = 0;
    {
        int changed=(lines != ui->console_lines_last ||
                     errs != ui->console_errors_last ||
                     warns != ui->console_warns_last);
        if (changed && ui->dev_section == 5 && !ui->dev_scroll_drag)
            ui->dev_scroll = 1e9f; /* follow the log, clamped after rebuild */
    }
    ui->console_lines_last = lines;
    ui->console_errors_last = errs;
    ui->console_warns_last = warns;
    lines = cwv_host_console(host, ui->console_dump, sizeof(ui->console_dump));
    ui->console_row_count = 0;
    {
        const char *p = ui->console_dump;
        const char *nl;
        static const char *lvl_names[] = {"log", "info", "warn", "error", "hint"};
        while ((nl = strchr(p, '\n')) != NULL &&
               ui->console_row_count < 512 - 8) {
            int len = (int)(nl - p);
            int reps = 1, first_row = 0, li;
            unsigned color = RGBA8(255, 255, 255, 255);
            const char *seg = p;
            int rem;
            if (len > 256) len = 256;
            /* level colors ("error:"/"warn:"/...) */
#define CLEVEL(LV, RR, GG, BB) \
            if (len > (int)sizeof(LV) - 1 && !strncmp(seg, LV, sizeof(LV) - 1) && \
                seg[sizeof(LV) - 1] == ':') color = RGBA8(RR, GG, BB, 255); else
            CLEVEL("error", 255, 80, 80)
            CLEVEL("warn", 255, 215, 0)
            CLEVEL("hint", 150, 220, 255)
            CLEVEL("info", 200, 220, 255)
            {}
#undef CLEVEL
            /* hide the "level: " prefix, like a real browser console */
            for (li = 0; li < 5; li++) {
                int l = (int)strlen(lvl_names[li]);
                if (len > l + 2 && !strncmp(p, lvl_names[li], l) &&
                    p[l] == ':' && p[l + 1] == ' ') {
                    p += l + 2; len -= l + 2;
                    break;
                }
            }
            /* ring "  [xN]" repeat suffix -> count badge */
            if (len > 6 && !strncmp(p + len - 4, "[x", 2) && p[len - 1] == ']' &&
                p[len - 6] == ' ' && p[len - 5] == ' ') {
                int v = 0, k = len - 2, mul = 1;
                while (k > len - 6 && p[k] >= '0' && p[k] <= '9') {
                    v += (p[k] - '0') * mul; mul *= 10; k--;
                }
                if (v > 1) { reps = v; len -= 6; if (len > 0 && p[len - 1] == ' ') len--; }
            }
            /* wrap into rows that fit (measure with the cache width) */
            rem = len;
            first_row = ui->console_row_count;
            while (rem > 0 && ui->console_row_count < 512) {
                /* width measured with the panel geometry below (fixed
                 * devtools grammar: panel 20/106/700/270 => 628px) */
                int take = console_wrap_len(ui, seg, rem, 628.0f, 0.60f);
                ui->console_rows[ui->console_row_count].seg = seg;
                ui->console_rows[ui->console_row_count].len = take;
                ui->console_rows[ui->console_row_count].color = color;
                ui->console_rows[ui->console_row_count].reps = 0;
                ui->console_row_count++;
                seg += take; rem -= take;
            }
            if (reps > 1 && first_row < ui->console_row_count)
                ui->console_rows[first_row].reps = reps;
            p = nl + 1;
        }
    }
}

/* devtools scroll max: rows past the panel bottom (console uses the
 * wrapped row count instead). */
static float devtools_scroll_max(const CwvMenuUi *ui)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    const VitaMenuTemplate *panel = tpl_find(tpl, "content-panel");
    const VitaMenuTemplate *sep = tpl_find(tpl, "content-separator");
    const VitaMenuTemplate *t;
    float list_top, viewport_bottom, content_bottom;
    if (!panel) return 0.0f;
    if (ui->dev_section == 5) {
        /* console: visible rows = viewport height / row height */
        float m;
        int visible;
        float vh = panel->h - 62.0f - 10.0f;
        visible = (int)(vh / DEV_CONSOLE_ROW_H);
        m = ((float)ui->console_row_count - (float)visible) * DEV_CONSOLE_ROW_H;
        return m > 0.0f ? m : 0.0f;
    }
    list_top = sep ? (sep->y + 8.0f) : (panel->y + 48.0f);
    viewport_bottom = panel->y + panel->h - 8.0f;
    content_bottom = list_top;
    for (t = tpl; t && t->name; ++t) {
        if (t->y <= list_top) continue;
        if (t->kind == VITA_MENU_DECORATION || t->kind == VITA_MENU_SLIDER ||
            t->kind == VITA_MENU_SCROLLBAR ||
            !strcmp(t->name, "content-panel") || !strcmp(t->name, "content-title") ||
            !strcmp(t->name, "content-separator") ||
            !strcmp(t->name, "footer") || !strcmp(t->name, "footer-separator") ||
            !strcmp(t->name, "back")) continue;
        if (t->y + t->h > content_bottom) content_bottom = t->y + t->h;
    }
    {
        float m = content_bottom - viewport_bottom;
        return m > 0.0f ? m : 0.0f;
    }
}

static void clamp_dev_scroll(CwvMenuUi *ui)
{
    float m = devtools_scroll_max(ui);
    if (ui->dev_scroll < 0.0f) ui->dev_scroll = 0.0f;
    if (ui->dev_scroll > m) ui->dev_scroll = m;
}

/* devtools live row: template-positioned text (Vita devtools_template_text
 * parity, including the wrap-inside-box rule and border keys). */
static void dev_text(const CwvMenuUi *ui, const VitaMenuTemplate *tpl,
                     const char *name, float x, float y, unsigned color,
                     float scale, const char *text)
{
    const VitaMenuTemplate *t = tpl_find(tpl, name);
    if (!t || !text) return;
    paint_text(ui, x + t->x + 14.0f, y + t->y - ui->dev_scroll + 25.0f,
               color, scale, text);
}

/* The overrides section: 7 rows, template positioned, accent highlight
 * on the selected one (Vita parity). */
static void draw_dev_overrides(CwvMenuUi *ui, const VitaMenuTemplate *tpl,
                               float x, float y, float cx, float cy,
                               float ch)
{
    static const char *names[7] = {
        "content-row-0", "content-row-1", "content-row-2", "content-row-3",
        "content-row-4", "content-row-5", "content-row-6"
    };
    char rows[7][112];
    const CwvMenuOverrides *o = &ui->overrides;
    const VitaMenuTemplate *row0 = tpl_find(tpl, "content-row-0");
    const VitaMenuTemplate *track = scrollbar_find(tpl, "content");
    const float list_top = y + (row0 ? row0->y : 154.0f);
    const float list_bottom = cy + ch - 8.0f;
    int i;
    (void)cx;

    snprintf(rows[0], sizeof(rows[0]), "Control mode: %s",
             o->control_mode < 0 ? "Default (Page-defined)"
             : cwv_control_mode_name(o->control_mode));
    snprintf(rows[1], sizeof(rows[1]), "Scroll input: %s",
             o->scroll < 0 ? (ui->settings.scroll_mode == 0 ? "R Analog"
             : ui->settings.scroll_mode == 1 ? "L Analog" : "D-pad")
             : o->scroll == 0 ? "R Analog" : o->scroll == 1 ? "L Analog" : "D-pad");
    snprintf(rows[2], sizeof(rows[2]), "Zoom input: %s",
             o->zoom < 0 ? (ui->settings.zoom_mode == 0 ? "R Analog"
             : ui->settings.zoom_mode == 1 ? "L Analog" : "Off")
             : o->zoom == 0 ? "R Analog" : o->zoom == 1 ? "L Analog" : "Off");
    snprintf(rows[3], sizeof(rows[3]), "Theme: %s",
             o->theme < 0 ? (ui->settings.dark_mode ? "Dark" : "Light")
             : o->theme ? "Dark" : "Light");
    snprintf(rows[4], sizeof(rows[4]), "Scrollbars: %s",
             o->scrollbars < 0 ? "Auto" : o->scrollbars ? "Shown" : "Hidden");
    if (o->zoom_level > 0.0f)
        snprintf(rows[5], sizeof(rows[5]), "Zoom level: %.2fx", o->zoom_level);
    else
        snprintf(rows[5], sizeof(rows[5]), "Zoom level: Default (%.2fx)",
                 ui->settings.zoom_level);
    snprintf(rows[6], sizeof(rows[6]), "Button wait: %s",
             o->button_wait < 0 ? "Default (Page-defined)"
             : o->button_wait_spec[0] ? o->button_wait_spec
             : o->button_wait == 0 ? "release"
             : o->button_wait == 1 ? "none"
             : o->button_wait == 2 ? "1s"
             : o->button_wait == 3 ? "1.5s 0.5s" : "(custom)");

    for (i = 0; i < 7; i++) {
        const VitaMenuTemplate *rt = tpl_find(tpl, names[i]);
        float yy = y + (rt ? rt->y : (154.0f + (float)i * 42.0f)) - ui->dev_scroll;
        float rw = rt ? rt->w : 660.0f, rh = rt ? rt->h : 38.0f;
        float rx = x + (rt ? rt->x : 40.0f);
        if (yy < list_top || yy + rh > list_bottom) continue;
        if (ui->dev_option == i) {
            paint_rect(ui, rx, yy, rw, rh, style_theme_color(ui, rt, 0.22f));
            paint_rect(ui, rx, yy, 4.0f, rh, style_accent_color(ui, rt, 1));
        }
        paint_text(ui, rx + 14.0f, yy + 25.0f,
                   ui->dev_option == i ? RGBA8(255, 255, 255, 255)
                                       : RGBA8(180, 195, 215, 255),
                   0.70f, rows[i]);
    }
    if (track) {
        float track_h = list_bottom - list_top;
        draw_scrollbar(ui, track, x, y, track_h + devtools_scroll_max(ui),
                       track_h, ui->dev_scroll);
    }
}

/* The console section: wrapped color-coded log + repeat badges +
 * follow-the-log + scrollbar (Vita parity). */
static void draw_dev_console(CwvMenuUi *ui, const VitaMenuTemplate *tpl,
                             float x, float y, float cx, float cy,
                             float cw, float ch)
{
    const VitaMenuTemplate *title = tpl_find(tpl, "content-title");
    const VitaMenuTemplate *track = scrollbar_find(tpl, "content");
    const float ctp = cy + 52.0f, cbt = cy + ch - 10.0f;
    const float cxs = cx + 14.0f;
    const float cscale = 0.60f;
    int i;

    /* status chip top-right */
    if (ui->host && title) {
        int lines = ui->console_lines_last;
        char chip[128];
        int errs = 0, warns = 0;
        cwv_host_console_stats(ui->host, &errs, &warns, &lines);
        snprintf(chip, sizeof(chip), "JS %s | %d line%s",
                 cwv_host_js_active(ui->host) ? "on" : "off",
                 lines, lines == 1 ? "" : "s");
        {
            float sw = paint_text_width(ui, 0.55f, chip);
            paint_text(ui, cx + cw - sw - 14.0f, y + title->y + 22.0f,
                       RGBA8(150, 165, 185, 255), 0.55f, chip);
        }
    }
    if (ui->console_row_count == 0) {
        paint_text(ui, cxs, ctp + 18.0f, RGBA8(170, 185, 205, 255), cscale,
                   "(no output yet) console.log/info/warn/error land here.");
    }
    for (i = 0; i < ui->console_row_count; i++) {
        const float yy = ctp + (float)i * DEV_CONSOLE_ROW_H - ui->dev_scroll;
        char seg[258];
        if (yy < ctp) continue;
        if (yy + DEV_CONSOLE_ROW_H > cbt) break;
        if (ui->console_rows[i].len > 0) {
            int n = ui->console_rows[i].len;
            if (n > 256) n = 256;
            memcpy(seg, ui->console_rows[i].seg, (size_t)n);
            seg[n] = 0;
            paint_text(ui, cxs, yy + 16.0f, ui->console_rows[i].color, cscale, seg);
        }
        if (ui->console_rows[i].reps > 1) {
            const float brx = cx + cw - 26.0f, bry = yy + 9.0f;
            char num[8];
            paint_circle(ui, brx, bry, 8.0f, style_theme_color(ui, NULL, 1.0f));
            snprintf(num, sizeof(num), "%d", ui->console_rows[i].reps);
            {
                float nw = paint_text_width(ui, 0.42f, num);
                paint_text(ui, brx - nw * 0.5f, bry + 3.0f,
                           RGBA8(255, 255, 255, 255), 0.42f, num);
            }
        }
    }
    if (track && devtools_scroll_max(ui) > 0.0f)
        draw_scrollbar(ui, track, x, y, (cbt - ctp) + devtools_scroll_max(ui),
                       cbt - ctp, ui->dev_scroll);
}

static const char *dev_section_name(int section)
{
    static const char *names[8] = {
        "Performance", "Renderer", "Input", "Page",
        "Overrides", "Console", "Platform", "Close"
    };
    return section >= 0 && section < 8 ? names[section] : "?";
}

static void draw_devtools(CwvMenuUi *ui, CwvHost *host, float x, float y,
                          const CwvMenuData *data)
{
    const VitaMenuTemplate *tpl = active_template(ui);
    static const char *tabs[8] = {
        "performance", "renderer", "input", "page",
        "overrides", "console", "platform", "close"
    };
    const VitaMenuTemplate *panel = tpl_find(tpl, "content-panel");
    const VitaMenuTemplate *title = tpl_find(tpl, "content-title");
    const VitaMenuTemplate *sep = tpl_find(tpl, "content-separator");
    const VitaMenuTemplate *row0 = tpl_find(tpl, "content-row-0");
    const VitaMenuTemplate *track = scrollbar_find(tpl, "content");
    float cx, cy, cw, ch, rx;
    char line[256];

    draw_tab_strip(ui, tpl, x, y,
                   ui->settings.show_exit_app ? 8 : 7, tabs,
                   ui->dev_section); /* close is not "active" by index */
    /* the close tab highlights only when selected */
    {
        const VitaMenuTemplate *t = tpl_find(tpl, "close");
        if (t && ui->dev_section == 7) {
            float tw = paint_text_width(ui, 0.72f, t->label);
            float xx = x + t->x, yy = y + t->y;
            paint_text(ui, xx + (t->w - tw) * 0.5f, yy + 24.0f,
                       RGBA8(255, 255, 255, 255), 0.72f, t->label);
            paint_rect(ui, xx + (t->w - tw) * 0.5f, yy + t->h - 3.0f, tw, 2.0f,
                       style_accent_color(ui, t, 1));
        }
    }

    cx = x + (panel ? panel->x : 20.0f);
    cy = y + (panel ? panel->y : 106.0f);
    cw = panel ? panel->w : 700.0f;
    ch = panel ? panel->h : 270.0f;
    draw_content_panel(ui, panel, x, y);
    rx = x + (title ? title->x : 40.0f);

    paint_text(ui, rx, title ? y + title->y + 20.0f : cy + 30.0f,
               RGBA8(190, 210, 235, 255), 0.82f,
               ui->dev_section == 7 ? "Close" : dev_section_name(ui->dev_section));
    draw_separator(ui, sep, x, y);
    draw_section_accent(ui, tpl, x, y);

    if (ui->dev_section == 7) {
        /* close confirm (Vita parity: message + No/Yes buttons) */
        const VitaMenuTemplate *cm = tpl_find(tpl, "close-message");
        const VitaMenuTemplate *cn = tpl_find(tpl, "close-no");
        const VitaMenuTemplate *cyes = tpl_find(tpl, "close-yes");
        const char *app = host ? cwv_host_page_title(host) : "CWebView";
        char confirm[256];
        if (!app || !*app) app = "CWebView";
        paint_text(ui, x + (cm ? cm->x : 40.0f),
                   y + (cm ? cm->y : 198.0f) + 20.0f,
                   RGBA8(245, 245, 250, 255), 0.82f,
                   "Are you sure you want to exit");
        snprintf(confirm, sizeof(confirm), "%s?", app);
        paint_text(ui, x + (cm ? cm->x : 40.0f),
                   y + (cm ? cm->y : 198.0f) + 52.0f,
                   RGBA8(245, 245, 250, 255), 0.82f, confirm);
        if (cn) {
            float nw;
            paint_rect(ui, x + cn->x, y + cn->y, cn->w, cn->h,
                       ui->exit_confirm_selected == 0
                       ? style_accent_color(ui, cn, 1)
                       : style_theme_color(ui, cn, 0.30f));
            nw = paint_text_width(ui, 0.74f, cn->label ? cn->label : "");
            paint_text(ui, x + cn->x + (cn->w - nw) * 0.5f, y + cn->y + 29.0f,
                       RGBA8(255, 255, 255, 255), 0.74f,
                       cn->label ? cn->label : "");
        }
        if (cyes) {
            float yw;
            paint_rect(ui, x + cyes->x, y + cyes->y, cyes->w, cyes->h,
                       ui->exit_confirm_selected == 1
                       ? style_accent_color(ui, cyes, 1)
                       : style_theme_color(ui, cyes, 0.30f));
            yw = paint_text_width(ui, 0.74f, cyes->label ? cyes->label : "");
            paint_text(ui, x + cyes->x + (cyes->w - yw) * 0.5f, y + cyes->y + 29.0f,
                       RGBA8(255, 255, 255, 255), 0.74f,
                       cyes->label ? cyes->label : "");
        }
    } else if (ui->dev_section == 0) {
        if (data) {
            snprintf(line, sizeof(line), "FPS: %.1f", data->fps);
            dev_text(ui, tpl, "fps", x, y, RGBA8(235, 240, 250, 255), 0.72f, line);
            snprintf(line, sizeof(line), "Frames: %u    Frame time: %.2f ms",
                     data->frame_count, data->frame_ms);
            dev_text(ui, tpl, "frames", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            snprintf(line, sizeof(line), "Scroll: %.1f    Zoom: %.2fx",
                     data->scroll_offset, data->zoom);
            dev_text(ui, tpl, "scroll-zoom", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
        }
        dev_text(ui, tpl, "diagnostics", x, y, RGBA8(150, 165, 185, 255), 0.64f,
                 "Lightweight diagnostics; no polling thread.");
    } else if (ui->dev_section == 1) {
        dev_text(ui, tpl, "engine", x, y, RGBA8(235, 240, 250, 255), 0.70f,
                 "Engine: native renderer + Duktape ES5.1");
        if (data) {
            snprintf(line, sizeof(line), "Viewport: %dx%d    Zoom: %.2fx",
                     data->viewport_w, data->viewport_h, data->zoom);
            dev_text(ui, tpl, "viewport", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            snprintf(line, sizeof(line), "DOM nodes: %d", data->node_count);
            dev_text(ui, tpl, "dom-nodes", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            snprintf(line, sizeof(line), "JS: %s", data->js_active ? "active" : "inactive");
            dev_text(ui, tpl, "js-state", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            if (data->js_last_error[0])
                snprintf(line, sizeof(line), "Last JS error: %.90s", data->js_last_error);
            else
                snprintf(line, sizeof(line), "Last JS error: (none)");
        } else {
            snprintf(line, sizeof(line), "Last JS error: (none)");
        }
        dev_text(ui, tpl, "last-error", x, y, RGBA8(150, 165, 185, 255), 0.64f, line);
    } else if (ui->dev_section == 2) {
        if (data) {
            snprintf(line, sizeof(line), "Control mode: %s",
                     data->control_mode_name[0] ? data->control_mode_name
                     : "Browser");
            dev_text(ui, tpl, "control-mode", x, y, RGBA8(235, 240, 250, 255), 0.70f, line);
            snprintf(line, sizeof(line), "Scroll: %s    Zoom: %s",
                     cwv_menu_ui_effective_scroll_mode(ui) == 0 ? "R Analog"
                     : cwv_menu_ui_effective_scroll_mode(ui) == 1 ? "L Analog" : "D-pad",
                     cwv_menu_ui_effective_zoom_mode(ui) == 0 ? "R Analog"
                     : cwv_menu_ui_effective_zoom_mode(ui) == 1 ? "L Analog" : "Off");
            dev_text(ui, tpl, "scroll-zoom", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            snprintf(line, sizeof(line), "Last button: %s",
                     data->last_button[0] ? data->last_button : "-");
            dev_text(ui, tpl, "last-btn", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            snprintf(line, sizeof(line), "D-pad focus: %s",
                     data->dpad_focus[0] ? data->dpad_focus : "(none)");
            dev_text(ui, tpl, "dpad-focus", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
        }
    } else if (ui->dev_section == 3) {
        if (data) {
            snprintf(line, sizeof(line), "Title: %.110s",
                     data->page_title[0] ? data->page_title : "(untitled)");
            dev_text(ui, tpl, "title", x, y, RGBA8(235, 240, 250, 255), 0.68f, line);
            snprintf(line, sizeof(line), "URL: %.110s",
                     data->page_url[0] ? data->page_url : "about:blank");
            dev_text(ui, tpl, "url", x, y, RGBA8(220, 230, 240, 255), 0.64f, line);
            /* (81) version_display holds the platform name (Wii/Wii U/
             * Windows); the line now shows the internal CWebView version,
             * the platform, and the app's own version in bright white. */
            snprintf(line, sizeof(line), "Version: %s (%s)", CWV_VERSION_DISPLAY,
                     data->version_display);
            dev_text(ui, tpl, "version", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
            {
                const VitaMenuTemplate *vt = tpl_find(tpl, "version");
                float vx = x + (vt ? vt->x : 0.0f) + 14.0f;
                float vy = y + (vt ? vt->y : 0.0f) - ui->dev_scroll + 25.0f;
                float vw = paint_text_width(ui, 0.68f, line);
                snprintf(line, sizeof(line), "  App: %s", CWV_APP_VERSION);
                paint_text(ui, vx + vw, vy, RGBA8(255, 255, 255, 255), 0.68f, line);
            }
            snprintf(line, sizeof(line), "JS: %s",
                     data->js_active ? "active" : "inactive");
            dev_text(ui, tpl, "js-state", x, y, RGBA8(150, 165, 185, 255), 0.64f, line);
        }
        if (ui->host) {
            int cerr = 0, cwarn = 0, clines = 0;
            cwv_host_console_stats(ui->host, &cerr, &cwarn, &clines);
            snprintf(line, sizeof(line), "Console: %d lines (%d errors, %d warns)",
                     clines, cerr, cwarn);
            dev_text(ui, tpl, "console-stats", x, y, RGBA8(220, 230, 240, 255), 0.68f, line);
        }
    } else if (ui->dev_section == 4) {
        draw_dev_overrides(ui, tpl, x, y, cx, cy, ch);
    } else if (ui->dev_section == 5) {
        draw_dev_console(ui, tpl, x, y, cx, cy, cw, ch);
    } else if (ui->dev_section == 6) {
        dev_text(ui, tpl, "native-renderer", x, y, RGBA8(220, 230, 240, 255), 0.66f,
                 "HTML/CSS native renderer is active.");
        dev_text(ui, tpl, "javascript-dom", x, y, RGBA8(220, 230, 240, 255), 0.66f,
                 "JS engine: Duktape 2.7.0 - understands ES5.1 only.");
        if (data) {
            dev_text(ui, tpl, "esm-module", x, y, RGBA8(150, 165, 185, 255), 0.62f,
                     data->platform_row_esm[0] ? data->platform_row_esm
                     : "ESM module: (state unknown)");
            dev_text(ui, tpl, "platform-status", x, y, RGBA8(150, 165, 185, 255), 0.62f,
                     data->platform_row_status[0] ? data->platform_row_status
                     : "Platform integration is active.");
        }
        snprintf(line, sizeof(line), "CWMenu script v%d, %d menus loaded",
                 cwmenu_script_version(), cwm_dynamic_menu_total());
        dev_text(ui, tpl, "cwm-script", x, y, RGBA8(150, 165, 185, 255), 0.62f, line);
        if (data && data->uptime[0]) {
            snprintf(line, sizeof(line), "Uptime: %s", data->uptime);
            dev_text(ui, tpl, "uptime", x, y, RGBA8(150, 165, 185, 255), 0.62f, line);
        }
    }

    if (ui->dev_section != 4 && ui->dev_section != 5 &&
        ui->dev_section != 7 && devtools_scroll_max(ui) > 0.0f && track) {
        float viewport = ch - 8.0f - ((sep ? sep->y : 148.0f) + 8.0f - (panel ? panel->y : 106.0f));
        draw_scrollbar(ui, track, x, y, viewport + devtools_scroll_max(ui),
                       viewport, ui->dev_scroll);
    }

    {
        static const char *back[1] = {"back"};
        draw_action_row(ui, tpl, x, y, 1, back,
                        ui->dev_section == 7 ? 2 : -1);
    }
    (void)row0;
}

/* Additional CWMenu 2 template kinds.  Built-in pages keep their tailored
 * layout; custom menus and passive partner windows use this generic pass. */
static void draw_template_item(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                               float x, float y, int selected)
{
    float bx=x+t->x, by=y+t->y, cx, r;
    unsigned fill=style_theme_color(ui,t,0.24f);
    unsigned edge=style_accent_color(ui,t,1);
    const char *label=t->label?t->label:"";
    if(t->kind==VITA_MENU_WINDOW || t->kind==VITA_MENU_HEADER ||
       t->kind==VITA_MENU_SCROLLBAR)return;
    switch(t->kind){
    case VITA_MENU_PANEL: paint_rect(ui,bx,by,t->w,t->h,fill);break;
    case VITA_MENU_UNDERLINE: paint_rect(ui,bx,by,t->w,1,edge);break;
    case VITA_MENU_DECORATION:
        if(!strcmp(label,"accent")) paint_rect(ui,bx,by,t->w,t->h,edge);
        else if(!strcmp(label,"dot")||!strcmp(label,"marker"))
            paint_rect(ui,bx,by,t->w,t->h,fill);
        break;
    case VITA_MENU_SHAPE_RECTANGLE:
        paint_rect(ui,bx,by,t->w,t->h,fill);
        paint_rect(ui,bx,by,t->w,2,edge);
        paint_rect(ui,bx,by,2,t->h,edge);
        paint_rect(ui,bx,by+t->h-2,t->w,2,edge);
        paint_rect(ui,bx+t->w-2,by,2,t->h,edge);
        break;
    case VITA_MENU_SHAPE_ROUNDED_RECTANGLE:
        r=(t->w<t->h?t->w:t->h)*0.22f;
        paint_rounded_rect(ui,bx,by,t->w,t->h,r,edge);
        if(t->w>4 && t->h>4)paint_rounded_rect(ui,bx+2,by+2,t->w-4,t->h-4,r>2?r-2:1,fill);
        break;
    case VITA_MENU_SHAPE_CIRCLE:
        r=(t->w<t->h?t->w:t->h)*0.5f;
        cx=bx+t->w*0.5f;
        paint_circle(ui,cx,by+t->h*0.5f,r,edge);
        if(r>2)paint_circle(ui,cx,by+t->h*0.5f,r-2,fill);
        break;
    case VITA_MENU_LINE: {
        const char *p=label;
        float px=0,py=0,nx,ny;
        int have=0,used=0;
        while(*p && used++<64){
            char *end;
            while(*p==' '||*p==',')p++;
            if(*p=='_'){have=0;p++;continue;}
            nx=strtof(p,&end);if(end==p)break;p=end;
            while(*p==' '||*p==',')p++;
            ny=strtof(p,&end);if(end==p)break;p=end;
            if(have && ui->paint && ui->paint->draw_line)
                ui->paint->draw_line(bx+px,by+py,bx+nx,by+ny,edge,ui->paint->ud);
            px=nx;py=ny;have=1;
        }
        break;
    }
    case VITA_MENU_ICON: {
        const char *symbol=label;
        if(!strcmp(label,"check"))symbol="✓";
        else if(!strcmp(label,"info"))symbol="i";
        else if(!strcmp(label,"warning"))symbol="!";
        else if(!strcmp(label,"gear"))symbol="*";
        else if(!strcmp(label,"arrow-left"))symbol="<";
        else if(!strcmp(label,"arrow-right"))symbol=">";
        paint_rect(ui,bx,by,t->w,t->h,style_theme_color(ui,t,0.14f));
        paint_rect(ui,bx,by,t->w,2,edge);
        paint_text(ui,bx+(t->w-paint_text_width(ui,0.70f,symbol))*0.5f,
                   by+t->h*0.66f,style_text_color(t),0.70f,symbol);
        break;
    }
    case VITA_MENU_BADGE: {
        float tw=paint_text_width(ui,0.54f,label), bw=tw+24;
        float xx=bx+t->w-bw-12, yy=by+(t->h-24)*0.5f;
        paint_rect(ui,xx,yy,bw,24,fill);paint_rect(ui,xx,yy,bw,2,edge);
        paint_text(ui,xx+12,yy+17,style_text_color(t),0.54f,label);
        break;
    }
    case VITA_MENU_PROGRESS:
        paint_text(ui,bx+14,by+22,style_text_color(t),0.66f,label);
        paint_rect(ui,bx+t->w-300,by+(t->h-10)*0.5f,260,10,style_theme_color(ui,t,0.16f));
        paint_rect(ui,bx+t->w-300,by+(t->h-10)*0.5f,169,10,edge);
        break;
    case VITA_MENU_VALUE:
        paint_text(ui,bx+14,by+22,style_text_color(t),0.66f,label);
        paint_text(ui,bx+t->w-65,by+22,style_text_color(t),0.64f,"Ready");
        break;
    case VITA_MENU_LIST:
    case VITA_MENU_SEGMENTED: {
        char copy[512],*item;
        int count=1,index=0;
        for(const char *p=label;*p;p++)if(*p=='|')count++;
        if(count>16)count=16;
        snprintf(copy,sizeof(copy),"%s",label);
        item=strtok(copy,"|");
        while(item && index<count){
            float cell=t->w/(float)count,xx=bx+cell*index;
            if(selected && index==0)paint_rect(ui,xx,by,cell,t->h,fill);
            if(index>0)paint_rect(ui,xx,by,1,t->h,edge);
            paint_text(ui,xx+6,by+t->h*0.68f,style_text_color(t),0.60f,item);
            index++;item=strtok(NULL,"|");
        }
        break;
    }
    case VITA_MENU_CHECKBOX:
        paint_rect(ui,bx,by+7,20,20,fill);
        paint_rect(ui,bx,by+7,20,2,edge);
        if(selected)paint_text(ui,bx+4,by+24,edge,0.65f,"✓");
        paint_text(ui,bx+28,by+25,style_text_color(t),0.68f,label);
        break;
    case VITA_MENU_BUTTON:
        /* Unified row grammar (2026-10-05): CWMenus render with the exact
         * same chrome as the Settings/DevTools rows - selected row gets
         * the theme fill + 4px accent bar, every row a 1px separator. */
        if(selected){
            paint_rect(ui,bx,by,t->w,t->h,style_theme_color(ui,t,0.23f));
            paint_rect(ui,bx,by,4.0f,t->h,edge);
        }
        paint_rect(ui,bx,by+t->h-1.0f,t->w,1.0f,RGBA8(60, 70, 88, 255));
        paint_text(ui,bx+16.0f,by+t->h*0.68f,style_text_color(t),0.72f,label);
        break;
    case VITA_MENU_SLIDER:
        draw_slider_row(ui,t,x,y,0);
        break;
    case VITA_MENU_TEXT:
    case VITA_MENU_FOOTER:
        row_borders(ui,t,bx,by,0);
        if(t->action && *t->action){
            /* Actionable text rows are menu rows (extras.cwm content-row-N):
             * same Settings/DevTools grammar as buttons (2026-10-05). */
            if(selected){
                paint_rect(ui,bx,by,t->w,t->h,style_theme_color(ui,t,0.23f));
                paint_rect(ui,bx,by,4.0f,t->h,edge);
            }
            paint_rect(ui,bx,by+t->h-1.0f,t->w,1.0f,RGBA8(60, 70, 88, 255));
            paint_text(ui,bx+16.0f,by+t->h*0.68f,style_text_color(t),0.72f,label);
        }else{
            paint_text(ui,bx+12,by+t->h*0.68f,style_text_color(t),0.66f,label);
        }
        break;
    default:break;
    }
}

static int custom_action_count(const CwvMenuUi *ui)
{
    int n=0;
    const VitaMenuTemplate *tpl=active_template(ui);
    for(const VitaMenuTemplate *t=tpl;t && t->name;t++)
        if(t->action && *t->action)n++;
    return n;
}
static const char *custom_action_at(const CwvMenuUi *ui,int index)
{
    const VitaMenuTemplate *tpl=active_template(ui);
    for(const VitaMenuTemplate *t=tpl;t && t->name;t++)
        if(t->action && *t->action && index--==0)return t->action;
    return NULL;
}

static void draw_template_extras(const CwvMenuUi *ui,const VitaMenuTemplate *tpl,float x,float y)
{
    if(!tpl)return;
    for(const VitaMenuTemplate *t=tpl;t->name;t++){
        if((t->kind==VITA_MENU_BUTTON && t->action &&
            !strncmp(t->action,"open_menu:",10)) ||
           t->kind==VITA_MENU_ICON || t->kind==VITA_MENU_PROGRESS ||
           t->kind==VITA_MENU_VALUE || t->kind==VITA_MENU_LIST ||
           t->kind==VITA_MENU_SHAPE_RECTANGLE ||
           t->kind==VITA_MENU_SHAPE_ROUNDED_RECTANGLE ||
           t->kind==VITA_MENU_SHAPE_CIRCLE || t->kind==VITA_MENU_LINE ||
           t->kind==VITA_MENU_CHECKBOX || t->kind==VITA_MENU_SEGMENTED)
            draw_template_item(ui,t,x,y,0);
    }
}

static void draw_pair_window(const CwvMenuUi *ui,const VitaMenuTemplate *active)
{
    const VitaMenuTemplate *win=tpl_find(active,"window"),*partner,*pwin,*header;
    size_t count=0;
    float x,y,w,h;
    if(!win || !win->win_pair || !*win->win_pair)return;
    partner=vita_native_menu_find(win->win_pair,&count);
    if(!partner || !count || !(pwin=tpl_find(partner,"window")))return;
    menu_frame(partner,&x,&y,&w,&h);
    if(pwin->win_corner==1)paint_rounded_rect(ui,x,y,w,h,12,style_theme_color(ui,pwin,0.24f));
    else paint_rect(ui,x,y,w,h,style_theme_color(ui,pwin,0.24f));
    header=tpl_find(partner,"header");
    if(header){
        paint_rect(ui,x+header->x,y+header->y,header->w,header->h,
                   style_theme_color(ui,header,0.48f));
        paint_text(ui,x+header->x+18,y+header->y+29,style_text_color(header),
                   1.0f,header->label);
    }
    for(const VitaMenuTemplate *t=partner;t->name;t++){
        if(t->kind==VITA_MENU_WINDOW || t->kind==VITA_MENU_HEADER)continue;
        draw_template_item(ui,t,x,y,0);
    }
}

/* ------------------------------------------------------------------ */
/* actions                                                             */
/* ------------------------------------------------------------------ */

/* Defaults applied at init / reset. */
static void overrides_defaults(CwvMenuUi *ui)
{
    CwvMenuOverrides *o=&ui->overrides;
    o->control_mode=o->scroll=o->zoom=o->theme=o->scrollbars=-1;
    o->zoom_level=0.0f;
    o->button_wait=-1;
    o->button_wait_spec[0]=0;
}

static void settings_defaults(CwvMenuUi *ui)
{
    ui->settings.scroll_mode = 0;
    ui->settings.scroll_speed = 1;
    ui->settings.zoom_mode = 0;
    ui->settings.zoom_level = 1.0f;
    ui->settings.dark_mode = 1;
    ui->settings.accent = 0;
    ui->settings.menu_hotkey = 0;
    ui->settings.show_exit_app = 0;
    ui->settings.css_cursors = 1;   /* CSS cursor: on by default (45) */
    ui->settings.dirty = 0;
}

static void settings_select_tab(CwvMenuUi *ui, int tab)
{
    if (tab < 0) tab = 2;
    if (tab > 2) tab = 0;
    ui->settings_tab = tab;
    ui->settings_item = 0;
    ui->settings_action = -1;
    ui->settings_scroll = 0.0f;
    ui->settings_scroll_drag = 0;
}

/* LEFT/RIGHT cycling helper: wraps in [0,count). */
/* Backend control-mode id -> display name (0 none/hybrid, 1 browser,
 * 2 standalone, 3 touch, 4 cursor - mirrors cwv_html.c's
 * cwv-control-mode attribute and the Vita VCM_* enum). */
static const char *cwv_control_mode_name(int mode)
{
    switch (mode) {
        case 1: return "Browser";
        case 2: return "Standalone";
        case 3: return "Touch";
        case 4: return "Cursor";
        default: return "None";
    }
}

static int cycle(int current, int direction, int count)
{
    int state = current + direction;
    if (state >= count) state = 0;
    if (state < 0) state = count - 1;
    return state;
}

/* Zoom level from a slider stop index (6 stops, 0.75x..2.0x). */
static float zoom_from_stop(int v, int stops)
{
    float z = 0.75f + (1.25f * (float)v / (float)(stops > 1 ? stops - 1 : 1));
    if (z < 0.75f) z = 0.75f;
    if (z > 2.0f) z = 2.0f;
    return z;
}

/* Dispatch a CWMenu template action.  Shared actions run here; the
 * port-specific ones (open-url, exit, cache) go through ops. */
static void run_action(CwvMenuUi *ui, const char *action)
{
    if (!action || !action[0]) return;
    if (menu_action_hidden(action)) return;   /* disabled feature: dead */

    if (!strncmp(action,"open_menu:",10)) {
        (void)cwv_menu_ui_open_named(ui,action+10);
        return;
    }
    /* CWMenu 4 script linking: `open_script:<file>` loads another .cwm
     * (the port resolves it against its cwmenu directory through the
     * load_script ops hook) and opens a menu from it.  <file> may carry
     * "|<menu>" to name the menu to open; without it the linked script's
     * `main` is assumed.  Linked menus replace same-named menus and the
     * linked theme merges, so a script can re-skin the whole shell. */
    if (!strncmp(action,"open_script:",12)) {
        const char *spec=action+12, *sep=strchr(spec,'|');
        char file[160], menu[64]="main";
        if(!spec || !*spec) return;
        if(sep){
            size_t fl=(size_t)(sep-spec);
            if(fl>=sizeof(file)) fl=sizeof(file)-1;
            memcpy(file,spec,fl); file[fl]='\0';
            snprintf(menu,sizeof(menu),"%s",sep+1);
        }else{
            snprintf(file,sizeof(file),"%s",spec);
        }
        if(!*file || !*menu) return;
        if(ui->ops && ui->ops->load_script &&
           ui->ops->load_script(file,ui->ops->ud)==0){
            ui->script_error[0]='\0';
            (void)cwv_menu_ui_open_named(ui,menu);
        }else{
            /* visible feedback: the main footer shows it until the next
             * action (ports also route the line through ops->log). */
            snprintf(ui->script_error,sizeof(ui->script_error),
                     "script not found: %s",file);
            if(ui->ops && ui->ops->log)
                ui->ops->log(ui->script_error,ui->ops->ud);
        }
        return;
    }
    if (!strcmp(action, "back") || !strcmp(action, "menu-back")) {
        if (ui->mode == CWV_MENU_PAGEOPS || ui->mode == CWV_MENU_DEVTOOLS ||
            ui->mode == CWV_MENU_CUSTOM)
            cwv_menu_ui_open(ui, CWV_MENU_MAIN);
        else cwv_menu_ui_close(ui);
        return;
    }
    if (!strcmp(action, "page-operations")) { cwv_menu_ui_open(ui, CWV_MENU_PAGEOPS); return; }
    if (!strcmp(action, "settings"))         { cwv_menu_ui_open(ui, CWV_MENU_SETTINGS); return; }
    if (!strcmp(action, "devtools"))         { cwv_menu_ui_open(ui, CWV_MENU_DEVTOOLS); return; }
    if (!strcmp(action, "open-url")) {
        if (ui->ops && ui->ops->open_url_prompt)
            ui->ops->open_url_prompt(ui->ops->ud);
        return;
    }
    if (!strcmp(action, "history-back")) {
        if (ui->host && ui->history_index > 0) {
            int next = ui->history_index - 1;
            if (cwv_host_load(ui->host, ui->history[next]) == 0) ui->history_index = next;
        }
        return;
    }
    if (!strcmp(action, "forward")) {
        if (ui->host && ui->history_index + 1 < ui->history_count) {
            int next = ui->history_index + 1;
            if (cwv_host_load(ui->host, ui->history[next]) == 0) ui->history_index = next;
        }
        return;
    }
    if (!strcmp(action, "reload")) {
        if (ui->host) {
            const char *url = cwv_host_current_url(ui->host);
            char page[1024];
            if (url && *url) {
                snprintf(page, sizeof(page), "%s", url);
                (void)cwv_host_load(ui->host, page);
            }
        }
        return;
    }

    /* settings tabs */
    if (!strcmp(action, "appearance-tab")) { settings_select_tab(ui, 0); return; }
    if (!strcmp(action, "controls-tab"))   { settings_select_tab(ui, 1); return; }
    if (!strcmp(action, "preferences-tab")){ settings_select_tab(ui, 2); return; }

    /* settings content */
    if (!strcmp(action, "toggle_theme")) {
        ui->settings.dark_mode = !ui->settings.dark_mode;
        ui->settings.dirty = 1;
        return;
    }
    if (!strcmp(action, "cycle_accent")) {
        ui->settings.accent = cycle(ui->settings.accent, 1, cwv_menu_ui_accent_count());
        ui->settings.dirty = 1;
        return;
    }
    if (!strcmp(action, "set_zoom") || !strcmp(action, "set_scroll_speed")) {
        /* slider rows cycle one stop on activate */
        const VitaMenuTemplate *t = NULL;
        const VitaMenuTemplate *tpl = active_template(ui);
        char row_name[32];
        int row = ui->settings_item;
        if (row < 0) row = 0;
        snprintf(row_name, sizeof(row_name), "content-row-%d", row);
        t = tpl_find(tpl, row_name);
        if (t && t->kind == VITA_MENU_SLIDER) {
            int stops = template_stops(t);
            int v;
            if (!strcmp(action, "set_zoom")) {
                v = cycle(zoom_stop_index(ui, t), 1, stops);
                ui->settings.zoom_level = zoom_from_stop(v, stops);
                if (ui->host) (void)cwv_host_set_zoom(ui->host,ui->settings.zoom_level);
            } else {
                v = cycle(ui->settings.scroll_speed, 1, stops);
                ui->settings.scroll_speed = v;
            }
            ui->settings.dirty = 1;
        }
        return;
    }
    if (!strcmp(action, "cycle_scroll_input")) {
        ui->settings.scroll_mode = cycle(ui->settings.scroll_mode, 1, 3);
        ui->settings.dirty = 1;
        return;
    }
    if (!strcmp(action, "cycle_zoom_input")) {
        ui->settings.zoom_mode = cycle(ui->settings.zoom_mode, 1, 3);
        ui->settings.dirty = 1;
        return;
    }
    if (!strcmp(action, "cycle_menu_hotkey")) {
        if (ui->fixed_hotkey) return;   /* Wii HOME: not remappable */
        ui->settings.menu_hotkey = cycle(ui->settings.menu_hotkey, 1, 3);
        ui->settings.dirty = 1;
        return;
    }
    if (!strcmp(action, "toggle_css_cursors")) {
        ui->settings.css_cursors = !ui->settings.css_cursors;
        ui->settings.dirty = 1;
        ui_log(ui, ui->settings.css_cursors
               ? "settings: CSS cursors on (pages choose pointer art)"
               : "settings: CSS cursors off (default pointer art)");
        return;
    }
    if (!strcmp(action, "toggle_exit_app")) {
        ui->settings.show_exit_app = !ui->settings.show_exit_app;
        ui->settings.dirty = 1;
        if (!ui->settings.show_exit_app && ui->mode == CWV_MENU_DEVTOOLS &&
            ui->dev_section == 7) ui->dev_section = 0;
        return;
    }
    if (!strcmp(action, "clear_cache")) {
        if (ui->ops && ui->ops->clear_cache)
            ui->settings_status = ui->ops->clear_cache(ui->ops->ud);
        else ui->settings_status = CWV_MENU_STATUS_NONE;
        ui_log(ui, ui->settings_status == CWV_MENU_STATUS_CACHE_CLEARED
               ? "settings: cache cleared" : "settings: cache clear unavailable");
        return;
    }
    if (!strcmp(action, "reset_preferences")) {
        settings_defaults(ui);
        overrides_defaults(ui);
        ui->settings.dirty = 1;
        ui->settings_status = CWV_MENU_STATUS_PREFS_RESET;
        if (ui->host) {
            (void)cwv_host_set_theme(ui->host, cwv_menu_ui_effective_dark(ui));
            (void)cwv_host_set_zoom(ui->host, ui->settings.zoom_level);
            (void)cwv_host_set_scrollbars(ui->host, 1);
        }
        ui_log(ui, "settings: preferences reset to defaults");
        return;
    }
    if (!strcmp(action, "undo")) {
        /* Vita parity: reload persisted prefs (undo restores the last
         * saved state). */
        if (ui->ops && ui->ops->load_prefs &&
            ui->ops->load_prefs(ui->ops->ud) == 0) {
            ui->settings.dirty = 0;
            if (ui->host) (void)cwv_host_set_zoom(ui->host,ui->settings.zoom_level);
        }
        return;
    }
    if (!strcmp(action, "save")) {
        if (ui->ops && ui->ops->save_prefs &&
            ui->ops->save_prefs(ui->ops->ud) == 0)
            ui->settings.dirty = 0;
        return;
    }
    if (!strcmp(action, "exit")) {
        if (ui->mode == CWV_MENU_SETTINGS) cwv_menu_ui_open(ui, CWV_MENU_MAIN);
        else if (ui->settings.show_exit_app && ui->ops && ui->ops->exit_app)
            ui->ops->exit_app(ui->ops->ud);
        return;
    }

    /* devtools tabs */
    {
        static const char *dt_tabs[8] = {
            "performance", "renderer", "input", "page",
            "overrides", "console", "platform", "close"
        };
        int i;
        for (i = 0; i < 8; i++) {
            if (!strcmp(action, dt_tabs[i])) {
                if (i == 7 && !ui->settings.show_exit_app) return;
                if (i == 7) ui->exit_confirm_section_return = ui->dev_section;
                ui->dev_section = i;
                ui->dev_scroll = 0.0f;
                ui->dev_scroll_drag = 0;
                ui->exit_confirm_selected = 0;
                return;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

int cwv_menu_ui_touch(CwvMenuUi *ui, int x, int y, int down)
{
    const VitaMenuTemplate *tpl;
    float mx, my, mw, mh;
    int i;

    if (ui->mode == CWV_MENU_CLOSED) return 0;
    /* real screen pixels in, template pixels out (see CwvMenuUi.scale) */
    if (ui->scale > 0.0f && ui->scale != 1.0f) {
        x = (int)((float)x / ui->scale + 0.5f);
        y = (int)((float)y / ui->scale + 0.5f);
    }
    tpl = active_template(ui);
    menu_frame(tpl, &mx, &my, &mw, &mh);

    if (down == 1) {
        ui->touch_down = 1;
        ui->touch_x = x; ui->touch_y = y;
    } else if (down == 2) {
        if (!ui->touch_down) return 1;
        /* scroll drag on settings/devtools content */
        if (ui->mode == CWV_MENU_SETTINGS) {
            ui->settings_scroll += (float)(ui->touch_y - y);
            /* Four 38px rows fit the 214px viewport; never drag content
             * over tabs or footer when no scrollbar is visible. */
            ui->settings_scroll = 0.0f;
            ui->settings_scroll_drag = 1;
        } else if (ui->mode == CWV_MENU_DEVTOOLS) {
            ui->dev_scroll += (float)(ui->touch_y - y);
            clamp_dev_scroll(ui);
            ui->dev_scroll_drag = 1;
        }
        ui->touch_x = x; ui->touch_y = y;
        return 1;
    } else if (down == 0) {
        ui->touch_down = 0;
        /* Activation happened on press; release only ends capture. */
        ui->settings_scroll_drag = ui->dev_scroll_drag = 0;
        return 1;
    }

    /* tap dispatch (down==0 release with a stable position, or down==1) */
    {
        int tx = x, ty = y;
        const VitaMenuTemplate *t;

        /* Paired window is display-only and drawn above the active menu:
         * swallow its taps before active menu hit-testing. */
        {
            const VitaMenuTemplate *win=tpl_find(tpl,"window");
            const VitaMenuTemplate *partner=NULL;
            size_t count=0;
            float px,py,pw,ph;
            if(win && win->win_pair)
                partner=vita_native_menu_find(win->win_pair,&count);
            if(partner && count && tpl_find(partner,"window")){
                menu_frame(partner,&px,&py,&pw,&ph);
                if(rect_hit(px,py,pw,ph,tx,ty))return 1;
            }
        }
        /* devtools close-confirm swallows taps inside its buttons */
        if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section == 7) {
            t = tpl_find(tpl, "close-no");
            if (t && rect_hit(mx + t->x, my + t->y, t->w, t->h, tx, ty)) {
                ui->exit_confirm_selected = 0;
                return 1;
            }
            t = tpl_find(tpl, "close-yes");
            if (t && rect_hit(mx + t->x, my + t->y, t->w, t->h, tx, ty)) {
                ui->exit_confirm_selected = 1;
                run_action(ui, "exit");
                return 1;
            }
            return 1;
        }

        /* Overrides are navigable by touch as well as D-pad. */
        if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section == 4) {
            for (int row=0;row<7;row++) {
                char name[32];
                snprintf(name,sizeof(name),"content-row-%d",row);
                t=tpl_find(tpl,name);
                if(t && rect_hit(mx+t->x,my+t->y-ui->dev_scroll,
                                 t->w,t->h,tx,ty) &&
                   ty>=my+154 && ty<my+368) {
                    ui->dev_option=row;
                    (void)cwv_menu_ui_input(ui,CWV_MENU_INPUT_RIGHT);
                    return 1;
                }
            }
        }
        /* header/tab/footer stay fixed while content rows scroll. */
        /* every element with an action is tappable (template hit test) */
        for (i = 0; tpl[i].name; i++) {
            const VitaMenuTemplate *e = &tpl[i];
            if (!e->action || !e->action[0]) continue;
            if (ui->mode == CWV_MENU_DEVTOOLS &&
                !ui->settings.show_exit_app && !strcmp(e->action,"close")) continue;
            /* Project-gated rows are never tappable: the cursor_toggle
             * rule above (Vita-only back-touchpad row) plus every feature
             * the project disabled (cwv_project.h, 2026-09-23 (39)). */
            if (menu_row_hidden(ui, e->action)) continue;
            if (e->kind == VITA_MENU_SCROLLBAR) continue;
            if (rect_hit(mx + e->x, my + e->y
                         - ((ui->mode == CWV_MENU_SETTINGS && !strncmp(e->name, "content-row-", 12))
                            ? ui->settings_scroll
                            : (ui->mode == CWV_MENU_DEVTOOLS && !strncmp(e->name, "content-row-", 12))
                            ? ui->dev_scroll : 0.0f),
                         e->w, e->h, tx, ty)) {
                /* Remember which row was touched before slider dispatch. */
                if (ui->mode == CWV_MENU_CUSTOM) {
                    int choice=0;
                    for(const VitaMenuTemplate *a=tpl;a<e;a++)
                        if(a->action && *a->action)choice++;
                    ui->custom_selected=choice;
                }
                if (ui->mode == CWV_MENU_SETTINGS &&
                    !strncmp(e->name, "content-row-", 12)) {
                    ui->settings_item = atoi(e->name + 12);
                    ui->settings_action = -1;
                }
                if (ui->mode == CWV_MENU_DEVTOOLS &&
                    !strncmp(e->name, "content-row-", 12))
                    ui->dev_option = atoi(e->name + 12);
                run_action(ui, e->action);
                return 1;
            }
        }

        /* cancel taps outside the window close one level (Vita parity) */
        if (!rect_hit(mx, my, mw, mh, tx, ty)) {
            cwv_menu_ui_close(ui);
            return 1;
        }
    }
    return 1;
}

int cwv_menu_ui_input(CwvMenuUi *ui, CwvMenuInput in)
{
    if (ui->mode == CWV_MENU_CLOSED) return 0;

    switch (in) {
        case CWV_MENU_INPUT_CANCEL:
            if (ui->mode == CWV_MENU_CUSTOM) {
                cwv_menu_ui_open(ui,CWV_MENU_MAIN);
                return 1;
            }
            if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section == 7) {
                ui->dev_section = ui->exit_confirm_section_return;
                return 1;
            }
            cwv_menu_ui_close(ui);
            return 1;

        case CWV_MENU_INPUT_L1:
        case CWV_MENU_INPUT_R1: {
            int dir = (in == CWV_MENU_INPUT_R1) ? 1 : -1;
            if (ui->mode == CWV_MENU_SETTINGS)
                settings_select_tab(ui, ui->settings_tab + dir);
            else if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section != 7) {
                int next=cycle(ui->dev_section, dir,
                    ui->settings.show_exit_app ? 8 : 7);
                if(next==7)ui->exit_confirm_section_return=ui->dev_section;
                ui->dev_section=next;
                ui->dev_scroll = 0.0f;
            }
            return 1;
        }

        case CWV_MENU_INPUT_CONFIRM:
            switch (ui->mode) {
                case CWV_MENU_MAIN: {
                    /* Template-driven like the touch path: the row element
                     * owns its action, so scripts can re-target main rows
                     * (the More row's open_script: link lives in the .cwm,
                     * not in C).  Missing rows simply do nothing. */
                    static const char *row_names[5] = {
                        "page-operations", "open-url", "settings", "more", "back"
                    };
                    if (ui->main_selected >= 0 && ui->main_selected < 5) {
                        const VitaMenuTemplate *rt =
                            tpl_find(active_template(ui), row_names[ui->main_selected]);
                        if (rt && rt->action) run_action(ui, rt->action);
                    }
                    return 1;
                }
                case CWV_MENU_PAGEOPS:
                    if (ui->pageops_selected >= 0 && ui->pageops_selected < 3) {
                        static const char *acts[3] = {
                            "history-back", "forward", "reload"
                        };
                        run_action(ui, acts[ui->pageops_selected]);
                    } else {
                        run_action(ui, "menu-back");
                    }
                    return 1;
                case CWV_MENU_SETTINGS:
                    if (ui->settings_action >= 0) {
                        static const char *acts[3] = {"undo", "save", "exit"};
                        run_action(ui, acts[ui->settings_action]);
                    } else if (ui->settings_item >= 0) {
                        const VitaMenuTemplate *tpl = active_template(ui);
                        char row_name[32];
                        const VitaMenuTemplate *t;
                        int row = ui->settings_item;
                        snprintf(row_name, sizeof(row_name), "content-row-%d", row);
                        t = tpl_find(tpl, row_name);
                        if (t && t->action) run_action(ui, t->action);
                    }
                    return 1;
                case CWV_MENU_CUSTOM: {
                    const char *action=custom_action_at(ui,ui->custom_selected);
                    if(action)run_action(ui,action);
                    return 1;
                }
                case CWV_MENU_DEVTOOLS:
                    if (ui->dev_section == 7) {
                        if (ui->exit_confirm_selected == 1)
                            run_action(ui, "exit");
                        else {
                            ui->exit_confirm_section_return = ui->dev_section;
                            cwv_menu_ui_close(ui);
                        }
                        return 1;
                    }
                    if (ui->dev_section == 4) {
                        /* LEFT/RIGHT cycle the selected override (below) */
                        return 1;
                    }
                    return 1;
                default:
                    return 1;
            }

        case CWV_MENU_INPUT_UP:
            if (ui->mode == CWV_MENU_CUSTOM) {
                int count=custom_action_count(ui);
                if(count)ui->custom_selected=cycle(ui->custom_selected,-1,count);
            } else if (ui->mode == CWV_MENU_MAIN) {
                ui->main_selected = cycle(ui->main_selected, -1, 5);
            } else if (ui->mode == CWV_MENU_PAGEOPS) {
                ui->pageops_selected = cycle(ui->pageops_selected, -1, 4);
            } else if (ui->mode == CWV_MENU_SETTINGS) {
                if (ui->settings_action >= 0) {
                    int last = settings_row_last_visible(ui, ui->settings_tab);
                    ui->settings_action = -1;
                    ui->settings_item = last < 0 ? 0 : last;
                } else {
                    /* Walk up over rows the project hid (or the script
                     * dropped); the row budget keeps the shipped layout. */
                    int idx = ui->settings_item;
                    do { idx--; } while (idx > 0 &&
                           !settings_row_visible(ui, ui->settings_tab, idx));
                    ui->settings_item = idx < 0 ? 0 : idx;
                }
            } else if (ui->mode == CWV_MENU_DEVTOOLS) {
                if (ui->dev_section == 4)
                    ui->dev_option = cycle(ui->dev_option, -1, 7);
            }
            return 1;

        case CWV_MENU_INPUT_DOWN:
            if (ui->mode == CWV_MENU_CUSTOM) {
                int count=custom_action_count(ui);
                if(count)ui->custom_selected=cycle(ui->custom_selected,1,count);
            } else if (ui->mode == CWV_MENU_MAIN) {
                ui->main_selected = cycle(ui->main_selected, 1, 5);
            } else if (ui->mode == CWV_MENU_PAGEOPS) {
                ui->pageops_selected = cycle(ui->pageops_selected, 1, 4);
            } else if (ui->mode == CWV_MENU_SETTINGS) {
                if (ui->settings_action >= 0) {
                    /* already on actions; nothing below */
                } else {
                    int last = settings_row_last_visible(ui, ui->settings_tab);
                    int idx = ui->settings_item;
                    if (ui->settings_item < 0 || ui->settings_item >= last) {
                        ui->settings_action = 0;
                        ui->settings_item = -1;
                    } else {
                        /* Walk down over rows the project hid (or the
                         * script dropped), then hand off to the actions. */
                        do { idx++; } while (idx < last &&
                               !settings_row_visible(ui, ui->settings_tab, idx));
                        ui->settings_item = idx;
                    }
                }
            } else if (ui->mode == CWV_MENU_DEVTOOLS) {
                if (ui->dev_section == 4)
                    ui->dev_option = cycle(ui->dev_option, 1, 7);
            }
            return 1;

        case CWV_MENU_INPUT_LEFT:
        case CWV_MENU_INPUT_RIGHT: {
            int dir = (in == CWV_MENU_INPUT_RIGHT) ? 1 : -1;
            if (ui->mode == CWV_MENU_SETTINGS) {
                if (ui->settings_action >= 0) {
                    ui->settings_action = cycle(ui->settings_action, dir, 3);
                } else {
                    const VitaMenuTemplate *tpl = active_template(ui);
                    char row_name[32];
                    const VitaMenuTemplate *t;
                    int row = ui->settings_item < 0 ? 0 : ui->settings_item;
                    snprintf(row_name, sizeof(row_name), "content-row-%d", row);
                    t = tpl_find(tpl, row_name);
                    if (t && t->action) {
                        /* cycle the row's value through its action */
                        if (!strcmp(t->action, "toggle_theme")) {
                            ui->settings.dark_mode = dir > 0;
                            ui->settings.dirty = 1;
                        } else if (!strcmp(t->action, "cycle_accent")) {
                            ui->settings.accent = cycle(ui->settings.accent, dir, 6);
                            ui->settings.dirty = 1;
                        } else if (!strcmp(t->action, "set_zoom")) {
                            int stops = template_stops(t);
                            int v = cycle(zoom_stop_index(ui, t), dir, stops);
                            ui->settings.zoom_level = zoom_from_stop(v, stops);
                            if (ui->host) (void)cwv_host_set_zoom(ui->host,ui->settings.zoom_level);
                            ui->settings.dirty = 1;
                        } else if (!strcmp(t->action, "cycle_scroll_input")) {
                            ui->settings.scroll_mode = cycle(ui->settings.scroll_mode, dir, 3);
                            ui->settings.dirty = 1;
                        } else if (!strcmp(t->action, "cycle_zoom_input")) {
                            ui->settings.zoom_mode = cycle(ui->settings.zoom_mode, dir, 3);
                            ui->settings.dirty = 1;
                        } else if (!strcmp(t->action, "cycle_menu_hotkey")) {
                            if (!ui->fixed_hotkey)
                                ui->settings.menu_hotkey=cycle(ui->settings.menu_hotkey,dir,3);
                            ui->settings.dirty=1;
                        } else if (!strcmp(t->action, "toggle_exit_app")) {
                            ui->settings.show_exit_app=dir>0;
                            ui->settings.dirty=1;
                        } else if (!strcmp(t->action, "toggle_css_cursors")) {
                            ui->settings.css_cursors=dir>0;
                            ui->settings.dirty=1;
                        } else if (!strcmp(t->action, "set_scroll_speed")) {
                            int stops = template_stops(t);
                            ui->settings.scroll_speed =
                                cycle(ui->settings.scroll_speed, dir,
                                      stops > 3 ? 3 : stops);
                            ui->settings.dirty = 1;
                        }
                    }
                }
            } else if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section == 4) {
                /* cycle the selected override row */
                switch (ui->dev_option) {
                    case 0: {   /* Default -> Browser -> Standalone -> Touch
                                 * -> Cursor (backend VCM numbering, -1 = default) */
                        int v = cycle(ui->overrides.control_mode < 0 ? 0
                                          : ui->overrides.control_mode,
                                      dir, 5);
                        ui->overrides.control_mode = v == 0 ? -1 : v;
                        break;
                    }
                    case 1: ui->overrides.scroll = cycle(
                                 ui->overrides.scroll < 0 ? 2 : ui->overrides.scroll,
                                 dir, 3); break;
                    case 2: ui->overrides.zoom = cycle(
                                 ui->overrides.zoom < 0 ? 2 : ui->overrides.zoom,
                                 dir, 3); break;
                    case 3: ui->overrides.theme = cycle(
                                 ui->overrides.theme < 0 ? 1 : ui->overrides.theme,
                                 dir, 2); break;
                    case 4: ui->overrides.scrollbars = cycle(
                                 ui->overrides.scrollbars < 0 ? 1 : ui->overrides.scrollbars,
                                 dir, 2); break;
                    case 5: {
                        int stops = 6;
                        int v = ui->overrides.zoom_level > 0.0f
                            ? (int)((ui->overrides.zoom_level - 0.75f) / 0.25f + 0.5f)
                            : 0;
                        v = cycle(v, dir, stops);
                        ui->overrides.zoom_level = v == 0 ? 0.0f : zoom_from_stop(v, stops);
                        break;
                    }
                    case 6: {
                        /* button wait presets: default, release, none, 1s, 1.5s 0.5s */
                        static const char *presets[4] = {"release", "none", "1s", "1.5s 0.5s"};
                        int v = ui->overrides.button_wait < 0
                            ? -1 : cycle(ui->overrides.button_wait, dir, 4);
                        if (v < 0) v = 0;
                        if (dir < 0 && ui->overrides.button_wait == 0) v = -1;
                        ui->overrides.button_wait = v;
                        if (v >= 0)
                            snprintf(ui->overrides.button_wait_spec,
                                     sizeof(ui->overrides.button_wait_spec),
                                     "%s", presets[v]);
                        else
                            ui->overrides.button_wait_spec[0] = 0;
                        break;
                    }
                    default: break;
                }
            }
            return 1;
        }

        default:
            return 1;
    }
}

void cwv_menu_ui_scroll(CwvMenuUi *ui, float delta)
{
    if (!ui || ui->mode != CWV_MENU_DEVTOOLS) return;
    ui->dev_scroll += delta;
    clamp_dev_scroll(ui);
}

/* ------------------------------------------------------------------ */
/* lifecycle + runtime                                                  */
/* ------------------------------------------------------------------ */

void cwv_menu_ui_init(CwvMenuUi *ui, const CwvMenuPaint *paint,
                      const CwvMenuPlatformOps *ops, CwvHost *host,
                      int screen_w, int screen_h)
{
    memset(ui, 0, sizeof(*ui));
    ui->mode = CWV_MENU_CLOSED;
    ui->paint = paint;
    ui->ops = ops;
    ui->host = host;
    ui->screen_w = screen_w;
    ui->screen_h = screen_h;
    ui->scale = 1.0f;
    ui->cursor_toggle = 0;
    ui->fixed_hotkey = 0;
    settings_defaults(ui);
    overrides_defaults(ui);
    /* UI-default selections (Vita native_menu_open parity) */
    ui->main_selected = 0;
    ui->pageops_selected = 0;
    ui->settings_item = 0;
    ui->settings_action = -1;
    ui->dev_option = 0;
    ui->exit_confirm_selected = 0;
    ui->console_lines_last = -1;
    ui->console_errors_last = ui->console_warns_last = -1;
    ui->history_index = -1;
}

void cwv_menu_ui_set_scale(CwvMenuUi *ui, float scale)
{
    if (!ui) return;
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 1.25f) scale = 1.25f;
    ui->scale = scale;
}

void cwv_menu_ui_open(CwvMenuUi *ui, CwvMenuMode mode)
{
    ui->mode = mode;
    switch (mode) {
        case CWV_MENU_MAIN:
            ui->main_selected = 0;
            break;
        case CWV_MENU_PAGEOPS:
            ui->pageops_selected = 0;
            break;
        case CWV_MENU_SETTINGS:
            settings_select_tab(ui, 0);
            ui->settings_status = CWV_MENU_STATUS_NONE;
            break;
        case CWV_MENU_CUSTOM:
            ui->custom_selected=0;
            break;
        case CWV_MENU_DEVTOOLS:
            ui->dev_scroll = 0.0f;
            ui->dev_scroll_drag = 0;
            ui->exit_confirm_selected = 0;
            break;
        default:
            break;
    }
}

int cwv_menu_ui_open_named(CwvMenuUi *ui,const char *name)
{
    size_t count=0;
    if(!ui || !name || !*name || strlen(name)>=sizeof(ui->custom_name) ||
       !vita_native_menu_find(name,&count) || !count)return -1;
    snprintf(ui->custom_name,sizeof(ui->custom_name),"%s",name);
    cwv_menu_ui_open(ui,CWV_MENU_CUSTOM);
    return 0;
}

void cwv_menu_ui_close(CwvMenuUi *ui)
{
    ui->mode = CWV_MENU_CLOSED;
}

CwvMenuMode cwv_menu_ui_mode(const CwvMenuUi *ui)
{
    return ui->mode;
}

/* CWMenu 4 linked submenus: elements named like the main menu's page
 * identity card (page-icon / page-info) draw LIVE data in CUSTOM menus
 * instead of static template labels, so a linked "modified main menu"
 * shows the real page just like the stock one.  Drawn by the custom
 * pass below; all other names fall through to the generic renderer. */
static int draw_custom_live_meta(const CwvMenuUi *ui, const VitaMenuTemplate *t,
                                 float x, float y)
{
    float bx = x + t->x, by = y + t->y;
    if (!strcmp(t->name, "page-icon")) {
        paint_rect(ui, bx, by, t->w, t->h, style_theme_color(ui, t, 0.25f));
        paint_rect(ui, bx, by, t->w, 2.0f, style_accent_color(ui, t, 1));
        if (ui->host && ui->paint && ui->paint->favicon) {
            const char *favicon = cwv_host_favicon_url(ui->host);
            ui->paint->favicon(bx + 4.0f, by + 4.0f,
                               t->w - 8.0f, t->h - 8.0f,
                               favicon ? favicon : "", ui->paint->ud);
        }
        return 1;
    }
    if (!strcmp(t->name, "page-info")) {
        const char *pt = ui->host ? cwv_host_page_title(ui->host) : "";
        const char *pu = ui->host ? cwv_host_current_url(ui->host) : "";
        char mode_line[64];
        paint_text(ui, bx, by + 16.0f, RGBA8(245, 245, 250, 255), 0.66f,
                   (pt && *pt) ? pt : "Untitled");
        paint_text(ui, bx, by + 32.0f, RGBA8(185, 195, 210, 255), 0.54f,
                   (pu && *pu) ? pu : "about:blank");
        snprintf(mode_line, sizeof(mode_line), "Mode: %s",
                 cwv_control_mode_name(ui->host ? cwv_host_control_mode(ui->host) : 0));
        paint_text(ui, bx, by + 46.0f, RGBA8(185, 195, 210, 255), 0.54f, mode_line);
        return 1;
    }
    return 0;
}

void cwv_menu_ui_draw(CwvMenuUi *ui, const CwvMenuData *data)
{
    const VitaMenuTemplate *tpl;
    const VitaMenuTemplate *shell_win;
    float x, y, w, h;
    float shell_scale = 1.0f;
    const VitaMenuTemplate *header;
    int orr = 0, ogg = 0, obb = 0;

    if (ui->mode == CWV_MENU_CLOSED) return;
    if (ui->mode == CWV_MENU_DEVTOOLS && ui->dev_section == 5) {
        console_cache_rebuild(ui, ui->host);
        clamp_dev_scroll(ui);
    }
    tpl = active_template(ui);
    menu_frame(tpl, &x, &y, &w, &h);

    /* overlay backdrop (theme `overlay` owns the rgb; alpha fixed).
     * screen_w/h are REAL pixels: undo the paint-wrapper scale so the
     * backdrop still covers the whole screen on scaled-down ports. */
    (void)cwm_theme_get_rgb("overlay", &orr, &ogg, &obb);
    paint_rect(ui, 0.0f, 0.0f,
               (float)ui->screen_w / menu_scale(ui),
               (float)ui->screen_h / menu_scale(ui),
               RGBA8(orr, ogg, obb, 120));

    /* window chrome: corner + size keys */
    shell_win = tpl_find(tpl, "window");
    if (shell_win && shell_win->win_scale > 0.0f) {
        shell_scale = shell_win->win_scale;
        if (shell_scale < 0.75f) shell_scale = 0.75f;
        if (shell_scale > 1.25f) shell_scale = 1.25f;
    }
    if (shell_win && shell_win->win_corner == 1)
        paint_rounded_rect(ui, x, y, w, h, 12.0f * shell_scale,
                           style_theme_color(ui, shell_win, 0.24f));
    else
        paint_rect(ui, x, y, w, h, style_theme_color(ui, shell_win, 0.24f));

    /* header bar */
    header = tpl_find(tpl, "header");
    {
        float hx = x + (header ? header->x : 0.0f);
        float hy = y + (header ? header->y : 0.0f);
        float hw = header ? header->w : w;
        float hh = (header ? header->h : 44.0f) * shell_scale;
        paint_rect(ui, hx, hy, hw, hh, style_theme_color(ui, header, 0.48f));
        if (header && header->label)
            paint_text(ui, hx + 18.0f, hy + 29.0f * shell_scale,
                       style_text_color(header), 1.0f * shell_scale, header->label);
    }

    /* mode content */
    switch (ui->mode) {
        case CWV_MENU_MAIN:
            draw_main_menu(ui, ui->host, x, y);
            break;
        case CWV_MENU_PAGEOPS:
            draw_pageops(ui, ui->host, x, y);
            break;
        case CWV_MENU_SETTINGS:
            draw_settings(ui, x, y);
            break;
        case CWV_MENU_DEVTOOLS:
            draw_devtools(ui, ui->host, x, y, data);
            break;
        case CWV_MENU_CUSTOM: {
            int choice=0;
            for(const VitaMenuTemplate *t=tpl;t && t->name;t++) {
                if(t->kind==VITA_MENU_WINDOW || t->kind==VITA_MENU_HEADER)continue;
                if(!draw_custom_live_meta(ui,t,x,y))
                    draw_template_item(ui,t,x,y,
                        t->action && *t->action && choice==ui->custom_selected);
                if(t->action && *t->action)choice++;
            }
            break;
        }
        default:
            break;
    }
    if(ui->mode != CWV_MENU_CUSTOM)draw_template_extras(ui,tpl,x,y);
    draw_pair_window(ui,tpl); /* passive partner sits above active menu */
}

int cwv_menu_ui_effective_dark(const CwvMenuUi *ui)
{
    return ui->overrides.theme >= 0 ? ui->overrides.theme
                                    : ui->settings.dark_mode;
}

int cwv_menu_ui_effective_scroll_mode(const CwvMenuUi *ui)
{
    return ui->overrides.scroll >= 0 ? ui->overrides.scroll
                                     : ui->settings.scroll_mode;
}

int cwv_menu_ui_effective_zoom_mode(const CwvMenuUi *ui)
{
    return ui->overrides.zoom >= 0 ? ui->overrides.zoom
                                    : ui->settings.zoom_mode;
}

void cwv_menu_ui_apply_runtime(CwvMenuUi *ui)
{
    if (!ui->host) return;
    cwv_menu_ui_record_page(ui);
    (void)cwv_host_set_theme(ui->host, cwv_menu_ui_effective_dark(ui));
    (void)cwv_host_set_scrollbars(ui->host,
        ui->overrides.scrollbars < 0 ? 1 : ui->overrides.scrollbars);
    /* A physical zoom change is live state; only the devtools override
     * may force a temporary zoom every frame (Vita parity). */
    if (ui->overrides.zoom_level > 0.0f)
        (void)cwv_host_set_zoom(ui->host, ui->overrides.zoom_level);
    if (ui->overrides.button_wait >= 0)
        (void)cwv_host_set_button_wait_override(ui->host,
                                                ui->overrides.button_wait_spec);
}

/* Vita-parity bounded history, tracking page-initiated navigation and loads
 * from the Wii U IME.  Back/forward update history_index only on a
 * successful load and do not append duplicates. */
void cwv_menu_ui_record_page(CwvMenuUi *ui)
{
    const char *url;
    if (!ui || !ui->host) return;
    url = cwv_host_current_url(ui->host);
    if (!url || !*url) return;
    if (ui->history_index >= 0 &&
        !strcmp(ui->history[ui->history_index], url)) return;
    if (ui->history_index + 1 < ui->history_count)
        ui->history_count = ui->history_index + 1;
    if (ui->history_count >= 16) {
        memmove(ui->history[0], ui->history[1], 15 * sizeof(ui->history[0]));
        ui->history_count = 15;
    }
    snprintf(ui->history[ui->history_count], sizeof(ui->history[0]), "%s", url);
    ui->history_index = ui->history_count++;
}
