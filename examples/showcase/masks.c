#include "showcase.h"

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
    gr_layer *lit = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(lit, full));
    set_image(lit, picture);
    gr_image_unref(picture);
    d->spotlight = new_layer(d->ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_bounds(d->spotlight, EX_RECT(0, 0, 120, 120)));
    gr_gradient_stop stops[] = {{0, EX_WHITE}, {0.6f, EX_WHITE}, {1, alpha(EX_WHITE, 0)}};
    set_gradient(d->spotlight,
                 gradient(true, EX_POINT(0.5, 0.5), EX_POINT(0, 0), 0.5, (gr_gradient_stop_list){stops, 3}));
    EX_OK(gr_layer_set_mask(lit, d->spotlight));
    EX_OK(gr_layer_add_child(card, lit));
    d->badge = new_layer(d->ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_frame(d->badge, EX_RECT(CARD_WIDTH - 92, 14, 78, 78)));
    d->badge_mask = new_layer(d->ex, GR_LAYER_SHAPE);
    EX_OK(gr_layer_set_frame(d->badge_mask, EX_RECT(0, 0, 78, 78)));
    set_path(d->badge_mask, star_path(EX_POINT(39, 39), 38, 18, 6, 0));
    fill(d->badge_mask, EX_WHITE);
    EX_OK(gr_layer_set_mask(d->badge, d->badge_mask));
    EX_OK(gr_layer_add_child(card, d->badge));
}

void masks_update(showcase_scene *d, double t)
{
    EX_OK(gr_layer_set_position(
        d->spotlight, EX_POINT(CARD_WIDTH / 2 + 90 * sin(t * 0.9), CARD_HEIGHT / 2 + 70 * sin(t * 1.3 + 0.5))));
    EX_OK(gr_layer_set_transform(d->spotlight, scaling(0.85 + 0.25 * wave(t, 2.5, 0))));
    double a = t * 0.9;
    gr_gradient_stop stops[] = {{0, hex(EX_YELLOW)}, {0.5f, hex(EX_PINK)}, {1, hex(EX_PURPLE)}};
    set_gradient(d->badge,
                 gradient(false, EX_POINT(0.5 - 0.5 * cos(a), 0.5 - 0.5 * sin(a)),
                          EX_POINT(0.5 + 0.5 * cos(a), 0.5 + 0.5 * sin(a)), 0, (gr_gradient_stop_list){stops, 3}));
    EX_OK(gr_layer_set_transform(d->badge_mask, rotation(-t * EX_TAU / 10)));
}
