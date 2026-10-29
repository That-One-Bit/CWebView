#ifndef CWM_MENU_API_H
#define CWM_MENU_API_H

#include <stddef.h>

/* This is the complete source-tree API header paired with src/cwmenu_api.c.
 * It is intentionally self-contained; it is not a forwarding/pointer header. */

/* CWMenu (Vita Webview Menu) is the declarative native-menu layer.  Menu objects
 * live in .cwm scripts; the API parses them into renderer-neutral templates. */
#define CWM_MENU_TEMPLATE_CAPACITY 96
#define CWM_MENU_LABEL_CAPACITY 256

/* ---- Script versioning ------------------------------------------------
 * A .cwm file starts with `CWMenu <number>` (a file without a header is
 * treated as version 1).  The controller PARSES any version it is given -
 * unknown lines are ignored, so a newer script degrades gracefully - and
 * exposes both numbers so the host can log a warning instead of crashing:
 *   cwmenu_script_version()      -> version of the loaded script
 *   cwm_script_version_supported() -> highest version THIS controller knows
 * Version history:
 *   1: menus, elements, actions, reactive `var` values
 *   2: `theme` block (colors + accent labels owned by the script, not C)
 *   3: `project` block + `when <expr>` element conditionals (feature
 *      switches a project sets: cursor / zoom / scroll / devtools / history,
 *      all default ON when the project says nothing)
 *   4: script LINKING - a loaded script can open another .cwm at runtime
 *      (cwmenu_script_link + the host `open_script:` action; see below) */
#define CWM_SCRIPT_VERSION_SUPPORTED 4
int cwmenu_script_version(void);
int cwm_script_version_supported(void);

/* ---- Project variables (CWMenu 3) ---------------------------------------
 * Project feature switches (see the `project` block below).  The HOST
 * injects the effective values with cwm_project_set() BEFORE loading a
 * script (values from the package.json keys, e.g. `"zoom": false`);
 * `when <expr>` element conditionals then drop every element whose
 * switch is off, so the menu simply does not contain those items.
 * Script `project` block lines only fill variables the host has NOT
 * set, so a script can carry its own defaults for standalone use.
 *
 * Value truthiness: "on"/"true"/"1"/"yes" = on; "off"/"false"/"0"/"no"
 * = off; any other non-empty value = on; unset = off (for `when`).
 *
 * `when <expr>` sits after the element label like any other key:
 *   element content-row-1 text 40 196 660 38 normal "Zoom input" action cycle_zoom_input when zoom
 * expr = a variable name, or `!name` for negation.  An element whose
 * condition is false is dropped at parse time (it never reaches the
 * host; older hosts that never see the variable drop it too, which is
 * exactly the wanted behavior for a disabled feature). */
int cwm_project_set(const char *name, const char *value);  /* host injection, wins */
int cwm_project_set_default(const char *name, const char *value); /* only when unset */
int cwm_project_get(const char *name, const char **value);  /* 0 = found */
int cwm_project_is_on(const char *name);                    /* truthy eval */
void cwm_project_clear(void);                               /* reset all */

/* ---- Theme block (CWMenu 2) -----------------------------------------------
 * `theme { accent0 #466996 ... }` moves every menu color decision from
 * hardcoded C into the .cwm script.  Applications ship their own palette
 * simply by editing these lines; the C side keeps built-in fallbacks so
 * CWMenu 1 scripts (or partial themes) still render.  Slot names:
 *   accent0..accent5  the six accent palettes cycled by Settings
 *   label0..label5    display names for the accents (as quoted strings)
 *   text-normal/light/dim   style text colors
 *   neutral           base color the accent tint mixes toward
 *   overlay           backdrop dim behind an open menu
 * Any other name is accepted and stored too - lookups are by name. */
int cwm_theme_get_rgb(const char *slot, int *r, int *g, int *b);   /* 0 = found */
const char *cwm_theme_get_label(int accent_index);                  /* NULL = none */
int cwm_theme_count(void);



typedef enum VitaMenuStyle {
    VITA_MENU_STYLE_NORMAL,
    VITA_MENU_STYLE_LIGHT,
    VITA_MENU_STYLE_DIM
} VitaMenuStyle;

typedef enum VitaMenuElementKind {
    VITA_MENU_WINDOW,
    VITA_MENU_PANEL,
    VITA_MENU_HEADER,
    VITA_MENU_BUTTON,
    VITA_MENU_TEXT,
    VITA_MENU_UNDERLINE,
    VITA_MENU_SLIDER,
    VITA_MENU_CHECKBOX,
    VITA_MENU_SEGMENTED,
    VITA_MENU_BADGE,
    VITA_MENU_FOOTER,
    VITA_MENU_DECORATION,
    VITA_MENU_ICON,
    VITA_MENU_PROGRESS,
    VITA_MENU_VALUE,
    VITA_MENU_LIST,
    VITA_MENU_SHAPE_RECTANGLE,
    VITA_MENU_SHAPE_ROUNDED_RECTANGLE,
    VITA_MENU_SHAPE_CIRCLE,
    VITA_MENU_LINE,
    VITA_MENU_SCROLLBAR
} VitaMenuElementKind;

typedef struct VitaMenuTemplate {
    const char *name;
    float x, y, w, h;
    VitaMenuElementKind kind;
    const char *label;
    const char *action;
    VitaMenuStyle style;
    int stops; /* Slider stopping points; 0 means the object default. Maximum 6. */
    float brightness; /* 0.25..2.0; 0 means use style default. */
    const char *brightness_var; /* Optional CWMenu numeric variable. */
    float tint; /* 0..1 accent tint; -1 means use the normal/full accent. */
    const char *tint_var; /* Optional CWMenu numeric variable. */
    const char *stops_var; /* Optional CWMenu numeric variable. */
    const char *scroll_owner; /* Optional scrollbar group owner. */
    const char *scroll_items; /* Optional | separated item names owned by a scrollbar. */
    /* ---- CWMenu 2 window specification -----------------------------------
     * Optional keys on the `window` element (ignored by other kinds):
     *   anchor <ax> <ay>   absolute screen anchor for the window rect;
     *                      without it the host's default base applies
     *   size <scale>       window chrome scale, clamped 0.75..1.25
     *                      (absent/0 = 1.0)
     *   corner rounded|sharp  chrome corner style (default sharp = legacy)
     *   crop <l> <t> <r> <b>   chrome insets; the window rect shrinks by
     *                          these amounts and content past the cropped
     *                          edge is not guaranteed visible
     *   pair <menu>        dual window: also render the named menu's
     *                      window (one level deep; the partner's own anchor/
     *                      size/corner/crop apply; the active menu keeps
     *                      all input) */
    float win_anchor_x, win_anchor_y;
    int win_anchor_set;    /* 0 = use the host default anchor */
    float win_scale;       /* 0 = 1.0 */
    int win_corner;        /* 0 = sharp (legacy), 1 = rounded */
    float crop_l, crop_t, crop_r, crop_b;
    int crop_set;
    const char *win_pair;  /* dual-window partner menu name (NULL = single) */
    /* CWMenu `border` key on any element: comma/space separated sides
     * (top=1, right=2, bottom=4, left=8, all=15, fill=16, none=0).
     * Text elements default to 0 = plain text with no panel or edges. */
    int border_flags;
} VitaMenuTemplate;

typedef struct CWMenuMenuScriptTarget {
    const char *menu_name;
    VitaMenuTemplate *items;
    size_t capacity;
    size_t count;
} CWMenuMenuScriptTarget;

/* Loads every `menu` block in a CWMenu script. Existing targets are cleared. */
int cwmenu_script_load(const char *path, CWMenuMenuScriptTarget *targets,
                         size_t target_count);

/* ---- Script linking (CWMenu 4) ---------------------------------------
 * Parses a SECOND .cwm INTO the state the primary script built, instead
 * of replacing it: theme slots and project defaults merge (existing
 * values win for project defaults, per-slot last-write-wins for theme),
 * vars merge, and every `menu <name>` block REPLACES that menu - a
 * compiled-in target is refilled, an app-defined dynamic menu is
 * re-registered under the same name.  The linked script's version is
 * folded into the reported maximum.  Hosts trigger this through the
 * `open_script:<file>` element action (the port resolves <file> against
 * its cwmenu directory and calls this); see cwv_menu_ui.h. */
int cwmenu_script_link(const char *path, CWMenuMenuScriptTarget *targets,
                       size_t target_count);

/* ---- Dynamic menus ------------------------------------------------------
 * `menu` blocks whose name is not one of the compiled-in targets are kept
 * in a registry instead of being dropped.  Hosts built on Vita WebView
 * can define their own app-specific menus and read them back here. */
const VitaMenuTemplate *cwm_dynamic_menu(const char *name, size_t *count);
int cwm_dynamic_menu_total(void);
const char *cwm_dynamic_menu_name(int index);
/* How many elements a named menu received from the loaded script: -1 =
 * unknown name / no script, 0 = the script removed or renamed that menu
 * (hosts use this to warn or hide the entry).  name == NULL returns the
 * number of compiled-in targets. */
int cwmenu_script_menu_count(const char *name);

/* Small reactive state store used by CWMenu properties/actions. */
int cwm_var_set_number(const char *name, float value);
int cwm_var_get_number(const char *name, float *value);

/* Scrollbar primitive shared by every CWMenu menu. */
typedef struct CWMenuMenuScrollbar {
    float content_size;
    float viewport_size;
    float offset;
    float min_thumb_size;
    float track_size;
} CWMenuMenuScrollbar;

void cwm_scrollbar_init(CWMenuMenuScrollbar *bar, float min_thumb_size);
void cwm_scrollbar_set(CWMenuMenuScrollbar *bar, float content_size,
                       float viewport_size, float offset, float track_size);
float cwm_scrollbar_max(const CWMenuMenuScrollbar *bar);
int cwm_scrollbar_visible(const CWMenuMenuScrollbar *bar);
float cwm_scrollbar_thumb_size(const CWMenuMenuScrollbar *bar);
float cwm_scrollbar_thumb_position(const CWMenuMenuScrollbar *bar);
void cwm_scrollbar_set_offset(CWMenuMenuScrollbar *bar, float offset);
float cwm_scrollbar_offset_from_position(const CWMenuMenuScrollbar *bar,
                                         float position);

#endif
