#include "demo.h"

static gr_layer *card(demo *d, size_t col, size_t row)
{
    gr_layer *c =
        pill(d, gr_scene_get_root(d->scene),
             D_RECT(24 + col * (CARD_WIDTH + 24), 24 + row * (CARD_HEIGHT + 24), CARD_WIDTH, CARD_HEIGHT), D_WHITE, 20);
    D_OK(gr_layer_set_masks_to_bounds(c, true));
    border(c, 1, alpha(D_BLACK, 0.06f));
    demo_shadow(c, alpha(D_BLACK, 0.16f), 0, 6, 10, NULL);
    return c;
}

void showcase_build(demo *d)
{
    gr_layer *backdrop = new_layer(d, GR_LAYER_GRADIENT);
    D_OK(gr_layer_set_frame(backdrop, D_RECT(0, 0, 960, 640)));
    gr_gradient_stop stops[] = {{0, hex(0xEEF1F8)}, {1, hex(0xF6EEF7)}};
    set_gradient(backdrop, gradient(false, D_POINT(0, 0), D_POINT(1, 1), 0, (gr_gradient_stop_list){stops, 2}));
    D_OK(gr_layer_add_child(gr_scene_get_root(d->scene), backdrop));
    clock_build(d, card(d, 0, 0));
    drawing_build(d, card(d, 1, 0));
    shapes_build(d, card(d, 2, 0));
    masks_build(d, card(d, 0, 1));
    clipping_build(d, card(d, 1, 1));
    compositing_build(d, card(d, 2, 1));
}

void showcase_update(demo *d, double t)
{
    clock_update(d, t);
    D_OK(gr_content_layer_set_needs_display(d->drawing));
    shapes_update(d, t);
    masks_update(d, t);
    clipping_update(d, t);
    compositing_update(d, t);
}
