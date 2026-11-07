#ifndef GRAPHITE_SHOWCASE_H
#define GRAPHITE_SHOWCASE_H

#include "example.h"

#define CARD_WIDTH 288.0
#define CARD_HEIGHT 284.0

typedef struct showcase_scene showcase_scene;

struct showcase_scene
{
    example *ex;
    double time;
    gr_layer *hour, *minute, *second, *orbit, *planets[6], *moon_orbit;
    gr_layer *drawing, *rings[3], *blob, *star, *compound;
    gr_layer *spotlight, *badge, *badge_mask;
    gr_layer *phone, *indicator, *morph, *leaf, *bar;
    gr_layer *carousel, *photos[5], *group[2], *single[2], *dot;
    size_t photo_order[5]; /**< Back to front. */
    gr_image *drawing_picture;
};

gr_image *landscape(uint32_t width, uint32_t height, double scale);
gr_image *checker(uint32_t a, uint32_t b);

/* One card each; layers are borrowed from d->ex. */
void clock_build(showcase_scene *d, gr_layer *card);
void clock_update(showcase_scene *d, double t);
void drawing_build(showcase_scene *d, gr_layer *card);
void shapes_build(showcase_scene *d, gr_layer *card);
void shapes_update(showcase_scene *d, double t);
void masks_build(showcase_scene *d, gr_layer *card);
void masks_update(showcase_scene *d, double t);
void clipping_build(showcase_scene *d, gr_layer *card);
void clipping_update(showcase_scene *d, double t);
void compositing_build(showcase_scene *d, gr_layer *card);
void compositing_update(showcase_scene *d, double t);

#endif
