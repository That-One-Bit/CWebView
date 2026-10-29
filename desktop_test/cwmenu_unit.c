/* cwmenu_unit.c - desktop unit test for the CWMenu 2 script parser.
 *
 * Loads cwmenu/default.cwm against the same target list the app uses and
 * verifies, in order:
 *   1. version header          - cwmenu_script_version() == 2
 *   2. theme block             - accent/label/text/neutral/overlay slots
 *   3. window keys             - anchor / size / corner / crop keys on a
 *                                 probe window (embedded script; the Menu
 *                                 Tests demo menus were removed)
 *   4. dual-window partner     - `pair` menu resolves via
 *                                 vita_native_menu_find() (probe script)
 *   5. zoom slider             - settings appearance row 2 is a slider with
 *                                 6 stops and action set_zoom
 *   6. per-menu counts         - window/header/menu-back present in every
 *                                 target menu the controller dispatches to
 *   7. dynamic registry        - an unregistered `menu` block stays out of
 *                                 the targets but is reachable via find()
 *   8. forward compatibility  - a CWMenu 3 file parses (version 3 reported,
 *                                 unknown constructs counted, not fatal)
 *   9. project + when          - host-injected switches drop tagged rows,
 *                                clear() restores script defaults, unknown
 *                                switches fail safe (row dropped)
 *  10. linking (CWMenu 4)     - cwmenu_script_link() merges a second .cwm
 *                                into the loaded state: dynamic menus register,
 *                                compiled-in targets are replaced per-menu,
 *                                theme merges, the version folds to max
 *
 * Build: gcc + cwmenu_api.c + native_menu_templates.c (see
 * build_cwmenu_unit.sh).  Exits 0 on success, 1 on any failure.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "menu/cwmenu_api.h"
#include "menu/native_menu_templates.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  PASS  %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* The window element of a named menu (NULL when absent). */
static const VitaMenuTemplate *find_window(const char *menu)
{
    size_t count = 0;
    const VitaMenuTemplate *items = vita_native_menu_find(menu, &count);
    if (!items) return NULL;
    for (const VitaMenuTemplate *t = items; t->name; t++)
        if (!strcmp(t->name, "window")) return t;
    return NULL;
}

static const VitaMenuTemplate *find_named(const char *menu, const char *element)
{
    size_t count = 0;
    const VitaMenuTemplate *items = vita_native_menu_find(menu, &count);
    if (!items) return NULL;
    for (const VitaMenuTemplate *t = items; t->name; t++)
        if (!strcmp(t->name, element)) return t;
    return NULL;
}

int main(void)
{
    int rc;
    int r, g, b;

    printf("== CWMenu 2 parser unit test ==\n");
    rc = vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm");
    printf("load rc=%d (negative = parse error count)\n", rc);
    check(rc == 0, "default.cwm parses with zero errors");

    /* 1. version -------------------------------------------------------- */
    printf("[version]\n");
    /* CWMenu 3 (2026-09-23 (39)): the shipped script carries the project
     * block + `when <expr>` conditionals; the controller reports the file's
     * version, and 2 was the previous generation. */
    check(cwmenu_script_version() == 3, "script reports version 3");
    check(cwm_script_version_supported() >= 2,
          "controller supports at least version 2");

    /* 2. theme block ---------------------------------------------------- */
    printf("[theme]\n");
    /* 11 color slots expected: 6 accents + 3 text styles + neutral + overlay.
       (labelN names live in their own array, not the slot count.) */
    check(cwm_theme_count() == 11, "theme block registered its 11 slots");
    check(cwm_theme_get_rgb("accent2", &r, &g, &b) == 0 &&
          r == 205 && g == 112 && b == 48,
          "accent2 parses as #CD7030");
    check(cwm_theme_get_rgb("text-normal", &r, &g, &b) == 0 &&
          r == 235 && g == 240 && b == 250,
          "text-normal parses as #EBF0FA");
    check(cwm_theme_get_label(2) && !strcmp(cwm_theme_get_label(2), "Orange"),
          "label2 names accent2 \"Orange\"");
    check(cwm_theme_get_rgb("accent0", &r, &g, &b) == 0 &&
          r == 70 && g == 105 && b == 150,
          "accent0 parses as #466996");
    check(cwm_theme_get_rgb("nosuchslot", &r, &g, &b) != 0,
          "unknown theme slot fails cleanly");

    /* 3. window keys ---------------------------------------------------- */
    /* The Menu Tests demo menus are gone, so window keys are pinned with a
     * small embedded script loaded into the dynamic registry. */
    printf("[window keys]\n");
    {
        /* Legacy windows (no keys) must stay byte-identical: main menu.
         * Checked BEFORE the probe script replaces the loaded templates. */
        {
            const VitaMenuTemplate *mwin = find_window("main");
            check(mwin && !mwin->win_anchor_set && !mwin->win_pair &&
                  mwin->win_scale == 0.0f && mwin->win_corner == 0,
                  "legacy window parses with all keys unset");
        }
        FILE *f = fopen("/tmp/cwm_winkeys.cwm", "w");
        const char *probe =
            "CWMenu 2\n"
            "menu probe_pair\n"
            "element window window 0 0 200 100 normal \"\"\n"
            "end\n"
            "menu probe_win\n"
            "element window window 10 20 300 150 normal \"\" anchor 560 120 size 1.15 corner rounded crop 8 8 60 8 pair probe_pair\n"
            "element header header 0 0 300 30 normal \"P\"\n"
            "end\n";
        const VitaMenuTemplate *pwin;
        const VitaMenuTemplate *awin;
        fputs(probe, f);
        fclose(f);
        rc = vita_native_menu_templates_load("/tmp/cwm_winkeys.cwm");
        check(rc == 0, "window-key probe script loads");
        pwin = find_window("probe_pair");
        awin = find_window("probe_win");
        check(pwin != NULL, "pair menu has a window element");
        check(awin != NULL, "window-key menu has a window element");
        if (awin) {
            check(awin->win_anchor_set && awin->win_anchor_x == 560.0f &&
                  awin->win_anchor_y == 120.0f,
                  "window anchor 560,120 parses");
            check(awin->win_scale > 1.14f && awin->win_scale < 1.16f,
                  "window size 1.15 parses");
            check(awin->win_corner == 1, "window corner rounded parses");
            check(awin->crop_set && awin->crop_l == 8.0f &&
                  awin->crop_r == 60.0f, "window crop 8,8,60,8 parses");
            check(awin->win_pair && !strcmp(awin->win_pair, "probe_pair"),
                  "window pairs with probe_pair");
        }
        /* 4. dual-window partner resolves through the registry API. */
        check(vita_native_menu_find("probe_pair", NULL) != NULL,
              "vita_native_menu_find resolves the partner menu");
        /* Back to the real script so the remaining checks see real data. */
        rc = vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm");
        check(rc == 0, "default script reloads after the probe");
    }

    /* 5. zoom slider ---------------------------------------------------- */
    printf("[zoom slider]\n");
    {
        const VitaMenuTemplate *row =
            find_named("settings_appearance", "content-row-2");
        check(row != NULL, "settings appearance row 2 present");
        check(row && row->kind == VITA_MENU_SLIDER,
              "zoom row is a real slider element");
        check(row && row->stops == 6, "zoom slider has 6 stops (0.75x..2.0x)");
        check(row && row->action && !strcmp(row->action, "set_zoom"),
              "zoom slider keeps the set_zoom action");
    }

    /* 6. per-menu counts ------------------------------------------------- */
    printf("[menu completeness]\n");
    {
        static const char *must_have[] = {
            "main", "settings_appearance", "settings_controls",
            "settings_preferences", "devtools_performance",
            "devtools_renderer", "devtools_input", "devtools_page",
            "devtools_overrides", "devtools_console", "devtools_platform",
            "devtools_close"
        };
        size_t i;
        int ok = 1;
        for (i = 0; i < sizeof(must_have)/sizeof(must_have[0]); i++) {
            const VitaMenuTemplate *win = find_window(must_have[i]);
            int cnt = vita_native_menu_count(must_have[i]);
            if (!win || cnt <= 0) {
                printf("         menu '%s' incomplete (window=%d count=%d)\n",
                       must_have[i], !!win, cnt);
                ok = 0;
            }
        }
        check(ok, "every key menu loads with a window and elements");
        /* DevTools keeps the removed "Show Debug Info" button out of every tab;
           START is page input now, not a debug-overlay shortcut. The footer
           must remain at x=180 with the full width so nothing overflows. */
        {
            static const char *dt[] = {
                "devtools_performance", "devtools_renderer", "devtools_input",
                "devtools_page", "devtools_overrides", "devtools_console",
                "devtools_platform", "devtools_close"
            };
            int allok = 1, plainok = 1;
            size_t di;
            for (di = 0; di < sizeof(dt)/sizeof(dt[0]); di++) {
                const VitaMenuTemplate *e = find_named(dt[di], "dbg");
                if (e) {
                    printf("         '%s' still has the dbg button\n", dt[di]);
                    allok = 0;
                }
                e = find_named(dt[di], "footer");
                if (!e || e->x < 179.0f) {
                    printf("         '%s' footer not restored to x=180\n", dt[di]);
                    allok = 0;
                }
                /* Tabs stay plain text; Back is a real button again
                   (user feedback round 3). */
                static const char *tabs[] = {
                    "performance", "renderer", "input", "page", "overrides",
                    "console", "platform", "close"
                };
                size_t ri;
                for (ri = 0; ri < sizeof(tabs)/sizeof(tabs[0]); ri++) {
                    const VitaMenuTemplate *r = find_named(dt[di], tabs[ri]);
                    if (!r || r->kind != VITA_MENU_TEXT || !r->action) {
                        printf("         '%s' tab '%s' is not plain text\n", dt[di], tabs[ri]);
                        plainok = 0;
                    }
                }
                {
                    const VitaMenuTemplate *r = find_named(dt[di], "back");
                    if (!r || r->kind != VITA_MENU_BUTTON || !r->action) {
                        printf("         '%s' back row is not a button\n", dt[di]);
                        plainok = 0;
                    }
                }
            }
            check(allok, "DevTools menus: dbg button removed, footer restored");
            check(plainok, "DevTools menus: tabs plain text, Back a button");
        }
        /* The More row and linked extras.cwm were removed. The stock main
           menu now has three action rows and a separate Back footer button. */
        {
            const char *rows3[3] = {"page-operations", "open-url", "settings"};
            const float ys3[3] = {204.0f, 246.0f, 288.0f};
            int allok = 1;
            size_t ri;
            for (ri = 0; ri < 3; ri++) {
                const VitaMenuTemplate *r = find_named("main", rows3[ri]);
                if (!r || r->kind != VITA_MENU_BUTTON || r->y != ys3[ri]) {
                    printf("         main row '%s' wrong kind/y\n", rows3[ri]);
                    allok = 0;
                }
            }
            if (find_named("main", "more")) allok = 0;
            check(allok, "main menu: three action rows; More removed");
        }
        /* The live DevTools rows: renderer/input/page tabs must carry the
           real-data anchor elements (user feedback round 3). */
        {
            int allok = 1;
            if (!find_named("devtools_renderer", "dom-nodes") ||
                !find_named("devtools_renderer", "last-error")) allok = 0;
            if (!find_named("devtools_input", "last-btn") ||
                !find_named("devtools_input", "dpad-focus")) allok = 0;
            if (!find_named("devtools_page", "console-stats") ||
                !find_named("devtools_page", "js-state")) allok = 0;
            check(allok, "DevTools renderer/input/page carry live rows");
        }
    }

    /* 7. + 8. forward compatibility probe -------------------------------- */
    printf("[forward compatibility]\n");
    {
        FILE *f = fopen("/tmp/cwm_future.cwm", "w");
        const char *future =
            "CWMenu 3\n"
            "menu main\n"
            "element window window 0 0 740 436 normal \"\"\n"
            "element header header 0 0 740 44 normal \"Future\"\n"
            "end\n"
            "menu app_extra\n"
            "element window window 0 0 200 100 normal \"\"\n"
            "end\n";
        size_t count = 0;
        const VitaMenuTemplate *items;
        fputs(future, f);
        fclose(f);
        rc = vita_native_menu_templates_load("/tmp/cwm_future.cwm");
        check(rc == 0, "CWMenu 3 file loads without errors");
        check(cwmenu_script_version() == 3,
              "version 3 reported from the newer file");
        items = vita_native_menu_find("app_extra", &count);
        check(items != NULL && count == 1,
              "unregistered menu lands in the dynamic registry");
        check(find_named("app_extra", "window") != NULL &&
              find_named("app_extra", "window")->w == 200.0f,
              "dynamic menu elements parse normally");
    }

    /* 9. project block + `when` conditionals (CWMenu 3, 2026-09-23 (39)) -
     *    host-injected values drop the tagged rows; clear() restores the
     *    script defaults; an unknown switch fails safe (row dropped). */
    printf("[project/when]\n");
    {
        size_t count = 0;
        const VitaMenuTemplate *items;
        const char *v = NULL;

        cwm_project_set("cursor", "off");
        rc = vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm");
        check(rc == 0, "reload with cursor=off parses cleanly");
        items = vita_native_menu_find("settings_controls", &count);
        {
            int i, pointer = 0, scroll = 0, zoom = 0;
            for (i = 0; items && i < (int)count; i++) {
                if (items[i].action && *items[i].action) {
                    if (!strcmp(items[i].action, "cycle_pointer")) pointer = 1;
                    if (!strcmp(items[i].action, "cycle_scroll_input")) scroll = 1;
                    if (!strcmp(items[i].action, "cycle_zoom_input")) zoom = 1;
                }
            }
            check(!pointer, "cursor=off drops the touchpad row");
            check(scroll && zoom, "scroll/zoom rows survive cursor=off");
        }
        check(cwm_project_get("zoom", &v) == 0 && v && !strcmp(v, "on"),
              "script default fills unset vars (zoom=on)");
        check(cwm_project_is_on("cursor") == 0, "host value wins (cursor off)");
        check(cwm_project_is_on("devtools") == 1, "devtools on by default");

        cwm_project_clear();
        rc = vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm");
        items = vita_native_menu_find("settings_controls", &count);
        {
            int i, pointer = 0;
            for (i = 0; items && i < (int)count; i++)
                if (items[i].action && !strcmp(items[i].action, "cycle_pointer"))
                    pointer = 1;
            check(rc == 0 && pointer, "project_clear() restores the row");
        }

        /* unknown switch: `when ghost` drops the row (a typo fails safe) */
        {
            FILE *f = fopen("/tmp/cwm_when.cwm", "w");
            const char *script =
                "CWMenu 3\n"
                "project\n"
                "    cursor on\n"
                "end\n"
                "menu when_probe\n"
                "element window window 0 0 200 100 normal \"\"\n"
                "element ghost text 0 0 100 30 normal \"X\" when ghost\n"
                "element keep text 0 40 100 30 normal \"K\" when cursor\n"
                "end\n";
            fputs(script, f);
            fclose(f);
            rc = vita_native_menu_templates_load("/tmp/cwm_when.cwm");
            items = vita_native_menu_find("when_probe", &count);
            check(rc == 0, "when-probe script parses");
            check(items && find_named("when_probe", "keep") != NULL,
                  "true condition keeps its row");
            check(!items || find_named("when_probe", "ghost") == NULL,
                  "unknown/off condition drops its row");
        }
        cwm_project_clear();
    }

    /* 10. CWMenu 4 linking ------------------------------------------------
     * A second .cwm links INTO the state the primary script built: dynamic
     * menus register, `menu main` blocks replace their target per-menu,
     * theme merges, and the reported version folds to the max. */
    printf("[linking CWMenu 4]\n");
    {
        size_t count = 0;
        const VitaMenuTemplate *items;
        int main_before;

        rc = vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm");
        check(rc == 0, "reload default.cwm for the linking probe");
        main_before = vita_native_menu_count("main");
        check(main_before > 0, "main menu has elements before linking");

        /* Linking remains supported by the parser, but the bundled More
           submenu is intentionally no longer shipped. */
        check(vita_native_menu_count("main") == main_before,
              "default main menu remains intact before external-link probe");

        /* a linked script may REPLACE a compiled-in target per-menu */
        {
            FILE *f = fopen("/tmp/cwm_link.cwm", "w");
            const char *script =
                "CWMenu 4\n"
                "menu main\n"
                "element window window 0 0 740 436 normal \"\"\n"
                "element header header 0 0 740 44 normal \"Linked\"\n"
                "end\n";
            if (!f) { check(0, "link probe file opens"); }
            else {
                fputs(script, f);
                fclose(f);
                rc = vita_native_menu_templates_link("/tmp/cwm_link.cwm");
                check(rc == 0, "target-replacing script links");
                check(vita_native_menu_count("main") == 2,
                      "linked `menu main` replaces the target's content");
                items = vita_native_menu_find("main", &count);
                check(items && find_named("main", "header") &&
                      !strcmp(find_named("main", "header")->label, "Linked"),
                      "replaced main carries the linked content");
            }
        }

        /* a missing file fails soft: state from the last good link stands */
        check(vita_native_menu_templates_link("/tmp/cwm_nope.cwm") == -1,
              "missing script file returns -1 (no crash)");
        check(vita_native_menu_count("main") > 0,
              "failed link leaves existing state intact");
    }

    printf("== %s (%d failure%s) ==\n",
           failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
