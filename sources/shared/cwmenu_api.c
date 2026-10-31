#include "cwmenu_api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define CWM_STRING_POOL_SIZE (64 * 1024)
#define CWM_MAX_TARGETS 64
#define CWM_MAX_VARS 96
#define CWM_THEME_MAX 24
#define CWM_THEME_NAME_MAX 24
#define CWM_THEME_LABELS 6
#define CWM_DYNAMIC_MENUS 8
#define CWM_DYNAMIC_MENU_CAPACITY 32
static char g_cwm_string_pool[CWM_STRING_POOL_SIZE];
static size_t g_cwm_string_used;
typedef struct { const char *name; float value; } CWMenuNumberVar;
static CWMenuNumberVar g_cwm_vars[CWM_MAX_VARS];
static size_t g_cwm_var_count;

/* --- script version + theme (see cwmenu_api.h) --- */
static int g_script_version = 1;
static size_t g_script_loaded = 0;
typedef struct { char name[CWM_THEME_NAME_MAX]; unsigned char r,g,b; } CWMenuThemeColor;
static CWMenuThemeColor g_theme[CWM_THEME_MAX];
static size_t g_theme_count;
static const char *g_theme_labels[CWM_THEME_LABELS];

/* --- dynamic (app-defined) menu registry --- */
/* ---- Project variables (CWMenu 3) ------------------------------------
 * Host-injected feature switches consumed by `when <expr>` conditionals.
 * NOT cleared by cwmenu_script_load(): the host sets them before each
 * load, and script `project` block lines only fill unset entries, so
 * the effective values survive reloads of the same script. */
#define CWM_PROJECT_MAX 24
#define CWM_PROJECT_NAME_MAX 24
#define CWM_PROJECT_VALUE_MAX 24
typedef struct { char name[CWM_PROJECT_NAME_MAX]; char value[CWM_PROJECT_VALUE_MAX]; } CwmProjectVar;
static CwmProjectVar g_project[CWM_PROJECT_MAX];
static size_t g_project_count;

static int cwm_ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca=*a, cb=*b;
        if (ca>='A' && ca<='Z') ca=(char)(ca-'A'+'a');
        if (cb>='A' && cb<='Z') cb=(char)(cb-'A'+'a');
        if (ca!=cb) return 0;
        ++a; ++b;
    }
    return *a=='\0' && *b=='\0';
}

int cwm_project_set(const char *name, const char *value) {
    size_t i;
    if (!name || !*name) return -1;
    for (i = 0; i < g_project_count; i++)
        if (cwm_ci_equal(g_project[i].name, name)) {
            snprintf(g_project[i].value, sizeof(g_project[i].value), "%s",
                    value ? value : "");
            return 0;
        }
    if (g_project_count >= CWM_PROJECT_MAX) return -1;
    snprintf(g_project[g_project_count].name, sizeof(g_project[0].name), "%s", name);
    snprintf(g_project[g_project_count].value, sizeof(g_project[0].value), "%s",
             value ? value : "");
    g_project_count++;
    return 0;
}

int cwm_project_get(const char *name, const char **value) {
    size_t i;
    if (name) for (i = 0; i < g_project_count; i++)
        if (cwm_ci_equal(g_project[i].name, name)) {
            if (value) *value = g_project[i].value;
            return 0;
        }
    return -1;
}

int cwm_project_is_on(const char *name) {
    const char *v = NULL;
    if (cwm_project_get(name, &v) < 0 || !v || !*v) return 0;
    if (cwm_ci_equal(v, "off") || cwm_ci_equal(v, "false") ||
        cwm_ci_equal(v, "0") || cwm_ci_equal(v, "no")) return 0;
    return 1;
}

void cwm_project_clear(void) { g_project_count = 0; }

/* Fill-only (script `project` block + cwv_project_publish()): sets the
 * variable ONLY when it is unset, so a host-injected value (either
 * cwm_project_set from the app or a prior override) always wins. */
int cwm_project_set_default(const char *name, const char *value) {
    const char *cur = NULL;
    if (cwm_project_get(name, &cur) == 0) return 0;
    return cwm_project_set(name, value);
}
static void cwm_project_default(const char *name, const char *value) {
    (void)cwm_project_set_default(name, value);
}

static VitaMenuTemplate g_dynamic_items[CWM_DYNAMIC_MENUS][CWM_DYNAMIC_MENU_CAPACITY];
static size_t g_dynamic_counts[CWM_DYNAMIC_MENUS];
static char g_dynamic_names[CWM_DYNAMIC_MENUS][CWM_THEME_NAME_MAX];
static size_t g_dynamic_menu_count;

static char *cwm_pool_copy(const char *s);

int cwmenu_script_version(void){ return g_script_version; }
int cwm_script_version_supported(void){ return CWM_SCRIPT_VERSION_SUPPORTED; }

int cwm_theme_get_rgb(const char *slot,int *r,int *g,int *b)
{
    size_t i;
    if(!slot||!*slot) return -1;
    for(i=0;i<g_theme_count;i++) if(!strcmp(g_theme[i].name,slot)){
        if(r)*r=g_theme[i].r;
        if(g)*g=g_theme[i].g;
        if(b)*b=g_theme[i].b;
        return 0;
    }
    return -1;
}

const char *cwm_theme_get_label(int accent_index)
{
    if(accent_index<0||accent_index>=CWM_THEME_LABELS) return NULL;
    return g_theme_labels[accent_index];
}

int cwm_theme_count(void){ return (int)g_theme_count; }

const VitaMenuTemplate *cwm_dynamic_menu(const char *name,size_t *count)
{
    size_t i;
    if(!name) return NULL;
    for(i=0;i<g_dynamic_menu_count;i++) if(!strcmp(g_dynamic_names[i],name)){
        if(count)*count=g_dynamic_counts[i];
        return g_dynamic_items[i];
    }
    return NULL;
}

int cwm_dynamic_menu_total(void){ return (int)g_dynamic_menu_count; }
const char *cwm_dynamic_menu_name(int index)
{
    if(index<0||(size_t)index>=g_dynamic_menu_count) return NULL;
    return g_dynamic_names[index];
}

static CWMenuMenuScriptTarget g_dynamic_targets[CWM_DYNAMIC_MENUS];
static CWMenuMenuScriptTarget *g_loaded_targets;
static size_t g_loaded_target_count;
static CWMenuMenuScriptTarget *cwm_dynamic_slot(const char *name)
{
    size_t i;
    if(!name||!*name) return NULL;
    for(i=0;i<g_dynamic_menu_count;i++)
        if(!strcmp(g_dynamic_names[i],name)) return &g_dynamic_targets[i];
    if(g_dynamic_menu_count>=CWM_DYNAMIC_MENUS) return NULL;   /* registry full */
    snprintf(g_dynamic_names[g_dynamic_menu_count],sizeof(g_dynamic_names[0]),"%.23s",name);
    g_dynamic_targets[g_dynamic_menu_count].menu_name=g_dynamic_names[g_dynamic_menu_count];
    g_dynamic_targets[g_dynamic_menu_count].items=g_dynamic_items[g_dynamic_menu_count];
    g_dynamic_targets[g_dynamic_menu_count].capacity=CWM_DYNAMIC_MENU_CAPACITY;
    g_dynamic_targets[g_dynamic_menu_count].count=0;
    memset(g_dynamic_items[g_dynamic_menu_count],0,sizeof(g_dynamic_items[0]));
    g_dynamic_menu_count++;
    return &g_dynamic_targets[g_dynamic_menu_count-1];
}

int cwm_var_set_number(const char *name, float value)
{
    size_t i;
    if(!name || !*name) return -1;
    for(i=0;i<g_cwm_var_count;i++) if(!strcmp(g_cwm_vars[i].name,name)){ g_cwm_vars[i].value=value; return 0; }
    if(g_cwm_var_count>=CWM_MAX_VARS) return -1;
    g_cwm_vars[g_cwm_var_count].name=cwm_pool_copy(name);
    if(!g_cwm_vars[g_cwm_var_count].name) return -1;
    g_cwm_vars[g_cwm_var_count].value=value;
    g_cwm_var_count++;
    return 0;
}

int cwm_var_get_number(const char *name, float *value)
{
    size_t i;
    if(!name || !value) return -1;
    for(i=0;i<g_cwm_var_count;i++) if(!strcmp(g_cwm_vars[i].name,name)){ *value=g_cwm_vars[i].value; return 0; }
    return -1;
}

static float cwm_clampf(float value, float low, float high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

static char *cwm_pool_copy(const char *s)
{
    size_t n;
    char *dst;
    if(!s) s="";
    n=strlen(s)+1;
    if(g_cwm_string_used+n>sizeof(g_cwm_string_pool)) return NULL;
    dst=g_cwm_string_pool+g_cwm_string_used;
    memcpy(dst,s,n);
    g_cwm_string_used+=n;
    return dst;
}

static char *cwm_trim(char *s)
{
    char *end;
    while(*s && isspace((unsigned char)*s)) ++s;
    end=s+strlen(s);
    while(end>s && isspace((unsigned char)end[-1])) --end;
    *end='\0';
    return s;
}

static int cwm_kind(const char *s, VitaMenuElementKind *out)
{
    static const struct { const char *name; VitaMenuElementKind kind; } map[] = {
        {"window",VITA_MENU_WINDOW},{"panel",VITA_MENU_PANEL},{"header",VITA_MENU_HEADER},
        {"button",VITA_MENU_BUTTON},{"text",VITA_MENU_TEXT},{"underline",VITA_MENU_UNDERLINE},
        {"slider",VITA_MENU_SLIDER},{"checkbox",VITA_MENU_CHECKBOX},{"segmented",VITA_MENU_SEGMENTED},
        {"badge",VITA_MENU_BADGE},{"footer",VITA_MENU_FOOTER},{"decoration",VITA_MENU_DECORATION},
        {"icon",VITA_MENU_ICON},{"progress",VITA_MENU_PROGRESS},{"value",VITA_MENU_VALUE},
        {"list",VITA_MENU_LIST},{"shape_rectangle",VITA_MENU_SHAPE_RECTANGLE},
        {"shape_rounded_rectangle",VITA_MENU_SHAPE_ROUNDED_RECTANGLE},{"shape_circle",VITA_MENU_SHAPE_CIRCLE},
        {"line",VITA_MENU_LINE},{"scrollbar",VITA_MENU_SCROLLBAR}
    };
    size_t i;
    if(!s||!out) return -1;
    for(i=0;i<sizeof(map)/sizeof(map[0]);++i) if(!strcmp(s,map[i].name)){*out=map[i].kind;return 0;}
    return -1;
}

static int cwm_style(const char *s, VitaMenuStyle *out)
{
    if(!s||!out) return -1;
    if(!strcmp(s,"normal")){*out=VITA_MENU_STYLE_NORMAL;return 0;}
    if(!strcmp(s,"light")){*out=VITA_MENU_STYLE_LIGHT;return 0;}
    if(!strcmp(s,"dim")){*out=VITA_MENU_STYLE_DIM;return 0;}
    return -1;
}

static CWMenuMenuScriptTarget *cwm_target(CWMenuMenuScriptTarget *targets,size_t count,const char *name)
{
    size_t i;
    for(i=0;i<count;++i) if(targets[i].menu_name && !strcmp(targets[i].menu_name,name)) return &targets[i];
    return NULL;
}

static int cwm_script_parse(FILE *fp, CWMenuMenuScriptTarget *targets,
                            size_t target_count, int link_mode)
{
    char line[1024];
    CWMenuMenuScriptTarget *current=NULL;
    int errors=0;
    int scrollbar_block=0;
    char scrollbar_owner[96]={0};
    char scrollbar_items[768]={0};
    float scrollbar_x=0.0f, scrollbar_y=0.0f, scrollbar_w=5.0f, scrollbar_h=0.0f;
    VitaMenuStyle scrollbar_style=VITA_MENU_STYLE_NORMAL;
    (void)link_mode;
    int theme_block=0;
    int project_block=0;
    while(fgets(line,sizeof(line),fp)){
        char *s=cwm_trim(line);
        char menu_name[96], name[96], kind_name[64], style_name[32], label[768];
        float x,y,w,h;
        VitaMenuElementKind kind;
        VitaMenuStyle style;
        if(!*s || *s=='#') continue;

        /* `CWMenu <n>` header.  Parse ANY version (the host logs a warning for
         * versions newer than the controller) - unknown constructs in a
         * newer script are ignored line by line, so it degrades safely. */
        if(!theme_block && strncmp(s,"CWMenu",6)==0){
            int ver=0;
            if(sscanf(s,"CWMenu %d",&ver)==1 && ver>0){
                /* link mode: the linked script may be newer than the
                 * primary; report the max so host warnings stay honest. */
                if(link_mode){ if(ver>g_script_version) g_script_version=ver; }
                else g_script_version=ver;
            }
            continue;
        }

        /* theme block: named color slots, either `#RRGGBB` or `R G B`,
         * plus `labelN "Name"` accent display names. */
        if(theme_block){
            char slot[CWM_THEME_NAME_MAX];
            if(!strcmp(s,"end")){ theme_block=0; continue; }
            if(sscanf(s,"%23s",slot)==1 && !strncmp(slot,"label",5)){
                /* accent display name: `label2 "Orange"` */
                int li=atoi(slot+5);
                const char *q=strchr(s,'"');
                if(li>=0&&li<CWM_THEME_LABELS&&q){
                    char lab[64]={0};
                    size_t ln=strcspn(q+1,"\"");
                    if(ln>=sizeof(lab)) ln=sizeof(lab)-1;
                    memcpy(lab,q+1,ln);
                    g_theme_labels[li]=cwm_pool_copy(lab);
                    continue;
                }
                errors++;           /* label without a quoted name */
                continue;
            }
            {
                unsigned hex=0;
                int r=-1,g=-1,b=-1;
                if(sscanf(s,"%23s #%x",slot,&hex)==2){
                    r=(int)((hex>>16)&0xff); g=(int)((hex>>8)&0xff); b=(int)(hex&0xff);
                }else if(sscanf(s,"%23s %d %d %d",slot,&r,&g,&b)!=4){
                    r=g=b=-1;
                }
                if(r>=0&&g>=0&&b>=0&&r<=255&&g<=255&&b<=255){
                    size_t ti;
                    for(ti=0;ti<g_theme_count;ti++) if(!strcmp(g_theme[ti].name,slot)) break;
                    if(ti<CWM_THEME_MAX){
                        snprintf(g_theme[ti].name,CWM_THEME_NAME_MAX,"%s",slot);
                        g_theme[ti].r=(unsigned char)r;
                        g_theme[ti].g=(unsigned char)g;
                        g_theme[ti].b=(unsigned char)b;
                        if(ti==g_theme_count) g_theme_count++;
                    }
                    continue;
                }
            }
            errors++;               /* unknown/typo line inside theme */
            continue;
        }
        if(!strcmp(s,"theme")){ theme_block=1; continue; }

        /* Scrollbar groups are deliberately CSS-like: the identifier before
         * `scrollbar` names the group, and the brace contains any number of
         * item names. Both `content scrollbar {` and `scrollbar content {`
         * are accepted so CWMenu scripts remain pleasant to read. */
        if(!scrollbar_block && current){
            char owner_a[96]={0}, owner_b[96]={0}, brace[8]={0};
            int matched=0;
            if(sscanf(s,"%95s %95s %7s",owner_a,owner_b,brace)==3 && !strcmp(owner_b,"scrollbar") && !strcmp(brace,"{")){
                matched=1;
            }else if(sscanf(s,"scrollbar %95s %7s",owner_a,brace)==2 && !strcmp(brace,"{")){
                matched=1;
            }
            if(matched){
                scrollbar_block=1;
                snprintf(scrollbar_owner,sizeof(scrollbar_owner),"%s",!strcmp(owner_b,"scrollbar")?owner_a:owner_a);
                scrollbar_items[0]='\0';
                scrollbar_x=0.0f; scrollbar_y=0.0f; scrollbar_w=5.0f; scrollbar_h=0.0f;
                scrollbar_style=VITA_MENU_STYLE_NORMAL;
                continue;
            }
        }
        if(scrollbar_block){
            if(!strcmp(s,"}")){
                if(scrollbar_h<=0.0f || !current || current->count+1>=current->capacity){ errors++; scrollbar_block=0; continue; }
                {
                    VitaMenuTemplate *t=&current->items[current->count];
                    char generated[128];
                    snprintf(generated,sizeof(generated),"scrollbar:%s",scrollbar_owner[0]?scrollbar_owner:"content");
                    t->name=cwm_pool_copy(generated);
                    t->x=scrollbar_x; t->y=scrollbar_y; t->w=scrollbar_w; t->h=scrollbar_h;
                    t->kind=VITA_MENU_SCROLLBAR; t->label=cwm_pool_copy(""); t->action=cwm_pool_copy("scrollbar");
                    t->style=scrollbar_style; t->stops=0; t->brightness=0.0f;
                    t->brightness_var=NULL; t->tint=-1.0f; t->tint_var=NULL; t->stops_var=NULL;
                    t->scroll_owner=cwm_pool_copy(scrollbar_owner);
                    t->scroll_items=cwm_pool_copy(scrollbar_items);
                    if(!t->name||!t->label||!t->action||!t->scroll_owner||!t->scroll_items){ errors++; }
                    else current->count++;
                }
                scrollbar_block=0;
                continue;
            }
            {
                float bx,by,bw,bh; char bs[32]={0};
                if(sscanf(s,"track %f %f %f %f %31s",&bx,&by,&bw,&bh,bs)==5){
                    VitaMenuStyle st;
                    if(cwm_style(bs,&st)<0){ errors++; continue; }
                    scrollbar_x=bx; scrollbar_y=by; scrollbar_w=bw; scrollbar_h=bh; scrollbar_style=st;
                    continue;
                }
                if(!strncmp(s,"item ",5)){
                    char item[96]={0};
                    if(sscanf(s,"item %95s",item)==1){
                        if(scrollbar_items[0]) strncat(scrollbar_items,"|",sizeof(scrollbar_items)-strlen(scrollbar_items)-1);
                        strncat(scrollbar_items,item,sizeof(scrollbar_items)-strlen(scrollbar_items)-1);
                        continue;
                    }
                }
                if(!strncmp(s,"items ",6)){
                    const char *items=cwm_trim(s+6);
                    if(scrollbar_items[0]) strncat(scrollbar_items,"|",sizeof(scrollbar_items)-strlen(scrollbar_items)-1);
                    strncat(scrollbar_items,items,sizeof(scrollbar_items)-strlen(scrollbar_items)-1);
                    continue;
                }
            }
            errors++;
            continue;
        }

        /* `project` block (CWMenu 3): `name value` lines are script-side
         * DEFAULTS for the feature switches - host-injected values (set
         * via cwm_project_set before load) always win. */
        if(project_block){
            char pn[CWM_PROJECT_NAME_MAX];
            if(!strcmp(s,"end")){ project_block=0; continue; }
            {
                const char *q=strchr(s,'"');
                if(q){
                    char val[CWM_PROJECT_VALUE_MAX]={0};
                    size_t ln;
                    if(sscanf(s,"%23s",pn)==1){
                        ln=strcspn(q+1,"\"");
                        if(ln>=sizeof(val)) ln=sizeof(val)-1;
                        memcpy(val,q+1,ln);
                        cwm_project_default(pn,val);
                        continue;
                    }
                }
                {
                    char pv[CWM_PROJECT_VALUE_MAX]={0};
                    if(sscanf(s,"%23s %23s",pn,pv)==2){ cwm_project_default(pn,pv); continue; }
                }
            }
            errors++;               /* malformed project line */
            continue;
        }
        if(!strcmp(s,"project")){ project_block=1; continue; }

        {
            char var_name[96]; float var_value;
            if(sscanf(s,"var %95s %f",var_name,&var_value)==2){ cwm_var_set_number(var_name,var_value); continue; }
        }
        if(sscanf(s,"menu %95s",menu_name)==1){
            current=cwm_target(targets,target_count,menu_name);
            if(!current) current=cwm_dynamic_slot(menu_name);   /* app menu */
            /* A menu block replaces whatever the menu held before - the
             * load-time full reset used to handle this; linking (CWMenu 4)
             * needs it per-menu so a linked script can restyle a single
             * menu without redefining the whole file. */
            if(current && current->items && current->capacity){
                current->count=0;
                memset(current->items,0,current->capacity*sizeof(*current->items));
            }
            continue;
        }
        if(!strcmp(s,"end")){ current=NULL; continue; }
        if(strncmp(s,"element ",8)!=0 || !current) continue;
        label[0]='\0';
        if(sscanf(s,"element %95s %63s %f %f %f %f %31s",name,kind_name,&x,&y,&w,&h,style_name)!=7){ errors++; continue; }
        const char *label_qe=NULL;
        {
            const char *q=strchr(s,'"');
            const char *qe=q?strrchr(q+1,'"'):NULL;
            label_qe=qe;
            size_t ln;
            if(!q || !qe || qe<=q){ errors++; continue; }
            ln=(size_t)(qe-q-1); if(ln>=sizeof(label)) ln=sizeof(label)-1;
            memcpy(label,q+1,ln); label[ln]='\0';
        }
        /* CWMenu 3 `when <expr>`: drop the element when the project switch
         * is off/unknown.  expr = `name` or `!name` (after the label,
         * like every other element key).  Dropped elements simply do not
         * exist in the parsed menu. */
        {
            const char *kw = label_qe ? strstr(label_qe+1, " when ") : NULL;
            if(kw){
                char expr[CWM_PROJECT_NAME_MAX]={0};
                const char *e=kw+6;
                int neg=0, on;
                size_t ln=0;
                while(*e==' ') ++e;
                if(*e=='!'){ neg=1; ++e; }
                while(*e==' ') ++e;
                while(e[ln] && !isspace((unsigned char)e[ln]) &&
                      ln<sizeof(expr)-1) ++ln;
                memcpy(expr,e,ln);
                expr[ln]='\0';
                if(!expr[0]){ errors++; continue; }
                on=cwm_project_is_on(expr);
                if(neg) on=!on;
                if(!on) continue;  /* condition false: element is dropped */
            }
        }
        if(current->count+1>=current->capacity || cwm_kind(kind_name,&kind)<0 || cwm_style(style_name,&style)<0){ errors++; continue; }
        {
            VitaMenuTemplate *t=&current->items[current->count];
            char *n=cwm_pool_copy(name), *l=cwm_pool_copy(label);
            const char *a=label_qe?strstr(label_qe+1,"action "):NULL;
            char action_buf[128]={0}, stops_var_buf[96]={0}, tint_var_buf[96]={0};
            char *ac=NULL,*bv=NULL,*tv=NULL,*sv=NULL;
            int stops=0, border_flags=0; float brightness=0.0f, tint=-1.0f;
            int anchor_set=0, corner_sharp=0, crop_set=0;
            float anchor_x=0.0f, anchor_y=0.0f, win_scale=0.0f;
            float crop_l=0.0f, crop_t=0.0f, crop_r=0.0f, crop_b=0.0f;
            const char *win_pair=NULL;
            const char *after_label=(label_qe && *label_qe)?label_qe+1:NULL;
            if(a){ a+=7; while(*a && isspace((unsigned char)*a)) ++a; sscanf(a,"%127s",action_buf); if(action_buf[0]) ac=cwm_pool_copy(action_buf); }
            if(after_label){
                const char *st=strstr(after_label,"stops "), *svp=strstr(after_label,"stops $"), *br=strstr(after_label," brightness "), *tn=strstr(after_label," tint "), *tnv=strstr(after_label," tint $");
                if(st && !svp) stops=atoi(st+6);
                if(svp){ sscanf(svp+6,"%95s",stops_var_buf); if(stops_var_buf[0]=='$') sv=cwm_pool_copy(stops_var_buf+1); }
                if(br){ char raw[96]={0}; sscanf(br+12,"%95s",raw); if(raw[0]=='$') bv=cwm_pool_copy(raw+1); else brightness=(float)atof(raw); }
                if(tn && (!tnv || tn < tnv)){ char raw[96]={0}; sscanf(tn+6,"%95s",raw); if(raw[0] && raw[0]!='$') tint=(float)atof(raw); }
                if(tnv){ sscanf(tnv+6,"%95s",tint_var_buf); if(tint_var_buf[0]=='$') tv=cwm_pool_copy(tint_var_buf+1); }
                /* CWMenu 2 window specification keys (harmless on non-window
                 * elements: the host only reads them on the window rect). */
                {
                    const char *ka=strstr(after_label,"anchor ");
                    float aax, aay;
                    if(ka && sscanf(ka+7,"%f %f",&aax,&aay)==2){
                        anchor_set=1; anchor_x=aax; anchor_y=aay;
                    }
                }
                {
                    const char *ks=strstr(after_label,"size ");
                    if(ks) win_scale=(float)atof(ks+5);
                }
                {
                    const char *kc=strstr(after_label,"corner ");
                    if(kc) corner_sharp=(strncmp(kc+7,"rounded",7)==0)?1:0;
                }
                {
                    const char *kp=strstr(after_label,"crop ");
                    float cl,ct,cr2,cb;
                    if(kp && sscanf(kp+5,"%f %f %f %f",&cl,&ct,&cr2,&cb)==4){
                        crop_set=1; crop_l=cl; crop_t=ct; crop_r=cr2; crop_b=cb;
                    }
                }
                {
                    const char *kpr=strstr(after_label,"pair ");
                    char pair_buf[96]={0};
                    if(kpr && sscanf(kpr+5,"%95s",pair_buf)==1 && pair_buf[0])
                        win_pair=cwm_pool_copy(pair_buf);
                }
                {
                    /* `border <sides>` - comma/space separated side list:
                     * top right bottom left all fill none.  Default none
                     * (text draws as plain text). */
                    const char *kb=strstr(after_label,"border ");
                    if(kb){
                        const char *ks=kb+7;
                        int flags=0, done=0;
                        while(!done){
                            char tok[16]={0};
                            if(sscanf(ks,"%15[^,\t\r\n ]",tok)!=1 || !tok[0]) break;
                            if(!strcmp(tok,"top")) flags|=1;
                            else if(!strcmp(tok,"right")) flags|=2;
                            else if(!strcmp(tok,"bottom")) flags|=4;
                            else if(!strcmp(tok,"left")) flags|=8;
                            else if(!strcmp(tok,"all")) flags|=15;
                            else if(!strcmp(tok,"fill")) flags|=16;
                            else if(!strcmp(tok,"none")){ flags=0; done=1; }
                            else break;
                            if(!done){
                                ks+=strlen(tok);
                                while(*ks==','||isspace((unsigned char)*ks)) ++ks;
                            }
                        }
                        border_flags=flags;
                    }
                }
            }
            if(stops<0) stops=0;
            if(stops>6) stops=6;
            if(brightness>2.0f) brightness=2.0f;
            if(brightness>0.0f && brightness<0.25f) brightness=0.25f;
            if(tint>1.0f) tint=1.0f;
            if(tint<-1.0f) tint=0.0f;
            if(win_scale>0.0f && win_scale<0.75f) win_scale=0.75f;
            if(win_scale>1.25f) win_scale=1.25f;
            if(crop_set){
                if(crop_l<0.0f)crop_l=0.0f;
                if(crop_t<0.0f)crop_t=0.0f;
                if(crop_r<0.0f)crop_r=0.0f;
                if(crop_b<0.0f)crop_b=0.0f;
            }
            /* (audit 2026-10-01) the pair check was `(win_pair && !win_pair)` -
             * always false, so `pair` with an empty menu name passed
             * validation and later matched every unnamed menu. The
             * parallel to the action check is a non-empty token with an
             * empty stored name. */
            if(!n||!l||(action_buf[0]&&!ac)||(win_pair&&!win_pair[0])){errors++;break;}
            memset(t,0,sizeof(*t));
            t->name=n;t->x=x;t->y=y;t->w=w;t->h=h;t->kind=kind;t->label=l;t->action=ac;t->style=style;
            t->stops=stops;t->brightness=brightness;t->brightness_var=bv;t->tint=tint;t->tint_var=tv;t->stops_var=sv;
            t->scroll_owner=NULL;t->scroll_items=NULL;
            t->win_anchor_x=anchor_x;t->win_anchor_y=anchor_y;t->win_anchor_set=anchor_set;
            t->win_scale=win_scale;t->win_corner=corner_sharp;
            t->crop_l=crop_l;t->crop_t=crop_t;t->crop_r=crop_r;t->crop_b=crop_b;t->crop_set=crop_set;
            t->win_pair=win_pair;
            t->border_flags=border_flags;
            current->count++;
        }
    }
    if(scrollbar_block||theme_block) errors++;
    g_script_loaded=1;
    { int i; for(i=0;i<(int)g_dynamic_menu_count;++i) g_dynamic_counts[i]=g_dynamic_targets[i].count; }
    g_loaded_targets=targets;
    g_loaded_target_count=target_count;
    { size_t ti; for(ti=0;ti<target_count;++ti){ if(targets[ti].items&&targets[ti].capacity) memset(&targets[ti].items[targets[ti].count],0,sizeof(*targets[ti].items)); } }
    return errors?-errors:0;
}

int cwmenu_script_load(const char *path, CWMenuMenuScriptTarget *targets, size_t target_count)
{
    FILE *fp;
    size_t i;
    int rc;
    if(!path||!targets||target_count==0||target_count>CWM_MAX_TARGETS) return -1;
    fp=fopen(path,"rb");
    if(!fp) return -1;
    g_cwm_string_used=0;
    g_cwm_var_count=0;
    g_script_version=1;             /* no header seen yet -> legacy file */
    g_theme_count=0;
    { size_t ti; for(ti=0;ti<CWM_THEME_LABELS;ti++) g_theme_labels[ti]=NULL; }
    for(i=0;i<CWM_DYNAMIC_MENUS;i++){ g_dynamic_counts[i]=0; g_dynamic_names[i][0]='\0'; }
    g_dynamic_menu_count=0;
    for(i=0;i<target_count;++i){ targets[i].count=0; if(targets[i].items&&targets[i].capacity) memset(targets[i].items,0,targets[i].capacity*sizeof(*targets[i].items)); }
    rc=cwm_script_parse(fp,targets,target_count,0);
    fclose(fp);
    return rc;
}

int cwmenu_script_link(const char *path, CWMenuMenuScriptTarget *targets, size_t target_count)
{
    /* CWMenu 4 linking: parse INTO the existing state.  No resets here -
     * the string pool keeps growing (shared capacity), theme/project/var
     * values merge (parse code is already last-write/fill-only), and
     * `menu` blocks replace their previous content per the parser. */
    FILE *fp;
    int rc;
    if(!path||!targets||target_count==0||target_count>CWM_MAX_TARGETS) return -1;
    fp=fopen(path,"rb");
    if(!fp) return -1;
    rc=cwm_script_parse(fp,targets,target_count,1);
    fclose(fp);
    return rc;
}

/* Elements received by a named menu from the loaded script.  Returns -1 for
 * unknown names or when no script was loaded; 0 means the script removed or
 * renamed that menu (hosts use this to warn or hide the entry).  With a NULL
 * name it returns the number of compiled-in targets. */
int cwmenu_script_menu_count(const char *name)
{
    size_t i;
    if(!g_script_loaded) return -1;
    if(!name) return (int)g_loaded_target_count;
    if(g_loaded_targets){
        for(i=0;i<g_loaded_target_count;i++)
            if(g_loaded_targets[i].menu_name && !strcmp(g_loaded_targets[i].menu_name,name))
                return (int)g_loaded_targets[i].count;
    }
    for(i=0;i<g_dynamic_menu_count;i++)
        if(!strcmp(g_dynamic_names[i],name)) return (int)g_dynamic_counts[i];
    return -1;
}

void cwm_scrollbar_init(CWMenuMenuScrollbar *bar, float min_thumb_size)
{
    if(!bar) return;
    bar->content_size=0.0f; bar->viewport_size=0.0f; bar->offset=0.0f;
    bar->min_thumb_size=min_thumb_size>0.0f?min_thumb_size:24.0f; bar->track_size=0.0f;
}
void cwm_scrollbar_set(CWMenuMenuScrollbar *bar,float content_size,float viewport_size,float offset,float track_size)
{
    if(!bar)return;
    bar->content_size=content_size>0.0f?content_size:0.0f;
    bar->viewport_size=viewport_size>0.0f?viewport_size:0.0f;
    bar->track_size=track_size>0.0f?track_size:0.0f;
    cwm_scrollbar_set_offset(bar,offset);
}
float cwm_scrollbar_max(const CWMenuMenuScrollbar *bar){return !bar?0.0f:(bar->content_size>bar->viewport_size?bar->content_size-bar->viewport_size:0.0f);}
int cwm_scrollbar_visible(const CWMenuMenuScrollbar *bar){return cwm_scrollbar_max(bar)>0.0f&&bar->track_size>0.0f;}
float cwm_scrollbar_thumb_size(const CWMenuMenuScrollbar *bar)
{
    float thumb;
    if(!bar||bar->track_size<=0.0f)return 0.0f;
    if(bar->content_size<=0.0f||bar->viewport_size<=0.0f)return bar->track_size;
    thumb=bar->track_size*(bar->viewport_size/bar->content_size);
    if(thumb<bar->min_thumb_size)thumb=bar->min_thumb_size;
    if(thumb>bar->track_size)thumb=bar->track_size;
    return thumb;
}
float cwm_scrollbar_thumb_position(const CWMenuMenuScrollbar *bar)
{
    float max,thumb,travel;
    if(!bar)return 0.0f;
    max=cwm_scrollbar_max(bar); thumb=cwm_scrollbar_thumb_size(bar); travel=bar->track_size-thumb;
    if(max<=0.0f||travel<=0.0f)return 0.0f;
    return (cwm_clampf(bar->offset,0.0f,max)/max)*travel;
}
void cwm_scrollbar_set_offset(CWMenuMenuScrollbar *bar,float offset){if(bar)bar->offset=cwm_clampf(offset,0.0f,cwm_scrollbar_max(bar));}
float cwm_scrollbar_offset_from_position(const CWMenuMenuScrollbar *bar,float position)
{
    float thumb,travel,max;
    if(!bar)return 0.0f;
    thumb=cwm_scrollbar_thumb_size(bar); travel=bar->track_size-thumb; max=cwm_scrollbar_max(bar);
    if(max<=0.0f||travel<=0.0f)return 0.0f;
    position=cwm_clampf(position,0.0f,travel);
    return (position/travel)*max;
}
