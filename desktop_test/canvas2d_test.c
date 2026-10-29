#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cwv_renderer.h"

int main(int argc, char **argv) {
    CwvBackend backend;
    const CwvBackendOps *ops = cwv_backend_default_ops();
    CwvRenderer *r;
    int i, found = -1;
    memset(&backend, 0, sizeof(backend));
    cwv_platform_set_app_root("file:///canvas-test",
                              argc > 1 ? argv[1] : "canvas2d_fixture");
    if (ops->initialize(&backend, 320, 240) != 0) return 1;
    if (ops->load(&backend, "file:///canvas-test/canvas.html") != 0) {
        puts("FAIL: fixture page did not load"); return 1;
    }
    r = (CwvRenderer *)backend.userdata;
    for (i = 0; i < r->node_count; i++)
        if (!strcmp(r->nodes[i].id, "surface")) { found = i; break; }
    if (found < 0 || !r->nodes[found].is_canvas || !r->nodes[found].canvas_pixels) {
        char console[4096];
        js_console_dump(r, console, sizeof(console));
        printf("FAIL: canvas backing store was not created (nodes=%d flag=%d pixels=%p)\n%s\n", r->node_count, found >= 0 ? r->nodes[found].is_canvas : -1, found >= 0 ? (void *)r->nodes[found].canvas_pixels : NULL, console);
        for (i = 0; i < r->node_count; i++) printf("node %d tag=%s id=%s canvas=%d\n", i, r->nodes[i].tag, r->nodes[i].id, r->nodes[i].is_canvas);
        return 1;
    }
    {
        CwvNode *n = &r->nodes[found];
        const unsigned char *cleared = n->canvas_pixels;
        const unsigned char *painted = n->canvas_pixels + (size_t)3 * n->canvas_width * 4u + 3u * 4u;
        if (cleared[0] || cleared[1] || cleared[2] || cleared[3]) {
            puts("FAIL: clearRect did not clear the pixel"); return 1;
        }
        if (painted[0] != 0x12 || painted[1] != 0x34 || painted[2] != 0x56 || painted[3] != 255) {
            printf("FAIL: fillRect pixel was %u,%u,%u,%u\n", painted[0], painted[1], painted[2], painted[3]);
            return 1;
        }
        if (!n->canvas_dirty) { puts("FAIL: canvas was not marked dirty"); return 1; }
        {
            const unsigned char *stroke_edge = n->canvas_pixels + ((size_t)2 * n->canvas_width + 13u) * 4u;
            const unsigned char *stroke_inside = n->canvas_pixels + ((size_t)3 * n->canvas_width + 13u) * 4u;
            if (stroke_edge[0] != 0 || stroke_edge[1] != 255 || stroke_edge[2] != 0 || stroke_edge[3] != 255) {
                printf("FAIL: strokeRect edge pixel was %u,%u,%u,%u\n", stroke_edge[0], stroke_edge[1], stroke_edge[2], stroke_edge[3]);
                return 1;
            }
            if (stroke_inside[0] || stroke_inside[1] || stroke_inside[2] || stroke_inside[3]) {
                printf("FAIL: strokeRect filled its interior (%u,%u,%u,%u)\n", stroke_inside[0], stroke_inside[1], stroke_inside[2], stroke_inside[3]);
                return 1;
            }
        }
        {
            const unsigned char *triangle = n->canvas_pixels + ((size_t)7 * n->canvas_width + 22u) * 4u;
            const unsigned char *curve = n->canvas_pixels + ((size_t)13 * n->canvas_width + 3u) * 4u;
            const unsigned char *arc = n->canvas_pixels + ((size_t)16 * n->canvas_width + 22u) * 4u;
            const unsigned char *arc_center = n->canvas_pixels + ((size_t)16 * n->canvas_width + 20u) * 4u;
            const unsigned char *bezier = n->canvas_pixels + ((size_t)13 * n->canvas_width + 12u) * 4u;
            const unsigned char *clipped_line = n->canvas_pixels + ((size_t)22 * n->canvas_width + 16u) * 4u;
            if (triangle[2] != 255 || triangle[3] != 255) {
                printf("FAIL: path fill pixel was %u,%u,%u,%u\n", triangle[0], triangle[1], triangle[2], triangle[3]);
                return 1;
            }
            if (curve[0] != 255 || curve[1] != 255 || curve[2] != 0 || curve[3] != 255) {
                printf("FAIL: quadratic curve pixel was %u,%u,%u,%u\n", curve[0], curve[1], curve[2], curve[3]);
                return 1;
            }
            if (arc[0] != 0 || arc[1] != 255 || arc[2] != 255 || arc[3] != 255) {
                printf("FAIL: arc pixel was %u,%u,%u,%u\n", arc[0], arc[1], arc[2], arc[3]);
                return 1;
            }
            if (arc_center[3] != 0) {
                printf("FAIL: stroked arc filled its center (%u,%u,%u,%u)\n", arc_center[0], arc_center[1], arc_center[2], arc_center[3]);
                return 1;
            }
            if (bezier[0] != 255 || bezier[1] != 0 || bezier[2] != 255 || bezier[3] != 255) {
                printf("FAIL: Bezier curve pixel was %u,%u,%u,%u\n", bezier[0], bezier[1], bezier[2], bezier[3]);
                return 1;
            }
            if (clipped_line[0] != 255 || clipped_line[1] != 136 || clipped_line[2] != 0 || clipped_line[3] != 255) {
                printf("FAIL: clipped path pixel was %u,%u,%u,%u\n", clipped_line[0], clipped_line[1], clipped_line[2], clipped_line[3]);
                return 1;
            }
            {
                const unsigned char *rect_pixel = n->canvas_pixels + ((size_t)3 * n->canvas_width + 27u) * 4u;
                if (rect_pixel[0] != 0 || rect_pixel[1] != 170 || rect_pixel[2] != 255 || rect_pixel[3] != 255) {
                    printf("FAIL: rect() path fill pixel was %u,%u,%u,%u\n", rect_pixel[0], rect_pixel[1], rect_pixel[2], rect_pixel[3]);
                    return 1;
                }
            }
            {
                const unsigned char *restored = n->canvas_pixels + ((size_t)11 * n->canvas_width + 27u) * 4u;
                if (restored[0] != 255 || restored[1] != 0 || restored[2] != 0 || restored[3] != 255) {
                    printf("FAIL: save()/restore() state was %u,%u,%u,%u\n", restored[0], restored[1], restored[2], restored[3]);
                    return 1;
                }
            }
        }
        {
            const unsigned char *repeat_context_pixel = n->canvas_pixels + 4u * 4u;
            if (repeat_context_pixel[0] != 0x12 || repeat_context_pixel[1] != 0x34 ||
                repeat_context_pixel[2] != 0x56 || repeat_context_pixel[3] != 255) {
                printf("FAIL: repeated getContext did not preserve fillStyle (%u,%u,%u,%u)\n",
                       repeat_context_pixel[0], repeat_context_pixel[1],
                       repeat_context_pixel[2], repeat_context_pixel[3]);
                return 1;
            }
        }
        {
            const unsigned char *alpha_pixel = n->canvas_pixels + ((size_t)4 * n->canvas_width + 4u) * 4u;
            const unsigned char *clipped_pixel = n->canvas_pixels + ((size_t)10 * n->canvas_width + 15u) * 4u;
            if (alpha_pixel[0] < 136 || alpha_pixel[0] > 138 ||
                alpha_pixel[1] < 25 || alpha_pixel[1] > 27 ||
                alpha_pixel[2] < 42 || alpha_pixel[2] > 44 || alpha_pixel[3] != 255) {
                printf("FAIL: alpha composite pixel was %u,%u,%u,%u\n",
                       alpha_pixel[0], alpha_pixel[1], alpha_pixel[2], alpha_pixel[3]);
                return 1;
            }
            if (clipped_pixel[0] != 0x12 || clipped_pixel[1] != 0x34 ||
                clipped_pixel[2] != 0x56 || clipped_pixel[3] != 255) {
                printf("FAIL: clipped huge rectangle pixel was %u,%u,%u,%u\n",
                       clipped_pixel[0], clipped_pixel[1], clipped_pixel[2], clipped_pixel[3]);
                return 1;
            }
        }
    }
    puts("Canvas 2D test: PASS (context identity, path/rect fill, save/restore, quadratic/Bézier curves, arcs, fill/stroke/clear rectangles, alpha, clipping, RGBA backing store)");
    ops->shutdown(&backend);
    return 0;
}
