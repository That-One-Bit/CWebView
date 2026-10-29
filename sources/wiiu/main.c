/* main.c - Wii U app loop for CWebView (wut + SDL2).
 *
 * STATUS: engine-first port.  This loop drives the SHARED host
 * (sources/shared/sources/core/cwv_host.c) on the Wii U GamePad screen (854x480):
 * page load, rendering, touch/mouse taps, focus navigation and button
 * activation all work through the platform seam.  The Vita-style native
 * native menus are driven by the shared CWMenu shell on the 960x544
 * virtual canvas. Vita's legacy implementation remains intact.
 *
 * ESM: the built-in transformer is used directly (cwv_host_enable_es_builtin)
 * - on Wii U there are no suprx modules; RPX/RPL come later per
 * sources/wiiu/README-WIIU.md.
 *
 * IME: wut's nn::swkbd in-app keyboard (cwv_ime_swkbd.cpp) - the seam
 * contract is unchanged, but unlike the Vita's system dialog the app
 * must DRAW the keyboard itself: cwv_ime_draw() runs every frame after
 * the page (draw-last placement) and is a no-op while no textfield is
 * active.
 *
 * Input mapping (wiiu-sdl2 GamePad):
 *   A 0: activate; B 1: back only while menu is open;
 *   L 6 / R 7: menu tab navigation; PLUS 10 / MINUS 11: START/SELECT.
 *   L+R+SELECT opens/closes CWMenu (configurable in Preferences).
 *   Exit App in DevTools is hidden by default and can be enabled there.
 *   Touch and left analog scroll use the virtual-canvas transform. */
#include "core/cwv_host.h"      /* CwvCursorInfo via cwv_renderer.h below */
#include "core/cwv_platform.h"
#include "core/cwv_renderer.h"   /* CwvCursorInfo, CWV_CSTYLE_* (45) */
#include "menu/cwv_menu_ui.h"
#include "core/cwv_cursor.h"
#include "core/cwv_project.h"
#include "cwv_ime_swkbd.h"
#include "menu_hotkey.h"

/* Wiimote IR cursor: SDL2 Wii U joystick layer never surfaces IR, so the
 * pointing data is read straight from KPAD (wut padscore), which the
 * wiiu-sdl2 joystick backend already keeps initialized. */
#include <padscore/kpad.h>   /* cwv_ime_draw: system keyboard on top */
#include <whb/proc.h>        /* ProcUI foreground/background/exit lifecycle */
#include <coreinit/debug.h>  /* OSReport: startup trace, lands in Cemu's log */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* Provided by sources/wiiu/cwv_platform.c: hands the SDL renderer to
 * the texture/font/draw forwards so the engine's draws land on the
 * GamePad screen. */
void cwv_wiiu_set_renderer(SDL_Renderer *renderer);
void cwv_wiiu_text_cache_clear(void);

#ifndef CWV_WIIU_WEBPAGE
#define CWV_WIIU_WEBPAGE "web/index.html"
#endif

/* wiiu-sdl2 GamePad button indices (verify on hardware). */
#define WIIU_BTN_A      0
#define WIIU_BTN_B      1
/* wiiu-sdl2 2.32.10 joystick order (devkitPro/wut-packages SDL2 patch):
 * A,B,X,Y,stick-L,stick-R,L,R,ZL,ZR,PLUS,MINUS.  SELECT is MINUS. */
#define WIIU_BTN_L      6
#define WIIU_BTN_R      7
#define WIIU_BTN_START  10
#define WIIU_BTN_SELECT 11
#define WIIU_BTN_LEFT   12
#define WIIU_BTN_UP     13
#define WIIU_BTN_RIGHT  14
#define WIIU_BTN_DOWN   15

#define WIIU_SCREEN_W 854
#define WIIU_SCREEN_H 480
/* The host runs on a VIRTUAL 960x544 canvas - the Vita reference layout -
 * and the platform seam (cwv_platform.c WIIU_FIT_*) fits it onto the
 * 854x480 GamePad: identical line wrapping and proportions to the Vita,
 * no letterboxing, and every engine calculation stays in one coordinate
 * space.  Pointer input converts device -> virtual (cwv_wiiu_fit_pointer)
 * before the engine sees it. */
#define WIIU_VIRTUAL_W 960
#define WIIU_VIRTUAL_H 544
/* Runtime log lives next to the app on the SD card (the WUHB content
 * itself is read-only) - the same debugging story as the Vita's
 * ux0:data/VWWV.log: engine failures land here even on script-free
 * pages.  Remove the file to clear it. */
/* The runtime log lives in the FOLDER THE PACKAGE SITS IN (set from
 * the resolved app root at boot; empty until then, which just skips the
 * file until main() fills it - the stderr path always works). */
static char s_log_path[300] = "";

/* 2026-09-26 (46): mkdir -p for fs: paths.  A fresh Cemu mlc01 has no
 * sdcard/wiiu/ at all, so the no-root fallback log MUST create its own
 * directory chain or Cemu rejects the file ("FSC: File create
 * failed").  mkdir() rides the WUT fs devoptab (FSCreateDir).  Every
 * error except "already exists" is non-fatal - the log write itself
 * reports failure to the boot screen. */
static void wiiu_ensure_dir_chain(const char *path)
{
    char part[300];
    size_t i, n = strlen(path);
    if (n == 0 || n >= sizeof(part)) return;
    memcpy(part, path, n + 1);
    for (i = 1; i < n; i++) {
        if (part[i] != '/') continue;
        part[i] = 0;
        mkdir(part, 0777);   /* EEXIST (and real failures) are fine */
        part[i] = '/';
    }
    mkdir(part, 0777);
}

/* Startup trace: OSReport goes to the Cafe OS console, which Cemu
 * mirrors into its log window/log.txt (on hardware it is harmless).
 * This exists so a crash anywhere in boot shows exactly how far we
 * got - the minidump analysis showed Cemu dying before any app-level
 * output, so these markers bracket every early-return path. */
#define WIIU_TRACE(msg) OSReport("CWV: %s\n", (msg))

static int s_log_file_ok = 1;   /* give up on the log file after the
                                 * first failed write (e.g. no SD in the
                                 * test environment) instead of paying a
                                 * failed FS round-trip every line */
static void wiiu_runtime_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", line);
    /* Debug/dev builds keep the log file (user correction 2026-10-01);
     * the stderr trace above stays for launcher consoles either way. */
    if (!s_log_file_ok) return;
    if (!s_log_path[0]) return;
    if (cwv_file_append(s_log_path, line, strlen(line)) != 0 ||
        cwv_file_append(s_log_path, "\n", 1) != 0)
        s_log_file_ok = 0;
}

/* Boot diagnostics: when the initial page cannot load, draw the cause
 * on screen (and retry every couple of seconds) instead of sitting on
 * a blank page - the SAME behaviour on hardware and in Cemu, so a
 * missing-content setup is visible instead of mysterious. */
static int s_boot_failed = 0;
static char s_boot_error[256] = "";

/* implemented in sources/wiiu/cwv_platform.c (Wii U only, not part
 * of the shared seam) */
extern const char *cwv_wiiu_app_root(void);
extern void cwv_wiiu_app_root_init(const char *argv0);
extern const char *cwv_wiiu_app_root_argv0(void);
extern const char *cwv_wiiu_app_root_source(void);
extern void cwv_wiiu_fit_pointer(int *x, int *y);   /* device -> virtual px */

/* ---- dynamic entry page (2026-09-21 (28)) ----------------------------
 * The entry page is NOT baked into the RPX anymore: the webapp's own
 * manifest - web/package.json, shipped INSIDE the web content - names
 * it via the same "webpage" field the build scripts read (see
 * webapp/package.json / cwebview.schema.json).  Reading it at RUNTIME
 * means repointing the entry page is a content edit (edit the manifest
 * on the SD card / re-pack the WUHB web/ folder only) - no rebuild of
 * the RPX itself.  Precedence: argv[1] > manifest > compile-time
 * CWV_WIIU_WEBPAGE default (kept as the always-works fallback).
 * Manifest values are webapp-ROOT relative ("index.html"), so they get
 * the "web/" prefix here; argv and the compile-time default are already
 * package-folder-relative and go through as-is, exactly as before. */
#define WIIU_MANIFEST_PAGE "web/"

static int wiiu_ci_find(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
        if (strncasecmp(p, needle, nl) == 0) return (int)(p - hay);
    return -1;
}

static int wiiu_manifest_webpage(char *out, size_t cap)
{
    char manifest[400];
    snprintf(manifest, sizeof(manifest), "%s/web/package.json",
             cwv_wiiu_app_root());
    long size = cwv_file_size(manifest);
    if (size <= 0 || size > 65536) return -1;
    char *buf = malloc((size_t)size + 1);
    if (!buf) return -1;
    int fd = cwv_file_open_read(manifest);
    if (fd < 0) { free(buf); return -1; }
    int got = cwv_file_read(fd, buf, (int)size);
    cwv_file_close(fd);
    if (got != (int)size) { free(buf); return -1; }
    buf[size] = 0;

    int at = wiiu_ci_find(buf, "\"webpage\"");
    int rc = -1;
    if (at >= 0) {
        const char *q = buf + at + strlen("\"webpage\"");
        q = strchr(q, ':');
        if (q) {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == '"') {
                q++;
                const char *end = strchr(q, '"');
                size_t len = end ? (size_t)(end - q) : 0;
                /* sane manifest values: a relative page name, no
                   traversal, no subverting the app root prefix */
                if (len > 0 && len < 200 && !strstr(q, "..") &&
                    snprintf(out, cap, "web/%.*s", (int)len, q) > 0)
                    rc = 0;
            }
        }
    }
    free(buf);
    return rc;
}

static void draw_boot_screen(void *font)
{
    const unsigned white = 0xFFFFFFFFu, accent = 0xFF00A5FFu;
    int y = 40;
    cwv_clear_screen();
    if (!font) return;               /* no bundled font -> nothing to draw */
    cwv_font_draw_text(font, 20, y, accent, 24, "CWebView: page could not be loaded"); y += 34;
    if (s_boot_error[0]) { cwv_font_draw_text(font, 20, y, white, 24, s_boot_error); y += 34; }
    const char *root = cwv_wiiu_app_root();
    char line[512];
    if (root[0]) {
        snprintf(line, sizeof(line), "Content root (%s):",
                 cwv_wiiu_app_root_source());
        cwv_font_draw_text(font, 20, y, white, 24, line); y += 30;
        snprintf(line, sizeof(line), "  %s", root);
        cwv_font_draw_text(font, 20, y, white, 24, line); y += 30;
    } else {
        cwv_font_draw_text(font, 20, y, accent, 24,
            "Loaded a .wuhb? It carries NO web/ by design (the"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "WUHB holds only the RPX; content stays beside it)."); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "Cemu: copy build/wiiu/content/* to"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "  <Cemu>/mlc01/sdcard/wiiu/apps/CWebView/"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "then File -> Load executable -> CWebView.rpx"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "(the staged RPX, NOT the .wuhb)."); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "Real hardware / Aroma: HBL or the homebrew-on-menu"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "plugin passes the app path and web/ is found via"); y += 30;
        cwv_font_draw_text(font, 20, y, white, 24,
            "fs:/vol/external01/wiiu/apps/ automatically."); y += 30;
        /* what the launcher passed - the missing piece when a scan
         * finds nothing is WHICH launcher this is and what it passed */
        const char *argv0 = cwv_wiiu_app_root_argv0();
        snprintf(line, sizeof(line), "Launcher argv[0]: %s",
                 argv0[0] ? argv0 : "(nothing)");
        cwv_font_draw_text(font, 20, y, white, 24, line); y += 30;
        /* and where the diagnostic log landed (always written now) */
        snprintf(line, sizeof(line), "Log: %s",
                 s_log_path[0] ? s_log_path : "(nowhere)");
        cwv_font_draw_text(font, 20, y, white, 24, line); y += 30;
    }
    cwv_font_draw_text(font, 20, y, accent, 24, "Retrying every 2 seconds... L+R+SELECT opens menu."); y += 34;
}


/* CWMenu is entirely shared; only drawing, persistent storage, and the
 * platform IME live here. All geometry stays in Vita-reference virtual
 * pixels; the platform seam fits both page and menu to 854x480. */
static CwvMenuUi s_menu;
static CwvCursor s_cursor;              /* Wiimote IR cursor, drawn last */
static CwvMenuPaint s_menu_paint;
static CwvMenuPlatformOps s_menu_ops;
static SDL_Renderer *g_menu_renderer;
static void *s_menu_font;
static void *s_menu_favicon;
static char s_menu_favicon_source[1024];
static CwvHost *s_menu_host;
static int s_menu_quit;
static int s_cursor_a;                  /* cursor A tap in flight */
static int s_menu_url_ime;
static cwv_utf16 s_menu_url_text[1024];
static cwv_utf16 s_menu_url_prefill[1024];
/* Settings live in the folder the package sits in (set at boot). */
static char s_prefs_path[300] = "settings.cfg";

/* CWMenu 4 script linking: resolve a bare file name against the app's
 * cwmenu directory (where the build stages every .cwm) and link it.
 * Names only - a path separator in a script-controlled string never
 * reaches the filesystem traversal. */
static int wiiu_files_list(const char *relative, CwvMenuFileEntry *out,
                           int capacity, void *ud)
{
    char path[1024];
    DIR *dir;
    struct dirent *ent;
    int n = 0;
    (void)ud;
    if (!out || capacity <= 0) return 0;
    if (relative && (strstr(relative, "..") || strchr(relative, '\\'))) return -1;
    if (snprintf(path, sizeof(path), "%s/web/%s%s", cwv_platform_path_root(),
                 relative && *relative ? relative : "",
                 relative && *relative ? "/" : "") >= (int)sizeof(path)) return -1;
    dir = opendir(path);
    if (!dir) return 0;
    while ((ent = readdir(dir)) != NULL && n < capacity) {
        char full[1100]; struct stat st;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(full, sizeof(full), "%s%s", path, ent->d_name) >= (int)sizeof(full)) continue;
        if (stat(full, &st) != 0) continue;
        snprintf(out[n].name, sizeof(out[n].name), "%s", ent->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode);
        ++n;
    }
    closedir(dir);
    return n;
}
static int menu_load_script(const char *file, void *ud)
{
    char path[512];
    const char *root=cwv_wiiu_app_root();
    (void)ud;
    if(!root || !*root || !file || !*file) return -1;
    if(strchr(file,'/') || strchr(file,'\\')) return -1;
    snprintf(path,sizeof(path),"%s/cwmenu/%s",root,file);
    return vita_native_menu_templates_link(path);
}

static void menu_fill(float x,float y,float w,float h,unsigned c,void *ud)
{ (void)ud; cwv_draw_rectangle(x,y,w,h,c); }
static void menu_circle(float x,float y,float r,unsigned c,void *ud)
{ (void)ud; cwv_draw_fill_circle(x,y,r,c); }
static void menu_text(float x,float y,unsigned c,float z,const char *txt,void *ud)
{ (void)ud; if(s_menu_font) cwv_pgf_draw_text(s_menu_font,x,y,c,z,txt); }
static float menu_text_width(float z,const char *txt,void *ud)
{ (void)ud; return s_menu_font
  ? (float)cwv_pgf_text_width(s_menu_font,z,txt) * (960.0f/854.0f)
  : (float)strlen(txt)*8.0f*z; }

static void menu_favicon(float x,float y,float w,float h,const char *url,void *ud)
{
    char path[1024];
    char prefix[300];
    snprintf(prefix,sizeof(prefix),"%s/",cwv_platform_url_root());
    const char *ext,*end;
    size_t len;
    (void)ud;
    if (!url || !*url) {
        if(s_menu_favicon)cwv_tex_free(s_menu_favicon);
        s_menu_favicon=NULL;
        s_menu_favicon_source[0]=0;
        return;
    }
    if (strncmp(url,prefix,strlen(prefix))!=0) {
        if(s_menu_favicon)cwv_tex_free(s_menu_favicon);
        s_menu_favicon=NULL;
        s_menu_favicon_source[0]=0;
        return;
    }
    if (strcmp(url,s_menu_favicon_source)!=0) {
        if (s_menu_favicon) cwv_tex_free(s_menu_favicon);
        s_menu_favicon=NULL;
        snprintf(s_menu_favicon_source,sizeof(s_menu_favicon_source),"%s",url);
        end=strpbrk(url+strlen(prefix),"?#");
        len=end?(size_t)(end-(url+strlen(prefix))):strlen(url+strlen(prefix));
        {
            size_t pl=strlen(cwv_platform_path_root());
            if(len+pl+1>=sizeof(path))return;
            memcpy(path,cwv_platform_path_root(),pl);
            memcpy(path+pl,url+strlen(prefix)-1,len);   /* keep the '/' */
            path[len+pl]=0;
        }
        ext=strrchr(path,'.');
        if (ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg")))
            s_menu_favicon=cwv_tex_load_jpeg_file(path);
        else if (ext && !strcasecmp(ext,".png"))
            s_menu_favicon=cwv_tex_load_png_file(path);
    }
    if (s_menu_favicon && cwv_tex_width(s_menu_favicon)>0 && cwv_tex_height(s_menu_favicon)>0)
        cwv_draw_texture_scale(s_menu_favicon,x,y,
            w/(float)cwv_tex_width(s_menu_favicon),
            h/(float)cwv_tex_height(s_menu_favicon));
}
static void menu_line(float x1,float y1,float x2,float y2,unsigned color,void *ud)
{
    SDL_Color c={color&255,(color>>8)&255,(color>>16)&255,(color>>24)&255};
    (void)ud;
    SDL_SetRenderDrawColor(g_menu_renderer,c.r,c.g,c.b,c.a);
    SDL_RenderDrawLineF(g_menu_renderer,x1*(854.0f/960.0f),y1*(480.0f/544.0f),
                      x2*(854.0f/960.0f),y2*(480.0f/544.0f));
}
static int menu_save_prefs(void *ud)
{
    CwvMenuUi *ui=(CwvMenuUi *)ud;
    char out[256];
    int n=snprintf(out,sizeof(out),
        "scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\ndark=%d\naccent=%d\nmenu_hotkey=%d\nshow_exit_app=%d\ncss_cursors=%d\n",
        ui->settings.scroll_mode,ui->settings.scroll_speed,
        ui->settings.zoom_mode,(int)(ui->settings.zoom_level*100.0f+0.5f),
        ui->settings.dark_mode,ui->settings.accent,
        ui->settings.menu_hotkey,ui->settings.show_exit_app,
        ui->settings.css_cursors);
    {
        char base[300];
        snprintf(base, sizeof(base), "%s", cwv_wiiu_app_root());
        if (base[0]) (void)cwv_file_mkdir(base);
    }
    return n>0 && n<(int)sizeof(out)
        ? cwv_file_write_all(s_prefs_path,out,(size_t)n) : -1;
}
static int menu_load_prefs(void *ud)
{
    CwvMenuUi *ui=(CwvMenuUi *)ud;
    char buf[256];
    int fd=cwv_file_open_read(s_prefs_path),n;
    int sc=0,spd=1,zm=0,zl=100,dk=1,ac=0,hk=0,show_exit=0,cc=1;
    if(fd<0)return -1;
    n=cwv_file_read(fd,buf,sizeof(buf)-1);cwv_file_close(fd);
    if(n<=0)return -1;
    buf[n]=0;
    if(sscanf(buf,"scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\ndark=%d\naccent=%d",
              &sc,&spd,&zm,&zl,&dk,&ac)!=6)return -1;
    { /* Older settings.cfg files have only the original six fields. */
        const char *extra=strstr(buf,"menu_hotkey=");
        if(extra && sscanf(extra,"menu_hotkey=%d\nshow_exit_app=%d",&hk,&show_exit)!=2)
            return -1;
    }
    { /* css_cursors ships 2026-09-26 (45); older files default ON. */
        const char *extra=strstr(buf,"css_cursors=");
        if(extra) (void)sscanf(extra,"css_cursors=%d",&cc);
    }
    if(sc<0||sc>2||spd<0||spd>2||zm<0||zm>2||zl<75||zl>200||
       dk<0||dk>1||ac<0||ac>5||hk<0||hk>2||show_exit<0||show_exit>1||
       cc<0||cc>1)return -1;
    ui->settings.scroll_mode=sc;ui->settings.scroll_speed=spd;
    ui->settings.zoom_mode=zm;ui->settings.zoom_level=(float)zl/100.0f;
    ui->settings.dark_mode=dk;ui->settings.accent=ac;
    ui->settings.menu_hotkey=hk;ui->settings.show_exit_app=show_exit;
    ui->settings.css_cursors=cc;
    ui->settings.dirty=0;
    if(s_menu_host) (void)cwv_host_set_zoom(s_menu_host,ui->settings.zoom_level);
    return 0;
}
static void menu_open_url(void *ud)
{
    (void)ud;
    static const cwv_utf16 title[]={ 'O','p','e','n',' ','U','R','L',0 };
    s_menu_url_text[0]=0;
    /* The prompt auto-fills with the package's own web folder entry
     * page (user direction, 2026-09-23 (33)) - the URL the app loaded
     * at boot, e.g. file:///fs:/vol/.../wiiu/apps/CWebView/web/index.html.
     * Entry names are manifest-controlled ASCII; non-ASCII folder names
     * still load fine, only the PROMPT prefill is ASCII-converted. */
    s_menu_url_ime=(cwv_ime_open(title,s_menu_url_prefill,0,0,s_menu_url_text,1024)==0);
}
static void menu_quit_app(void *ud){(void)ud;s_menu_quit=1;}
static int menu_clear_cache(void *ud)
{
    char cache_dir[512],file[768];
    DIR *dir;
    struct dirent *ent;
    int failed=0;
    (void)ud;
    /* Restrict deletion to files inside the console's dedicated cache
     * directory. Never traverse subdirectories or touch saved prefs. */
    if(cwv_console_cache_dir(cache_dir,sizeof(cache_dir))<0)return CWV_MENU_STATUS_NONE;
    dir=opendir(cache_dir);
    if(!dir)return CWV_MENU_STATUS_NONE;
    while((ent=readdir(dir))!=NULL){
        struct stat st;
        if(ent->d_name[0]=='.')continue;
        if(snprintf(file,sizeof(file),"%s/%s",cache_dir,ent->d_name)>=(int)sizeof(file)){
            failed=1;continue;
        }
        if(stat(file,&st)!=0){failed=1;continue;}
        if(S_ISREG(st.st_mode) && remove(file)!=0)failed=1;
    }
    closedir(dir);
    return failed?CWV_MENU_STATUS_NONE:CWV_MENU_STATUS_CACHE_CLEARED;
}
static void menu_log(const char *msg,void *ud){(void)ud;wiiu_runtime_log("menu: %s",msg);}
static void menu_poll_url(void)
{
    int done;
    char url[2048];
    size_t i=0,o=0;
    if(!s_menu_url_ime)return;
    done=cwv_ime_poll(s_menu_url_text,1024);
    if(done==0)return;
    s_menu_url_ime=0;
    if(done!=1)return;
    /* Decode UTF-16 to UTF-8; the native keyboard can enter non-ASCII
     * URLs, so an ASCII truncation would silently navigate elsewhere. */
    while(s_menu_url_text[i]&&o<sizeof(url)-5){
        unsigned cp=s_menu_url_text[i++];
        if(cp>=0xd800&&cp<=0xdbff){
            unsigned lo=s_menu_url_text[i];
            if(lo<0xdc00||lo>0xdfff)continue;
            i++;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
        }else if(cp>=0xdc00&&cp<=0xdfff)continue;
        if(cp<0x80)url[o++]=(char)cp;
        else if(cp<0x800){url[o++]=(char)(0xc0|(cp>>6));url[o++]=(char)(0x80|(cp&63));}
        else if(cp<0x10000){url[o++]=(char)(0xe0|(cp>>12));url[o++]=(char)(0x80|((cp>>6)&63));url[o++]=(char)(0x80|(cp&63));}
        else {url[o++]=(char)(0xf0|(cp>>18));url[o++]=(char)(0x80|((cp>>12)&63));url[o++]=(char)(0x80|((cp>>6)&63));url[o++]=(char)(0x80|(cp&63));}
    }
    url[o]=0;
    if(!o)return;
    if(cwv_host_load(s_menu_host,url)==0){
        cwv_menu_ui_record_page(&s_menu);
        cwv_menu_ui_close(&s_menu);
    }else wiiu_runtime_log("menu: Open URL failed: %s",cwv_host_last_error(s_menu_host));
}
static void menu_fill_live(CwvMenuData *d,CwvHost *host,int frame,float ms)
{
    const char *s;
    memset(d,0,sizeof(*d));
    d->frame_count=(unsigned)frame;d->frame_ms=ms;
    d->fps=ms>0.0f?1000.0f/ms:0.0f;
    d->scroll_offset=cwv_host_scroll_offset(host);d->zoom=cwv_host_zoom(host);
    d->viewport_w=WIIU_VIRTUAL_W;d->viewport_h=WIIU_VIRTUAL_H;
    d->node_count=cwv_host_node_count(host);d->js_active=cwv_host_js_active(host);
    s=cwv_host_js_last_error(host);if(s)snprintf(d->js_last_error,sizeof(d->js_last_error),"%s",s);
    s=cwv_host_page_title(host);if(s)snprintf(d->page_title,sizeof(d->page_title),"%s",s);
    s=cwv_host_current_url(host);if(s)snprintf(d->page_url,sizeof(d->page_url),"%s",s);
    snprintf(d->version_display,sizeof(d->version_display),"Wii U");
    /* backend ids: 1 browser, 2 standalone, 3 touch, 4 cursor (0 none) */
    snprintf(d->control_mode_name,sizeof(d->control_mode_name),"%s",
             cwv_host_control_mode(host)==1?"Browser":
             cwv_host_control_mode(host)==2?"Standalone":
             cwv_host_control_mode(host)==3?"Touch":
             cwv_host_control_mode(host)==4?"Cursor":"None");
    cwv_host_focus_info(host,d->dpad_focus,sizeof(d->dpad_focus));
    snprintf(d->platform_row_esm,sizeof(d->platform_row_esm),
             "ESM module: built-in ES2015 transformer");
    snprintf(d->platform_row_status,sizeof(d->platform_row_status),
             "Wii U platform integration is active (GamePad 854x480).");
}

int main(int argc, char **argv)
{
    CwvHost *host;
    static char s_url_root[320];
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Event ev;
    char entry_url[1024];
    void *boot_font = NULL;
    int running = 1;
    int finger_down = 0, finger_dragging = 0;
    int finger_start_x = 0, finger_start_y = 0;
    int finger_last_x = 0, finger_last_y = 0;
    uint32_t last_ticks=0;
    unsigned menu_buttons=0;

    WIIU_TRACE("main entered");

    /* Aroma and Tiramisu launch RPX/WUHB apps like normal titles.  SDL's
     * event loop alone does not service ProcUI foreground/background and
     * exit requests; without this wrapper the launcher can keep its loading
     * audio running and Home/exit transitions can misbehave. */
    WHBProcInit();

    /* Prevent SDL from synthesizing duplicate mouse clicks for the finger
     * events handled below. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    /* THE APP ROOT IS THE FOLDER THE PACKAGE SITS IN (user direction,
     * 2026-09-23 (33)): the launcher passes the package's path in
     * argv[0], so .../exampledir/s.rpx loads .../exampledir/web/.
     * app0: stays Vita-only; the engine's URL space points at this
     * real folder so the URL bar stays honest. */
    cwv_wiiu_app_root_init(argc > 0 ? argv[0] : NULL);
    {
        const char *root = cwv_wiiu_app_root();
        if (root && *root) {
            snprintf(s_url_root, sizeof(s_url_root), "file:///%s", root);
            cwv_platform_set_app_root(s_url_root, root);
            snprintf(s_log_path, sizeof(s_log_path), "%s/CWV.log", root);
            snprintf(s_prefs_path, sizeof(s_prefs_path), "%s/settings.cfg", root);
            OSReport("CWV: app root (%s): %s\n",
                     cwv_wiiu_app_root_source(), root);
        } else {
            /* Bug report 2026-09-23 (35): "no file was written!" - with
             * no app root the log path stayed EMPTY, so diagnostics
             * went nowhere at all.  Always write SOMEWHERE.
             * Hardware report 2026-09-26 (46): the old fallback
             * (sd:/wiiu/CWV.log) assumed sd:/wiiu/ exists - in a
             * fresh Cemu mlc01 nothing is there, and Cemu logged
             * "FSC: File create failed" so the log silently vanished.
             * Now we stage the CANONICAL app folder ourselves and
             * create the directory chain before any write. */
            static const char fb[] = "fs:/vol/external01/wiiu/apps/CWebView";
            wiiu_ensure_dir_chain(fb);
            snprintf(s_log_path, sizeof(s_log_path),
                     "%s/CWV.log", fb);
            snprintf(s_prefs_path, sizeof(s_prefs_path),
                     "%s/settings.cfg", fb);
            OSReport("CWV: no app root (argv0=%s); log at %s\n",
                     cwv_wiiu_app_root_argv0()[0]
                         ? cwv_wiiu_app_root_argv0() : "(none)",
                     s_log_path);
        }
    }

    WIIU_TRACE("main entered, calling SDL_Init(VIDEO)");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        OSReport("CWV: SDL_Init failed: %s\n", SDL_GetError());
        WHBProcShutdown();
        return 1;
    }
    WIIU_TRACE("SDL_Init ok");
    /* Joystick input (GamePad buttons/sticks) - non-fatal if the
     * subsystem is unavailable; the touch/pointer path still works. */
    SDL_Joystick *stick = NULL;
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0) {
        if (SDL_NumJoysticks() > 0) stick = SDL_JoystickOpen(0);
    } else {
        fprintf(stderr, "joystick subsystem unavailable: %s\n", SDL_GetError());
    }
    WIIU_TRACE("joystick probed, creating window");
    window = SDL_CreateWindow("CWebView",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              WIIU_SCREEN_W, WIIU_SCREEN_H, 0);
    if (!window) { OSReport("CWV: SDL_CreateWindow failed\n"); SDL_Quit(); WHBProcShutdown(); return 1; }
    WIIU_TRACE("window ok, creating renderer");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { OSReport("CWV: SDL_CreateRenderer failed\n"); SDL_DestroyWindow(window); SDL_Quit(); WHBProcShutdown(); return 1; }
    WIIU_TRACE("renderer ok");
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    cwv_wiiu_set_renderer(renderer);
    g_menu_renderer=renderer;

    host = cwv_host_create(WIIU_VIRTUAL_W, WIIU_VIRTUAL_H);
    if (!host) { OSReport("CWV: host create failed\n"); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); WHBProcShutdown(); return 1; }
    WIIU_TRACE("host created");
    s_menu_host=host;
    s_menu_font=cwv_pgf_load_default();
    s_menu_paint=(CwvMenuPaint){menu_fill,menu_circle,menu_text,menu_text_width,NULL,menu_favicon,menu_line};
    s_menu_ops=(CwvMenuPlatformOps){menu_save_prefs,menu_load_prefs,
        menu_open_url,menu_quit_app,menu_clear_cache,menu_log,
        menu_load_script,wiiu_files_list,&s_menu};
    cwv_menu_ui_init(&s_menu,&s_menu_paint,&s_menu_ops,host,WIIU_VIRTUAL_W,WIIU_VIRTUAL_H);
    {
        char script[512]="(no app root)";
        const char *root=cwv_wiiu_app_root();
        int rc=-1;
        if(root && *root){
            snprintf(script,sizeof(script),"%s/cwmenu/default.cwm",root);
            rc=vita_native_menu_templates_load(script);
        }
        if(rc!=0) wiiu_runtime_log("CWMenu script unavailable (%s): %d",script,rc);
        else wiiu_runtime_log("CWMenu %d loaded from %s",cwmenu_script_version(),script);
    }
    (void)menu_load_prefs(&s_menu);
    /* Set the saved theme BEFORE parsing/running the first page script.
     * A first-frame light parse followed by a dark pass visibly flashes
     * default-colored text against the dark page on the GamePad. */
    (void)cwv_host_set_theme(host,cwv_menu_ui_effective_dark(&s_menu));
#if CWV_CURSOR_ENABLED
    /* Pointer cursor art lives NEXT TO THE RPX: <app root>/assets/
     * cursor-wiiu.png (the WUHB is code-only since (36); the build stages
     * the art into the app folder).  CWV_CURSOR_ENABLED=0 (project key
     * "wiiu":{"cursor":false}) skips the cursor entirely - no art is
     * staged and these calls compile out (2026-09-23 (39)). */
    {
        char art[400];
        snprintf(art, sizeof(art), "%s/assets/cursor-wiiu.png", cwv_wiiu_app_root());
        cwv_cursor_init(&s_cursor, art);
        /* CSS cursor pack (2026-09-26 (45)): <app root>/assets/cursors/
         * staged by the build.  Missing art falls back to the console
         * art above, then the code arrow. */
        snprintf(art, sizeof(art), "%s/assets/cursors", cwv_wiiu_app_root());
        cwv_cursor_set_style_dir(&s_cursor, art);
    }
#endif
    cwv_host_set_runtime_log(host, wiiu_runtime_log);

    /* ES2015+ support: the built-in transformer (no suprx on Wii U). */
    cwv_host_enable_es_builtin(host);
    cwv_host_enable_ts_builtin(host);

    char entry_page[512];
    const char *entry_src = "default";
    if (argc > 1 && argv[1][0]) {
        snprintf(entry_page, sizeof(entry_page), "%s", argv[1]);
        entry_src = "argv";
    } else if (wiiu_manifest_webpage(entry_page, sizeof(entry_page)) == 0) {
        entry_src = "manifest";
    } else {
        snprintf(entry_page, sizeof(entry_page), "%s", CWV_WIIU_WEBPAGE);
    }
    /* The URL names the package's own folder (app0: is Vita-only). */
    snprintf(entry_url, sizeof(entry_url), "%s/%s",
             cwv_platform_url_root(), entry_page);
    {
        size_t i;
        for (i = 0; i + 1 < sizeof(s_menu_url_prefill)/sizeof(s_menu_url_prefill[0])
                      && entry_url[i]; i++)
            s_menu_url_prefill[i] = (cwv_utf16)(unsigned char)entry_url[i];
        s_menu_url_prefill[i] = 0;
    }
    wiiu_runtime_log("boot: CWebView Wii U %dx%d (virtual canvas %dx%d), page %s (from %s)",
                     WIIU_SCREEN_W, WIIU_SCREEN_H,
                     WIIU_VIRTUAL_W, WIIU_VIRTUAL_H, entry_url, entry_src);
    /* font for the boot diagnostics screen: the package's
     * assets/font.ttf, and if that cannot be opened, the built-in 8x8
     * fallback (bug report (35): "no text on start" - a missing root
     * used to blank the entire boot screen). */
    {
        char font_path[400];
        snprintf(font_path, sizeof(font_path), "%s/assets/font.ttf",
                 cwv_wiiu_app_root());
        boot_font = cwv_font_load_file(font_path);
        if (!boot_font)
            boot_font = cwv_pgf_load_default();
    }
    WIIU_TRACE("calling initial host_load");
    if (cwv_host_load(host, entry_url) != 0) {
        s_boot_failed = 1;
        snprintf(s_boot_error, sizeof(s_boot_error), "%s",
                 cwv_host_last_error(host));
        wiiu_runtime_log("boot: initial load failed: %s", s_boot_error);
    }

    WIIU_TRACE(s_boot_failed ? "initial load failed, boot screen active" : "page loaded, entering loop");
    if(!s_boot_failed)cwv_menu_ui_record_page(&s_menu);
    int frame = 0;
    while (running && WHBProcIsRunning()) {
        frame++;
        /* Content may appear late (e.g. the emulated SD gets its files
         * copied in during a Cemu test session) - retry every 2s. */
        if (s_boot_failed && (frame % 120) == 0) {
            if (cwv_host_load(host, entry_url) == 0) {
                s_boot_failed = 0;
                wiiu_runtime_log("boot: retry succeeded, page loaded");
            }
        }
        /* ---- input -------------------------------------------------- */
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                int px = (int)(ev.tfinger.x * (float)WIIU_SCREEN_W);
                int py = (int)(ev.tfinger.y * (float)WIIU_SCREEN_H);
                cwv_wiiu_fit_pointer(&px, &py);
                if (s_menu_url_ime) break;
                if (cwv_menu_ui_mode(&s_menu) != CWV_MENU_CLOSED) {
                    cwv_menu_ui_touch(&s_menu, px, py,
                        ev.type == SDL_FINGERDOWN ? 1 :
                        ev.type == SDL_FINGERUP ? 0 : 2);
                    break;
                }
                if (ev.type == SDL_FINGERDOWN) {
                    finger_down = 1;
                    finger_dragging = 0;
                    finger_start_x = finger_last_x = px;
                    finger_start_y = finger_last_y = py;
                } else if (ev.type == SDL_FINGERMOTION && finger_down) {
                    int dx = px - finger_start_x;
                    int dy = py - finger_start_y;
                    if (!finger_dragging && (dx * dx + dy * dy) >= 64)
                        finger_dragging = 1;
                    if (finger_dragging) {
                        /* Finger movement is opposite document movement.
                         * Dispatch at the finger position so the nearest
                         * nested overflow region gets first refusal. */
                        (void)cwv_host_scroll_at(host,
                            (float)(finger_last_y - py), px, py);
                        (void)cwv_host_scroll_x_at(host,
                            (float)(finger_last_x - px), px, py);
                    }
                    finger_last_x = px;
                    finger_last_y = py;
                } else if (ev.type == SDL_FINGERUP && finger_down) {
                    if (!finger_dragging) {
                        (void)cwv_host_pointer_event(host, finger_start_x,
                                                     finger_start_y, 1);
                        (void)cwv_host_pointer_event(host, px, py, 0);
                    }
                    finger_down = 0;
                    finger_dragging = 0;
                }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION: {
                int px = ev.type==SDL_MOUSEMOTION?ev.motion.x:ev.button.x;
                int py = ev.type==SDL_MOUSEMOTION?ev.motion.y:ev.button.y;
                int held=(ev.type==SDL_MOUSEMOTION && ev.motion.state!=0);
                if(ev.type==SDL_MOUSEMOTION && !held)break;
                cwv_wiiu_fit_pointer(&px,&py);
                if(s_menu_url_ime)break;
                if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                    cwv_menu_ui_touch(&s_menu,px,py,
                        ev.type==SDL_MOUSEBUTTONDOWN?1:
                        ev.type==SDL_MOUSEBUTTONUP?0:2);
                else cwv_host_pointer_event(host,px,py,
                        ev.type!=SDL_MOUSEBUTTONUP);
                break;
            }
            case SDL_MOUSEWHEEL:
                /* USB mouse wheel (SDL maps it to this event): vertical
                 * steps scroll the page; horizontal (tilt) goes sideways.
                 * Same 60px step the keyboard arrows use. */
                if(cwv_menu_ui_mode(&s_menu)==CWV_MENU_CLOSED){
                    cwv_host_scroll_by(host,(float)ev.wheel.y*60.0f);
                    cwv_host_scroll_x_by(host,(float)ev.wheel.x*60.0f);
                }
                break;
            case SDL_KEYUP:
                if (ev.key.keysym.sym == SDLK_RETURN)
                    cwv_host_focus_release(host);
                break;
            case SDL_KEYDOWN: {
                /* USB keyboard (SDL2/wut exposes it): the SAME key grammar
                 * as the Windows and Wii ports - F1 menu, arrows scroll /
                 * navigate, PgUp/PgDn pages, Home top, Tab focus cycle,
                 * Return activate, Escape clear, F5 reload. */
                SDL_Keycode sym=ev.key.keysym.sym;
                if(sym==SDLK_F1){
                    if(cwv_menu_ui_mode(&s_menu)==CWV_MENU_CLOSED)
                        cwv_menu_ui_open(&s_menu,CWV_MENU_MAIN);
                    else cwv_menu_ui_close(&s_menu);
                    break;
                }
                if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED){
                    switch(sym){
                    case SDLK_UP:    cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_UP);break;
                    case SDLK_DOWN:  cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_DOWN);break;
                    case SDLK_LEFT:  cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_LEFT);break;
                    case SDLK_RIGHT: cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_RIGHT);break;
                    case SDLK_RETURN: cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_CONFIRM);break;
                    case SDLK_ESCAPE: cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_CANCEL);break;
                    case SDLK_PAGEDOWN: cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_R1);break;
                    case SDLK_PAGEUP:  cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_L1);break;
                    }
                    break;
                }
                switch(sym){
                case SDLK_UP:     cwv_host_scroll_by(host,-60.0f);break;
                case SDLK_DOWN:   cwv_host_scroll_by(host,60.0f);break;
                case SDLK_LEFT:   /* 2026-10-06 (74): USB-keyboard arrows
                                     could not scroll horizontally while
                                     browsing (menu only) - same gap the
                                     Win/Wii ports had. */
                                  cwv_host_scroll_x_by(host,-60.0f);break;
                case SDLK_RIGHT:  cwv_host_scroll_x_by(host,60.0f);break;
                case SDLK_PAGEDOWN: cwv_host_scroll_by(host,480.0f);break;
                case SDLK_PAGEUP:  cwv_host_scroll_by(host,-480.0f);break;
                case SDLK_HOME:   cwv_host_scroll_to(host,0.0f);break;
                case SDLK_TAB:    cwv_host_focus_cycle(host,1);break;
                case SDLK_RETURN: cwv_host_focus_activate(host);break;
                case SDLK_ESCAPE: cwv_host_focus_clear(host);break;
                case SDLK_F5: {
                    const char *cur=cwv_host_current_url(host);
                    if(cur && cwv_host_load(host,cur)==0)
                        cwv_menu_ui_record_page(&s_menu);
                    break;
                }
                default: break;
                }
                break;
            }
            case SDL_JOYBUTTONUP:
                menu_buttons &= ~cwv_wiiu_hotkey_bit(ev.jbutton.button);
                if (ev.jbutton.button == WIIU_BTN_START &&
                    !s_menu_url_ime && cwv_menu_ui_mode(&s_menu)==CWV_MENU_CLOSED)
                    (void)cwv_host_eval(host, "window.CwvInput && window.CwvInput.buttonUp('START')");
                if (ev.jbutton.button == WIIU_BTN_A)
                    cwv_host_focus_release(host);
                if(ev.jbutton.button==WIIU_BTN_A && s_cursor_a){
                    int cx,cy;
                    s_cursor_a=0;
                    if(cwv_cursor_position(&s_cursor,&cx,&cy)){
                        if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                            cwv_menu_ui_touch(&s_menu,cx,cy,0);
                        else cwv_host_pointer_event(host,cx,cy,0);
                    }
                }
                break;
            case SDL_JOYBUTTONDOWN: {
                unsigned before=menu_buttons;
                menu_buttons |= cwv_wiiu_hotkey_bit(ev.jbutton.button);
                if(s_menu_url_ime)break; /* swkbd owns pad until completed */
                if(cwv_wiiu_hotkey_edge(s_menu.settings.menu_hotkey,before,menu_buttons)){
                    if(cwv_menu_ui_mode(&s_menu)==CWV_MENU_CLOSED)
                        cwv_menu_ui_open(&s_menu,CWV_MENU_MAIN);
                    else cwv_menu_ui_close(&s_menu);
                    break;
                }
                switch (ev.jbutton.button) {
                case WIIU_BTN_START:
                    if (cwv_menu_ui_mode(&s_menu)==CWV_MENU_CLOSED)
                        (void)cwv_host_eval(host, "window.CwvInput && window.CwvInput.buttonDown('START')");
                    break;
                case WIIU_BTN_A: {
                    int cx,cy;
                    if(s_cursor.visible && cwv_cursor_position(&s_cursor,&cx,&cy)){
                        /* A pointing Wiimote A taps at the cursor. */
                        s_cursor_a=1;
                        if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                            cwv_menu_ui_touch(&s_menu,cx,cy,1);
                        else cwv_host_pointer_event(host,cx,cy,1);
                    } else if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                        cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_CONFIRM);
                    else cwv_host_focus_activate(host);
                    break;
                }
                case WIIU_BTN_B:
                    if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                        cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_CANCEL);
                    break;
                case WIIU_BTN_L:
                case WIIU_BTN_R:
                    if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED)
                        cwv_menu_ui_input(&s_menu,ev.jbutton.button==WIIU_BTN_L
                            ?CWV_MENU_INPUT_L1:CWV_MENU_INPUT_R1);
                    break;
                case WIIU_BTN_UP:
                case WIIU_BTN_DOWN:
                case WIIU_BTN_LEFT:
                case WIIU_BTN_RIGHT: {
                    int is_menu=cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED;
                    int button=ev.jbutton.button;
                    if(is_menu)cwv_menu_ui_input(&s_menu,
                        button==WIIU_BTN_UP?CWV_MENU_INPUT_UP:
                        button==WIIU_BTN_DOWN?CWV_MENU_INPUT_DOWN:
                        button==WIIU_BTN_LEFT?CWV_MENU_INPUT_LEFT:CWV_MENU_INPUT_RIGHT);
                    else if(cwv_menu_ui_effective_scroll_mode(&s_menu)==2){
                        float step=s_menu.settings.scroll_speed==0?4.0f:
                                   s_menu.settings.scroll_speed==2?14.0f:8.0f;
                        if(button==WIIU_BTN_UP)cwv_host_scroll_by(host,-step);
                        else if(button==WIIU_BTN_DOWN)cwv_host_scroll_by(host,step);
                        else if(button==WIIU_BTN_LEFT)cwv_host_scroll_x_by(host,-step);
                        else cwv_host_scroll_x_by(host,step);
                    }else if(button==WIIU_BTN_UP||button==WIIU_BTN_DOWN)
                        cwv_host_focus_cycle(host,button==WIIU_BTN_UP?-1:1);
                    break;
                }
                case WIIU_BTN_SELECT:
                    /* SELECT has no standalone action. START is dispatched above. */
                    break;
                default: break;
                }
                break;
            }
            case SDL_JOYHATMOTION:
                if(s_menu_url_ime)break;
                if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED){
                    if(ev.jhat.value&SDL_HAT_UP)cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_UP);
                    else if(ev.jhat.value&SDL_HAT_DOWN)cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_DOWN);
                    else if(ev.jhat.value&SDL_HAT_LEFT)cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_LEFT);
                    else if(ev.jhat.value&SDL_HAT_RIGHT)cwv_menu_ui_input(&s_menu,CWV_MENU_INPUT_RIGHT);
                }else if(cwv_menu_ui_effective_scroll_mode(&s_menu)==2){
                    /* A hat can report diagonals (UP|LEFT, etc.). Dispatch
                     * each axis independently so one direction doesn't mask
                     * the other; this also keeps page x/y offsets in sync. */
                    if(ev.jhat.value&SDL_HAT_UP)cwv_host_scroll_by(host,-60.0f);
                    if(ev.jhat.value&SDL_HAT_DOWN)cwv_host_scroll_by(host,60.0f);
                    if(ev.jhat.value&SDL_HAT_LEFT)cwv_host_scroll_x_by(host,-60.0f);
                    if(ev.jhat.value&SDL_HAT_RIGHT)cwv_host_scroll_x_by(host,60.0f);
                }else{
                    if(ev.jhat.value&SDL_HAT_UP)cwv_host_focus_cycle(host,-1);
                    else if(ev.jhat.value&SDL_HAT_DOWN)cwv_host_focus_cycle(host,1);
                }
                break;
            default:
                break;
            }
        }
        /* Wii U SDL2 maps L stick x/y to 0/1, R stick x/y to 2/3.
         * Honor the same selectable scroll and zoom axes as Vita. */
        if (stick && !s_menu_url_ime) {
            int sm=cwv_menu_ui_effective_scroll_mode(&s_menu);
#if CWV_ZOOM_ENABLED
            int zm=cwv_menu_ui_effective_zoom_mode(&s_menu);
#endif
            int is_menu=cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED;
            int axis=is_menu?3:sm==1?1:3;
            int sy=SDL_JoystickGetAxis(stick,axis);
            if ((is_menu || sm!=2) && (sy < -8000 || sy > 8000)) {
                float speed=s_menu.settings.scroll_speed==0?0.5f:
                            s_menu.settings.scroll_speed==2?1.5f:1.0f;
                float delta=(float)sy/32768.0f*9.0f*speed;
                if (is_menu)cwv_menu_ui_scroll(&s_menu,delta);
                else cwv_host_scroll_by(host,delta);
            }
            if(!is_menu && sm!=2){
                int sx=SDL_JoystickGetAxis(stick,sm==1?0:2);
                if(sx < -8000 || sx > 8000)
                    cwv_host_scroll_x_by(host,(float)sx/32768.0f*9.0f);
            }
#if CWV_ZOOM_ENABLED
            /* Project zoom:off (package.json wiiu.zoom=false) removes the
             * analog zoom axis too - the menu rows and actions are already
             * gated in cwv_menu_ui.c (2026-09-23 (39)). */
            if(!is_menu && zm!=2 && s_menu.overrides.zoom_level<=0.0f){
                int za=SDL_JoystickGetAxis(stick,zm==1?1:3);
                if(za < -8000 || za > 8000){
                    float next=cwv_host_zoom(host)-(float)za/32768.0f*0.018f;
                    if(next<0.75f)next=0.75f;
                    if(next>2.0f)next=2.0f;
                    if(next!=cwv_host_zoom(host)){
                        (void)cwv_host_set_zoom(host,next);
                        s_menu.settings.zoom_level=next;
                        s_menu.settings.dirty=1;
                    }
                }
            }
#endif
        }

        #if CWV_CURSOR_ENABLED
        /* ---- Wiimote IR cursor -------------------------------------- */
        /* Cursor exists ONLY while a Wii Remote (any attachment: bare,
         * MotionPlus, nunchuk, MotionPlus+nunchuk) is actually pointing at
         * the sensor bar.  Classic/Pro extensions have no IR, so no cursor
         * - and there is deliberately no setting for it.  KPAD pos is in
         * the OS pointing space (1280x720, the same space as the DRC touch
         * panel); map it to the 960x544 virtual canvas. */
        {
            int chan, pointing = 0;
            for (chan = 0; chan < 4 && !pointing; chan++) {
                KPADStatus k;
                KPADError kerr;
                memset(&k, 0, sizeof(k));
                if (KPADReadEx(chan, &k, 1, &kerr) != 1 ||
                    kerr != KPAD_ERROR_OK)
                    continue;
                if (k.posValid == 0)
                    continue;
                if (k.extensionType == WPAD_EXT_CORE ||
                    k.extensionType == WPAD_EXT_MPLUS ||
                    k.extensionType == WPAD_EXT_NUNCHUK ||
                    k.extensionType == WPAD_EXT_MPLUS_NUNCHUK) {
                    pointing = 1;
                    /* KPAD builds differ in whether pos is already in
                     * 1280x720 OS pixels or normalized.  Normalized values
                     * previously collapsed the cursor into the top-left
                     * couple of pixels.  Accept both representations. */
                    {
                        float px = k.pos.x, py = k.pos.y;
                        if (px >= -1.01f && px <= 1.01f &&
                            py >= -1.01f && py <= 1.01f) {
                            if (px < 0.0f || py < 0.0f) {
                                px = (px + 1.0f) * 640.0f;
                                py = (py + 1.0f) * 360.0f;
                            } else {
                                px *= 1280.0f;
                                py *= 720.0f;
                            }
                        }
                        cwv_cursor_place(&s_cursor,
                            px * (960.0f / 1280.0f),
                            py * (544.0f / 720.0f), 960, 544);
                    }
                }
            }
            cwv_cursor_show(&s_cursor, pointing);
        }
#endif /* CWV_CURSOR_ENABLED */

        /* ---- engine frame ------------------------------------------- */
        menu_poll_url();
        cwv_host_update(host);
        cwv_menu_ui_apply_runtime(&s_menu);
        if(s_menu_quit)running=0;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        if (s_boot_failed) draw_boot_screen(boot_font);
        else cwv_host_draw(host);
        if(cwv_menu_ui_mode(&s_menu)!=CWV_MENU_CLOSED){
            CwvMenuData live;
            uint32_t now=SDL_GetTicks();
            float ms=last_ticks?(float)(now-last_ticks):16.7f;
            menu_fill_live(&live,host,frame,ms);
            cwv_menu_ui_draw(&s_menu,&live);
        }
#if CWV_CURSOR_ENABLED
        /* The cursor vanishes while ANY CWMenu is open (user direction,
         * 2026-09-26 (43)) - it used to render on top of the dimmed
         * page OUTSIDE the menu window too.  Position/taps keep
         * updating (A taps the menu through the cursor position),
         * only the drawing is suppressed. */
        if (cwv_menu_ui_mode(&s_menu) == CWV_MENU_CLOSED) {
            /* CSS cursor (2026-09-26 (45)): ask the engine what sits
             * under the pointer this frame and apply it to the art.
             * The settings toggle gates the whole feature; NULL always
             * restores the default console art/arrow. */
            CwvCursorInfo info;
            int info_x, info_y;
            if (s_menu.settings.css_cursors &&
                cwv_cursor_position(&s_cursor, &info_x, &info_y) &&
                cwv_host_cursor_at(host, info_x, info_y, &info))
                cwv_cursor_set_style(&s_cursor, &info);
            else
                cwv_cursor_set_style(&s_cursor, NULL);
            cwv_cursor_draw(&s_cursor);
        }
#endif
        last_ticks=SDL_GetTicks();
        /* System keyboard LAST, on top of the page (see the draw-hook
         * note in cwv_ime_swkbd.h): wut's swkbd must draw inside the
         * frame's GX2 context, before the swap in SDL_RenderPresent. */
        cwv_ime_draw();
        SDL_RenderPresent(renderer);
    }

    cwv_host_destroy(host);
#if CWV_CURSOR_ENABLED
    cwv_cursor_destroy(&s_cursor);
#endif
    if (s_menu_favicon) cwv_tex_free(s_menu_favicon);
    if (boot_font) cwv_font_free(boot_font);
    if (stick) SDL_JoystickClose(stick);
    cwv_wiiu_text_cache_clear();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    WHBProcShutdown();
    return 0;
}
