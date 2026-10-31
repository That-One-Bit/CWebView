#include "native_menu_templates.h"
#include "cwv_project.h"
#include <stdio.h>
#include <string.h>

/* The menu layout is no longer compiled into main.c/native_menu_templates.c.
 * These are runtime-backed CWMenu script surfaces.  Keeping fixed arrays here
 * preserves the existing renderer ABI while allowing .cwm files to define
 * buttons, controls, decorations, shapes, progress bars, etc. */
VitaMenuTemplate vita_menu_main_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_page_operations_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_settings_appearance_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_settings_controls_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_settings_preferences_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_performance_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_renderer_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_input_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_page_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_overrides_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_console_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_platform_template[VITA_MENU_TEMPLATE_CAPACITY];
VitaMenuTemplate vita_menu_devtools_close_template[VITA_MENU_TEMPLATE_CAPACITY];
static CWMenuMenuScriptTarget g_targets[] = {
    {"main", vita_menu_main_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"page_operations", vita_menu_page_operations_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"settings_appearance", vita_menu_settings_appearance_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"settings_controls", vita_menu_settings_controls_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"settings_preferences", vita_menu_settings_preferences_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_performance", vita_menu_devtools_performance_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_renderer", vita_menu_devtools_renderer_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_input", vita_menu_devtools_input_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_page", vita_menu_devtools_page_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_overrides", vita_menu_devtools_overrides_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_console", vita_menu_devtools_console_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_platform", vita_menu_devtools_platform_template, VITA_MENU_TEMPLATE_CAPACITY, 0},
    {"devtools_close", vita_menu_devtools_close_template, VITA_MENU_TEMPLATE_CAPACITY, 0}
};

int vita_native_menu_templates_load(const char *path)
{
    if(!path || !*path) return -1;
    /* Publish the build's project feature keys (cwv_project.h) BEFORE the
     * script parse so CWMenu 3 `when <expr>` conditionals drop the rows of
     * every feature this project disabled (2026-09-23 (39)).  The values
     * come from the compile-time defines of THIS translation unit, i.e.
     * the console-specific flags the build scripts pass. */
    cwv_project_publish();
    return cwmenu_script_load(path, g_targets,
                                sizeof(g_targets)/sizeof(g_targets[0]));
}

/* CWMenu 4: link a second script in (menus replace, theme merges). */
int vita_native_menu_templates_link(const char *path)
{
    if(!path || !*path) return -1;
    /* publish is fill-only (cwv_project.h), so re-running it is a no-op;
     * it is kept here so a linked script also sees the feature gates. */
    cwv_project_publish();
    return cwmenu_script_link(path, g_targets,
                               sizeof(g_targets)/sizeof(g_targets[0]));
}

/* Elements a named menu received from the loaded .cwm script.
 * -1 = unknown name, 0 = removed/renamed by the script. */
int vita_native_menu_count(const char *name)
{
    return cwmenu_script_menu_count(name);
}

/* Any loaded menu by name: compiled-in targets, then the dynamic registry
 * (app-defined menus that no target claimed). */
const VitaMenuTemplate *vita_native_menu_find(const char *name, size_t *count)
{
    size_t i, n = sizeof(g_targets)/sizeof(g_targets[0]);
    if(!name || !*name) return NULL;
    for(i=0;i<n;i++){
        if(!strcmp(g_targets[i].menu_name,name)){
            if(count) *count = g_targets[i].count;
            return g_targets[i].items;
        }
    }
    return cwm_dynamic_menu(name,count);
}
