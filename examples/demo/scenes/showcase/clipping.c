#include "demo.h"

void clipping_build(demo *d, gr_layer *card)
{
    d->phone = pill(d, card, D_RECT(20, 20, 124, 244), hex(0xF2F2F7), 22);
    border(d->phone, 3, hex(D_INK));
    D_OK(gr_layer_set_masks_to_bounds(d->phone, true));
    uint32_t colors[] = {D_BLUE, D_GREEN, D_ORANGE, D_PURPLE, D_TEAL, D_PINK, D_YELLOW, D_INDIGO};
    for (size_t i = 0; i < 12; ++i)
    {
        gr_layer *row = pill(d, d->phone, D_RECT(10, 12 + (double)i * 60, 104, 50), D_WHITE, 12);
        demo_shadow(row, alpha(D_BLACK, 0.08f), 0, 1, 1.5, NULL);
        pill(d, row, D_RECT(8, 10, 30, 30), hex(colors[i % 8]), 15);
        pill(d, row, D_RECT(46, 14, 50, 8), hex(0xD1D1D6), 4);
        pill(d, row, D_RECT(46, 28, 32 + (double)(i * 7 % 18), 8), hex(0xE5E5EA), 4);
    }
    d->indicator = pill(d, card, D_RECT(150, 20, 4, 48), hex(D_INK), 2);
    D_OK(gr_layer_set_opacity(d->indicator, 0.35f));
    d->morph = new_layer(d, GR_LAYER_GRADIENT);
    D_OK(gr_layer_set_frame(d->morph, D_RECT(176, 22, 88, 88)));
    gr_gradient_stop stops[] = {{0, hex(D_TEAL)}, {1, hex(D_BLUE)}};
    set_gradient(d->morph, gradient(false, D_POINT(0, 0), D_POINT(1, 1), 0, (gr_gradient_stop_list){stops, 2}));
    D_OK(gr_layer_set_masks_to_bounds(d->morph, true));
    D_OK(gr_layer_add_child(card, d->morph));
    d->leaf = pill(d, card, D_RECT(176, 126, 88, 56), hex(D_GREEN), 28);
    gr_layer *frame = pill(d, card, D_RECT(166, 200, 108, 64), hex(0xF2F2F7), 18);
    D_OK(gr_layer_set_masks_to_bounds(frame, true));
    border(frame, 2, alpha(hex(D_INK), 0.2f));
    gr_layer *tiles = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(tiles, D_RECT(0, 0, 108, 64)));
    gr_image *image = checker(0xF2F2F7, 0xE1E3EC);
    set_image(tiles, image);
    gr_image_unref(image);
    D_OK(gr_content_layer_set_gravity(tiles, GR_GRAVITY_ASPECT_FILL));
    D_OK(gr_layer_add_child(frame, tiles));
    d->bar = pill(d, frame, D_RECT(-26, 24, 160, 16), hex(D_ORANGE), 8);
}

void clipping_update(demo *d, double t)
{
    double progress = smooth(wave(t, 8, 0));
    D_OK(gr_layer_set_bounds(d->phone, D_RECT(0, progress * (24 + 12 * 60 - 10 - 244), 124, 244)));
    D_OK(gr_layer_set_position(d->indicator, D_POINT(152, 44 + progress * (244 - 48))));
    double m = smooth(wave(t, 4, 0));
    D_OK(gr_layer_set_corner_radius(d->morph, 44 * m));
    border(d->morph, 6 * m, alpha(D_WHITE, 0.9f));
    D_OK(gr_layer_set_transform(d->morph, rotation(m * D_PI / 4)));
    gr_corners steps[] = {GR_CORNER_TOP_LEFT | GR_CORNER_BOTTOM_RIGHT, GR_CORNER_TOP_RIGHT | GR_CORNER_BOTTOM_LEFT,
                          GR_CORNER_TOP_LEFT | GR_CORNER_TOP_RIGHT, GR_CORNERS_ALL};
    D_OK(gr_layer_set_masked_corners(d->leaf, steps[(size_t)(t / 1.5) % 4]));
    D_OK(gr_layer_set_transform(d->bar, rotation(t * 1.2)));
}
