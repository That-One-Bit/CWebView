/* desktop_test stub: network.h - mirrors the REAL libogc networking header
 * (gc/network.h, included as <network.h>; libogc has no ogc/net.h).
 * Used by sources/wii/cwv_fetch_net.c (see that file for the API reality
 * check).  The stub exists so the desktop Wii gate
 * (build_wii_gate.sh) syntax-checkes the fetch port against the same
 * function names the real devkitPPC build sees; if libogc renames
 * anything, the real build fails and this stub gets corrected with it.
 *
 * Real API (libogc master, gc/network.h):
 *   s32 if_config(char *ip, char *netmask, char *gateway,
 *                 bool use_dhcp, s32 max_retries);
 *   struct hostent *net_gethostbyname(const char *name);
 *   s32 net_socket(u32 domain, u32 type, u32 protocol);
 *   s32 net_connect(s32 s, struct sockaddr *addr, socklen_t addrlen);
 *   s32 net_send(s32 s, const void *data, s32 size, u32 flags);
 *   s32 net_recv(s32 s, void *mem, s32 len, u32 flags);
 *   s32 net_setsockopt(s32 s, u32 level, u32 optname,
 *                      const void *optval, u32 optlen);
 *   s32 net_close(s32 s);
 */
#ifndef STUB_WII_NETWORK_H
#define STUB_WII_NETWORK_H

#include <sys/types.h>     /* real net.h pulls the socket types too; on
                            * devkitPPC these come from newlib's
                            * sys/socket.h - which the fetch port also
                            * includes explicitly */
#include <sys/socket.h>
#include <netdb.h>         /* struct hostent */
#include <stdbool.h>

/* s32/u32 come from gctypes via grrlib.h, which the fetch port
 * includes first; the console build gets the same types from the real
 * libogc headers. */

s32 if_config(char *ip, char *netmask, char *gateway,
              bool use_dhcp, s32 max_retries);
struct hostent *net_gethostbyname(const char *name);
s32 net_socket(s32 domain, s32 type, s32 protocol);
s32 net_connect(s32 s, struct sockaddr *addr, socklen_t addrlen);
s32 net_send(s32 s, const void *data, s32 size, s32 flags);
s32 net_recv(s32 s, void *mem, s32 len, s32 flags);
s32 net_setsockopt(s32 s, u32 level, u32 optname,
                   const void *optval, u32 optlen);
s32 net_close(s32 s);

#endif /* STUB_WII_NETWORK_H */
