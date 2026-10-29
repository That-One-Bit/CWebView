#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "core/cwv_renderer.h"
#include "core/cwv_backend.h"
static void mylog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
int main(void) {
    CwvBackend b; char out[8192];
    b.ops = cwv_backend_default_ops();
    if (b.ops->initialize(&b, 960, 544) != 0) { printf("INIT FAILED\n"); return 1; }
    cwv_backend_set_runtime_log(&b, mylog);
    /* script-free page with a MISSING image: no JS context can exist */
    int rc = b.ops->load(&b, "file:///app0/loghook.html");
    printf("load rc=%d\n", rc);
    js_console_dump((CwvRenderer *)b.userdata, out, sizeof(out));
    printf("console: %s", out);
    return 0;
}
