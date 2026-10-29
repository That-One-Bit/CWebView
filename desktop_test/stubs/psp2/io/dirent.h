/* Desktop stub of <psp2/io/dirent.h> - mirrors the real VitaSDK surface:
 * d_stat is a full SceIoStat (the old copy was an opaque 48-byte pad, so
 * st_mode/st_size were invisible to the desktop syntax gate). */
#ifndef CWV_STUB_PSP2_IO_DIRENT_H
#define CWV_STUB_PSP2_IO_DIRENT_H
#include <psp2/io/stat.h>
typedef struct SceIoDirent { SceIoStat d_stat; char d_name[256]; void *d_private; int dummy; } SceIoDirent;
int sceIoDopen(const char*);
int sceIoDread(int,SceIoDirent*);
int sceIoDclose(int);
#endif
