/* cwv_fetch_nets2.cpp - Wii U HTTP fetch via the NATIVE network stack.
 *
 * Per user direction this uses the nn network path, not a portlib:
 *   - nn::ac       : bring up the console's internet connection
 *                   (auto-connect; usually already online from HBL)
 *   - sockets      : wut's BSD socket surface (sys/socket.h), which is
 *                   backed by nsysnet / nets2 - the native Nintendo
 *                   network libraries.  Plain HTTP/1.1 GET only: nets2
 *                   has no TLS, so https:// is refused with -1 (the
 *                   engine surfaces that to the page like any fetch
 *                   failure).  Raw socket HTTP is the same shape as
 *                   the Vita's sceHttp path: timeouts (15s, via
 *                   select() - see sock_wait() for why, wut's socket
 *                   layer has no SO_RCVTIMEO), a 256 KiB body cap,
 *                   status code returned, body copied to the caller's
 *                   buffer.
 *
 * The seam (cwv_platform.h) stays C - this is the app's 2nd and last
 * C++ translation unit, like cwv_ime_swkbd.cpp.
 */
#include "core/cwv_platform.h"
#include <nn/ac.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#define CWV_FETCH_TIMEOUT_SEC 15
#define CWV_FETCH_CAP (256 * 1024)

static int g_ac_up = 0;

/* Bring up AC once, lazily.  Connect() returns an error code; even on
 * error we try the socket path - the console may already be online. */
static void net_ensure(void) {
    if (g_ac_up) return;
    nn::ac::Initialize();
    nn::ac::Connect();
    g_ac_up = 1;
}

/* Wait until `fd` is ready to read (`writing` == 0) or write, or the
 * timeout expires.  Returns 1 = ready, 0 = timed out, -1 = select error.
 *
 * WHY SELECT: wut's nsysnet socket surface has NO SO_RCVTIMEO /
 * SO_SNDTIMEO - verified against wut 1.9.1's include/sys/socket.h, and
 * this file must never invent option numbers the console stack does not
 * implement (that was the 2026-09-20 (14) build break: those constants
 * compiled on desktop glibc but not against the real SDK).  The native
 * way to bound a blocking socket operation on the Wii U is select()
 * (wut's sys/select.h), so the timeout lives HERE, around each send /
 * recv, instead of in socket options.  connect() and gethostbyname()
 * stay blocking exactly as before. */
static int sock_wait(int fd, int writing, int sec)
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    int n = select(fd + 1, writing ? NULL : &fds, writing ? &fds : NULL, NULL, &tv);
    if (n < 0) return -1;
    return n == 0 ? 0 : 1;
}

/* Raw HTTP/1.1 GET.  Returns the HTTP status code (>=0) with the body
 * in `body` (cap-limited), or -1 on any transport failure. */
int cwv_console_fetch(const char *url, char *body, size_t cap) {
    if (!url || !body || cap == 0) return -1;
    if (strncmp(url, "http://", 7) != 0) return -1;   /* https/other: no TLS in nets2 */

    net_ensure();
    if (cap > CWV_FETCH_CAP) cap = CWV_FETCH_CAP;

    /* --- parse http://host[:port]/path --- */
    const char *host = url + 7;
    char hostbuf[256];
    int port = 80;
    const char *path = "/";
    {
        const char *slash = strchr(host, '/');
        const char *colon = strchr(host, ':');
        if (colon && (!slash || colon < slash)) {
            size_t hl = (size_t)(colon - host);
            if (hl >= sizeof(hostbuf)) return -1;
            memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
            port = atoi(colon + 1);
            if (port <= 0) port = 80;
        } else {
            size_t hl = slash ? (size_t)(slash - host) : strlen(host);
            if (hl >= sizeof(hostbuf)) return -1;
            memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
        }
        if (slash) path = slash;
    }

    /* --- resolve + connect --- */
    struct hostent *he = gethostbyname(hostbuf);
    if (!he || he->h_addrtype != AF_INET) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }

    /* --- send request (Connection: close so recv ends cleanly) --- */
    {
        char req[512];
        int rl = snprintf(req, sizeof(req),
                          "GET %.240s HTTP/1.1\r\n"
                          "Host: %.200s\r\n"
                          "User-Agent: CWebView\r\n"
                          "Accept: */*\r\n"
                          "Connection: close\r\n\r\n",
                          path, hostbuf);
        if (rl <= 0) { close(fd); return -1; }
        int off = 0;
        while (off < rl) {
            if (sock_wait(fd, 1, CWV_FETCH_TIMEOUT_SEC) != 1) { close(fd); return -1; }
            int n = (int)send(fd, req + off, (size_t)(rl - off), 0);
            if (n <= 0) { close(fd); return -1; }
            off += n;
        }
    }

    /* --- receive into one buffer (headers + body), then split --- */
    {
        size_t total = 0;
        size_t bufsz = cap + 32768;                 /* body cap + header room */
        char *raw = (char *)malloc(bufsz + 1);
        if (!raw) { close(fd); return -1; }
        for (;;) {
            if (total >= bufsz) break;
            if (sock_wait(fd, 0, CWV_FETCH_TIMEOUT_SEC) != 1) break;  /* timeout / error: keep what we have */
            int n = (int)recv(fd, raw + total, bufsz - total, 0);
            if (n <= 0) break;                       /* closed */
            total += (size_t)n;
        }
        close(fd);
        raw[total] = 0;

        /* status line: "HTTP/1.x 200 ..." */
        int status = -1;
        if (sscanf(raw, "HTTP/%*d.%*d %d", &status) != 1) { free(raw); return -1; }

        /* headers end at CRLFCRLF; body follows */
        const char *split = strstr(raw, "\r\n\r\n");
        if (!split) { free(raw); return status >= 0 ? status : -1; }
        const char *bstart = split + 4;
        size_t blen = total - (size_t)(bstart - raw);
        if (blen > cap) blen = cap;
        if (blen > 0) memcpy(body, bstart, blen);
        body[blen > 0 ? blen : 0] = 0;
        free(raw);
        return status;
    }
}

/* Byte-exact GET for remote resources (images/@font-face fonts/CSS)
 * loaded by the shared renderer's resource loader - see cwv_platform.h.
 * Same raw-socket HTTP/1.1 GET as cwv_console_fetch() above (nets2 has
 * no TLS: http:// only), but the body is copied out exactly instead of
 * being NUL-terminated, since binary data may contain NUL bytes. */
int cwv_console_fetch_bin(const char *url, unsigned char *buf, size_t cap,
                          size_t *out_len) {
    if (!url || !buf || cap == 0) return -1;
    if (strncmp(url, "http://", 7) != 0) return -1;   /* https/other: no TLS in nets2 */

    net_ensure();

    const char *host = url + 7;
    char hostbuf[256];
    int port = 80;
    const char *path = "/";
    {
        const char *slash = strchr(host, '/');
        const char *colon = strchr(host, ':');
        if (colon && (!slash || colon < slash)) {
            size_t hl = (size_t)(colon - host);
            if (hl >= sizeof(hostbuf)) return -1;
            memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
            port = atoi(colon + 1);
            if (port <= 0) port = 80;
        } else {
            size_t hl = slash ? (size_t)(slash - host) : strlen(host);
            if (hl >= sizeof(hostbuf)) return -1;
            memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
        }
        if (slash) path = slash;
    }

    struct hostent *he = gethostbyname(hostbuf);
    if (!he || he->h_addrtype != AF_INET) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }

    {
        char req[512];
        int rl = snprintf(req, sizeof(req),
                          "GET %.240s HTTP/1.1\r\n"
                          "Host: %.200s\r\n"
                          "User-Agent: CWebView\r\n"
                          "Accept: */*\r\n"
                          "Connection: close\r\n\r\n",
                          path, hostbuf);
        if (rl <= 0) { close(fd); return -1; }
        int off = 0;
        while (off < rl) {
            if (sock_wait(fd, 1, CWV_FETCH_TIMEOUT_SEC) != 1) { close(fd); return -1; }
            int n = (int)send(fd, req + off, (size_t)(rl - off), 0);
            if (n <= 0) { close(fd); return -1; }
            off += n;
        }
    }

    {
        size_t total = 0;
        size_t bufsz = cap + 32768;                 /* body cap + header room */
        char *raw = (char *)malloc(bufsz + 1);
        if (!raw) { close(fd); return -1; }
        for (;;) {
            if (total >= bufsz) break;
            if (sock_wait(fd, 0, CWV_FETCH_TIMEOUT_SEC) != 1) break;  /* timeout / error: keep what we have */
            int n = (int)recv(fd, raw + total, bufsz - total, 0);
            if (n <= 0) break;                       /* closed */
            total += (size_t)n;
        }
        close(fd);
        raw[total] = 0;

        int status = -1;
        if (sscanf(raw, "HTTP/%*d.%*d %d", &status) != 1) { free(raw); return -1; }

        const char *split = strstr(raw, "\r\n\r\n");
        if (!split) { free(raw); return -1; }
        const char *bstart = split + 4;
        size_t blen = total - (size_t)(bstart - raw);
        if (blen > cap) blen = cap;
        if (blen > 0) memcpy(buf, bstart, blen);
        free(raw);
        if (out_len) *out_len = blen;
        return 0;
    }
}
