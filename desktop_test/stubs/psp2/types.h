/* Minimal psp2/types.h stub for the desktop syntax gate ONLY. */
#ifndef VW_STUB_PSP2_TYPES_H
#define VW_STUB_PSP2_TYPES_H
#include <stdint.h>
typedef int SceUID;
typedef int SceSSize;
typedef int SceSize;
typedef unsigned int SceUInt;
typedef unsigned short SceWChar16;
typedef unsigned char SceUChar8;
typedef unsigned char SceUInt8;
typedef signed char SceInt8;
typedef short SceInt16;
typedef unsigned int SceUInt32;
typedef unsigned long long SceUInt64;
#endif
/* real psp2/types.h: SceOff is the 64-bit file offset type */
typedef long long SceOff;
