/* cursor_test.c - unit test for the shared pointer cursor (2026-09-23):
 * art contract fallback, clamping, tap position, and that the code-drawn
 * arrow only paints while visible.  Uses the recording desktop stubs the
 * same way the golden renderer tests do. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cwv_cursor.h"
#include "core/cwv_renderer.h"
#include "stubs/vita2d.h"

static int fails;
static void check(int v, const char *what)
{
    printf("%s %s\n", v ? "PASS" : "FAIL", what);
    if (!v) fails++;
}

int main(void)
{
    CwvCursor c;
    char *buf = NULL;
    size_t cap = 0;
    FILE *log = open_memstream(&buf, &cap);
    int x = -1, y = -1;
    extern void stub_set_log(FILE *f);
    stub_set_log(log);   /* stub draws must land in the memstream */

    /* 1. no art on disk anywhere -> code-drawn arrow fallback (-1). */
    check(cwv_cursor_init(&c, "app0:/assets/cursor-vita.png") == -1,
          "missing art falls back to the code arrow");
    check(c.art == NULL, "fallback cursor carries no texture");

    /* 2. hidden cursor: no position, no draw output. */
    cwv_cursor_place(&c, 100.0f, 200.0f, 960, 544);
    check(cwv_cursor_position(&c, &x, &y) == 0,
          "position unavailable while hidden");
    cwv_cursor_draw(&c);
    fflush(log);
    check(ftell(log) == 0, "hidden cursor draws nothing");

    /* 3. show: rounded tap position. */
    cwv_cursor_show(&c, 1);
    check(cwv_cursor_position(&c, &x, &y) == 1 && x == 100 && y == 200,
          "position returns rounded tip coordinates");

    /* 4. clamping to the screen. */
    cwv_cursor_place(&c, 1200.0f, -5.0f, 960, 544);
    check(c.x == 959.0f && c.y == 0.0f, "place clamps to the screen");
    cwv_cursor_place(&c, 431.6f, 287.4f, 960, 544);
    cwv_cursor_position(&c, &x, &y);
    check(x == 432 && y == 287, "position rounds half-up");

    /* 5. the code arrow paints only while visible. */
    cwv_cursor_place(&c, 50.0f, 60.0f, 960, 544);
    fseek(log, 0, SEEK_SET);
    cwv_cursor_draw(&c);
    fflush(log);
    check(buf && strstr(buf, "RECT") == buf,
          "visible fallback cursor paints rectangles");
    {
        int rects = 0;
        const char *p = buf;
        while ((p = strstr(p, "RECT")) != NULL) { rects++; p += 4; }
        check(rects >= 20, "arrow outline + fill both paint");
    }
    cwv_cursor_show(&c, 0);
    fseek(log, 0, SEEK_SET);
    cwv_cursor_draw(&c);
    fflush(log);
    check(ftell(log) == 0, "hidden again draws nothing");

    /* 6. destroy is safe with the fallback (no texture to free). */
    cwv_cursor_destroy(&c);
    check(c.art == NULL && c.visible == 0, "destroy clears state");

    /* 7. CSS cursor styles (2026-09-26 (45)): AUTO (NULL info) keeps
     * the default art; NONE hides the draw but the pointer still
     * taps; a keyword with a missing pack falls back to the default;
     * a URL style draws the engine-owned art at its CSS hotspot. */
    {
        CwvCursorInfo info;
        check(cwv_cursor_init(&c, "app0:/assets/cursor-vita.png") == -1,
              "re-init without art still gives the arrow");
        check(c.hotspot == CWV_CURSOR_CENTER,
              "default hotspot is CENTER (round cursors exact)");
        cwv_cursor_set_hotspot(&c, CWV_CURSOR_TIP);
        check(c.hotspot == CWV_CURSOR_TIP, "hotspot switches to TIP");
        cwv_cursor_set_hotspot(&c, CWV_CURSOR_CENTER);
        check(c.hotspot == CWV_CURSOR_CENTER, "hotspot back to CENTER");

        cwv_cursor_set_style(&c, NULL);   /* pages tab away: default */
        check(c.style == CWV_CSTYLE_AUTO && !c.style_hidden,
              "NULL info restores the default");

        info.style = CWV_CSTYLE_NONE;
        info.img = NULL; info.hot_x = 0; info.hot_y = 0;
        cwv_cursor_set_style(&c, &info);
        check(c.style == CWV_CSTYLE_NONE && c.style_hidden,
              "cursor:none hides the cursor");
        cwv_cursor_show(&c, 1);
        check(cwv_cursor_position(&c, &x, &y) == 1,
              "hidden-by-CSS pointer still taps");
        cwv_cursor_draw(&c);
        fflush(log);
        check(ftell(log) == 0, "cursor:none draws nothing");
        cwv_cursor_show(&c, 0);

        /* keyword with a missing pack -> default, remembered (no retry storm) */
        cwv_cursor_set_style_dir(&c, "/nonexistent/cursors");
        info.style = CWV_CSTYLE_POINTER;
        cwv_cursor_set_style(&c, &info);
        check(c.style == CWV_CSTYLE_AUTO && c.style_art == NULL,
              "missing pack art falls back to the default");
        cwv_cursor_set_style(&c, &info);
        check((c.pack_tried & (1u << CWV_CSTYLE_POINTER)) != 0,
              "failed pack load is remembered");
        check(c.pack_art[CWV_CSTYLE_POINTER] == NULL,
              "no pack texture was cached");

        /* URL style: engine-owned art + CSS hotspot; draw must not free
         * it (a second application must still reference it). */
        {
            /* stub vita2d texture (see stub_impl.c): 64x64 by default */
            extern vita2d_texture *cwv_stub_cursor_test_texture(void);
            vita2d_texture *fake_img = cwv_stub_cursor_test_texture();
            fake_img->width = 17; fake_img->height = 29;
            info.style = CWV_CSTYLE_URL;
            info.img = fake_img;
            info.hot_x = 3; info.hot_y = 4;
            cwv_cursor_set_style(&c, &info);
            check(c.style == CWV_CSTYLE_URL && c.style_art == fake_img,
                  "url cursor applies engine art");
            check(c.style_hot_x == 3 && c.style_hot_y == 4,
                  "url cursor keeps its CSS hotspot");
            cwv_cursor_show(&c, 1);
            cwv_cursor_place(&c, 100.0f, 100.0f, 960, 544);
            fseek(log, 0, SEEK_SET);
            cwv_cursor_draw(&c);
            fflush(log);
            check(buf && strstr(buf, "TEXSCALE") != NULL,
                  "url cursor draws the custom art");
            {
                /* the draw anchored the art at (x-hx, y-hy) */
                char expect[128];
                snprintf(expect, sizeof(expect),
                         "TEXSCALE 97.00 96.00 1.00 1.00 17x29");
                check(buf && strstr(buf, expect) != NULL,
                      "custom art anchors at the CSS hotspot");
            }
            cwv_cursor_show(&c, 0);
            cwv_cursor_set_style(&c, &info);
            check(c.style_art == fake_img,
                  "engine art survives a re-apply (never freed)");
        }
        cwv_cursor_destroy(&c);
    }

    fclose(log);
    free(buf);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return !!fails;
}
