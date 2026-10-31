#ifndef VITA_NATIVE_MENU_TEMPLATES_H
#define VITA_NATIVE_MENU_TEMPLATES_H

#include "cwmenu_api.h"

/* CWMenu script-backed templates. The arrays are populated from the packaged
 * .cwm script at startup; the C renderer only consumes the resulting visual
 * contracts. */
#define VITA_MENU_TEMPLATE_CAPACITY CWM_MENU_TEMPLATE_CAPACITY

extern VitaMenuTemplate vita_menu_main_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_page_operations_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_settings_appearance_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_settings_controls_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_settings_preferences_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_performance_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_renderer_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_input_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_page_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_overrides_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_console_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_platform_template[VITA_MENU_TEMPLATE_CAPACITY];
extern VitaMenuTemplate vita_menu_devtools_close_template[VITA_MENU_TEMPLATE_CAPACITY];

/* Loads the default CWMenu menu script. If it cannot be loaded, the caller can
 * fall back to the built-in renderer-safe defaults. */
int vita_native_menu_templates_load(const char *path);
/* CWMenu 4 linking: parse a second .cwm INTO the loaded state (menu
 * blocks replace; theme/project/vars merge) without a full reset. */
int vita_native_menu_templates_link(const char *path);

/* Elements a named menu received from the loaded .cwm script
 * (-1 unknown, 0 removed by the script). */
int vita_native_menu_count(const char *name);

/* Look up any loaded menu by name: compiled-in targets first, then the
 * dynamic (app-defined) registry. NULL = no such menu. */
const VitaMenuTemplate *vita_native_menu_find(const char *name, size_t *count);

#endif
