#include "showcase.h"

#define PHOTOS 5

void compositing_build(showcase_scene *d, gr_layer *card)
{
    d->carousel = layer(d->ex, card, EX_RECT(0, 0, CARD_WIDTH, CARD_HEIGHT));
    gr_image *picture = landscape(120, 80, 2);
    gr_gravity gravities[] = {GR_GRAVITY_ASPECT_FILL, GR_GRAVITY_ASPECT, GR_GRAVITY_RESIZE, GR_GRAVITY_CENTER};
    for (size_t i = 0; i < PHOTOS; ++i)
    {
        gr_layer *p = new_layer(d->ex, GR_LAYER_CONTENT);
        d->photos[i] = p;
        EX_OK(gr_layer_set_bounds(p, EX_RECT(0, 0, 76, 100)));
        background(p, hex(0xE5E5EA));
        set_image(p, picture);
        EX_OK(gr_content_layer_set_gravity(p, gravities[i % 4]));
        EX_OK(gr_layer_set_corner_radius(p, 12));
        EX_OK(gr_layer_set_masks_to_bounds(p, true));
        border(p, 3, EX_WHITE);
        drop_shadow(p, alpha(EX_BLACK, 0.3f), 0, 4, 5, NULL);
        EX_OK(gr_layer_add_child(d->carousel, p));
    }
    gr_image_unref(picture);
    gr_layer *left = layer(d->ex, card, EX_RECT(16, 196, 124, 76)),
             *right = layer(d->ex, card, EX_RECT(148, 196, 124, 76));
    EX_OK(gr_layer_set_opacity(left, 0.5f));
    uint32_t colors[] = {EX_PINK, EX_BLUE};
    for (size_t i = 0; i < 2; ++i)
    {
        double x = 14 + (double)i * 40;
        d->group[i] = pill(d->ex, left, EX_RECT(x, 6, 64, 64), hex(colors[i]), 32);
        d->single[i] = pill(d->ex, right, EX_RECT(x, 6, 64, 64), hex(colors[i]), 32);
        EX_OK(gr_layer_set_opacity(d->single[i], 0.5f));
    }
    d->dot = pill(d->ex, card, EX_RECT(CARD_WIDTH - 28, 14, 12, 12), hex(EX_PINK), 6);
}

/* A looping carousel: every 2.4 s it pages one photo to the left. The centered photo is upright, sharp and slightly
 * enlarged; its neighbors shrink, tilt outward, blur and fade towards the edges. Photos sit 100 points apart, wider
 * than any photo's tilted extent, so they never overlap and their stacking order never shows. */
static void carousel_update(showcase_scene *d, double t)
{
    double page = t / 2.4, step = floor(page), progress = step + smooth(fmin(1, (page - step) / 0.3));
    double offsets[PHOTOS];
    for (size_t i = 0; i < PHOTOS; ++i)
    {
        double s = fmod((double)i - progress, PHOTOS);
        if (s < -PHOTOS / 2.0)
            s += PHOTOS;
        else if (s >= PHOTOS / 2.0)
            s -= PHOTOS;
        offsets[i] = s;
        double near = fmin(fabs(s), 1), far = fmax(fabs(s) - 1, 0), side = s < 0 ? -1 : 1;
        gr_affine m = rotation(side * near * 0.22);
        double k = 1.12 - 0.3 * near - 0.12 * far;
        m = (gr_affine){m.a * k, m.b * k, m.c * k, m.d * k, 0, 0};
        EX_OK(gr_layer_set_position(d->photos[i], EX_POINT(CARD_WIDTH / 2 + 100 * s, 98)));
        EX_OK(gr_layer_set_transform(d->photos[i], m));
        EX_OK(gr_layer_set_blur(d->photos[i], 3.5 * near + 2 * far));
        EX_OK(gr_layer_set_opacity(d->photos[i], (float)fmax(0, fmin(1, (1.8 - fabs(s)) / 0.8))));
    }
    /* Photos nearer the center draw on top; reorder only when the order changes. */
    size_t order[PHOTOS];
    for (size_t i = 0; i < PHOTOS; ++i)
        order[i] = i;
    for (size_t i = 1; i < PHOTOS; ++i)
        for (size_t j = i; j > 0 && fabs(offsets[order[j]]) > fabs(offsets[order[j - 1]]); --j)
        {
            size_t swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    if (memcmp(order, d->photo_order, sizeof(order)))
    {
        memcpy(d->photo_order, order, sizeof(order));
        for (size_t i = 0; i < PHOTOS; ++i)
            EX_OK(gr_layer_add_child(d->carousel, d->photos[order[i]]));
    }
}

void compositing_update(showcase_scene *d, double t)
{
    carousel_update(d, t);
    double slide = 12 * sin(t * 1.6);
    for (size_t i = 0; i < 2; ++i)
    {
        gr_layer **layers = i ? d->single : d->group;
        EX_OK(gr_layer_set_position(layers[0], EX_POINT(46 + slide, 38)));
        EX_OK(gr_layer_set_position(layers[1], EX_POINT(86 - slide, 38)));
    }
    EX_OK(gr_layer_set_hidden(d->dot, (size_t)(t * 2) % 2 == 1));
}
