#include "demo.h"

void masks_build(demo *d, gr_layer *card)
{
    gr_rect full = D_RECT(0, 0, CARD_WIDTH, CARD_HEIGHT);
    gr_image *picture = landscape((uint32_t)CARD_WIDTH, (uint32_t)CARD_HEIGHT, 2);
    background(layer(d, card, full), hex(0x0B0D2A));
    gr_layer *dim = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(dim, full));
    set_image(dim, picture);
    D_OK(gr_layer_set_opacity(dim, 0.3f));
    D_OK(gr_layer_add_child(card, dim));
    gr_layer *lit = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(lit, full));
    set_image(lit, picture);
    gr_image_unref(picture);
    d->spotlight = new_layer(d, GR_LAYER_GRADIENT);
    D_OK(gr_layer_set_bounds(d->spotlight, D_RECT(0, 0, 120, 120)));
    gr_gradient_stop stops[] = {{0, D_WHITE}, {0.6f, D_WHITE}, {1, alpha(D_WHITE, 0)}};
    set_gradient(d->spotlight,
                 gradient(true, D_POINT(0.5, 0.5), D_POINT(0, 0), 0.5, (gr_gradient_stop_list){stops, 3}));
    D_OK(gr_layer_set_mask(lit, d->spotlight));
    D_OK(gr_layer_add_child(card, lit));
    d->badge = new_layer(d, GR_LAYER_GRADIENT);
    D_OK(gr_layer_set_frame(d->badge, D_RECT(CARD_WIDTH - 92, 14, 78, 78)));
    d->badge_mask = new_layer(d, GR_LAYER_SHAPE);
    D_OK(gr_layer_set_frame(d->badge_mask, D_RECT(0, 0, 78, 78)));
    set_path(d->badge_mask, star_path(D_POINT(39, 39), 38, 18, 6, 0));
    fill(d->badge_mask, D_WHITE);
    D_OK(gr_layer_set_mask(d->badge, d->badge_mask));
    D_OK(gr_layer_add_child(card, d->badge));
}

void masks_update(demo *d, double t)
{
    D_OK(gr_layer_set_position(d->spotlight,
                               D_POINT(CARD_WIDTH / 2 + 90 * sin(t * 0.9), CARD_HEIGHT / 2 + 70 * sin(t * 1.3 + 0.5))));
    D_OK(gr_layer_set_transform(d->spotlight, scaling(0.85 + 0.25 * wave(t, 2.5, 0))));
    double a = t * 0.9;
    gr_gradient_stop stops[] = {{0, hex(D_YELLOW)}, {0.5f, hex(D_PINK)}, {1, hex(D_PURPLE)}};
    set_gradient(d->badge,
                 gradient(false, D_POINT(0.5 - 0.5 * cos(a), 0.5 - 0.5 * sin(a)),
                          D_POINT(0.5 + 0.5 * cos(a), 0.5 + 0.5 * sin(a)), 0, (gr_gradient_stop_list){stops, 3}));
    D_OK(gr_layer_set_transform(d->badge_mask, rotation(-t * D_TAU / 10)));
}
