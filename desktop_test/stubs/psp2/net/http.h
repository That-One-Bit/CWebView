/* Minimal psp2/net/http.h stub for the desktop syntax gate ONLY.
 * Matches the REAL VitaSDK header (vitasdk/vita-headers include/psp2/net/http.h):
 * same names, same signatures, same constant values - verified 2026-09-18.
 * Nothing here is ever executed; desktop tests never run the sceHttp path. */
#ifndef VW_STUB_PSP2_NET_HTTP_H
#define VW_STUB_PSP2_NET_HTTP_H

#include <stddef.h>

/* real values from the SDK enums */
#define SCE_HTTP_VERSION_1_0 1
#define SCE_HTTP_VERSION_1_1 2
#define SCE_HTTP_PROXY_AUTO  0
#define SCE_HTTP_PROXY_MANUAL 1
#define SCE_HTTP_METHOD_GET   0

int sceHttpInit(unsigned int poolSize);
int sceHttpTerm(void);
int sceHttpCreateTemplate(const char *userAgent, int httpVersion,
                          int enableAutoProxy);
int sceHttpDeleteTemplate(int templateId);
int sceHttpCreateConnectionWithURL(int templateId, const char *url,
                                   int enableKeepalive);
int sceHttpDeleteConnection(int connectionId);
int sceHttpCreateRequestWithURL(int connectionId, int method,
                                 const char *url,
                                 unsigned long long contentLength);
int sceHttpDeleteRequest(int requestId);
int sceHttpSendRequest(int requestId, const void *postData,
                       unsigned int size);
int sceHttpReadData(int requestId, void *buf, unsigned int size);
int sceHttpGetStatusCode(int requestId, int *statusCode);
int sceHttpSetSendTimeOut(int id, unsigned int usec);
int sceHttpSetRecvTimeOut(int id, unsigned int usec);

#endif
