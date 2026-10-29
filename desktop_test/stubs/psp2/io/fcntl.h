/* Desktop test stub for <psp2/io/fcntl.h> */
#ifndef STUB_PSP2_IO_FCNTL_H
#define STUB_PSP2_IO_FCNTL_H
typedef int SceUID;
#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_CREAT  0x0200
#define SCE_O_TRUNC  0x0400
#define SCE_O_APPEND 0x0100
int sceIoRemove(const char*);





int sceIoOpen(const char *file, int flags, int mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *buf, int len);
int sceIoWrite(SceUID fd, const void *buf, int len);
int sceIoMkdir(const char *, int);
#endif
