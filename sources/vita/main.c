#include <psp2/ctrl.h>
#include <psp2/power.h>

/* DEBUG-BUILD FLAG (compile-time).  1 = debug build: the on-page debug
 * overlay is live; SELECT toggles/collapses it and SELECT+X customizes rows.
 * START remains available to the webpage in both debug and production builds.
 * Enabled at build time by `build-vita.sh --debug` or the webapp manifest
 * "debug": true; never by anything a page or settings file can set at runtime. */
#ifndef CWV_DEBUG
#define CWV_DEBUG 0
#endif
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdarg.h>
#include <psp2/display.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <psp2/system_param.h>
#include <psp2/apputil.h>
#include "cwv_boot_display.h"

#include "core/cwv_host.h"
#include "core/cwv_renderer.h"   /* CwvCursorInfo, CWV_CSTYLE_* (2026-09-26 (45)) */
#include "core/cwv_backend.h"
#include "cwv_scroll.h"
#include "menu/native_menu_templates.h"
#include "menu/cwmenu_api.h"
#include "core/cwv_cursor.h"
#include "cwv_kbm_vita.h"   /* Bluetooth HID keyboard + mouse (SceHid) */
#include "core/cwv_project.h"
#include "cwv_version.h"


#define SCREEN_W 960
#define SCREEN_H 544



typedef enum {
    MENU_CLOSED=0,
    MENU_MAIN=1,
    MENU_SETTINGS=2,
    MENU_DEVTOOLS=3,
    MENU_PAGEOPS=4,
    MENU_FILES=5   /* (84) web-content tree browser */
} NativeMenuMode;

/* Menu systems are swappable at the launch site. For example, changing this
 * to MENU_DEVTOOLS makes the main-menu Page Operations entry open the full
 * DevTools system (template, tabs, input, scrolling, and close behavior)
 * without changing the main menu renderer. */
#ifndef CWV_PAGE_OPERATIONS_MENU_MODE
#define CWV_PAGE_OPERATIONS_MENU_MODE MENU_PAGEOPS
#endif
#ifndef CWV_SETTINGS_MENU_MODE
#define CWV_SETTINGS_MENU_MODE MENU_SETTINGS
#endif
#ifndef CWV_DEVTOOLS_MENU_MODE
#define CWV_DEVTOOLS_MENU_MODE MENU_DEVTOOLS
#endif
/* Debug overlay row ids (order = display order).  A row is visible when
 * (g_menu.dbg_mask & (1<<id)) and the overlay itself is toggled by SELECT. */
enum { DBG_FPS=0, DBG_FRAME, DBG_BTN, DBG_ERR, DBG_ESM, DBG_UPT, DBG_URL, DBG_CNS, DBG_COUNT };
static const char *const dbg_row_names[DBG_COUNT]={
    "fps","frame","button","last error","ESM","uptime","url","console"
};

typedef struct {
    NativeMenuMode mode;
    int selected;
    int scroll_mode; /* 0=RAnalog, 1=LAnalog, 2=DPad */
    int scroll_speed; /* 0=Slow, 1=Normal, 2=Fast */
    int zoom_mode;   /* 0=RAnalog, 1=LAnalog, 2=Off */
    float zoom_level; /* 0.75x-2.0x */
    int dark_mode;
    int accent; /* 0 blue, 1 red, 2 orange, 3 green, 4 cyan, 5 dark purple */
    int pointer; /* rear-touchpad cursor: 0 off, 1 on (Settings > Controls) */
    int css_cursors; /* 1 = pages choose pointer art via CSS cursor (2026-09-26 (45)) */
    int dirty;
    char history[16][1024];
    int history_count;
    int history_index;
    int touch_down;
    int touch_x;
    int touch_y;
    int touch_capture;
    float settings_scroll;
    int settings_scroll_drag;
    int settings_tab;       /* 0=Appearance, 1=Controls, 2=Preferences */
    int settings_item;      /* content item within the active settings tab */
    int settings_status;    /* short-lived UI status indicator */
    int dbg_overlay;       /* debug overlay drawn on top of the page */
    int dbg_mask;          /* which overlay rows are shown (1<<DBG_*) */
    int dbg_custom;        /* overlay customize mode (cursor + toggling) */
    int dbg_item;          /* customize cursor row */
    char dbg_lastbtn[16];  /* last pressed button, by name ("" = none) */
    float devtools_scroll;
    int devtools_scroll_drag;
#if CWV_FILES_ENABLED
    int files_selected;      /* current-directory row cursor */
    char files_path[320];    /* current directory, relative to app0: */
    float files_scroll;      /* current-directory scroll offset (px) */
    int files_scroll_drag;   /* finger is dragging the tree */
    float files_drag_anchor_scroll; /* scroll value when the drag began */
    int files_drag_anchor_y;       /* screen y when the drag began */
#endif
    int dev_section; /* 0=performance,1=renderer,2=input,3=page,4=overrides,5=console,6=platform */
    int pageops_selected; /* 0=Back, 1=Forward, 2=Reload */
    int dev_option;  /* selected row within an override panel */
    int exit_requested;
    int exit_confirm_selected; /* 0=No, 1=Yes */
    int exit_confirm_return_section;
    int override_vcm;       /* -1 = page/config, otherwise VCM_* */
    int override_scroll;    /* -1 = settings, otherwise 0..2 */
    int override_zoom;      /* -1 = settings, otherwise 0..2 */
    int override_theme;     /* -1 = settings, otherwise 0/1 */
    float override_zoom_level; /* <0 = settings */
    int override_scrollbars; /* -1 = automatic, 0 hidden, 1 visible */
    int override_button_wait; /* -1 = page/default; >=0 = explicit override */
    /* Menu window appearance. Defaults preserve the existing menu. */
    int menu_window_corner_style; /* 0=rounded, 1=sharp */
    float menu_window_scale;       /* default 1.0, range 0.75..1.25 */
    char override_button_wait_spec[64];
    int button_wait_ime_active;
    SceWChar16 button_wait_ime_initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
    SceWChar16 button_wait_ime_buffer[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
    int open_url_ime_active;
    SceWChar16 open_url_ime_initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
    SceWChar16 open_url_ime_buffer[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
    vita2d_texture *page_favicon;
    char favicon_source[1024];
} NativeMenuState;

static NativeMenuState g_menu;

typedef enum {
    VCM_NONE=0,
    VCM_BROWSER=1,
    VCM_STANDALONE=2,
    VCM_TOUCH=3,
    VCM_CURSOR=4
} VitaControlMode;

static const char *vcm_name(int mode){
    switch(mode){
        case VCM_BROWSER:return "Browser";
        case VCM_STANDALONE:return "Standalone";
        case VCM_TOUCH:return "Touch";
        case VCM_CURSOR:return "Cursor";
        default:return "None";
    }
}

static void write_runtime_log(const char *fmt, ...);

/* Vita power callbacks run on a dedicated callback thread.  The callback
 * only publishes flags; renderer, input, and logging work stays on the main
 * thread so no Vita2D or CWV host APIs are called from the callback context. */
static volatile int g_vita_suspended = 0;
static volatile int g_vita_resume_pending = 0;
static SceUID g_vita_power_callback_id = -1;

static int vita_power_callback(int notify_id, int notify_count,
                               int power_info, void *common)
{
    (void)notify_count;
    (void)power_info;
    (void)common;

    if (notify_id & (SCE_POWER_CB_APP_SUSPEND | SCE_POWER_CB_SYSTEM_SUSPEND)) {
        g_vita_suspended = 1;
    }
    if (notify_id & (SCE_POWER_CB_APP_RESUME | SCE_POWER_CB_SYSTEM_RESUME |
                     SCE_POWER_CB_AFTER_SYSTEM_RESUME)) {
        g_vita_suspended = 0;
        g_vita_resume_pending = 1;
    }
    return 0;
}

static int vita_power_callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    g_vita_power_callback_id = sceKernelCreateCallback(
        "CWV Power Callback", 0, vita_power_callback, NULL);
    if (g_vita_power_callback_id < 0) {
        write_runtime_log("power: callback creation failed (%d)",
                          (int)g_vita_power_callback_id);
        sceKernelExitThread(0);
        return 0;
    }
    int rc = scePowerRegisterCallback(g_vita_power_callback_id);
    write_runtime_log("power: callback id=%d register rc=%d",
                      (int)g_vita_power_callback_id, rc);
    if (rc < 0) {
        g_vita_power_callback_id = -1;
        sceKernelExitThread(0);
        return 0;
    }
    for (;;) sceKernelDelayThreadCB(10 * 1000 * 1000);
    return 0;
}

static void vita_power_callbacks_start(void)
{
    SceUID thread_id = sceKernelCreateThread(
        "CWV Power Events", vita_power_callback_thread,
        0x10000100, 0x10000, 0, 0, NULL);
    if (thread_id < 0) {
        write_runtime_log("power: callback thread creation failed (%d)",
                          (int)thread_id);
        return;
    }
    int rc = sceKernelStartThread(thread_id, 0, NULL);
    write_runtime_log("power: callback thread id=%d start rc=%d",
                      (int)thread_id, rc);
}

/* Read the physical Vita controller from the normal positive polling API.
 *
 * The normal API is the stable path for the built-in Vita controller and
 * exposes both the digital button mask and the analogue axes. The extended
 * API remains a fallback if the normal poll fails. Using the extended API as
 * the primary source made physical hardware and Vita3K behave differently for
 * scrolling input.
 */
static int vita_read_controller(SceCtrlData *pad)
{
    if(!pad) return -1;
    memset(pad,0,sizeof(*pad));

    int rc=sceCtrlPeekBufferPositive(0,pad,1);
    if(rc<=0){
        memset(pad,0,sizeof(*pad));
        rc=sceCtrlPeekBufferPositiveExt(0,pad,1);
    }
    if(rc<=0) return rc;

    if(pad->up)       pad->buttons |= SCE_CTRL_UP;
    if(pad->right)    pad->buttons |= SCE_CTRL_RIGHT;
    if(pad->down)     pad->buttons |= SCE_CTRL_DOWN;
    if(pad->left)     pad->buttons |= SCE_CTRL_LEFT;
    if(pad->l1)       pad->buttons |= SCE_CTRL_L1;
    if(pad->r1)       pad->buttons |= SCE_CTRL_R1;
    if(pad->triangle) pad->buttons |= SCE_CTRL_TRIANGLE;
    if(pad->circle)   pad->buttons |= SCE_CTRL_CIRCLE;
    if(pad->cross)    pad->buttons |= SCE_CTRL_CROSS;
    if(pad->square)   pad->buttons |= SCE_CTRL_SQUARE;

    if(pad->lt || pad->l1) pad->buttons |= SCE_CTRL_LTRIGGER;
    if(pad->rt || pad->r1) pad->buttons |= SCE_CTRL_RTRIGGER;

    /* Bluetooth HID keyboard (cwv_kbm_vita.c): held keys synthesize the
     * SAME controller bits, so menus, settings, devtools and page all
     * treat the keyboard exactly like the D-pad.  Edge detection in the
     * main loop keeps working: it compares consecutive controller reads. */
    pad->buttons |= cwv_kbm_vita_ctrl_bits();
    return rc;
}

static void browser_back(CwvHost *host);
static void browser_forward(CwvHost *host);
static void browser_refresh(CwvHost *host);


typedef struct {
    int active;
    uint64_t next_us;
} PageButtonWaitState;

static PageButtonWaitState g_page_wait[11];

static int page_button_index(unsigned mask){
    switch(mask){
        case SCE_CTRL_CROSS:return 0; case SCE_CTRL_CIRCLE:return 1;
        case SCE_CTRL_SQUARE:return 2; case SCE_CTRL_TRIANGLE:return 3;
        case SCE_CTRL_UP:return 4; case SCE_CTRL_RIGHT:return 5;
        case SCE_CTRL_DOWN:return 6; case SCE_CTRL_LEFT:return 7;
        case SCE_CTRL_LTRIGGER:return 8; case SCE_CTRL_RTRIGGER:return 9;
        case SCE_CTRL_START:return 10;
        default:return -1;
    }
}

/* Returns 1 for a press/repeat, -1 for the release edge, 0 for no event.
 * Web pages retain the historical edge-triggered behavior unless the page
 * explicitly opts into cwv-button-wait. */
static int page_button_event(CwvHost *host,unsigned mask,int held,uint64_t now_us){
    int idx=page_button_index(mask);
    if(idx<0)return 0;
    PageButtonWaitState *st=&g_page_wait[idx];
    const char *spec=cwv_host_button_wait(host);
    if(!spec || !*spec){
        if(held && !st->active){st->active=1;return 1;}
        if(!held){int was=st->active;st->active=0;return was?-1:0;}
        return 0;
    }
    while(isspace((unsigned char)*spec))spec++;
    if(!strcasecmp(spec,"release")){
        if(held && !st->active){st->active=1;return 1;}
        if(!held){int was=st->active;st->active=0;return was?-1:0;}
        return 0;
    }
    if(!strcasecmp(spec,"none")){
        if(held){st->active=1;return 1;}
        {int was=st->active;st->active=0;return was?-1:0;}
    }
    char *end=NULL; double first=strtod(spec,&end);
    if(end==spec || first<0.0){
        if(held && !st->active){st->active=1;return 1;}
        if(!held){int was=st->active;st->active=0;return was?-1:0;}
        return 0;
    }
    while(isspace((unsigned char)*end))end++;
    if(*end=='s'||*end=='S')end++;
    while(isspace((unsigned char)*end))end++;
    double repeat=first;
    if(*end){
        char *e2=NULL; double second=strtod(end,&e2);
        if(e2!=end && second>=0.0){ while(isspace((unsigned char)*e2))e2++; if(*e2=='s'||*e2=='S')repeat=second; }
    }
    uint64_t first_us=(uint64_t)(first*1000000.0);
    uint64_t repeat_us=(uint64_t)(repeat*1000000.0);
    if(held){
        if(!st->active){st->active=1;st->next_us=now_us+first_us;return 1;}
        if(now_us>=st->next_us){
            st->next_us=now_us+(repeat_us?repeat_us:1);
            return 1;
        }
        return 0;
    }
    {int was=st->active;st->active=0;st->next_us=0;return was?-1:0;}
}

static void page_button_reset_all(void){ memset(g_page_wait,0,sizeof(g_page_wait)); }


/* Back-touchpad pointer cursor (Settings > Controls > Touchpad pointer).
 * The rear pad is read like the front one but only drives the cursor;
 * taps on release go through the same menu/page dispatch as a front tap. */
static CwvCursor g_cursor;
static int g_cursor_down, g_cursor_down_x, g_cursor_down_y;
/* Cursor page mode (VCM_CURSOR) tap state - kept separate so the back
 * touchpad pointer and the D-pad cursor can never fight over one flag. */
static int g_cursor_tap, g_cursor_tap_x, g_cursor_tap_y;

static void settings_defaults(void){
    g_menu.settings_tab=0; g_menu.settings_item=0; g_menu.settings_status=0;
    g_menu.scroll_mode=1; g_menu.scroll_speed=1;
    cwm_var_set_number("scroll_speed_stops",3.0f);
    cwm_var_set_number("pitch_stops",6.0f);
    cwm_var_set_number("menu_brightness",1.0f);
    cwm_var_set_number("menu_accent_tint",1.0f);
    g_menu.zoom_mode=0; g_menu.zoom_level=1.0f; g_menu.dark_mode=1; g_menu.accent=0; g_menu.pointer=1; g_menu.css_cursors=1; g_menu.dirty=0;
    g_menu.override_vcm=-1; g_menu.override_scroll=-1; g_menu.override_zoom=-1;
    g_menu.override_theme=-1; g_menu.override_zoom_level=-1.0f; g_menu.override_scrollbars=-1; g_menu.override_button_wait=-1;
    g_menu.override_button_wait_spec[0]=0;
    g_menu.button_wait_ime_active=0;
    g_menu.open_url_ime_active=0;
    g_menu.dbg_overlay=0;
    g_menu.dbg_mask=(1<<DBG_FPS)|(1<<DBG_FRAME)|(1<<DBG_BTN)|(1<<DBG_ERR)|(1<<DBG_ESM);
    g_menu.dbg_custom=0; g_menu.dbg_item=0;
    g_menu.dbg_lastbtn[0]=0;
}
static const char *app_data_dir(void){
    static char dir[128];
    static int initialized=0;
    if(initialized)return dir;
    initialized=1;
    snprintf(dir,sizeof(dir),"%s",CWV_APP_DATA_DIR);
    /* package.json controls the namespace, not an arbitrary Vita path.
       Keep it to one safe directory component so ../ cannot escape the
       CWebView data root. */
    if(!dir[0] || !strcmp(dir,".") || !strcmp(dir,"..") || strstr(dir,".."))
        snprintf(dir,sizeof(dir),"%s",VITA_TITLEID);
    for(size_t i=0;dir[i];i++){
        unsigned char c=(unsigned char)dir[i];
        if(!(isalnum(c)||c=='_'||c=='-'||c=='.'))dir[i]='_';
    }
    if(!dir[0])snprintf(dir,sizeof(dir),"%s",VITA_TITLEID);
    return dir;
}

/* (85) data layout per user correction: <data>/CWebView/$TITLEID.  The
 * CWebView folder is the app-family parent; the per-app segment is the
 * TITLE ID (a custom package.json storageDirectory replaces it, so two
 * CWebView-based apps never collide - the actual (84) complaint).
 * (84) briefly used a flat ux0:data/<TITLE_ID>/ root; that root is kept
 * as a one-time read-migration source below. */
static void app_data_path(char *out,size_t cap,const char *file){
    if(!out||!cap)return;
    snprintf(out,cap,"ux0:data/CWebView/%s/%s",app_data_dir(),
             file?file:"settings.cfg");
}

/* (84) flat title-id root, kept for one-time read migration only. */
static void app_data_legacy_path(char *out,size_t cap,const char *file){
    if(!out||!cap)return;
    const char *ns=app_data_dir();
    if(!strcmp(ns,VITA_TITLEID))
        snprintf(out,cap,"ux0:data/%s/%s",VITA_TITLEID,file?file:"settings.cfg");
    else
        snprintf(out,cap,"ux0:data/%s/%s/%s",VITA_TITLEID,ns,file?file:"settings.cfg");
}

static void app_data_prepare(void){
    char root[256];
    const char *ns=app_data_dir();
    snprintf(root,sizeof(root),"ux0:data/CWebView/%s",ns);
    sceIoMkdir("ux0:data",0777);
    sceIoMkdir("ux0:data/CWebView",0777);
    sceIoMkdir(root,0777);
}

static void settings_load(void){
    SceUID fd;
    char b[256];
    int n;
    int scroll=1, scroll_speed=1, zoom=0, dark=1, accent=0;
    float zoom_level=1.0f;
    char path[256];

    settings_defaults();
    app_data_path(path,sizeof(path),"settings.cfg");
    fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0){
        /* (85) settings saved under the (84) flat title-id root still
         * load once; the next save writes ux0:data/CWebView/<TITLE_ID>/.
         * The pre-84 layout was already <data>/CWebView/<ns>, so apps
         * upgrading straight from (83) keep their settings untouched. */
        char legacy[256];
        app_data_legacy_path(legacy,sizeof(legacy),"settings.cfg");
        fd=sceIoOpen(legacy,SCE_O_RDONLY,0);
        if(fd>=0)
            write_runtime_log("settings: migrated %s -> %s",legacy,path);
        else{
            write_runtime_log("settings: no config file at %s (rc=%d); using defaults",path,(int)fd);
            return;
        }
    }

    n=sceIoRead(fd,b,sizeof(b)-1);
    sceIoClose(fd);
    if(n<=0){
        write_runtime_log("settings: config empty/read failed (%d); using defaults",n);
        return;
    }
    b[n]=0;

    /* Accept normal LF, CRLF, arbitrary line order, and harmless whitespace. */
    {
        char *line=b;
        while(line && *line){
            char *next=strchr(line,'\n');
            char key[32]={0};
            int value;
            if(next)*next=0;
            if(sscanf(line," %31[^= ] = %d",key,&value)==2){
                if(!strcmp(key,"scroll")) scroll=value;
                else if(!strcmp(key,"scroll_speed")) scroll_speed=value;
                else if(!strcmp(key,"zoom")) zoom=value;
                else if(!strcmp(key,"zoom_level")) zoom_level=(float)value/100.0f;
                else if(!strcmp(key,"dark")) dark=value;
                else if(!strcmp(key,"accent")) accent=value;
                else if(!strcmp(key,"dbgshow")) g_menu.dbg_overlay=(value!=0);
#if !CWV_DEBUG
                /* production: a dbgshow=1 left over from a debug session
                   must NOT re-enable the compiled-out overlay */
                if(!strcmp(key,"dbgshow")) g_menu.dbg_overlay=0;
#endif
                else if(!strcmp(key,"dbgmask")) g_menu.dbg_mask=value;
                else if(!strcmp(key,"pointer")) g_menu.pointer=(value!=0);
                else if(!strcmp(key,"css_cursors")) g_menu.css_cursors=(value!=0);
            }
            if(!next)break;
            line=next+1;
        }
    }

    if(scroll<0||scroll>2)scroll=1;
    if(scroll_speed<0||scroll_speed>2)scroll_speed=1;
    if(zoom<0||zoom>2)zoom=0;
    if(zoom_level<0.75f||zoom_level>2.0f)zoom_level=1.0f;
    if(accent<0||accent>5)accent=0;
    g_menu.scroll_mode=scroll;
    g_menu.scroll_speed=scroll_speed;
    g_menu.zoom_mode=zoom;
    g_menu.zoom_level=zoom_level;
    g_menu.dark_mode=dark?1:0;
    g_menu.accent=accent;
    g_menu.dirty=0;
    write_runtime_log("settings: loaded namespace=%s scroll=%d speed=%d zoom=%d dark=%d accent=%d",
                      app_data_dir(),g_menu.scroll_mode,g_menu.scroll_speed,g_menu.zoom_mode,g_menu.dark_mode,g_menu.accent);
}
static void settings_save(void){
    char path[256];
    app_data_prepare();
    app_data_path(path,sizeof(path),"settings.cfg");
    SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd>=0){
        char b[192];
        int n=snprintf(b,sizeof(b),"scroll=%d\nscroll_speed=%d\nzoom=%d\nzoom_level=%d\ndark=%d\naccent=%d\ndbgshow=%d\ndbgmask=%d\npointer=%d\ncss_cursors=%d\n",g_menu.scroll_mode,g_menu.scroll_speed,g_menu.zoom_mode,(int)(g_menu.zoom_level*100.0f+0.5f),g_menu.dark_mode,g_menu.accent,g_menu.dbg_overlay?1:0,g_menu.dbg_mask,g_menu.pointer,g_menu.css_cursors);
        sceIoWrite(fd,b,n);
        sceIoClose(fd);
    }
    g_menu.dirty=0;
}
static void history_reset(const char *url){
    g_menu.history_count=0;g_menu.history_index=-1;
    if(url&&*url){snprintf(g_menu.history[0],sizeof(g_menu.history[0]),"%s",url);g_menu.history_count=1;g_menu.history_index=0;}
}
static void history_record(const char *url){
    if(!url||!*url)return;
    if(g_menu.history_index>=0 && strcmp(g_menu.history[g_menu.history_index],url)==0)return;
    if(g_menu.history_index+1<g_menu.history_count)g_menu.history_count=g_menu.history_index+1;
    if(g_menu.history_count>=16){memmove(g_menu.history[0],g_menu.history[1],15*sizeof(g_menu.history[0]));g_menu.history_count=15;g_menu.history_index=14;}
    snprintf(g_menu.history[g_menu.history_count],sizeof(g_menu.history[g_menu.history_count]),"%s",url);
    g_menu.history_count++;g_menu.history_index=g_menu.history_count-1;
}
static int menu_button(float x,float y,float w,float h,int tx,int ty){return tx>=(int)x&&tx<=(int)(x+w)&&ty>=(int)y&&ty<=(int)(y+h);}
static const VitaMenuTemplate *menu_template_find(const VitaMenuTemplate *list, const char *name){
    if(!list||!name)return NULL;
    for(const VitaMenuTemplate *t=list;t->name;t++) if(!strcmp(t->name,name)) return t;
    return NULL;
}
static int menu_template_hit(const VitaMenuTemplate *list,const char *name,float ox,float oy,int tx,int ty){
    const VitaMenuTemplate *t=menu_template_find(list,name);
    return t ? menu_button(ox+t->x,oy+t->y,t->w,t->h,tx,ty) : 0;
}

static const VitaMenuTemplate *menu_scrollbar_find(const VitaMenuTemplate *list,const char *owner)
{
    const VitaMenuTemplate *legacy=NULL;
    if(!list) return NULL;
    for(const VitaMenuTemplate *t=list;t->name;t++){
        if(t->kind!=VITA_MENU_SCROLLBAR) continue;
        if(t->name && !strcmp(t->name,"scroll-track")) legacy=t;
        if(owner && t->scroll_owner && !strcmp(t->scroll_owner,owner)) return t;
        if(!owner && t->scroll_owner) return t;
    }
    return legacy;
}

static float menu_scrollbar_content_bottom(const VitaMenuTemplate *tpl,const VitaMenuTemplate *sb,float fallback)
{
    if(!tpl || !sb || !sb->scroll_items || !*sb->scroll_items) return fallback;
    char names[1024];
    snprintf(names,sizeof(names),"%s",sb->scroll_items);
    float bottom=fallback;
    char *part=strtok(names,"|");
    while(part){
        const VitaMenuTemplate *item=menu_template_find(tpl,part);
        if(item && item->y+item->h>bottom) bottom=item->y+item->h;
        part=strtok(NULL,"|");
    }
    return bottom;
}

/* CWMenu actions are the behavioral contract for interactive objects.  The
 * renderer may still use object names for lookup, but input dispatch consumes
 * the script-defined action first.  This keeps visual templates replaceable
 * without tying behavior to a particular label/name. */
static const char *menu_action(const VitaMenuTemplate *t)
{
    return (t && t->action && *t->action) ? t->action : (t ? t->name : "");
}
static int menu_action_is(const VitaMenuTemplate *t,const char *action)
{
    return t && action && !strcmp(menu_action(t),action);
}

/* Project feature gates (cwv_project.h, 2026-09-23 (39)): a disabled
 * feature blanks its settings rows and every activation path no-ops.
 * Enabled features compile the checks away entirely. */
static int vita_row_action_hidden(const VitaMenuTemplate *t)
{
    if(!t) return 0;
#if !CWV_ZOOM_ENABLED
    if(menu_action_is(t,"set_zoom")||menu_action_is(t,"cycle_zoom_input")) return 1;
#endif
#if !CWV_SCROLL_ENABLED
    if(menu_action_is(t,"cycle_scroll_input")||menu_action_is(t,"set_scroll_speed")) return 1;
#endif
#if !CWV_CURSOR_ENABLED
    if(menu_action_is(t,"cycle_pointer")) return 1;
#endif
#if !CWV_DEVTOOLS_ENABLED
    if(menu_action_is(t,"devtools")) return 1;
#endif
#if !CWV_HISTORY_ENABLED
    if(menu_action_is(t,"history-back")||menu_action_is(t,"forward")) return 1;
#endif
    return 0;
}

/* Every inset tab has its own template.  These selectors are the only place
 * where menu behavior chooses a visual contract; rendering and hit-testing
 * then consume that same template. */
static const VitaMenuTemplate *pageops_active_template(void){
    return vita_menu_page_operations_template;
}
static const VitaMenuTemplate *settings_active_template(void);
static const VitaMenuTemplate *devtools_active_template(void);

/* Every menu has a complete visual template. Rendering and hit-testing use
 * the same active template, making menu replacement safe and predictable. */
static const VitaMenuTemplate *native_menu_template(void){
    /* This is the single menu-to-template registry. To add a menu, add one
     * enum value, one template, and one case here; all shell rendering and
     * hit-testing then follow the selected template automatically. */
    switch(g_menu.mode){
        case MENU_MAIN: return vita_menu_main_template;
#if CWV_FILES_ENABLED
        case MENU_FILES:   return vita_menu_main_template; /* shell donor */
#endif
        case MENU_PAGEOPS: return pageops_active_template();
        case MENU_SETTINGS: return settings_active_template();
        case MENU_DEVTOOLS: return devtools_active_template();
        default: return vita_menu_main_template;
    }
}

/* Window rect from the CWMenu 2 window keys.  Legacy scripts (no keys) render
 * exactly as before: base anchor 110/54, scale 1.0, no crop. */
static void native_menu_frame(const VitaMenuTemplate *tpl,float *x,float *y,float *w,float *h){
    const VitaMenuTemplate *window=menu_template_find(tpl,"window");
    if(window){
        float scale=window->win_scale>0.0f?window->win_scale:1.0f;
        float cl=window->crop_l,ct=window->crop_t,cr=window->crop_r,cb=window->crop_b;
        if(scale<0.75f)scale=0.75f;
        if(scale>1.25f)scale=1.25f;
        if(cl<0.0f)cl=0.0f;
        if(ct<0.0f)ct=0.0f;
        if(cr<0.0f)cr=0.0f;
        if(cb<0.0f)cb=0.0f;
        /* Offsets: `anchor` is the absolute screen position, element x/y are
         * additional offsets from it.  This is what lets two windows (the
         * active menu and its `pair` partner) sit side by side anywhere. */
        *x=(window->win_anchor_set?window->win_anchor_x:110.0f)+window->x+cl*scale;
        *y=(window->win_anchor_set?window->win_anchor_y:54.0f)+window->y+ct*scale;
        *w=(window->w-cl-cr)*scale;
        *h=(window->h-ct-cb)*scale;
        if(*w<40.0f)*w=40.0f;
        if(*h<40.0f)*h=40.0f;
    }else{
        *x=110.0f; *y=54.0f; *w=740.0f; *h=436.0f;
    }
}

#if CWV_FILES_ENABLED
static void files_scan(void);            /* (84) Files menu - defined below */
static void files_scroll_to_selection(void);
#endif
static void native_menu_open(NativeMenuMode mode){
    g_menu.mode=mode;
    switch(mode){
        case MENU_MAIN:
            g_menu.selected=0;
            break;
        case MENU_PAGEOPS:
            g_menu.selected=0;
            g_menu.pageops_selected=0;
            break;
#if CWV_FILES_ENABLED
        case MENU_FILES:
            g_menu.selected=0;
            g_menu.files_selected=0;
            g_menu.files_path[0]=0;
            g_menu.files_scroll=0.0f;
            g_menu.files_scroll_drag=0;
            files_scan();
            break;
#endif
        case MENU_SETTINGS:
            g_menu.selected=3;
            g_menu.settings_tab=0;
            g_menu.settings_item=0;
            g_menu.settings_scroll=0.0f;
            g_menu.settings_scroll_drag=0;
            break;
        case MENU_DEVTOOLS:
            g_menu.selected=0;
            g_menu.dev_section=0;
            g_menu.dev_option=0;
            g_menu.exit_confirm_selected=0;
            g_menu.exit_confirm_return_section=0;
            g_menu.devtools_scroll=0.0f;
            g_menu.devtools_scroll_drag=0;
            break;
        default:
            break;
    }
}

static const VitaMenuTemplate *settings_active_template(void){
    switch(g_menu.settings_tab){
        case 1: return vita_menu_settings_controls_template;
        case 2: return vita_menu_settings_preferences_template;
        default: return vita_menu_settings_appearance_template;
    }
}
static unsigned menu_style_accent_color(const VitaMenuTemplate *t,int active);

static const VitaMenuTemplate *devtools_active_template(void){
    if(g_menu.selected==7) return vita_menu_devtools_close_template;
    switch(g_menu.dev_section){
        case 1: return vita_menu_devtools_renderer_template;
        case 2: return vita_menu_devtools_input_template;
        case 3: return vita_menu_devtools_page_template;
        case 4: return vita_menu_devtools_overrides_template;
        case 5: return vita_menu_devtools_console_template;
        case 6: return vita_menu_devtools_platform_template;
        default: return vita_menu_devtools_performance_template;
    }
}
static void menu_accent_rgb(int accent,int *rr,int *gg,int *bb)
{
    /* C built-ins are the FALLBACK since CWMenu 2: a `theme` block in the
     * .cwm script owns these via accent0..accent5, so applications set
     * their whole color scheme without touching C. */
    static const unsigned char colors[][3] = {
        {70,105,150}, {180,55,60}, {205,112,45},
        {55,145,85}, {45,155,175}, {92,55,135}
    };
    char slot[16];
    int i=(accent<0||accent>5)?0:accent;
    snprintf(slot,sizeof(slot),"accent%d",i);
    if(cwm_theme_get_rgb(slot,rr,gg,bb)==0) return;   /* CWMenu theme owns it */
    *rr=colors[i][0]; *gg=colors[i][1]; *bb=colors[i][2];
}
static unsigned menu_theme_color(int accent,float modifier)
{
    int rr,gg,bb; menu_accent_rgb(accent,&rr,&gg,&bb);
    if(modifier<0.0f)modifier=0.0f;
    if(modifier>1.0f)modifier=1.0f;
    return RGBA8((unsigned)(rr*modifier),(unsigned)(gg*modifier),(unsigned)(bb*modifier),255);
}
static unsigned menu_accent_color(int accent, int active){
    return menu_theme_color(accent,active?0.82f:0.34f);
}
static float menu_template_brightness(const VitaMenuTemplate *t)
{
    float b=0.0f, v;
    if(!t) return 1.0f;
    if(t->brightness_var && cwm_var_get_number(t->brightness_var,&v)==0) b=v;
    else if(t->brightness>0.0f) b=t->brightness;
    else {
        switch(t->style){ case VITA_MENU_STYLE_LIGHT: b=1.16f; break; case VITA_MENU_STYLE_DIM: b=0.62f; break; default: b=1.0f; break; }
    }
    if(b<0.25f) b=0.25f;
    if(b>2.0f) b=2.0f;
    return b;
}
static float menu_template_tint(const VitaMenuTemplate *t)
{
    float tint=1.0f, v;
    if(!t) return 1.0f;
    if(t->tint_var && cwm_var_get_number(t->tint_var,&v)==0) tint=v;
    else if(t->tint>=0.0f) tint=t->tint;
    if(tint<0.0f) tint=0.0f;
    if(tint>1.0f) tint=1.0f;
    return tint;
}

static int menu_template_stops(const VitaMenuTemplate *t)
{
    float v; int stops=t?t->stops:0;
    if(t && t->stops_var && cwm_var_get_number(t->stops_var,&v)==0) stops=(int)(v+0.5f);
    if(stops<=0) stops=3;
    if(stops>6) stops=6;
    if(stops<2) stops=2;
    return stops;
}

static unsigned menu_style_theme_color(const VitaMenuTemplate *t,float modifier){
    if(!t) return menu_theme_color(g_menu.accent,modifier);
    modifier*=menu_template_brightness(t);
    if(modifier>1.0f)modifier=1.0f;
    if(modifier<0.0f)modifier=0.0f;
    return menu_theme_color(g_menu.accent,modifier);
}
static unsigned menu_style_accent_color(const VitaMenuTemplate *t,int active){
    float modifier=(active?0.82f:0.34f);
    float tint=menu_template_tint(t);
    unsigned accent, neutral;
    unsigned ar,ag,ab,nr,ng,nb;
    if(t) modifier*=menu_template_brightness(t);
    if(modifier>1.0f)modifier=1.0f;
    accent=menu_theme_color(g_menu.accent,modifier);
    if(tint>=0.999f) return accent;
    {
        int nr=150,ng=160,nb=175;                     /* CWMenu theme `neutral` */
        (void)cwm_theme_get_rgb("neutral",&nr,&ng,&nb);
        neutral=RGBA8((unsigned)((float)nr*modifier),(unsigned)((float)ng*modifier),(unsigned)((float)nb*modifier),255);
    }
    ar=(accent>>0)&0xff; ag=(accent>>8)&0xff; ab=(accent>>16)&0xff;
    nr=(neutral>>0)&0xff; ng=(neutral>>8)&0xff; nb=(neutral>>16)&0xff;
    return RGBA8((unsigned)(nr+(ar-nr)*tint),
                  (unsigned)(ng+(ag-ng)*tint),
                  (unsigned)(nb+(ab-nb)*tint),255);
}
static unsigned menu_style_text_color(const VitaMenuTemplate *t){
    /* CWMenu 2 theme slots text-normal/text-light/text-dim own these; the
     * C values are the built-in fallback. */
    int r,g,b;
    if(!t || t->style==VITA_MENU_STYLE_NORMAL){
        if(cwm_theme_get_rgb("text-normal",&r,&g,&b)==0) return RGBA8(r,g,b,255);
        return RGBA8(235,240,250,255);
    }
    if(t->style==VITA_MENU_STYLE_LIGHT){
        if(cwm_theme_get_rgb("text-light",&r,&g,&b)==0) return RGBA8(r,g,b,255);
        return RGBA8(255,248,238,255);
    }
    if(cwm_theme_get_rgb("text-dim",&r,&g,&b)==0) return RGBA8(r,g,b,255);
    return RGBA8(155,165,180,255);
}
static const char *menu_accent_name(int accent){
    /* label0..label5 in the CWMenu theme block override the C names. */
    static const char *names[]={"Blue","Red","Orange","Green","Cyan","Dark Purple"};
    int i=(accent<0||accent>5)?0:accent;
    const char *themed=cwm_theme_get_label(i);
    return themed?themed:names[i];
}

static void menu_rect(float x,float y,float w,float h,int active){
    vita2d_draw_rectangle(x,y,w,h,menu_accent_color(g_menu.accent,active));
    vita2d_draw_rectangle(x,y,w,2,menu_theme_color(g_menu.accent,0.92f));
    vita2d_draw_rectangle(x,y,2,h,menu_theme_color(g_menu.accent,0.92f));
}
static const char *dev_section_name(int section)
{
    switch (section) {
        case 0: return "Performance";
        case 1: return "Renderer";
        case 2: return "Input";
        case 3: return "Page";
        case 4: return "Overrides";
        case 5: return "Console";
        default: return "Web Platform";
    }
}

static float scroll_speed_multiplier(void){
    switch(g_menu.scroll_speed){
        case 0: return 0.55f;
        case 2: return 2.25f;
        default: return 1.0f;
    }
}
static float settings_scroll_max(void){
    const VitaMenuTemplate *tpl=settings_active_template();
    const VitaMenuTemplate *panel=menu_template_find(tpl,"content-panel");
    const VitaMenuTemplate *sep=menu_template_find(tpl,"content-separator");
    if(!panel || !sep) return 0.0f;
    const float list_top=sep->y+8.0f;
    const float viewport_bottom=panel->y+panel->h-8.0f;
    const VitaMenuTemplate *sb=menu_scrollbar_find(tpl,"controls");
    float content_bottom=list_top;
    if(sb && sb->scroll_items && *sb->scroll_items){
        content_bottom=menu_scrollbar_content_bottom(tpl,sb,content_bottom);
    }else{
        for(const VitaMenuTemplate *t=tpl;t&&t->name;++t){
            if(t->y<=list_top) continue;
            if(t->kind==VITA_MENU_DECORATION || t->kind==VITA_MENU_SCROLLBAR ||
               t->kind==VITA_MENU_SLIDER || !strcmp(t->name,"content-panel") ||
               !strcmp(t->name,"content-title") || !strcmp(t->name,"content-separator") ||
               !strcmp(t->name,"content-status") || !strcmp(t->name,"footer") ||
               !strcmp(t->name,"undo") || !strcmp(t->name,"save") || !strcmp(t->name,"exit")) continue;
            if(t->y+t->h>content_bottom) content_bottom=t->y+t->h;
        }
    }
    { float max=content_bottom-viewport_bottom; return max>0.0f?max:0.0f; }
}
static void clamp_settings_scroll(void){
    float m=settings_scroll_max();
    if(g_menu.settings_scroll<0.0f)g_menu.settings_scroll=0.0f;
    if(g_menu.settings_scroll>m)g_menu.settings_scroll=m;
}
static void settings_ensure_selected_visible(void){
    if(g_menu.settings_tab!=1)return;
    int row=g_menu.selected-3;
    if(row<0||row>4)return;
    const VitaMenuTemplate *tpl=settings_active_template();
    const VitaMenuTemplate *panel=menu_template_find(tpl,"content-panel");
    const VitaMenuTemplate *sep=menu_template_find(tpl,"content-separator");
    char name[32]; snprintf(name,sizeof(name),"content-row-%d",row);
    const VitaMenuTemplate *rt=menu_template_find(tpl,name);
    if(!panel||!sep||!rt)return;
    const float list_top=sep->y+8.0f;
    const float list_bottom=panel->y+panel->h-8.0f;
    float top=rt->y-g_menu.settings_scroll;
    float bottom=top+rt->h;
    if(top<list_top)g_menu.settings_scroll-=list_top-top;
    if(bottom>list_bottom)g_menu.settings_scroll+=bottom-list_bottom;
    clamp_settings_scroll();
}
static int effective_scroll_mode(void){ return g_menu.override_scroll>=0?g_menu.override_scroll:g_menu.scroll_mode; }
static int effective_zoom_mode(void){ return g_menu.override_zoom>=0?g_menu.override_zoom:g_menu.zoom_mode; }
static int effective_dark_mode(void){ return g_menu.override_theme>=0?g_menu.override_theme:g_menu.dark_mode; }
static int effective_control_mode(CwvHost *host){ int v=g_menu.override_vcm>=0?g_menu.override_vcm:cwv_host_control_mode(host); return v; }
/* DevTools override has five states: Default/page-defined, then the four
 * explicit VCM policies. Keeping the default as a real cycle value means
 * LEFT/RIGHT and touch can return to the page's value without requiring the
 * global "clear all overrides" action. */
static int cycle_control_override(int current, int direction){
    int state = current < 0 ? 0 : current + 1; /* 0=Default, 1..5=VCM 0..4 */
    state = (state + direction) % 6;
    if(state < 0) state += 6;
    return state == 0 ? -1 : state - 1;
}
static const char *control_override_name(int override){
    return override < 0 ? "Default (Page-defined)" : vcm_name(override);
}
static int cycle_button_wait_preset(int current,int direction){
    /* Presets are conveniences only; custom values are entered through the
     * Vita keyboard and are not restricted to this list. */
    int state=current<0?0:current+1;
    state=(state+direction)%5;
    if(state<0)state+=5;
    return state==0?-1:state-1;
}
static const char *button_wait_override_name(int override){
    if(override<0)return "Default (Page-defined)";
    if(g_menu.override_button_wait_spec[0])return g_menu.override_button_wait_spec;
    switch(override){
        case 0:return "release";
        case 1:return "none";
        case 2:return "1s";
        case 3:return "1.5s 0.5s";
        default:return "(custom)";
    }
}
static void button_wait_override_set(const char *spec){
    if(!spec || !*spec){
        g_menu.override_button_wait=-1;
        g_menu.override_button_wait_spec[0]=0;
        return;
    }
    snprintf(g_menu.override_button_wait_spec,sizeof(g_menu.override_button_wait_spec),"%s",spec);
    g_menu.override_button_wait=4;
}
static int button_wait_override_open_keyboard(void){
    if(g_menu.button_wait_ime_active)return 1;
    const char *current=g_menu.override_button_wait_spec;
    if(!current[0] && g_menu.override_button_wait>=0){
        switch(g_menu.override_button_wait){
            case 0:current="release";break;
            case 1:current="none";break;
            case 2:current="1s";break;
            case 3:current="1.5s 0.5s";break;
            default:current="";break;
        }
    }
    memset(g_menu.button_wait_ime_initial,0,sizeof(g_menu.button_wait_ime_initial));
    memset(g_menu.button_wait_ime_buffer,0,sizeof(g_menu.button_wait_ime_buffer));
    size_t n=strlen(current);
    if(n>=SCE_IME_DIALOG_MAX_TEXT_LENGTH)n=SCE_IME_DIALOG_MAX_TEXT_LENGTH-1;
    for(size_t i=0;i<n;i++)g_menu.button_wait_ime_initial[i]=(SceWChar16)(unsigned char)current[i];
    g_menu.button_wait_ime_initial[n]=0;
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.type=SCE_IME_TYPE_DEFAULT;
    param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title=(const SceWChar16 *)u"Vita Button Wait";
    param.maxTextLength=SCE_IME_DIALOG_MAX_TEXT_LENGTH-1;
    param.initialText=g_menu.button_wait_ime_initial;
    param.inputTextBuffer=g_menu.button_wait_ime_buffer;
    if(sceImeDialogInit(&param)<0)return 0;
    g_menu.button_wait_ime_active=1;
    return 1;
}
static void button_wait_override_update_keyboard(void){
    if(!g_menu.button_wait_ime_active)return;
    if(sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
    SceImeDialogResult result;
    memset(&result,0,sizeof(result));
    sceImeDialogGetResult(&result);
    if(result.button==SCE_IME_DIALOG_BUTTON_ENTER){
        char text[64]; size_t n=0;
        while(n<sizeof(text)-1 && g_menu.button_wait_ime_buffer[n]){
            SceWChar16 c=g_menu.button_wait_ime_buffer[n];
            text[n]=(char)(c<128?c:'?');
            n++;
        }
        text[n]=0;
        while(n>0 && isspace((unsigned char)text[n-1]))text[--n]=0;
        size_t first=0;
        while(text[first] && isspace((unsigned char)text[first]))first++;
        if(first)memmove(text,text+first,strlen(text+first)+1);
        button_wait_override_set(text);
    }
    sceImeDialogTerm();
    g_menu.button_wait_ime_active=0;
}
/* ---- Open URL (main-menu "open-url" row) -------------------------------
 * Opens the Vita IME keyboard and loads the entered URL.  The dialog is
 * PREFILLED with the current page's path RELATIVE TO web/
 * ("./nested/second.html"), and the input is resolved the SAME way -
 * relative to the web root, so editing the prefill never double-nests
 * a directory ("./nested/x.html" is ALWAYS app0/web/nested/x.html,
 * wherever you currently are):
 *   ./file.html / bare file.html  -> web root (app0/web/)
 *   ../file.html (repeatable)     -> one directory up, clamped at web/
 *   web/...                       -> app web root (file:///app0/web/...)
 *   app0/...                      -> app root; file:///... passes through
 * Remote http(s) URLs are rejected with a console error: this app is a
 * webapp wrapper, not a browser - scripts can use fetch() for network
 * data, and full browsing belongs to the future Vita WebView Online
 * project.  Every failure path only logs; nothing here can crash or
 * unload the current page. */
static int open_url_ime_start(CwvHost *host){
    SceImeDialogParam param;
    if(g_menu.open_url_ime_active)return 1;
    memset(g_menu.open_url_ime_initial,0,sizeof(g_menu.open_url_ime_initial));
    memset(g_menu.open_url_ime_buffer,0,sizeof(g_menu.open_url_ime_buffer));
    /* Prefill = the CURRENT page as a relative reference ("./file.html").
     * The user edits it in place: swap the name for a sibling page, add a
     * ../ to climb, etc. - no hidden prefix is appended afterwards. */
    {
        const char *cur=host?cwv_host_current_url(host):NULL;
        if(cur && strncmp(cur,"file:///",8)==0){
            /* Placeholder = the current page's path RELATIVE TO web/:
             * app0/web/nested/second.html prefills ./nested/second.html,
             * so typing a sibling or child path feels natural and "web/"
             * never has to be typed. */
            const char *base=cur+8;         /* "app0/web/.../file.html" */
            const char *q=strchr(base,'?');/* drop any query string */
            const char *web=strstr(base,"app0/web/");
            const char *rel=web?web+9:base;
            size_t rlen=q?(size_t)(q-rel):strlen(rel);
            if(*rel && rlen>0 && rlen<200){
                char init[224];
                snprintf(init,sizeof(init),"./%.*s",(int)rlen,rel);
                size_t n=strlen(init);
                for(size_t i=0;i<n;i++) g_menu.open_url_ime_initial[i]=(SceWChar16)(unsigned char)init[i];
            }
        }
    }
    sceImeDialogParamInit(&param);
    param.type=SCE_IME_TYPE_DEFAULT;
    param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title=(const SceWChar16 *)u"Open URL";
    param.maxTextLength=511;
    param.initialText=g_menu.open_url_ime_initial;
    param.inputTextBuffer=g_menu.open_url_ime_buffer;
    if(sceImeDialogInit(&param)<0){
        write_runtime_log("Open URL: IME dialog init failed");
        return 0;
    }
    g_menu.open_url_ime_active=1;
    return 1;
}

/* Try to load the entered text. Relative references resolve against the
 * WEB ROOT (see the comment above open_url_ime_start); remote URLs are
 * refused. */
static void open_url_navigate(CwvHost *host,const char *input){
    char url[600];
    int rc;
    if(!host||!input||!*input)return;
    if(strncmp(input,"http://",7)==0||strncmp(input,"https://",8)==0){
        write_runtime_log("Open URL: remote URL rejected (%.200s)",input);
        cwv_host_eval(host,
            "console.error('Open URL: remote browsing is not supported in this app. "
            "Use a local page like ./editor.html or ../index.html; scripts can call fetch().')");
        return;
    }
    if(strncmp(input,"file:///",8)==0){
        snprintf(url,sizeof(url),"%.512s",input);
    }else if(strncmp(input,"app0/",5)==0){
        /* already "app0/..." form */
        snprintf(url,sizeof(url),"file:///%.512s",input);
    }else if(strncmp(input,"web/",4)==0){
        /* web-root-relative convenience: "web/pages/x.html" */
        snprintf(url,sizeof(url),"file:///app0/%.512s",input);
    }else{
        /* Web-root-relative reference - the same convention the IME
         * prefill uses.  Base is ALWAYS app0/web/ (never the current
         * page's directory: the prefill is already relative to web/, so
         * resolving against the page dir would double the subpath -
         * "./nested/x.html" while ON /web/nested/page.html became
         * /web/nested/nested/x.html).  Each "../" climbs one directory
         * and clamps at the web root. */
        char out[600];
        size_t olen;
        const char *p=input;
        snprintf(out,sizeof(out),"file:///app0/web/");
        olen=strlen(out);
        while(*p){
            const char *seg=p;
            size_t sl;
            while(*p && *p!='/')p++;
            sl=(size_t)(p-seg);
            if(*p=='/')p++;            /* consume the separator */
            if(sl==1 && seg[0]=='.'){
                /* "./" - stay in the current directory */
            }else if(sl==2 && seg[0]=='.' && seg[1]=='.'){
                /* "../" - climb one segment, never above the web root.
                 * Two shapes matter: if `out` ends with a plain segment
                 * (".../web/a/b"), THAT segment is the climb target;
                 * if it ends with a slash (".../web/a/"), the directory
                 * before the slash is.  The "web" segment itself is
                 * never removable - the clamp holds the web root. */
                char *last=strrchr(out,'/');
                if(last){
                    if(last[1]==0){
                        /* ends with '/': drop the last DIRECTORY */
                        *last=0;
                        char *prev=strrchr(out,'/');
                        if(prev && strcmp(prev+1,"web")!=0) prev[1]=0;
                        else snprintf(out,sizeof(out),"file:///app0/web/");
                    }else if(strcmp(last+1,"web")==0){
                        /* would remove "web" itself - clamp */
                        snprintf(out,sizeof(out),"file:///app0/web/");
                    }else{
                        /* drop just the trailing segment */
                        *last=0;
                    }
                }
                olen=strlen(out);
            }else if(sl>0){
                /* a real path segment - append with a separating slash if
                 * the buffer does not already end in one */
                size_t room=sizeof(out)-olen-2;
                if(olen>0 && out[olen-1]!='/' && room>0){
                    out[olen++]='/'; out[olen]=0; room--;
                }
                if(sl<=room){
                    memcpy(out+olen,seg,sl);
                    olen+=sl; out[olen]=0;
                }
            }
            /* an empty segment (from a trailing slash) is skipped */
        }
        snprintf(url,sizeof(url),"%.590s",out);
    }
    rc=cwv_host_load(host,url);
    write_runtime_log("Open URL: '%.200s' -> rc=%d",url,rc);
    if(rc==0){
        g_menu.mode=MENU_CLOSED;   /* show the loaded page */
        g_menu.dirty=1;
    }else{
        cwv_host_eval(host,
            "console.error('Open URL: could not load that page; check the path.')");
    }
}

/* Per-frame poll while the IME dialog is up (mirrors button_wait). */
static void open_url_ime_update(CwvHost *host){
    char text[512];
    size_t n=0,first,last;
    SceImeDialogResult result;
    if(!g_menu.open_url_ime_active)return;
    if(sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
    memset(&result,0,sizeof(result));
    sceImeDialogGetResult(&result);
    g_menu.open_url_ime_active=0;
    sceImeDialogTerm();
    if(result.button!=SCE_IME_DIALOG_BUTTON_ENTER)return;  /* cancelled */
    while(n<sizeof(text)-1 && g_menu.open_url_ime_buffer[n]){
        SceWChar16 c=g_menu.open_url_ime_buffer[n];
        text[n]=(char)(c<128?(char)c:'?');
        n++;
    }
    text[n]=0;
    first=0;
    while(text[first] && isspace((unsigned char)text[first]))first++;
    last=strlen(text);
    while(last>first && isspace((unsigned char)text[last-1]))last--;
    text[last]=0;
    if(!text[first]){
        write_runtime_log("Open URL: empty input, nothing loaded");
        return;
    }
    open_url_navigate(host,text+first);
}

static float cycle_zoom_override(float current,int direction){
    static const float values[]={0.75f,1.00f,1.25f,1.50f,1.75f,2.00f};
    int idx=0;
    if(current>0.0f){
        idx=0;
        for(int i=0;i<6;i++) if(current>=values[i]-0.001f) idx=i;
        idx=(idx+direction+6)%6;
    }
    return values[idx];
}
/* DevTools console tab: a runtime log viewport (not template rows).
 * The draw pass counts the wrapped console rows into dev_console_rows and
 * the visible capacity into dev_console_visible each frame; the scroll
 * range, touch scrollbar and d-pad paging all read those. */
#define DEV_CONSOLE_ROW_H 22.0f
static int dev_console_rows=0;      /* wrapped rows in the current dump */
static int dev_console_visible=0;  /* rows that fit the viewport */
static int dev_console_last_lines=-1;

/* ESM chain self-test verdict: 1 = PASS, 0 = FAIL, -1 = not run.
 * The ES6/TS provider is the shared built-in transformer, the same one
 * every port uses (cwv_host_enable_es_builtin); the self-test proves
 * the chain end-to-end by evaluating an ES6 canary at boot, before
 * the first page can run.  The verdict is shown in DevTools >
 * Platform, the debug overlay and (on failure) the console. */
static int g_esm_selftest=-1;       /* final verdict the UI shows */
static int g_esm_enabled=0;          /* the built-in install succeeded */
static char g_esm_selftest_report[128]="";
static int g_esm_selftest_announced=0;

static float devtools_scroll_max(void){
    /* The console tab scrolls runtime log rows, not template elements, so
     * its range comes from the wrap pass instead of the template walk. */
    if(g_menu.mode==MENU_DEVTOOLS && g_menu.dev_section==5){
        float m=((float)dev_console_rows-(float)dev_console_visible)*DEV_CONSOLE_ROW_H;
        return m>0.0f?m:0.0f;
    }
    const VitaMenuTemplate *tpl=devtools_active_template();
    const VitaMenuTemplate *panel=menu_template_find(tpl,"content-panel");
    const VitaMenuTemplate *sep=menu_template_find(tpl,"content-separator");
    if(!panel)return 0.0f;
    const float list_top=sep?(sep->y+8.0f):(panel->y+48.0f);
    const float viewport_bottom=panel->y+panel->h-8.0f;
    float content_bottom=list_top;
    for(const VitaMenuTemplate *t=tpl; t && t->name; ++t){
        if(t->y<=list_top)continue;
        if(t->kind==VITA_MENU_DECORATION || t->kind==VITA_MENU_SLIDER ||
           !strcmp(t->name,"content-panel") || !strcmp(t->name,"content-title") ||
           !strcmp(t->name,"content-separator") || t->kind==VITA_MENU_SCROLLBAR ||
           !strcmp(t->name,"footer") || !strcmp(t->name,"footer-separator") ||
           !strcmp(t->name,"back")) continue;
        if(t->y+t->h>content_bottom)content_bottom=t->y+t->h;
    }
    {
        float m=content_bottom-viewport_bottom;
        return m>0.0f?m:0.0f;
    }
}
static void clamp_devtools_scroll(void){
    float m=devtools_scroll_max();
    if(g_menu.devtools_scroll<0.0f)g_menu.devtools_scroll=0.0f;
    if(g_menu.devtools_scroll>m)g_menu.devtools_scroll=m;
}
static void settings_select_content(int index){
    if(index<3)index=3;
    if(index>7)index=7;
    g_menu.selected=index;
    g_menu.settings_item=index-3;
}
static void settings_select_tab(int tab){
    if(tab<0)tab=2;
    if(tab>2)tab=0;
    g_menu.settings_tab=tab;
    g_menu.settings_item=0;
    g_menu.settings_scroll=0.0f;
    g_menu.settings_scroll_drag=0;
    g_menu.selected=3;
}
static void settings_set_status(int status){
    g_menu.settings_status=status;
}
static void settings_clear_cache(void){
    /* The native renderer currently caches remote @font-face resources.  The
       actual files live in the per-application namespace after the backend
       cache migration below.  Delete the known cache directory contents
       without touching saved preferences. */
    char dir[256];
    app_data_path(dir,sizeof(dir),"cache");
    SceUID d=sceIoDopen(dir);
    if(d>=0){
        SceIoDirent ent;
        memset(&ent,0,sizeof(ent));
        while(sceIoDread(d,&ent)>0){
            if(ent.d_name[0]=='.')continue;
            char path[600];
            snprintf(path,sizeof(path),"%.*s/%.*s",(int)(sizeof(dir)-1),dir,(int)(sizeof(path)-sizeof(dir)-2),ent.d_name);
            /* Vita SceIoDirent has no POSIX d_type.  The cache is flat;
             * attempt removal and ignore entries that cannot be removed. */
            (void)sceIoRemove(path);
        }
        sceIoDclose(d);
    }
    settings_set_status(1);
    write_runtime_log("settings: cache cleared for namespace=%s",app_data_dir());
}
static void settings_reset_preferences(CwvHost *host){
    settings_defaults();
    g_menu.dirty=1;
    if(host){
        (void)cwv_host_set_theme(host,g_menu.dark_mode);
        (void)cwv_host_set_zoom(host,g_menu.zoom_level);
        (void)cwv_host_set_scrollbars(host,1);
    }
    settings_set_status(2);
}


static void apply_runtime_overrides(CwvHost *host){
    if(!host)return;
    (void)cwv_host_set_theme(host,effective_dark_mode());
    (void)cwv_host_set_scrollbars(host,g_menu.override_scrollbars<0?1:g_menu.override_scrollbars);
    /* A physical zoom change is a live renderer state. Do not overwrite it
       every frame with the saved 1.0x setting. DevTools can still force a
       temporary zoom override. */
    if(g_menu.override_zoom_level>0.0f)
        (void)cwv_host_set_zoom(host,g_menu.override_zoom_level);
    const char *wait_spec="";
    if(g_menu.override_button_wait>=0) wait_spec=g_menu.override_button_wait_spec;
    (void)cwv_host_set_button_wait_override(host,wait_spec);
}


static int menu_favicon_path(const char *url, char *out, size_t cap){
    const char *p, *end;
    size_t len;
    if(!url||!out||cap==0)return -1;
    if(strncmp(url,"file:///app0/",13)!=0)return -1;
    p=url+7; /* /app0/... */
    if(strncmp(p,"/app0/",6)!=0)return -1;
    p+=6;
    end=strpbrk(p,"?#"); len=end?(size_t)(end-p):strlen(p);
    if(len+6>=cap)return -1;
    memcpy(out,"app0:/",6); memcpy(out+6,p,len); out[len+6]='\0';
    return 0;
}
static void menu_update_favicon(CwvHost *host){
    const char *url=cwv_host_favicon_url(host);
    if(!url||!url[0]){
        if(g_menu.page_favicon){vita2d_free_texture(g_menu.page_favicon);g_menu.page_favicon=NULL;}
        g_menu.favicon_source[0]='\0'; return;
    }
    if(!strcmp(g_menu.favicon_source,url))return;
    if(g_menu.page_favicon){vita2d_free_texture(g_menu.page_favicon);g_menu.page_favicon=NULL;}
    snprintf(g_menu.favicon_source,sizeof(g_menu.favicon_source),"%s",url);
    char path[1024]; SceIoStat st; SceUID fd; unsigned char *data; int n;
    if(menu_favicon_path(url,path,sizeof(path))<0)return;
    memset(&st,0,sizeof(st)); if(sceIoGetstat(path,&st)<0 || st.st_size<=0 || st.st_size>1024*1024)return;
    data=malloc((size_t)st.st_size); if(!data)return;
    fd=sceIoOpen(path,SCE_O_RDONLY,0); if(fd<0){free(data);return;}
    n=sceIoRead(fd,data,(int)st.st_size); sceIoClose(fd);
    if(n<=0){free(data);return;}
    const char *dot=strrchr(path,'.');
    if(dot && (!strcasecmp(dot,".jpg")||!strcasecmp(dot,".jpeg"))) g_menu.page_favicon=vita2d_load_JPEG_buffer(data,(unsigned long)n);
    else if(dot && !strcasecmp(dot,".bmp")) g_menu.page_favicon=vita2d_load_BMP_buffer(data);
    else if(dot && !strcasecmp(dot,".png")) g_menu.page_favicon=vita2d_load_PNG_buffer(data);
    free(data);
}
static void menu_draw_page_icon(float x,float y,float size){
    if(g_menu.page_favicon){
        vita2d_draw_texture_scale(g_menu.page_favicon,x,y,size/(float)vita2d_texture_get_width(g_menu.page_favicon),size/(float)vita2d_texture_get_height(g_menu.page_favicon));
    }
}
static void menu_separator(float x,float y,float w){
    vita2d_draw_rectangle(x,y,w,1.0f,menu_theme_color(g_menu.accent,0.72f));
}
static void menu_separator_style(const VitaMenuTemplate *t,float x,float y,float w){
    vita2d_draw_rectangle(x,y,w,1.0f,menu_style_theme_color(t,0.72f));
}
static void menu_row_icon(vita2d_pgf *pgf,float x,float y,int kind){
    const unsigned c=menu_accent_color(g_menu.accent,1);
    vita2d_draw_rectangle(x,y,28,28,menu_theme_color(g_menu.accent,0.22f));
    vita2d_draw_rectangle(x,y,28,2,c);
    vita2d_draw_rectangle(x,y,2,28,c);
    vita2d_draw_rectangle(x,y+26,28,2,c);
    vita2d_draw_rectangle(x+26,y,2,28,c);
    if(kind==0) vita2d_pgf_draw_text(pgf,x+7,y+21,RGBA8(255,255,255,255),0.72f,"W");
    else if(kind==1) vita2d_pgf_draw_text(pgf,x+8,y+21,RGBA8(255,255,255,255),0.72f,"R");
    else if(kind==2) vita2d_pgf_draw_text(pgf,x+4,y+21,RGBA8(255,255,255,255),0.48f,"URL");
    else vita2d_pgf_draw_text(pgf,x+8,y+21,RGBA8(255,255,255,255),0.72f,"S");
}
static void menu_main_row(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,float w,float h,int active,int icon,const char *label){
    if(active){
        vita2d_draw_rectangle(x,y,w,h,menu_style_theme_color(t,0.25f));
        vita2d_draw_rectangle(x,y,4,h,menu_style_accent_color(t,1));
    }
    menu_separator_style(t,x,y+h-1,w);
    menu_row_icon(pgf,x+12,y+7,icon);
    vita2d_pgf_draw_text(pgf,x+54,y+27,menu_style_text_color(t),0.80f,label);
    vita2d_pgf_draw_text(pgf,x+w-27,y+27,menu_style_accent_color(t,1),0.90f,">");
}
static int menu_parse_float_pair(const char **pp,float *a,float *b){
    char *end;
    const char *p=*pp;
    while(*p && (isspace((unsigned char)*p)||*p==','))p++;
    if(!*p)return 0;
    *a=(float)strtod(p,&end); if(end==p)return 0; p=end;
    while(*p && (isspace((unsigned char)*p)||*p==','))p++;
    *b=(float)strtod(p,&end); if(end==p)return 0; p=end;
    *pp=end; return 1;
}

/* Draw a polyline encoded as x,y,x,y,_,x,y,... . Underscore starts a new
 * segment, so disconnected decorative strokes can live in one template
 * element. Coordinates are relative to the element's x/y origin. */
static void menu_draw_line(const VitaMenuTemplate *t,float x,float y){
    if(!t||!t->label)return;
    const char *p=t->label;
    float px=0,py=0,nx=0,ny=0;
    int have=0;
    while(*p){
        while(*p && isspace((unsigned char)*p))p++;
        if(*p=='_'){ have=0; p++; continue; }
        if(!menu_parse_float_pair(&p,&nx,&ny))break;
        if(have) vita2d_draw_line(x+t->x+px,y+t->y+py,x+t->x+nx,y+t->y+ny,menu_style_accent_color(t,1));
        px=nx; py=ny; have=1;
        while(*p && isspace((unsigned char)*p))p++;
        if(*p==',')p++;
    }
}

static void menu_draw_decoration(const VitaMenuTemplate *t, float x, float y)
{
    if(!t)return;
    if(!strcmp(t->label,"accent")){
        vita2d_draw_rectangle(x+t->x,y+t->y,t->w,t->h,menu_style_accent_color(t,1));
    }else if(!strcmp(t->label,"marker") || !strcmp(t->label,"dot")){
        vita2d_draw_rectangle(x+t->x,y+t->y,t->w,t->h,menu_style_theme_color(t,0.72f));
    }
}

static void menu_draw_rounded_rect(float x,float y,float w,float h,float r,unsigned color){
    if(r<1.0f){ vita2d_draw_rectangle(x,y,w,h,color); return; }
    if(r>w*0.5f)r=w*0.5f;
    if(r>h*0.5f)r=h*0.5f;
    vita2d_draw_rectangle(x+r,y,w-2.0f*r,h,color);
    vita2d_draw_rectangle(x,y+r,w,h-2.0f*r,color);
    vita2d_draw_fill_circle(x+r,y+r,r,color);
    vita2d_draw_fill_circle(x+w-r,y+r,r,color);
    vita2d_draw_fill_circle(x+r,y+h-r,r,color);
    vita2d_draw_fill_circle(x+w-r,y+h-r,r,color);
}
static void menu_draw_shape(const VitaMenuTemplate *t,float x,float y){
    if(!t)return;
    const unsigned fill=menu_style_theme_color(t,0.24f);
    const unsigned edge=menu_style_accent_color(t,1);
    if(t->kind==VITA_MENU_SHAPE_RECTANGLE){
        vita2d_draw_rectangle(x+t->x,y+t->y,t->w,t->h,fill);
        vita2d_draw_rectangle(x+t->x,y+t->y,t->w,2.0f,edge);
        vita2d_draw_rectangle(x+t->x,y+t->y,2.0f,t->h,edge);
        vita2d_draw_rectangle(x+t->x,y+t->y+t->h-2.0f,t->w,2.0f,edge);
        vita2d_draw_rectangle(x+t->x+t->w-2.0f,y+t->y,2.0f,t->h,edge);
    }else if(t->kind==VITA_MENU_SHAPE_ROUNDED_RECTANGLE){
        const float radius=(t->w<t->h?t->w:t->h)*0.22f;
        menu_draw_rounded_rect(x+t->x,y+t->y,t->w,t->h,radius,fill);
        /* A compact rounded accent treatment: a full outer stroke is built
         * from a slightly larger filled shape followed by the inner surface. */
        menu_draw_rounded_rect(x+t->x,y+t->y,t->w,t->h,radius,edge);
        menu_draw_rounded_rect(x+t->x+2.0f,y+t->y+2.0f,t->w-4.0f,t->h-4.0f,(radius>2.0f?radius-2.0f:1.0f),fill);
    }else if(t->kind==VITA_MENU_SHAPE_CIRCLE){
        float r=(t->w<t->h?t->w:t->h)*0.5f;
        float cx=x+t->x+t->w*0.5f, cy=y+t->y+t->h*0.5f;
        vita2d_draw_fill_circle(cx,cy,r,edge);
        if(r>2.0f) vita2d_draw_fill_circle(cx,cy,r-2.0f,fill);
    }else if(t->kind==VITA_MENU_LINE){
        menu_draw_line(t,x,y);
    }
}

static void menu_draw_icon(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y){
    if(!t)return;
    const float bx=x+t->x, by=y+t->y;
    vita2d_draw_rectangle(bx,by,t->w,t->h,menu_style_theme_color(t,0.14f));
    vita2d_draw_rectangle(bx,by,t->w,2.0f,menu_style_accent_color(t,1));
    const char *text=t->label?t->label:"";
    /* Small symbolic icon vocabulary keeps templates portable without
     * requiring image assets. */
    if(!strcmp(text,"check")) text="✓";
    else if(!strcmp(text,"info")) text="i";
    else if(!strcmp(text,"warning")) text="!";
    else if(!strcmp(text,"gear")) text="*";
    else if(!strcmp(text,"arrow-left")) text="<";
    else if(!strcmp(text,"arrow-right")) text=">";
    float tw=(float)vita2d_pgf_text_width(pgf,0.70f,text);
    vita2d_pgf_draw_text(pgf,bx+(t->w-tw)*0.5f,by+t->h*0.66f,menu_style_text_color(t),0.70f,text);
}

static void menu_draw_progress(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,float value){
    if(!t)return;
    if(value<0)value=0;
    if(value>1)value=1;
    const float bx=x+t->x, by=y+t->y;
    vita2d_pgf_draw_text(pgf,bx+14,by+22,menu_style_text_color(t),0.66f,t->label?t->label:"Progress");
    const float sx=bx+t->w-300.0f, sy=by+(t->h-10.0f)*0.5f, sw=260.0f;
    vita2d_draw_rectangle(sx,sy,sw,10.0f,menu_style_theme_color(t,0.16f));
    vita2d_draw_rectangle(sx,sy,sw*value,10.0f,menu_style_accent_color(t,1));
}

static void menu_draw_value(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,const char *value){
    if(!t)return;
    const float bx=x+t->x, by=y+t->y;
    vita2d_pgf_draw_text(pgf,bx+14,by+22,menu_style_text_color(t),0.66f,t->label?t->label:"Value");
    const char *v=value&&*value?value:"";
    float tw=(float)vita2d_pgf_text_width(pgf,0.64f,v);
    vita2d_pgf_draw_text(pgf,bx+t->w-tw-14,by+22,menu_style_text_color(t),0.64f,v);
}

static void menu_draw_list(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,int selected){
    if(!t)return;
    char items[512]; snprintf(items,sizeof(items),"%s",t->label?t->label:"");
    char *parts[16]; int count=0;
    char *item=strtok(items,"|");
    while(item && count<16){ parts[count++]=item; item=strtok(NULL,"|"); }
    if(count<=0)return;
    const float bx=x+t->x, by=y+t->y;
    const float cell_w=t->w/(float)count;
    for(int i=0;i<count;i++){
        float cx=bx+cell_w*(float)i;
        if(i==selected) vita2d_draw_rectangle(cx,by,cell_w,t->h,menu_style_theme_color(t,0.22f));
        float tw=(float)vita2d_pgf_text_width(pgf,0.60f,parts[i]);
        float tx=cx+(cell_w-tw)*0.5f;
        if(tx<cx+6.0f)tx=cx+6.0f;
        vita2d_pgf_draw_text(pgf,tx,by+t->h*0.68f,menu_style_text_color(t),0.60f,parts[i]);
        if(i>0) vita2d_draw_rectangle(cx,by,1,t->h,menu_style_theme_color(t,0.45f));
    }
    menu_separator_style(t,bx,by+t->h-1,t->w);
}

static void menu_draw_badge(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,const char *value)
{
    if(!t)return;
    const char *text=value&&*value?value:t->label;
    if(!text)text="";
    float tw=(float)vita2d_pgf_text_width(pgf,0.54f,text);
    float bw=tw+24.0f;
    float bx=x+t->x+t->w-bw-12.0f, by=y+t->y+(t->h-24.0f)*0.5f;
    vita2d_draw_rectangle(bx,by,bw,24,menu_style_theme_color(t,0.22f));
    vita2d_draw_rectangle(bx,by,bw,2,menu_style_accent_color(t,1));
    vita2d_pgf_draw_text(pgf,bx+12,by+17,menu_style_text_color(t),0.54f,text);
}



static int menu_slider_current_value(const VitaMenuTemplate *t,int fallback){
    int stops=menu_template_stops(t);
    float v;
    if(!t || stops<2) return 0;
    if(t->action && !strcmp(t->action,"set_scroll_speed")) return g_menu.scroll_speed;
    if(t->action && !strcmp(t->action,"set_pitch_stops")){
        if(cwm_var_get_number("pitch_stops",&v)==0){
            int n=(int)(v+0.5f)-2;
            if(n<0)n=0;
            if(n>=stops)n=stops-1;
            return n;
        }
    }
    if(t->action && !strcmp(t->action,"set_menu_brightness")){
        if(cwm_var_get_number("menu_brightness",&v)==0){
            int n=(int)(((v-0.25f)/1.75f)*(float)(stops-1)+0.5f);
            if(n<0)n=0;
            if(n>=stops)n=stops-1;
            return n;
        }
    }
    if(t->action && !strcmp(t->action,"set_menu_tint")){
        if(cwm_var_get_number("menu_accent_tint",&v)==0){
            int n=(int)(v*(float)(stops-1)+0.5f);
            if(n<0)n=0;
            if(n>=stops)n=stops-1;
            return n;
        }
    }
    if(t->action && !strcmp(t->action,"pitch")){
        if(fallback<0)fallback=0;
        if(fallback>=stops)fallback=stops-1;
        return fallback;
    }
    if(fallback<0)fallback=0;
    if(fallback>=stops)fallback=stops-1;
    return fallback;
}

static void menu_draw_slider(vita2d_pgf *pgf,const VitaMenuTemplate *t,float x,float y,int value)
{
    if(!t)return;
    const float sx=x+t->x+190.0f, sy=y+t->y+27.0f, sw=t->w-220.0f;
    const unsigned track=menu_style_theme_color(t,0.28f), active=menu_style_accent_color(t,1);
    static const char *pitch_labels[] = {"Very Low","Low","Normal","High","Very High","Maximum"};
    static const char *speed_labels[] = {"Slow","Normal","Fast"};
    int stops=menu_template_stops(t);
    int v=menu_slider_current_value(t,value);
    float step=stops>1?sw/(float)(stops-1):0.0f;
    const char *label="Slider";
    char dynamic_label[48];
    if(t->action && !strcmp(t->action,"set_scroll_speed")){
        label=speed_labels[v<3?v:2];
    }else if(t->action && !strcmp(t->action,"set_zoom")){
        /* Zoom slider: 6 stops span 0.75x..2.00x (step 0.25). */
        float z=0.75f+(1.25f*(float)v/(float)(stops>1?stops-1:1));
        snprintf(dynamic_label,sizeof(dynamic_label),"%.2fx",z);
        label=dynamic_label;
    }else if(t->action && !strcmp(t->action,"set_menu_brightness")){
        float b=0.25f+(1.75f*(float)v/(float)(stops-1));
        snprintf(dynamic_label,sizeof(dynamic_label),"%.2fx",b);
        label=dynamic_label;
    }else if(t->action && !strcmp(t->action,"set_menu_tint")){
        static const char *tint_labels[] = {"Neutral","Subtle","Soft","Medium","Strong","Full"};
        label=tint_labels[v<6?v:5];
    }else if(t->action && !strcmp(t->action,"set_pitch_stops")){
        int count=v+2;
        snprintf(dynamic_label,sizeof(dynamic_label),"%d stops",count);
        label=dynamic_label;
    }else if(stops<=6){
        label=pitch_labels[v];
    }
    vita2d_pgf_draw_text(pgf,x+t->x+14,y+t->y+22,menu_style_text_color(t),0.66f,t->label?t->label:"Slider");
    vita2d_draw_rectangle(sx,sy-2.0f,sw,4.0f,track);
    for(int i=0;i<stops;i++){
        float px=sx+step*(float)i;
        vita2d_draw_rectangle(px-3.0f,sy-6.0f,6.0f,12.0f,i==v?active:track);
    }
    { float px=sx+step*(float)v; vita2d_draw_rectangle(px-7.0f,sy-7.0f,14.0f,14.0f,active); }
    { float tw=(float)vita2d_pgf_text_width(pgf,0.60f,label); vita2d_pgf_draw_text(pgf,sx+sw-tw,y+t->y+22,RGBA8(170,185,205,255),0.60f,label); }
}

static void menu_draw_scrollbar(const VitaMenuTemplate *t,float x,float y,
                                 float content_size,float viewport_size,float offset){
    if(!t || t->kind!=VITA_MENU_SCROLLBAR) return;
    CWMenuMenuScrollbar bar;
    cwm_scrollbar_init(&bar,24.0f);
    cwm_scrollbar_set(&bar,content_size,viewport_size,offset,t->h);
    if(!cwm_scrollbar_visible(&bar)) return;
    const unsigned track=menu_style_theme_color(t,0.16f);
    const unsigned thumb=menu_style_accent_color(t,1);
    float thumb_h=cwm_scrollbar_thumb_size(&bar);
    float thumb_y=y+t->y+cwm_scrollbar_thumb_position(&bar);
    vita2d_draw_rectangle(x+t->x,y+t->y,t->w,t->h,track);
    vita2d_draw_rectangle(x+t->x,thumb_y,t->w,thumb_h,thumb);
}

static void menu_draw_template_widgets(vita2d_pgf *pgf,const VitaMenuTemplate *tpl,float x,float y){
    if(!tpl)return;
    for(const VitaMenuTemplate *t=tpl;t&&t->name;++t){
        switch(t->kind){
            case VITA_MENU_DECORATION:
                menu_draw_decoration(t,x,y);
                break;
            case VITA_MENU_SHAPE_RECTANGLE:
            case VITA_MENU_SHAPE_ROUNDED_RECTANGLE:
            case VITA_MENU_SHAPE_CIRCLE:
            case VITA_MENU_LINE:
                menu_draw_decoration(t,x,y);
                menu_draw_shape(t,x,y);
                break;
            case VITA_MENU_ICON: menu_draw_icon(pgf,t,x,y); break;
            case VITA_MENU_PROGRESS: menu_draw_progress(pgf,t,x,y,0.65f); break;
            case VITA_MENU_VALUE: menu_draw_value(pgf,t,x,y,"Ready"); break;
            case VITA_MENU_LIST: menu_draw_list(pgf,t,x,y,0); break;
            default: break;
        }
    }
}

/* defined further down, next to console_wrap_len it builds on */
static int menu_draw_text_wrapped(vita2d_pgf *pgf,float x,float y,float w,
                                  float h,unsigned color,float scale,
                                  const char *text);

static void devtools_template_text(vita2d_pgf *pgf, const VitaMenuTemplate *tpl,
                                   const char *name, float fallback_x, float fallback_y,
                                   unsigned color, float scale, const char *text)
{
    const VitaMenuTemplate *t=menu_template_find(tpl,name);
    float tx=fallback_x, ty=fallback_y;
    if(t){
        const float ry=t->y-g_menu.devtools_scroll;
        /* Template coordinates are always relative to the native menu frame.
         * fallback_x already contains the frame origin, so recover that origin
         * from the title/content geometry rather than drawing at screen x=0. */
        const VitaMenuTemplate *window=menu_template_find(tpl,"window");
        const VitaMenuTemplate *title=menu_template_find(tpl,"content-title");
        /* fallback_x is the frame-relative title origin used by all DevTools
         * sections. Recover the actual frame origin from the template rather
         * than assuming the menu always starts at x=110. This keeps template
         * replacement and differently-sized menu shells aligned. */
        const float frame_x=title ? (fallback_x-title->x) : (window ? 110.0f+window->x : 110.0f);
        tx=frame_x+t->x+14.0f;
        ty=54.0f+ry+25.0f;
        if(t->border_flags){
            /* CWMenu `border` key: text elements are PLAIN by default (the
             * user asked for just text); only the sides the .cwm author
             * asked for are drawn.  Sides: top,right,bottom,left,all,
             * fill (soft panel background), none. */
            const float bx=frame_x+t->x;
            const float by=54.0f+ry;
            if(t->border_flags&16)
                vita2d_draw_rectangle(bx,by,t->w,t->h,
                                      menu_style_theme_color(t,0.12f));
            if(t->border_flags&1)
                vita2d_draw_rectangle(bx,by,t->w,1.0f,
                                      menu_style_theme_color(t,0.55f));
            if(t->border_flags&4)
                menu_separator_style(t,bx,by+t->h-1,t->w);
            if(t->border_flags&8)
                vita2d_draw_rectangle(bx,by,4.0f,t->h,
                                      menu_style_theme_color(t,0.55f));
            if(t->border_flags&2)
                vita2d_draw_rectangle(bx+t->w-1.0f,by,1.0f,t->h,
                                      menu_style_theme_color(t,0.55f));
        }
    }
    if(t && t->w>28.0f){
        /* wrap inside the element's own box: long live rows (URLs, error
           text) can never run past the window edge anymore */
        (void)menu_draw_text_wrapped(pgf,tx,ty,t->w-20.0f,t->h,color,scale,text);
    }else{
        vita2d_pgf_draw_text(pgf,tx,ty,color,scale,text);
    }
}

/* Dual windows (CWMenu 2 `pair` window key): render the partner menu's window
 * as a live second surface.  The partner's own window element carries its
 * anchor/size/corner/crop, so both windows are positioned independently -
 * this is the "window offsets" half of dual-window support.  One level deep:
 * a partner's own pair key is ignored, and the ACTIVE menu keeps all input,
 * so hit-testing is never ambiguous. */
/* Resolve the dual-window partner's on-screen frame.  Returns 0 and fills
   x/y/w/h when the active menu declares `pair <menu>` and that menu is
   loaded; -1 when there is no partner.  The touch path uses this to
   swallow taps that land on the (display-only) floating window, so a tap
   on the visible partner can no longer activate a hidden active-menu
   element underneath it. */
static int menu_pair_frame(const VitaMenuTemplate *active_tpl,float *ox,float *oy,float *ow,float *oh){
    const VitaMenuTemplate *win,*ptpl,*pwin;
    size_t pcnt=0;
    if(!active_tpl||!ox||!oy||!ow||!oh)return -1;
    win=menu_template_find(active_tpl,"window");
    if(!win||!win->win_pair||!win->win_pair[0])return -1;
    ptpl=vita_native_menu_find(win->win_pair,&pcnt);
    if(!ptpl||pcnt==0)return -1;
    pwin=menu_template_find(ptpl,"window");
    if(!pwin)return -1;
    native_menu_frame(ptpl,ox,oy,ow,oh);
    return 0;
}

static void menu_draw_pair_window(vita2d_pgf *pgf,const VitaMenuTemplate *active_tpl){
    const VitaMenuTemplate *win=menu_template_find(active_tpl,"window");
    const VitaMenuTemplate *ptpl=NULL;
    const VitaMenuTemplate *pwin,*phead;
    size_t pcnt=0;
    float px,py,pw,ph,ps;
    if(!win||!win->win_pair||!win->win_pair[0]) return;
    ptpl=vita_native_menu_find(win->win_pair,&pcnt);
    if(!ptpl||pcnt==0) return;
    pwin=menu_template_find(ptpl,"window");
    if(!pwin) return;
    native_menu_frame(ptpl,&px,&py,&pw,&ph);
    ps=(pwin->win_scale>0.0f)?pwin->win_scale:1.0f;
    if(ps<0.75f)ps=0.75f;
    if(ps>1.25f)ps=1.25f;
    if(pwin->win_corner==1)
        menu_draw_rounded_rect(px,py,pw,ph,12.0f*ps,menu_style_theme_color(pwin,0.24f));
    else
        vita2d_draw_rectangle(px,py,pw,ph,menu_style_theme_color(pwin,0.24f));
    phead=menu_template_find(ptpl,"header");
    if(phead){
        float hx=px+phead->x, hy=py+phead->y, hh=phead->h*ps;
        vita2d_draw_rectangle(hx,hy,phead->w,hh,menu_style_theme_color(phead,0.48f));
        if(phead->label)
            vita2d_pgf_draw_text(pgf,hx+18,hy+29.0f*ps,menu_style_text_color(phead),ps,phead->label);
    }
    /* Static content pass for the partner.  It mirrors the template-driven
     * tests branch so buttons/text/badges/sliders/shapes all show, but no
     * focus highlight and no input: the active menu owns interaction. */
    for(const VitaMenuTemplate *t=ptpl;t->name;t++){
        if(t->kind==VITA_MENU_WINDOW||t->kind==VITA_MENU_HEADER||t->kind==VITA_MENU_SCROLLBAR) continue;
        if(!strcmp(t->name,"content-title")||!strcmp(t->name,"content-separator")) continue;
        if(strstr(t->name,"-tab")) continue;
        if(!strcmp(t->name,"menu-back")) continue;
        switch(t->kind){
            case VITA_MENU_PANEL:
                vita2d_draw_rectangle(px+t->x,py+t->y,t->w,t->h,menu_style_theme_color(t,0.24f));
                break;
            case VITA_MENU_UNDERLINE:
                vita2d_draw_rectangle(px+t->x,py+t->y,t->w,1.0f,menu_style_accent_color(t,1));
                break;
            case VITA_MENU_BUTTON:
                vita2d_draw_rectangle(px+t->x,py+t->y,t->w,t->h,menu_style_theme_color(t,0.12f));
                vita2d_draw_rectangle(px+t->x,py+t->y,3,t->h,menu_style_accent_color(t,1));
                menu_separator_style(t,px+t->x,py+t->y+t->h-1,t->w);
                (void)menu_draw_text_wrapped(pgf,px+t->x+14,py+t->y+24,
                                             t->w-22.0f,t->h,
                                             menu_style_text_color(t),0.68f,
                                             t->label?t->label:"");
                break;
            case VITA_MENU_TEXT:
            case VITA_MENU_FOOTER:
                /* Wrapped passive labels use the same bounded text path as
                 * active menu rows, but never receive focus or input. */
                (void)menu_draw_text_wrapped(pgf,px+t->x+12,py+t->y+24,
                                             t->w-16.0f,t->h,
                                             menu_style_text_color(t),0.66f,
                                             t->label?t->label:"");
                break;
            case VITA_MENU_SLIDER:
                menu_draw_slider(pgf,t,px,py,0);
                break;
            case VITA_MENU_CHECKBOX:
                vita2d_draw_rectangle(px+t->x,py+t->y+7,20,20,menu_style_theme_color(t,0.22f));
                vita2d_draw_rectangle(px+t->x,py+t->y+7,20,2,menu_style_accent_color(t,1));
                vita2d_pgf_draw_text(pgf,px+t->x+28,py+t->y+25,menu_style_text_color(t),0.68f,t->label?t->label:"");
                break;
            case VITA_MENU_SEGMENTED:
            case VITA_MENU_LIST:
                menu_draw_list(pgf,t,px,py,-1);
                break;
            case VITA_MENU_BADGE:
                menu_draw_badge(pgf,t,px,py,t->label);
                break;
            case VITA_MENU_ICON:
                menu_draw_icon(pgf,t,px,py);
                break;
            case VITA_MENU_PROGRESS:
                menu_draw_progress(pgf,t,px,py,0.65f);
                break;
            case VITA_MENU_VALUE:
                menu_draw_value(pgf,t,px,py,"Ready");
                break;
            case VITA_MENU_DECORATION:
                menu_draw_decoration(t,px,py);
                break;
            case VITA_MENU_SHAPE_RECTANGLE: case VITA_MENU_SHAPE_ROUNDED_RECTANGLE: case VITA_MENU_SHAPE_CIRCLE: case VITA_MENU_LINE:
                menu_draw_shape(t,px,py);
                break;
            default: break;
        }
    }
}

/* Zoom now moves in CWMenu slider steps (Settings > Appearance): the row is a
 * real 6-stop slider, so the D-pad and touch both snap to the same stops
 * the slider renders (0.75x, 1.00x, 1.25x, 1.50x, 1.75x, 2.00x). */
/* Which .cwm actually loaded at boot (shown in DevTools > Platform so a
 * stale per-app data-dir override is obvious on hardware). */
static char g_cwm_load_source[64]="built-in C fallback";

static void settings_zoom_step(int dir,CwvHost *host)
{
    const VitaMenuTemplate *rt=menu_template_find(settings_active_template(),"content-row-2");
    int stops=menu_template_stops(rt);
    float span=1.25f/(float)(stops>1?stops-1:1);
    int v=(int)((g_menu.zoom_level-0.75f)/span+0.5f)+dir;
    if(v<0)v=0;
    if(v>=stops)v=stops-1;
    g_menu.zoom_level=0.75f+span*(float)v;
    (void)cwv_host_set_zoom(host,g_menu.zoom_level);
    g_menu.dirty=1;
}

/* Renders ONE active menu mode.  Every mode branch returns when done.
 * The public entry point below adds the CWMenu 2 dual-window partner AFTER the
 * whole active menu so the partner opens ABOVE it (user-facing fix: the
 * pair window used to render beneath the current menu). */
/* Longest prefix (1..len) of s that still fits maxw at the given pgf scale,
 * measured with a binary search over prefix lengths (few probes per row).
 * Used to word-wrap DevTools console lines into viewport-width segments. */
static int console_wrap_len(vita2d_pgf *pgf,const char *s,int len,float maxw,float scale){
    char probe[258];
    int lo=1,hi=len,best=1;
    if(len<=0)return 0;
    while(lo<=hi){
        int mid=(lo+hi)/2;
        if(mid>256)mid=256;
        memcpy(probe,s,(size_t)mid);
        probe[mid]=0;
        if((float)vita2d_pgf_text_width(pgf,scale,probe)<=maxw){best=mid;lo=mid+1;}
        else hi=mid-1;
    }
    return best;
}

/* Word-wrap `text` inside `w` pixels and draw it starting at (x,y).
 * The CWMenu text contract: a label NEVER runs past its element width - it
 * wraps at word boundaries (a single word wider than w breaks hard), and
 * when the wrapped lines would be taller than `h`, the last visible line
 * ends with "..." so the text also never leaves its element's height.
 * Lines advance 22*scale, the proven console row pitch.  Returns the
 * number of lines drawn. */
static int menu_draw_text_wrapped(vita2d_pgf *pgf,float x,float y,float w,float h,
                                  unsigned color,float scale,const char *text)
{
    const float lh=22.0f*scale;
    int maxlines,nrows=0;
    char line[258];
    const char *p=text;
    if(!pgf||!text)return 0;
    if(w<=8.0f){   /* no room to wrap: never hide text, draw it raw */
        vita2d_pgf_draw_text(pgf,x,y,color,scale,text);
        return 1;
    }
    maxlines=(int)((h>0.0f?h:lh)/lh);
    if(maxlines<1)maxlines=1;
    while(*p){
        int len,fit,cut,i;
        while(*p==' ')++p;           /* swallow the break we cut at */
        if(!*p)break;
        len=(int)strlen(p);
        fit=console_wrap_len(pgf,p,len,w,scale);
        if(fit<=0)break;
        /* prefer breaking at a word boundary: back up to the last space
           inside the fitting segment unless the word itself is too wide */
        cut=fit;
        if(p[fit] && p[fit]!=' '){
            for(i=fit;i>0;--i) if(p[i-1]==' '){cut=i-1;break;}
            if(cut<1)cut=fit;
        }
        if(cut>256)cut=256;
        memcpy(line,p,(size_t)cut);
        line[cut]=0;
        /* last allowed line but more text follows: end with an ellipsis */
        if(nrows==maxlines-1 && p[cut]){
            while(cut>1 && (float)vita2d_pgf_text_width(pgf,scale,line)+
                           (float)vita2d_pgf_text_width(pgf,scale,"...")>w)
                line[--cut]=0;
            strcat(line,"...");
            vita2d_pgf_draw_text(pgf,x,y+(float)nrows*lh,color,scale,line);
            return nrows+1;
        }
        vita2d_pgf_draw_text(pgf,x,y+(float)nrows*lh,color,scale,line);
        nrows++;
        p+=cut;
        if(nrows>=maxlines)break;
    }
    return nrows;
}


/* ==== (84) Files menu: web-content tree browser =========================
 * A native menu (MENU_FILES) listing everything packaged under the web
 * root (app0:web by default), drawn from the same template shell as the
 * main menu.  Entry: the "Files" button natively drawn to the LEFT of the
 * DevTools button (works with any CWMenu script that positions the
 * devtools element; a script-level `files` element takes precedence for
 * its rect). Directories open as navigable folders; `..` goes up but never
 * above the web root. CROSS/tap on a .html/.htm row opens it normally.
 * Feature key: package.json "vita":{"files":false} compiles it out. */
#if CWV_FILES_ENABLED
#define CWV_FILES_MAX 512
#define CWV_FILES_NAME 256
#define CWV_FILES_REL 320
#define CWV_FILES_ROW_H 38.0f
#define CWV_FILES_VIEW_H 318.0f

typedef struct {
    char name[CWV_FILES_NAME];
    char rel[CWV_FILES_REL];   /* relative to app0:, forward slashes */
    int depth;
    int is_dir;
    long size;
} CwvFilesEntry;

static CwvFilesEntry g_files[CWV_FILES_MAX];
static int g_files_count;
static long g_files_total_bytes;

/* The picker always opens at the packaged web root, independent of which
 * page is currently loaded. CWV_WEBPAGE is relative to web/ in normal builds;
 * it must not accidentally make the picker start in app0: or a nested page. */
static const char *files_root_rel(void){ return "web"; }

static void files_scan(void){
    char dpath[CWV_FILES_REL+16];
    typedef struct{char name[256];int is_dir;long size;} CwvFilesKid;
    CwvFilesKid kids[96];
    int nk=0;
    SceUID d;
    SceIoDirent ent;
    g_files_count=0;
    g_files_total_bytes=0;
    if(!g_menu.files_path[0])
        snprintf(g_menu.files_path,sizeof(g_menu.files_path),"%s",files_root_rel());
    if(snprintf(dpath,sizeof(dpath),"app0:/%s",g_menu.files_path)
        >=(int)sizeof(dpath)) return;
    d=sceIoDopen(dpath);
    if(d<0){
        write_runtime_log("files: cannot open directory %s (rc=%d)",dpath,(int)d);
        return;
    }
    memset(&ent,0,sizeof(ent));
    while(sceIoDread(d,&ent)>0){
        if(!strcmp(ent.d_name,".")||!strcmp(ent.d_name,".."))continue;
        if(nk>=(int)(sizeof(kids)/sizeof(kids[0])))break;
        { size_t name_len=strlen(ent.d_name);
          if(name_len>=sizeof(kids[nk].name))continue;
          memcpy(kids[nk].name,ent.d_name,name_len+1); }
        kids[nk].is_dir=((ent.d_stat.st_mode&0xF000)==0x4000);
        kids[nk].size=(long)ent.d_stat.st_size;
        nk++;
    }
    sceIoDclose(d);
    for(int i=1;i<nk;i++){
        CwvFilesKid k=kids[i]; int j=i-1;
        while(j>=0 && ((kids[j].is_dir!=k.is_dir)?(kids[j].is_dir&&!k.is_dir)
                                      :(strcasecmp(kids[j].name,k.name)>0))){
            kids[j+1]=kids[j]; j--;
        }
        kids[j+1]=k;
    }
    /* Synthetic parent row appears first, except at the web root. */
    if(strcmp(g_menu.files_path,files_root_rel())!=0){
        CwvFilesEntry *e=&g_files[g_files_count++];
        memset(e,0,sizeof(*e));
        snprintf(e->name,sizeof(e->name),"..");
        e->is_dir=2;
        const char *last=strrchr(g_menu.files_path,'/');
        if(last && (size_t)(last-g_menu.files_path)>=strlen(files_root_rel()))
            snprintf(e->rel,sizeof(e->rel),"%.*s",(int)(last-g_menu.files_path),g_menu.files_path);
        else snprintf(e->rel,sizeof(e->rel),"%s",files_root_rel());
    }
    for(int i=0;i<nk && g_files_count<CWV_FILES_MAX;i++){
        size_t base_len=strlen(g_menu.files_path);
        size_t name_len=strlen(kids[i].name);
        CwvFilesEntry *e;
        /* Never show a truncated path: skip an entry if its full relative
           path cannot fit in the bounded Files-browser path buffer. */
        if(base_len+1+name_len>=sizeof(g_files[0].rel))continue;
        e=&g_files[g_files_count++];
        memset(e,0,sizeof(*e));
        memcpy(e->name,kids[i].name,name_len+1);
        memcpy(e->rel,g_menu.files_path,base_len);
        e->rel[base_len]='/';
        memcpy(e->rel+base_len+1,kids[i].name,name_len+1);
        e->depth=0; e->is_dir=kids[i].is_dir; e->size=kids[i].size;
        if(!e->is_dir)g_files_total_bytes+=e->size;
    }
    if(g_menu.files_selected>=g_files_count)g_menu.files_selected=g_files_count>0?g_files_count-1:0;
    if(g_menu.files_selected<0)g_menu.files_selected=0;
    g_menu.files_scroll=0.0f;
    write_runtime_log("files: %d entries under app0:/%s",g_files_count,g_menu.files_path);
}

static float files_scroll_max(void){
    float m=(float)g_files_count*CWV_FILES_ROW_H-CWV_FILES_VIEW_H;
    return m>0.0f?m:0.0f;
}

static void files_scroll_to_selection(void){
    if(g_files_count<=0)return;
    if(g_menu.files_selected<0)g_menu.files_selected=0;
    if(g_menu.files_selected>=g_files_count)g_menu.files_selected=g_files_count-1;
    float top=(float)g_menu.files_selected*CWV_FILES_ROW_H;
    if(top<g_menu.files_scroll)g_menu.files_scroll=top;
    if(top+CWV_FILES_ROW_H>g_menu.files_scroll+CWV_FILES_VIEW_H)
        g_menu.files_scroll=top+CWV_FILES_ROW_H-CWV_FILES_VIEW_H;
    if(g_menu.files_scroll<0.0f)g_menu.files_scroll=0.0f;
    if(g_menu.files_scroll>files_scroll_max())g_menu.files_scroll=files_scroll_max();
}

/* CROSS on a row: .html/.htm files navigate via the Open URL path
 * (history entry + load + page view).  Everything else is inert. */
static void files_open_entry(CwvHost *host){
    if(g_menu.files_selected<0||g_menu.files_selected>=g_files_count)return;
    const CwvFilesEntry *e=&g_files[g_menu.files_selected];
    if(e->is_dir){
        snprintf(g_menu.files_path,sizeof(g_menu.files_path),"%s",e->rel);
        g_menu.files_selected=0;
        g_menu.files_scroll=0.0f;
        files_scan();
        return;
    }
    const char *dot=strrchr(e->name,'.');
    if(!dot||(strcasecmp(dot,".html")&&strcasecmp(dot,".htm")))return;
    char url[512];
    snprintf(url,sizeof(url),"file:///app0/%s",e->rel);
    open_url_navigate(host,url);
}

/* Button rect: a script-level `files` element wins; otherwise the native
 * fallback sits exactly one button-width + gap LEFT of DevTools (works
 * on legacy v3 scripts, per user report 2026-10-07). */
static void files_button_rect(float mx,float my,float *fx,float *fy,float *fw,float *fh){
    const VitaMenuTemplate *f=menu_template_find(vita_menu_main_template,"files");
    if(f){ *fx=mx+f->x;*fy=my+f->y;*fw=f->w;*fh=f->h; return; }
    const VitaMenuTemplate *d=menu_template_find(vita_menu_main_template,"devtools");
    if(d){ *fx=mx+d->x-(d->w+12.0f);*fy=my+d->y;*fw=d->w;*fh=d->h; return; }
    *fx=mx+486.0f;*fy=my+7.0f;*fw=112.0f;*fh=30.0f;
}

static void draw_files_menu(vita2d_pgf *pgf,float x,float y){
    float w=740.0f;
    /* header (over the shell's) */
    vita2d_draw_rectangle(x,y,w,44.0f,menu_theme_color(g_menu.accent,0.48f));
    vita2d_pgf_draw_text(pgf,x+18,y+29,RGBA8(255,255,255,255),1.0f,"Files");
    {
        char rootline[CWV_FILES_REL+16];
        snprintf(rootline,sizeof(rootline),"app0:/%s",g_menu.files_path[0]?g_menu.files_path:files_root_rel());
        float rw=(float)vita2d_pgf_text_width(pgf,0.62f,rootline);
        vita2d_pgf_draw_text(pgf,x+w-18-rw,y+28,RGBA8(190,200,215,255),0.62f,rootline);
    }
    /* content panel */
    vita2d_draw_rectangle(x+20,y+56,700,CWV_FILES_VIEW_H,menu_theme_color(g_menu.accent,0.25f));
    if(g_files_count<=0){
        vita2d_pgf_draw_text(pgf,x+40,y+106,RGBA8(190,200,215,255),0.72f,
            "No web content found.");
    }else{
        int first=(int)(g_menu.files_scroll/CWV_FILES_ROW_H);
        float frac=g_menu.files_scroll-(float)first*CWV_FILES_ROW_H;
        for(int vi=0;vi<9;vi++){
            int ri=first+vi;
            if(ri>=g_files_count)break;
            const CwvFilesEntry *e=&g_files[ri];
            float ry=y+56.0f+frac+(float)vi*CWV_FILES_ROW_H;
            if(ry<y+56.0f||ry+CWV_FILES_ROW_H>y+56.0f+CWV_FILES_VIEW_H)continue;
            if(ri==g_menu.files_selected){
                vita2d_draw_rectangle(x+20,ry,700,CWV_FILES_ROW_H,menu_theme_color(g_menu.accent,0.30f));
                if(e->is_dir){
                    /* Folder rows use an outset cue: bright top/right edges,
                       dark bottom/left edges, opposite the inset file focus. */
                    unsigned hi=RGBA8(220,235,255,210), lo=RGBA8(35,45,62,230);
                    vita2d_draw_rectangle(x+20,ry,700,2.0f,hi);
                    vita2d_draw_rectangle(x+718,ry,2.0f,CWV_FILES_ROW_H,hi);
                    vita2d_draw_rectangle(x+20,ry+CWV_FILES_ROW_H-2.0f,700,2.0f,lo);
                    vita2d_draw_rectangle(x+20,ry,2.0f,CWV_FILES_ROW_H,lo);
                }
            }
            float indent=14.0f*(float)e->depth;
            if(e->is_dir){
                char label[CWV_FILES_NAME+12];
                snprintf(label,sizeof(label),"%s %s%s",e->is_dir==2?"<":"[DIR]",e->name,e->is_dir==2?"":"/");
                vita2d_pgf_draw_text(pgf,x+40+indent,ry+26,RGBA8(235,240,250,255),0.70f,label);
            }else{
                vita2d_pgf_draw_text(pgf,x+40+indent,ry+26,RGBA8(210,216,228,255),0.70f,e->name);
                char sz[24];
                if(e->size>=1024) snprintf(sz,sizeof(sz),"%ld KB",e->size/1024);
                else              snprintf(sz,sizeof(sz),"%ld B",e->size);
                float sw=(float)vita2d_pgf_text_width(pgf,0.58f,sz);
                vita2d_pgf_draw_text(pgf,x+700-sw,ry+26,RGBA8(150,160,175,255),0.58f,sz);
            }
        }
        /* scrollbar */
        float max=files_scroll_max();
        if(max>0.0f){
            float track_h=CWV_FILES_VIEW_H;
            float thumb_h=track_h*(CWV_FILES_VIEW_H/((float)g_files_count*CWV_FILES_ROW_H));
            if(thumb_h<24.0f)thumb_h=24.0f;
            float thumb_y=y+56.0f+track_h*(g_menu.files_scroll/max);
            if(thumb_y+thumb_h>y+56.0f+track_h)thumb_y=y+56.0f+track_h-thumb_h;
            vita2d_draw_rectangle(x+713,y+56.0f,5,track_h,menu_theme_color(g_menu.accent,0.20f));
            vita2d_draw_rectangle(x+713,thumb_y,5,thumb_h,menu_theme_color(g_menu.accent,0.60f));
        }
    }
    /* footer */
    {
        const VitaMenuTemplate *sep=menu_template_find(vita_menu_main_template,"footer-separator");
        if(sep) menu_separator_style(sep,x+sep->x,y+sep->y,sep->w);
        char foot[192];
        snprintf(foot,sizeof(foot),"%d entries",g_files_count);
        vita2d_pgf_draw_text(pgf,x+180,y+417,RGBA8(245,245,250,255),0.66f,foot);
        vita2d_pgf_draw_text(pgf,x+300,y+417,RGBA8(245,245,250,255),0.66f,"X Open page");
        vita2d_pgf_draw_text(pgf,x+450,y+417,RGBA8(245,245,250,255),0.66f,"O Back");
        menu_rect(x+20,y+392,150,38,0);
        float bw=(float)vita2d_pgf_text_width(pgf,0.68f,"Back");
        vita2d_pgf_draw_text(pgf,x+20+(150-bw)*0.5f,y+392+25,RGBA8(255,255,255,255),0.68f,"Back");
    }
}

/* touch: tap selects; tap on the already-selected row opens; finger-drag
 * scrolls (rows misfire-proof: a drag only selects, never opens). */
static int handle_files_touch(int x,int y,CwvHost *host,float mx,float my){
    /* back button */
    if(x>=mx+20&&x<mx+170&&y>=my+392&&y<my+430){
        g_menu.mode=MENU_MAIN;g_menu.selected=0;return 1;
    }
    float lx=mx+20.0f,ly=my+56.0f;
    if(x>=lx&&x<lx+700&&y>=ly&&y<ly+CWV_FILES_VIEW_H){
        if(!g_menu.files_scroll_drag){
            /* start of a gesture: select the row under the finger and arm
             * the drag; a move turns it into a scroll. */
            g_menu.files_scroll_drag=1;
            g_menu.files_drag_anchor_y=y;
            g_menu.files_drag_anchor_scroll=g_menu.files_scroll;
            int ri=(int)((float)(y-ly+g_menu.files_scroll)/CWV_FILES_ROW_H);
            if(ri>=0&&ri<g_files_count){
                if(ri==g_menu.files_selected)files_open_entry(host);
                else{g_menu.files_selected=ri;files_scroll_to_selection();}
            }
        }else{
            g_menu.files_scroll=g_menu.files_drag_anchor_scroll-(float)(y-g_menu.files_drag_anchor_y);
            if(g_menu.files_scroll<0.0f)g_menu.files_scroll=0.0f;
            if(g_menu.files_scroll>files_scroll_max())g_menu.files_scroll=files_scroll_max();
        }
        return 1;
    }
    return 0;
}
#endif /* CWV_FILES_ENABLED */

static void draw_native_menu_content(vita2d_pgf *pgf,CwvHost *host, unsigned frame_count, float fps_value, float frame_ms)
{
    if(!pgf||g_menu.mode==MENU_CLOSED)return;

    /* The active template owns the native shell geometry and header. */
    const VitaMenuTemplate *active_tpl=native_menu_template();
    float x,y,w,h;
    native_menu_frame(active_tpl,&x,&y,&w,&h);
    {   /* CWMenu theme `overlay` owns the backdrop rgb; alpha stays fixed. */
        int orr=0,ogg=0,obb=0;
        (void)cwm_theme_get_rgb("overlay",&orr,&ogg,&obb);
        vita2d_draw_rectangle(0,0,SCREEN_W,SCREEN_H,RGBA8(orr,ogg,obb,120));
    }
    {   /* CWMenu 2 window keys: `corner` picks rounded vs sharp chrome and
         * `size` scales the chrome (header included). */
        const VitaMenuTemplate *shell_win=menu_template_find(active_tpl,"window");
        float shell_scale=(shell_win&&shell_win->win_scale>0.0f)?shell_win->win_scale:1.0f;
        if(shell_scale<0.75f)shell_scale=0.75f;
        if(shell_scale>1.25f)shell_scale=1.25f;
        if(shell_win && shell_win->win_corner==1)
            menu_draw_rounded_rect(x,y,w,h,12.0f*shell_scale,menu_style_theme_color(shell_win,0.24f));
        else
            vita2d_draw_rectangle(x,y,w,h,menu_style_theme_color(shell_win,0.24f));
        /* Dual window: the partner renders at the END of draw_native_menu()
         * so it floats ABOVE the active menu wherever they overlap (input
         * still belongs to the active menu only). */
        {
        const VitaMenuTemplate *header=menu_template_find(active_tpl,"header");
        float hx=x+(header?header->x:0.0f);
        float hy=y+(header?header->y:0.0f);
        float hw=header?header->w:w;
        float hh=(header?header->h:44.0f)*shell_scale;
        vita2d_draw_rectangle(hx,hy,hw,hh,menu_style_theme_color(header,0.48f));
        if(header && header->label)
            vita2d_pgf_draw_text(pgf,hx+18,hy+29.0f*shell_scale,menu_style_text_color(header),1.0f*shell_scale,header->label);
        }
    }

    if(g_menu.mode==MENU_MAIN){
        /* Concept-style browser menu: page identity at the top, four large
         * action rows, and a compact Vita navigation footer. */
        menu_update_favicon(host);
#if CWV_DEVTOOLS_ENABLED
        {
            const VitaMenuTemplate *t=menu_template_find(vita_menu_main_template,"devtools");
            if(t){
                float bx=x+t->x, by=y+t->y;
                vita2d_draw_rectangle(bx,by,t->w,t->h,menu_style_theme_color(t,0.34f));
                vita2d_draw_rectangle(bx,by,t->w,2,menu_style_theme_color(t,0.92f));
                vita2d_draw_rectangle(bx,by,2,t->h,menu_style_theme_color(t,0.92f));
                vita2d_pgf_draw_text(pgf,bx+12,by+21,menu_style_text_color(t),0.70f,t->label);
            }
        }
#endif /* CWV_DEVTOOLS_ENABLED */
#if CWV_FILES_ENABLED
        {   /* (84) Files button, one button-width LEFT of DevTools */
            float fx,fy,fw,fh;
            files_button_rect(x,y,&fx,&fy,&fw,&fh);
            menu_rect(fx,fy,fw,fh,0);
            float tw=(float)vita2d_pgf_text_width(pgf,0.70f,"Files");
            vita2d_pgf_draw_text(pgf,fx+(fw-tw)*0.5f,fy+21,RGBA8(255,255,255,255),0.70f,"Files");
        }
#endif /* CWV_FILES_ENABLED */

        vita2d_pgf_draw_text(pgf,x+20,y+82,RGBA8(190,210,235,255),0.76f,"Page");
        if(g_menu.page_favicon) menu_draw_page_icon(x+20,y+94,70.0f);
        else {
            vita2d_draw_rectangle(x+20,y+94,70,70,menu_theme_color(g_menu.accent,0.25f));
            vita2d_draw_rectangle(x+20,y+94,70,2,menu_accent_color(g_menu.accent,1));
        }
        const char *pt=host?cwv_host_page_title(host):"";
        const char *pu=host?cwv_host_current_url(host):"";
        vita2d_pgf_draw_text(pgf,x+104,y+117,RGBA8(245,245,250,255),0.84f,(pt&&*pt)?pt:"Untitled");
        vita2d_pgf_draw_text(pgf,x+104,y+141,RGBA8(185,195,210,255),0.66f,(pu&&*pu)?pu:"about:blank");
        char mode_line[64];
        snprintf(mode_line,sizeof(mode_line),"Control mode: %s",vcm_name(cwv_host_control_mode(host)));
        vita2d_pgf_draw_text(pgf,x+104,y+162,RGBA8(185,195,210,255),0.66f,mode_line);
        char version_line[128];
        snprintf(version_line,sizeof(version_line),"Version: %s",CWV_VERSION_DISPLAY);
        vita2d_pgf_draw_text(pgf,x+420,y+162,RGBA8(155,165,180,255),0.62f,version_line);
        /* (81) the app's own version (manifest "version") beside the
         * internal CWebView version, in bright white. */
        snprintf(version_line,sizeof(version_line),"  App: %s",CWV_APP_VERSION);
        vita2d_pgf_draw_text(pgf,x+420+vita2d_pgf_text_width(pgf,0.62f,
            "Version: " CWV_VERSION_DISPLAY),y+162,RGBA8(255,255,255,255),0.62f,version_line);

        {
            const VitaMenuTemplate *sep=menu_template_find(vita_menu_main_template,"page-separator");
            if(sep) menu_separator_style(sep,x+sep->x,y+sep->y,sep->w);
        }
        {
            static const char *names[3]={"page-operations","open-url","settings"};
            for(int i=0;i<3;i++){
                const VitaMenuTemplate *t=menu_template_find(vita_menu_main_template,names[i]);
                if(t) menu_main_row(pgf,t,x+t->x,y+t->y,t->w,t->h,g_menu.selected==i,i,t->label);
            }
        }

        {
            const VitaMenuTemplate *sep=menu_template_find(vita_menu_main_template,"footer-separator");
            const VitaMenuTemplate *back=menu_template_find(vita_menu_main_template,"back");
            const VitaMenuTemplate *footer=menu_template_find(vita_menu_main_template,"footer");
            if(sep) menu_separator_style(sep,x+sep->x,y+sep->y,sep->w);
            if(back){
                int active=(g_menu.selected==3);
                menu_rect(x+back->x,y+back->y,back->w,back->h,active);
                float bw=(float)vita2d_pgf_text_width(pgf,0.68f,back->label);
                vita2d_pgf_draw_text(pgf,x+back->x+(back->w-bw)*0.5f,y+back->y+25,RGBA8(255,255,255,255),0.68f,back->label);
            }
            if(footer){
                float fy=y+footer->y;
                vita2d_pgf_draw_text(pgf,x+footer->x+24,fy+25,RGBA8(245,245,250,255),0.66f,"X Select");
                vita2d_pgf_draw_text(pgf,x+footer->x+180,fy+25,RGBA8(245,245,250,255),0.66f,"O Close");
                vita2d_pgf_draw_text(pgf,x+footer->x+330,fy+25,RGBA8(245,245,250,255),0.66f,"Top-right DevTools");
            }
        }
        return;
    }

#if CWV_FILES_ENABLED
    if(g_menu.mode==MENU_FILES){
        draw_files_menu(pgf,x,y);
        return;
    }
#endif /* CWV_FILES_ENABLED */

    if(g_menu.mode==MENU_PAGEOPS){
        const VitaMenuTemplate *tpl=pageops_active_template();
        const VitaMenuTemplate *panel=menu_template_find(tpl,"content-panel");
        const VitaMenuTemplate *title_t=menu_template_find(tpl,"content-title");
        const VitaMenuTemplate *sep=menu_template_find(tpl,"content-separator");
        const float cx=x+(panel?panel->x:20.0f), cy=y+(panel?panel->y:86.0f);
        const float cw=panel?panel->w:700.0f, ch=panel?panel->h:300.0f;
        vita2d_draw_rectangle(cx,cy,cw,ch,menu_style_theme_color(panel,0.12f));
        vita2d_draw_rectangle(cx,cy,cw,2,menu_style_theme_color(panel,0.75f));
        vita2d_draw_rectangle(cx,cy,2,ch,menu_style_theme_color(panel,0.75f));
        if(title_t) vita2d_pgf_draw_text(pgf,x+title_t->x,y+title_t->y+20,menu_style_text_color(title_t),0.82f,title_t->label);
        if(sep) menu_separator_style(sep,x+sep->x,y+sep->y,sep->w);
        const char *names[]={"history-back","forward","reload"};
        for(int i=0;i<3;i++){
            const VitaMenuTemplate *rt=menu_template_find(tpl,names[i]);
            if(!rt) continue;
            if(vita_row_action_hidden(rt)) continue;  /* project-gated */
            float ry=y+rt->y;
            int active=g_menu.pageops_selected==i;
            if(active){
                vita2d_draw_rectangle(x+rt->x,ry,rt->w,rt->h,menu_style_theme_color(rt,0.23f));
                vita2d_draw_rectangle(x+rt->x,ry,4,rt->h,menu_style_accent_color(rt,1));
            }
            menu_separator_style(rt,x+rt->x,ry+rt->h-1,rt->w);
            vita2d_pgf_draw_text(pgf,x+rt->x+16,ry+25,menu_style_text_color(rt),0.72f,rt->label?rt->label:"");
        }
        {
            const VitaMenuTemplate *fs=menu_template_find(tpl,"footer-separator");
            if(fs) menu_separator_style(fs,x+fs->x,y+fs->y,fs->w);
        }
        {
            const VitaMenuTemplate *bt=menu_template_find(tpl,"menu-back");
            if(bt){
                int active=g_menu.pageops_selected==3;
                vita2d_draw_rectangle(x+bt->x,y+bt->y,bt->w,bt->h,menu_style_accent_color(bt,active));
                vita2d_draw_rectangle(x+bt->x,y+bt->y,bt->w,2,menu_style_theme_color(bt,0.92f));
                vita2d_draw_rectangle(x+bt->x,y+bt->y,2,bt->h,menu_style_theme_color(bt,0.92f));
                vita2d_pgf_draw_text(pgf,x+bt->x+16,y+bt->y+25,menu_style_text_color(bt),0.68f,bt->label?bt->label:"Back");
            }
        }
        {
            const VitaMenuTemplate *footer=menu_template_find(tpl,"footer");
            if(footer) vita2d_pgf_draw_text(pgf,x+footer->x,y+footer->y+16,RGBA8(145,155,170,255),0.60f,
                             "D-pad: select   CROSS: activate   CIRCLE: back");
        }
        return;
    }

    if(g_menu.mode==MENU_SETTINGS){
        /* Settings uses the same browser-style shell as the main menu:
         * tabbed categories, a clean content card, and persistent actions at
         * the bottom.  Save/Undo/Exit remain reachable independently of the
         * selected content item. */
        static const char *tab_names[3]={"appearance-tab","controls-tab","preferences-tab"};
        for(int ti=0;ti<3;ti++){
            const VitaMenuTemplate *t=menu_template_find(settings_active_template(),tab_names[ti]);
            if(t){
                float xx=x+t->x, yy=y+t->y;
                /* Settings tabs follow the DevTools grammar: plain labels,
                 * bright active tab, slim accent underline, no boxes. */
                float tab_w=(float)vita2d_pgf_text_width(pgf,0.72f,t->label);
                vita2d_pgf_draw_text(pgf,xx+(t->w-tab_w)*0.5f,yy+24,
                    g_menu.settings_tab==ti?RGBA8(255,255,255,255):RGBA8(150,160,175,255),
                    0.72f,t->label);
                if(g_menu.settings_tab==ti)
                    vita2d_draw_rectangle(xx+(t->w-tab_w)*0.5f,yy+t->h-3.0f,
                                          tab_w,2.0f,menu_style_accent_color(t,1));
            }
        }

        const VitaMenuTemplate *st_panel=menu_template_find(settings_active_template(),"content-panel");
        const VitaMenuTemplate *st_row0=menu_template_find(settings_active_template(),"content-row-0");
        const VitaMenuTemplate *st_status=menu_template_find(settings_active_template(),"content-status");
        const float cx=x+(st_panel?st_panel->x:20.0f), cy=y+(st_panel?st_panel->y:108.0f);
        const float cw=st_panel?st_panel->w:700.0f, ch=st_panel?st_panel->h:250.0f;
        vita2d_draw_rectangle(cx,cy,cw,ch,menu_style_theme_color(st_panel,0.12f));
        vita2d_draw_rectangle(cx,cy,cw,2,menu_style_theme_color(st_panel,0.75f));
        vita2d_draw_rectangle(cx,cy,2,ch,menu_style_theme_color(st_panel,0.75f));

        const float rowx=x+(st_row0?st_row0->x:38.0f);
        const float roww=st_row0?st_row0->w:664.0f, rowh=st_row0?st_row0->h:52.0f;
        const char *labels[5]={"","","","",""};
        char v0[128],v1[128],v2[128],v3[128],v4[128];

        if(g_menu.settings_tab==0){
            labels[0]="Theme"; snprintf(v0,sizeof(v0),"%s",g_menu.dark_mode?"Dark":"Light");
            labels[1]="Accent color"; snprintf(v1,sizeof(v1),"%s",menu_accent_name(g_menu.accent));
            labels[2]="Zoom level"; snprintf(v2,sizeof(v2),"%.2fx",g_menu.zoom_level);
            /* CSS cursors (2026-09-26 (45)): pages may restyle the pointer
             * (keyword pack + url() art); default on. */
            labels[3]="CSS cursors"; snprintf(v3,sizeof(v3),"%s",g_menu.css_cursors?"On":"Off");
        }else if(g_menu.settings_tab==1){
            labels[0]="Scroll input"; snprintf(v0,sizeof(v0),"%s",g_menu.scroll_mode==0?"R Analog":g_menu.scroll_mode==1?"L Analog":"D-pad");
            labels[1]="Zoom input"; snprintf(v1,sizeof(v1),"%s",g_menu.zoom_mode==0?"R Analog":g_menu.zoom_mode==1?"L Analog":"Off");
            labels[2]="Scroll speed"; snprintf(v2,sizeof(v2),"%s",g_menu.scroll_speed==0?"Slow":g_menu.scroll_speed==1?"Normal":"Fast");
            labels[3]="D-pad focus"; snprintf(v3,sizeof(v3),"%s","Browser-style");
            labels[4]="Touchpad pointer"; snprintf(v4,sizeof(v4),"%s",g_menu.pointer?"Back pad":"Off");
        }else{
            labels[0]="Data namespace"; snprintf(v0,sizeof(v0),"%s",app_data_dir());
            labels[1]="Clear cache"; snprintf(v1,sizeof(v1),"%s",g_menu.settings_status==1?"Cleared":"Font/resource cache");
            labels[2]="Reset preferences"; snprintf(v2,sizeof(v2),"%s",g_menu.settings_status==2?"Reset to defaults":"Restore defaults");
        }
#if !CWV_ZOOM_ENABLED
        /* project zoom:off - blank the zoom rows (draws as an empty slot,
         * matching the existing out-of-range rows of shorter tabs) */
        if(g_menu.settings_tab==0) labels[2]="";
        if(g_menu.settings_tab==1) labels[1]="";
#endif
#if !CWV_SCROLL_ENABLED
        if(g_menu.settings_tab==1){ labels[0]=""; labels[2]=""; }
#endif
#if !CWV_CURSOR_ENABLED
        if(g_menu.settings_tab==1) labels[4]="";
        if(g_menu.settings_tab==0) labels[3]="";
#endif

        for(int ri=0;ri<(g_menu.settings_tab==1?5:(g_menu.settings_tab==0?4:3));ri++){
            char row_name[32];
            snprintf(row_name,sizeof(row_name),"content-row-%d",ri);
            const VitaMenuTemplate *rt=menu_template_find(settings_active_template(),row_name);
            float yy=y+(rt?rt->y:(158.0f+ri*52.0f));
            if(g_menu.settings_tab==1) yy-=g_menu.settings_scroll;
            float rw=rt?rt->w:roww, rh=rt?rt->h:rowh;
            int active=(g_menu.selected==3+ri);
            if(active){
                vita2d_draw_rectangle(rowx,yy,rw,rh,menu_style_theme_color(rt,0.23f));
                vita2d_draw_rectangle(rowx,yy,4,rh,menu_style_accent_color(rt,1));
            }
            menu_separator(rowx,yy+rh-1,rw);
            if(rt && rt->kind==VITA_MENU_SLIDER && !vita_row_action_hidden(rt)){
                /* Any settings row whose CWMenu element is a real slider renders
                 * as one (CWMenu 2): the appearance tab's zoom row and the
                 * controls tab's scroll-speed row both come from default.cwm.
                 * The slider owns its label and value, so the generic row
                 * text is not drawn a second time. */
                int value=0;
                if(menu_action_is(rt,"set_zoom")){
                    float span=1.25f/(float)(menu_template_stops(rt)>1?menu_template_stops(rt)-1:1);
                    value=(int)((g_menu.zoom_level-0.75f)/span+0.5f);
                    if(value<0)value=0;
                    if(value>=menu_template_stops(rt))value=menu_template_stops(rt)-1;
                }else{
                    value=g_menu.scroll_speed;
                }
                menu_draw_slider(pgf,rt,x,y-g_menu.settings_scroll,value);
            } else {
                vita2d_pgf_draw_text(pgf,rowx+16,yy+23,RGBA8(190,210,235,255),0.68f,labels[ri]);
                vita2d_pgf_draw_text(pgf,rowx+300,yy+23,RGBA8(250,250,255,255),0.68f,ri==0?v0:ri==1?v1:ri==2?v2:ri==3?v3:v4);
            }
            if(g_menu.settings_tab==2 && ri>0)
                vita2d_pgf_draw_text(pgf,rowx+rw-25,yy+23,menu_accent_color(g_menu.accent,1),0.82f,">");
        }

        if(g_menu.settings_tab==1){
            const VitaMenuTemplate *sb=menu_scrollbar_find(settings_active_template(),"controls");
            const VitaMenuTemplate *panel=menu_template_find(settings_active_template(),"content-panel");
            const VitaMenuTemplate *sep=menu_template_find(settings_active_template(),"content-separator");
            if(sb && panel && sep){
                float viewport=panel->y+panel->h-8.0f-(sep->y+8.0f);
                menu_draw_scrollbar(sb,x,y,viewport+settings_scroll_max(),viewport,g_menu.settings_scroll);
            }
        }

        {
            const VitaMenuTemplate *ct=menu_template_find(settings_active_template(),"content-title");
            const VitaMenuTemplate *cs=menu_template_find(settings_active_template(),"content-separator");
            const VitaMenuTemplate *status=st_status;
            const char *tab_title=g_menu.settings_tab==0?"Appearance":g_menu.settings_tab==1?"Controls":"Preferences";
            if(ct) vita2d_pgf_draw_text(pgf,x+ct->x,y+ct->y+20,RGBA8(190,210,235,255),0.72f,tab_title);
            if(cs) menu_separator_style(cs,x+cs->x,y+cs->y,cs->w);
            if(status && g_menu.settings_tab!=1){
                const char *msg=g_menu.dirty?"Unsaved changes":"Changes are saved per application";
                vita2d_pgf_draw_text(pgf,x+status->x,y+status->y+15,
                    g_menu.dirty?RGBA8(235,190,120,255):RGBA8(145,155,170,255),0.60f,msg);
            }
        }

        /* Persistent actions, with the same footer separator as DevTools. */
        {
            const VitaMenuTemplate *fs=menu_template_find(settings_active_template(),"footer-separator");
            if(fs) menu_separator_style(fs,x+fs->x,y+fs->y,fs->w);
        }
        {
            static const char *action_names[3]={"undo","save","exit"};
            for(int ai=0;ai<3;ai++){
                const VitaMenuTemplate *t=menu_template_find(settings_active_template(),action_names[ai]);
                if(t){
                    float ax=x+t->x, ay=y+t->y, aw=t->w, ah=t->h;
                    menu_rect(ax,ay,aw,ah,g_menu.selected==ai);
                    float text_w=(float)vita2d_pgf_text_width(pgf,0.68f,t->label);
                    vita2d_pgf_draw_text(pgf,ax+(aw-text_w)/2.0f,ay+26,menu_style_text_color(t),0.68f,t->label);
                }
            }
        }

        vita2d_pgf_draw_text(pgf,x+20,y+425,RGBA8(145,155,170,255),0.60f,
                             "L/R: tab   D-pad: item   LEFT/RIGHT: change   CROSS: select");
        return;
    }

    /* DevTools: same visual language as the main menu, with compact tabs
     * across the top and a single content card underneath. */
    {
        {
            static const char *tab_names[8]={"performance","renderer","input","page","overrides","console","platform","close"};
            for(int i=0;i<8;i++){
                const VitaMenuTemplate *t=menu_template_find(devtools_active_template(),tab_names[i]);
                if(t){
                    /* Plain-text tabs (user feedback): no boxes.  The active
                     * tab is bright with an accent underline; inactive tabs
                     * are muted. */
                    float xx=x+t->x, yy=y+t->y;
                    int active=(i==7)?(g_menu.selected==7):(g_menu.dev_section==i && g_menu.selected==i);
                    float tab_w=(float)vita2d_pgf_text_width(pgf,0.72f,t->label);
                    vita2d_pgf_draw_text(pgf,xx+(t->w-tab_w)/2.0f,yy+24,
                        active?RGBA8(255,255,255,255):RGBA8(150,160,175,255),0.72f,t->label);
                    if(active) vita2d_draw_rectangle(xx+(t->w-tab_w)/2.0f,yy+t->h-3.0f,tab_w,2.0f,
                        menu_style_accent_color(t,1));
                }
            }
        }

        const VitaMenuTemplate *dt_panel=menu_template_find(devtools_active_template(),"content-panel");
        const VitaMenuTemplate *dt_title=menu_template_find(devtools_active_template(),"content-title");
        const VitaMenuTemplate *dt_sep=menu_template_find(devtools_active_template(),"content-separator");
        const VitaMenuTemplate *dt_row0=menu_template_find(devtools_active_template(),"content-row-0");
        const VitaMenuTemplate *dt_track=menu_scrollbar_find(devtools_active_template(),"content");
        const float cx=x+(dt_panel?dt_panel->x:20.0f), cy=y+(dt_panel?dt_panel->y:106.0f);
        const float cw=dt_panel?dt_panel->w:700.0f, ch=dt_panel?dt_panel->h:270.0f;
        vita2d_draw_rectangle(cx,cy,cw,ch,menu_style_theme_color(dt_panel,0.12f));
        vita2d_draw_rectangle(cx,cy,cw,2,menu_style_theme_color(dt_panel,0.75f));
        vita2d_draw_rectangle(cx,cy,2,ch,menu_style_theme_color(dt_panel,0.75f));

        float rx=x+(dt_title?dt_title->x:40.0f);
        char line[256];
        vita2d_pgf_draw_text(pgf,rx,(dt_title?y+dt_title->y+20:cy+30),RGBA8(190,210,235,255),0.82f,
                             g_menu.selected==7 ? "Close" : dev_section_name(g_menu.dev_section));
        if(dt_sep) menu_separator_style(dt_sep,x+dt_sep->x,y+dt_sep->y,dt_sep->w);
        else menu_separator(rx,cy+42,cw-40);
        {
            const VitaMenuTemplate *tpl=devtools_active_template();
            menu_draw_template_widgets(pgf,tpl,x,y);
        }

        if(g_menu.selected==7){
            const char *app_name=host?cwv_host_page_title(host):"CWebView";
            if(!app_name || !*app_name) app_name="CWebView";
            const VitaMenuTemplate *cm=menu_template_find(devtools_active_template(),"close-message");
            const VitaMenuTemplate *cn=menu_template_find(devtools_active_template(),"close-no");
            const VitaMenuTemplate *cyes=menu_template_find(devtools_active_template(),"close-yes");
            float msg_x=x+(cm?cm->x:40.0f), msg_y=y+(cm?cm->y:198.0f);
            vita2d_pgf_draw_text(pgf,msg_x,msg_y+20,RGBA8(245,245,250,255),0.82f,
                                 "Are you sure you want to exit");
            char confirm[256];
            snprintf(confirm,sizeof(confirm),"%s?",app_name);
            vita2d_pgf_draw_text(pgf,msg_x,msg_y+52,RGBA8(245,245,250,255),0.82f,confirm);
            if(cn){
                menu_rect(x+cn->x,y+cn->y,cn->w,cn->h,g_menu.exit_confirm_selected==0);
                float nw=(float)vita2d_pgf_text_width(pgf,0.74f,cn->label);
                vita2d_pgf_draw_text(pgf,x+cn->x+(cn->w-nw)/2.0f,y+cn->y+29,RGBA8(255,255,255,255),0.74f,cn->label);
            }
            if(cyes){
                menu_rect(x+cyes->x,y+cyes->y,cyes->w,cyes->h,g_menu.exit_confirm_selected==1);
                float yw=(float)vita2d_pgf_text_width(pgf,0.74f,cyes->label);
                vita2d_pgf_draw_text(pgf,x+cyes->x+(cyes->w-yw)/2.0f,y+cyes->y+29,RGBA8(255,255,255,255),0.74f,cyes->label);
            }
        }else if(g_menu.dev_section==0){
            const VitaMenuTemplate *tpl=devtools_active_template();
            snprintf(line,sizeof(line),"FPS: %.1f",fps_value);
            devtools_template_text(pgf,tpl,"fps",rx,cy+76,RGBA8(235,240,250,255),0.72f,line);
            snprintf(line,sizeof(line),"Frames: %u    Frame time: %.2f ms",frame_count,frame_ms);
            devtools_template_text(pgf,tpl,"frames",rx,cy+108,RGBA8(220,230,240,255),0.68f,line);
            snprintf(line,sizeof(line),"Scroll: %.1f    Zoom: %.2fx",cwv_host_scroll_offset(host),cwv_host_zoom(host));
            devtools_template_text(pgf,tpl,"scroll-zoom",rx,cy+140,RGBA8(220,230,240,255),0.68f,line);
            devtools_template_text(pgf,tpl,"diagnostics",rx,cy+184,RGBA8(150,165,185,255),0.64f,
                                    "Lightweight diagnostics; no polling thread.");
        }else if(g_menu.dev_section==1){
            /* Live renderer stats (user feedback: every tab should show
             * real data like Performance/Console, not static labels). */
            const VitaMenuTemplate *tpl=devtools_active_template();
            const char *js_err=cwv_host_js_last_error(host);
            snprintf(line,sizeof(line),"Engine: native renderer + Duktape ES5.1");
            devtools_template_text(pgf,tpl,"engine",rx,cy+76,RGBA8(235,240,250,255),0.70f,line);
            snprintf(line,sizeof(line),"Viewport: %dx%d    Zoom: %.2fx",SCREEN_W,SCREEN_H,cwv_host_zoom(host));
            devtools_template_text(pgf,tpl,"viewport",rx,cy+108,RGBA8(220,230,240,255),0.68f,line);
            snprintf(line,sizeof(line),"DOM nodes: %d",cwv_host_node_count(host));
            devtools_template_text(pgf,tpl,"dom-nodes",rx,cy+140,RGBA8(220,230,240,255),0.68f,line);
            snprintf(line,sizeof(line),"JS: %s",cwv_host_js_active(host)?"active":"inactive");
            devtools_template_text(pgf,tpl,"js-state",rx,cy+172,RGBA8(220,230,240,255),0.68f,line);
            if(js_err&&js_err[0]){
                char clean[96]; int wi=0;
                for(int si=0; js_err[si] && wi<90; si++)
                    clean[wi++]= (js_err[si]=='\n')?' ':(js_err[si]=='\r')?' ':js_err[si];
                clean[wi]=0;
                snprintf(line,sizeof(line),"Last JS error: %.90s",clean);
            }else snprintf(line,sizeof(line),"Last JS error: (none)");
            devtools_template_text(pgf,tpl,"last-error",rx,cy+204,RGBA8(150,165,185,255),0.64f,line);
        }else if(g_menu.dev_section==2){
            const VitaMenuTemplate *tpl=devtools_active_template();
            snprintf(line,sizeof(line),"Control mode: %s",vcm_name(effective_control_mode(host)));
            devtools_template_text(pgf,tpl,"control-mode",rx,cy+76,RGBA8(235,240,250,255),0.70f,line);
            snprintf(line,sizeof(line),"Scroll: %s    Zoom: %s",
                     effective_scroll_mode()==0?"R Analog":effective_scroll_mode()==1?"L Analog":"D-pad",
                     effective_zoom_mode()==0?"R Analog":effective_zoom_mode()==1?"L Analog":"Off");
            devtools_template_text(pgf,tpl,"scroll-zoom",rx,cy+108,RGBA8(220,230,240,255),0.68f,line);
            snprintf(line,sizeof(line),"Last button: %s",g_menu.dbg_lastbtn[0]?g_menu.dbg_lastbtn:"-");
            devtools_template_text(pgf,tpl,"last-btn",rx,cy+150,RGBA8(220,230,240,255),0.68f,line);
            {
                char finfo[80];
                cwv_host_focus_info(host,finfo,sizeof(finfo));
                snprintf(line,sizeof(line),"D-pad focus: %s",finfo);
            }
            devtools_template_text(pgf,tpl,"dpad-focus",rx,cy+182,RGBA8(220,230,240,255),0.68f,line);
        }else if(g_menu.dev_section==3){
            const VitaMenuTemplate *tpl=devtools_active_template();
            snprintf(line,sizeof(line),"Title: %.110s",cwv_host_page_title(host));
            devtools_template_text(pgf,tpl,"title",rx,cy+76,RGBA8(235,240,250,255),0.68f,line);
            snprintf(line,sizeof(line),"URL: %.110s",cwv_host_current_url(host));
            devtools_template_text(pgf,tpl,"url",rx,cy+108,RGBA8(220,230,240,255),0.64f,line);
            snprintf(line,sizeof(line),"Version: %s",CWV_VERSION_DISPLAY);
            devtools_template_text(pgf,tpl,"version",rx,cy+140,RGBA8(220,230,240,255),0.68f,line);
            /* (81) the app's own version, bright white, beside the internal one. */
            snprintf(line,sizeof(line),"  App: %s",CWV_APP_VERSION);
            devtools_template_text(pgf,tpl,"version",rx+vita2d_pgf_text_width(pgf,0.68f,
                "Version: " CWV_VERSION_DISPLAY),cy+140,RGBA8(255,255,255,255),0.68f,line);
            {
                int cerrors=0,cwarns=0,clines=0;
                cwv_host_console_stats(host,&cerrors,&cwarns,&clines);
                snprintf(line,sizeof(line),"Console: %d lines (%d errors, %d warns)",clines,cerrors,cwarns);
            }
            devtools_template_text(pgf,tpl,"console-stats",rx,cy+172,RGBA8(220,230,240,255),0.68f,line);
            snprintf(line,sizeof(line),"JS: %s",cwv_host_js_active(host)?"active":"inactive");
            devtools_template_text(pgf,tpl,"js-state",rx,cy+204,RGBA8(150,165,185,255),0.64f,line);
        }else if(g_menu.dev_section==4){
            const char *vcm=control_override_name(g_menu.override_vcm);
            const char *sc=effective_scroll_mode()==0?"R Analog":effective_scroll_mode()==1?"L Analog":"D-pad";
            const char *zm=effective_zoom_mode()==0?"R Analog":effective_zoom_mode()==1?"L Analog":"Off";
            const char *rows[7];
            char ov0[96],ov1[96],ov2[96],ov3[96],ov4[96],ov5[96],ov6[96];
            snprintf(ov0,sizeof(ov0),"Control mode: %s",vcm);
            snprintf(ov1,sizeof(ov1),"Scroll input: %s",sc);
            snprintf(ov2,sizeof(ov2),"Zoom input: %s",zm);
            snprintf(ov3,sizeof(ov3),"Theme: %s",effective_dark_mode()?"Dark":"Light");
            snprintf(ov4,sizeof(ov4),"Scrollbars: %s",g_menu.override_scrollbars<0?"Auto":(g_menu.override_scrollbars?"Shown":"Hidden"));
            if(g_menu.override_zoom_level>0.0f) snprintf(ov5,sizeof(ov5),"Zoom level: %.2fx",g_menu.override_zoom_level);
            else snprintf(ov5,sizeof(ov5),"Zoom level: Default (%.2fx)",g_menu.zoom_level);
            snprintf(ov6,sizeof(ov6),"Button wait: %s",button_wait_override_name(g_menu.override_button_wait));
            rows[0]=ov0;rows[1]=ov1;rows[2]=ov2;rows[3]=ov3;rows[4]=ov4;rows[5]=ov5;rows[6]=ov6;
            /* Keep the heading fixed and give the list exactly five complete
             * rows of visible space.  Seven override rows therefore scroll
             * in two-row increments without ever painting over the heading. */
            const float list_top=y+(dt_row0?dt_row0->y:154.0f);
            const float list_bottom=cy+ch-8.0f;
            const float row_h=dt_row0?dt_row0->h:38.0f;
            const float row_step=42.0f;
            const float row_w=dt_row0?dt_row0->w:660.0f;
            for(int oi=0;oi<7;oi++){
                char row_name[32];
                snprintf(row_name,sizeof(row_name),"content-row-%d",oi);
                const VitaMenuTemplate *rt=menu_template_find(devtools_active_template(),row_name);
                float yy=(y+(rt?rt->y:(154.0f+oi*row_step)))-g_menu.devtools_scroll;
                float rw=rt?rt->w:row_w, rh=rt?rt->h:row_h;
                if(yy<list_top || yy+rh>list_bottom) continue;
                if(g_menu.dev_option==oi){
                    vita2d_draw_rectangle(x+(rt?rt->x:40.0f),yy,rw,rh,menu_theme_color(g_menu.accent,0.22f));
                    vita2d_draw_rectangle(x+(rt?rt->x:40.0f),yy,4,rh,menu_accent_color(g_menu.accent,1));
                }
                vita2d_pgf_draw_text(pgf,x+(rt?rt->x:40.0f)+14,yy+25,
                    g_menu.dev_option==oi?RGBA8(255,255,255,255):RGBA8(180,195,215,255),
                    0.70f,rows[oi]);
            }
            if(dt_track){
                const float track_h=list_bottom-list_top;
                menu_draw_scrollbar(dt_track,x,y,
                                    track_h+devtools_scroll_max(),track_h,
                                    g_menu.devtools_scroll);
            }
        }else if(g_menu.dev_section==5){
            /* A real console: the whole ring buffer flows into a scrolling,
             * color-coded log viewport below the separator - no card rows.
             * Long lines word-wrap by measuring prefixes of the line; the
             * scrollbar appears only when the wrapped rows overflow. */
            static char cbuf[16640];   /* 64 lines x (256 + newline) + slack */
            static struct { const char *s; int len; unsigned color; int reps; } crow[512];
            const float ctp=cy+52.0f, cbt=cy+ch-10.0f;
            const float cxs=cx+14.0f, ctw=cw-72.0f;   /* right strip = badge */
            const float cscale=0.60f;
            static int nrows=0;          /* wrapped rows - cached, see below */
            static int lines=0;          /* console line count - cached */
            static int last_errs=-1, last_warns=-1;
            int i;
            /* Console cache: the dump (~16 KB copy of the whole ring)
               plus parsing and word-wrapping every row used to run EVERY
               frame while this tab was open - by far the most expensive
               per-frame work in DevTools.  The result only depends on the
               ring content, so it re-runs only when a new line lands. */
            {
                int errs=0, warns=0, stat=0;
                cwv_host_console_stats(host,&errs,&warns,&stat);
                if (stat!=lines || errs!=last_errs || warns!=last_warns) {
                    last_errs=errs; last_warns=warns;
                    lines=cwv_host_console(host,cbuf,sizeof(cbuf));
                    nrows=0;
                    const char *p=cbuf;
                    const char *nl;
            /* level colors (console lines start "level: message") */
            #define CLEVEL(LV,COL) \
                if(len>=(int)sizeof(LV)-1 && !strncmp(seg0,LV,sizeof(LV)-1) && \
                    seg0[sizeof(LV)-1]==':') color=COL;
            while((nl=strchr(p,'\n'))!=NULL && nrows<512-8){
                int len=(int)(nl-p);
                int reps=1, first_row;
                unsigned color=RGBA8(255,255,255,255);   /* log: white */
                const char *seg0=p;
                if(len>256)len=256;      /* ring lines are capped at 256 */
                CLEVEL("error",RGBA8(255,80,80,255))    /* red */
                else CLEVEL("warn", RGBA8(255,215,0,255))   /* gold/yellow */
                else CLEVEL("hint", RGBA8(150,220,255,255))
                else CLEVEL("info", RGBA8(200,220,255,255))  /* blue-white */
                /* Hide the level prefix itself ("log: ", "info: ", ...):
                 * the row color already tells the user the severity, and
                 * console.log("hi") should read as just "hi", like a real
                 * browser's console. Storage keeps the prefix (repeat
                 * detection + error-row lookup rely on it) - display only. */
                {
                    static const char *lvl_names[]={"log","info","warn","error","hint"};
                    int li;
                    for(li=0; li<5; li++){
                        int l=(int)strlen(lvl_names[li]);
                        if(len>l+2 && !strncmp(p,lvl_names[li],l) && p[l]==':' && p[l+1]==' '){
                            p+=l+2; len-=l+2; break;
                        }
                    }
                }
                /* the ring appends "  [xN]" when the SAME line repeats;
                 * show that as a count badge instead of raw text */
                if(len>6 && !strncmp(p+len-4,"[x",2) && p[len-1]==']' &&
                    p[len-6]==' ' && p[len-5]==' '){
                    int v=0,k=len-2,mul=1;
                    while(k>len-6 && p[k]>='0' && p[k]<='9'){v+=(p[k]-'0')*mul;mul*=10;k--;}
                    if(v>1){ reps=v; len-=6; if(len>0 && p[len-1]==' ')len--; }
                }
                /* wrap the line into segments that fit the viewport width */
                {
                    const char *seg=p;
                    int rem=len;
                    first_row=nrows;
                    while(rem>0 && nrows<512){
                        int take=console_wrap_len(pgf,seg,rem,ctw,cscale);
                        crow[nrows].s=seg; crow[nrows].len=take;
                        crow[nrows].color=color; crow[nrows].reps=0; nrows++;
                        seg+=take; rem-=take;
                    }
                    if(reps>1 && first_row<nrows) crow[first_row].reps=reps;
                }
                p=nl+1;
            }
                }
            }
            #undef CLEVEL
            dev_console_rows=nrows;
            dev_console_visible=(int)((cbt-ctp)/DEV_CONSOLE_ROW_H);
            /* follow the log: snap to the bottom when new lines arrive,
             * unless the user is dragging the scrollbar mid-read. */
            if(lines!=dev_console_last_lines){
                dev_console_last_lines=lines;
                if(!g_menu.devtools_scroll_drag){
                    g_menu.devtools_scroll=1e9f;
                    clamp_devtools_scroll();
                }
            }else{
                clamp_devtools_scroll();
            }
            /* status chip in the panel's top-right corner */
            snprintf(line,sizeof(line),"JS %s | ESM %s | %d line%s",
                     cwv_host_js_active(host)?"on":"off",
                     !g_esm_enabled?"OFF"
                     :(g_esm_selftest==0?"on(FAIL)"
                     :(g_esm_selftest==1?"on(pass)":"on")),
                     lines,lines==1?"":"s");
            {
                float sw=(float)vita2d_pgf_text_width(pgf,0.55f,line);
                vita2d_pgf_draw_text(pgf,cx+cw-sw-14.0f,
                                     y+(dt_title?dt_title->y+22.0f:cy+30.0f),
                                     RGBA8(150,165,185,255),0.55f,line);
            }
            if(nrows==0){
                vita2d_pgf_draw_text(pgf,cxs,ctp+18,RGBA8(170,185,205,255),cscale,
                                     "(no output yet) console.log/info/warn/error land here.");
            }
            for(i=0;i<nrows;i++){
                const float yy=ctp+(float)i*DEV_CONSOLE_ROW_H-g_menu.devtools_scroll;
                char seg[258];
                if(yy<ctp)continue;
                if(yy+DEV_CONSOLE_ROW_H>cbt)break;
                if(crow[i].len>0){
                    memcpy(seg,crow[i].s,crow[i].len);
                    seg[crow[i].len]=0;
                    vita2d_pgf_draw_text(pgf,cxs,yy+16.0f,crow[i].color,cscale,seg);
                }
                /* repeat badge: "this same line occurred N times in a
                 * row" - a filled accent circle with the count, like the
                 * real DevTools.  Drawn with the same shape primitives the
                 * CWMenu circle items use, tinted by the user's accent. */
                if(crow[i].reps>1){
                    const float brx=cx+cw-26.0f, bry=yy+9.0f;
                    char num[8];
                    vita2d_draw_fill_circle(brx,bry,8.0f,menu_theme_color(g_menu.accent,1.0f));
                    snprintf(num,sizeof(num),"%d",crow[i].reps);
                    vita2d_pgf_draw_text(pgf,brx-vita2d_pgf_text_width(pgf,0.42f,num)*0.5f,
                                         bry+3.0f,RGBA8(255,255,255,255),0.42f,num);
                }
            }
            /* scrollbar appears only when the log overflows the viewport */
            if(dt_track && devtools_scroll_max()>0.0f)
                menu_draw_scrollbar(dt_track,x,y,(cbt-ctp)+devtools_scroll_max(),
                                    cbt-ctp,g_menu.devtools_scroll);
        }else{
            const VitaMenuTemplate *tpl=devtools_active_template();
            devtools_template_text(pgf,tpl,"native-renderer",rx,cy+76,RGBA8(220,230,240,255),0.66f,
                                   "HTML/CSS native renderer is active.");
            devtools_template_text(pgf,tpl,"javascript-dom",rx,cy+110,RGBA8(220,230,240,255),0.66f,
                                   "JS engine: Duktape 2.7.0 - understands ES5.1 only.");
            {
                const char *esm_state =
                    !g_esm_enabled ? "NOT ENABLED (scripts run raw)"
                    : (g_esm_selftest==1 ? "ACTIVE, self-test PASS"
                    : (g_esm_selftest==0 ? "ACTIVE but SELF-TEST FAILED (see runtime log)"
                                         : "ACTIVE (self-test not run)"));
                snprintf(line,sizeof(line),"ESM: built-in transformer, %s - converts let/const, arrows, templates, "
                         "for..of, import/export into ES5.1 at load time.", esm_state);
            }
            devtools_template_text(pgf,tpl,"esm-module",rx,cy+144,RGBA8(150,165,185,255),0.62f,line);
            devtools_template_text(pgf,tpl,"platform-status",rx,cy+178,RGBA8(150,165,185,255),0.62f,
                                   "Vita platform integration is active.");
            /* Shows WHICH menu script is live - a stale per-app override in
             * the data dir is the usual reason new menu features "don't
             * appear" after an update (boot prefers the data-dir copy). */
            snprintf(line,sizeof(line),"CWMenu script v%d, loaded from: %s",
                     cwmenu_script_version(),g_cwm_load_source);
            devtools_template_text(pgf,tpl,"cwm-script",rx,cy+212,RGBA8(150,165,185,255),0.62f,line);
        }

        if(g_menu.dev_section!=4 && devtools_scroll_max()>0.0f){
            const VitaMenuTemplate *dt_panel2=menu_template_find(devtools_active_template(),"content-panel");
            const VitaMenuTemplate *dt_sep2=menu_template_find(devtools_active_template(),"content-separator");
            const VitaMenuTemplate *dt_track2=menu_scrollbar_find(devtools_active_template(),"content");
            const float track_y=y+(dt_sep2?(dt_sep2->y+8.0f):154.0f);
            const float panel_bottom=y+(dt_panel2?(dt_panel2->y+dt_panel2->h):376.0f)-8.0f;
            const float track_h=panel_bottom-track_y;
            if(dt_track2){
                menu_draw_scrollbar(dt_track2,x,y,track_h+devtools_scroll_max(),track_h,g_menu.devtools_scroll);
            }
        }
        {
            const VitaMenuTemplate *footer_sep=menu_template_find(devtools_active_template(),"footer-separator");
            if(footer_sep) menu_separator_style(footer_sep,x+footer_sep->x,y+footer_sep->y,footer_sep->w);
        }
        {
            const VitaMenuTemplate *t=menu_template_find(devtools_active_template(),"back");
            if(t){
                /* Back is a real button again (user feedback round 2):
                 * same rounded-panel style as the main-menu rows. */
                int active=(g_menu.selected==8);
                menu_rect(x+t->x,y+t->y,t->w,t->h,active);
                {
                    float bw=(float)vita2d_pgf_text_width(pgf,0.70f,t->label);
                    vita2d_pgf_draw_text(pgf,x+t->x+(t->w-bw)*0.5f,y+t->y+t->h*0.5f+7.0f,
                        RGBA8(255,255,255,255),0.70f,t->label);
                }
            }
        }
        {
            const VitaMenuTemplate *footer=menu_template_find(devtools_active_template(),"footer");
            if(footer) vita2d_pgf_draw_text(pgf,x+footer->x+25,y+footer->y+25,RGBA8(150,165,185,255),0.60f,
#if CWV_DEBUG
                             g_menu.selected==7 ? "LEFT/RIGHT: choose   CROSS: select   L/R: tabs" :
                             "L/R: tabs   D-pad: navigate   CROSS: select   TRIANGLE: clear overrides");
#else
                             g_menu.selected==7 ? "LEFT/RIGHT: choose   CROSS: select   L/R: tabs" :
                             "L/R: tabs   D-pad: navigate   CROSS: select   TRIANGLE: clear overrides");
#endif
        }
    }

}

/* Public draw entry: active menu first, dual-window partner last (on top). */
/* ==== Debug overlay =========================================================
 * A live diagnostics panel drawn ON TOP of the rendered page.  Rows are
 * individually toggleable; the whole thing is toggled with SELECT, and
 * SELECT+X (while visible) opens the customize mode where d-pad + X flip
 * each row.  All styling goes through the user's accent color so the
 * overlay reads like part of the native UI, not a foreign layer. */
static void draw_debug_overlay(vita2d_pgf *pgf,CwvHost *host,
                               unsigned frame_count, float fps_value,
                               float frame_ms)
{
    char val[160];
    const unsigned accent=menu_theme_color(g_menu.accent,1.0f);
    int rows=0,i;
    float x=12.0f,y=12.0f,w=340.0f,h;
    for(i=0;i<DBG_COUNT;i++) if(g_menu.dbg_mask&(1<<i)) rows++;
    h=30.0f+18.0f*(float)rows+(g_menu.dbg_custom?20.0f:0.0f);
    menu_draw_rounded_rect(x,y,w,h,10.0f,RGBA8(12,16,22,205));
    vita2d_draw_rectangle(x,y,3.0f,h,accent);
    vita2d_pgf_draw_text(pgf,x+14.0f,y+18.0f,accent,0.52f,"debug");
    {
        char cap[24];
        snprintf(cap,sizeof(cap),"%s",g_menu.dbg_custom?"X toggle / SELECT close":"SELECT+X edit");
        vita2d_pgf_draw_text(pgf,x+w-vita2d_pgf_text_width(pgf,0.40f,cap)-12.0f,
                             y+18.0f,RGBA8(140,155,175,255),0.40f,cap);
    }
    y+=30.0f;
    for(i=0;i<DBG_COUNT;i++){
        const char *v=NULL;
        if(!(g_menu.dbg_mask&(1<<i))) continue;
        switch(i){
        case DBG_FPS:  snprintf(val,sizeof(val),"%.1f fps  (%.2f ms)",fps_value,frame_ms); v=val; break;
        case DBG_FRAME:snprintf(val,sizeof(val),"%u",frame_count); v=val; break;
        case DBG_BTN:  v=g_menu.dbg_lastbtn[0]?g_menu.dbg_lastbtn:"-"; break;
        case DBG_ERR: {
            const char *e=cwv_host_js_last_error(host);
            if(!e) e="";
            if(!strncmp(e,"error: ",7)) e+=7;
            snprintf(val,sizeof(val),"%.60s",e[0]?e:"(none)"); v=val; break;
        }
        case DBG_ESM:
            if(!g_esm_enabled) v="OFF (ES5.1 only)";
            else if(g_esm_selftest==0) v="built-in, SELF-TEST FAIL";
            else v=(g_esm_selftest==1)?"built-in (self-test PASS)":"built-in";
            break;
        case DBG_UPT: {
            unsigned sec=(unsigned)(sceKernelGetProcessTimeWide()/1000000ULL);
            snprintf(val,sizeof(val),"%u:%02u:%02u",sec/3600U,(sec/60U)%60U,sec%60U); v=val; break;
        }
        case DBG_URL: {
            const char *u=cwv_host_current_url(host);
            snprintf(val,sizeof(val),"%.58s",u?u:""); v=val; break;
        }
        case DBG_CNS: {
            int errs=0,wrns=0,lines=0;
            cwv_host_console_stats(host,&errs,&wrns,&lines);
            snprintf(val,sizeof(val),"%d line%s  (%d error%s, %d warn)",
                     lines,lines==1?"":"s",errs,errs==1?"":"s",wrns); v=val; break;
        }
        }
        if(g_menu.dbg_custom && g_menu.dbg_item==i)
            vita2d_draw_rectangle(x+4.0f,y,w-8.0f,16.0f,RGBA8(255,255,255,36));
        vita2d_pgf_draw_text(pgf,x+14.0f,y+12.0f,accent,0.50f,dbg_row_names[i]);
        if(g_menu.dbg_custom){
            {
                char st[8];
                snprintf(st,sizeof(st),"[%s]",(g_menu.dbg_mask&(1<<i))?"on":"off");
                vita2d_pgf_draw_text(pgf,x+w-vita2d_pgf_text_width(pgf,0.44f,st)-12.0f,y+12.0f,
                                     RGBA8(170,185,205,255),0.44f,st);
            }
        }
        if(v)
            vita2d_pgf_draw_text(pgf,x+92.0f,y+12.0f,RGBA8(235,240,248,255),0.50f,v);
        y+=18.0f;
    }
}

static void draw_native_menu(vita2d_pgf *pgf,CwvHost *host, unsigned frame_count, float fps_value, float frame_ms)
{
    draw_native_menu_content(pgf,host,frame_count,fps_value,frame_ms);
    menu_draw_pair_window(pgf,native_menu_template());
}

static int handle_menu_touch(int x,int y,CwvHost *host)
{
    const VitaMenuTemplate *active_tpl=native_menu_template();
    float menu_x,menu_y,menu_w,menu_h;
    native_menu_frame(active_tpl,&menu_x,&menu_y,&menu_w,&menu_h);
    (void)menu_w; (void)menu_h;

    /* Dual-window partner (window key `pair <menu>`): it is display-only
       and drawn ON TOP, so a tap that lands inside its frame must not fall
       through to an active-menu element hidden underneath it - swallow it.
       The partner currently exposes no controls of its own; when it does,
       this is where its hit-testing goes. */
    {
        float px,py,pw,ph;
        if(menu_pair_frame(active_tpl,&px,&py,&pw,&ph)==0 &&
           x>=px&&x<(px+pw)&&y>=py&&y<(py+ph))
            return 1;
    }

    if(g_menu.mode==MENU_MAIN){
#if CWV_DEVTOOLS_ENABLED
        if(menu_template_hit(vita_menu_main_template,"devtools",menu_x,menu_y,x,y)){
            native_menu_open(CWV_DEVTOOLS_MENU_MODE);return 1;
        }
#endif /* CWV_DEVTOOLS_ENABLED */
#if CWV_FILES_ENABLED
        {   /* (84) native Files button (script element or fallback rect) */
            float fx,fy,fw,fh;
            files_button_rect(menu_x,menu_y,&fx,&fy,&fw,&fh);
            if(x>=fx&&x<(fx+fw)&&y>=fy&&y<(fy+fh)){
                native_menu_open(MENU_FILES);return 1;
            }
        }
#endif /* CWV_FILES_ENABLED */
        if(menu_template_hit(vita_menu_main_template,"back",menu_x,menu_y,x,y)){
            g_menu.mode=MENU_CLOSED;
            return 1;
        }
        const char *names[]={"page-operations","open-url","settings"};
        for(int i=0;i<3;i++) if(menu_template_hit(vita_menu_main_template,names[i],menu_x,menu_y,x,y)){
            g_menu.selected=i;
            if(i==0){
                native_menu_open(CWV_PAGE_OPERATIONS_MENU_MODE);
            }else if(i==1){
                open_url_ime_start(host);
            }else{
                native_menu_open(CWV_SETTINGS_MENU_MODE);
            }
            return 1;
        }
#if CWV_FILES_ENABLED
    }else if(g_menu.mode==MENU_FILES){
        return handle_files_touch(x,y,host,menu_x,menu_y)?1:0;
#endif /* CWV_FILES_ENABLED */
    }else if(g_menu.mode==MENU_PAGEOPS){
        const VitaMenuTemplate *tpl=pageops_active_template();
        const char *names[]={"history-back","forward","reload"};
        for(int i=0;i<3;i++){
            if(menu_template_hit(tpl,names[i],menu_x,menu_y,x,y)){
                g_menu.pageops_selected=i;
                if(i==0) browser_back(host);
                else if(i==1) browser_forward(host);
                else browser_refresh(host);
                return 1;
            }
        }
        if(menu_template_hit(tpl,"menu-back",menu_x,menu_y,x,y)){
            g_menu.pageops_selected=3;
            g_menu.mode=MENU_MAIN;
            g_menu.selected=0;
            return 1;
        }
        return 1;
    }else if(g_menu.mode==MENU_SETTINGS){
        {
            static const char *tab_names[3]={"appearance-tab","controls-tab","preferences-tab"};
            for(int ti=0;ti<3;ti++){
                if(menu_template_hit(settings_active_template(),tab_names[ti],menu_x,menu_y,x,y)){
                    settings_select_tab(ti);
                    return 1;
                }
            }
        }
        if(g_menu.settings_tab==1 && settings_scroll_max()>0.0f){
            const VitaMenuTemplate *sb=menu_scrollbar_find(settings_active_template(),"controls");
            float tx=menu_x+(sb?sb->x:704.0f), ty=menu_y+(sb?sb->y:154.0f), th=sb?sb->h:194.0f;
            if(x>=tx-12.0f && x<=tx+18.0f && y>=ty && y<=ty+th){
                float t=(y-ty)/th; if(t<0)t=0; if(t>1)t=1; g_menu.settings_scroll=t*settings_scroll_max(); g_menu.settings_scroll_drag=1; return 1;
            }
            if(g_menu.settings_scroll_drag){
                float t=(y-ty)/th; if(t<0)t=0; if(t>1)t=1; g_menu.settings_scroll=t*settings_scroll_max(); return 1;
            }
        }
        int hit_row=-1;
        for(int ri=0;ri<(g_menu.settings_tab==1?5:(g_menu.settings_tab==0?4:3));ri++){
            char row_name[32];
            snprintf(row_name,sizeof(row_name),"content-row-%d",ri);
            const VitaMenuTemplate *rt=menu_template_find(settings_active_template(),row_name);
            if(rt && menu_button(menu_x+rt->x,menu_y+rt->y-g_menu.settings_scroll*(g_menu.settings_tab==1),rt->w,rt->h,x,y)){ hit_row=ri; break; }
        }
        if(hit_row>=0){
            int row=hit_row;
            const VitaMenuTemplate *rt=menu_template_find(settings_active_template(),row==0?"content-row-0":row==1?"content-row-1":row==2?"content-row-2":row==3?"content-row-3":"content-row-4");
            if(vita_row_action_hidden(rt)) return 1;  /* project-gated row */
            settings_select_content(3+row);
            if(g_menu.settings_tab==0){
                if(menu_action_is(rt,"toggle_theme")){g_menu.dark_mode=!g_menu.dark_mode;g_menu.dirty=1;}
                else if(menu_action_is(rt,"cycle_accent")){g_menu.accent=(g_menu.accent+1)%6;g_menu.dirty=1;}
                else if(menu_action_is(rt,"toggle_css_cursors")){g_menu.css_cursors=!g_menu.css_cursors;g_menu.dirty=1;}
                else if(menu_action_is(rt,"set_zoom")){
                    /* The zoom row is a CWMenu slider (CWMenu 2): a tap snaps to
                     * the nearest rendered stop instead of a free 300px band.
                     * Track geometry matches menu_draw_slider (x+190, w-220). */
                    int stops=menu_template_stops(rt);
                    float rel=(float)(x-(menu_x+rt->x+190.0f))/(rt->w-220.0f);
                    int v=(int)(rel*(float)(stops-1)+0.5f);
                    if(v<0)v=0;
                    if(v>=stops)v=stops-1;
                    g_menu.zoom_level=0.75f+(1.25f*(float)v/(float)(stops-1));
                    g_menu.dirty=1;
                    (void)cwv_host_set_zoom(host,g_menu.zoom_level);
                }
            }else if(g_menu.settings_tab==1){
                if(menu_action_is(rt,"cycle_scroll_input")){g_menu.scroll_mode=(g_menu.scroll_mode+1)%3;if(g_menu.scroll_mode==2)cwv_host_focus_clear(host);g_menu.dirty=1;}
                else if(menu_action_is(rt,"cycle_zoom_input")){g_menu.zoom_mode=(g_menu.zoom_mode+1)%3;g_menu.dirty=1;}
                else if(menu_action_is(rt,"cycle_pointer")){g_menu.pointer=!g_menu.pointer;g_menu.dirty=1;}
                else if(menu_action_is(rt,"set_scroll_speed")){ int stops=menu_template_stops(rt); float rel=(float)(x-(menu_x+rt->x+190.0f))/(rt->w-220.0f); int v=(int)(rel*(float)(stops-1)+0.5f); if(v<0)v=0; if(v>=stops)v=stops-1; g_menu.scroll_speed=v; g_menu.dirty=1; }
            }else{
                if(menu_action_is(rt,"clear_cache"))settings_clear_cache();
                else if(menu_action_is(rt,"reset_preferences"))settings_reset_preferences(host);
            }
            return 1;
        }
        if(menu_template_hit(settings_active_template(),"undo",menu_x,menu_y,x,y)){
            g_menu.selected=0; settings_load(); (void)cwv_host_set_zoom(host,g_menu.zoom_level); return 1;
        }
        if(menu_template_hit(settings_active_template(),"save",menu_x,menu_y,x,y)){
            g_menu.selected=1; settings_save(); return 1;
        }
        if(menu_template_hit(settings_active_template(),"exit",menu_x,menu_y,x,y)){
            g_menu.selected=2; g_menu.mode=MENU_MAIN; g_menu.selected=0; return 1;
        }

    }else if(g_menu.mode==MENU_DEVTOOLS){
        {
            static const char *tab_names[8]={"performance","renderer","input","page","overrides","console","platform","close"};
            for(int i=0;i<8;i++){
                if(menu_template_hit(devtools_active_template(),tab_names[i],menu_x,menu_y,x,y)){
                    if(i==7){
                        g_menu.exit_confirm_return_section=g_menu.dev_section;
                        g_menu.exit_confirm_selected=0;
                        g_menu.selected=7;
                        g_menu.devtools_scroll=0.0f;
                    }else{
                        g_menu.dev_section=i;
                        g_menu.selected=i;
                        g_menu.dev_option=0;
                        g_menu.devtools_scroll=0.0f;
                    }
                    return 1;
                }
            }
        }
        if(menu_template_hit(devtools_active_template(),"back",menu_x,menu_y,x,y)){
            if(g_menu.selected==7){
                g_menu.selected=g_menu.exit_confirm_return_section;
                g_menu.dev_section=g_menu.exit_confirm_return_section;
                g_menu.dev_option=0;
            }else{
                g_menu.mode=MENU_MAIN; g_menu.selected=0;
            }
            g_menu.devtools_scroll=0.0f; g_menu.devtools_scroll_drag=0;
            return 1;
        }
        if(g_menu.selected==7){
            const VitaMenuTemplate *cn=menu_template_find(devtools_active_template(),"close-no");
            const VitaMenuTemplate *cyes=menu_template_find(devtools_active_template(),"close-yes");
            if(cn && menu_template_hit(devtools_active_template(),"close-no",menu_x,menu_y,x,y)){
                g_menu.exit_confirm_selected=0;
                return 1;
            }
            if(cyes && menu_template_hit(devtools_active_template(),"close-yes",menu_x,menu_y,x,y)){
                g_menu.exit_confirm_selected=1;
                g_menu.exit_requested=1;
                g_menu.mode=MENU_CLOSED;
                return 1;
            }
            return 1;
        }
        /* All DevTools content tabs share the same optional inset scrollbar.
         * It remains hidden when the tab fits inside its template viewport. */
        if(g_menu.dev_section!=4 && devtools_scroll_max()>0.0f){
            const VitaMenuTemplate *dt_panel=menu_template_find(devtools_active_template(),"content-panel");
            const VitaMenuTemplate *dt_sep=menu_template_find(devtools_active_template(),"content-separator");
            const VitaMenuTemplate *dt_track=menu_scrollbar_find(devtools_active_template(),"content");
            const float list_top=menu_y+(dt_sep?(dt_sep->y+8.0f):154.0f);
            const float panel_x=menu_x+(dt_panel?dt_panel->x:20.0f);
            const float panel_y=menu_y+(dt_panel?dt_panel->y:106.0f);
            const float panel_w=dt_panel?dt_panel->w:700.0f;
            const float list_bottom=panel_y+(dt_panel?dt_panel->h:270.0f)-8.0f;
            const float track_x=menu_x+(dt_track?dt_track->x:708.0f);
            const float track_w=dt_track?dt_track->w:5.0f;
            if(x>=track_x-12.0f && x<=track_x+track_w+12.0f && y>=list_top && y<=list_bottom){
                float t=(float)(y-list_top)/(list_bottom-list_top);
                if(t<0.0f)t=0.0f;
                if(t>1.0f)t=1.0f;
                g_menu.devtools_scroll=t*devtools_scroll_max();
                g_menu.devtools_scroll_drag=1;
                return 1;
            }
            if(g_menu.devtools_scroll_drag){
                if(x>=panel_x && x<=panel_x+panel_w && y>=list_top && y<=list_bottom){
                    float t=(float)(y-list_top)/(list_bottom-list_top);
                    if(t<0.0f)t=0.0f;
                    if(t>1.0f)t=1.0f;
                    g_menu.devtools_scroll=t*devtools_scroll_max();
                }
                return 1;
            }
        }

        if(g_menu.dev_section==4){
            const VitaMenuTemplate *dt_panel=menu_template_find(devtools_active_template(),"content-panel");
            const VitaMenuTemplate *dt_row0=menu_template_find(devtools_active_template(),"content-row-0");
            const VitaMenuTemplate *dt_track=menu_scrollbar_find(devtools_active_template(),"content");
            const float panel_x=menu_x+(dt_panel?dt_panel->x:20.0f);
            const float panel_y=menu_y+(dt_panel?dt_panel->y:106.0f);
            const float panel_w=dt_panel?dt_panel->w:700.0f;
            const float panel_h=dt_panel?dt_panel->h:270.0f;
            const float list_top=menu_y+(dt_row0?dt_row0->y:154.0f);
            const float list_bottom=panel_y+panel_h-8.0f;
            const float track_x=menu_x+(dt_track?dt_track->x:(20.0f+700.0f-12.0f));
            const float track_w=dt_track?dt_track->w:5.0f;
            if(x>=track_x-12.0f && x<=track_x+track_w+12.0f && y>=list_top && y<=list_bottom && devtools_scroll_max()>0.0f){
                float t=(float)(y-list_top)/(list_bottom-list_top);
                if(t<0) t=0;
                if(t>1) t=1;
                g_menu.devtools_scroll=t*devtools_scroll_max();
                g_menu.devtools_scroll_drag=1;
                return 1;
            }
            if(g_menu.devtools_scroll_drag){
                if(x>=panel_x && x<=panel_x+panel_w && y>=list_top && y<=list_bottom){
                    float t=(float)(y-list_top)/(list_bottom-list_top);
                    if(t<0) t=0;
                    if(t>1) t=1;
                    g_menu.devtools_scroll=t*devtools_scroll_max();
                }
                return 1;
            }
            if(x>=panel_x && x<=panel_x+panel_w-30.0f && y>=list_top && y<=list_bottom){
                int row=-1;
                for(int ri=0;ri<7;ri++){
                    char row_name[32];
                    snprintf(row_name,sizeof(row_name),"content-row-%d",ri);
                    const VitaMenuTemplate *rt=menu_template_find(devtools_active_template(),row_name);
                    if(!rt) continue;
                    float ry=menu_y+rt->y-g_menu.devtools_scroll;
                    if(menu_button(menu_x+rt->x,ry,rt->w,rt->h,x,y)){ row=ri; break; }
                }
                if(row<0) return 1;
                if(row>6) row=6;
                g_menu.dev_option=row;
                if(row==0)g_menu.override_vcm=cycle_control_override(g_menu.override_vcm,1);
                else if(row==1)g_menu.override_scroll=(g_menu.override_scroll<0)?0:(g_menu.override_scroll+1)%3;
                else if(row==2)g_menu.override_zoom=(g_menu.override_zoom<0)?0:(g_menu.override_zoom+1)%3;
                else if(row==3)g_menu.override_theme=(g_menu.override_theme<0)?effective_dark_mode():(1-g_menu.override_theme);
                else if(row==4)g_menu.override_scrollbars=(g_menu.override_scrollbars<0)?0:(g_menu.override_scrollbars+1)%2;
                else if(row==5)g_menu.override_zoom_level=cycle_zoom_override(g_menu.override_zoom_level,1);
                else { (void)button_wait_override_open_keyboard(); }
                apply_runtime_overrides(host);
                return 1;
            }
        }
        if(menu_template_hit(devtools_active_template(),"back",menu_x,menu_y,x,y)){
            g_menu.mode=MENU_MAIN;g_menu.selected=0;
            return 1;
        }
    }

    return 1;
}

static void send_button(CwvHost *host, const char *name, int down)
{
    char script[128];
    snprintf(script, sizeof(script),
             "window.CwvInput && window.CwvInput.%s('%s')",
             down ? "buttonDown" : "buttonUp", name);
    cwv_host_eval(host, script);
}

static void send_key(CwvHost *host, const char *key, int shift, int down)
{
    char script[192];
    snprintf(script, sizeof(script),
             "window.CwvInput && window.CwvInput.key%s('%s',%s)",
             down ? "Down" : "Up", key, shift ? "true" : "false");
    cwv_host_eval(host, script);
}

/* Per-app runtime log (user direction (85)): the log lives at
 * ux0:data/CWebView/<TITLE_ID>/CWV.log (<data>/CWebView/$TITLEID),
 * so two CWebView-based apps never share or overwrite each other's
 * diagnostics. The directory chain is created on first use; the path
 * is computed once. */
static const char *app_data_dir(void);
static const char *runtime_log_path(void)
{
    static char path[272];
    static int init = 0;
    if (!init) {
        init = 1;
        char nsdir[256];
        snprintf(nsdir, sizeof(nsdir), "ux0:data/CWebView/%s", app_data_dir());
        sceIoMkdir("ux0:data", 0777);
        sceIoMkdir("ux0:data/CWebView", 0777);
        sceIoMkdir(nsdir, 0777);
        snprintf(path, sizeof(path), "%s/CWV.log", nsdir);
    }
    return path;
}

/* Debug/dev builds keep logging ENABLED (user correction 2026-10-01:
 * debugging on hardware needs CWV.log; CWV_DEBUG adds the on-screen
 * overlay and boot trace on top). */
static void write_runtime_log(const char *fmt, ...)
{
    SceUID fd = sceIoOpen(runtime_log_path(),
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0) return;

    va_list ap;
    char buffer[512];
    int len;
    va_start(ap, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if (len > (int)sizeof(buffer) - 2) len = (int)sizeof(buffer) - 2;
    buffer[len++] = '\n';
    buffer[len] = '\0';
    sceIoWrite(fd, buffer, len);
    sceIoClose(fd);
}

/* Common dialogs (the IME keyboard among them) need TWO things that real
 * hardware enforces and Vita3K silently does not:
 *   1. a config param set once at boot (sceCommonDialogSetConfigParam), and
 *   2. a per-frame sceCommonDialogUpdate() (see the main loop) so the system
 *      can composite the dialog into our backbuffer.
 * Without these the keyboard NEVER APPEARS on a real Vita while the app keeps
 * running and still answers START - exactly the "opens the keyboard and the
 * app freezes / keyboard invisible" symptom on hardware. */
static void init_common_dialog(void)
{
    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    /* Fallbacks if the system params cannot be read: English UI, X=enter. */
    config.language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    config.enterButtonAssign = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    int module_rc = sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL);
    if (module_rc >= 0) {
        int lang = (int)config.language, btn = (int)config.enterButtonAssign;
        if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) >= 0)
            config.language = (SceSystemParamLang)lang;
        if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON,
                                        &btn) >= 0)
            config.enterButtonAssign = (SceSystemParamEnterButtonAssign)btn;
    }
    int set_rc = sceCommonDialogSetConfigParam(&config);
    write_runtime_log("common dialog configured "
                     "(apputil module=%d, set rc=%d, lang=%d, enter=%d)",
                     module_rc, set_rc, (int)config.language,
                     (int)config.enterButtonAssign);
}

static void ensure_data_directory(void)
{
    /* (84) the data root is the app's TITLE ID; the first diagnostic log
     * writes via the same path builder so a failed mkdir cannot hide the
     * evidence we need. VitaSDK examples use sceIoMkdir for Vita paths
     * rather than relying on host POSIX mkdir. */
    char base[192];
    snprintf(base, sizeof(base), "ux0:data/%s", VITA_TITLEID);
    int rc = sceIoMkdir(base, 0777);
    write_runtime_log("sceIoMkdir(%s)=%d", base, rc);
}


static void draw_boot_diagnostic(const char *line1, const char *line2)
{
    vita2d_start_drawing();
    vita2d_set_clear_color(RGBA8(18, 18, 24, 255));
    vita2d_clear_screen();
    /* Deliberately avoid fonts here. If these rectangles appear, the GPU/
     * framebuffer path is alive independently of PGF/WebKit. */
    vita2d_draw_rectangle(0, 0, SCREEN_W, 80, RGBA8(42, 55, 80, 255));
    vita2d_draw_rectangle(40, 150, 260, 220, RGBA8(220, 60, 60, 255));
    vita2d_draw_rectangle(350, 150, 260, 220, RGBA8(60, 200, 90, 255));
    vita2d_draw_rectangle(660, 150, 260, 220, RGBA8(70, 110, 225, 255));
    (void)line1; (void)line2;
    vita2d_end_drawing();
    vita2d_swap_buffers();
    sceDisplayWaitVblankStart();
}

/* Native scrollbar interaction.  The renderer draws the scrollbar chrome;
 * main.c owns the touch gesture so dragging it never becomes a page click. */
static int scrollbar_hit(const CwvHost *host, int x, int y, int *axis,
                         float *thumb_start, float *thumb_size, float *max_scroll)
{
    if (!host) return 0;
    const float sb = 8.0f;
    const float inset = 12.0f;
    float cw = cwv_host_content_width(host);
    float ch = cwv_host_content_height(host);
    int vertical = ch > (float)SCREEN_H-2.0f*inset;
    int horizontal = cw > (float)SCREEN_W-2.0f*inset;
    if(horizontal && ch > (float)SCREEN_H-2.0f*inset-sb) vertical = 1;
    if(vertical && cw > (float)SCREEN_W-2.0f*inset-sb) horizontal = 1;
    float viewport_w=(float)SCREEN_W-2.0f*inset-(vertical?sb:0.0f);
    float viewport_h=(float)SCREEN_H-2.0f*inset-(horizontal?sb:0.0f);
    float max_x = cw - viewport_w;
    float max_y = ch - viewport_h;
    if (max_x < 0.0f) max_x = 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;

    /* Scrollbar chrome is on the physical screen edge. It does not consume
       another 12px of page inset. */
    if (vertical && max_y > 0.0f && x >= (int)(SCREEN_W-sb) && x < SCREEN_W &&
        y >= 0 && y < (int)(SCREEN_H-(horizontal?sb:0.0f))) {
        float track_h=(float)SCREEN_H-(horizontal?sb:0.0f);
        float th = track_h * (viewport_h / ch);
        if (th < 24.0f) th = 24.0f;
        if (th > track_h) th = track_h;
        float ts = (track_h-th) * (cwv_host_scroll_offset(host) / max_y);
        if (y >= ts && y <= ts+th) {
            if (axis) *axis=1;
            if (thumb_start) *thumb_start=ts;
            if (thumb_size) *thumb_size=th;
            if (max_scroll) *max_scroll=max_y;
            return 1;
        }
    }

    if (horizontal && max_x > 0.0f && y >= (int)(SCREEN_H-sb) && y < SCREEN_H &&
        x >= 0 && x < (int)(SCREEN_W-(vertical?sb:0.0f))) {
        float track_w=(float)SCREEN_W-(vertical?sb:0.0f);
        float tw = track_w * (viewport_w / cw);
        if (tw < 24.0f) tw = 24.0f;
        if (tw > track_w) tw = track_w;
        float ts = (track_w-tw) * (cwv_host_scroll_x_offset(host) / max_x);
        if (x >= ts && x <= ts+tw) {
            if (axis) *axis=2;
            if (thumb_start) *thumb_start=ts;
            if (thumb_size) *thumb_size=tw;
            if (max_scroll) *max_scroll=max_x;
            return 1;
        }
    }
    return 0;
}

static void browser_back(CwvHost *host)
{
    if(g_menu.history_index>0){
        g_menu.history_index--;
        cwv_host_load(host,g_menu.history[g_menu.history_index]);
    }
}

static void browser_forward(CwvHost *host)
{
    if(g_menu.history_index+1<g_menu.history_count){
        g_menu.history_index++;
        cwv_host_load(host,g_menu.history[g_menu.history_index]);
    }
}

static void browser_refresh(CwvHost *host)
{
    const char *u=cwv_host_current_url(host);
    if(u&&*u)cwv_host_load(host,u);
}













/* CWMenu reusable feature set. */
enum {
    CWM_FEATURE_WINDOW_STYLE = 1 << 0,
    CWM_FEATURE_LAYOUT       = 1 << 1,
    CWM_FEATURE_TOGGLE       = 1 << 2,
    CWM_FEATURE_RADIO        = 1 << 3,
    CWM_FEATURE_STEPPER      = 1 << 4,
    CWM_FEATURE_SLIDER       = 1 << 5,
    CWM_FEATURE_PROGRESS     = 1 << 6,
    CWM_FEATURE_SHAPE        = 1 << 7,
    CWM_FEATURE_SCROLLBAR    = 1 << 8,
    CWM_FEATURE_VARIABLES    = 1 << 9,
    CWM_FEATURE_CONDITIONS   = 1 << 10,
    CWM_FEATURE_INPUT_WAIT   = 1 << 11,
    CWM_FEATURE_TAB_NAV      = 1 << 12,
    CWM_FEATURE_TINT         = 1 << 13
};











int main(void)
{
    int vita2d_rc;

    write_runtime_log("--- CWebView boot ---");
    write_runtime_log("boot display diagnostic previously passed");
    ensure_data_directory();
    write_runtime_log("main() reached; beginning renderer initialization");

    vita2d_rc = vita2d_init();
    write_runtime_log("vita2d_init() = %d", vita2d_rc);
    if (vita2d_rc < 0) {
        /* Some libvita2d builds expose the advanced initializer separately.
         * Try it once before giving up, while recording the exact result. */
        int retry_rc = vita2d_init_advanced(4 * 1024 * 1024);
        write_runtime_log("vita2d_init_advanced(4 MiB) = %d", retry_rc);
        if (retry_rc < 0) {
            sceKernelDelayThread(250000);
            sceKernelExitProcess(-1);
            return -1;
        }
    }
    vita2d_set_clear_color(RGBA8(18, 18, 24, 255));
    write_runtime_log("vita2d initialized successfully");

    /* Must run before any IME/common dialog can open (see the comment in
       init_common_dialog): this is the real-hardware keyboard fix. */
    init_common_dialog();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    vita_power_callbacks_start();

    /* Load the scroll/input user module before using its exported stub API.
       The module is packaged with the application rather than installed as a
       taiHEN plugin, so it remains self-contained and Vita3K-friendly. */
    int scroll_module_status = 0;
    SceUID scroll_module = sceKernelLoadStartModule(
        "app0:sce_module/CwvScroll.suprx", 0, NULL, 0, NULL,
        &scroll_module_status);
    write_runtime_log("CwvScroll load uid=%d status=%d",
                      scroll_module, scroll_module_status);
    int scroll_enabled = (scroll_module >= 0 &&
                          cwv_scroll_initialize(SCREEN_H) >= 0);

    /* Re-assert controller sampling after loading the optional scroll module.
     * The module is allowed to install its own input plumbing, so doing this
     * here guarantees the application still receives the full physical Vita
     * controller state on hardware as well as in Vita3K. */
    (void)sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    (void)sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG);

    CwvHost *host = cwv_host_create(SCREEN_W, SCREEN_H);
    write_runtime_log("cwv_host_create() = %p", (void *)host);
    /* Image-load failures mirror into VWWV.log: pages without scripts
     * have no JS context, so their console lines are dropped - the log
     * is the one channel that works on EVERY page. */
    cwv_host_set_runtime_log(host, write_runtime_log);

    /* Bluetooth KBM (2026-10-05): paired HID keyboards synthesize
     * controller bits (see vita_read_controller); a Bluetooth mouse
     * drives the same virtual cursor the rear touchpad uses.  Absent
     * devices simply leave this inert. */
    cwv_kbm_vita_set_screen(SCREEN_W, SCREEN_H);
    cwv_kbm_vita_init();
    if (!host) {
        vita2d_fini();
        sceKernelExitProcess(-1);
        return -1;
    }

    /* ES6/TS provider: the shared built-in transformer + library
       prelude, exactly like every other port (cwv_host_enable_es_builtin).
       The old CwvESM.suprx runtime module was removed (ESM round,
       2026-10-03): it kept a SECOND copy of the transformer in RAM
       (the built-in fallback had to be linked into the eboot anyway),
       slowed every boot by a module load, and on Vita3K its exported
       NIDs were stubbed to "return 0" so pages silently ran raw ES6.
       The self-test stays - it proves the chain end-to-end at boot,
       before the first page can run. */
    if (cwv_host_enable_ts_builtin(host) == 0 &&
        cwv_host_enable_es_builtin(host) == 0) {
        g_esm_enabled = 1;
        g_esm_selftest = cwv_host_esm_selftest(
            host, g_esm_selftest_report, sizeof(g_esm_selftest_report));
        write_runtime_log("ESM built-in enabled; self-test %s: %s",
                          g_esm_selftest==1?"PASS":"FAIL",
                          g_esm_selftest_report);
    } else {
        write_runtime_log("ESM built-in unavailable; running ES5.1 only");
    }

    /* The Vita-specific web layer is enabled by the query parameter. */
    #ifndef CWV_WEBPAGE
#define CWV_WEBPAGE "index.html"
#endif
#ifndef CWV_LAUNCH_PAGE
#define CWV_LAUNCH_PAGE CWV_WEBPAGE
#endif
    char entry_url[1024];
    snprintf(entry_url, sizeof(entry_url), "file:///app0/web/%s?vita=1", CWV_LAUNCH_PAGE);
    /* Initialize the native shell state before loading persistent settings.
     * Previously settings_load() ran before this memset(), so the loaded values
     * were immediately erased. */
    memset(&g_menu,0,sizeof(g_menu));
    g_menu.mode=MENU_CLOSED;
#if CWV_CURSOR_ENABLED
    /* Cursor art: app0:/assets/cursor-vita.png inside the VPK (the build
     * stages it there; package.json vita.cursor overrides the source).
     * CWV_CURSOR_ENABLED=0 compiles the whole cursor out. */
    cwv_cursor_init(&g_cursor,"app0:/assets/cursor-vita.png");
    /* CSS cursor pack (2026-09-26 (45)): app0:/assets/cursors/ inside
     * the VPK (build stages it).  Missing art falls back to the console
     * art above, then the code arrow. */
    cwv_cursor_set_style_dir(&g_cursor,"app0:/assets/cursors");
#endif
    settings_load();
#if CWV_CURSOR_ENABLED
    cwv_cursor_place(&g_cursor,(float)SCREEN_W/2.0f,(float)SCREEN_H/2.0f,
                     SCREEN_W,SCREEN_H);
    cwv_cursor_show(&g_cursor,g_menu.pointer);
#endif

    /* CWMenu menu geometry and visual objects are declarative. Prefer a
     * per-application override so an app/developer can customize menus
     * without rebuilding the executable, then fall back to the packaged
     * default script. */
    {
        char cwm_override[320];
        app_data_path(cwm_override,sizeof(cwm_override),"cwmenu/default.cwm");
        int cwm_rc=vita_native_menu_templates_load(cwm_override);
        if(cwm_rc<0){
            cwm_rc=vita_native_menu_templates_load("app0:/cwmenu/default.cwm");
            snprintf(g_cwm_load_source,sizeof(g_cwm_load_source),"packaged default");
            write_runtime_log("CWMenu: packaged menu script load rc=%d",cwm_rc);
        }else{
            snprintf(g_cwm_load_source,sizeof(g_cwm_load_source),"app-data override");
            write_runtime_log("CWMenu: loaded per-application menu script rc=%d",cwm_rc);
        }
        { float vv; if(cwm_var_get_number("scroll_speed_stops",&vv)!=0) cwm_var_set_number("scroll_speed_stops",3.0f);
          if(cwm_var_get_number("pitch_stops",&vv)!=0) cwm_var_set_number("pitch_stops",6.0f);
          if(cwm_var_get_number("menu_brightness",&vv)!=0) cwm_var_set_number("menu_brightness",1.0f);
          if(cwm_var_get_number("menu_accent_tint",&vv)!=0) cwm_var_set_number("menu_accent_tint",1.0f); }
        /* ---- versioning + dynamic checks (CWMenu 2) ----------------------
         * The controller parses ANY version and ignores unknown constructs,
         * so a newer script degrades gracefully; this just makes it LOUD
         * in the runtime log instead of a silent surprise. */
        {
            int ver=cwmenu_script_version();
            int sup=cwm_script_version_supported();
            write_runtime_log("CWMenu: script version %d (controller supports %d)%s",
                              ver,sup,(ver<sup)?" - legacy script, built-in defaults for newer features":"");
            if(ver>sup)
                write_runtime_log("CWMenu: script is NEWER than this controller - unknown constructs are ignored");
            /* theme block actually present? */
            write_runtime_log("CWMenu: theme slots %d, palette owned by %s",
                              cwm_theme_count(),(cwm_theme_count()>0)?"the .cwm script":"built-in C defaults");
            /* menus the app removed or renamed get a warning, not a crash:
               their templates stay empty and hit-testing simply finds nothing */
            static const char *checked[]={"main","settings_appearance","devtools_performance"};
            size_t ci;
            for(ci=0;ci<sizeof(checked)/sizeof(checked[0]);ci++){
                int cnt=vita_native_menu_count(checked[ci]);
                if(cnt==0)
                    write_runtime_log("CWMenu: menu '%s' not provided by the script - functionality hidden",checked[ci]);
            }
            /* app-specific menus from the script land in the dynamic registry */
            { int di; for(di=0;di<cwm_dynamic_menu_total();di++)
                  write_runtime_log("CWMenu: app menu '%s' (%d elements) available via cwm_dynamic_menu()",
                                    cwm_dynamic_menu_name(di),
                                    cwmenu_script_menu_count(cwm_dynamic_menu_name(di))); }
        }
    }
    (void)cwv_host_set_theme(host,g_menu.dark_mode);
    (void)cwv_host_set_scrollbars(host,1);

    int web_load_result = cwv_host_load(host, entry_url);
    write_runtime_log("cwv_host_load() = %d", web_load_result);
    if (web_load_result >= 0) { history_reset(cwv_host_current_url(host)); cwv_host_set_zoom(host,g_menu.zoom_level); }
    if (web_load_result < 0) {
        /* Keep the failure visible instead of presenting a misleading black
         * frame. This path is also useful while the native renderer evolves. */
        SceCtrlData diagnostic;
        memset(&diagnostic, 0, sizeof(diagnostic));
        for (;;) {
            (void)vita_read_controller(&diagnostic);
            draw_boot_diagnostic("web load failed", "press START to quit");
            if (diagnostic.buttons & SCE_CTRL_START) break;
            sceKernelDelayThread(16667);
        }
        cwv_host_destroy(host);
        vita2d_fini();
        sceKernelExitProcess(-1);
        return -1;
    }

    SceCtrlData previous;
    SceCtrlData current;
    SceTouchData touch;
    SceTouchData previousTouch;
    vita2d_pgf *devtools_pgf = vita2d_load_default_pgf();
    memset(&previous, 0, sizeof(previous));
    memset(&previousTouch, 0, sizeof(previousTouch));
    int previous_page_touch_x = -1;
    int previous_page_touch_y = -1;
    int page_pan_start_x = -1, page_pan_start_y = -1;   /* 2D pan (74) */
    int horizontal_touch_drag = 0;
    int scrollbar_drag_axis = 0;
    float scrollbar_drag_start = 0.0f;
    float scrollbar_drag_scroll = 0.0f;
    float scrollbar_drag_max = 0.0f;
    int menu_cross_latched = 0;
    int suspend_input_cleared = 0;

    apply_runtime_overrides(host);
    unsigned frame_count = 0;
    uint64_t fps_window_start = (uint64_t)sceKernelGetProcessTimeWide();
    unsigned fps_window_frames = 0;
    float fps_value = 60.0f, frame_ms = 16.67f;
    for (;;) {
        /* Do not update JavaScript, process input, or submit GPU frames while
         * the OS has suspended the application.  Vita power callbacks only
         * set flags; the main thread performs all renderer/input recovery. */
        if (g_vita_suspended) {
            if (!suspend_input_cleared) {
                static const char *const release_names[] = {
                    "cross", "circle", "square", "triangle", "up", "right",
                    "down", "left", "l", "r", "CROSS", "CIRCLE", "SQUARE",
                    "TRIANGLE", "DPAD_UP", "DPAD_RIGHT", "DPAD_DOWN", "DPAD_LEFT",
                    "LTRIGGER", "RTRIGGER", "START", "SELECT"
                };
                size_t ri;
                cwv_host_pointer_cancel(host);
                for (ri = 0; ri < sizeof(release_names)/sizeof(release_names[0]); ++ri)
                    send_button(host, release_names[ri], 0);
                memset(&previous, 0, sizeof(previous));
                memset(&current, 0, sizeof(current));
                memset(&previousTouch, 0, sizeof(previousTouch));
                memset(&touch, 0, sizeof(touch));
                previous_page_touch_x = previous_page_touch_y = -1;
                page_pan_start_x = page_pan_start_y = -1;
                horizontal_touch_drag = 0;
                scrollbar_drag_axis = 0;
                suspend_input_cleared = 1;
                write_runtime_log("power: app suspended; input and frame updates paused");
            }
            sceKernelDelayThread(50000);
            continue;
        }
        if (suspend_input_cleared || g_vita_resume_pending) {
            static const char *const release_names[] = {
                "cross", "circle", "square", "triangle", "up", "right",
                "down", "left", "l", "r", "CROSS", "CIRCLE", "SQUARE",
                "TRIANGLE", "DPAD_UP", "DPAD_RIGHT", "DPAD_DOWN", "DPAD_LEFT",
                "LTRIGGER", "RTRIGGER", "START", "SELECT"
            };
            size_t ri;
            cwv_host_pointer_cancel(host);
            for (ri = 0; ri < sizeof(release_names)/sizeof(release_names[0]); ++ri)
                send_button(host, release_names[ri], 0);
            g_vita_resume_pending = 0;
            suspend_input_cleared = 0;
            (void)sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
            (void)sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG);
            (void)sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
            memset(&previous, 0, sizeof(previous));
            memset(&current, 0, sizeof(current));
            memset(&previousTouch, 0, sizeof(previousTouch));
            memset(&touch, 0, sizeof(touch));
            previous_page_touch_x = previous_page_touch_y = -1;
            page_pan_start_x = page_pan_start_y = -1;
            horizontal_touch_drag = 0;
            scrollbar_drag_axis = 0;
            scrollbar_drag_start = scrollbar_drag_scroll = scrollbar_drag_max = 0.0f;
            menu_cross_latched = 0;
            write_runtime_log("power: app resumed; controller/touch sampling restored");
        }
        if(vita_read_controller(&current)<=0){
            memset(&current,0,sizeof(current));
        }
        button_wait_override_update_keyboard();
        open_url_ime_update(host);
        if(g_menu.open_url_ime_active){
            previous=current;
            sceKernelDelayThread(16667);
            continue;
        }
        if(g_menu.button_wait_ime_active){
            previous=current;
            sceKernelDelayThread(16667);
            continue;
        }
        /* Menu navigation is edge-triggered: only a newly pressed button
         * may activate a native menu control.  The webpage input layer still
         * receives the full press/release state below. */
        unsigned pressed = current.buttons & ~previous.buttons;

        /* START belongs to the page, not the homebrew shell. Dispatch it
         * independently of control mode so touch, cursor, browser and
         * standalone pages can all consume it. */
        if (g_menu.mode==MENU_CLOSED && !g_menu.open_url_ime_active &&
            !g_menu.button_wait_ime_active) {
            int start_event=page_button_event(host,SCE_CTRL_START,
                !!(current.buttons&SCE_CTRL_START),
                (uint64_t)sceKernelGetProcessTimeWide());
            if(start_event==1)send_button(host,"START",1);
            else if(start_event==-1)send_button(host,"START",0);
        }

        /* Debug overlay: remember the newest button press by name. */
        if(pressed){
            static const struct{unsigned m;const char *n;} dbg_btns[]={
                {SCE_CTRL_SELECT,"select"},{SCE_CTRL_START,"start"},
                {SCE_CTRL_CROSS,"cross"},{SCE_CTRL_CIRCLE,"circle"},
                {SCE_CTRL_SQUARE,"square"},{SCE_CTRL_TRIANGLE,"triangle"},
                {SCE_CTRL_UP,"up"},{SCE_CTRL_RIGHT,"right"},
                {SCE_CTRL_DOWN,"down"},{SCE_CTRL_LEFT,"left"},
                {SCE_CTRL_LTRIGGER,"l"},{SCE_CTRL_RTRIGGER,"r"}
            };
            unsigned bi;
            for(bi=0;bi<sizeof(dbg_btns)/sizeof(dbg_btns[0]);bi++)
                if(pressed&dbg_btns[bi].m){
                    snprintf(g_menu.dbg_lastbtn,sizeof(g_menu.dbg_lastbtn),"%s",dbg_btns[bi].n);
                    break;
                }
        }

        /* CIRCLE is the universal native-menu back operation.  Keeping this
         * outside individual tabs prevents a template change from silently
         * breaking the back path on one inset menu. */
        if(g_menu.mode!=MENU_CLOSED && (pressed&SCE_CTRL_CIRCLE)){
            if(g_menu.mode==MENU_DEVTOOLS){
                if(g_menu.selected==7){
                    g_menu.selected=g_menu.exit_confirm_return_section;
                    g_menu.dev_section=g_menu.exit_confirm_return_section;
                    g_menu.dev_option=0;
                }else {
                    g_menu.mode=MENU_MAIN; g_menu.selected=0;
                }
                g_menu.devtools_scroll=0.0f;
                g_menu.devtools_scroll_drag=0;
            }else if(g_menu.mode==MENU_SETTINGS){
                g_menu.mode=MENU_MAIN; g_menu.selected=0;
            }else if(g_menu.mode==MENU_PAGEOPS){
                g_menu.mode=MENU_MAIN; g_menu.selected=0;
#if CWV_FILES_ENABLED
            }else if(g_menu.mode==MENU_FILES){
                g_menu.mode=MENU_MAIN; g_menu.selected=0;
                g_menu.files_scroll_drag=0;
#else
            }else if(g_menu.mode==MENU_FILES){
                g_menu.mode=MENU_MAIN; g_menu.selected=0;
#endif
            }else if(g_menu.mode==MENU_MAIN){
                g_menu.mode=MENU_CLOSED;
            }
            previous=current;
            continue;
        }

        /* Native browser-style menu. DevTools is now a submenu of this menu;
         * the menu is opened with L + R + SELECT. */
        unsigned menu_combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
        static int menu_combo_latched = 0;
        static int menu_lanalog_latched = 0;
        if ((current.buttons & menu_combo) == menu_combo) {
            if (!menu_combo_latched) { page_button_reset_all(); g_menu.mode = (g_menu.mode==MENU_CLOSED)?MENU_MAIN:MENU_CLOSED; g_menu.selected=0; g_menu.settings_scroll=0.0f; g_menu.settings_scroll_drag=0; g_menu.devtools_scroll=0.0f; g_menu.devtools_scroll_drag=0; menu_combo_latched=1; }
        } else menu_combo_latched = 0;

#if CWV_DEBUG
        /* Debug overlay lives on the page, so it only reacts while no
         * native menu is open.  SELECT alone toggles it; SELECT+X opens
         * the row-customize mode; there the d-pad moves the cursor and
         * X flips the row.  L/R+SELECT (the menu combo) is excluded so
         * opening the menu never flickers the overlay.
         * PRODUCTION BUILDS: this whole block is compiled out - SELECT
         * never touches the overlay (see CWV_DEBUG above). */
        if(g_menu.mode==MENU_CLOSED){
            const int lr_held=(current.buttons&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))!=0;
            if((pressed&SCE_CTRL_SELECT)&&!lr_held){
                if(g_menu.dbg_custom) g_menu.dbg_custom=0;
                else { g_menu.dbg_overlay=!g_menu.dbg_overlay; settings_save(); }
            }
            if(g_menu.dbg_overlay&&!g_menu.dbg_custom&&!lr_held&&
               (current.buttons&SCE_CTRL_SELECT)&&(pressed&SCE_CTRL_CROSS)){
                g_menu.dbg_custom=1;
                g_menu.dbg_item=0;
            }
            if(g_menu.dbg_custom){
                if(pressed&SCE_CTRL_UP)   g_menu.dbg_item=(g_menu.dbg_item+DBG_COUNT-1)%DBG_COUNT;
                if(pressed&SCE_CTRL_DOWN) g_menu.dbg_item=(g_menu.dbg_item+1)%DBG_COUNT;
                if((pressed&SCE_CTRL_CROSS)&&!(current.buttons&SCE_CTRL_SELECT))
                    g_menu.dbg_mask^=(1<<g_menu.dbg_item);
            }
        }
#endif /* CWV_DEBUG */

        /* Settings uses two physical navigation layers:
         *   - D-pad selects the inset, non-button settings rows (3..7).
         *   - L Analog selects the left-side action buttons (0..2).
         * This keeps Save/Undo/Back separate from value browsing and makes it
         * possible to browse settings without accidentally activating actions. */
        if(g_menu.mode==MENU_SETTINGS){
            int ly=(int)current.ly;
            if((ly<82 || ly>174)){
                if(!menu_lanalog_latched){
                    if(ly<82){
                        if(g_menu.selected>=0 && g_menu.selected<=2) g_menu.selected=(g_menu.selected+2)%3;
                        else g_menu.selected=2;
                    }else{
                        if(g_menu.selected>=0 && g_menu.selected<=2) g_menu.selected=(g_menu.selected+1)%3;
                        else g_menu.selected=0;
                    }
                    menu_lanalog_latched=1;
                }
            }else{
                menu_lanalog_latched=0;
            }
        }
        if (g_menu.mode!=MENU_CLOSED && (pressed & (SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_TRIANGLE))) {
            if(g_menu.mode==MENU_DEVTOOLS){
                /* L/R always switch the top-level tabs, including while the
                 * Overrides tab is open.  The old implementation let the
                 * override editor consume these buttons instead. */
                if(pressed & (SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){
                    if(current.buttons & SCE_CTRL_LTRIGGER){
                        g_menu.selected=(g_menu.selected+7)%8;
                    }
                    if(current.buttons & SCE_CTRL_RTRIGGER){
                        g_menu.selected=(g_menu.selected+1)%8;
                    }
                    if(g_menu.selected==7){
                        g_menu.exit_confirm_return_section=g_menu.dev_section;
                        g_menu.exit_confirm_selected=0;
                        g_menu.devtools_scroll=0.0f;
                    }else{
                        g_menu.dev_section=g_menu.selected;
                        g_menu.dev_option=0;
                        g_menu.devtools_scroll=0.0f;
                    }
                }
                if(g_menu.selected==8){
                    if(current.buttons & SCE_CTRL_CROSS){
                        g_menu.mode=MENU_MAIN; g_menu.selected=0; g_menu.devtools_scroll=0.0f;
                    }
                }else if(g_menu.selected==7){
                    if(current.buttons & (SCE_CTRL_LEFT|SCE_CTRL_RIGHT))
                        g_menu.exit_confirm_selected = !g_menu.exit_confirm_selected;
                    if((current.buttons&SCE_CTRL_UP) || (current.buttons&SCE_CTRL_DOWN))
                        g_menu.exit_confirm_selected = !g_menu.exit_confirm_selected;
                    if((current.buttons&SCE_CTRL_CROSS)){
                        if(g_menu.exit_confirm_selected){
                            g_menu.exit_requested=1;
                            g_menu.mode=MENU_CLOSED;
                        }else{
                            g_menu.selected=g_menu.exit_confirm_return_section;
                            g_menu.dev_section=g_menu.exit_confirm_return_section;
                            g_menu.dev_option=0;
                        }
                    }
                }else if(g_menu.dev_section==4){
                    if(current.buttons & SCE_CTRL_UP){ if(g_menu.dev_option>0)g_menu.dev_option--; }
                    if(current.buttons & SCE_CTRL_DOWN){ if(g_menu.dev_option<6)g_menu.dev_option++; }
                    float target=g_menu.dev_option*42.0f;
                    const float list_view=210.0f;
                    if(target<g_menu.devtools_scroll)g_menu.devtools_scroll=target;
                    if(target+42.0f>g_menu.devtools_scroll+list_view)g_menu.devtools_scroll=target+42.0f-list_view;
                    clamp_devtools_scroll();
                }else if(g_menu.dev_section==5){
                    /* Console tab: up/down pages the log (held to repeat),
                     * L/R still switches the DevTools tabs. */
                    if(current.buttons & SCE_CTRL_UP){
                        g_menu.devtools_scroll-=DEV_CONSOLE_ROW_H*3.0f;
                        clamp_devtools_scroll();
                    }
                    if(current.buttons & SCE_CTRL_DOWN){
                        g_menu.devtools_scroll+=DEV_CONSOLE_ROW_H*3.0f;
                        clamp_devtools_scroll();
                    }
                }else{
                    if(current.buttons & SCE_CTRL_UP){
                        if(g_menu.selected>0) g_menu.selected--;
                        else g_menu.selected=8;
                    }
                    if(current.buttons & SCE_CTRL_DOWN){
                        if(g_menu.selected<8) g_menu.selected++;
                        else g_menu.selected=0;
                    }
                    if(g_menu.selected==7){
                        g_menu.exit_confirm_return_section=g_menu.dev_section;
                        g_menu.exit_confirm_selected=0;
                        g_menu.devtools_scroll=0.0f;
                    }else{
                        g_menu.dev_section=g_menu.selected;
                        g_menu.dev_option=0;
                    }
                }
            } else if(g_menu.mode==MENU_PAGEOPS){
                if(current.buttons & SCE_CTRL_UP){
                    g_menu.pageops_selected=(g_menu.pageops_selected+3)%4;
                }
                if(current.buttons & SCE_CTRL_DOWN){
                    g_menu.pageops_selected=(g_menu.pageops_selected+1)%4;
                }
#if CWV_FILES_ENABLED
            } else if(g_menu.mode==MENU_FILES){
                if(pressed & (SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){
                    if(current.buttons & SCE_CTRL_LTRIGGER)
                        g_menu.files_selected-=10;
                    if(current.buttons & SCE_CTRL_RTRIGGER)
                        g_menu.files_selected+=10;
                    if(g_menu.files_selected<0)g_menu.files_selected=0;
                    if(g_files_count>0&&g_menu.files_selected>=g_files_count)
                        g_menu.files_selected=g_files_count-1;
                    files_scroll_to_selection();
                }
                if(current.buttons & SCE_CTRL_UP){
                    if(g_menu.files_selected>0)g_menu.files_selected--;
                    files_scroll_to_selection();
                }
                if(current.buttons & SCE_CTRL_DOWN){
                    if(g_menu.files_selected<g_files_count-1)g_menu.files_selected++;
                    files_scroll_to_selection();
                }
#endif /* CWV_FILES_ENABLED */
            } else if(g_menu.mode==MENU_SETTINGS){
                if(pressed & (SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){
                    if(current.buttons & SCE_CTRL_LTRIGGER) settings_select_tab(g_menu.settings_tab-1);
                    if(current.buttons & SCE_CTRL_RTRIGGER) settings_select_tab(g_menu.settings_tab+1);
                }
                if(current.buttons & SCE_CTRL_UP){
                    settings_select_content(g_menu.selected<=3 ? 7 : g_menu.selected-1);
                    settings_ensure_selected_visible();
                }
                if(current.buttons & SCE_CTRL_DOWN){
                    settings_select_content(g_menu.selected>=7 ? 3 : g_menu.selected+1);
                    settings_ensure_selected_visible();
                }
                if(current.buttons & SCE_CTRL_LEFT){
                    if(g_menu.settings_tab==0 && g_menu.selected==3){g_menu.dark_mode=!g_menu.dark_mode;g_menu.dirty=1;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==4){g_menu.accent=(g_menu.accent+5)%6;g_menu.dirty=1;}
#if CWV_ZOOM_ENABLED
                    else if(g_menu.settings_tab==0 && g_menu.selected==5){ settings_zoom_step(-1,host); }
                    else if(g_menu.settings_tab==1 && g_menu.selected==4){g_menu.zoom_mode=(g_menu.zoom_mode+2)%3;g_menu.dirty=1;}
#endif
#if CWV_SCROLL_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==3){g_menu.scroll_mode=(g_menu.scroll_mode+2)%3;if(g_menu.scroll_mode==2)cwv_host_focus_clear(host);g_menu.dirty=1;}
                    else if(g_menu.settings_tab==1 && g_menu.selected==5){g_menu.scroll_speed=(g_menu.scroll_speed+2)%3;g_menu.dirty=1;}
#endif
#if CWV_CURSOR_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==7){g_menu.pointer=!g_menu.pointer;g_menu.dirty=1;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==6){g_menu.css_cursors=(g_menu.css_cursors?0:1);g_menu.dirty=1;}
#endif
                }
                if(current.buttons & SCE_CTRL_RIGHT){
                    if(g_menu.settings_tab==0 && g_menu.selected==3){g_menu.dark_mode=!g_menu.dark_mode;g_menu.dirty=1;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==4){g_menu.accent=(g_menu.accent+1)%6;g_menu.dirty=1;}
#if CWV_ZOOM_ENABLED
                    else if(g_menu.settings_tab==0 && g_menu.selected==5){ settings_zoom_step(1,host); }
                    else if(g_menu.settings_tab==1 && g_menu.selected==4){g_menu.zoom_mode=(g_menu.zoom_mode+1)%3;g_menu.dirty=1;}
#endif
#if CWV_SCROLL_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==3){g_menu.scroll_mode=(g_menu.scroll_mode+1)%3;if(g_menu.scroll_mode==2)cwv_host_focus_clear(host);g_menu.dirty=1;}
                    else if(g_menu.settings_tab==1 && g_menu.selected==5){g_menu.scroll_speed=(g_menu.scroll_speed+1)%3;g_menu.dirty=1;}
#endif
#if CWV_CURSOR_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==7){g_menu.pointer=!g_menu.pointer;g_menu.dirty=1;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==6){g_menu.css_cursors=(g_menu.css_cursors?0:1);g_menu.dirty=1;}
#endif
                }
            } else {
                if(current.buttons & SCE_CTRL_UP){ if(g_menu.selected>0)g_menu.selected--; }
                if(current.buttons & SCE_CTRL_DOWN){
                    g_menu.selected++;
                    if(g_menu.mode==MENU_MAIN&&g_menu.selected>3)g_menu.selected=0;
                    if(g_menu.mode==MENU_DEVTOOLS&&g_menu.selected>8)g_menu.selected=0;
                    if(g_menu.mode==MENU_DEVTOOLS && g_menu.selected<7){
                        /* Top tabs are the focus targets now; keep the visual
                           active tab synchronized with D-pad navigation. */
                        g_menu.dev_section=g_menu.selected;
                        g_menu.dev_option=0;
                    }
                }
            }
            if((current.buttons & SCE_CTRL_CROSS) && !menu_cross_latched){
                menu_cross_latched = 1;
                if(g_menu.mode==MENU_MAIN){
                    if(g_menu.selected==0){
                        native_menu_open(CWV_PAGE_OPERATIONS_MENU_MODE);
                    }else if(g_menu.selected==1){
                        open_url_ime_start(host);
                    }else if(g_menu.selected==2){
                        native_menu_open(CWV_SETTINGS_MENU_MODE);
                    }else if(g_menu.selected==3){
                        g_menu.mode=MENU_CLOSED;
                    }
                }else if(g_menu.mode==MENU_PAGEOPS){
#if CWV_HISTORY_ENABLED
                    if(g_menu.pageops_selected==0) browser_back(host);
                    else if(g_menu.pageops_selected==1) browser_forward(host);
                    else
#endif
                    if(g_menu.pageops_selected==2) browser_refresh(host);
                    else { g_menu.mode=MENU_MAIN; g_menu.selected=0; }
                }else if(g_menu.mode==MENU_SETTINGS){
                    if(g_menu.selected==0){settings_load(); (void)cwv_host_set_zoom(host,g_menu.zoom_level);}
                    else if(g_menu.selected==1){settings_save();}
                    else if(g_menu.selected==2){g_menu.mode=MENU_MAIN;g_menu.selected=0;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==3){g_menu.dark_mode=!g_menu.dark_mode;g_menu.dirty=1;}
                    else if(g_menu.settings_tab==0 && g_menu.selected==4){g_menu.accent=(g_menu.accent+1)%6;g_menu.dirty=1;}
#if CWV_ZOOM_ENABLED
                    else if(g_menu.settings_tab==0 && g_menu.selected==5){/* slider is adjusted with LEFT/RIGHT */}
                    else if(g_menu.settings_tab==1 && g_menu.selected==4){g_menu.zoom_mode=(g_menu.zoom_mode+1)%3;g_menu.dirty=1;}
#endif
#if CWV_CURSOR_ENABLED
                    else if(g_menu.settings_tab==0 && g_menu.selected==6){g_menu.css_cursors=(g_menu.css_cursors?0:1);g_menu.dirty=1;}
#endif
#if CWV_SCROLL_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==3){g_menu.scroll_mode=(g_menu.scroll_mode+1)%3;if(g_menu.scroll_mode==2)cwv_host_focus_clear(host);g_menu.dirty=1;}
                    else if(g_menu.settings_tab==1 && g_menu.selected==5){g_menu.scroll_speed=(g_menu.scroll_speed+1)%3;g_menu.dirty=1;}
#endif
#if CWV_CURSOR_ENABLED
                    else if(g_menu.settings_tab==1 && g_menu.selected==7){g_menu.pointer=!g_menu.pointer;g_menu.dirty=1;}
#endif
                    else if(g_menu.settings_tab==2 && g_menu.selected==4){settings_clear_cache();}
                    else if(g_menu.settings_tab==2 && g_menu.selected==5){settings_reset_preferences(host);}
#if CWV_FILES_ENABLED
                }else if(g_menu.mode==MENU_FILES){
                    files_open_entry(host);
#endif /* CWV_FILES_ENABLED */
                }else{
                    if(g_menu.selected==8){
                        g_menu.mode=MENU_MAIN; g_menu.selected=0;
                    }else if(g_menu.selected==7){
                        /* Close is a confirmation screen, not an immediate exit. */
                    }else if(g_menu.dev_section==4){ /* CROSS edits the selected override below. */ }
                    else if(g_menu.selected<7){g_menu.dev_section=g_menu.selected;g_menu.dev_option=0;}
                    else {g_menu.mode=MENU_MAIN;g_menu.selected=0;g_menu.devtools_scroll=0.0f;g_menu.devtools_scroll_drag=0;}
                }
            }
            if(g_menu.mode==MENU_DEVTOOLS && !g_menu.exit_requested && g_menu.dev_section==4){
                if(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS)){
                    int dir=(current.buttons&SCE_CTRL_LEFT)?-1:1;
                    if(g_menu.dev_option==0){ g_menu.override_vcm = cycle_control_override(g_menu.override_vcm,dir); }
                    else if(g_menu.dev_option==1){ g_menu.override_scroll = (g_menu.override_scroll<0)?0:(g_menu.override_scroll+dir+3)%3; }
                    else if(g_menu.dev_option==2){ g_menu.override_zoom = (g_menu.override_zoom<0)?0:(g_menu.override_zoom+dir+3)%3; }
                    else if(g_menu.dev_option==3){ g_menu.override_theme = (g_menu.override_theme<0)?effective_dark_mode():(1-g_menu.override_theme); }
                    else if(g_menu.dev_option==4){ g_menu.override_scrollbars = (g_menu.override_scrollbars<0)?0:(g_menu.override_scrollbars+1)%2; }
                    else if(g_menu.dev_option==5){ g_menu.override_zoom_level = cycle_zoom_override(g_menu.override_zoom_level,dir); }
                    else if(g_menu.dev_option==6){
                        if(current.buttons&SCE_CTRL_CROSS){
                            (void)button_wait_override_open_keyboard();
                        }else{
                            int preset=cycle_button_wait_preset(g_menu.override_button_wait<4?g_menu.override_button_wait:-1,dir);
                            g_menu.override_button_wait=preset;
                            if(preset<0)g_menu.override_button_wait_spec[0]=0;
                            else {
                                static const char *presets[]={"release","none","1s","1.5s 0.5s"};
                                snprintf(g_menu.override_button_wait_spec,sizeof(g_menu.override_button_wait_spec),"%s",presets[preset]);
                            }
                        }
                    }
                }
            }
            if(g_menu.mode==MENU_DEVTOOLS && !g_menu.exit_requested && g_menu.dev_section==4 && (pressed&SCE_CTRL_TRIANGLE)){
                g_menu.override_vcm=-1;g_menu.override_scroll=-1;g_menu.override_zoom=-1;g_menu.override_theme=-1;g_menu.override_zoom_level=-1.0f;g_menu.override_scrollbars=-1;g_menu.override_button_wait=-1;g_menu.override_button_wait_spec[0]=0;
            }
            if(pressed&SCE_CTRL_CIRCLE){
                if(g_menu.mode==MENU_PAGEOPS){
                    g_menu.mode=MENU_MAIN;
                    g_menu.selected=0;
                }else if(g_menu.mode==MENU_SETTINGS){
                    g_menu.mode=MENU_MAIN;
                    g_menu.selected=0;
                }else if(g_menu.mode==MENU_DEVTOOLS){
                    if(g_menu.selected==7){
                        g_menu.selected=g_menu.exit_confirm_return_section;
                        g_menu.dev_section=g_menu.exit_confirm_return_section;
                        g_menu.dev_option=0;
                    }else{
                        g_menu.mode=MENU_MAIN;
                        g_menu.selected=0;
                    }
                }else{
                    g_menu.mode=MENU_CLOSED;
                }
            }
        }

        if (!(current.buttons & SCE_CTRL_CROSS)) menu_cross_latched = 0;

        apply_runtime_overrides(host);

        if (g_menu.mode==MENU_CLOSED && !g_menu.dbg_custom) {
            int vcm = effective_control_mode(host);
            uint64_t input_now=(uint64_t)sceKernelGetProcessTimeWide();
            /* leaving cursor mode mid-press: cancel the synthetic tap so a
             * page swap can never strand a pointer-down (2026-09-25 (40)). */
            if (vcm != VCM_CURSOR && g_cursor_tap) {
                g_cursor_tap = 0;
                (void)cwv_host_pointer_cancel(host);
            }

            if(vcm==VCM_BROWSER){
                int e;
                e=page_button_event(host,SCE_CTRL_DOWN,!!(current.buttons&SCE_CTRL_DOWN),input_now); if(e==1){(void)cwv_host_focus_cycle(host,+1);send_key(host,"Tab",0,1);} else if(e==-1)send_key(host,"Tab",0,0);
                e=page_button_event(host,SCE_CTRL_UP,!!(current.buttons&SCE_CTRL_UP),input_now); if(e==1){(void)cwv_host_focus_cycle(host,-1);send_key(host,"Tab",1,1);} else if(e==-1)send_key(host,"Tab",1,0);
                e=page_button_event(host,SCE_CTRL_CROSS,!!(current.buttons&SCE_CTRL_CROSS),input_now);
                if(e==1)(void)cwv_host_focus_activate(host);
                else if(e==-1)cwv_host_focus_release(host);
                e=page_button_event(host,SCE_CTRL_CIRCLE,!!(current.buttons&SCE_CTRL_CIRCLE),input_now); if(e==1)browser_back(host);
                e=page_button_event(host,SCE_CTRL_SQUARE,!!(current.buttons&SCE_CTRL_SQUARE),input_now); if(e==1){g_menu.mode=MENU_MAIN;g_menu.selected=0;}
                e=page_button_event(host,SCE_CTRL_TRIANGLE,!!(current.buttons&SCE_CTRL_TRIANGLE),input_now); if(e==1)browser_refresh(host);
                e=page_button_event(host,SCE_CTRL_LTRIGGER,!!(current.buttons&SCE_CTRL_LTRIGGER),input_now); if(e==1)browser_back(host);
                e=page_button_event(host,SCE_CTRL_RTRIGGER,!!(current.buttons&SCE_CTRL_RTRIGGER),input_now); if(e==1)browser_forward(host);
            } else if(vcm==VCM_STANDALONE){
                const unsigned btns[]={SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,SCE_CTRL_SQUARE,SCE_CTRL_TRIANGLE,SCE_CTRL_UP,SCE_CTRL_RIGHT,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_LTRIGGER,SCE_CTRL_RTRIGGER};
                const char *names[]={"cross","circle","square","triangle","up","right","down","left","l","r"};
                for(unsigned i=0;i<sizeof(btns)/sizeof(btns[0]);i++){
                    int e=page_button_event(host,btns[i],!!(current.buttons&btns[i]),input_now);
                    if(e==1)(void)cwv_host_vcm_activate(host,names[i]);
                    else if(e==-1 && btns[i]==SCE_CTRL_CROSS)
                        cwv_host_focus_release(host);
                }
#if CWV_CURSOR_ENABLED
            } else if(vcm==VCM_CURSOR){
                /* Cursor page mode (cwv-control-mode="cursor", 2026-09-25 (40)):
                 * the whole pad becomes a mouse.  Left stick + D-pad move the
                 * shared pointer cursor, CROSS taps what is under it (hold it
                 * while moving to drag), CIRCLE goes back, TRIANGLE refreshes,
                 * SQUARE opens the menu, and the shoulders keep history
                 * duties.  The native scroll/zoom axes below are skipped so
                 * the stick stays the pointer. */
                {
                    int cx=SCREEN_W/2, cy=SCREEN_H/2;
                    int have=cwv_cursor_position(&g_cursor,&cx,&cy);
                    int e;
                    if(!have){ cx=SCREEN_W/2; cy=SCREEN_H/2; }
                    /* analog: left stick, same deadzone model as scrolling */
                    {
                        int dx=(int)current.lx-128, dy=(int)current.ly-128;
                        const int dz=18;
                        if(dx<-dz||dx>dz){
                            int m=(dx<0?-dx:dx)-dz;
                            float st=(float)m/109.0f*8.0f;
                            cx += dx>0 ? (int)(st+0.5f) : -(int)(st+0.5f);
                        }
                        if(dy<-dz||dy>dz){
                            int m=(dy<0?-dy:dy)-dz;
                            float st=(float)m/109.0f*8.0f;
                            cy += dy>0 ? (int)(st+0.5f) : -(int)(st+0.5f);
                        }
                    }
                    /* D-pad steps on press edges */
                    e=page_button_event(host,SCE_CTRL_LEFT,!!(current.buttons&SCE_CTRL_LEFT),input_now);  if(e==1)cx-=14;
                    e=page_button_event(host,SCE_CTRL_RIGHT,!!(current.buttons&SCE_CTRL_RIGHT),input_now); if(e==1)cx+=14;
                    e=page_button_event(host,SCE_CTRL_UP,!!(current.buttons&SCE_CTRL_UP),input_now);        if(e==1)cy-=14;
                    e=page_button_event(host,SCE_CTRL_DOWN,!!(current.buttons&SCE_CTRL_DOWN),input_now);   if(e==1)cy+=14;
                    cwv_cursor_place(&g_cursor,(float)cx,(float)cy,SCREEN_W,SCREEN_H);
                    cwv_cursor_show(&g_cursor,1);
                    /* CROSS = the mouse button.  Down taps the page at the
                     * cursor; up releases it (tap when it stayed put, cancel
                     * when it moved = drag), mirroring the touchpad path. */
                    e=page_button_event(host,SCE_CTRL_CROSS,!!(current.buttons&SCE_CTRL_CROSS),input_now);
                    if(e==1){
                        g_cursor_tap=1; g_cursor_tap_x=cx; g_cursor_tap_y=cy;
                        (void)cwv_host_pointer_event(host,cx,cy,1);
                    } else if(e==-1 && g_cursor_tap){
                        g_cursor_tap=0;
                        {
                            int tx=cx-g_cursor_tap_x, ty=cy-g_cursor_tap_y;
                            if(tx*tx+ty*ty < 12*12){
                                char script[192];
                                (void)cwv_host_pointer_event(host,cx,cy,0);
                                snprintf(script,sizeof(script),
                                    "window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'up',false)",cx,cy);
                                (void)cwv_host_eval(host,script);
                            } else (void)cwv_host_pointer_cancel(host);
                        }
                    }
                    /* the rest of the pad keeps its Browser-mode duties */
                    e=page_button_event(host,SCE_CTRL_CIRCLE,!!(current.buttons&SCE_CTRL_CIRCLE),input_now); if(e==1)browser_back(host);
                    e=page_button_event(host,SCE_CTRL_SQUARE,!!(current.buttons&SCE_CTRL_SQUARE),input_now); if(e==1){g_menu.mode=MENU_MAIN;g_menu.selected=0;}
                    e=page_button_event(host,SCE_CTRL_TRIANGLE,!!(current.buttons&SCE_CTRL_TRIANGLE),input_now); if(e==1)browser_refresh(host);
                    e=page_button_event(host,SCE_CTRL_LTRIGGER,!!(current.buttons&SCE_CTRL_LTRIGGER),input_now); if(e==1)browser_back(host);
                    e=page_button_event(host,SCE_CTRL_RTRIGGER,!!(current.buttons&SCE_CTRL_RTRIGGER),input_now); if(e==1)browser_forward(host);
                }
#endif /* CWV_CURSOR_ENABLED */
            } else if(vcm!=VCM_TOUCH){
                if(effective_scroll_mode() != 2) {
                    int e=page_button_event(host,SCE_CTRL_DOWN,!!(current.buttons&SCE_CTRL_DOWN),input_now); if(e==1){(void)cwv_host_focus_cycle(host,+1);send_key(host,"Tab",0,1);} else if(e==-1)send_key(host,"Tab",0,0);
                    e=page_button_event(host,SCE_CTRL_UP,!!(current.buttons&SCE_CTRL_UP),input_now); if(e==1){(void)cwv_host_focus_cycle(host,-1);send_key(host,"Tab",1,1);} else if(e==-1)send_key(host,"Tab",1,0);
                }
                const unsigned btns[]={SCE_CTRL_CROSS,SCE_CTRL_CIRCLE,SCE_CTRL_SQUARE,SCE_CTRL_TRIANGLE,SCE_CTRL_UP,SCE_CTRL_RIGHT,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_LTRIGGER,SCE_CTRL_RTRIGGER};
                const char *names[]={"CROSS","CIRCLE","SQUARE","TRIANGLE","DPAD_UP","DPAD_RIGHT","DPAD_DOWN","DPAD_LEFT","LTRIGGER","RTRIGGER"};
                for(unsigned i=0;i<sizeof(btns)/sizeof(btns[0]);i++){
                    int e=page_button_event(host,btns[i],!!(current.buttons&btns[i]),input_now);
                    if(e==1){
                        send_button(host,names[i],1);
                        /* CROSS also activates the focused DPAD stop, so
                           every tab-selectable element is X-clickable in
                           this mode too: links navigate, buttons press,
                           selects open, textfields open the IME. */
                        if(btns[i]==SCE_CTRL_CROSS) (void)cwv_host_focus_activate(host);
                    } else if(e==-1) {
                        send_button(host,names[i],0);
                        if(btns[i]==SCE_CTRL_CROSS) cwv_host_focus_release(host);
                    }
                }
            }
        }

        previous = current;

        int scroll_touch_active = 0;
        int scroll_touch_x = 0;
        int scroll_touch_y = 0;
        memset(&touch, 0, sizeof(touch));

        /* Front touchscreen: native menu handling always takes priority over
         * page input. This deliberately ignores cwv-control-mode because the
         * menu belongs to the Vita shell, not the webpage. */
        if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
            SceTouchPanelInfo panel;
            memset(&panel, 0, sizeof(panel));
            if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel) >= 0) {
                int active = touch.reportNum > 0;
                int x = active ? touch.report[0].x : (previousTouch.reportNum>0 ? previousTouch.report[0].x : 0);
                int y = active ? touch.report[0].y : (previousTouch.reportNum>0 ? previousTouch.report[0].y : 0);
                int min_x=panel.minAaX,min_y=panel.minAaY,max_x=panel.maxAaX,max_y=panel.maxAaY;
                if (max_x <= min_x)
                    max_x = min_x + 1;
                if (max_y <= min_y)
                    max_y = min_y + 1;
                int px=(x-min_x)*SCREEN_W/(max_x-min_x), py=(y-min_y)*SCREEN_H/(max_y-min_y);
                if (px < 0)
                    px = 0;
                if (py < 0)
                    py = 0;
                if (px >= SCREEN_W)
                    px = SCREEN_W - 1;
                if (py >= SCREEN_H)
                    py = SCREEN_H - 1;

                if(g_menu.mode!=MENU_CLOSED || g_menu.touch_capture){
                    if(active){
                        if(!g_menu.touch_capture){g_menu.touch_capture=1;handle_menu_touch(px,py,host);}
                        else if(g_menu.mode==MENU_SETTINGS && g_menu.settings_scroll_drag) handle_menu_touch(px,py,host);
                        else if(g_menu.mode==MENU_DEVTOOLS && g_menu.devtools_scroll_drag) handle_menu_touch(px,py,host);
#if CWV_FILES_ENABLED
                        else if(g_menu.mode==MENU_FILES && g_menu.files_scroll_drag) handle_menu_touch(px,py,host);
#endif /* CWV_FILES_ENABLED */
                    }else if(g_menu.touch_capture){
                        g_menu.touch_capture=0;
                        g_menu.settings_scroll_drag=0;
                        g_menu.devtools_scroll_drag=0;
#if CWV_FILES_ENABLED
                        g_menu.files_scroll_drag=0;
#endif /* CWV_FILES_ENABLED */
                    }
                    scroll_touch_active=0;
                    scroll_touch_x=-1;
                }else if(active){
                    scroll_touch_active=1; scroll_touch_x=px; scroll_touch_y=py;
                    int was_active=previousTouch.reportNum>0;
                    
                    /* Scrollbar thumbs are native chrome.  Grab them before
                     * forwarding the touch to the webpage. */
                    if (!was_active && scrollbar_drag_axis==0) {
                        /* 2D pan (2026-10-06 (74)): remember where the
                         * finger went down; the pan latch below engages on
                         * 7px TOTAL displacement from THIS point. */
                        page_pan_start_x=px; page_pan_start_y=py;
                        int axis=0; float ts=0.0f, th=0.0f, ms=0.0f;
                        if (scrollbar_hit(host,px,py,&axis,&ts,&th,&ms)) {
                            scrollbar_drag_max=ms;
                            scrollbar_drag_axis=axis;
                            scrollbar_drag_start=(axis==1)?(float)py:(float)px;
                            scrollbar_drag_scroll=(axis==1)?
                                cwv_host_scroll_offset(host):
                                cwv_host_scroll_x_offset(host);
                            (void)cwv_host_pointer_cancel(host);
                        }
                    }
                    int old_page_touch_x=previous_page_touch_x;
                    int old_page_touch_y=previous_page_touch_y;   /* 2D pan (74) */
                    if (scrollbar_drag_axis) {
                        float pos=(scrollbar_drag_axis==1)?(float)py:(float)px;
                        float cw=cwv_host_content_width(host);
                        float ch=cwv_host_content_height(host);
                        const float inset=12.0f;
                        int vertical=ch>(float)SCREEN_H-2.0f*inset;
                        int horizontal=cw>(float)SCREEN_W-2.0f*inset;
                        if(horizontal && ch>(float)SCREEN_H-2.0f*inset-8.0f) vertical=1;
                        if(vertical && cw>(float)SCREEN_W-2.0f*inset-8.0f) horizontal=1;
                        float track=(scrollbar_drag_axis==1)?(float)SCREEN_H-(horizontal?8.0f:0.0f):(float)SCREEN_W-(vertical?8.0f:0.0f);
                        float content=(scrollbar_drag_axis==1)?ch:cw;
                        float viewport=(scrollbar_drag_axis==1)?(float)SCREEN_H-2.0f*inset-(horizontal?8.0f:0.0f):(float)SCREEN_W-2.0f*inset-(vertical?8.0f:0.0f);
                        float thumb=track*(viewport/content);
                        if (thumb<24.0f) thumb=24.0f;
                        if (thumb>track) thumb=track;
                        float travel=track-thumb;
                        float delta=(travel>0.0f)?
                            (pos-scrollbar_drag_start)*(scrollbar_drag_max/travel):0.0f;
                        if (scrollbar_drag_axis==1)
                            (void)cwv_host_scroll_to(host,scrollbar_drag_scroll+delta);
                        else
                            (void)cwv_host_scroll_page_x_by(host,
                                (scrollbar_drag_scroll+delta) -
                                cwv_host_scroll_x_offset(host));
                    } else if(was_active && old_page_touch_x>=0){
                        /* 2D pan (2026-10-06 (74)): engage when the finger
                         * has moved >=7px from the touch START on EITHER
                         * axis (the same threshold the Y scroll module
                         * uses), then track BOTH axes EVERY frame.  The
                         * old code only scrolled x on frames with a
                         * per-frame |dx|>=7, so a slow circular motion
                         * moved y smoothly but never (or in jumpy 7px
                         * steps) moved x - now a smooth circular gesture
                         * scrolls both axes at the same time. */
                        int dx=px-old_page_touch_x;
                        if(!horizontal_touch_drag){
                            int tdx=px-page_pan_start_x;
                            int tdy=py-page_pan_start_y;
                            if((tdx<0?-tdx:tdx)>=7 || (tdy<0?-tdy:tdy)>=7){
                                horizontal_touch_drag=1;
                                (void)cwv_host_pointer_cancel(host);
                            }
                        }
                        if(horizontal_touch_drag){
                            (void)cwv_host_scroll_x_at(host,(float)(-dx),px,py);
                            if(!scroll_enabled){
                                /* module absent (degraded build): the
                                 * native path pans Y as well, so both
                                 * axes work without the module. */
                                int dy=py-old_page_touch_y;
                                (void)cwv_host_scroll_at(host,(float)(-dy),px,py);
                            }
                        }
                    }
                    previous_page_touch_x=px;
                    previous_page_touch_y=py;
                    if (!scrollbar_drag_axis) {
                        char script[192];
                        /* The backend receives every touch sample so native
                           controls such as <select> can track hover while the
                           finger moves.  Do not dispatch this twice: the old
                           duplicate call could toggle a select open/closed in
                           a single frame. */
                        (void)cwv_host_pointer_event(host,px,py,1);
                        snprintf(script,sizeof(script),"window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'%s',true)",px,py,was_active?"move":"down");
                        cwv_host_eval(host,script);
                    }
                }else if(previousTouch.reportNum>0){
                    if (scrollbar_drag_axis) {
                        scrollbar_drag_axis=0;
                        scrollbar_drag_max=0.0f;
                        previous_page_touch_x=-1;
                        previous_page_touch_y=-1;
                        page_pan_start_x=-1; page_pan_start_y=-1;
                        horizontal_touch_drag=0;
                        (void)cwv_host_pointer_cancel(host);
                    }
                    int suppress_click=horizontal_touch_drag; char script[192];
                    if(scroll_enabled){
                        (void)cwv_scroll_update(0,0,(int)current.ry);
                        (void)cwv_scroll_take_delta();
                        suppress_click=cwv_scroll_is_dragging();
                        if(suppress_click){cwv_host_pointer_cancel(host);cwv_scroll_reset();}
                    }
                    if(!suppress_click)(void)cwv_host_pointer_event(host,px,py,0);
                    snprintf(script,sizeof(script),"window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'up',false)",px,py);
                    previous_page_touch_x=-1;
                    previous_page_touch_y=-1;
                    page_pan_start_x=-1; page_pan_start_y=-1;
                    horizontal_touch_drag=0;
                    cwv_host_eval(host,script);
                }
            }
        } else {
            previous_page_touch_x=-1;
            previous_page_touch_y=-1;
            page_pan_start_x=-1; page_pan_start_y=-1;
            horizontal_touch_drag=0;
            scrollbar_drag_axis=0;
            scrollbar_drag_max=0.0f;
        }

        previousTouch=touch;

        /* Bluetooth HID mouse (cwv_kbm_vita.c, 2026-10-05): a paired BT
         * mouse drives the SAME virtual cursor the rear pad uses, but
         * independent of the Settings toggle - a real mouse is always a
         * mouse.  Relative movement is kept absolute here (clamped to
         * the screen); left = tap like the front panel (down on press,
         * up on release); right toggles the menu like right-click on
         * Windows; the wheel scrolls the page. */
#if CWV_CURSOR_ENABLED
        {
            CwvKbmVitaMouse km;
            cwv_kbm_vita_mouse_poll(&km);
            if (km.present) {
                char script[192];
                cwv_cursor_place(&g_cursor,(float)km.x,(float)km.y,
                                SCREEN_W,SCREEN_H);
                cwv_cursor_show(&g_cursor,1);
                if (km.wheel)
                    (void)cwv_host_scroll_by(host,
                                             -(float)km.wheel * 40.0f);
                if (km.left_pressed) {
                    if(g_menu.mode!=MENU_CLOSED) handle_menu_touch(km.x,km.y,host);
                    else{
                        (void)cwv_host_pointer_event(host,km.x,km.y,1);
                        snprintf(script,sizeof(script),
                            "window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'down',false)",km.x,km.y);
                        (void)cwv_host_eval(host,script);
                    }
                }
                if (km.left_released) {
                    if(g_menu.mode!=MENU_CLOSED) { /* menu taps fire on press */ }
                    else{
                        (void)cwv_host_pointer_event(host,km.x,km.y,0);
                        snprintf(script,sizeof(script),
                            "window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'up',false)",km.x,km.y);
                        (void)cwv_host_eval(host,script);
                    }
                }
                if (km.right_pressed) {
                    page_button_reset_all();
                    g_menu.mode=(g_menu.mode==MENU_CLOSED)?MENU_MAIN:MENU_CLOSED;
                    g_menu.selected=0;
                    g_menu.settings_scroll=0.0f;
                    g_menu.settings_scroll_drag=0;
                    g_menu.devtools_scroll=0.0f;
                    g_menu.devtools_scroll_drag=0;
                }
            }
        }
#endif /* CWV_CURSOR_ENABLED */

        /* Rear touchpad: absolute-position cursor (toggleable, unlike the
         * front panel which IS the tap).  Down + small movement on release
         * = tap at the cursor; a big drag cancels (no accidental click). */
#if CWV_CURSOR_ENABLED
        if (g_menu.pointer) {
            SceTouchData rear;
            memset(&rear,0,sizeof(rear));
            if (sceTouchPeek(SCE_TOUCH_PORT_BACK,&rear,1)>0 && rear.reportNum>0) {
                SceTouchPanelInfo rpanel;
                memset(&rpanel,0,sizeof(rpanel));
                if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK,&rpanel)>=0) {
                    int rmin_x=rpanel.minAaX,rmin_y=rpanel.minAaY,
                        rmax_x=rpanel.maxAaX,rmax_y=rpanel.maxAaY;
                    if(rmax_x<=rmin_x) rmax_x=rmin_x+1;
                    if(rmax_y<=rmin_y) rmax_y=rmin_y+1;
                    float fx=((float)rear.report[0].x-(float)rmin_x)/
                             (float)(rmax_x-rmin_x);
                    float fy=((float)rear.report[0].y-(float)rmin_y)/
                             (float)(rmax_y-rmin_y);
                    int px=(int)(fx*(float)SCREEN_W), py=(int)(fy*(float)SCREEN_H);
                    if(!g_cursor_down){
                        g_cursor_down=1; g_cursor_down_x=px; g_cursor_down_y=py;
                    }
                    cwv_cursor_place(&g_cursor,(float)px,(float)py,SCREEN_W,SCREEN_H);
                    cwv_cursor_show(&g_cursor,1);
                }
            } else if(g_cursor_down){
                g_cursor_down=0;
                int cx,cy;
                if(cwv_cursor_position(&g_cursor,&cx,&cy)){
                    int tx=cx-g_cursor_down_x, ty=cy-g_cursor_down_y;
                    if(tx*tx+ty*ty < 12*12){
                        if(g_menu.mode!=MENU_CLOSED)
                            handle_menu_touch(cx,cy,host);
                        else{
                            char script[192];
                            (void)cwv_host_pointer_event(host,cx,cy,1);
                            (void)cwv_host_pointer_event(host,cx,cy,0);
                            snprintf(script,sizeof(script),
                                "window.CwvInput && window.CwvInput.pointerEvent(%d,%d,'up',false)",cx,cy);
                            (void)cwv_host_eval(host,script);
                        }
                    } else (void)cwv_host_pointer_cancel(host);
                }
                /* Keep the cursor visible after the finger lifts so users
                 * can see where the rear-pad pointer was left. */
                cwv_cursor_show(&g_cursor,1);
            }
        } else if(g_cursor.visible) cwv_cursor_show(&g_cursor,0);
#endif /* CWV_CURSOR_ENABLED */

        if(g_menu.exit_requested) break;

        if (g_menu.mode==MENU_CLOSED) {
            int vcm = effective_control_mode(host);
            /* Touch scrolling still uses the optional module when available.
             * Physical analogue/D-pad scrolling is handled natively below and
             * never depends on that module being present. */
            if (scroll_enabled) {
                (void)cwv_scroll_update(scroll_touch_active, scroll_touch_y, 128);
                float touch_scroll_delta = cwv_scroll_take_delta();
                if (touch_scroll_delta != 0.0f)
                    (void)cwv_host_scroll_at(host, touch_scroll_delta,
                                             scroll_touch_x, scroll_touch_y);
            }

            if (vcm != VCM_TOUCH && vcm != VCM_STANDALONE && vcm != VCM_CURSOR && effective_scroll_mode() == 2) {
                /* D-pad scroll (84): focus is deliberately KEPT - the
                 * focused node's region chain consumes the delta first
                 * (backend_scroll_by), and only the leftover reaches the
                 * page, exactly like a browser.  Clearing focus here used
                 * to make the page scroll away while an element was
                 * selected. */
                const float step=18.0f*scroll_speed_multiplier();
                if (current.buttons & SCE_CTRL_UP)
                    (void)cwv_host_scroll_by(host,-step);
                if (current.buttons & SCE_CTRL_DOWN)
                    (void)cwv_host_scroll_by(host,step);
                if (current.buttons & SCE_CTRL_LEFT)
                    (void)cwv_host_scroll_x_by(host,-step);
                if (current.buttons & SCE_CTRL_RIGHT)
                    (void)cwv_host_scroll_x_by(host,step);
            } else if(vcm != VCM_TOUCH && vcm != VCM_STANDALONE && vcm != VCM_CURSOR && effective_scroll_mode() != 2) {
                /* Physical analogue scrolling is deliberately independent of
                 * the touch-scroll module. */
                int sy = (effective_scroll_mode()==1) ? (int)current.ly : (int)current.ry;
                int sx = (effective_scroll_mode()==1) ? (int)current.lx : (int)current.rx;
                int sy_delta = sy-128;
                int sx_delta = sx-128;
                const int deadzone=18;
                if (sy_delta < -deadzone || sy_delta > deadzone) {
                    int magnitude = sy_delta < 0 ? -sy_delta : sy_delta;
                    float step = (float)(magnitude-deadzone) / (float)(127-deadzone) * (7.0f*scroll_speed_multiplier());
                    if(step>7.0f) step=7.0f;
                    (void)cwv_host_scroll_by(host,sy_delta>0 ? step : -step);
                }
                if (sx_delta < -deadzone || sx_delta > deadzone) {
                    int magnitude = sx_delta < 0 ? -sx_delta : sx_delta;
                    float step = (float)(magnitude-deadzone) / (float)(127-deadzone) * (7.0f*scroll_speed_multiplier());
                    if(step>7.0f) step=7.0f;
                    (void)cwv_host_scroll_x_by(host,sx_delta>0 ? step : -step);
                }
            }

            /* Native zoom is independent of page controls.  Either analog can
             * be selected in Settings; vertical movement controls zoom.
             * Project zoom:off (cwv_project.h) compiles the axis out. */
#if CWV_ZOOM_ENABLED
            if (vcm != VCM_TOUCH && vcm != VCM_STANDALONE && vcm != VCM_CURSOR && effective_zoom_mode() != 2) {
                int za = (effective_zoom_mode()==1 ? (int)current.ly : (int)current.ry) - 128;
                int zm=za<0?-za:za;
                if(zm>18) {
                    float z=cwv_host_zoom(host);
                    float step=(float)(zm-18)/109.0f*0.035f;
                    if(step>0.035f)step=0.035f;
                    z += za<0 ? step : -step;
                    (void)cwv_host_set_zoom(host,z);
                    g_menu.zoom_level=z;
                    if(g_menu.override_zoom_level>0.0f) g_menu.override_zoom_level=z;
                    g_menu.dirty=1;
                }
            }
#endif /* CWV_ZOOM_ENABLED */

        }

        int update_rc = cwv_host_update(host);
        history_record(cwv_host_current_url(host));
        /* Announce the ESM state exactly once, in the page's console -
           the one place the user actually looks when scripts misbehave.
           A loud error only on a failed self-test; a PASS stays silent
           in DevTools. */
        if (!g_esm_selftest_announced && cwv_host_js_active(host)) {
            char msg[384];
            g_esm_selftest_announced=1;
            if (g_esm_selftest==0) {
                snprintf(msg,sizeof(msg),
                         "console.error('ESM SELF-TEST FAILED: %.120s - "
                         "the built-in transformer did not convert the "
                         "ES6 canary correctly; report this with the "
                         "report string')",
                         g_esm_selftest_report);
                cwv_host_eval(host,msg);
            }
        }
        vita2d_start_drawing();
        int draw_rc = cwv_host_draw(host);
        if (CWV_DEBUG && g_menu.mode==MENU_CLOSED && g_menu.dbg_overlay)
            draw_debug_overlay(devtools_pgf,host,frame_count,fps_value,frame_ms);
        if (g_menu.mode!=MENU_CLOSED) draw_native_menu(devtools_pgf,host,frame_count,fps_value,frame_ms);
        /* Front touchscreen contacts are already dispatched to the page
         * below as touch input. Draw a small visual marker for EVERY active
         * contact so multi-finger gestures are visible; these markers do not
         * synthesize clicks and do not alter the existing first-contact
         * input behavior. */
        if (touch.reportNum > 0) {
            SceTouchPanelInfo front_panel;
            memset(&front_panel, 0, sizeof(front_panel));
            if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &front_panel) >= 0) {
                int min_x = front_panel.minAaX, min_y = front_panel.minAaY;
                int max_x = front_panel.maxAaX, max_y = front_panel.maxAaY;
                int count = touch.reportNum;
                if (max_x <= min_x) max_x = min_x + 1;
                if (max_y <= min_y) max_y = min_y + 1;
                if (count > 6) count = 6;
                for (int ti = 0; ti < count; ++ti) {
                    int tx = (touch.report[ti].x - min_x) * SCREEN_W / (max_x - min_x);
                    int ty = (touch.report[ti].y - min_y) * SCREEN_H / (max_y - min_y);
                    if (tx < 0) tx = 0; else if (tx >= SCREEN_W) tx = SCREEN_W - 1;
                    if (ty < 0) ty = 0; else if (ty >= SCREEN_H) ty = SCREEN_H - 1;
                    cwv_draw_fill_circle((float)tx, (float)ty, 7.0f, RGBA8(20, 220, 255, 190));
                    cwv_draw_fill_circle((float)tx, (float)ty, 2.0f, RGBA8(255, 255, 255, 245));
                }
            }
        }
#if CWV_CURSOR_ENABLED
        /* The cursor vanishes while ANY menu is open (user direction,
         * 2026-09-26 (43)) - it used to render over the dimmed page
         * outside the menu area as well.  Placement keeps running
         * (taps route into the menu); only the draw is suppressed. */
        if (g_menu.mode == MENU_CLOSED) {
            /* CSS cursor (2026-09-26 (45)): apply the engine's
             * under-pointer style each frame; the Settings > Appearance
             * toggle gates it, NULL restores the default art. */
            CwvCursorInfo info;
            int info_x, info_y;
            if (g_menu.css_cursors &&
                cwv_cursor_position(&g_cursor, &info_x, &info_y) &&
                cwv_host_cursor_at(host, info_x, info_y, &info))
                cwv_cursor_set_style(&g_cursor, &info);
            else
                cwv_cursor_set_style(&g_cursor, NULL);
            cwv_cursor_draw(&g_cursor);
        }
#endif
        vita2d_end_drawing();
        /* Real-hardware common dialogs (the IME keyboard) composite into
           the backbuffer HERE, after the scene ends and before the swap.
           Vita3K draws dialogs regardless, which is why the keyboard
           appeared there but froze the app on a real Vita. */
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();

        frame_count++;
        fps_window_frames++;
        uint64_t now_us=(uint64_t)sceKernelGetProcessTimeWide();
        uint64_t elapsed_us=now_us-fps_window_start;
        if(elapsed_us>=500000){
            fps_value=(float)fps_window_frames*1000000.0f/(float)elapsed_us;
            frame_ms=(float)elapsed_us/(float)fps_window_frames/1000.0f;
            fps_window_start=now_us;fps_window_frames=0;
        }
        if ((frame_count % 60) == 0) {
            write_runtime_log("frame=%u fps=%.2f frame_ms=%.2f update=%d draw=%d status=%d error=%s",
                              frame_count, fps_value, frame_ms, update_rc, draw_rc,
                              (int)cwv_host_status(host),
                              cwv_host_last_error(host));
        }

        if (draw_rc < 0) {
            /* Keep a visible native signal even if the backend fails. */
            draw_boot_diagnostic("backend draw failed", "see runtime.log");
        }

        sceKernelDelayThread(16667);
    }

    if (g_menu.page_favicon) { vita2d_free_texture(g_menu.page_favicon); g_menu.page_favicon=NULL; }
    if (devtools_pgf) vita2d_free_pgf(devtools_pgf);
    cwv_host_destroy(host);
    if (scroll_module >= 0) {
        int unload_status = 0;
        int unload_rc = sceKernelStopUnloadModule(scroll_module, 0, NULL, 0, NULL, &unload_status);
        write_runtime_log("CwvScroll unload rc=%d status=%d", unload_rc, unload_status);
    }
    vita2d_fini();
    write_runtime_log("shutdown complete");
    sceKernelExitProcess(0);
    return 0;
}
