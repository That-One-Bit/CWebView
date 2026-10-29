#ifndef MS_MOD_H
#define MS_MOD_H
#include <stddef.h>
typedef int SceUID;
typedef int SceSize;
int sceKernelLoadStartModule(const char*,SceSize,void*,unsigned int,void*,int*);
int sceKernelStopUnloadModule(SceUID,SceSize,void*,unsigned int,void*,int*);
#endif
