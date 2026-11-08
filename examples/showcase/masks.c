#include "showcase.h"

#define SPOT 120.0

static void set_mask(gr_layer *l, gr_mask mask)
{
    EX_OK(gr_layer_set_mask(l, &mask));
}

void masks_build(showcase_scene *d, gr_layer *card)
{
    gr_rect full = EX_RECT(0, 0, CARD_WIDTH, CARD_HEIGHT);
    gr_image *picture = landscape((uint32_t)CARD_WIDTH, (uint32_t)CARD_HEIGHT, 2);
    background(layer(d->ex, card, full), hex(0x0B0D2A));
    gr_layer *dim = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(dim, full));
    set_image(dim, picture);
    EX_OK(gr_layer_set_opacity(dim, 0.3f));
    EX_OK(gr_layer_add_child(card, dim));
    /* A gradient-masked window that moves over the picture; the picture inside moves back to stay in place. */
    d->spotlight = layer(d->ex, card, EX_RECT(0, 0, SPOT, SPOT));
    d->spot_picture = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(d->spot_picture, full));
    set_image(d->spot_picture, picture);
    gr_image_unref(picture);
    EX_OK(gr_layer_add_child(d->spotlight, d->spot_picture));
    d->badge = new_layer(d->ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_frame(d->badge, EX_RECT(CARD_WIDTH - 92, 14, 78, 78)));
    EX_OK(gr_layer_add_child(card, d->badge));
}

void masks_update(showcase_scene *d, double t)
{
    gr_point center = {CARD_WIDTH / 2 + 90 * sin(t * 0.9), CARD_HEIGHT / 2 + 70 * sin(t * 1.3 + 0.5)};
    EX_OK(gr_layer_set_position(d->spotlight, center));
    EX_OK(gr_layer_set_position(d->spot_picture,
                                EX_POINT(CARD_WIDTH / 2 - center.x + SPOT / 2, CARD_HEIGHT / 2 - center.y + SPOT / 2)));
    gr_gradient_stop light[] = {{0, EX_WHITE}, {0.6f, EX_WHITE}, {1, alpha(EX_WHITE, 0)}};
    gr_paint *spot = gradient(true, EX_POINT(0.5, 0.5), EX_POINT(0, 0), 0.5 * (0.75 + 0.25 * wave(t, 2.5, 0)),
                              (gr_gradient_stop_list){light, 3});
    set_mask(d->spotlight, (gr_mask){.kind = GR_MASK_GRADIENT, .value.gradient = spot});
    gr_paint_unref(spot);
    double a = t * 0.9;
    gr_gradient_stop stops[] = {{0, hex(EX_YELLOW)}, {0.5f, hex(EX_PINK)}, {1, hex(EX_PURPLE)}};
    set_gradient(d->badge,
                 gradient(false, EX_POINT(0.5 - 0.5 * cos(a), 0.5 - 0.5 * sin(a)),
                          EX_POINT(0.5 + 0.5 * cos(a), 0.5 + 0.5 * sin(a)), 0, (gr_gradient_stop_list){stops, 3}));
    gr_path *star = star_path(EX_POINT(39, 39), 38, 18, 6, -t * EX_TAU / 10);
    set_mask(d->badge, (gr_mask){.kind = GR_MASK_PATH, .value.path = {star, GR_FILL_NONZERO}});
    gr_path_unref(star);
}
