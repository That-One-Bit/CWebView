#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "menu/cwv_menu_ui.h"
static int fails, rects, texts, partner_rects, scaled_rects;
static int custom_rects=0;
static void rect(float x,float y,float w,float h,unsigned c,void *u){(void)c;(void)u;if(x==130&&y==160&&w==700&&h==270)rects++;
 if(x==130&&y==154&&w==700&&h==300)custom_rects++;
 if(x==150&&y==205&&w==210&&h==100)partner_rects++;
 /* scaled settings panel: abs (130,160,700,270) * (640/960) */
 if(x>86.0f&&x<87.5f&&y>106.0f&&y<107.5f&&w>466.0f&&w<467.5f&&h>179.0f&&h<180.5f)scaled_rects++;}
static void circle(float x,float y,float r,unsigned c,void *u){(void)x;(void)y;(void)r;(void)c;(void)u;}
static void text(float x,float y,unsigned c,float scale,const char *s,void *u){(void)x;(void)y;(void)c;(void)scale;(void)u;if(s && *s)texts++;}
static float width(float scale,const char *s,void *u){(void)u;return (float)strlen(s)*8*scale;}
static void check(int v,const char *what){printf("%s %s\n",v?"PASS":"FAIL",what);if(!v)fails++;}
/* Host services called by the shared shell; no real host is needed. */
static char current_url[1024];
int cwv_host_set_theme(CwvHost *h,int d){(void)h;(void)d;return 0;}
int cwv_host_set_zoom(CwvHost *h,float z){(void)h;(void)z;return 0;}
int cwv_host_set_scrollbars(CwvHost *h,int v){(void)h;(void)v;return 0;}
int cwv_host_set_button_wait_override(CwvHost *h,const char *s){(void)h;(void)s;return 0;}
int cwv_host_load(CwvHost *h,const char *s){(void)h;snprintf(current_url,sizeof(current_url),"%s",s);return 0;}
const char *cwv_host_current_url(const CwvHost *h){(void)h;return current_url;}
const char *cwv_host_page_title(const CwvHost *h){(void)h;return "";}
const char *cwv_host_favicon_url(const CwvHost *h){(void)h;return "";}
int cwv_host_control_mode(const CwvHost *h){(void)h;return 0;}
/* The URL root is only used by file activation, which this test does not exercise. */
const char *cwv_platform_url_root(void){return "file:///test-app";}
/* CWMenu linking is no longer exercised by this menu UI test. */
int cwv_host_js_active(CwvHost *h){(void)h;return 0;}
void cwv_host_console_stats(CwvHost *h,int *e,int *w,int *n){(void)h;*e=*w=*n=0;}
int cwv_host_console(CwvHost *h,char *b,size_t cap){(void)h;if(cap)*b=0;return 0;}

int main(void){
 CwvMenuUi ui; CwvMenuPaint p={rect,circle,text,width,NULL,NULL,NULL};
 check(vita_native_menu_templates_load("../sources/shared/sources/menu/scripts/default.cwm")==0,"script parses");
 cwv_menu_ui_init(&ui,&p,NULL,NULL,960,544);
 cwv_menu_ui_open(&ui,CWV_MENU_PAGEOPS);cwv_menu_ui_draw(&ui,NULL);
 check(rects>0,"page ops uses devtools panel geometry");
 for(int i=0;i<3;i++)cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);
 check(ui.pageops_selected==3,"page ops pad selection");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
 check(ui.mode==CWV_MENU_MAIN,"page ops Back returns to main");
 cwv_menu_ui_open(&ui,CWV_MENU_SETTINGS);rects=0;texts=0;cwv_menu_ui_draw(&ui,NULL);
 check(rects>0&&texts>10,"settings uses shared panel and paints labels");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.settings.zoom_level>1.0f&&ui.settings.dirty,"zoom slider responds to pad");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_R1);
 check(ui.settings_tab==1,"settings tab advances");
 cwv_menu_ui_touch(&ui,110+468+10,54+58+10,1);
 check(ui.settings_tab==2,"settings tab responds to touch");
 for(int i=0;i<3;i++)cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.settings.menu_hotkey==1,"preferences change shortcut to START");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
 check(ui.settings.show_exit_app==1,"preferences can reveal Exit App");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
 check(ui.settings.show_exit_app==0,"preferences can hide Exit App again");
 cwv_menu_ui_open(&ui,CWV_MENU_DEVTOOLS);rects=0;texts=0;cwv_menu_ui_draw(&ui,NULL);
 check(rects>0&&texts>=9,"devtools paints from script");
 check(ui.settings.show_exit_app==0,"Exit App hidden by default");
 for(int i=0;i<4;i++) cwv_menu_ui_input(&ui,CWV_MENU_INPUT_R1);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.dev_section==4&&ui.overrides.control_mode>=0,"override changes from pad");
 /* The control-mode override cycles Default -> Browser(1) -> Standalone(2)
  * -> Touch(3) -> Cursor(4) in backend VCM numbering (2026-09-25 (40)). */
 check(ui.overrides.control_mode==1,"override starts at Browser after one step");
 for(int i=0;i<3;i++) cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.overrides.control_mode==4,"override cycles through to Cursor");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.overrides.control_mode==-1,"Cursor wraps to Default");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_RIGHT);
 check(ui.overrides.control_mode==1,"Default wraps to Browser");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_LEFT);
 check(ui.overrides.control_mode==-1,"left from Browser returns to Default");
 check(ui.overrides.zoom_level==0.0f,"override zoom starts at valid default");
 /* Record page-initiated navigation, then run the actual Page Ops actions. */
 ui.host=(CwvHost *)&ui;
 snprintf(current_url,sizeof(current_url),"file:///app0/web/a.html");
 cwv_menu_ui_record_page(&ui);
 snprintf(current_url,sizeof(current_url),"file:///app0/web/b.html");
 cwv_menu_ui_record_page(&ui);
 check(ui.history_count==2&&ui.history_index==1,"page history records two loads");
 cwv_menu_ui_open(&ui,CWV_MENU_PAGEOPS);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
 check(ui.history_index==0&&!strcmp(current_url,"file:///app0/web/a.html"),
       "history Back loads prior page without adding a duplicate");
 cwv_menu_ui_record_page(&ui);
 check(ui.history_count==2,"history does not duplicate Back load");
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_DOWN);
 cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
 check(ui.history_index==1&&!strcmp(current_url,"file:///app0/web/b.html"),
       "history Forward loads next page");
 ui.host=NULL;
 /* A dynamic .cwm has a real paired floating window and actionable rows. */
 {
  char tmp[]="cwmenu_ui_fixture_XXXXXX";
  int fd=mkstemp(tmp);
  FILE *in=fopen("../sources/shared/sources/menu/scripts/default.cwm","rb");
  FILE *out=fd>=0?fdopen(fd,"wb"):NULL;
  check(in!=NULL&&out!=NULL,"custom script test fixture opens");
  if(in&&out){
   char buffer[4096];size_t n;
   while((n=fread(buffer,1,sizeof(buffer),in))>0)fwrite(buffer,1,n,out);
   fputs("\nmenu wiiu_widget_test\n"
         "element window window 0 0 740 436 normal \"\" pair wiiu_partner\n"
         "element header header 0 0 740 44 normal \"Widget test\"\n"
         "element primary button 40 154 220 38 normal \"Settings\" action settings\n"
         "element icon icon 320 154 30 30 normal \"gear\"\n"
         "element progress progress 40 250 500 38 normal \"Progress\"\n"
         "end\n"
         "menu wiiu_partner\n"
         "element window window 0 0 210 100 normal \"\" anchor 150 205\n"
         "element header header 0 0 210 44 normal \"Partner\"\n"
         "element badge badge 15 50 170 28 normal \"Ready\"\n"
         "end\n",out);
   fclose(out);fclose(in);
   check(vita_native_menu_templates_load(tmp)==0,"dynamic menu and pair parse");
   check(cwv_menu_ui_open_named(&ui,"wiiu_widget_test")==0&&
         ui.mode==CWV_MENU_CUSTOM,"dynamic menu opens by name");
   cwv_menu_ui_draw(&ui,NULL);
   check(partner_rects>0,"paired window draws above custom widgets");
   cwv_menu_ui_touch(&ui,170,218,1);
   check(ui.mode==CWV_MENU_CUSTOM,"partner tap does not activate covered button");
   cwv_menu_ui_input(&ui,CWV_MENU_INPUT_CONFIRM);
   check(ui.mode==CWV_MENU_SETTINGS,"custom button action works on pad");
   unlink(tmp);
  }else{if(in)fclose(in);if(out)fclose(out);else if(fd>=0)close(fd);}
 }

 /* ---- Wii scale: the 960x544 menu on a 640x480 screen (2026-09-23) ---- */
 {
  CwvMenuUi ui; CwvMenuData d; CwvMenuPaint p={rect,circle,text,width,NULL,NULL,NULL};
  int dark_before;
  memset(&ui,0,sizeof(ui));
  cwv_menu_ui_init(&ui,&p,NULL,NULL,640,480);
  cwv_menu_ui_set_scale(&ui,640.0f/960.0f);
  check(ui.scale>0.66f&&ui.scale<0.67f,"set_scale stores the Wii factor");
  scaled_rects=0;
  cwv_menu_ui_open(&ui,CWV_MENU_SETTINGS);
  cwv_menu_ui_draw(&ui,&d);
  check(scaled_rects>0,"settings panel paints at the scaled geometry");
  /* inverse touch map: real 640px taps reach the 960px template */
  dark_before=ui.settings.dark_mode;
  cwv_menu_ui_touch(&ui,100,139,1); /* theme row abs (150,208) scaled */
  check(ui.settings.dark_mode!=dark_before,"scaled touch hits the theme row");
  /* ---- Vita-only pointer row stays inert in the shared shell ---- */
  {
   int item_before;
   cwv_menu_ui_input(&ui,CWV_MENU_INPUT_R1);   /* -> controls tab */
   check(ui.settings_tab==1,"scaled shell switches to the controls tab");
   item_before=ui.settings_item;
   /* with cursor_toggle (Vita) the row IS where this tap lands */
   ui.cursor_toggle=1;
   cwv_menu_ui_touch(&ui,100,251,1); /* pointer row abs (150,376) scaled */
   check(ui.settings_item==4,"pointer row located at the tap when enabled");
   /* shared shell: guard makes the same tap a no-op */
   ui.cursor_toggle=0;
   ui.settings_item=item_before;
   cwv_menu_ui_touch(&ui,100,251,1);
   check(ui.settings_item==item_before,"cycle_pointer row not tappable (Vita only)");
  }
  /* ---- fixed hotkey (Wii HOME): the Menu shortcut row stops cycling ---- */
  {
   int hk=ui.settings.menu_hotkey;
   cwv_menu_ui_touch(&ui,100,223,1); /* shortcut row abs (150,334) scaled */
   check(ui.settings.menu_hotkey==hk,"hotkey row inert while fixed_hotkey");
  }
 }

 printf("%s (%d failures)\n",fails?"FAILED":"ALL PASS",fails);
 return !!fails;
}
