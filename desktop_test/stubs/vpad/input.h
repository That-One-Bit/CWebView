/* Minimal desktop stub of wut 1.9.1's vpad/input.h.  Only for
 * syntax-checking sources/wiiu/cwv_ime_swkbd.cpp (build_wiiu_gate.sh);
 * the real build uses devkitPro wut. */
#ifndef STUB_VPAD_INPUT_H
#define STUB_VPAD_INPUT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VPADTouchData {
    uint16_t x, y;
    uint16_t touched;
} VPADTouchData;

typedef struct VPADStatus {
    /* compressed: the app only feeds .tpNormal back into the keyboard */
    VPADTouchData tpNormal;
} VPADStatus;

typedef enum VPADChan { VPAD_CHAN_0 = 0 } VPADChan;

typedef enum VPADReadError {
    VPAD_READ_SUCCESS = 0,
    VPAD_READ_NO_SAMPLES = -1,
    VPAD_READ_INVALID_CONTROLLER = -2,
    VPAD_READ_BUSY = -4,
    VPAD_READ_UNINITIALIZED = -5,
} VPADReadError;

int32_t VPADRead(VPADChan chan, VPADStatus *buffers, uint32_t count,
                 VPADReadError *outError);
void VPADGetTPCalibratedPoint(VPADChan chan, VPADTouchData *calibrated,
                               const VPADTouchData *uncalibrated);

#ifdef __cplusplus
}
#endif
#endif
