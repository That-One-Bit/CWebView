#include <psp2/kernel/processmgr.h>
#include <math.h>
#include "cwv_scroll.h"

#define TOUCH_DRAG_THRESHOLD 7.0f
#define ANALOG_DEADZONE 18
#define ANALOG_MAX 127
#define ANALOG_MAX_STEP 7.0f

static volatile int g_initialized;
static volatile int g_touch_active;
static volatile int g_touch_y;
static volatile int g_touch_dragging;
static volatile int g_touch_moved;
static volatile float g_delta;

int cwv_scroll_initialize(int viewport_height)
{
    (void)viewport_height;
    g_touch_active = 0;
    g_touch_y = 0;
    g_touch_dragging = 0;
    g_touch_moved = 0;
    g_delta = 0.0f;
    g_initialized = 1;
    return 0;
}

int cwv_scroll_update(int touch_active, int touch_y, int ry)
{
    if (!g_initialized) return -1;
    if (touch_active) {
        if (!g_touch_active) {
            g_touch_y = touch_y;
            g_touch_dragging = 0;
            g_touch_moved = 0;
        } else {
            float dy = (float)(g_touch_y - touch_y);
            if (fabsf(dy) >= TOUCH_DRAG_THRESHOLD) g_touch_dragging = 1;
            if (g_touch_dragging) {
                g_delta += dy;
                g_touch_moved = 1;
            }
            g_touch_y = touch_y;
        }
        g_touch_active = 1;
    } else {
        g_touch_active = 0;
        g_touch_y = 0;
    }

    /* Positive raw stick Y means the stick is pushed downward. The page
       should therefore move downward too. */
    int a = ry - 128;
    int magnitude = a < 0 ? -a : a;
    if (!touch_active && magnitude > ANALOG_DEADZONE) {
        int usable = magnitude - ANALOG_DEADZONE;
        int range = ANALOG_MAX - ANALOG_DEADZONE;
        float speed = (float)usable / (float)range * ANALOG_MAX_STEP;
        if (speed > ANALOG_MAX_STEP) speed = ANALOG_MAX_STEP;
        g_delta += (a > 0 ? speed : -speed);
    }
    return 0;
}

float cwv_scroll_take_delta(void)
{
    float d = g_delta;
    g_delta = 0.0f;
    return d;
}

int cwv_scroll_is_dragging(void)
{
    return g_touch_dragging && g_touch_moved;
}

void cwv_scroll_reset(void)
{
    g_touch_active = 0;
    g_touch_y = 0;
    g_touch_dragging = 0;
    g_touch_moved = 0;
    g_delta = 0.0f;
}

int main(void) { return 0; }
int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    g_initialized = 0;
    g_delta = 0.0f;
    return 0;
}
