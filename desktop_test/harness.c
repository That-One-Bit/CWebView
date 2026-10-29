/* Golden-test harness for the Vita WebView renderer.
 *
 * It includes the renderer .c file directly (so all static internals are
 * visible), runs a fixed set of pages through load/draw/input, and prints a
 * canonical dump of the parsed node tree plus every recorded draw call.
 * Run it against the ORIGINAL and the REFACTORED source and diff the output:
 * identical output = behavior preserved.
 *
 * Build (legacy single-file backend, e.g. the ORIGINAL source):
 *   gcc -I stubs -I <dir> -DBACKEND_SRC='"<backend>.c"' harness.c stub_impl.c
 * Build (current split renderer: header + css/html/js/backend files):
 *   gcc -I stubs -I ../src/backend -I ../src/backend/js harness.c stub_impl.c \
 *       ../src/backend/cwv_{css,html,js,backend}.c ../src/backend/js/duktape.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void stub_set_log(FILE *f);
#ifdef BACKEND_SRC
#include BACKEND_SRC          /* legacy mode: original single .c file */
#else
#include "core/cwv_renderer.h" /* current mode: split renderer */
#endif

static void dump_node(int i, const CwvNode *n) {
    printf("NODE %d tag=[%s] text=[%.80s] x=%.2f y=%.2f w=%.2f h=%.2f "
           "color=%08X cexp=%d bg=%08X hasbg=%d hasb=%d bw=%.2f bc=%08X bstyle=%d "
           "fs=%.3f bold=%d cent=%d dnone=%d dexp=%d dmode=%d "
           "mt=%.2f mr=%.2f mb=%.2f ml=%.2f pl=%.2f pr=%.2f pt=%.2f pb=%.2f "
           "img=%d islink=%d href=[%s] tgt=[%s] btn=%d press=%d focusself=%d "
           "dis=%d tab=%d sel=%d tf=%d pw=%d ml=%d ph=[%s] opt=%d optsel=%d "
           "selopen=%d selidx=%d selhov=%d ovx=%d ovy=%d grp=%d vcm=[%s] "
           "id=[%s] cls=[%s] vcmact=%d modal=%d "
           "sreg=%d st=%.2f sl=%.2f smy=%.2f smx=%.2f ovexp=%d\n",
           i, n->tag, n->text, n->x, n->y, n->w, n->h,
           n->color, n->color_explicit, n->background, n->has_background,
           n->has_border, n->border_width, n->border_color, n->border_style,
           n->font_size, n->bold, n->centered, n->display_none, n->display_explicit,
           n->display_mode,
           n->margin_top, n->margin_right, n->margin_bottom, n->margin_left,
           n->padding_left, n->padding_right, n->padding_top, n->padding_bottom,
           n->is_image, n->is_link, n->href, n->link_target,
           n->is_button, n->button_pressed, n->focusable_self, n->disabled,
           n->tabindex, n->is_select, n->is_textfield, n->textfield_password,
           n->textfield_multiline, n->placeholder, n->is_option, n->option_selected,
           n->select_open, n->select_index, n->select_hover_index,
           n->overflow_x, n->overflow_y, n->button_group,
           n->vcm, n->id, n->cls, n->vcm_active, n->control_modal,
           n->scroll_region, n->scroll_top, n->scroll_left,
           n->scroll_max_y, n->scroll_max_x, n->overflow_explicit);
}

static void dump_state(const char *label, CwvBackend *b) {
    CwvRenderer *r = (CwvRenderer *)b->userdata;
    printf("=== %s ===\n", label);
    printf("STATE nodes=%d rules=%d fonts=%d bodybg=%08X bodycol=%08X "
           "curx=%.2f cury=%.2f lh=%.2f scx=%.2f scy=%.2f "
           "ovx=%d ovy=%d focus=%d selfocus=%d imenode=%d\n",
           r->node_count, r->rule_count, r->font_count,
           r->body_background, r->body_color,
           r->cursor_x, r->cursor_y, r->line_height_current,
           r->scroll_x, r->scroll_y, r->overflow_x, r->overflow_y,
           r->focus_node, r->select_focus_node, r->ime_node);
    for (int i = 0; i < r->rule_count; i++)
        printf("RULE %d sel=[%s] decl=[%.120s]\n", i, r->rules[i].selector,
               r->rules[i].declarations);
    for (int i = 0; i < r->node_count; i++) dump_node(i, &r->nodes[i]);
    fflush(stdout);
}

static void run_page(const char *file, int w, int h, int scenario) {
    CwvBackend b;
    char url[512];
    snprintf(url, sizeof(url), "file:///app0/%s", file);
    b.ops = cwv_backend_default_ops();
    printf("\n##### PAGE %s (%dx%d) scenario=%d #####\n", file, w, h, scenario);
    if (b.ops->initialize(&b, w, h) != 0) { printf("INIT FAILED\n"); return; }
    int rc = b.ops->load(&b, url);
    printf("LOAD rc=%d\n", rc);
    if (rc != 0) return;
    dump_state("after load", &b);

    printf("--- draw 1 ---\n");
    b.ops->draw(&b);

    CwvRenderer *r = (CwvRenderer *)b.userdata;
    if (scenario >= 1) {
        /* Press + release on whatever is at a few fixed points. */
        int px[] = { 40, 200, 360, 500 };
        int py[] = { 30, 120, 250, 400 };
        for (int k = 0; k < 4; k++) {
            printf("--- pointer (%d,%d) down ---\n", px[k], py[k]);
            b.ops->pointer_event(&b, px[k], py[k], 1);
            printf("--- pointer (%d,%d) up ---\n", px[k], py[k]);
            b.ops->pointer_event(&b, px[k], py[k], 0);
        }
        dump_state("after taps", &b);
        printf("--- draw 2 ---\n");
        b.ops->draw(&b);
    }
    if (scenario >= 2) {
        for (int k = 0; k < 5; k++) {
            printf("--- focus next ---\n");
            b.ops->focus_cycle(&b, 1);
            dump_state("focus", &b);
            b.ops->draw(&b);
            if (k == 2) {
                printf("--- focus activate ---\n");
                b.ops->focus_activate(&b);
                dump_state("activated", &b);
                b.ops->draw(&b);
            }
        }
        for (int k = 0; k < 2; k++) {
            printf("--- focus prev ---\n");
            b.ops->focus_cycle(&b, -1);
            b.ops->draw(&b);
        }
    }
    if (scenario >= 3) {
        printf("--- scroll by 40 x3 ---\n");
        for (int k = 0; k < 3; k++) { b.ops->scroll_by(&b, 40.0f); b.ops->draw(&b); }
        printf("--- scroll to 0 ---\n");
        b.ops->scroll_to(&b, 0.0f);
        printf("--- scroll_x_by 15 ---\n");
        b.ops->scroll_x_by(&b, 15.0f);
        b.ops->draw(&b);
        printf("--- vcm activate cross ---\n");
        b.ops->vcm_activate(&b, "cross");
        b.ops->draw(&b);
        printf("--- update (ime) ---\n");
        b.ops->update(&b);
        dump_state("final", &b);
    }
    (void)r;
    b.ops->shutdown(&b);
}

int main(void) {
    stub_set_log(stdout);
    run_page("basic.html", 960, 544, 0);
    run_page("styled.html", 960, 544, 1);
    run_page("controls.html", 960, 544, 2);
    run_page("long.html", 960, 544, 3);
    run_page("edge.html", 480, 272, 1);
    run_page("layout_test.html", 960, 544, 0);
    run_page("img_test.html", 960, 544, 2);
    run_page("img_test.html#frag", 960, 544, 2);
    run_page("sub/img_climb.html", 960, 544, 0);
    run_page("scroll_region.html", 960, 544, 3);
    run_page("svg_edge.html", 960, 544, 0);
    run_page("svg_hostile.html", 960, 544, 0);
    run_page("comments_test.html", 960, 544, 0);
    run_page("lists_test.html", 960, 544, 0);
    run_page("bgimage_test.html", 960, 544, 0);
    printf("\n##### DONE #####\n");
    return 0;
}
