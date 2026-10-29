/* Desktop test stub for <curl/curl.h> */
#ifndef STUB_CURL_H
#define STUB_CURL_H
#include <stddef.h>
typedef void CURL;
typedef int CURLcode;
#define CURLE_OK 0
#define CURL_GLOBAL_DEFAULT 3L
#define CURLOPT_URL 0x10002
#define CURLOPT_FOLLOWLOCATION 0x00030002
#define CURLOPT_WRITEFUNCTION 0x20011
#define CURLOPT_WRITEDATA 0x10001
#define CURLOPT_USERAGENT 0x10002
#define CURLOPT_TIMEOUT 0x10003
CURLcode curl_global_init(long flags);
void curl_global_cleanup(void);
CURL *curl_easy_init(void);
CURLcode curl_easy_setopt(CURL *h, int opt, ...);
CURLcode curl_easy_perform(CURL *h);
void curl_easy_cleanup(CURL *h);
#endif
