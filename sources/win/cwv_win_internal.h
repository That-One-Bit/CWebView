/* cwv_win_internal.h - shared declarations between the Windows platform
 * seam (cwv_platform.c) and the app loop (main.c). Nothing here is used
 * by the shared engine; the seam in cwv_platform.h stays unchanged. */
#ifndef CWV_WIN_INTERNAL_H
#define CWV_WIN_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define CWV_WIN_DEFAULT_W 960
#define CWV_WIN_DEFAULT_H 544
/* pgf-style text sizes arrive as float scale factors; 1.0 maps to this
 * pixel height, matching the visual size the consoles render. */
/* CWV_WIN_BASE_FONT_PX (24) was REMOVED in the 2026-10-03 render
 * parity round: GDI em size now comes from the per-face fit factor
 * (font_fit in cwv_platform.c - cell height = the engine's 28 px/unit
 * line box), replacing the fixed 24 px/unit approximation. */

/* Frame lifecycle driven by main.c around cwv_host_update/draw. */
void cwv_win_frame_begin(void);
void cwv_win_frame_end(HWND hwnd);
/* Change the virtual page resolution (used when the host is created). */
void cwv_win_frame_size(int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* CWV_WIN_INTERNAL_H */
