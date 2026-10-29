/* Desktop stub of wut's padscore/kpad.h - ONLY the surface used by
 * sources/wiiu/main.c (Wiimote IR cursor).  Mirrors wut 1.9.1's
 * KPADStatus layout (field names and semantics verified against the
 * wut master header); the real build uses devkitPPC's padscore headers. */
#ifndef STUB_WUT_PADSCORE_KPAD_H
#define STUB_WUT_PADSCORE_KPAD_H
#include <stdint.h>
#include <padscore/wpad.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KPADError {
    KPAD_ERROR_OK = 0,
    KPAD_ERROR_NO_SAMPLES = -1,
    KPAD_ERROR_INVALID_CONTROLLER = -2,
    KPAD_ERROR_WPAD_UNINIT = -3,
    KPAD_ERROR_BUSY = -4,
    KPAD_ERROR_UNINITIALIZED = -5
} KPADError;

typedef struct KPADVec2D { float x; float y; } KPADVec2D;
typedef struct KPADVec3D { float x; float y; float z; } KPADVec3D;

typedef struct KPADStatus {
    uint32_t hold;
    uint32_t trigger;
    uint32_t release;
    KPADVec3D acc;
    float accMagnitude;
    float accVariation;
    /* Position the Wii Remote is pointing at; positive right/down. */
    KPADVec2D pos;
    KPADVec2D posDiff;
    float posDiffMagnitude;
    KPADVec2D angle;
    KPADVec2D angleDiff;
    float angleDiffMagnitude;
    float dist;
    float distDiff;
    float distDiffMagnitude;
    KPADVec2D down;
    uint8_t extensionType;   /* WPADExtensionType */
    int8_t error;
    int8_t posValid;         /* nonzero while `pos` is a real IR point */
    uint8_t format;
} KPADStatus;

int32_t KPADReadEx(int chan, KPADStatus *bufs, uint32_t count,
                   KPADError *error);

#ifdef __cplusplus
}
#endif

#endif
