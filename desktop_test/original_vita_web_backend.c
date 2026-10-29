#include "vita_web_backend.h"

#include <vita2d.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <curl/curl.h>

#define VW_MAX_HTML 262144
#define VW_MAX_TEXT 4096
#define VW_MAX_NODES 512
#define VW_MAX_CSS 131072
#define VW_MAX_RULES 256
#define VW_MAX_IMAGES 64
#define VW_MAX_FONTS 32

typedef struct {
    char selector[128];
    char declarations[2048];
} VitaWebCssRule;

typedef struct {
    char family[96];
    char src[1024];
    int weight;
    int italic;
    vita2d_font *font;
} VitaWebFont;

typedef struct {
    char tag[16];
    char text[VW_MAX_TEXT];
    char src[1024];
    float x, y, w, h;
    unsigned color;
    int color_explicit;
    unsigned background;
    int has_background;
    int has_border;
    float border_width;
    unsigned border_color;
    int border_style; /* 0=none, 1=solid, 2=dashed, 3=dotted, 4=double */
    float font_size;
    int bold;
    int centered;
    int display_none;
    int display_explicit;
    float margin_top, margin_right, margin_bottom, margin_left;
    float padding_left, padding_right, padding_top, padding_bottom;
    int is_image;
    vita2d_texture *image;
    vita2d_font *font;
    char font_family[96];
    int is_link;
    char href[1024];
    char link_target[32];
    int is_button;
    int button_pressed;
    int focusable_self;
    int disabled;
    int tabindex;
    int is_select;
    int is_textfield;
    int textfield_password;
    int textfield_multiline;
    char placeholder[256];
    int is_option;
    int option_selected;
    int select_open;
    int select_index;
    int select_hover_index;
    int select_touch_started_on_control;
    int select_touch_moved_to_option;
    /* CSS overflow policy: 0=auto, 1=visible, 2=hidden, 3=scroll. */
    int overflow_x;
    int overflow_y;
    int button_group;
    /* CSS display: 0=block, 1=inline, 2=inline-block. */
    int display_mode;
    int is_box;
    char vcm[24];
    char id[64];
    char cls[128];
    int vcm_active; /* persistent active state for the selected VCM binding */
    int control_modal;
    char control_modal_title[96];
} VitaWebNode;

typedef struct {
    int width, height, loaded;
    char url[1024];
    char base_url[1024];
    char *html;
    VitaWebNode nodes[VW_MAX_NODES];
    int node_count;
    VitaWebCssRule rules[VW_MAX_RULES];
    int rule_count;
    VitaWebFont fonts[VW_MAX_FONTS];
    int font_count;
    unsigned body_background, body_color;
    int body_background_explicit, body_color_explicit;
    int theme_dark;
    int scrollbars_visible;
    float cursor_y;
    float scroll_y;
    float scroll_x;
    float zoom;
    int overflow_x;
    int overflow_y;
    int focus_node;
    int select_focus_node;
    float cursor_x;
    float line_height_current;
    vita2d_font *system_font;
    vita2d_pgf *system_pgf;
    char pending_navigation[1024];
    char pending_target[32];
    int control_mode; /* 0=None, 1=Browser, 2=Standalone, 3=Touch */
    char button_wait[32]; /* optional vita-button-wait policy; empty = host default */
    int ime_active;
    int ime_node;
    SceWChar16 ime_initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
    SceWChar16 ime_buffer[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
} VitaWebRenderer;

static unsigned rgba(unsigned r,unsigned g,unsigned b,unsigned a){return RGBA8(r,g,b,a);}

static int ci_eq(const char *a,const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a==0 && *b==0;
}
static int ci_ncmp(const char *a,const char *b,size_t n) {
    size_t i; for(i=0;i<n;i++){unsigned char x=tolower((unsigned char)a[i]),y=tolower((unsigned char)b[i]);if(x!=y)return (int)x-(int)y;if(!x)return 0;}return 0;
}
static int hex_digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}

static unsigned parse_color(const char *s,unsigned fallback){
    int h[6]; if(!s)return fallback; while(isspace((unsigned char)*s))s++;
    if(*s=='#'){s++; if(strlen(s)>=6){int i;for(i=0;i<6;i++)h[i]=hex_digit(s[i]);if(h[0]>=0&&h[1]>=0&&h[2]>=0&&h[3]>=0&&h[4]>=0&&h[5]>=0)return rgba(h[0]*16+h[1],h[2]*16+h[3],h[4]*16+h[5],255);}
      if(strlen(s)>=3){int i;for(i=0;i<3;i++)h[i]=hex_digit(s[i]);if(h[0]>=0&&h[1]>=0&&h[2]>=0)return rgba(h[0]*17,h[1]*17,h[2]*17,255);}}
    if(ci_ncmp(s,"white",5)==0)return rgba(255,255,255,255);
    if(ci_ncmp(s,"black",5)==0)return rgba(0,0,0,255);
    if(ci_ncmp(s,"red",3)==0)return rgba(255,0,0,255);
    if(ci_ncmp(s,"green",5)==0)return rgba(0,128,0,255);
    if(ci_ncmp(s,"blue",4)==0)return rgba(0,0,255,255);
    if(ci_ncmp(s,"yellow",6)==0)return rgba(255,255,0,255);
    if(ci_ncmp(s,"orange",6)==0)return rgba(255,165,0,255);
    if(ci_ncmp(s,"purple",6)==0)return rgba(128,0,128,255);
    if(ci_ncmp(s,"gray",4)==0||ci_ncmp(s,"grey",4)==0)return rgba(128,128,128,255);
    if(ci_ncmp(s,"transparent",11)==0)return rgba(0,0,0,0);
    return fallback;
}

static const char *find_attr(const char *start,const char *end,const char *name,char *out,size_t cap){
    const char *p=start; size_t n=strlen(name); if(!out||!cap)return NULL;out[0]=0;
    while(p&&p<end){
        while(p<end&&isspace((unsigned char)*p))p++;
        if(p+n>end||ci_ncmp(p,name,n)!=0){while(p<end&&!isspace((unsigned char)*p))p++;continue;}
        p+=n;while(p<end&&isspace((unsigned char)*p))p++;if(p>=end||*p!='=')return NULL;p++;while(p<end&&isspace((unsigned char)*p))p++;
        char qchar=(*p=='"'||*p=='\'')?*p++:0;const char *q=p;
        while(q<end&&((qchar&&*q!=qchar)||(!qchar&&!isspace((unsigned char)*q)&&*q!='>')))q++;
        size_t len=(size_t)(q-p);if(len>=cap)len=cap-1;memcpy(out,p,len);out[len]=0;return out;
    } return NULL;
}

static void trim(char *s){
    char *p=s;size_t n;if(!s)return;while(isspace((unsigned char)*p))p++;if(p!=s)memmove(s,p,strlen(p)+1);
    n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;
}
static void append_text(char *out,size_t cap,const char *src,size_t len){
    size_t u=strlen(out),i;for(i=0;i<len&&u+1<cap;i++){unsigned char c=src[i];if(c=='\r')continue;if(c=='\n'||c=='\t')c=' ';
      if(c=='&'){if(i+4<len&&!strncmp(src+i,"&lt;",4)){c='<';i+=3;}else if(i+4<len&&!strncmp(src+i,"&gt;",4)){c='>';i+=3;}
      else if(i+5<len&&!strncmp(src+i,"&amp;",5)){c='&';i+=4;}else if(i+6<len&&!strncmp(src+i,"&quot;",6)){c='"';i+=5;}}
      out[u++]=(char)c;}out[u]=0;
}

static float css_length_px(const char *value, float font_size){
    char buf[64]; char *end; double number;
    if(!value) return 0.0f;
    snprintf(buf,sizeof(buf),"%s",value); trim(buf);
    number=strtod(buf,&end);
    if(end==buf) return 0.0f;
    while(isspace((unsigned char)*end)) end++;
    if(ci_ncmp(end,"em",2)==0) return (float)(number * (font_size * (16.0/0.75)));
    if(ci_ncmp(end,"rem",3)==0) return (float)(number * (16.0));
    if(ci_ncmp(end,"px",2)==0 || *end==0) return (float)number;
    return (float)number;
}

static float css_font_size(const char *value, float parent_size){
    char buf[64]; char *end; double number;
    if(!value) return parent_size;
    snprintf(buf,sizeof(buf),"%s",value); trim(buf);
    number=strtod(buf,&end);
    if(end==buf) return parent_size;
    while(isspace((unsigned char)*end)) end++;
    if(ci_ncmp(end,"em",2)==0) return (float)(parent_size * number);
    if(ci_ncmp(end,"rem",3)==0) return (float)(0.75 * number);
    if(ci_ncmp(end,"px",2)==0 || *end==0) return (float)(number / 32.0);
    return (float)(number / 32.0);
}

static int parse_lengths(const char *value, float font_size, float *a, float *b, float *c, float *d){
    char work[256], *tok; float vals[4]; int count=0;
    if(!value) return 0;
    snprintf(work,sizeof(work),"%s",value);
    tok=strtok(work," \t\r\n");
    while(tok && count<4){ vals[count++]=css_length_px(tok,font_size); tok=strtok(NULL," \t\r\n"); }
    if(a) *a = count>0 ? vals[0] : 0.0f;
    if(b) *b = count>1 ? vals[1] : 0.0f;
    if(c) *c = count>2 ? vals[2] : 0.0f;
    if(d) *d = count>3 ? vals[3] : 0.0f;
    return count;
}

static int css_overflow_value(const char *v){
    char b[32];
    if(!v) return 0;
    snprintf(b,sizeof(b),"%s",v); trim(b);
    if(ci_eq(b,"hidden")) return 2;
    if(ci_eq(b,"visible")) return 1;
    if(ci_eq(b,"scroll")) return 3;
    return 0; /* auto and unknown values use the renderer's automatic policy. */
}

static int css_border_style_value(const char *v){
    char b[32]; if(!v)return 0; snprintf(b,sizeof(b),"%s",v); trim(b);
    if(ci_eq(b,"none")||ci_eq(b,"hidden")) return 0;
    if(ci_eq(b,"solid")) return 1;
    if(ci_eq(b,"dashed")) return 2;
    if(ci_eq(b,"dotted")) return 3;
    if(ci_eq(b,"double")) return 4;
    /* Vita2D's primitive renderer cannot reproduce groove/ridge/inset/outset
       faithfully yet; use solid as the closest low-cost representation. */
    if(ci_eq(b,"groove")||ci_eq(b,"ridge")||ci_eq(b,"inset")||ci_eq(b,"outset")) return 1;
    return 0;
}

static int parse_border_shorthand(VitaWebNode *n,const char *v){
    char work[256]; char *tok; int found_style=0; float width=0.0f;
    if(!n||!v)return 0;
    snprintf(work,sizeof(work),"%s",v);
    tok=strtok(work," \t\r\n");
    while(tok){
        int bs=css_border_style_value(tok);
        if(bs){n->border_style=bs;found_style=1;}
        else {
            char *end=NULL;
            strtod(tok,&end);
            if(end!=tok){ while(isspace((unsigned char)*end))end++;
                if(ci_ncmp(end,"px",2)==0||ci_ncmp(end,"em",2)==0||ci_ncmp(end,"rem",3)==0||*end==0) width=css_length_px(tok,n->font_size);
            }
            unsigned old=n->border_color, parsed=parse_color(tok,old);
            if(parsed!=old || ci_eq(tok,"black")||ci_eq(tok,"white")||ci_eq(tok,"red")||ci_eq(tok,"green")||ci_eq(tok,"blue")||ci_eq(tok,"orange")||ci_eq(tok,"purple")) n->border_color=parsed;
        }
        tok=strtok(NULL," \t\r\n");
    }
    if(width>0.0f){n->border_width=width;n->has_border=1;}
    if(found_style){n->has_border=(n->border_style!=0); if(n->border_width<=0)n->border_width=1.0f;}
    return found_style;
}

static void style_apply(VitaWebNode *n,const char *style){
    char work[2048];char *p,*item;snprintf(work,sizeof(work),"%s",style);p=work;
    while(p){item=p;char *semi=strchr(p,';');if(semi){*semi=0;p=semi+1;}else p=NULL;char *colon=strchr(item,':');if(!colon)continue;*colon=0;char *k=item,*v=colon+1;trim(k);trim(v);
      for(char *q=k;*q;q++)*q=(char)tolower((unsigned char)*q);
      if(!strcmp(k,"color")){n->color=parse_color(v,n->color);n->color_explicit=1;}
      else if(!strcmp(k,"background")||!strcmp(k,"background-color")){
          /* For now the native backend treats a background declaration as a
             solid color. Ignore image/gradient portions instead of losing a
             valid color entirely. */
          char bg[128];snprintf(bg,sizeof(bg),"%s",v);char *sp=strpbrk(bg," \t");if(sp)*sp=0;
          unsigned parsed=parse_color(bg,n->background);
          if(parsed!=n->background||ci_eq(bg,"transparent")){n->background=parsed;n->has_background=1;}
      }
      else if(!strcmp(k,"border")){ parse_border_shorthand(n,v); }
      else if(!strcmp(k,"border-width")){ float x=css_length_px(v,n->font_size); if(x>0&&x<128){n->border_width=x;n->has_border=1;if(n->border_style==0)n->border_style=1;} }
      else if(!strcmp(k,"border-style")){ int bs=css_border_style_value(v); n->border_style=bs; n->has_border=(bs!=0); if(bs&&n->border_width<=0)n->border_width=1.0f; }
      else if(!strcmp(k,"border-color")){n->border_color=parse_color(v,n->border_color);n->has_border=1;if(n->border_style==0)n->border_style=1;}
      else if(!strcmp(k,"font-weight"))n->bold=ci_ncmp(v,"bold",4)==0||atoi(v)>=600;
      else if(!strcmp(k,"text-align"))n->centered=ci_ncmp(v,"center",6)==0;
      else if(!strcmp(k,"overflow")){
          int ov=css_overflow_value(v); n->overflow_x=ov; n->overflow_y=ov;
       }
      else if(!strcmp(k,"overflow-x")){ n->overflow_x=css_overflow_value(v); }
      else if(!strcmp(k,"overflow-y")){ n->overflow_y=css_overflow_value(v); }
      else if(!strcmp(k,"display")){
          n->display_explicit=1;
          if(ci_ncmp(v,"none",4)==0){n->display_none=1;}
          else if(ci_ncmp(v,"inline-block",12)==0){n->display_none=0;n->display_mode=2;}
          else if(ci_ncmp(v,"inline",6)==0){n->display_none=0;n->display_mode=1;}
          else if(ci_ncmp(v,"block",5)==0){n->display_none=0;n->display_mode=0;}
       }
      else if(!strcmp(k,"width")){float x=css_length_px(v,n->font_size);if(x>0&&x<4096)n->w=x;}
      else if(!strcmp(k,"height")){float x=css_length_px(v,n->font_size);if(x>0&&x<4096)n->h=x;}
      else if(!strcmp(k,"margin-top")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->margin_top=x;}
      else if(!strcmp(k,"margin-bottom")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->margin_bottom=x;}
      else if(!strcmp(k,"padding-left")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->padding_left=x;}
      else if(!strcmp(k,"padding-right")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->padding_right=x;}
      else if(!strcmp(k,"padding-top")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->padding_top=x;}
      else if(!strcmp(k,"padding-bottom")){float x=css_length_px(v,n->font_size);if(x>=0&&x<1024)n->padding_bottom=x;}
      else if(!strcmp(k,"margin")){
          float a=0,b=0,c=0,d=0; int cnt=parse_lengths(v,n->font_size,&a,&b,&c,&d);
          if(cnt==1){if(a>=0&&a<1024)n->margin_top=n->margin_bottom=a;}
          else if(cnt==2){if(a>=0&&a<1024)n->margin_top=n->margin_bottom=a;}
          else if(cnt==3){if(a>=0&&a<1024)n->margin_top=a;if(c>=0&&c<1024)n->margin_bottom=c;}
          else if(cnt>=4){if(a>=0&&a<1024)n->margin_top=a;if(c>=0&&c<1024)n->margin_bottom=c;}
      }
      else if(!strcmp(k,"padding")){
          float a=0,b=0,c=0,d=0; int cnt=parse_lengths(v,n->font_size,&a,&b,&c,&d);
          if(cnt==1){if(a>=0&&a<1024)n->padding_left=n->padding_right=n->padding_top=n->padding_bottom=a;}
          else if(cnt==2){if(a>=0&&a<1024)n->padding_top=n->padding_bottom=a;if(b>=0&&b<1024)n->padding_left=n->padding_right=b;}
          else if(cnt==3){if(a>=0&&a<1024)n->padding_top=a;if(b>=0&&b<1024)n->padding_left=n->padding_right=b;if(c>=0&&c<1024)n->padding_bottom=c;}
          else if(cnt>=4){if(a>=0&&a<1024)n->padding_top=a;if(b>=0&&b<1024)n->padding_right=b;if(c>=0&&c<1024)n->padding_bottom=c;if(d>=0&&d<1024)n->padding_left=d;}
      }
      else if(!strcmp(k,"font-size")){float x=css_font_size(v,n->font_size);if(x>0.1f&&x<8.0f)n->font_size=x;}
      else if(!strcmp(k,"font-family")){char *q=v;while(*q=='"'||*q=='\''||isspace((unsigned char)*q))q++;char *e=q+strlen(q);while(e>q&&(isspace((unsigned char)e[-1])||e[-1]=='"'||e[-1]=='\''))e--;size_t l=(size_t)(e-q);if(l>=sizeof(n->font_family))l=sizeof(n->font_family)-1;memcpy(n->font_family,q,l);n->font_family[l]=0;}
    }
}

static void add_css_rule(VitaWebRenderer *r,const char *sel,const char *decl){
    if(r->rule_count>=VW_MAX_RULES)return;
    snprintf(r->rules[r->rule_count].selector,sizeof(r->rules[0].selector),"%s",sel);
    snprintf(r->rules[r->rule_count].declarations,sizeof(r->rules[0].declarations),"%s",decl);trim(r->rules[r->rule_count].selector);r->rule_count++;
}
static void parse_css(VitaWebRenderer *r,const char *css){
    const char *p=css;while(p&&*p&&r->rule_count<VW_MAX_RULES){const char *a=strchr(p,'{');if(!a)break;const char *b=strchr(a+1,'}');if(!b)break;
      char sel[128],decl[2048];size_t sl=(size_t)(a-p);if(sl>=sizeof(sel))sl=sizeof(sel)-1;memcpy(sel,p,sl);sel[sl]=0;size_t dl=(size_t)(b-a-1);if(dl>=sizeof(decl))dl=sizeof(decl)-1;memcpy(decl,a+1,dl);decl[dl]=0;
      /* Strip comments. */
      char *c=strstr(sel,"/*");if(c)*c=0;trim(sel);if(*sel)add_css_rule(r,sel,decl);p=b+1;
    }
}
static int selector_matches_one(const char *sel,const char *tag,const char *id,const char *cls,int active){
    char s[256];snprintf(s,sizeof(s),"%s",sel);trim(s);
    int requires_active=0;
    char *pseudo=strstr(s,":active");
    if(pseudo){
        char after[64]={0};
        snprintf(after,sizeof(after),"%s",pseudo+7);
        if(after[0] && after[0]!='.' && after[0]!='#' && !isspace((unsigned char)after[0])) return 0;
        *pseudo=0;
        trim(s);
        requires_active=1;
    }
    if(requires_active && !active)return 0;
    if(!requires_active && active)return 0;
    char wanted_tag[32]={0},wanted_id[64]={0},wanted_cls[64]={0};const char *p=s;
    if(*p!='.'&&*p!='#'){size_t n=0;while(p[n]&&p[n]!='.'&&p[n]!='#'&&!isspace((unsigned char)p[n])&&n<31)n++;memcpy(wanted_tag,p,n);wanted_tag[n]=0;p+=n;}
    while(*p){char kind=*p++;char buf[64];size_t n=0;while(*p&&!isspace((unsigned char)*p)&&*p!='.'&&*p!='#'&&n<63)buf[n++]=*p++;buf[n]=0;if(kind=='.')snprintf(wanted_cls,sizeof(wanted_cls),"%s",buf);else if(kind=='#')snprintf(wanted_id,sizeof(wanted_id),"%s",buf);else return 0;}
    if(wanted_tag[0]&&!ci_eq(wanted_tag,tag))return 0;
    if(wanted_id[0]&&strcmp(wanted_id,id?id:"")!=0)return 0;
    if(wanted_cls[0]){
        if(!cls)return 0;
        char classes[128];snprintf(classes,sizeof(classes),"%s",cls);char *tok=strtok(classes," \t\r\n");int found=0;
        while(tok){if(strcmp(tok,wanted_cls)==0){found=1;break;}tok=strtok(NULL," \t\r\n");}
        if(!found)return 0;
    }
    return 1;
}
static int selector_matches(const char *sel,const char *tag,const char *id,const char *cls,int active){
    char list[256];snprintf(list,sizeof(list),"%s",sel);char *p=list;
    while(p){char *comma=strchr(p,',');if(comma)*comma=0;trim(p);if(*p&&selector_matches_one(p,tag,id,cls,active))return 1;if(!comma)break;p=comma+1;}
    return 0;
}
static void apply_rules(VitaWebRenderer *r,VitaWebNode *n,const char *id,const char *cls,int active){
    int i;for(i=0;i<r->rule_count;i++)if(selector_matches(r->rules[i].selector,n->tag,id,cls,active))style_apply(n,r->rules[i].declarations);
}
static int load_file(const char *path,char **out){
    SceIoStat st;SceUID fd;sceIoGetstat(path,&st);int size=(int)st.st_size;if(size<0||size>VW_MAX_HTML-1)return -1;
    fd=sceIoOpen(path,SCE_O_RDONLY,0);if(fd<0)return -2;char *buf=malloc(size+1);if(!buf){sceIoClose(fd);return -3;}int got=sceIoRead(fd,buf,size);sceIoClose(fd);if(got<0){free(buf);return -4;}buf[got]=0;*out=buf;return 0;
}
static int is_url(const char *s){return s&&(ci_ncmp(s,"http://",7)==0||ci_ncmp(s,"https://",8)==0);}
static void base_dir(const char *url,char *out,size_t cap){
    const char *q=strchr(url,'?');size_t n=q?(size_t)(q-url):strlen(url);if(n>=cap)n=cap-1;memcpy(out,url,n);out[n]=0;
    char *slash=strrchr(out,'/');if(slash)slash[1]=0;else out[0]=0;
}
static void resolve_url(const char *base,const char *ref,char *out,size_t cap){
    if(!ref||!*ref){out[0]=0;return;}if(is_url(ref)){snprintf(out,cap,"%s",ref);return;}
    if(ref[0]=='/')snprintf(out,cap,"file:///app0/web%s",ref);
    else {char dir[1024];base_dir(base,dir,sizeof(dir));snprintf(out,cap,"%s%s",dir,ref);}
}
typedef struct {char *data;size_t size;size_t cap;} CurlBuf;
static size_t curl_write(void *ptr,size_t sz,size_t nm,void *ud){CurlBuf *b=ud;size_t n=sz*nm;if(b->size+n+1>b->cap){size_t nc=b->cap?b->cap*2:16384;while(nc<b->size+n+1)nc*=2;char *p=realloc(b->data,nc);if(!p)return 0;b->data=p;b->cap=nc;}memcpy(b->data+b->size,ptr,n);b->size+=n;b->data[b->size]=0;return n;}
static int load_resource(VitaWebRenderer *r,const char *url,unsigned char **data,size_t *size){
    (void)r; if(!url)return -1;
    if(ci_ncmp(url,"file:///app0/",12)==0){char path[1024];snprintf(path,sizeof(path),"app0:/%s",url+12);char *p=NULL;int rc=load_file(path,&p);if(rc<0)return rc;SceIoStat st;sceIoGetstat(path,&st);*data=(unsigned char*)p;*size=(size_t)st.st_size;return 0;}
    if(is_url(url)){CURL *c=curl_easy_init();if(!c)return -10;CurlBuf b={0};curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,curl_write);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_USERAGENT,"VitaWebview/0.2.0");curl_easy_setopt(c,CURLOPT_TIMEOUT,15L);CURLcode ec=curl_easy_perform(c);curl_easy_cleanup(c);if(ec!=CURLE_OK){free(b.data);return -11;}*data=(unsigned char*)b.data;*size=b.size;return 0;}
    return -12;
}
static vita2d_texture *load_image(VitaWebRenderer *r,const char *url){
    unsigned char *data=NULL;size_t size=0;if(load_resource(r,url,&data,&size)<0)return NULL;vita2d_texture *t=NULL;
    const char *dot=strrchr(url,'.');if(dot&&ci_ncmp(dot,".jpg",4)==0)t=vita2d_load_JPEG_buffer(data, (unsigned long)size);else if(dot&&ci_ncmp(dot,".jpeg",5)==0)t=vita2d_load_JPEG_buffer(data, (unsigned long)size);else if(dot&&ci_ncmp(dot,".bmp",4)==0)t=vita2d_load_BMP_buffer(data);else t=vita2d_load_PNG_buffer(data);
    free(data);return t;
}
static int load_font(VitaWebRenderer *r,const char *family,const char *src){
    if(r->font_count>=VW_MAX_FONTS)return -1;
    char resolved[1024];resolve_url(r->base_url,src,resolved,sizeof(resolved));
    unsigned char *data=NULL;size_t size=0;if(load_resource(r,resolved,&data,&size)<0)return -2;
    vita2d_font *font=NULL;
    /* file:/// resources are inside the VPK. HTTP(S) fonts are cached to ux0:
       before loading so both local and remote fonts use vita2d_load_font_file. */
    char font_path[1024];
    if(is_url(resolved)){
        unsigned hash=2166136261u; for(const char *h=resolved;*h;h++) hash=(hash^(unsigned char)*h)*16777619u;
        char cache_dir[256];
        snprintf(cache_dir,sizeof(cache_dir),"ux0:data/VitaWebview/%s/cache",VITA_APP_DATA_DIR);
        sceIoMkdir("ux0:data/VitaWebview",0777);
        sceIoMkdir("ux0:data/VitaWebview/" VITA_APP_DATA_DIR,0777);
        sceIoMkdir(cache_dir,0777);
        snprintf(font_path,sizeof(font_path),"%s/font-%08X.ttf",cache_dir,hash);
        SceUID fd=sceIoOpen(font_path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
        if(fd<0){free(data);return -3;}
        int wr=sceIoWrite(fd,data,size);sceIoClose(fd);free(data);if(wr!=(int)size)return -3;
    } else {
        snprintf(font_path,sizeof(font_path),"app0:/%s",resolved+12); free(data);
    }
    font=vita2d_load_font_file(font_path);
    if(!font)return -3;
    VitaWebFont *f=&r->fonts[r->font_count++];snprintf(f->family,sizeof(f->family),"%s",family);snprintf(f->src,sizeof(f->src),"%s",resolved);f->font=font;
    return 0;
}
static vita2d_font *font_for(VitaWebRenderer *r,const char *family){
    int i;if(family&&*family)for(i=0;i<r->font_count;i++)if(ci_eq(r->fonts[i].family,family))return r->fonts[i].font;return r->system_font;
}
static void parse_font_faces(VitaWebRenderer *r,const char *css){
    const char *p=css;while((p=strstr(p,"@font-face"))){const char *a=strchr(p,'{'),*b=a?strchr(a,'}'):NULL;if(!a||!b)break;char block[2048];size_t n=(size_t)(b-a-1);if(n>=sizeof(block))n=sizeof(block)-1;memcpy(block,a+1,n);block[n]=0;
      char fam[96]={0},src[1024]={0};char *q=strstr(block,"font-family");if(q){q=strchr(q,':');if(q){q++;while(isspace((unsigned char)*q))q++;if(*q=='"'||*q=='\''){char qc=*q++;char *e=strchr(q,qc);if(e){size_t l=e-q;if(l>=sizeof(fam))l=sizeof(fam)-1;memcpy(fam,q,l);fam[l]=0;}}else sscanf(q,"%95[^;]",fam);trim(fam);}}
      q=strstr(block,"src");if(q){q=strchr(q,':');if(q){q++;char *u=strstr(q,"url(");if(u){u+=4;while(*u==' '||*u=='\''||*u=='"')u++;char *e=u;while(*e&&*e!=')'&&*e!='\''&&*e!='"')e++;size_t l=(size_t)(e-u);if(l>=sizeof(src))l=sizeof(src)-1;memcpy(src,u,l);src[l]=0;}}}
      if(fam[0]&&src[0])load_font(r,fam,src);
      p=b+1;
    }
}
static void load_external_css(VitaWebRenderer *r,const char *html){
    const char *p=html;while((p=strstr(p,"<link"))){const char *gt=strchr(p,'>');if(!gt)break;char rel[64],href[1024];if(find_attr(p+5,gt,"rel",rel,sizeof(rel))&&ci_ncmp(rel,"stylesheet",10)==0&&find_attr(p+5,gt,"href",href,sizeof(href))){
      char u[1024];resolve_url(r->base_url,href,u,sizeof(u));unsigned char *d=NULL;size_t sz=0;if(load_resource(r,u,&d,&sz)==0){parse_font_faces(r,(char*)d);parse_css(r,(char*)d);free(d);}}p=gt+1;}
}
static void parse_inline_styles(VitaWebRenderer *r,const char *html){
    const char *p=html;while((p=strstr(p,"<style"))){const char *gt=strchr(p,'>');if(!gt)break;const char *e=strstr(gt+1,"</style>");if(!e)break;size_t n=(size_t)(e-gt-1);char *css=malloc(n+1);if(!css)break;memcpy(css,gt+1,n);css[n]=0;parse_font_faces(r,css);parse_css(r,css);free(css);p=e+8;}
}
static int is_heading_tag(const char *tag){
    return tag && ((!strcmp(tag,"h1"))||(!strcmp(tag,"h2"))||(!strcmp(tag,"h3"))||
                   (!strcmp(tag,"h4"))||(!strcmp(tag,"h5"))||(!strcmp(tag,"h6")));
}
static int default_display_mode(const char *tag){
    if(!strcmp(tag,"span")||!strcmp(tag,"a")||!strcmp(tag,"strong")||!strcmp(tag,"em")||
       !strcmp(tag,"b")||!strcmp(tag,"i")||!strcmp(tag,"small")||!strcmp(tag,"label"))
        return 1; /* inline */
    if(!strcmp(tag,"button")||!strcmp(tag,"input")||!strcmp(tag,"select")||!strcmp(tag,"textarea")||!strcmp(tag,"textfield"))
        return 2; /* inline-block */
    return 0; /* block */
}
static void node_defaults(VitaWebRenderer *r,VitaWebNode *n,const char *tag){
    memset(n,0,sizeof(*n));snprintf(n->tag,sizeof(n->tag),"%s",tag);
    n->color=r->body_color;n->font_size=0.75f;n->background=rgba(0,0,0,0);
    n->border_color=rgba(0,0,0,255);n->border_style=0;n->display_mode=default_display_mode(tag);
    n->overflow_x=0; n->overflow_y=0;
    n->tabindex=0;

    if(!strcmp(tag,"h1")||!strcmp(tag,"h2")||!strcmp(tag,"h3")||!strcmp(tag,"h4")||!strcmp(tag,"h5")||!strcmp(tag,"h6")){
        n->bold=1;
        if(!strcmp(tag,"h1")) n->font_size=0.75f*2.0f;
        else if(!strcmp(tag,"h2")) n->font_size=0.75f*1.50f;
        else if(!strcmp(tag,"h3")) n->font_size=0.75f*1.17f;
        else if(!strcmp(tag,"h4")) n->font_size=0.75f*1.00f;
        else if(!strcmp(tag,"h5")) n->font_size=0.75f*0.83f;
        else n->font_size=0.75f*0.67f;
    }

    /* Small browser-like user-agent margins.  These are intentionally kept
       separate from padding: margins affect flow outside the painted box,
       while padding belongs inside the element background. */
    if(!strcmp(tag,"p")){
        n->margin_top=css_length_px("1em",n->font_size);
        n->margin_bottom=css_length_px("1em",n->font_size);
    } else if(!strcmp(tag,"h1")){
        n->margin_top=css_length_px("0.67em",n->font_size);
        n->margin_bottom=css_length_px("0.67em",n->font_size);
    } else if(!strcmp(tag,"h2")){
        n->margin_top=css_length_px("0.83em",n->font_size);
        n->margin_bottom=css_length_px("0.83em",n->font_size);
    } else if(!strcmp(tag,"h3")){
        n->margin_top=css_length_px("1em",n->font_size);
        n->margin_bottom=css_length_px("1em",n->font_size);
    } else if(!strcmp(tag,"h4")||!strcmp(tag,"h5")||!strcmp(tag,"h6")){
        n->margin_top=css_length_px("1.33em",n->font_size);
        n->margin_bottom=css_length_px("1.33em",n->font_size);
    }
    if(!strcmp(tag,"button")) n->margin_right=8.0f;
}
static int is_void_tag(const char *tag){
    return !strcmp(tag,"area")||!strcmp(tag,"base")||!strcmp(tag,"br")||!strcmp(tag,"col")||
           !strcmp(tag,"embed")||!strcmp(tag,"hr")||!strcmp(tag,"img")||!strcmp(tag,"input")||
           !strcmp(tag,"link")||!strcmp(tag,"meta")||!strcmp(tag,"param")||!strcmp(tag,"source")||
           !strcmp(tag,"track")||!strcmp(tag,"wbr");
}
static int is_ignored_tag(const char *tag){
    return !strcmp(tag,"script")||!strcmp(tag,"style")||!strcmp(tag,"head")||!strcmp(tag,"title")||
           !strcmp(tag,"noscript")||!strcmp(tag,"template");
}
static int find_matching_close(const char *open_gt, const char *end, const char *tag, const char **close_lt, const char **close_gt){
    const char *p=open_gt+1;int depth=1;
    while(p<end && *p){
        const char *lt=strchr(p,'<');if(!lt||lt>=end)break;
        const char *gt=strchr(lt,'>');if(!gt||gt>=end)break;
        if(lt[1]=='/' ){
            char t[16]={0};size_t n=0;const char *q=lt+2;
            while(q<gt&&!isspace((unsigned char)*q)&&*q!='>'&&n<sizeof(t)-1)t[n++]=(char)tolower((unsigned char)*q++);
            t[n]=0;if(ci_eq(t,tag)){depth--;if(depth==0){*close_lt=lt;*close_gt=gt;return 1;}}
        } else if(lt[1]!='!'&&lt[1]!='?'){
            char t[16]={0};size_t n=0;const char *q=lt+1;
            while(q<gt&&!isspace((unsigned char)*q)&&*q!='/'&&n<sizeof(t)-1)t[n++]=(char)tolower((unsigned char)*q++);
            t[n]=0;if(ci_eq(t,tag)&&!is_void_tag(t))depth++;
        }
        p=gt+1;
    }
    return 0;
}
static void node_from_parent(VitaWebRenderer *r,VitaWebNode *n,const char *tag,const VitaWebNode *parent){
    node_defaults(r,n,tag);
    if(parent){
        n->overflow_x=parent->overflow_x;
        n->overflow_y=parent->overflow_y;
        /* Inherit the properties that CSS actually inherits, but keep the
           renderer's built-in heading defaults.  The old implementation
           copied the parent's font-size and bold state onto every child,
           which meant <h1>/<h2>/... became the body's normal text size. */
        n->color=parent->color;
        /* text-align is inherited in CSS.  Preserve the parent's explicit
           centering state so descendants of a centered block are centered
           without making ordinary elements centered by default. */
        n->centered=parent->centered;
        if(!is_heading_tag(tag)){
            n->font_size=parent->font_size;
            n->bold=parent->bold;
        }
        n->font=parent->font;
        n->font_family[0]=0;
        if(parent->font_family[0])
            snprintf(n->font_family,sizeof(n->font_family),"%s",parent->font_family);

        /* Links are inherited by descendants for hit-testing.  An <a> may
           contain a block such as <div>, so the drawable box and all text
           inside that block must retain the anchor's destination. */
        if(parent->is_link){
            n->is_link=1;
            snprintf(n->href,sizeof(n->href),"%s",parent->href);
            snprintf(n->link_target,sizeof(n->link_target),"%s",parent->link_target);
        }
        if(parent->is_button){ n->is_button=1; n->button_pressed=parent->button_pressed; }
    }
}
static void wrap_and_add(VitaWebRenderer *r,VitaWebNode *n);
static void add_text_node(VitaWebRenderer *r,const char *tag,const char *text,size_t len,const VitaWebNode *style){
    if(r->node_count>=VW_MAX_NODES||!text||!len)return;
    VitaWebNode n;node_from_parent(r,&n,tag,style);
    n.margin_top=0.0f;
    n.margin_bottom=0.0f;
    if(style && style->is_link){
        n.is_link=1;
        snprintf(n.href,sizeof(n.href),"%s",style->href);
        snprintf(n.link_target,sizeof(n.link_target),"%s",style->link_target);
    }
    if(style && style->is_button){ n.is_button=1; n.button_pressed=style->button_pressed; }
    /* text-align is inherited by descendants.  node_from_parent already
       carries the inherited value; do not manufacture centering from the
       parent's display mode here, because an inline text node is not the
       same box as its block container. */
    append_text(n.text,sizeof(n.text),text,len);trim(n.text);if(!n.text[0])return;
    /* Text participates in the parent's inline formatting context.  It is
       not itself a block merely because its parent is a <div>/<p>/<header>.
       This is important for both browser-like inline flow and correct block
       height calculation. */
    n.display_mode=1;
    n.w=0.0f;
    n.x=r->cursor_x;
    n.y=r->cursor_y;
    if(style && style->is_button){
        n.display_mode=1;
    }
    if(n.font_family[0])n.font=font_for(r,n.font_family);
    wrap_and_add(r,&n);
}
static float measure_text_width(VitaWebRenderer *r,const VitaWebNode *n,const char *text){
    if(!text||!*text)return 0.0f;
    if(n->font){
        unsigned sz=(unsigned)(n->font_size*32.0f);
        if(sz<1)sz=1;
        return (float)vita2d_font_text_width(n->font,sz,text);
    }
    if(r->system_pgf){
        return (float)vita2d_pgf_text_width(r->system_pgf,n->font_size,text);
    }
    return (float)strlen(text)*(n->font_size*12.0f);
}

static float line_height(const VitaWebNode *n){
    /* Keep a little leading around the glyphs, like the browser's normal
       line box.  The baseline is positioned separately in draw_node(); it
       must not sit on the very bottom edge of the line box. */
    return n->font_size*28.0f;
}

static void parse_html_range(VitaWebRenderer *r,const char *start,const char *end,const VitaWebNode *parent){
    const char *p=start;
    float pending_block_margin_bottom=0.0f;
    int have_pending_block_margin=0;
    while(p<end && *p && r->node_count<VW_MAX_NODES){
        const char *lt=strchr(p,'<');
        if(!lt || lt>=end){
            if(p<end) add_text_node(r,parent?parent->tag:"p",p,(size_t)(end-p),parent);
            break;
        }
        if(lt>p) add_text_node(r,parent?parent->tag:"p",p,(size_t)(lt-p),parent);

        const char *gt=strchr(lt,'>');
        if(!gt || gt>=end)break;
        if(!strncmp(lt,"<!--",4)){
            const char *e=strstr(gt+1,"-->");
            p=(e&&e<end)?e+3:end;
            continue;
        }
        if(lt[1]=='/'||lt[1]=='!'||lt[1]=='?'){p=gt+1;continue;}

        char tag[16]={0};size_t ti=0;const char *q=lt+1;
        while(q<gt&&!isspace((unsigned char)*q)&&*q!='/'&&ti<sizeof(tag)-1)
            tag[ti++]=(char)tolower((unsigned char)*q++);
        tag[ti]=0;
        if(!tag[0]){p=gt+1;continue;}

        if(is_ignored_tag(tag)){
            const char *clt=NULL,*cgt=NULL;
            if(find_matching_close(gt,end,tag,&clt,&cgt))p=cgt+1;else p=gt+1;
            continue;
        }

        if(!strcmp(tag,"html")){
            VitaWebNode html_node;
            node_from_parent(r,&html_node,"html",parent);
            char html_style[2048],html_id[64]={0},html_cls[128]={0},control_mode[32]={0},button_wait[32]={0};
            if(find_attr(q,gt,"style",html_style,sizeof(html_style)))style_apply(&html_node,html_style);
            find_attr(q,gt,"id",html_id,sizeof(html_id));
            find_attr(q,gt,"class",html_cls,sizeof(html_cls));
            if(find_attr(q,gt,"vita-button-wait",button_wait,sizeof(button_wait))){
                trim(button_wait);
                snprintf(r->button_wait,sizeof(r->button_wait),"%s",button_wait);
            } else r->button_wait[0]=0;
            if(find_attr(q,gt,"vita-control-mode",control_mode,sizeof(control_mode))){
                trim(control_mode);
                if(ci_eq(control_mode,"browser")) r->control_mode=1;
                else if(ci_eq(control_mode,"standalone")) r->control_mode=2;
                else if(ci_eq(control_mode,"touch")) r->control_mode=3;
                else r->control_mode=0;
            } else r->control_mode=0;
            apply_rules(r,&html_node,html_id,html_cls,0);
            r->overflow_x=html_node.overflow_x;
            r->overflow_y=html_node.overflow_y;
            const char *clt=NULL,*cgt=NULL;
            if(find_matching_close(gt,end,tag,&clt,&cgt)){
                parse_html_range(r,gt+1,clt,&html_node); p=cgt+1;
            }else{
                parse_html_range(r,gt+1,end,&html_node); p=end;
            }
            continue;
        }

        if(!strcmp(tag,"body")){
            VitaWebNode body;
            node_from_parent(r,&body,"body",parent);
            char style[2048],id[64]={0},cls[128]={0};
            if(find_attr(q,gt,"style",style,sizeof(style)))style_apply(&body,style);
            find_attr(q,gt,"id",id,sizeof(id));find_attr(q,gt,"class",cls,sizeof(cls));
            apply_rules(r,&body,id,cls,0);
            r->body_color=body.color;
            r->body_color_explicit=body.color_explicit;
            r->overflow_x=body.overflow_x;
            r->overflow_y=body.overflow_y;
            r->body_background_explicit=body.has_background;
            if(body.has_background)r->body_background=body.background;
            const char *clt=NULL,*cgt=NULL;
            if(find_matching_close(gt,end,tag,&clt,&cgt)){
                parse_html_range(r,gt+1,clt,&body);p=cgt+1;
            }else{
                parse_html_range(r,gt+1,end,&body);p=end;
            }
            continue;
        }

        if(!strcmp(tag,"img")){
            char src[1024]={0};
            if(find_attr(q,gt,"src",src,sizeof(src))&&r->node_count<VW_MAX_NODES){
                VitaWebNode n;node_from_parent(r,&n,"img",parent);n.is_image=1;
                snprintf(n.src,sizeof(n.src),"%s",src);
                char u[1024];resolve_url(r->base_url,src,u,sizeof(u));n.image=load_image(r,u);
                n.w=320.0f;n.h=180.0f;
                char w[64],h[64];
                if(find_attr(q,gt,"width",w,sizeof(w)))n.w=(float)atof(w);
                if(find_attr(q,gt,"height",h,sizeof(h)))n.h=(float)atof(h);
                char style[2048],id[64]={0},cls[128]={0};
                if(find_attr(q,gt,"style",style,sizeof(style)))style_apply(&n,style);
                find_attr(q,gt,"id",id,sizeof(id));find_attr(q,gt,"class",cls,sizeof(cls));apply_rules(r,&n,id,cls,0);
                n.x=12.0f+n.padding_left;n.y=r->cursor_y+n.margin_top;
                r->nodes[r->node_count++]=n;
                r->cursor_y=n.y+n.h+n.margin_bottom;
            }
            p=gt+1;continue;
        }

        VitaWebNode n;
        node_from_parent(r,&n,tag,parent);
        char style[2048],id[64]={0},cls[128]={0},vcm[24]={0},control_modal[96]={0};
        if(find_attr(q,gt,"style",style,sizeof(style)))style_apply(&n,style);
        find_attr(q,gt,"id",id,sizeof(id));find_attr(q,gt,"class",cls,sizeof(cls));
        snprintf(n.id,sizeof(n.id),"%s",id); snprintf(n.cls,sizeof(n.cls),"%s",cls);
        {
            char tabindex_value[32]={0}, disabled_value[16]={0};
            if(find_attr(q,gt,"tabindex",tabindex_value,sizeof(tabindex_value))) n.tabindex=atoi(tabindex_value);
            if(find_attr(q,gt,"disabled",disabled_value,sizeof(disabled_value)) || strstr(q," disabled")!=NULL) n.disabled=1;
        }
        if(find_attr(q,gt,"vcm",vcm,sizeof(vcm))){ trim(vcm); snprintf(n.vcm,sizeof(n.vcm),"%s",vcm); }
        if(find_attr(q,gt,"vita-control-modal",control_modal,sizeof(control_modal))){
            trim(control_modal);
            n.control_modal=1;
            snprintf(n.control_modal_title,sizeof(n.control_modal_title),"%s",control_modal);
        }
        apply_rules(r,&n,id,cls,0);
        if(n.font_family[0])n.font=font_for(r,n.font_family);

        if(!strcmp(tag,"a")){
            char href[1024]={0},target[32]={0};
            n.is_link=1;
            n.focusable_self=1;
            if(find_attr(q,gt,"href",href,sizeof(href)))snprintf(n.href,sizeof(n.href),"%s",href);
            if(find_attr(q,gt,"vita-target",target,sizeof(target)))snprintf(n.link_target,sizeof(n.link_target),"%s",target);
            else snprintf(n.link_target,sizeof(n.link_target),"app");
        }

        if(!strcmp(tag,"textfield")){
            char value[VW_MAX_TEXT]={0};
            char placeholder[256]={0};
            char type[32]={0};
            char ml[16]={0};
            n.is_textfield=1;
            n.focusable_self=1;
            if(!n.display_explicit) n.display_mode=2;
            if(find_attr(q,gt,"value",value,sizeof(value))) snprintf(n.text,sizeof(n.text),"%s",value);
            if(find_attr(q,gt,"placeholder",placeholder,sizeof(placeholder))) snprintf(n.placeholder,sizeof(n.placeholder),"%s",placeholder);
            if(find_attr(q,gt,"type",type,sizeof(type)) && ci_eq(type,"password")) n.textfield_password=1;
            if(find_attr(q,gt,"multiline",ml,sizeof(ml))) n.textfield_multiline=1;
            if(!n.has_background){n.background=r->theme_dark?rgba(28,30,34,255):rgba(255,255,255,255);n.has_background=1;}
            if(!n.has_border){n.border_color=rgba(128,128,128,255);n.border_width=1.0f;n.border_style=1;n.has_border=1;}
            if(n.padding_left<=0)n.padding_left=9.0f;
            if(n.padding_right<=0)n.padding_right=9.0f;
            if(n.padding_top<=0)n.padding_top=8.0f;
            if(n.padding_bottom<=0)n.padding_bottom=8.0f;
            if(n.h<=0.0f)n.h=line_height(&n)+n.padding_top+n.padding_bottom+n.border_width*2.0f;
        }

        if(!strcmp(tag,"select")){
            n.is_select=1;
            n.focusable_self=1;
            n.centered=1;
            if(!n.display_explicit) n.display_mode=2;
            if(!n.has_background){n.background=r->theme_dark?rgba(0,0,0,255):rgba(245,245,245,255);n.has_background=1;}
            if(!n.has_border){n.border_color=rgba(128,128,128,255);n.border_width=1.0f;n.border_style=1;n.has_border=1;}
            if(n.padding_left<=0)n.padding_left=9.0f;
            if(n.padding_right<=0)n.padding_right=28.0f;
            if(n.padding_top<=0)n.padding_top=5.0f;
            if(n.padding_bottom<=0)n.padding_bottom=5.0f;
        }
        if(!strcmp(tag,"option")){
            n.is_option=1;
            char selected[16]={0};
            /* Boolean HTML attributes such as selected may appear with or
               without a value.  Presence is what matters. */
            if(find_attr(q,gt,"selected",selected,sizeof(selected)) ||
               (q<gt && strstr(q," selected")!=NULL)) n.option_selected=1;
        }

        if(!strcmp(tag,"button")){
            n.is_button=1;
            n.focusable_self=1;
            if(!n.display_explicit) n.display_mode=2;
            n.centered=1;
            n.button_group=r->node_count+1;
            /* Native HTML button defaults: black text on a light gray control. */
            if(!n.color_explicit) n.color=rgba(0,0,0,255);
            if(!n.has_background){n.background=rgba(224,224,224,255);n.has_background=1;}
            if(!n.has_border){n.border_color=rgba(128,128,128,255);n.border_width=1.0f;n.border_style=1;n.has_border=1;}
            if(n.padding_left<=0)n.padding_left=11.0f;
            if(n.padding_right<=0)n.padding_right=11.0f;
            if(n.padding_top<=0)n.padding_top=5.0f;
            if(n.padding_bottom<=0)n.padding_bottom=5.0f;
        }

        if(!strcmp(tag,"input") || !strcmp(tag,"textarea")) n.focusable_self=1;

        if(is_void_tag(tag)){p=gt+1;continue;}

        const char *clt=NULL,*cgt=NULL;
        int has_close=find_matching_close(gt,end,tag,&clt,&cgt);
        const char *child_end=has_close?clt:end;
        int block=(n.display_mode==0);

        /* Preserve the parent's flow position before child parsing. Child
           text must never move an inline-block control's own origin. */
        float flow_x=r->cursor_x;
        float flow_y=r->cursor_y;
        float flow_lh=r->line_height_current;

        /* A block starts a new formatting line. Finish any preceding inline
           content before placing its box. */
        if(block){
            if(r->cursor_x>12.0f && r->line_height_current>0.0f)
                r->cursor_y += r->line_height_current;
            r->cursor_x=12.0f;
            r->line_height_current=0.0f;
            /* Adjacent block vertical margins collapse: use the larger of
               the previous bottom margin and this element's top margin, not
               the sum. */
            if(have_pending_block_margin){
                r->cursor_y += (pending_block_margin_bottom>n.margin_top ?
                                pending_block_margin_bottom : n.margin_top);
                pending_block_margin_bottom=0.0f;
                have_pending_block_margin=0;
            }else{
                r->cursor_y += n.margin_top;
            }
        }
        float box_y=r->cursor_y;
        int box_index=-1;
        int child_start;
        /* A parent without padding/border participates in CSS margin
           collapsing with its first in-flow child.  We resolve the collapse
           after parsing the children below, because that is the first point
           where the actual child margin is known. */

        /*
         * Block boxes use the available width by default, exactly as a
         * browser's normal block formatting context does. Inline-block
         * controls (notably <button>) are different: they are shrink-to-fit
         * unless CSS explicitly supplies a width.
         */
        /* Padding and borders establish the content origin.  The previous
           renderer painted the padding but parsed children from the outer
           box origin, which made backgrounds and nested content disagree. */
        if(block){
            r->cursor_y=box_y+n.padding_top+n.border_width;
            r->cursor_x=12.0f+n.padding_left+n.border_width;
            r->line_height_current=0.0f;
        }
        if(block || n.has_background||n.has_border || n.vcm[0] || n.control_modal){
            if(r->node_count<VW_MAX_NODES){
                box_index=r->node_count++;
                r->nodes[box_index]=n;
                r->nodes[box_index].is_box=1;
                r->nodes[box_index].x=12.0f;
                /* Match the 0.1.0 renderer's visual box origin: the box
                   slightly surrounds the text rather than treating the
                   font's line-height as visible content. */
                r->nodes[box_index].y=box_y;
                if(n.w>0)r->nodes[box_index].w=n.w;
                else if(n.display_mode==0)r->nodes[box_index].w=(float)r->width-24.0f;
                else r->nodes[box_index].w=1.0f;
                r->nodes[box_index].h=(n.h>0?n.h:0.0f);
                r->nodes[box_index].text[0]=0;
            }
        }
        /* The generated border/background box is not one of this element's
           children. Start measuring margins and content after it. */
        child_start=r->node_count;

        parse_html_range(r,gt+1,child_end,&n);

        if(box_index>=0){
            r->nodes[box_index].vcm_active=n.vcm_active;
            r->nodes[box_index].control_modal=n.control_modal;
            snprintf(r->nodes[box_index].control_modal_title,sizeof(r->nodes[box_index].control_modal_title),"%s",n.control_modal_title);
            snprintf(r->nodes[box_index].vcm,sizeof(r->nodes[box_index].vcm),"%s",n.vcm);
            snprintf(r->nodes[box_index].id,sizeof(r->nodes[box_index].id),"%s",n.id);
            snprintf(r->nodes[box_index].cls,sizeof(r->nodes[box_index].cls),"%s",n.cls);
        }

        if(n.is_select){
            int chosen=-1, first=-1, option_count=0;
            /* option nodes are containers; copy their text from their first
               text child so the closed control and popup have real labels. */
            /* <option> content is represented by the option node plus one or
               more parsed descendants (normally a text node).  The option
               subtree belongs to the native picker, not to the page's normal
               paint/layout pass.  Hiding only the option container used to
               leave its text child visible underneath the closed <select>,
               producing the duplicated/ghosted labels visible on Vita.
               Hide the complete subtree while retaining the option node's
               copied label for the popup. */
            for(int i=child_start;i<r->node_count;i++){
                VitaWebNode *child=&r->nodes[i];
                if(!child->is_option)continue;
                if(first<0) first=i;
                if(child->option_selected) chosen=i;
                option_count++;

                for(int j=i+1;j<r->node_count;j++){
                    VitaWebNode *label=&r->nodes[j];
                    if(label->is_option)break;
                    if(label->text[0] && !child->text[0])
                        snprintf(child->text,sizeof(child->text),"%s",label->text);
                    label->display_none=1;
                }
                child->display_none=1;
            }
            if(chosen<0) chosen=first;
            n.select_index=0;
            n.select_hover_index=0;
            if(chosen>=0){
                int idx=0;
                for(int i=child_start;i<r->node_count;i++){
                    if(r->nodes[i].is_option){
                        if(i==chosen){n.select_index=idx;n.select_hover_index=idx;}
                        idx++;
                    }
                }
                if(box_index>=0 && r->focus_node==box_index)
                    r->select_focus_node=box_index;
                if(r->nodes[chosen].text[0]) snprintf(n.text,sizeof(n.text),"%s",r->nodes[chosen].text);
                if(!n.text[0] && first>=0) snprintf(n.text,sizeof(n.text),"%s",r->nodes[first].text);
                if(box_index>=0) snprintf(r->nodes[box_index].text,sizeof(r->nodes[box_index].text),"%s",n.text);
            }
            (void)option_count;
        }

        if(n.display_mode==2){
            /* Inline-block controls (especially <button>) participate in the
               current line.  They are shrink-to-fit unless CSS supplied a
               width.  Parsing the children may advance cursor_x, so restore
               the line origin while measuring and then position the children
               inside the finished control. */
            /* Children were parsed only to determine intrinsic size. */
            float saved_x=flow_x;
            float saved_y=flow_y;
            float content_w=0.0f,content_h=0.0f;
            int i;
            for(i=child_start;i<r->node_count;i++){
                VitaWebNode *child=&r->nodes[i];
                if(child->display_none)continue;
                if(child->text[0]){
                    float tw=measure_text_width(r,child,child->text);
                    if(tw>content_w)content_w=tw;
                }
                if(child->is_box && child->w>content_w)content_w=child->w;
                float bottom=(child->y+child->h)-box_y;
                if(bottom>content_h)content_h=bottom;
            }
            if(n.w<=0){
                /* CSS inline-block controls are shrink-to-fit.  Do not use
                   the parent's available width here: that is what caused
                   <button> to become a full-page-width control. */
                if(content_w<1.0f)content_w=32.0f;
                n.w=content_w+n.padding_left+n.padding_right+n.border_width*2.0f;
            }
            if(n.h<=0){
                float natural_h=(content_h>0.0f?content_h:line_height(&n))+
                                n.padding_top+n.padding_bottom+n.border_width*2.0f;
                /* Inline controls align on their own compact line box.  Do
                   not add another font line-height on top of measured text. */
                n.h=natural_h;
            }
            /* Wrap the control to the next line only when it no longer fits. */
            if(saved_x>12.0f && saved_x+n.margin_left+n.w+n.margin_right>(float)r->width-12.0f){
                r->cursor_y=saved_y+(flow_lh>0.0f?flow_lh:0.0f);
                r->cursor_x=12.0f;
                r->line_height_current=0.0f;
                box_y=r->cursor_y;
            }else{
                r->cursor_y=saved_y;
                r->cursor_x=saved_x;
            }
            n.x=r->cursor_x+n.margin_left;
            n.y=box_y;
            if(box_index>=0){
                r->nodes[box_index].x=n.x;
                r->nodes[box_index].y=n.y;
                r->nodes[box_index].w=n.w;
                r->nodes[box_index].h=n.h;
            }
            float inner_x=n.x+n.padding_left+n.border_width;
            float inner_w=n.w-n.padding_left-n.padding_right-n.border_width*2.0f;
            if(inner_w<0.0f)inner_w=0.0f;
            for(i=child_start;i<r->node_count;i++){
                VitaWebNode *child=&r->nodes[i];
                child->y=n.y+n.padding_top+n.border_width;
                if(child->is_box){
                    child->x=inner_x;
                    child->w=inner_w;
                }else if(n.is_button && child->text[0]){
                    float tw=measure_text_width(r,child,child->text);
                    child->x=inner_x+(inner_w-tw)*0.5f;
                    if(child->x<inner_x)child->x=inner_x;
                }else if(n.centered && child->text[0]){
                    float tw=measure_text_width(r,child,child->text);
                    child->x=inner_x+(inner_w-tw)*0.5f;
                    if(child->x<inner_x)child->x=inner_x;
                }else{
                    child->x=inner_x;
                }
            }
            r->cursor_x=n.x+n.w+n.margin_right;
            float control_lh=n.h+n.margin_top+n.margin_bottom;
            if(control_lh>r->line_height_current)r->line_height_current=control_lh;
        }else if(block){
            /*
             * Block formatting: the element's height is exactly the content
             * extent plus its own padding/border.  Do NOT add a synthetic
             * line-height or visual surround here.  A <div> with no padding
             * therefore ends exactly at the bottom of its last child.
             */
            float content_h=0.0f;
            int first_child=-1,last_child=-1;
            int i;
            for(i=child_start;i<r->node_count;i++){
                VitaWebNode *child=&r->nodes[i];
                if(child->display_none)continue;
                if(first_child<0)first_child=i;
                last_child=i;
                float bottom=(child->y+child->h)-box_y;
                if(bottom>content_h)content_h=bottom;
            }
            if(content_h<0.0f)content_h=0.0f;

            /* CSS margin collapse: when a block has no padding/border, its
               first and last in-flow child's vertical margins can collapse
               through the parent.  The collapsed margin must not be painted
               as part of the parent's background.  Shift the border box to
               the first child's content position and trim the trailing
               margin from the painted height. */
            float collapsed_top=0.0f,collapsed_bottom=0.0f;
            if(first_child>=0 && n.padding_top<=0.0f && n.border_width<=0.0f)
                collapsed_top=r->nodes[first_child].margin_top;
            if(last_child>=0 && n.padding_bottom<=0.0f && n.border_width<=0.0f)
                collapsed_bottom=r->nodes[last_child].margin_bottom;
            if(collapsed_top>0.0f){
                box_y+=collapsed_top;
                content_h-=collapsed_top;
                if(content_h<0.0f)content_h=0.0f;
            }
            /* A collapsed top margin is outside the painted border box.  The
             * box node was allocated before child parsing, so move it now;
             * otherwise the background begins at the pre-collapse position
             * and creates the visible strip above <h1>/<h2>/<p> content. */
            if(box_index>=0)r->nodes[box_index].y=box_y;

            /* The last child's margin is outside the parent's content box.
             * Keep it in the flow instead of subtracting it from the painted
             * height.  The old code did that and pulled the following sibling
             * upward, which is the source of the vertically scrunched layout. */
            float own_h=content_h+n.padding_top+n.padding_bottom+n.border_width*2.0f;
            if(n.h>0.0f)own_h=n.h;
            /* Block buttons are full-width by default, but their label is
               centered inside the button itself. Do not let the generic block
               text layout decide the label position against the page. */
            if(n.is_button){
                float inner_x=12.0f+n.padding_left+n.border_width;
                float inner_w=(n.w>0.0f?n.w:(float)r->width-24.0f)-n.padding_left-n.padding_right-n.border_width*2.0f;
                if(inner_w<0.0f)inner_w=0.0f;
                /* A button can contain several text nodes (for example a
                 * <span> followed by a nested <p>).  They are descendants of
                 * the same focus target, but they must still occupy separate
                 * line boxes instead of being painted at the same y-position.
                 * Stack their boxes vertically and grow an auto-height button
                 * to contain them. */
                float child_flow_h=0.0f;
                int visible_children=0;
                for(i=child_start;i<r->node_count;i++){
                    VitaWebNode *child=&r->nodes[i];
                    if(child->display_none)continue;
                    float ch=child->h>0.0f?child->h:line_height(child);
                    child_flow_h+=child->margin_top+ch+child->margin_bottom;
                    visible_children++;
                }
                if(visible_children>0 && n.h<=0.0f)
                    own_h=child_flow_h+n.padding_top+n.padding_bottom+n.border_width*2.0f;
                float child_y=box_y+n.padding_top+n.border_width;
                for(i=child_start;i<r->node_count;i++){
                    VitaWebNode *child=&r->nodes[i];
                    if(child->display_none)continue;
                    child_y+=child->margin_top;
                    child->y=child_y;
                    if(child->text[0]){
                        float tw=measure_text_width(r,child,child->text);
                        child->x=inner_x+(inner_w-tw)*0.5f;
                        if(child->x<inner_x)child->x=inner_x;
                    }else if(child->is_box){
                        child->x=inner_x;
                        child->w=inner_w;
                    }
                    child_y+=(child->h>0.0f?child->h:line_height(child))+child->margin_bottom;
                }
            }
            if(box_index>=0){
                r->nodes[box_index].w=(n.w>0.0f?n.w:(float)r->width-24.0f);
                r->nodes[box_index].h=own_h;
            }
            /* Keep the trailing margin pending so a following block can
               collapse it with its own top margin.  If this is the last child
               in a parent, its margin is considered by the parent's normal
               last-child collapse logic instead of being painted. */
            pending_block_margin_bottom=n.margin_bottom;
            if(collapsed_bottom>pending_block_margin_bottom)
                pending_block_margin_bottom=collapsed_bottom;
            have_pending_block_margin=1;
            r->cursor_y=box_y+own_h;
            r->cursor_x=12.0f;
            r->line_height_current=0.0f;
        }else{
            /* Explicit display:inline on a button is still an intrinsic-size
               control. It must not collapse to the width of its containing
               block or use the generic zero-width inline box path. */
            if(n.is_button){
                float content_w=0.0f,content_h=0.0f;
                int i;
                for(i=child_start;i<r->node_count;i++){
                    VitaWebNode *child=&r->nodes[i];
                    if(child->display_none)continue;
                    if(child->text[0]){
                        float tw=measure_text_width(r,child,child->text);
                        if(tw>content_w)content_w=tw;
                    }
                    if(child->h>content_h)content_h=child->h;
                }
                if(n.w<=0.0f)n.w=content_w+n.padding_left+n.padding_right+n.border_width*2.0f;
                if(n.h<=0.0f)n.h=(content_h>0.0f?content_h:line_height(&n))+n.padding_top+n.padding_bottom+n.border_width*2.0f;
                n.x=flow_x; n.y=flow_y;
                if(n.x>12.0f && n.x+n.w>(float)r->width-12.0f){
                    r->cursor_y=flow_y+(r->line_height_current>0.0f?r->line_height_current:0.0f);
                    r->cursor_x=12.0f;
                    n.x=12.0f; n.y=r->cursor_y;
                }
                float inner_x=n.x+n.padding_left+n.border_width;
                float inner_w=n.w-n.padding_left-n.padding_right-n.border_width*2.0f;
                if(inner_w<0.0f)inner_w=0.0f;
                for(i=child_start;i<r->node_count;i++){
                    VitaWebNode *child=&r->nodes[i];
                    if(child->display_none)continue;
                    child->y=n.y+n.padding_top+n.border_width+child->margin_top;
                    if(child->text[0]){
                        if(n.centered){
                            float tw=measure_text_width(r,child,child->text);
                            child->x=inner_x+(inner_w-tw)*0.5f;
                            if(child->x<inner_x)child->x=inner_x;
                        }else{
                            child->x=inner_x;
                        }
                    }else{
                        child->x=inner_x;
                    }
                }
                if(box_index>=0){
                    r->nodes[box_index].x=n.x; r->nodes[box_index].y=n.y;
                    r->nodes[box_index].w=n.w; r->nodes[box_index].h=n.h;
                }
                r->cursor_x=n.x+n.w;
                if(n.h>r->line_height_current)r->line_height_current=n.h;
            }else if(box_index>=0){
                /* Inline containers do not create a new line. */
                float content_h=r->line_height_current;
                if(content_h<1.0f)content_h=n.h;
                r->nodes[box_index].h=content_h;
                r->nodes[box_index].x=12.0f;
            }
        }

        p=has_close?cgt+1:end;
    }
    if(have_pending_block_margin)
        r->cursor_y += pending_block_margin_bottom;
}

static void wrap_and_add(VitaWebRenderer *r,VitaWebNode *n){
    if(n->display_none||!n->text[0])return;

    /* Inline text uses the remaining line width.  Block text gets the full
       content width so text-align:center can be resolved against the block,
       rather than against the current cursor position. */
    float available;
    if(n->centered && n->display_mode==1){
        available=(float)r->width-24.0f-n->padding_left-n->padding_right;
    }else{
        available=(float)r->width-24.0f-r->cursor_x-n->padding_left-n->padding_right;
    }
    if(available<1.0f)available=1.0f;

    char src[VW_MAX_TEXT];snprintf(src,sizeof(src),"%s",n->text);
    char *p=src;
    while(*p&&r->node_count<VW_MAX_NODES){
        char line[VW_MAX_TEXT]={0};int count=0;char *last_space=NULL;
        while(*p&&count<(int)sizeof(line)-1){
            if(*p=='\n'){p++;break;}
            line[count++]=*p++;
            if(line[count-1]==' ')last_space=&line[count-1];
            if(measure_text_width(r,n,line)>available&&count>1){
                if(last_space){
                    *last_space=0;p=last_space+1;while(*p==' ')p++;
                }else{
                    line[--count]=0;p--;
                }
                break;
            }
        }
        line[count]=0;trim(line);
        if(!*line)continue;

        VitaWebNode *x=&r->nodes[r->node_count++];
        *x=*n;snprintf(x->text,sizeof(x->text),"%s",line);
        float tw=measure_text_width(r,x,line);
        x->w=tw+n->padding_left+n->padding_right+n->border_width*2.0f;
        x->y=r->cursor_y;
        x->h=line_height(x);

        if(n->centered && n->display_mode==1){
            float content_left=12.0f+n->padding_left;
            float content_width=(float)r->width-24.0f-n->padding_left-n->padding_right;
            x->x=content_left+(content_width-tw)*0.5f;
            if(x->x<content_left)x->x=content_left;
        }else{
            x->x=r->cursor_x+n->padding_left;
            if(r->cursor_x>12.0f && x->x+x->w>(float)r->width-12.0f){
                r->cursor_y+=r->line_height_current>0.0f?r->line_height_current:line_height(x);
                r->cursor_x=12.0f;
                x->x=r->cursor_x+n->padding_left;
                x->y=r->cursor_y;
            }
        }

        float lh=x->h;
        if(lh>r->line_height_current)r->line_height_current=lh;
        if(n->centered && n->display_mode==1){
            /* Centered text in a block must not advance the inline cursor by
               its painted width. The containing block owns the line. */
            r->cursor_x=(float)r->width-12.0f;
        }else{
            r->cursor_x=x->x+x->w;
        }
    }
}



/*
 * HTML parsing/rendering path.
 *
 * The 0.1.0 renderer worked because each visible element was turned into a
 * drawable node. The later linear compatibility parser tried to recover that
 * behavior by finding the first </...> after an opening tag. That is not safe
 * for nested HTML: <header><h1>...</h1></header> made the header consume the
 * h1 as literal text, which is exactly why tags were appearing on screen.
 *
 * Use the recursive tree-aware parser below instead. It finds the matching
 * closing tag, applies inherited styles to descendants, and creates drawable
 * text/background nodes for the actual elements.
 */
static void parse_html(VitaWebRenderer *r){
    VitaWebNode root;

    r->node_count=0;
    r->control_mode=0;
    /* Start the document at the top of the viewport.  The previous build
       introduced a document-top inset here, but that was not intended to be
       part of the page's CSS layout.  Keep the 24px horizontal Vita safe-area
       inset in the element positioning code; do not add vertical page chrome. */
    r->cursor_y=0.0f;
    r->cursor_x=12.0f;
    r->line_height_current=0.0f;
    r->body_background=r->theme_dark?rgba(0,0,0,255):rgba(245,245,245,255);
    r->body_color=r->theme_dark?rgba(235,238,242,255):rgba(25,25,30,255);
    r->body_background_explicit=0;
    r->body_color_explicit=0;
    r->overflow_x=0;
    r->overflow_y=0;
    /* The recursive parser is the authoritative path.  It preserves the
       element tree, so tags inside containers remain elements rather than
       becoming literal text such as "<h1>...". */
    node_defaults(r,&root,"body");
    root.color=r->body_color;
    root.background=r->body_background;
    root.has_background=0;
    parse_html_range(r,r->html,r->html+strlen(r->html),&root);
    if(!r->body_background_explicit) r->body_background=r->theme_dark?rgba(0,0,0,255):rgba(245,245,245,255);
    if(!r->body_color_explicit) r->body_color=r->theme_dark?rgba(235,238,242,255):rgba(25,25,30,255);
}

static int backend_init(VitaWebBackend *backend,int width,int height){
    VitaWebRenderer *r=calloc(1,sizeof(*r));if(!r)return -1;r->width=width;r->height=height;r->zoom=1.0f;r->overflow_x=0;r->overflow_y=0;r->theme_dark=0;r->scrollbars_visible=1;r->body_background=rgba(245,245,245,255);r->body_color=rgba(25,25,30,255);r->system_pgf=vita2d_load_default_pgf();
    /* Custom TTF/OTF fonts use vita2d_font; the Vita system PGF remains the fallback. */
    backend->userdata=r;curl_global_init(CURL_GLOBAL_DEFAULT);return 0;
}
static int backend_load(VitaWebBackend *backend,const char *url){
    VitaWebRenderer *r=backend->userdata;if(!r||!url)return -1;
    r->pending_navigation[0]=0; r->pending_target[0]=0;const char *prefix="file:///app0/";if(strncmp(url,prefix,12)!=0)return VITA_WEB_BACKEND_NOT_IMPLEMENTED;
    const char *resource=url+12;char path[1024];const char *end=strpbrk(resource,"?#");size_t len=end?(size_t)(end-resource):strlen(resource);if(len+6>=sizeof(path))return -2;memcpy(path,"app0:/",6);memcpy(path+6,resource,len);path[len+6]=0;
    free(r->html);r->html=NULL;if(load_file(path,&r->html)<0)return -3;snprintf(r->url,sizeof(r->url),"%s",url);base_dir(url,r->base_url,sizeof(r->base_url));r->rule_count=0;r->font_count=0;r->scroll_y=0.0f;r->scroll_x=0.0f;r->focus_node=-1;
    load_external_css(r,r->html);parse_inline_styles(r,r->html);parse_html(r);r->loaded=1;return 0;
}
static int backend_eval(VitaWebBackend *backend,const char *javascript){(void)backend;(void)javascript;return 0;}
static void renderer_textfield_update(VitaWebRenderer *r);
static int backend_update(VitaWebBackend *backend){VitaWebRenderer *r=backend?(VitaWebRenderer *)backend->userdata:NULL;if(r)renderer_textfield_update(r);return 0;}
static float renderer_content_origin_x(const VitaWebRenderer *r);
static float renderer_content_origin_y(const VitaWebRenderer *r);
static void draw_node(VitaWebRenderer *r,VitaWebNode *n){
    if(n->display_none)return;
    /*
     * VCM activation is a persistent page state, not the browser's transient
     * :active mouse/keyboard state.  Apply matching :active CSS to a temporary
     * node so switching VCM controls never permanently mutates the base style.
     */
    VitaWebNode active_node;
    if(n->vcm_active){
        active_node=*n;
        apply_rules(r,&active_node,n->id,n->cls,1);
        n=&active_node;
    }
    float z=r->zoom>0.01f?r->zoom:1.0f;
    float origin_x=renderer_content_origin_x(r);
    float origin_y=renderer_content_origin_y(r);
    float sx=(n->x-origin_x)*z-r->scroll_x;
    float sy=(n->y-origin_y)*z-r->scroll_y;
    float sw=n->w*z, sh=n->h*z;
    if(n->is_image){
        if(n->image)vita2d_draw_texture_part_scale(n->image,0,0,vita2d_texture_get_width(n->image),vita2d_texture_get_height(n->image),sx,sy,sw,sh);
        return;
    }
    if(n->has_background){
        unsigned bg=n->background;
        if(n->is_button && n->button_pressed){
            unsigned a=(bg>>24)&255, rr=(bg>>16)&255, gg=(bg>>8)&255, bb=bg&255;
            bg=rgba((unsigned)(rr*0.72f),(unsigned)(gg*0.72f),(unsigned)(bb*0.72f),a);
        }
        vita2d_draw_rectangle(sx,sy,sw,sh,bg);
    }
    if(n->has_border&&n->border_width>0&&n->border_style!=0){
        float bw=n->border_width*z;
        if(n->border_style==1){
            vita2d_draw_rectangle(sx,sy,sw,bw,n->border_color);
            vita2d_draw_rectangle(sx,sy+sh-bw,sw,bw,n->border_color);
            vita2d_draw_rectangle(sx,sy,bw,sh,n->border_color);
            vita2d_draw_rectangle(sx+sw-bw,sy,bw,sh,n->border_color);
        } else if(n->border_style==4){
            /* CSS double border: two distinct strokes separated by a gap.
               On the Vita's pixel-sized raster, a 1px border must still
               produce two 1px strokes rather than collapsing into one. */
            float q=bw/3.0f; if(q<1.0f)q=1.0f;
            float inner=2.0f*q;
            float inner_w=sw-4.0f*q;
            float inner_h=sh-4.0f*q;
            if(inner_w>0.0f && inner_h>0.0f){
                vita2d_draw_rectangle(sx,sy,sw,q,n->border_color);
                vita2d_draw_rectangle(sx,sy+sh-q,sw,q,n->border_color);
                vita2d_draw_rectangle(sx,sy,q,sh,n->border_color);
                vita2d_draw_rectangle(sx+sw-q,sy,q,sh,n->border_color);
                vita2d_draw_rectangle(sx+inner,sy+inner,inner_w,q,n->border_color);
                vita2d_draw_rectangle(sx+inner,sy+sh-inner-q,inner_w,q,n->border_color);
                vita2d_draw_rectangle(sx+inner,sy+inner,q,inner_h,n->border_color);
                vita2d_draw_rectangle(sx+sw-inner-q,sy+inner,q,inner_h,n->border_color);
            } else {
                /* Extremely small boxes cannot physically contain two
                   separated strokes; keep a valid outer border instead. */
                vita2d_draw_rectangle(sx,sy,sw,q,n->border_color);
                vita2d_draw_rectangle(sx,sy+sh-q,sw,q,n->border_color);
                vita2d_draw_rectangle(sx,sy,q,sh,n->border_color);
                vita2d_draw_rectangle(sx+sw-q,sy,q,sh,n->border_color);
            }
        } else {
            float dash=bw*4.0f; if(dash<4)dash=4;
            for(float xx=sx;xx<sx+sw;xx+=dash*2) vita2d_draw_rectangle(xx,sy,fminf(dash,sx+sw-xx),bw,n->border_color);
            for(float xx=sx;xx<sx+sw;xx+=dash*2) vita2d_draw_rectangle(xx,sy+sh-bw,fminf(dash,sx+sw-xx),bw,n->border_color);
            for(float yy=sy;yy<sy+sh;yy+=dash*2) vita2d_draw_rectangle(sx,yy,bw,fminf(dash,sy+sh-yy),n->border_color);
            for(float yy=sy;yy<sy+sh;yy+=dash*2) vita2d_draw_rectangle(sx+sw-bw,yy,bw,fminf(dash,sy+sh-yy),n->border_color);
        }
    }
    if(r->focus_node==(int)(n-r->nodes)){
        const float fw=2.0f;
        unsigned fc=rgba(255,220,70,255);
        float ff=fw*z;
        vita2d_draw_rectangle(sx-ff,sy-ff,sw+2.0f*ff,ff,fc);
        vita2d_draw_rectangle(sx-ff,sy+sh,sw+2.0f*ff,ff,fc);
        vita2d_draw_rectangle(sx-ff,sy,ff,sh,fc);
        vita2d_draw_rectangle(sx+sw,sy,ff,sh,fc);
    }
    /* Keep the closed <select> label visible even while the native picker is
       open. The picker is rendered after the page and is positioned outside
       the control, so the selected label must remain part of the control
       chrome rather than disappearing while the menu is open. */
    if(!n->text[0] && !n->is_textfield)return;
    float text_metric=line_height(n)*z;
    float baseline=sy+n->font_size*21.0f*z;
    if(n->is_button){
        float content_top=sy+(n->padding_top+n->border_width)*z;
        float content_h=(n->h-n->padding_top-n->padding_bottom-n->border_width*2.0f)*z;
        if(content_h<text_metric)content_h=text_metric;
        baseline=content_top+(content_h+text_metric)*0.5f;
    }
    unsigned text_color=n->color;
    /* Button labels inherit the button's UA text color.  Keep this decision
       at draw time as well as during theme propagation so a button can never
       flash black when switching between light/dark defaults.  Explicit CSS
       color still wins. */
    if(n->is_button && !n->color_explicit)
        text_color=rgba(0,0,0,255);
    if(n->is_textfield){
        float inner_x=sx+(n->padding_left+n->border_width)*z;
        float inner_y=sy+(n->padding_top+n->border_width)*z;
        float text_metric_tf=line_height(n)*z;
        float text_y=inner_y+(sh-(n->padding_top+n->padding_bottom+2.0f*n->border_width)*z+text_metric_tf)*0.5f-2.0f*z;
        const char *shown=n->text[0]?n->text:n->placeholder;
        unsigned tc=n->text[0]?text_color:(r->theme_dark?rgba(150,156,166,255):rgba(120,124,132,255));
        char mask[256];
        if(n->textfield_password && n->text[0]){
            size_t m=strlen(n->text); if(m>=sizeof(mask))m=sizeof(mask)-1;
            for(size_t mi=0;mi<m;mi++)
                mask[mi]='*';
            mask[m]=0;
            shown=mask;
        }
        if(n->font) vita2d_font_draw_text(n->font,inner_x,text_y,tc,(unsigned)(n->font_size*32*z),shown);
        else if(r->system_pgf) vita2d_pgf_draw_text(r->system_pgf,inner_x,text_y,tc,n->font_size*z,shown);
        if(r->focus_node==(int)(n-r->nodes) && !r->ime_active){
            float cw=measure_text_width(r,n,n->text)*z;
            float cy=inner_y;
            float ch=sh-(n->padding_top+n->padding_bottom+2.0f*n->border_width)*z;
            vita2d_draw_rectangle(inner_x+cw+1.0f*z,cy,1.5f*z,ch,tc);
        }
    }

    if(n->is_select){
        /* Native <select> labels are centered in the control's content box,
           using the renderer's real font metrics rather than a character
           count approximation.  The old baseline also sat near the top of
           the box, which made centered text look vertically misaligned. */
        float inner_x=sx+(n->padding_left+n->border_width)*z;
        float inner_w=(n->w-n->padding_left-n->padding_right-2.0f*n->border_width)*z;
        float tw=measure_text_width(r,n,n->text)*z;
        float text_metric=line_height(n)*z;
        sx=inner_x+(inner_w-tw)*0.5f;
        if(sx<inner_x)sx=inner_x;
        /* Vita2D font baselines include glyph ascent/leading differently from
           the CSS line-box midpoint.  Lift the baseline slightly so the
           visible glyphs, rather than the font box, are vertically centered. */
        baseline=sy+(sh+text_metric)*0.5f-3.0f*z;
        if(n->font) vita2d_font_draw_text(n->font,sx,baseline,text_color,(unsigned)(n->font_size*32*z),n->text);
        else if(r->system_pgf) vita2d_pgf_draw_text(r->system_pgf,sx,baseline,text_color,n->font_size*z,n->text);

        /* A small native disclosure arrow makes it clear that this is a
           picker even when the original WebKit select chrome is unavailable. */
        float arrow_x=sx+(tw>inner_w?inner_w:tw)+10.0f*z;
        float arrow_cx=(n->x+n->w-n->padding_right-n->border_width)*z-r->scroll_x-6.0f*z;
        if(arrow_x>arrow_cx-8.0f*z)arrow_x=arrow_cx-8.0f*z;
        float arrow_y=sy+sh*0.5f;
        /* vita2d does not expose a public triangle primitive on all VitaSDK
           versions.  Draw the disclosure triangle as a stack of horizontal
           rectangles so this remains compatible with older SDK headers. */
        const float aw=5.0f*z;
        const float ah=6.0f*z;
        const float ay=arrow_y-2.0f*z;
        for(int row=0; row<6; ++row){
            float half=aw*(1.0f-(float)row/6.0f);
            float rw=half*2.0f;
            float rh=ah/6.0f+0.35f*z;
            vita2d_draw_rectangle(arrow_cx-half,ay+(float)row*(ah/6.0f),rw,rh,text_color);
        }
        return;
    }
    if(!n->is_textfield && !n->is_select){
        if(n->font) vita2d_font_draw_text(n->font,sx,baseline,text_color,(unsigned)(n->font_size*32*z),n->text);
        else if(r->system_pgf) vita2d_pgf_draw_text(r->system_pgf,sx,baseline,text_color,n->font_size*z,n->text);
    }
}

/* Keep the document inside a small browser-like safe area.  This is a
   renderer viewport inset, not CSS margin: it must remain present even when
   the page itself has no body margin.  The scrollbars stay on the physical
   screen edge, like a normal browser's scrollbar chrome. */
#define RENDERER_VIEW_INSET 12.0f

static float renderer_content_origin_y(const VitaWebRenderer *r)
{
    (void)r;
    return 0.0f;
}

static float renderer_content_origin_x(const VitaWebRenderer *r)
{
    (void)r;
    return 0.0f;
}

static float renderer_content_height(const VitaWebRenderer *r)
{
    if(!r)return 0.0f;
    const float inset=RENDERER_VIEW_INSET;
    float top=inset;
    float bottom=(float)r->height-inset;
    for(int i=0;i<r->node_count;i++){
        const VitaWebNode *n=&r->nodes[i];
        if(n->display_none)continue;
        float b=n->y+n->h+n->margin_bottom;
        if(b>bottom)bottom=b;
    }
    float extent=(bottom>inset?bottom:inset);
    float z=r->zoom>0.01f?r->zoom:1.0f;
    float scaled=(extent-top)*z;
    float viewport=(float)r->height-2.0f*inset;
    return scaled>viewport?scaled:viewport;
}

static float renderer_content_width(const VitaWebRenderer *r)
{
    if(!r)return 0.0f;
    const float inset=RENDERER_VIEW_INSET;
    float left=inset;
    float right=(float)r->width-inset;
    for(int i=0;i<r->node_count;i++){
        const VitaWebNode *n=&r->nodes[i];
        if(n->display_none)continue;
        float rr=n->x+n->w+n->margin_right;
        if(rr>right)right=rr;
    }
    float extent=(right>inset?right:inset);
    float z=r->zoom>0.01f?r->zoom:1.0f;
    float scaled=(extent-left)*z;
    float viewport=(float)r->width-2.0f*inset;
    return scaled>viewport?scaled:viewport;
}

static void renderer_scrollbar_state(const VitaWebRenderer *r, int *vertical, int *horizontal,
                                     float *viewport_w, float *viewport_h)
{
    const float sb=8.0f;
    float cw=renderer_content_width(r);
    float ch=renderer_content_height(r);
    int v=0,h=0;

    if(r->overflow_y!=1 && r->overflow_y!=2 && ch>(float)r->height-2.0f*RENDERER_VIEW_INSET) v=1;
    if(r->overflow_x!=1 && r->overflow_x!=2 && cw>(float)r->width-2.0f*RENDERER_VIEW_INSET) h=1;

    /* A horizontal scrollbar consumes vertical viewport space and can itself
       create vertical overflow; likewise for the vertical scrollbar.  A
       second pass resolves that interaction without introducing a layout
       dependency or moving the document origin. */
    if(h && r->overflow_y!=1 && r->overflow_y!=2 && ch>(float)r->height-2.0f*RENDERER_VIEW_INSET-sb) v=1;
    if(v && r->overflow_x!=1 && r->overflow_x!=2 && cw>(float)r->width-2.0f*RENDERER_VIEW_INSET-sb) h=1;

    if(vertical)*vertical=v;
    if(horizontal)*horizontal=h;
    if(viewport_w)*viewport_w=(float)r->width-2.0f*RENDERER_VIEW_INSET-(v?sb:0.0f);
    if(viewport_h)*viewport_h=(float)r->height-2.0f*RENDERER_VIEW_INSET-(h?sb:0.0f);
}

static float renderer_max_scroll_x(const VitaWebRenderer *r)
{
    if(r->overflow_x==2 || r->overflow_x==1) return 0.0f;
    float vw=0.0f; renderer_scrollbar_state(r,NULL,NULL,&vw,NULL);
    float max=renderer_content_width(r)-vw;
    return max>0.0f?max:0.0f;
}

static float renderer_max_scroll(const VitaWebRenderer *r)
{
    if(r->overflow_y==2 || r->overflow_y==1) return 0.0f;
    float vh=0.0f; renderer_scrollbar_state(r,NULL,NULL,NULL,&vh);
    float max=renderer_content_height(r)-vh;
    return max>0.0f?max:0.0f;
}

static int backend_scroll_x_by(VitaWebBackend *backend,float delta)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return -1;
    float target=r->scroll_x+delta;
    float max=renderer_max_scroll_x(r);
    if(target<0.0f)target=0.0f;
    if(target>max)target=max;
    r->scroll_x=target;
    return 0;
}

static float backend_scroll_x_offset(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return r?r->scroll_x:0.0f;
}

static float backend_content_width(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return r?renderer_content_width(r):0.0f;
}

static int backend_scroll_by(VitaWebBackend *backend,float delta)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return -1;
    float target=r->scroll_y+delta;
    float max=renderer_max_scroll(r);
    if(target<0.0f)target=0.0f;
    if(target>max)target=max;
    r->scroll_y=target;
    return 0;
}

static int backend_scroll_to(VitaWebBackend *backend,float offset)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return -1;
    float max=renderer_max_scroll(r);
    if(offset<0.0f)offset=0.0f;
    if(offset>max)offset=max;
    r->scroll_y=offset;
    return 0;
}

static float backend_scroll_offset(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return r?r->scroll_y:0.0f;
}

static float backend_content_height(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return r?renderer_content_height(r):0.0f;
}

static int renderer_focus_cycle(VitaWebRenderer *r,int direction);
static int select_option_count(const VitaWebRenderer *r,const VitaWebNode *select);
static VitaWebNode *select_option_at(VitaWebRenderer *r,VitaWebNode *select,int wanted);
static void select_set_index(VitaWebRenderer *r,VitaWebNode *select,int index);
static int renderer_focus_activate(VitaWebRenderer *r);
static int renderer_textfield_open(VitaWebRenderer *r,int node_index);

static int backend_draw(VitaWebBackend *backend){
    VitaWebRenderer *r=backend->userdata;
    if(!r)return -1;
    vita2d_clear_screen();
    vita2d_draw_rectangle(0,0,r->width,r->height,r->body_background);
    if(!r->loaded)return 0;
    for(int i=0;i<r->node_count;i++) draw_node(r,&r->nodes[i]);

    /* Lightweight select picker. It is rendered as native chrome rather than
       changing the page layout, like a browser's opened <select>. */
    for(int i=0;i<r->node_count;i++){
        VitaWebNode *sel=&r->nodes[i];
        if(!sel->is_select||!sel->select_open)continue;
        int count=select_option_count(r,sel); if(count<=0)continue;
        float z=r->zoom>0.01f?r->zoom:1.0f;
        float control_x=(sel->x-renderer_content_origin_x(r))*z-r->scroll_x;
        float control_y=(sel->y-renderer_content_origin_y(r))*z-r->scroll_y;
        float sx=control_x;
        float sw=sel->w*z; if(sw<140.0f)sw=140.0f;
        float row=28.0f;
        float sh=row*(count>8?8:count);
        /* Anchor the popup to the actual rendered select box.  Some HTML
           selects have no explicit CSS height, so use the renderer's
           measured line/padding height instead of a zero n->h.  A zero
           height used to make below_y equal control_y and caused the popup
           to paint directly over the select label. */
        float control_h=sel->h;
        if(control_h<4.0f){
            float lh=line_height(sel);
            control_h=lh+sel->padding_top+sel->padding_bottom+2.0f*sel->border_width;
        }
        if(control_h<28.0f)control_h=28.0f;
        float below_y=control_y+control_h*z;
        float above_y=control_y-sh;
        float sy;
        if(below_y+sh<=r->height-4.0f) sy=below_y;
        else if(above_y>=4.0f) sy=above_y;
        else {
            /* If neither side has enough room, keep the popup attached to
               the control without covering its center; clamp the popup to
               the viewport and let the visible option count remain capped. */
            sy=(float)r->height-4.0f-sh;
            if(sy<4.0f)sy=4.0f;
            if(sy+sh>control_y) sy=control_y-sh;
            if(sy<4.0f)sy=4.0f;
        }
        if(sx<4.0f)sx=4.0f;
        if(sx+sw>r->width-4.0f)sx=(float)r->width-sw-4.0f;
        if(sy<4.0f)sy=4.0f;
        unsigned popup_bg=r->theme_dark?rgba(0,0,0,255):rgba(250,250,250,255);
        unsigned popup_text=r->theme_dark?rgba(245,245,245,255):rgba(20,20,20,255);
        vita2d_draw_rectangle(sx,sy,sw,sh,popup_bg);
        for(int j=0;j<count;j++){
            VitaWebNode *opt=select_option_at(r,sel,j); if(!opt)continue;
            int selected=(j==sel->select_index);
            int hovered=(j==sel->select_hover_index);
            if(selected)
                vita2d_draw_rectangle(sx,sy+j*row,sw,row,rgba(70,105,150,255));
            if(hovered){
                /* Touch hover and DPAD focus both use the same subtle
                   darkening treatment.  When the hovered row is already the
                   selected row, darken the selected blue instead of replacing
                   it with a neutral gray. */
                unsigned hc=selected
                    ? (r->theme_dark?rgba(52,78,108,255):rgba(58,88,126,255))
                    : (r->theme_dark?rgba(42,42,42,255):rgba(226,226,226,255));
                vita2d_draw_rectangle(sx,sy+j*row,sw,row,hc);
            }
            if(opt->text[0]){
                unsigned tc=(selected)?rgba(255,255,255,255):popup_text;
                float tw=measure_text_width(r,opt,opt->text);
                float tx=sx+(sw-tw)*0.5f; if(tx<sx+8)tx=sx+8;
                float baseline=sy+j*row+(row+line_height(opt)*0.78f)*0.5f;
                vita2d_pgf_draw_text(r->system_pgf,tx,baseline,tc,0.75f,opt->text);
            }
        }
    }

    /* Minimal WebKit-like scrollbars.  They occupy the right/bottom edge of
       the viewport, and when both axes overflow they meet cleanly at the
       corner instead of drawing over one another. */
    int sb_vertical=0,sb_horizontal=0;
    float viewport_w=(float)r->width,viewport_h=(float)r->height;
    renderer_scrollbar_state(r,&sb_vertical,&sb_horizontal,&viewport_w,&viewport_h);
    float max_y=renderer_max_scroll(r);
    float max_x=renderer_max_scroll_x(r);
    const float sb=8.0f;
    const unsigned track=rgba(238,238,238,220);
    const unsigned thumb=rgba(128,128,128,230);
    if(r->scrollbars_visible && sb_vertical && max_y>0.0f){
        float track_h=(float)r->height-(sb_horizontal?sb:0.0f);
        float content_h=renderer_content_height(r);
        float thumb_h=track_h*(viewport_h/content_h);
        if(thumb_h<24.0f)thumb_h=24.0f;
        if(thumb_h>track_h)thumb_h=track_h;
        float ty=(track_h-thumb_h)*(r->scroll_y/max_y);
        float track_x=(float)r->width-sb;
        vita2d_draw_rectangle(track_x,0,sb,track_h,track);
        vita2d_draw_rectangle(track_x,ty,sb,thumb_h,thumb);
    }
    if(r->scrollbars_visible && sb_horizontal && max_x>0.0f){
        float track_w=(float)r->width-(sb_vertical?sb:0.0f);
        float content_w=renderer_content_width(r);
        float thumb_w=track_w*(viewport_w/content_w);
        if(thumb_w<24.0f)thumb_w=24.0f;
        if(thumb_w>track_w)thumb_w=track_w;
        float tx=(track_w-thumb_w)*(r->scroll_x/max_x);
        float track_y=(float)r->height-sb;
        vita2d_draw_rectangle(0,track_y,track_w,sb,track);
        vita2d_draw_rectangle(tx,track_y,thumb_w,sb,thumb);
    }

    /*
     * Optional VWW control modal.  This is deliberately native chrome: the
     * page only opts in with vita-control-modal="Title" on a VCM element.
     * The modal follows the same persistent VCM active state as :active CSS.
     */
    for(int i=0;i<r->node_count;i++){
        VitaWebNode *modal=&r->nodes[i];
        if(!modal->vcm_active || !modal->control_modal || !modal->vcm[0])continue;
        float mw=330.0f, mh=68.0f;
        float mx=((float)r->width-mw)*0.5f;
        float my=(float)r->height-mh-18.0f;
        if(my<8.0f)my=8.0f;
        unsigned mbg=r->theme_dark?rgba(24,27,31,245):rgba(250,250,250,245);
        unsigned mfg=r->theme_dark?rgba(245,247,250,255):rgba(25,28,32,255);
        unsigned maccent=rgba(70,105,150,255);
        vita2d_draw_rectangle(mx,my,mw,mh,mbg);
        vita2d_draw_rectangle(mx,my,mw,3.0f,maccent);
        vita2d_pgf_draw_text(r->system_pgf,mx+14.0f,my+25.0f,mfg,0.72f,
                             modal->control_modal_title[0]?modal->control_modal_title:"Vita Control");
        char hint[96];
        snprintf(hint,sizeof(hint),"VCM: %s",modal->vcm);
        vita2d_pgf_draw_text(r->system_pgf,mx+14.0f,my+49.0f,
                             r->theme_dark?rgba(190,198,208,255):rgba(80,86,94,255),0.58f,hint);
        break;
    }
    return 0;
}

static int node_hit(const VitaWebRenderer *r,const VitaWebNode *n,int x,int y){
    if(!r||!n||n->display_none)return 0;
    float z=r->zoom>0.01f?r->zoom:1.0f;
    float origin_x=renderer_content_origin_x(r), origin_y=renderer_content_origin_y(r);
    float ux=origin_x+(r->scroll_x+(float)x)/z;
    float uy=origin_y+(r->scroll_y+(float)y)/z;
    return ux>=n->x && ux<=n->x+n->w && uy>=n->y && uy<=n->y+n->h;
}

static int backend_pointer_event(VitaWebBackend *backend,int x,int y,int down){
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return 0;

    if(down){
        /* An opened select owns pointer input until an option is chosen. */
        for(int si=0;si<r->node_count;si++){
            VitaWebNode *sel=&r->nodes[si];
            if(!sel->is_select||!sel->select_open)continue;
            int count=select_option_count(r,sel); if(count<=0){sel->select_open=0;break;}
            float z=r->zoom>0.01f?r->zoom:1.0f;
            float control_x=(sel->x-renderer_content_origin_x(r))*z-r->scroll_x;
            float control_y=(sel->y-renderer_content_origin_y(r))*z-r->scroll_y;
            float sx=control_x;
            float sw=sel->w*z; if(sw<140.0f)sw=140.0f;
            float row=28.0f; float sh=row*(count>8?8:count);
            float below_y=control_y+sel->h*z;
            float above_y=control_y-sh;
            float sy=(below_y+sh<=r->height-4.0f)?below_y:above_y;
            if(sx<4.0f)sx=4.0f;
            if(sx+sw>r->width-4.0f)sx=(float)r->width-sw-4.0f;
            if(sy<4.0f)sy=4.0f;
            if(x>=sx&&x<=sx+sw&&y>=sy&&y<=sy+sh){
                int idx=(int)((y-sy)/row);
                if(idx<0)idx=0;
                if(idx>=count)idx=count-1;
                /* Pointer samples are also used for touch-move updates.  Keep
                   the picker open while the finger is down and only commit on
                   release once the finger has actually entered an option. */
                sel->select_hover_index=idx;
                sel->select_touch_moved_to_option=1;
                sel->select_touch_started_on_control=0;
                r->focus_node=si;
                return 1;
            }
            /* If the finger started on the closed select, the first release is
               the equivalent of a normal tap-to-open.  Do not immediately
               select the current row and close the popup.  Once the popup is
               already open, tapping outside it dismisses it. */
            if(sel->select_touch_started_on_control){
                return 1;
            }
            return 1;
        }
        for(int i=r->node_count-1;i>=0;--i){
            VitaWebNode *n=&r->nodes[i];
            if(n->is_textfield && node_hit(r,n,x,y)){
                r->focus_node=i;
                renderer_textfield_open(r,i);
                return 1;
            }
        }
        for(int i=r->node_count-1;i>=0;--i){
            VitaWebNode *n=&r->nodes[i];
            if(n->is_select && node_hit(r,n,x,y)){
                r->focus_node=i;
                n->select_open=1;
                n->select_hover_index=n->select_index;
                n->select_touch_started_on_control=1;
                n->select_touch_moved_to_option=0;
                return 1;
            }
        }
        int group=0;
        for(int i=r->node_count-1;i>=0;--i){
            VitaWebNode *n=&r->nodes[i];
            if(n->is_button && n->has_background && node_hit(r,n,x,y)){
                group=n->button_group;
                break;
            }
        }
        if(!group){
            for(int i=r->node_count-1;i>=0;--i){
                VitaWebNode *n=&r->nodes[i];
                if(n->is_button && node_hit(r,n,x,y)){group=n->button_group;break;}
            }
        }
        if(group){
            for(int i=0;i<r->node_count;i++)
                if(r->nodes[i].is_button&&r->nodes[i].button_group==group)
                    r->nodes[i].button_pressed=1;
            return 1;
        }
        return 0;
    }

    /* Commit an opened select on touch release.  The hover row is deliberately
       separate from select_index so the option can show its darkened hover
       state while the finger moves over it. */
    for(int si=0;si<r->node_count;si++){
        VitaWebNode *sel=&r->nodes[si];
        if(!sel->is_select||!sel->select_open)continue;
        if(!down){
            int count=select_option_count(r,sel);
            if(sel->select_touch_started_on_control){
                /* First tap opens the picker and intentionally leaves it open. */
                sel->select_touch_started_on_control=0;
                sel->select_touch_moved_to_option=0;
                r->focus_node=si;
                return 1;
            }
            if(sel->select_touch_moved_to_option && count>0 &&
               sel->select_hover_index>=0 && sel->select_hover_index<count)
                select_set_index(r,sel,sel->select_hover_index);
            sel->select_open=0;
            sel->select_touch_moved_to_option=0;
            r->focus_node=si;
            return 1;
        }
        return 1;
    }

    for(int i=r->node_count-1;i>=0;--i){
        VitaWebNode *n=&r->nodes[i];
        if(!n->is_button||!n->button_pressed)continue;
        int group=n->button_group;
        int inside=0;
        for(int j=0;j<r->node_count;j++){
            VitaWebNode *b=&r->nodes[j];
            if(b->is_button&&b->button_group==group&&b->has_background&&node_hit(r,b,x,y)){
                inside=1;break;
            }
        }
        for(int j=0;j<r->node_count;j++)
            if(r->nodes[j].is_button&&r->nodes[j].button_group==group)
                r->nodes[j].button_pressed=0;
        if(inside && n->is_link && n->href[0]){
            snprintf(r->pending_navigation,sizeof(r->pending_navigation),"%s",n->href);
            snprintf(r->pending_target,sizeof(r->pending_target),"%s",n->link_target[0]?n->link_target:"app");
            return 1;
        }
        return 1;
    }

    for(int i=r->node_count-1;i>=0;--i){
        VitaWebNode *n=&r->nodes[i];
        if(!n->is_link||n->is_button||!n->href[0])continue;
        if(node_hit(r,n,x,y)){
            snprintf(r->pending_navigation,sizeof(r->pending_navigation),"%s",n->href);
            snprintf(r->pending_target,sizeof(r->pending_target),"%s",n->link_target[0]?n->link_target:"app");
            return 1;
        }
    }
    return 0;
}

static void backend_pointer_cancel(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return;
    for(int i=0;i<r->node_count;i++){
        r->nodes[i].button_pressed=0;
        r->nodes[i].select_touch_started_on_control=0;
        r->nodes[i].select_touch_moved_to_option=0;
    }
    r->pending_navigation[0]=0;
    r->pending_target[0]=0;
}

static int renderer_is_focusable(const VitaWebNode *n){
    if(!n||n->display_none||n->disabled||!n->focusable_self||n->tabindex<0)return 0;
    /* tabindex=-1 is the default for elements that were not explicitly
       assigned a tab order in this lightweight renderer.  Native controls
       and links use their implicit browser order in that case. */
    return n->is_button || n->is_link || n->is_select || n->is_textfield ||
           (n->tag[0] && (!strcmp(n->tag,"input") || !strcmp(n->tag,"textarea")));
}

static int focus_tab_index(const VitaWebNode *n){
    if(!n)return -1;
    return n->tabindex;
}

static int renderer_focus_cycle(VitaWebRenderer *r,int direction){
    if(!r||direction==0)return -1;

    /* Browser-like keyboard/focus order:
       1. positive tabindex controls, ascending;
       2. implicit tabindex=0 controls, DOM order.
       Descendants which merely inherit link/button hit-testing are not
       focusable because focusable_self is only set on the actual interactive
       element.  This prevents a <p> inside a <div>/<button>/<a> from becoming
       a second DPAD stop. */
    int count=0;
    int current_tab=0;
    int current_node=r->focus_node;
    for(int i=0;i<r->node_count;i++){
        if(!renderer_is_focusable(&r->nodes[i]))continue;
        if(i==current_node){ current_tab=focus_tab_index(&r->nodes[i]); }
        count++;
    }
    if(count<=0)return 0;

    int found=-1;
    if(direction>0){
        /* Find the next candidate after the current one according to
           tabindex/DOM order.  Positive tabindex values are ordered first. */
        int best_tab=1000000,best_dom=1000000;
        if(current_node<0){
            /* Positive tabindex values form the first focus ring.  If none
               exist, start with the first implicit tabindex=0 control. */
            int have_positive=0;
            for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i]) && focus_tab_index(&r->nodes[i])>0){ have_positive=1; break; }
            best_tab=have_positive?1000000:0;
            best_dom=1000000;
            for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                int t=focus_tab_index(&r->nodes[i]);
                if((have_positive ? t>0 : t==0) && (t<best_tab || (t==best_tab&&i<best_dom))){
                    best_tab=t;best_dom=i;found=i;
                }
            }
        }else{
            int cur_tab=current_tab;
            for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                int t=focus_tab_index(&r->nodes[i]);
                if((t>cur_tab || (t==cur_tab&&i>current_node)) &&
                   (t<best_tab || (t==best_tab&&i<best_dom))){
                    best_tab=t;best_dom=i;found=i;
                }
            }
            if(found<0){
                /* Wrap to the first focusable candidate. */
                best_tab=1000000;best_dom=1000000;
                for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                    int t=focus_tab_index(&r->nodes[i]);
                    if(t<best_tab || (t==best_tab&&i<best_dom)){best_tab=t;best_dom=i;found=i;}
                }
            }
        }
    }else{
        int best_tab=-1,best_dom=-1;
        if(current_node<0){
            for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                int t=focus_tab_index(&r->nodes[i]);
                if(t>best_tab || (t==best_tab&&i>best_dom)){best_tab=t;best_dom=i;found=i;}
            }
        }else{
            int cur_tab=current_tab;
            for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                int t=focus_tab_index(&r->nodes[i]);
                if((t<cur_tab || (t==cur_tab&&i<current_node)) &&
                   (t>best_tab || (t==best_tab&&i>best_dom))){
                    best_tab=t;best_dom=i;found=i;
                }
            }
            if(found<0){
                best_tab=-1;best_dom=-1;
                for(int i=0;i<r->node_count;i++) if(renderer_is_focusable(&r->nodes[i])){
                    int t=focus_tab_index(&r->nodes[i]);
                    if(t>best_tab || (t==best_tab&&i>best_dom)){best_tab=t;best_dom=i;found=i;}
                }
            }
        }
    }
    if(found<0)return 0;
    r->focus_node=found;
    return 1;
}

static int select_option_count(const VitaWebRenderer *r,const VitaWebNode *select){
    if(!r||!select)return 0;
    int count=0, idx=(int)(select-r->nodes);
    for(int i=idx+1;i<r->node_count;i++){
        if(r->nodes[i].is_select && i!=idx)break;
        if(r->nodes[i].is_option)count++;
    }
    return count;
}

static VitaWebNode *select_option_at(VitaWebRenderer *r,VitaWebNode *select,int wanted){
    int idx=(int)(select-r->nodes), n=0;
    for(int i=idx+1;i<r->node_count;i++){
        if(r->nodes[i].is_select && i!=idx)break;
        if(r->nodes[i].is_option){if(n++==wanted)return &r->nodes[i];}
    }
    return NULL;
}

static void select_set_index(VitaWebRenderer *r,VitaWebNode *select,int index){
    int count=select_option_count(r,select); if(count<=0)return;
    if(index<0)index=count-1;
    if(index>=count)index=0;
    VitaWebNode *opt=select_option_at(r,select,index); if(!opt)return;
    select->select_index=index;
    select->select_hover_index=index;
    /* Keep the selected label on the actual <select> node.  The option
       subtree is hidden from the normal paint pass, so this is the only label
       used while the picker is closed. */
    snprintf(select->text,sizeof(select->text),"%s",opt->text);
    for(int i=(int)(select-r->nodes)+1;i<r->node_count;i++){
        if(r->nodes[i].is_select && &r->nodes[i]!=select)break;
        if(r->nodes[i].is_option)r->nodes[i].option_selected=(&r->nodes[i]==opt);
    }
}

static void utf8_to_utf16_simple(const char *src,SceWChar16 *dst,size_t cap){
    size_t di=0; const unsigned char *p=(const unsigned char *)(src?src:"");
    if(!dst||cap==0)return;
    while(*p&&di+1<cap){unsigned cp=0;int n=1;
        if(*p<0x80)cp=*p;
        else if((*p&0xE0)==0xC0&&p[1]){cp=((unsigned)(p[0]&0x1F)<<6)|(p[1]&0x3F);n=2;}
        else if((*p&0xF0)==0xE0&&p[1]&&p[2]){cp=((unsigned)(p[0]&0x0F)<<12)|((unsigned)(p[1]&0x3F)<<6)|(p[2]&0x3F);n=3;}
        else if((*p&0xF8)==0xF0&&p[1]&&p[2]&&p[3]){cp=((unsigned)(p[0]&7)<<18)|((unsigned)(p[1]&0x3F)<<12)|((unsigned)(p[2]&0x3F)<<6)|(p[3]&0x3F);n=4;}
        else{cp=0xFFFD;}
        p+=n;if(cp<=0xFFFF)dst[di++]=(SceWChar16)cp;else if(cp<=0x10FFFF&&di+2<cap){cp-=0x10000;dst[di++]=(SceWChar16)(0xD800+(cp>>10));dst[di++]=(SceWChar16)(0xDC00+(cp&0x3FF));}
    } dst[di]=0;
}
static void utf16_to_utf8_simple(const SceWChar16 *src,char *dst,size_t cap){
    size_t oi=0;if(!dst||cap==0)return;dst[0]=0;
    for(size_t i=0;src&&src[i]&&oi+1<cap;i++){unsigned cp=src[i];if(cp>=0xD800&&cp<=0xDBFF&&src[i+1]>=0xDC00&&src[i+1]<=0xDFFF){cp=0x10000+((cp-0xD800)<<10)+(src[++i]-0xDC00);}
        if(cp<0x80){if(oi+1>=cap)break;dst[oi++]=(char)cp;}else if(cp<0x800){if(oi+2>=cap)break;dst[oi++]=(char)(0xC0|(cp>>6));dst[oi++]=(char)(0x80|(cp&63));}else if(cp<0x10000){if(oi+3>=cap)break;dst[oi++]=(char)(0xE0|(cp>>12));dst[oi++]=(char)(0x80|((cp>>6)&63));dst[oi++]=(char)(0x80|(cp&63));}else{if(oi+4>=cap)break;dst[oi++]=(char)(0xF0|(cp>>18));dst[oi++]=(char)(0x80|((cp>>12)&63));dst[oi++]=(char)(0x80|((cp>>6)&63));dst[oi++]=(char)(0x80|(cp&63));}}
    dst[oi]=0;
}
static int renderer_textfield_open(VitaWebRenderer *r,int node_index){
    if(!r||node_index<0||node_index>=r->node_count)
        return -1;
    VitaWebNode *n=&r->nodes[node_index];
    if(!n->is_textfield||n->disabled)
        return 0;
    if(r->ime_active)
        return 1;
    utf8_to_utf16_simple(n->text,r->ime_initial,SCE_IME_DIALOG_MAX_TEXT_LENGTH);memcpy(r->ime_buffer,r->ime_initial,sizeof(r->ime_buffer));
    SceImeDialogParam param;sceImeDialogParamInit(&param);param.type=n->textfield_password?SCE_IME_TYPE_DEFAULT:SCE_IME_TYPE_DEFAULT;param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;param.textBoxMode=n->textfield_password?SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD:SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;param.title=(const SceWChar16 *)u"VitaWebview";param.maxTextLength=SCE_IME_DIALOG_MAX_TEXT_LENGTH-1;param.initialText=r->ime_initial;param.inputTextBuffer=r->ime_buffer;
    if(n->textfield_multiline) param.option|=SCE_IME_OPTION_MULTILINE;
    if(sceImeDialogInit(&param)<0)
        return -1;
    r->ime_node=node_index;
    r->ime_active=1;
    r->focus_node=node_index;
    return 1;
}
static void renderer_textfield_update(VitaWebRenderer *r){
    if(!r||!r->ime_active)
        return;
    if(sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)
        return;
    SceImeDialogResult result;memset(&result,0,sizeof(result));sceImeDialogGetResult(&result);
    if(r->ime_node>=0&&r->ime_node<r->node_count&&result.button==SCE_IME_DIALOG_BUTTON_ENTER)utf16_to_utf8_simple(r->ime_buffer,r->nodes[r->ime_node].text,sizeof(r->nodes[r->ime_node].text));
    sceImeDialogTerm();r->ime_active=0;r->ime_node=-1;
}

static int renderer_focus_activate(VitaWebRenderer *r){
    if(!r||r->focus_node<0||r->focus_node>=r->node_count)return 0;
    VitaWebNode *n=&r->nodes[r->focus_node];
    if(n->is_select){n->select_open=!n->select_open;n->select_hover_index=n->select_index;return 1;}
    if(n->is_textfield)return renderer_textfield_open(r,r->focus_node);
    if(n->is_button){n->button_pressed=1;return 1;}
    if(n->is_link&&n->href[0]){snprintf(r->pending_navigation,sizeof(r->pending_navigation),"%s",n->href);snprintf(r->pending_target,sizeof(r->pending_target),"%s",n->link_target[0]?n->link_target:"app");return 1;}
    return 0;
}

static int backend_focus_cycle(VitaWebBackend *backend,int direction){
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return -1;
    if(r->focus_node>=0&&r->focus_node<r->node_count){
        VitaWebNode *n=&r->nodes[r->focus_node];
        if(n->is_select&&n->select_open){select_set_index(r,n,n->select_index+(direction>0?1:-1));return 1;}
    }
    return renderer_focus_cycle(r,direction);
}
static int backend_focus_clear(VitaWebBackend *backend){
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r)return -1;
    r->focus_node=-1;
    r->select_focus_node=-1;
    return 0;
}

static int backend_focus_activate(VitaWebBackend *backend){
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return renderer_focus_activate(r);
}

static int backend_control_mode(VitaWebBackend *backend)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    return r ? r->control_mode : 0;
}

static int backend_vcm_activate(VitaWebBackend *backend,const char *control)
{
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r||!control||!*control)return 0;
    int matched=-1;
    for(int i=0;i<r->node_count;i++){
        VitaWebNode *n=&r->nodes[i];
        if(n->vcm[0] && ci_eq(n->vcm,control)){ matched=i; break; }
    }
    if(matched<0)return 0;

    /* A VCM is a single active page control.  Pressing another VCM removes
       :active from the previous one before activating the new one. */
    for(int i=0;i<r->node_count;i++) r->nodes[i].vcm_active=0;
    VitaWebNode *n=&r->nodes[matched];
    n->vcm_active=1;
    r->focus_node=matched;

    if(n->is_select){ n->select_open=!n->select_open; return 1; }
    if(n->is_textfield){ return renderer_textfield_open(r,matched); }
    if(n->is_button){ n->button_pressed=1; return 1; }
    if(n->is_link && n->href[0]){
        snprintf(r->pending_navigation,sizeof(r->pending_navigation),"%s",n->href);
        snprintf(r->pending_target,sizeof(r->pending_target),"%s",n->link_target[0]?n->link_target:"app");
        return 1;
    }
    return 1;
}

static const char *backend_navigation_url(VitaWebBackend *backend)
{
    VitaWebRenderer *r = (VitaWebRenderer *)backend->userdata;
    return r ? r->pending_navigation : "";
}

static const char *backend_navigation_target(VitaWebBackend *backend)
{
    VitaWebRenderer *r = (VitaWebRenderer *)backend->userdata;
    return r ? r->pending_target : "";
}

int vita_web_backend_clear_focus(VitaWebBackend *backend){
    return backend_focus_clear(backend);
}

int vita_web_backend_set_theme(VitaWebBackend *backend,int dark){
    VitaWebRenderer *r=backend?(VitaWebRenderer *)backend->userdata:NULL;if(!r)return -1;
    r->theme_dark=dark?1:0;
    if(!r->body_background_explicit) r->body_background=r->theme_dark?rgba(0,0,0,255):rgba(245,245,245,255);
    if(!r->body_color_explicit) r->body_color=r->theme_dark?rgba(235,238,242,255):rgba(25,25,30,255);
    if(!r->body_color_explicit || r->node_count>0){
        unsigned text_color=r->theme_dark?rgba(235,238,242,255):rgba(25,25,30,255);
        for(int i=0;i<r->node_count;i++){
            VitaWebNode *n=&r->nodes[i];
            if(n->is_button && !n->color_explicit)
                n->color=rgba(0,0,0,255);
            else if(!n->color_explicit)
                n->color=text_color;
        }
    }
    return 0;
}
int vita_web_backend_set_scrollbars(VitaWebBackend *backend,int visible){
    VitaWebRenderer *r=backend?(VitaWebRenderer *)backend->userdata:NULL;if(!r)return -1;r->scrollbars_visible=visible?1:0;return 0;
}

static void backend_shutdown(VitaWebBackend *backend){
    VitaWebRenderer *r=backend?backend->userdata:NULL;if(!r)return;for(int i=0;i<r->node_count;i++)if(r->nodes[i].image)vita2d_free_texture(r->nodes[i].image);
    for(int i=0;i<r->font_count;i++){if(r->fonts[i].font)vita2d_free_font(r->fonts[i].font);}
    if(r->system_pgf) vita2d_free_pgf(r->system_pgf);
    free(r->html);
    curl_global_cleanup();
    free(r);
    backend->userdata=NULL;
}
int vita_web_backend_set_zoom(VitaWebBackend *backend,float zoom)
{
    VitaWebRenderer *r=backend?(VitaWebRenderer *)backend->userdata:NULL;
    if(!r) return -1;
    if(zoom<0.75f) zoom=0.75f;
    if(zoom>2.0f) zoom=2.0f;
    float old=r->zoom>0.01f?r->zoom:1.0f;
    if(fabsf(zoom-old)>0.0001f){
        /* Zoom around the viewport centre instead of the page's top-left.
           This keeps the area under the user's view stable and makes the
           already-visible left side reachable again after zooming. */
        float ox=renderer_content_origin_x(r);
        float oy=renderer_content_origin_y(r);
        float center_doc_x=ox+(r->scroll_x+(float)r->width*0.5f)/old;
        float center_doc_y=oy+(r->scroll_y+(float)r->height*0.5f)/old;
        r->zoom=zoom;
        r->scroll_x=(center_doc_x-ox)*zoom-(float)r->width*0.5f;
        r->scroll_y=(center_doc_y-oy)*zoom-(float)r->height*0.5f;
    } else {
        r->zoom=zoom;
    }
    float max=renderer_max_scroll(r);
    if(r->scroll_y<0.0f) r->scroll_y=0.0f;
    if(r->scroll_y>max) r->scroll_y=max;
    float max_x=renderer_max_scroll_x(r);
    if(r->scroll_x<0.0f) r->scroll_x=0.0f;
    if(r->scroll_x>max_x) r->scroll_x=max_x;
    return 0;
}

float vita_web_backend_get_zoom(VitaWebBackend *backend)
{
    VitaWebRenderer *r=backend?(VitaWebRenderer *)backend->userdata:NULL;
    return r?(r->zoom>0.01f?r->zoom:1.0f):1.0f;
}

static const char *backend_button_wait(VitaWebBackend *backend){
    VitaWebRenderer *r=(VitaWebRenderer *)backend->userdata;
    if(!r || !r->button_wait[0]) return "";
    return r->button_wait;
}

static const VitaWebBackendOps g_default_ops={backend_init,backend_load,backend_eval,backend_update,backend_draw,backend_pointer_event,backend_pointer_cancel,backend_scroll_by,backend_scroll_to,backend_scroll_offset,backend_scroll_x_by,backend_scroll_x_offset,backend_content_width,backend_content_height,backend_focus_cycle,backend_focus_activate,backend_vcm_activate,backend_control_mode,backend_button_wait,backend_navigation_url,backend_navigation_target,backend_shutdown};
const VitaWebBackendOps *vita_web_backend_default_ops(void){return &g_default_ops;}
