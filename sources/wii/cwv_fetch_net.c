/* cwv_fetch_net.c - Wii HTTP fetch via libogc's network stack.
 *
 * Parity with the Vita (libhttp) and Wii U (nets2) fetch seams: a
 * blocking GET for the engine's fetch() hook and remote image loads.
 *
 *  - HTTP only: libogc ships lwip WITHOUT TLS, so https:// returns -1
 *    (the engine reports the fetch failure; pages still load locally).
 *  - HTTP/1.0 with "Connection: close" is requested deliberately: no
 *    chunked transfer coding, no keep-alive bookkeeping - the body is
 *    everything after the blank line, exactly what we parse.
 *  - The network stack comes up lazily on the FIRST fetch (if_config
 *    runs DHCP once) so an offline Wii never stalls at boot.
 *  - Every failure path returns -1; the seam contract forbids crashes.
 *
 * API names mirror the real libogc network.h (gc/network.h - there is no
 * ogc/net.h in libogc; verified against libogc master 2026-10-05): if_config/
 * net_gethostbyname/net_socket/
 * net_connect/net_send/net_recv/net_setsockopt/net_close.
 */
#include "core/cwv_platform.h"
#include <grrlib.h>          /* u8/s32 typedefs (gctypes via libogc) */
#include <network.h>   /* libogc's BSD socket layer (gc/network.h) */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
 #include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CWV_WII_FETCH_MAX  (2 * 1024 * 1024)   /* 2MB hard body cap */

/* s_net: 0 = never tried, 1 = up, -1 = failed to configure */
static s32 s_net = 0;

static int wii_net_up(void) {
    char ip[16], netmask[16], gateway[16];
    if (s_net) return s_net == 1;
    /* one DHCP attempt: 20 retries, ~a few seconds on a live network;
     * the result is cached either way (offline = instant -1 later) */
    if (if_config(ip, netmask, gateway, 1, 20) < 0) {
        s_net = -1;
        return 0;
    }
    s_net = 1;
    return 1;
}

/* http://host[:port]/path -> pieces. Returns 0 on success. */
static int split_url(const char *url, char *host, size_t host_cap,
                     int *port, char *path, size_t path_cap) {
    const char *p, *slash, *colon;
    size_t hlen;
    if (!url || strncmp(url, "http://", 7) != 0) return -1;
    p = url + 7;
    slash = strchr(p, '/');
    colon = strchr(p, ':');
    if (colon && (!slash || colon < slash)) {
        hlen = (size_t)(colon - p);
        *port = atoi(colon + 1);
        if (*port <= 0) *port = 80;
    } else {
        hlen = slash ? (size_t)(slash - p) : strlen(p);
        *port = 80;
    }
    if (hlen == 0 || hlen >= host_cap) return -1;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (slash)
        snprintf(path, path_cap, "%.490s", slash);
    else
        snprintf(path, path_cap, "/");
    return 0;
}

/* Fetch a URL body into buf (cap bytes), returning the body length or
 * -1.  Text fetches then NUL-terminate; binary fetches report the
 * length through cwv_console_fetch_bin. */
static long wii_http_get(const char *url, unsigned char *buf,
                         size_t cap) {
    char host[200], path[512], request[768];
    struct hostent *he;
    struct sockaddr_in sin;
    struct timeval tv;
    s32 sock;
    int port;
    size_t got, body_start;
    long status;
    unsigned char *scratch;
    size_t scratch_cap;

    if (strncmp(url, "https://", 8) == 0)
        return -1;                     /* no TLS in libogc (see header) */
    if (!buf || cap < 8) return -1;
    if (split_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return -1;
    if (!wii_net_up()) return -1;

    he = net_gethostbyname(host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) return -1;

    sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return -1;

    /* a read timeout bounds a dead server; 10s of nothing = give up */
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    net_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)port);
    memcpy(&sin.sin_addr, he->h_addr_list[0], 4);

    if (net_connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        net_close(sock);
        return -1;
    }

    /* HTTP/1.0: no chunked coding, server closes the connection */
    snprintf(request, sizeof(request),
             "GET %.480s HTTP/1.0\r\n"
             "Host: %.190s\r\n"
             "User-Agent: CWebView/Wii\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);
    if (net_send(sock, request, (s32)strlen(request), 0) <= 0) {
        net_close(sock);
        return -1;
    }

    /* Read into a scratch buffer, then split headers from body.  The
     * body cap and the fetch cap are both bounded. */
    scratch_cap = cap + 4096;
    if (scratch_cap > CWV_WII_FETCH_MAX) scratch_cap = CWV_WII_FETCH_MAX;
    scratch = (unsigned char *)malloc(scratch_cap);
    if (!scratch) { net_close(sock); return -1; }
    got = 0;
    for (;;) {
        s32 n = net_recv(sock, scratch + got,
                         (s32)(scratch_cap - got), 0);
        if (n <= 0) break;             /* closed, timeout or error */
        got += (size_t)n;
        if (got >= scratch_cap) break;
    }
    net_close(sock);
    if (got == 0) { free(scratch); return -1; }

    /* status line: "HTTP/1.x 200 ..." */
    if (got < 12 || memcmp(scratch, "HTTP/", 5) != 0) {
        free(scratch);
        return -1;
    }
    status = strtol((const char *)scratch + 9, NULL, 10);
    if (status < 200 || status >= 300) { free(scratch); return -1; }

    /* body starts after the blank line ("\r\n\r\n") */
    {
        size_t i;
        body_start = 0;
        for (i = 0; i + 3 < got; i++) {
            if (scratch[i] == '\r' && scratch[i + 1] == '\n' &&
                scratch[i + 2] == '\r' && scratch[i + 3] == '\n') {
                body_start = i + 4;
                break;
            }
        }
        if (body_start == 0) { free(scratch); return -1; }
    }
    if (body_start >= got) { free(scratch); return -1; }

    {
        size_t body_len = got - body_start;
        size_t copy = body_len < cap ? body_len : cap;
        memcpy(buf, scratch + body_start, copy);
        free(scratch);
        return (long)copy;
    }
}

int cwv_console_fetch(const char *url, char *body, size_t cap) {
    long n;
    if (!body || cap == 0) return -1;
    n = wii_http_get(url, (unsigned char *)body, cap - 1);
    if (n < 0) { body[0] = 0; return -1; }
    body[n] = 0;
    return (int)n;
}

int cwv_console_fetch_bin(const char *url, unsigned char *buf,
                          size_t cap, size_t *out_len) {
    long n = wii_http_get(url, buf, cap);
    if (out_len) *out_len = n < 0 ? 0 : (size_t)n;
    return n < 0 ? -1 : 0;
}
