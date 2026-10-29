#ifndef MS_THR_H
#define MS_THR_H
#include <psp2/types.h>
int sceKernelDelayThread(unsigned int);
int sceKernelDelayThreadCB(unsigned int);
int sceKernelCreateThread(const char*, int (*)(SceSize, void*), int, int, unsigned int, unsigned int, void*);
int sceKernelStartThread(int, int, void*);
void sceKernelExitThread(int);
int sceKernelWaitThreadEnd(int,int*,unsigned int);
int sceKernelCreateCallback(const char*, int, int (*)(int,int,int,void*), void*);
#endif
