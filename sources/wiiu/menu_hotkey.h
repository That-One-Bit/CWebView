#ifndef CWV_WIIU_MENU_HOTKEY_H
#define CWV_WIIU_MENU_HOTKEY_H
/* wiiu-sdl2's joystick button order is documented in the Wii U patch to
 * devkitPro/wut-packages SDL2.  Keep these bits private to the Wii U port:
 * no button mapping changes are needed in shared code or the Wii port. */
enum {
    CWV_WIIU_HK_L = 1u << 0, CWV_WIIU_HK_R = 1u << 1,
    CWV_WIIU_HK_START = 1u << 2, CWV_WIIU_HK_SELECT = 1u << 3
};
static inline unsigned cwv_wiiu_hotkey_bit(int button)
{
    switch (button) {
        case 6: return CWV_WIIU_HK_L;
        case 7: return CWV_WIIU_HK_R;
        case 10: return CWV_WIIU_HK_START;
        case 11: return CWV_WIIU_HK_SELECT;
        default: return 0;
    }
}
/* 0=SELECT (default), 1=START (legacy), 2=either.  Require a fresh
 * transition to the complete chord, not held/repeated button events. */
static inline int cwv_wiiu_hotkey_edge(int mode, unsigned before, unsigned after)
{
    unsigned base=CWV_WIIU_HK_L|CWV_WIIU_HK_R;
    unsigned select=base|CWV_WIIU_HK_SELECT;
    unsigned start=base|CWV_WIIU_HK_START;
    return ((mode==0 || mode==2) && (after&select)==select && (before&select)!=select)
        || ((mode==1 || mode==2) && (after&start)==start && (before&start)!=start);
}
#endif
