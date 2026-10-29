#ifndef CWV_SCROLL_H
#define CWV_SCROLL_H
#ifdef __cplusplus
extern "C" {
#endif
int cwv_scroll_initialize(int viewport_height);
int cwv_scroll_update(int touch_active, int touch_y, int ry);
float cwv_scroll_take_delta(void);
int cwv_scroll_is_dragging(void);
void cwv_scroll_reset(void);
#ifdef __cplusplus
}
#endif
#endif
