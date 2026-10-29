#ifndef CWV_PROJECT_H
#define CWV_PROJECT_H

#include "menu/cwmenu_api.h"

/* ============================================================================
 * CWebView - project feature keys (cwv_project.h, 2026-09-23 (39))
 * ============================================================================
 * Per-console feature switches a WEBAPP sets in package.json, e.g.
 *   "wiiu": { "zoom": false }
 * Every key defaults to ON when absent (the build only passes a define
 * when the project explicitly disables it), so existing apps and the
 * desktop tests compile unchanged.
 *
 * The build injects the values as compile-time defines:
 *   build-vita.sh   -> VITA_<KEY>_ENABLED cache var -> add_definitions
 *   build-wiiu.sh   -> CFLAGS -DCWV_<KEY>_ENABLED=0/1
 *   build-wii.sh    -> same (packaging lands with the app loop port)
 *
 * Engine effect of each key:
 *   cursor    no pointer cursor at all (no art staged, no draw, no input)
 *   zoom      zoom actions/keys are dead and the zoom settings rows vanish
 *   scroll    scrolling is disabled via CSS (overflow: hidden forced onto
 *             html/body AFTER the page's own sheets, so it wins) and the
 *             scroll settings rows vanish
 *   devtools  the DevTools menu entry is removed and the action is dead
 *   history   history back/forward are dead and their rows vanish
 *
 * Menu rows disappear through BOTH channels: the CWMenu 3 `when <expr>`
 * conditional drops them from the loaded script, and this compile-time
 * gating covers the no-script C fallback templates + action dispatch.
 * ========================================================================== */

#ifndef CWV_CURSOR_ENABLED
#define CWV_CURSOR_ENABLED 1
#endif
#ifndef CWV_ZOOM_ENABLED
#define CWV_ZOOM_ENABLED 1
#endif
#ifndef CWV_SCROLL_ENABLED
#define CWV_SCROLL_ENABLED 1
#endif
#ifndef CWV_DEVTOOLS_ENABLED
#define CWV_DEVTOOLS_ENABLED 1
#endif
/* (84) Files menu: web-content tree browser (Vita native menu). */
#ifndef CWV_FILES_ENABLED
#define CWV_FILES_ENABLED 1
#endif
#ifndef CWV_HISTORY_ENABLED
#define CWV_HISTORY_ENABLED 1
#endif

/* Push the effective values into the CWMenu project-variable store as
 * DEFAULTS (fill-only).  vita_native_menu_templates_load() calls this
 * before the script parse so `when <expr>` conditionals see the values.
 * Everything uses fill-only here and in the script's `project` block,
 * so the precedence is a single rule: cwm_project_set (host/runtime
 * injection) ALWAYS wins over compile flags and script defaults. */
static inline void cwv_project_publish(void)
{
    (void)cwm_project_set_default("cursor",   CWV_CURSOR_ENABLED   ? "on" : "off");
    (void)cwm_project_set_default("zoom",     CWV_ZOOM_ENABLED     ? "on" : "off");
    (void)cwm_project_set_default("scroll",   CWV_SCROLL_ENABLED   ? "on" : "off");
    (void)cwm_project_set_default("devtools", CWV_DEVTOOLS_ENABLED ? "on" : "off");
#if CWV_FILES_ENABLED
    (void)cwm_project_set_default("files", "on");
#endif
    (void)cwm_project_set_default("history",  CWV_HISTORY_ENABLED  ? "on" : "off");
}

#endif /* CWV_PROJECT_H */
