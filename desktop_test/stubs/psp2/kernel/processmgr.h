#ifndef MS_PROC_H
#define MS_PROC_H
#include <stddef.h>
typedef int SceSize;
typedef void SceKernelMainThreadFunc;
int sceKernelExitProcess(int);
int sceKernelProcessExit(int);
int sceKernelGetMainThread(SceSize*,void*);
int sceKernelChangeThreadPriority(int,int);
/* 64-bit process uptime in microseconds (main.c: FPS + uptime rows) */
typedef long long SceKernelSysClock;
long long sceKernelGetProcessTimeWide(void);
unsigned int sceKernelGetProcessTimeLow(void);
#endif
