#include "core/cwv_host.h"
#include "web/typescript/cwv_ts_core.h"
#include "core/cwv_backend.h"
#include "core/cwv_renderer.h"
#include "web/js/modern/cwv_es2015_core.h"
#include "web/js/modern/cwv_es_libs.h"

#include "core/cwv_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* fetch() trampoline (defined below; the blocking GET lives in the console seam) */
static int host_fetch_trampoline(const char *url, char *body, size_t cap,
                                 void *userdata);


struct CwvHost {
    int width;
    int height;
    CwvHostStatus status;
    char last_error[320];   /* >= the 300-byte msg buffers callers pass
                                 (gcc-15 -Wformat-truncation, bug 36) */
    char current_url[1024];
    char page_title[256];
    char favicon_url[1024];
    char button_wait[32];
    char button_wait_override[32];
    int navigation_requested;
    int backend_initialized;
    CwvBackend backend;

};

static void set_error(CwvHost *h, const char *message)
{
    if (!h) return;
    snprintf(h->last_error, sizeof(h->last_error), "%s",
             message ? message : "unknown error");
    h->status = CWV_STATUS_ERROR;
}

/* file:// URL under the app root -> physical path under the path root.
 * The root pair is per-console (see cwv_platform.h): app0: on Vita, the
 * folder the package sits in on Wii U and Wii.  Defaults keep Vita. */
static int file_url_to_app_path(const char *url, char *out, size_t out_size)
{
    const char *url_root = cwv_platform_url_root();
    const char *path_root = cwv_platform_path_root();
    size_t rl = strlen(url_root), pl = strlen(path_root);
    const char *path;
    const char *end;
    size_t len;
    if (!url || !out || out_size == 0) return -1;
    if (strncmp(url, url_root, rl) != 0 || url[rl] != '/') return -1;
    path = url + rl + 1;   /* after the root's separator slash */

    /* Query strings and fragments belong to the URL, not the console
     * file path.  The renderer is intentionally allowed to receive URLs
     * such as file:///app0/web/index.html?vita=1. */
    end = strpbrk(path, "?#");
    len = end ? (size_t)(end - path) : strlen(path);
    if (len + pl + 1 >= out_size) return -1;
    memcpy(out, path_root, pl);
    out[pl] = '/';
    memcpy(out + pl + 1, path, len);
    out[len + pl + 1] = '\0';
    return 0;
}

static int resolve_local_href(const char *base_url, const char *href, char *out, size_t out_size)
{
    const char *url_root = cwv_platform_url_root();
    size_t rl = strlen(url_root);
    char base[1024];
    char prefix[300];
    char *dir;
    if (!href || !*href || !out || out_size == 0) return -1;
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        snprintf(out, out_size, "%s", href);
        return 1;
    }
    if (strncmp(href, url_root, rl) == 0 && href[rl] == '/') {
        snprintf(out, out_size, "%s", href);
        return 0;
    }
    if (base_url) snprintf(base, sizeof(base), "%s", base_url);
    else snprintf(base, sizeof(base), "%s/web/index.html", url_root);
    dir = strrchr(base, '/');
    if (!dir) return -1;
    dir[1] = '\0';
    if (href[0] == '/') {
        snprintf(out, out_size, "%s%s", url_root, href);
    } else {
        snprintf(out, out_size, "%s%s", base, href);
    }
    /* Normalize simple ./ and ../ components in the path. */
    char work[1024];
    snprintf(prefix, sizeof(prefix), "%s/", url_root);
    if (strncmp(out, prefix, strlen(prefix)) != 0) return 0;
    snprintf(work, sizeof(work), "%s", out + strlen(prefix));
    char *parts[64]; int count = 0;
    char *tok = strtok(work, "/");
    while (tok && count < 64) {
        if (strcmp(tok, ".") == 0) { }
        else if (strcmp(tok, "..") == 0) { if (count > 0) count--; }
        else parts[count++] = tok;
        tok = strtok(NULL, "/");
    }
    char rebuilt[1024] = {0};
    snprintf(rebuilt, sizeof(rebuilt), "%s", prefix);
    for (int i=0;i<count;i++) {
        if (strlen(rebuilt)+strlen(parts[i])+2 >= sizeof(rebuilt)) return -1;
        strcat(rebuilt, parts[i]);
        if (i+1<count) strcat(rebuilt, "/");
    }
    snprintf(out, out_size, "%s", rebuilt);
    return 0;
}


static int ci_char(int c){ return tolower((unsigned char)c); }
static const char *find_ci(const char *haystack, const char *needle){
    size_t n;
    if(!haystack||!needle||!*needle)return haystack;
    n=strlen(needle);
    for(const char *p=haystack;*p;p++){
        size_t i=0;
        while(i<n && p[i] && ci_char(p[i])==ci_char(needle[i]))i++;
        if(i==n)return p;
    }
    return NULL;
}
static void trim_copy(const char *start,size_t len,char *out,size_t cap){
    size_t a=0,b=len;
    if(!out||cap==0){return;}
    while(a<b && isspace((unsigned char)start[a]))a++;
    while(b>a && isspace((unsigned char)start[b-1]))b--;
    if(b-a>=cap)b=a+cap-1;
    memcpy(out,start+a,b-a); out[b-a]='\0';
}
static void html_metadata_reset(CwvHost *host){
    if(!host)return;
    snprintf(host->page_title,sizeof(host->page_title),"CWebView");
    host->favicon_url[0]='\0';
    host->button_wait[0]='\0';
    host->button_wait_override[0]='\0';
}
static void html_metadata_parse(CwvHost *host,const char *url){
    char path[1024], *buf=NULL, title[256]={0}, icon_href[1024]={0};
    long fsize; int fd=-1; int n; size_t size;
    if(!host||!url)return;
    html_metadata_reset(host);
    if(file_url_to_app_path(url,path,sizeof(path))<0)return;
    fsize=cwv_file_size(path);
    if(fsize<=0 || fsize>1024*1024)return;
    size=(size_t)fsize;
    buf=malloc(size+1); if(!buf)return;
    fd=cwv_file_open_read(path); if(fd<0){free(buf);return;}
    n=cwv_file_read(fd,buf,(int)size); cwv_file_close(fd);
    if(n<=0){free(buf);return;} buf[n]='\0';
    {
        const char *a=find_ci(buf,"<title");
        if(a){ const char *gt=strchr(a,'>'); if(gt){ const char *e=find_ci(gt+1,"</title>"); if(e) trim_copy(gt+1,(size_t)(e-(gt+1)),title,sizeof(title)); } }
    }
    if(title[0])snprintf(host->page_title,sizeof(host->page_title),"%s",title);
    {
        const char *ha=find_ci(buf,"<html");
        if(ha){
            const char *he=strchr(ha,'>');
            const char *wa=find_ci(ha,"cwv-button-wait");
            if(he && wa && wa<he){
                const char *eq=strchr(wa,'=');
                if(eq && eq<he){
                    const char *vs=eq+1; while(vs<he&&isspace((unsigned char)*vs))vs++;
                    if(*vs=='\"'||*vs=='\''){
                        char quote=*vs++; const char *ve=vs; while(ve<he&&*ve!=quote)ve++;
                        trim_copy(vs,(size_t)(ve-vs),host->button_wait,sizeof(host->button_wait));
                    } else {
                        const char *ve=vs; while(ve<he&&!isspace((unsigned char)*ve))ve++;
                        trim_copy(vs,(size_t)(ve-vs),host->button_wait,sizeof(host->button_wait));
                    }
                }
            }
        }
    }
    {
        const char *scan=buf;
        while((scan=find_ci(scan,"<link"))!=NULL){
            const char *end=strchr(scan,'>');
            if(!end)break;
            const char *rel=find_ci(scan,"rel");
            const char *href=find_ci(scan,"href");
            if(rel && rel<end && href && href<end){
                const char *rv=strchr(rel,'='); const char *hv=strchr(href,'=');
                if(rv&&hv&&rv<end&&hv<end){
                    char relv[128]={0}; char hvv[1024]={0};
                    const char *rs=rv+1,*hs=hv+1; char q=0;
                    while(rs<end && isspace((unsigned char)*rs))rs++;
                    if(*rs=='"'||*rs=='\''){q=*rs++; const char *re=rs; while(re<end&&*re!=q)re++; trim_copy(rs,(size_t)(re-rs),relv,sizeof(relv));}
                    else {const char *re=rs; while(re<end&&!isspace((unsigned char)*re))re++; trim_copy(rs,(size_t)(re-rs),relv,sizeof(relv));}
                    while(hs<end && isspace((unsigned char)*hs))hs++;
                    if(*hs=='"'||*hs=='\''){q=*hs++; const char *he=hs; while(he<end&&*he!=q)he++; trim_copy(hs,(size_t)(he-hs),hvv,sizeof(hvv));}
                    else {const char *he=hs; while(he<end&&!isspace((unsigned char)*he))he++; trim_copy(hs,(size_t)(he-hs),hvv,sizeof(hvv));}
                    if(hvv[0] && (find_ci(relv,"icon")==relv || find_ci(relv,"shortcut")==relv)){
                        snprintf(icon_href,sizeof(icon_href),"%s",hvv); break;
                    }
                }
            }
            scan=end+1;
        }
    }
    if(icon_href[0]){
        if(resolve_local_href(url,icon_href,host->favicon_url,sizeof(host->favicon_url))<0)host->favicon_url[0]='\0';
    } else {
        char base[1024];
        snprintf(base,sizeof(base),"%s",url);
        char *q=strpbrk(base,"?#");
        if(q)*q='\0';
        char *slash=strrchr(base,'/');
        if(slash){
            slash[1]='\0';
            const size_t suffix_len=sizeof("favicon.png")-1;
            size_t base_len=strlen(base);
            if(base_len+suffix_len<sizeof(host->favicon_url))
                snprintf(host->favicon_url,sizeof(host->favicon_url),"%sfavicon.png",base);
        }
    }
    free(buf);
}
CwvHost *cwv_host_create(int width, int height)
{
    CwvHost *host;
    if (width <= 0 || height <= 0) return NULL;

    host = calloc(1, sizeof(*host));
    if (!host) return NULL;
    host->width = width;
    host->height = height;
    host->status = CWV_STATUS_INITIALIZED;
    host->backend.ops = cwv_backend_default_ops();

    if (host->backend.ops->initialize) {
        int rc = host->backend.ops->initialize(&host->backend, width, height);
        if (rc < 0) {
            set_error(host, "Web backend initialization failed.");
            return host;
        }
        host->backend_initialized = 1;
    }
    /* Console services (sysmodules, app-util, network) come in through
     * the platform seam; the page fetch() hook follows - if the console
     * has no network stack, fetch() simply fails gracefully (-1). */
    cwv_console_init();
    if (host->backend_initialized)
        cwv_backend_set_fetch_hook(&host->backend,
                                        host_fetch_trampoline, host);
    return host;
}

/* ---------------------------------------------------------------------------
 * fetch(url) backend for page scripts: forwarded to the console seam.
 * The blocking GET (timeouts, caps, cleanup) is per-console; see
 * cwv_platform.h.  A failed fetch NEVER crashes the app: the only
 * observable effect is the page callback getting (-1, "").
 * ------------------------------------------------------------------------ */
int cwv_host_fetch_url(CwvHost *host, const char *url,
                            char *body, size_t cap)
{
    if (!host || !url || !body || cap == 0) return -1;
    return cwv_console_fetch(url, body, cap);
}

int cwv_host_load(CwvHost *host, const char *url)
{
    char path[1024];
    int rc;

    if (!host || !url || !*url) return -1;
    if (host->status == CWV_STATUS_ERROR) return -2;

    snprintf(host->current_url, sizeof(host->current_url), "%s", url);
    html_metadata_parse(host, url);
    host->last_error[0] = '\0';
    host->navigation_requested = 0;
    host->status = CWV_STATUS_LOADING;

    if (file_url_to_app_path(url, path, sizeof(path)) < 0) {
        char msg[300];
        snprintf(msg, sizeof(msg),
                 "Only %s/... file URLs are supported by this backend.",
                 cwv_platform_url_root());
        set_error(host, msg);
        return -3;
    }

    if (cwv_file_size(path) < 0) {
        snprintf(host->last_error, sizeof(host->last_error),
                 "Web resource does not exist: %.*s",
                 (int)(sizeof(host->last_error) - 30), path);
        host->status = CWV_STATUS_ERROR;
        return -4;
    }

    /* Dispatch through the backend interface: the native renderer owns
     * HTML/CSS/JS from here on. */
    if (host->backend.ops && host->backend.ops->load) {
        rc = host->backend.ops->load(&host->backend, url);
        if (rc == 0) {
            host->navigation_requested = 0;
            host->status = CWV_STATUS_READY;
            return 0;
        }
        if (rc != CWV_BACKEND_NOT_IMPLEMENTED) {
            set_error(host, "Web backend navigation failed.");
            return rc;
        }
    }

    host->navigation_requested = 1;
    /* Remain LOADING: the URL is valid but no backend accepted navigation. */
    return 0;
}

int cwv_host_eval(CwvHost *host, const char *javascript)
{
    if (!host || !javascript) return -1;
    if (host->status != CWV_STATUS_READY &&
        host->status != CWV_STATUS_LOADING) return -2;
    if (!host->backend.ops || !host->backend.ops->eval) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->eval(&host->backend, javascript);
}

int cwv_host_open_image_preview(CwvHost *host, const char *uri)
{
    (void)host; (void)uri;
    return CWV_BACKEND_NOT_IMPLEMENTED;
}

int cwv_host_pointer_event(CwvHost *host, int x, int y, int down)
{
    const char *href; const char *target; char resolved[1024]; int rc;
    if(!host) return 0;
    if(!host->backend.ops || !host->backend.ops->pointer_event) return 0;
    rc=host->backend.ops->pointer_event(&host->backend,x,y,down);
    if(rc<=0 || down) return rc;
    href=host->backend.ops->navigation_url ? host->backend.ops->navigation_url(&host->backend) : "";
    target=host->backend.ops->navigation_target ? host->backend.ops->navigation_target(&host->backend) : "app";
    if(!href || !*href) return 0;
    if(strcmp(target,"browser")==0 || strcmp(target,"external")==0){
        if(cwv_console_open_browser(href)<0){ set_error(host,"Native browser launch failed."); return -1; }
        return 2;
    }
    if(resolve_local_href(host->current_url,href,resolved,sizeof(resolved))<0){ set_error(host,"Unable to resolve local link."); return -1; }
    return cwv_host_load(host,resolved);
}

int cwv_host_cursor_at(CwvHost *host, int x, int y, CwvCursorInfo *out)
{
    CwvCursorInfo tmp;
    if(!out) out = &tmp;
    if(!host || !host->backend.ops || !host->backend.ops->cursor_at)
        { out->style = CWV_CSTYLE_AUTO; out->img = NULL;
          out->hot_x = 0; out->hot_y = 0; return 0; }
    return host->backend.ops->cursor_at(&host->backend, x, y, out);
}

void cwv_host_pointer_cancel(CwvHost *host)
{
    if (!host) return;
    if (!host->backend.ops || !host->backend.ops->pointer_cancel) return;
    host->backend.ops->pointer_cancel(&host->backend);
}

int cwv_host_scroll_by(CwvHost *host, float delta)
{
    if (!host || !host->backend.ops || !host->backend.ops->scroll_by) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->scroll_by(&host->backend, delta);
}

int cwv_host_scroll_to(CwvHost *host, float offset)
{
    if (!host || !host->backend.ops || !host->backend.ops->scroll_to) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->scroll_to(&host->backend, offset);
}

int cwv_host_set_zoom(CwvHost *host, float zoom)
{
    if(!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    if(zoom<0.75f) zoom=0.75f;
    if(zoom>2.0f) zoom=2.0f;
    /* The native renderer exposes zoom directly; this deliberately does not
       alter page CSS or cwv-control-mode. */
    CwvBackend *b=&host->backend;
    if(!b->userdata) return CWV_BACKEND_NOT_IMPLEMENTED;
    /* backend-specific setter lives in the native backend below. */
    return cwv_backend_set_zoom(b,zoom);
}

float cwv_host_zoom(const CwvHost *host)
{
    if(!host) return 1.0f;
    return cwv_backend_get_zoom((CwvBackend *)&host->backend);
}

float cwv_host_scroll_offset(const CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->scroll_offset) return 0.0f;
    return host->backend.ops->scroll_offset((CwvBackend *)&host->backend);
}

int cwv_host_scroll_x_by(CwvHost *host, float delta)
{
    if (!host || !host->backend.ops || !host->backend.ops->scroll_x_by) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->scroll_x_by(&host->backend, delta);
}

int cwv_host_scroll_at(CwvHost *host, float delta, int x, int y)
{
    if (!host || !host->backend.ops) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (host->backend.ops->scroll_at)
        return host->backend.ops->scroll_at(&host->backend, delta, x, y);
    return cwv_host_scroll_by(host, delta);
}

int cwv_host_scroll_x_at(CwvHost *host, float delta, int x, int y)
{
    if (!host || !host->backend.ops) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (host->backend.ops->scroll_x_at)
        return host->backend.ops->scroll_x_at(&host->backend, delta, x, y);
    return cwv_host_scroll_x_by(host, delta);
}

int cwv_host_scroll_page_x_by(CwvHost *host, float delta)
{
    if (!host || !host->backend.ops) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (host->backend.ops->scroll_page_x_by)
        return host->backend.ops->scroll_page_x_by(&host->backend, delta);
    return cwv_host_scroll_x_by(host, delta);
}

float cwv_host_scroll_x_offset(const CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->scroll_x_offset) return 0.0f;
    return host->backend.ops->scroll_x_offset((CwvBackend *)&host->backend);
}

float cwv_host_content_width(const CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->content_width) return 0.0f;
    return host->backend.ops->content_width((CwvBackend *)&host->backend);
}


float cwv_host_content_height(const CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->content_height) return 0.0f;
    return host->backend.ops->content_height((CwvBackend *)&host->backend);
}

int cwv_host_focus_cycle(CwvHost *host, int direction)
{
    if (!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (!host->backend.ops || !host->backend.ops->focus_cycle) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->focus_cycle(&host->backend, direction >= 0 ? 1 : -1);
}

int cwv_host_focus_clear(CwvHost *host)
{
    if (!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    return cwv_backend_clear_focus(&host->backend);
}

void cwv_host_focus_release(CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->focus_release) return;
    host->backend.ops->focus_release(&host->backend);
}

int cwv_host_vcm_activate(CwvHost *host, const char *control)
{
    if (!host || !control) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (!host->backend.ops || !host->backend.ops->vcm_activate) return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->vcm_activate(&host->backend, control);
}

const char *cwv_host_button_wait(const CwvHost *host)
{
    if (!host) return "";
    if (host->button_wait_override[0]) return host->button_wait_override;
    if (host->button_wait[0]) return host->button_wait;
    if (!host->backend.ops || !host->backend.ops->button_wait) return "";
    return host->backend.ops->button_wait((CwvBackend *)&host->backend);
}

int cwv_host_set_button_wait_override(CwvHost *host, const char *spec)
{
    if (!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    if (!spec) spec = "";
    snprintf(host->button_wait_override, sizeof(host->button_wait_override), "%s", spec);
    return 0;
}

int cwv_host_control_mode(const CwvHost *host)
{
    if (!host || !host->backend.ops || !host->backend.ops->control_mode) return 0;
    return host->backend.ops->control_mode((CwvBackend *)&host->backend);
}

int cwv_host_set_theme(CwvHost *host, int dark)
{
    if(!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    extern int cwv_backend_set_theme(CwvBackend *backend,int dark);
    return cwv_backend_set_theme(&host->backend,dark);
}

int cwv_host_set_scrollbars(CwvHost *host, int visible)
{
    if(!host) return CWV_BACKEND_NOT_IMPLEMENTED;
    return cwv_backend_set_scrollbars(&host->backend,visible);
}

int cwv_host_focus_activate(CwvHost *host)
{
    int rc;
    const char *href; const char *target; char resolved[1024];
    if (!host || !host->backend.ops || !host->backend.ops->focus_activate) return CWV_BACKEND_NOT_IMPLEMENTED;
    rc=host->backend.ops->focus_activate(&host->backend);
    if(rc<=0) return rc;
    href=host->backend.ops->navigation_url ? host->backend.ops->navigation_url(&host->backend) : "";
    target=host->backend.ops->navigation_target ? host->backend.ops->navigation_target(&host->backend) : "app";
    if(!href || !*href) return rc;
    if(strcmp(target,"browser")==0 || strcmp(target,"external")==0){
        if(cwv_console_open_browser(href)<0){ set_error(host,"Native browser launch failed."); return -1; }
        return rc;
    }
    if(resolve_local_href(host->current_url,href,resolved,sizeof(resolved))<0){ set_error(host,"Unable to resolve local link."); return -1; }
    return cwv_host_load(host,resolved);
}

int cwv_host_update(CwvHost *host)
{
    int rc;
    if (!host) return -1;
    if (!host->backend.ops || !host->backend.ops->update)
        return CWV_BACKEND_NOT_IMPLEMENTED;
    rc = host->backend.ops->update(&host->backend);
    if (rc == 0 && host->navigation_requested) {
        host->navigation_requested = 0;
        host->status = CWV_STATUS_READY;
    } else if (rc < 0 && rc != CWV_BACKEND_NOT_IMPLEMENTED) {
        set_error(host, "Web backend update failed.");
    }
    return rc;
}

int cwv_host_draw(CwvHost *host)
{
    if (!host) return -1;
    if (!host->backend.ops || !host->backend.ops->draw)
        return CWV_BACKEND_NOT_IMPLEMENTED;
    return host->backend.ops->draw(&host->backend);
}

CwvHostStatus cwv_host_status(const CwvHost *host)
{
    return host ? host->status : CWV_STATUS_UNINITIALIZED;
}

const char *cwv_host_last_error(const CwvHost *host)
{
    return host ? host->last_error : "invalid host";
}

const char *cwv_host_current_url(const CwvHost *host)
{
    return host ? host->current_url : "";
}

const char *cwv_host_page_title(const CwvHost *host)
{
    return host ? host->page_title : "";
}

const char *cwv_host_favicon_url(const CwvHost *host)
{
    return host ? host->favicon_url : "";
}

/* ==========================================================================
 * ES6 support: the built-in provider + the services every provider shares
 * ========================================================================== */

/* Host callbacks handed to any ES6 provider so it can run imports. */
static int es6_cb_evaluate(void *ud, const char *javascript)
{
    return cwv_host_eval((CwvHost *)ud, javascript);
}

static int es6_cb_resolve_url(void *ud, const char *base_url,
                              const char *specifier, char *out,
                              size_t out_size)
{
    CwvHost *host = (CwvHost *)ud;
    const char *base = (base_url && base_url[0]) ? base_url
                                                 : host->current_url;
    /* resolve_local_href: 0 = local ok, 1 = remote ok, -1 = failure */
    return resolve_local_href(base, specifier, out, out_size) < 0 ? -1 : 0;
}

static int es6_cb_load_file(void *ud, const char *url, char *out,
                            size_t out_size)
{
    CwvHost *host = (CwvHost *)ud;
    CwvRenderer *r = (CwvRenderer *)host->backend.userdata;
    unsigned char *data = NULL;
    size_t size = 0, n;
    if (!r || !out || out_size < 2) return -1;
    if (load_resource(r, url, &data, &size) < 0) return -1;
    n = size < out_size - 1 ? size : out_size - 1;
    memcpy(out, data, n);
    out[n] = '\0';
    free(data);
    return (int)n;
}

/* The three host services every ES6 provider needs (evaluate a script,
 * resolve a module specifier, read a module file), with the host already
 * bound into `userdata`.  The console-neutral part of ES6 wiring lives
 * HERE: an alternate provider would fill its
 * module API from this struct without reaching into host internals. */
int cwv_host_es6_services(CwvHost *host, CwvHostEs6Services *out)
{
    if (!out) return -1;
    if (!host || !host->backend_initialized) return -1;
    out->userdata = host;
    out->evaluate = es6_cb_evaluate;
    out->resolve_url = es6_cb_resolve_url;
    out->load_file = es6_cb_load_file;
    return 0;
}

/* Register whichever provider the port chose: the source transformer
 * (ES6 syntax -> the ES5 the engine runs) and the library prelude
 * (Map/Set, Array.find, ... installed into every fresh JS context).
 * Providers call this; apps normally call an enable_*() wrapper. */
int cwv_host_install_es6(CwvHost *host,
                              int (*transform)(const char *in, char *out,
                                               size_t cap, void *userdata),
                              void *transform_data,
                              const char *(*prelude)(void *userdata),
                              void *prelude_data)
{
    if (!host || !host->backend_initialized) return -1;
    cwv_backend_set_source_transform(&host->backend, transform, transform_data);
    cwv_backend_set_js_prelude(&host->backend, prelude, prelude_data);
    return 0;
}

int cwv_host_set_runtime_log(CwvHost *host, void (*fn)(const char *fmt, ...))
{
    if (!host || !host->backend_initialized) return -1;
    cwv_backend_set_runtime_log(&host->backend, fn);
    return 0;
}

/* Backend-typedef-shaped adapters over the core transformer/prelude. */
static int es6_builtin_transform(const char *in, char *out, size_t cap,
                                 void *userdata)
{
    return vw_es2015_transform(in, out, cap, (const VwEsHooks *)userdata);
}

static const char *es6_builtin_prelude(void *userdata)
{
    (void)userdata;
    return cwv_es_editions_prelude();
}

/* BUILT-IN PROVIDER (every console): registers the transformer the app
 * already carries (cwv_es2015_core.c) directly with the backend.  The
 * single provider on every port since 2026-10-03, when the Vita's
 * CwvESM.suprx runtime module was removed: it duplicated this same code
 * in RAM and its exported NIDs were silently stubbed on Vita3K. */
int cwv_host_enable_es_builtin(CwvHost *host)
{
    static VwEsHooks hooks;   /* static: the transform reads it per call */
    CwvHostEs6Services services;
    if (cwv_host_es6_services(host, &services) != 0) return -1;
    hooks.userdata = services.userdata;
    hooks.resolve_url = services.resolve_url;
    hooks.load_file = services.load_file;
    hooks.evaluate = services.evaluate;
    /* Thin adapters so the hook signatures match the backend typedefs
     * exactly - no function-pointer casts anywhere. */
    return cwv_host_install_es6(host,
                                    es6_builtin_transform, &hooks,
                                    es6_builtin_prelude, NULL);
}

/* Built-in TypeScript provider (2026-10-01): registers the shared
 * cwv_ts_core.c stripper. The backend applies it to scripts the page
 * marked as TypeScript, then runs the ES6 transform on the result. */
static int ts_builtin_transform(const char *in, char *out, size_t cap,
                                void *userdata)
{
    (void)userdata;
    return vw_ts_strip(in, out, cap);
}

int cwv_host_enable_ts_builtin(CwvHost *host)
{
    if (!host || !host->backend_initialized) return -1;
    cwv_backend_set_ts_transform(&host->backend, ts_builtin_transform, NULL);
    return 0;
}

/* Runs the backend ESM self-test (see cwv_backend.h).  Thin wrapper:
 * main.c talks to the host, not the backend, everywhere else. */
int cwv_host_esm_selftest(CwvHost *host, char *report, size_t cap)
{
    if (!host || !host->backend_initialized) {
        if (report && cap) snprintf(report, cap, "backend not initialized");
        return 0;
    }
    return cwv_backend_esm_selftest(&host->backend, report, cap);
}

/* fetch(url, callback) trampoline: the renderer calls the C hook, which
 * performs the blocking GET (cwv_host_fetch_url above, via the console seam). */
static int host_fetch_trampoline(const char *url, char *body, size_t cap,
                                 void *userdata)
{
    return cwv_host_fetch_url((CwvHost *)userdata, url, body, cap);
}

const char *cwv_host_js_last_error(CwvHost *host)
{
    if (!host || !host->backend_initialized) return "";
    return cwv_backend_js_last_error(&host->backend);
}

int cwv_host_js_active(CwvHost *host)
{
    if (!host || !host->backend_initialized) return 0;
    return cwv_backend_js_active(&host->backend);
}

void cwv_host_console_stats(CwvHost *host, int *errors,
                                 int *warns, int *lines)
{
    if (!host || !host->backend_initialized) {
        if (errors) *errors = 0;
        if (warns) *warns = 0;
        if (lines) *lines = 0;
        return;
    }
    cwv_backend_console_stats(&host->backend, errors, warns, lines);
}

/* Live DevTools rows: how many DOM nodes the loaded document has, and
 * which element the D-pad focus cursor sits on ("(none)" when idle).
 * Read-only peeks at the backend's renderer; no renderer state changes. */
int cwv_host_node_count(CwvHost *host)
{
    CwvRenderer *r;
    if (!host || !host->backend_initialized) return 0;
    r = (CwvRenderer *)host->backend.userdata;
    if (!r) return 0;
    return r->node_count;
}

void cwv_host_focus_info(CwvHost *host, char *buf, size_t cap)
{
    CwvRenderer *r;
    if (!buf || cap == 0) return;
    buf[0] = 0;
    if (!host || !host->backend_initialized) { snprintf(buf, cap, "(none)"); return; }
    r = (CwvRenderer *)host->backend.userdata;
    if (!r || r->focus_node < 0 || r->focus_node >= r->node_count) {
        snprintf(buf, cap, "(none)");
        return;
    }
    if (r->nodes[r->focus_node].id[0])
        snprintf(buf, cap, "#%s", r->nodes[r->focus_node].id);
    else
        snprintf(buf, cap, "<%s>", r->nodes[r->focus_node].tag);
}

int cwv_host_console(CwvHost *host, char *buf, size_t size)
{
    if (!host || !buf || size == 0) return 0;
    buf[0] = '\0';
    if (!host->backend_initialized) return 0;
    return cwv_backend_console_dump(&host->backend, buf, size);
}


void cwv_host_destroy(CwvHost *host)
{
    if (!host) return;
    if (host->backend.ops && host->backend.ops->shutdown)
        host->backend.ops->shutdown(&host->backend);
    cwv_console_shutdown();
    free(host);
}
