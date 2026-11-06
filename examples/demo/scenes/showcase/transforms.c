#include "demo.h"

static void face_draw(void *userdata, gr_canvas *c, const gr_draw_info *info)
{
    (void)userdata;
    gr_point center = {info->bounds.size.width / 2, info->bounds.size.height / 2};
    gr_gradient_stop stops[] = {{0, D_WHITE}, {1, hex(0xE9EBF2)}};
    draw_fill(c, circle(center, 90), GR_FILL_NONZERO,
              gradient(true, center, D_POINT(0, 0), 90, (gr_gradient_stop_list){stops, 2}));
    draw_stroke(c, circle(center, 89),
                stroke(paint(alpha(D_BLACK, 0.12f)), 2, GR_CAP_BUTT, GR_JOIN_MITER, (gr_double_list){0}, 0));
    for (size_t i = 0; i < 60; ++i)
    {
        gr_canvas_save(c);
        D_OK(gr_canvas_concat_transform(c, translation(center)));
        D_OK(gr_canvas_concat_transform(c, rotation((double)i * D_TAU / 60)));
        bool major = i % 5 == 0;
        gr_path_builder *b = gr_path_builder_new();
        move_to(b, D_POINT(0, -82));
        line_to(b, D_POINT(0, -90 + (major ? 22 : 14)));
        draw_stroke(c, finish_path(b),
                    stroke(paint(alpha(hex(D_INK), major ? 0.9f : 0.35f)), major ? 3 : 1.2, GR_CAP_ROUND, GR_JOIN_MITER,
                           (gr_double_list){0}, 0));
        gr_canvas_restore(c);
    }
}

static gr_layer *hand(demo *d, gr_layer *card, double width, double length, double pivot, gr_color color)
{
    gr_layer *h = new_layer(d, GR_LAYER_CONTAINER);
    D_OK(gr_layer_set_bounds(h, D_RECT(0, 0, width, length)));
    D_OK(gr_layer_set_anchor_point(h, D_POINT(0.5, pivot)));
    D_OK(gr_layer_set_position(h, D_POINT(CARD_WIDTH / 2, CARD_HEIGHT / 2)));
    background(h, color);
    D_OK(gr_layer_set_corner_radius(h, width / 2));
    D_OK(gr_layer_add_child(card, h));
    return h;
}

void clock_build(demo *d, gr_layer *card)
{
    gr_point center = {CARD_WIDTH / 2, CARD_HEIGHT / 2};
    d->orbit = new_layer(d, GR_LAYER_CONTAINER);
    D_OK(gr_layer_set_bounds(d->orbit, D_RECT(-142, -142, 284, 284)));
    D_OK(gr_layer_set_position(d->orbit, center));
    D_OK(gr_layer_add_child(card, d->orbit));
    uint32_t colors[] = {D_BLUE, D_GREEN, D_ORANGE, D_PURPLE, D_TEAL, D_PINK};
    for (size_t i = 0; i < 6; ++i)
    {
        double a = (double)i * D_TAU / 6, size = i == 0 ? 20 : 11;
        gr_point at = {112 * cos(a), 112 * sin(a)};
        d->planets[i] =
            pill(d, d->orbit, D_RECT(at.x - size / 2, at.y - size / 2, size, size), hex(colors[i]), size / 2);
        if (i == 0)
        {
            d->moon_orbit = new_layer(d, GR_LAYER_CONTAINER);
            D_OK(gr_layer_set_bounds(d->moon_orbit, D_RECT(-16, -16, 32, 32)));
            D_OK(gr_layer_set_position(d->moon_orbit, D_POINT(size / 2, size / 2)));
            D_OK(gr_layer_add_child(d->planets[i], d->moon_orbit));
            pill(d, d->moon_orbit, D_RECT(9.5, -3.5, 7, 7), hex(D_INK), 3.5);
        }
    }
    gr_layer *face = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(face, D_RECT(center.x - 90, center.y - 90, 180, 180)));
    gr_drawer drawer = {NULL, face_draw, NULL};
    D_OK(gr_content_layer_set_drawer(face, &drawer));
    D_OK(gr_layer_add_child(card, face));
    d->hour = hand(d, card, 8, 52, 0.85, hex(D_INK));
    d->minute = hand(d, card, 6, 76, 0.88, hex(D_INK));
    d->second = hand(d, card, 2, 92, 0.82, hex(D_PINK));
    pill(d, card, D_RECT(center.x - 6, center.y - 6, 12, 12), hex(D_PINK), 6);
}

void clock_update(demo *d, double t)
{
    D_OK(gr_layer_set_transform(d->second, rotation(t * D_TAU / 8)));
    D_OK(gr_layer_set_transform(d->minute, rotation(1.1 + t * D_TAU / 60)));
    D_OK(gr_layer_set_transform(d->hour, rotation(3.6 + t * D_TAU / 480)));
    D_OK(gr_layer_set_transform(d->orbit, rotation(t * 0.35)));
    D_OK(gr_layer_set_transform(d->moon_orbit, rotation(t * 2.5)));
    for (size_t i = 1; i < 6; ++i)
        D_OK(gr_layer_set_transform(d->planets[i], scaling(1 + 0.35 * sin(3 * t + (double)i))));
}
