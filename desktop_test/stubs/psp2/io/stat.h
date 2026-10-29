/* Desktop stub of <psp2/io/stat.h> (self-contained: the old copy relied
 * on include_next, which only worked with an extra -I chain).  glibc's
 * <sys/stat.h> (main.c pulls it in) #defines st_atime/st_ctime/st_mtime
 * which collide with the Vita struct field names - undo them locally. */
#ifndef CWV_STUB_PSP2_IO_STAT_H
#define CWV_STUB_PSP2_IO_STAT_H
#include <psp2/types.h>
#ifdef st_atime
#undef st_atime
#endif
#ifdef st_ctime
#undef st_ctime
#endif
#ifdef st_mtime
#undef st_mtime
#endif
typedef struct SceIoDateTime {
    unsigned short year, month, day, hour, minute, second;
    unsigned int microseconds;
} SceIoDateTime;
typedef struct SceIoStat {
    unsigned int st_mode;
    unsigned int st_attr;
    SceIoDateTime st_ctime, st_atime, st_mtime;
    SceOff st_size;
    void *st_private;
    int dummy;                 /* alignment pad on the real Vita */
} SceIoStat;
int sceIoGetstat(const char *, SceIoStat *);
int sceIoChstat(const char *, SceIoStat *, unsigned int);
#endif
