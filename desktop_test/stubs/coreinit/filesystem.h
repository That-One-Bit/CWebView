/* Minimal desktop stub of wut 1.9.1's coreinit/filesystem.h.  Only for
 * syntax-checking sources/wiiu/cwv_ime_swkbd.cpp (build_wiiu_gate.sh);
 * the real build uses devkitPro wut.  FSClient is opaque here except
 * for its size - the app allocates one on the heap, so the struct must
 * be complete (the real header defines it with the same footprint). */
#ifndef STUB_COREINIT_FILESYSTEM_H
#define STUB_COREINIT_FILESYSTEM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FSErrorFlag { FS_ERROR_FLAG_NONE = 0 } FSErrorFlag;

typedef enum FSStatus { FS_STATUS_OK = 0 } FSStatus;

typedef struct FSClient { unsigned char _opaque[0x1900]; } FSClient;

void FSInit(void);
FSStatus FSAddClient(FSClient *client, FSErrorFlag errorMask);
FSStatus FSDelClient(FSClient *client, FSErrorFlag errorMask);

#ifdef __cplusplus
}
#endif
#endif
