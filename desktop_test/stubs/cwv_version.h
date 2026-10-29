#ifndef VITA_VERSION_H
#define VITA_VERSION_H

#define CWV_PRODUCTION_VERSION "@2.0@"
#define CWV_BUILD_VERSION "@dev@"
#define CWV_VERSION_DISPLAY CWV_PRODUCTION_VERSION " - " CWV_BUILD_VERSION
/* (81) mirrors the real/generated header fallback */
#ifndef CWV_APP_VERSION
#define CWV_APP_VERSION "0.0"
#endif

#endif
