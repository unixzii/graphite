#include "showcase.h"

static gr_path *blob_path(double t)
{
    gr_path_builder *b = gr_path_builder_new();
    for (size_t i = 0; i < 72; ++i)
    {
        double a = (double)i * EX_TAU / 72, r = 27 + 5 * sin(5 * a + 2 * t) + 3 * sin(3 * a - 3 * t);
        gr_point p = {40 + r * cos(a), 40 + r * sin(a)};
        if (i == 0)
            move_to(b, p);
        else
            line_to(b, p);
    }
    close_path(b);
    return finish_path(b);
}

static gr_path *compound_path(double t)
{
    gr_path_builder *b = gr_path_builder_new();
    EX_OK(gr_path_builder_add_rect(b, EX_RECT(14, 14, 52, 52)));
    double dx = 14 * sin(t * 1.4);
    EX_OK(gr_path_builder_add_ellipse(b, EX_RECT(4 + dx, 4, 40, 40)));
    EX_OK(gr_path_builder_add_ellipse(b, EX_RECT(36 - dx, 36, 40, 40)));
    return finish_path(b);
}

void shapes_build(showcase_scene *d, gr_layer *card)
{
    double radii[] = {78, 58, 38};
    uint32_t colors[] = {EX_PINK, EX_GREEN, EX_TEAL};
    for (size_t i = 0; i < 3; ++i)
    {
        double r = radii[i];
        gr_path *path = circle(EX_POINT(r + 8, r + 8), r);
        for (size_t j = 0; j < 2; ++j)
        {
            gr_layer *l = shape(d->ex, card, EX_RECT(CARD_WIDTH / 2 - r - 8, 100 - r - 8, 2 * r + 16, 2 * r + 16));
            set_path(l, gr_path_ref(path));
            shape_stroke(l, stroke(paint(alpha(hex(colors[i]), j ? 1 : 0.15f)), 16, GR_CAP_ROUND, GR_JOIN_MITER,
                                   (gr_double_list){0}, 0));
            EX_OK(gr_layer_set_transform(l, rotation(-EX_PI / 2)));
            if (j)
                d->rings[i] = l;
        }
        gr_path_unref(path);
    }
    d->blob = shape(d->ex, card, EX_RECT(16, 196, 80, 80));
    fill(d->blob, hex(EX_INDIGO));
    d->star = shape(d->ex, card, EX_RECT(104, 196, 80, 80));
    gr_path *path = star_path(EX_POINT(40, 40), 36, 15, 5, 0);
    set_path(d->star, gr_path_ref(path));
    fill(d->star, hex(EX_YELLOW));
    shape_stroke(d->star, stroke(paint(hex(EX_ORANGE)), 2, GR_CAP_BUTT, GR_JOIN_ROUND, (gr_double_list){0}, 0));
    drop_shadow(d->star, alpha(EX_BLACK, 0.35f), 0, 5, 3, path);
    gr_path_unref(path);
    d->compound = shape(d->ex, card, EX_RECT(192, 196, 80, 80));
    fill(d->compound, hex(EX_PURPLE));
    EX_OK(gr_shape_layer_set_fill_rule(d->compound, GR_FILL_EVEN_ODD));
}

void shapes_update(showcase_scene *d, double t)
{
    for (size_t i = 0; i < 3; ++i)
        EX_OK(
            gr_shape_layer_set_stroke_end(d->rings[i], 0.08 + 0.88 * smooth(wave(t, 3 + (double)i, (double)i * 0.2))));
    set_path(d->blob, blob_path(t));
    double dash[] = {6, 5};
    shape_stroke(d->blob, stroke(paint(alpha(hex(EX_INK), 0.8f)), 2, GR_CAP_BUTT, GR_JOIN_MITER,
                                 (gr_double_list){dash, 2}, t * 22));
    EX_OK(gr_layer_set_transform(d->star, rotation(t * 0.8)));
    set_path(d->compound, compound_path(t));
}
