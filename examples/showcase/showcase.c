#include "showcase.h"

static gr_layer *card(showcase_scene *d, size_t col, size_t row)
{
    gr_layer *c = pill(d->ex, gr_scene_get_root(d->ex->scene),
                       EX_RECT(24 + col * (CARD_WIDTH + 24), 24 + row * (CARD_HEIGHT + 24), CARD_WIDTH, CARD_HEIGHT),
                       EX_WHITE, 20);
    EX_OK(gr_layer_set_masks_to_bounds(c, true));
    border(c, 1, alpha(EX_BLACK, 0.06f));
    drop_shadow(c, alpha(EX_BLACK, 0.16f), 0, 6, 10, NULL);
    return c;
}

static void *build(example *ex)
{
    showcase_scene *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->ex = ex;
    gr_layer *backdrop = new_layer(d->ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_frame(backdrop, EX_RECT(0, 0, 960, 640)));
    gr_gradient_stop stops[] = {{0, hex(0xEEF1F8)}, {1, hex(0xF6EEF7)}};
    set_gradient(backdrop, gradient(false, EX_POINT(0, 0), EX_POINT(1, 1), 0, (gr_gradient_stop_list){stops, 2}));
    EX_OK(gr_layer_add_child(gr_scene_get_root(d->ex->scene), backdrop));
    clock_build(d, card(d, 0, 0));
    drawing_build(d, card(d, 1, 0));
    shapes_build(d, card(d, 2, 0));
    masks_build(d, card(d, 0, 1));
    clipping_build(d, card(d, 1, 1));
    compositing_build(d, card(d, 2, 1));
    return d;
}

static void update(void *state, double t)
{
    showcase_scene *d = state;
    d->time = t;
    clock_update(d, t);
    EX_OK(gr_content_layer_set_needs_display(d->drawing));
    shapes_update(d, t);
    masks_update(d, t);
    clipping_update(d, t);
    compositing_update(d, t);
}

static void free_state(void *state)
{
    showcase_scene *d = state;
    gr_image_unref(d->drawing_picture);
    free(d);
}

int main(int argc, char **argv)
{
    static const example_desc desc = {"showcase", {960, 640}, build, update, free_state, NULL, NULL};
    return example_main(argc, argv, &desc);
}
