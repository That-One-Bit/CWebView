#include "cwv_boot_display.h"

#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <stdint.h>
#include <string.h>

#define BOOT_FB_WIDTH 960
#define BOOT_FB_HEIGHT 544
#define BOOT_FB_SIZE (2 * 1024 * 1024)

static SceUID g_memblock = -1;
static uint32_t *g_framebuffer = NULL;
static int g_initialized = 0;
static SceDisplayFrameBuf g_frame;

static uint32_t rgba_to_bgrx(unsigned int rgba)
{
    unsigned int r = (rgba >> 24) & 0xff;
    unsigned int g = (rgba >> 16) & 0xff;
    unsigned int b = (rgba >> 8) & 0xff;
    unsigned int a = rgba & 0xff;
    (void)a;
    return (b << 16) | (g << 8) | r;
}

int cwv_boot_display_init(void)
{
    int rc;

    if (g_initialized)
        return 0;

    g_memblock = sceKernelAllocMemBlock(
        "CWVBootFB",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        BOOT_FB_SIZE,
        NULL);
    if (g_memblock < 0)
        return (int)g_memblock;

    rc = sceKernelGetMemBlockBase(g_memblock, (void **)&g_framebuffer);
    if (rc < 0 || !g_framebuffer) {
        sceKernelFreeMemBlock(g_memblock);
        g_memblock = -1;
        g_framebuffer = NULL;
        return rc < 0 ? rc : -1;
    }

    memset(&g_frame, 0, sizeof(g_frame));
    g_frame.size = sizeof(g_frame);
    g_frame.base = g_framebuffer;
    g_frame.pitch = BOOT_FB_WIDTH;
    g_frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    g_frame.width = BOOT_FB_WIDTH;
    g_frame.height = BOOT_FB_HEIGHT;

    rc = sceDisplaySetFrameBuf(&g_frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (rc < 0) {
        sceKernelFreeMemBlock(g_memblock);
        g_memblock = -1;
        g_framebuffer = NULL;
        return rc;
    }

    g_initialized = 1;
    cwv_boot_display_fill(0xff101820u);
    return 0;
}

void cwv_boot_display_fill(unsigned int rgba)
{
    uint32_t pixel;
    int y;

    if (!g_initialized || !g_framebuffer)
        return;

    pixel = rgba_to_bgrx(rgba);
    for (y = 0; y < BOOT_FB_HEIGHT; ++y) {
        uint32_t *row = g_framebuffer + (y * BOOT_FB_WIDTH);
        int x;
        for (x = 0; x < BOOT_FB_WIDTH; ++x)
            row[x] = pixel;
    }
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(&g_frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void cwv_boot_display_draw_test(void)
{
    int y;
    if (!g_initialized || !g_framebuffer)
        return;

    /* Three large unmistakable bands. No fonts, textures, vita2d, or WebKit. */
    for (y = 0; y < BOOT_FB_HEIGHT; ++y) {
        uint32_t pixel;
        if (y < 136)
            pixel = rgba_to_bgrx(0xe34a4aff);       /* red */
        else if (y < 272)
            pixel = rgba_to_bgrx(0x35c759ff);       /* green */
        else if (y < 408)
            pixel = rgba_to_bgrx(0x3f78e8ff);       /* blue */
        else
            pixel = rgba_to_bgrx(0x202830ff);       /* dark */

        uint32_t *row = g_framebuffer + (y * BOOT_FB_WIDTH);
        int x;
        for (x = 0; x < BOOT_FB_WIDTH; ++x)
            row[x] = pixel;
    }

    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(&g_frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void cwv_boot_display_shutdown(void)
{
    if (!g_initialized)
        return;

    sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_IMMEDIATE);
    if (g_memblock >= 0)
        sceKernelFreeMemBlock(g_memblock);
    g_memblock = -1;
    g_framebuffer = NULL;
    memset(&g_frame, 0, sizeof(g_frame));
    g_initialized = 0;
}
