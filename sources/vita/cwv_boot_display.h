#ifndef VITA_BOOT_DISPLAY_H
#define VITA_BOOT_DISPLAY_H

/* Minimal display diagnostic independent of vita2d/PGF/WebKit. */
int cwv_boot_display_init(void);
void cwv_boot_display_fill(unsigned int rgba);
void cwv_boot_display_draw_test(void);
void cwv_boot_display_shutdown(void);

#endif
