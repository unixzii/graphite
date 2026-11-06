#include "demo.h"

void compositing_build(demo *d, gr_layer *card)
{
    d->fan = layer(d, card, D_RECT(0, 0, CARD_WIDTH, CARD_HEIGHT));
    gr_image *picture = landscape(120, 80, 2);
    gr_gravity gravities[] = {GR_GRAVITY_ASPECT_FILL, GR_GRAVITY_ASPECT, GR_GRAVITY_RESIZE, GR_GRAVITY_CENTER};
    for (size_t i = 0; i < 4; ++i)
    {
        gr_layer *p = new_layer(d, GR_LAYER_CONTENT);
        d->photos[i] = p;
        D_OK(gr_layer_set_bounds(p, D_RECT(0, 0, 84, 112)));
        D_OK(gr_layer_set_anchor_point(p, D_POINT(0.5, 1)));
        D_OK(gr_layer_set_position(p, D_POINT(144, 176)));
        background(p, hex(0xE5E5EA));
        set_image(p, picture);
        D_OK(gr_content_layer_set_gravity(p, gravities[i]));
        D_OK(gr_layer_set_corner_radius(p, 12));
        D_OK(gr_layer_set_masks_to_bounds(p, true));
        border(p, 3, D_WHITE);
        demo_shadow(p, alpha(D_BLACK, 0.3f), 0, 4, 5, NULL);
        D_OK(gr_layer_add_child(d->fan, p));
    }
    gr_image_unref(picture);
    gr_layer *left = layer(d, card, D_RECT(16, 196, 124, 76)), *right = layer(d, card, D_RECT(148, 196, 124, 76));
    D_OK(gr_layer_set_opacity(left, 0.5f));
    uint32_t colors[] = {D_PINK, D_BLUE};
    for (size_t i = 0; i < 2; ++i)
    {
        double x = 14 + (double)i * 40;
        d->group[i] = pill(d, left, D_RECT(x, 6, 64, 64), hex(colors[i]), 32);
        d->single[i] = pill(d, right, D_RECT(x, 6, 64, 64), hex(colors[i]), 32);
        D_OK(gr_layer_set_opacity(d->single[i], 0.5f));
    }
    d->dot = pill(d, card, D_RECT(CARD_WIDTH - 28, 14, 12, 12), hex(D_PINK), 6);
}

void compositing_update(demo *d, double t)
{
    double spread = 0.1 + 0.24 * smooth(wave(t, 5, 0));
    for (size_t i = 0; i < 4; ++i)
        D_OK(gr_layer_set_transform(d->photos[i], rotation(((double)i - 1.5) * spread)));
    size_t front = (size_t)(t / 1.2) % 4;
    if (front != d->front)
    {
        d->front = front;
        D_OK(gr_layer_add_child(d->fan, d->photos[front]));
    }
    double slide = 12 * sin(t * 1.6);
    for (size_t i = 0; i < 2; ++i)
    {
        gr_layer **layers = i ? d->single : d->group;
        D_OK(gr_layer_set_position(layers[0], D_POINT(46 + slide, 38)));
        D_OK(gr_layer_set_position(layers[1], D_POINT(86 - slide, 38)));
    }
    D_OK(gr_layer_set_hidden(d->dot, (size_t)(t * 2) % 2 == 1));
}
