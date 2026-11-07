#include "showcase.h"

void clipping_build(showcase_scene *d, gr_layer *card)
{
    d->phone = pill(d->ex, card, EX_RECT(20, 20, 124, 244), hex(0xF2F2F7), 22);
    border(d->phone, 3, hex(EX_INK));
    EX_OK(gr_layer_set_masks_to_bounds(d->phone, true));
    uint32_t colors[] = {EX_BLUE, EX_GREEN, EX_ORANGE, EX_PURPLE, EX_TEAL, EX_PINK, EX_YELLOW, EX_INDIGO};
    for (size_t i = 0; i < 12; ++i)
    {
        gr_layer *row = pill(d->ex, d->phone, EX_RECT(10, 12 + (double)i * 60, 104, 50), EX_WHITE, 12);
        drop_shadow(row, alpha(EX_BLACK, 0.08f), 0, 1, 1.5, NULL);
        pill(d->ex, row, EX_RECT(8, 10, 30, 30), hex(colors[i % 8]), 15);
        pill(d->ex, row, EX_RECT(46, 14, 50, 8), hex(0xD1D1D6), 4);
        pill(d->ex, row, EX_RECT(46, 28, 32 + (double)(i * 7 % 18), 8), hex(0xE5E5EA), 4);
    }
    d->indicator = pill(d->ex, card, EX_RECT(150, 20, 4, 48), hex(EX_INK), 2);
    EX_OK(gr_layer_set_opacity(d->indicator, 0.35f));
    d->morph = new_layer(d->ex, GR_LAYER_GRADIENT);
    EX_OK(gr_layer_set_frame(d->morph, EX_RECT(176, 22, 88, 88)));
    gr_gradient_stop stops[] = {{0, hex(EX_TEAL)}, {1, hex(EX_BLUE)}};
    set_gradient(d->morph, gradient(false, EX_POINT(0, 0), EX_POINT(1, 1), 0, (gr_gradient_stop_list){stops, 2}));
    EX_OK(gr_layer_set_masks_to_bounds(d->morph, true));
    EX_OK(gr_layer_add_child(card, d->morph));
    d->leaf = pill(d->ex, card, EX_RECT(176, 126, 88, 56), hex(EX_GREEN), 28);
    gr_layer *frame = pill(d->ex, card, EX_RECT(166, 200, 108, 64), hex(0xF2F2F7), 18);
    EX_OK(gr_layer_set_masks_to_bounds(frame, true));
    border(frame, 2, alpha(hex(EX_INK), 0.2f));
    gr_layer *tiles = new_layer(d->ex, GR_LAYER_CONTENT);
    EX_OK(gr_layer_set_frame(tiles, EX_RECT(0, 0, 108, 64)));
    gr_image *image = checker(0xF2F2F7, 0xE1E3EC);
    set_image(tiles, image);
    gr_image_unref(image);
    EX_OK(gr_content_layer_set_gravity(tiles, GR_GRAVITY_ASPECT_FILL));
    EX_OK(gr_layer_add_child(frame, tiles));
    d->bar = pill(d->ex, frame, EX_RECT(-26, 24, 160, 16), hex(EX_ORANGE), 8);
}

void clipping_update(showcase_scene *d, double t)
{
    double progress = smooth(wave(t, 8, 0));
    EX_OK(gr_layer_set_bounds(d->phone, EX_RECT(0, progress * (24 + 12 * 60 - 10 - 244), 124, 244)));
    EX_OK(gr_layer_set_position(d->indicator, EX_POINT(152, 44 + progress * (244 - 48))));
    double m = smooth(wave(t, 4, 0));
    EX_OK(gr_layer_set_corner_radius(d->morph, 44 * m));
    border(d->morph, 6 * m, alpha(EX_WHITE, 0.9f));
    EX_OK(gr_layer_set_transform(d->morph, rotation(m * EX_PI / 4)));
    gr_corners steps[] = {GR_CORNER_TOP_LEFT | GR_CORNER_BOTTOM_RIGHT, GR_CORNER_TOP_RIGHT | GR_CORNER_BOTTOM_LEFT,
                          GR_CORNER_TOP_LEFT | GR_CORNER_TOP_RIGHT, GR_CORNERS_ALL};
    EX_OK(gr_layer_set_masked_corners(d->leaf, steps[(size_t)(t / 1.5) % 4]));
    EX_OK(gr_layer_set_transform(d->bar, rotation(t * 1.2)));
}
