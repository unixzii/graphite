#include "demo.h"

static gr_stroke *round_stroke(gr_paint *p, double width)
{
    return stroke(p, width, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){0}, 0);
}

static void draw(void *userdata, gr_canvas *cx, const gr_draw_info *info)
{
    (void)info;
    demo *d = userdata;
    double t = d->time, w = CARD_WIDTH, h = CARD_HEIGHT;
    double dash_grid[] = {2, 4};
    gr_path_builder *b = gr_path_builder_new();
    for (size_t i = 1; i < (size_t)(w / 24); ++i)
    {
        move_to(b, D_POINT((double)i * 24, 0));
        line_to(b, D_POINT((double)i * 24, h));
    }
    for (size_t i = 1; i < (size_t)(h / 24); ++i)
    {
        move_to(b, D_POINT(0, (double)i * 24));
        line_to(b, D_POINT(w, (double)i * 24));
    }
    draw_stroke(cx, finish_path(b),
                stroke(paint(alpha(D_BLACK, 0.08f)), 1, GR_CAP_BUTT, GR_JOIN_MITER, (gr_double_list){dash_grid, 2}, 0));
    for (size_t pass = 0; pass < 2; ++pass)
    {
        double k = pass ? 0.6 : 1;
        float opacity = pass ? 0.35f : 1;
        b = gr_path_builder_new();
        for (size_t i = 0; i <= 64; ++i)
        {
            double x = 16 + (double)i * (w - 32) / 64,
                   y = 64 + 34 * k * sin(x * 0.04 + t * 2.4) * cos(x * 0.011 - t * 0.7);
            if (i == 0)
                move_to(b, D_POINT(x, y));
            else
                line_to(b, D_POINT(x, y));
        }
        gr_gradient_stop stops[] = {{0, hex(D_BLUE)}, {0.5f, hex(D_PURPLE)}, {1, hex(D_PINK)}};
        gr_canvas_save(cx);
        D_OK(gr_canvas_set_alpha(cx, opacity));
        draw_stroke(
            cx, finish_path(b),
            round_stroke(gradient(false, D_POINT(16, 0), D_POINT(w - 16, 0), 0, (gr_gradient_stop_list){stops, 3}),
                         3.5));
        gr_canvas_restore(cx);
    }
    gr_point p0 = {24, 134}, p3 = {w - 24, 134}, c1 = {96, 134 - 42 * sin(t * 1.3)},
             c2 = {w - 96, 134 + 42 * cos(t * 1.1)};
    b = gr_path_builder_new();
    move_to(b, p0);
    curve_to(b, c1, c2, p3);
    double dash[] = {8, 6};
    draw_stroke(cx, finish_path(b),
                stroke(paint(hex(D_ORANGE)), 2.5, GR_CAP_ROUND, GR_JOIN_ROUND, (gr_double_list){dash, 2}, -t * 30));
    b = gr_path_builder_new();
    move_to(b, p0);
    line_to(b, c1);
    move_to(b, p3);
    line_to(b, c2);
    draw_stroke(cx, finish_path(b),
                stroke(paint(alpha(D_BLACK, 0.25f)), 1, GR_CAP_BUTT, GR_JOIN_MITER, (gr_double_list){0}, 0));
    draw_fill(cx, circle(c1, 4), GR_FILL_NONZERO, paint(hex(D_ORANGE)));
    draw_fill(cx, circle(c2, 4), GR_FILL_NONZERO, paint(hex(D_ORANGE)));
    gr_canvas_save(cx);
    D_OK(gr_canvas_concat_transform(cx, translation(D_POINT(76, 218))));
    D_OK(gr_canvas_concat_transform(cx, rotation(t * 0.6)));
    gr_path *star = star_polygon(D_POINT(0, 0), 50, 5, 2, 0);
    gr_gradient_stop stops[] = {{0, hex(D_YELLOW)}, {1, hex(D_ORANGE)}};
    draw_fill(cx, gr_path_ref(star), GR_FILL_EVEN_ODD,
              gradient(true, D_POINT(0, 0), D_POINT(0, 0), 50, (gr_gradient_stop_list){stops, 2}));
    draw_stroke(cx, star,
                stroke(paint(alpha(hex(D_INK), 0.7f)), 1.5, GR_CAP_BUTT, GR_JOIN_MITER, (gr_double_list){0}, 0));
    gr_canvas_restore(cx);
    gr_point disc = {212, 218};
    gr_canvas_save(cx);
    gr_path *clip = circle(disc, 52);
    D_OK(gr_canvas_clip_path(cx, clip, GR_FILL_NONZERO));
    gr_path_unref(clip);
    D_OK(gr_canvas_concat_transform(cx, translation(disc)));
    gr_canvas_save(cx);
    D_OK(gr_canvas_concat_transform(cx, rotation(D_PI / 4)));
    double slide = fmod(t * 24, 24);
    for (int i = -6; i < 6; ++i)
        draw_rect(cx, D_RECT((double)i * 12 + slide - 6, -80, 12, 160), hex(i % 2 == 0 ? D_TEAL : D_INDIGO));
    gr_canvas_restore(cx);
    D_OK(gr_canvas_set_alpha(cx, 0.85f));
    D_OK(gr_canvas_concat_transform(cx, rotation(0.25 * sin(t * 1.5))));
    D_OK(gr_canvas_draw_image(cx, d->drawing_picture, D_RECT(-34, -24, 68, 48)));
    gr_canvas_restore(cx);
    draw_stroke(cx, circle(disc, 52),
                stroke(paint(alpha(hex(D_INK), 0.7f)), 2, GR_CAP_BUTT, GR_JOIN_MITER, (gr_double_list){0}, 0));
    for (size_t i = 0; i < 12; ++i)
    {
        double a = (double)i * D_TAU / 12;
        float fade = (float)(0.5 + 0.5 * cos(a - t * 3));
        gr_canvas_save(cx);
        D_OK(gr_canvas_set_alpha(cx, 0.15f + 0.85f * fade));
        draw_fill(cx, circle(D_POINT(144 + 16 * cos(a), 218 + 16 * sin(a)), 3), GR_FILL_NONZERO, paint(hex(D_INK)));
        gr_canvas_restore(cx);
    }
}

void drawing_build(demo *d, gr_layer *card)
{
    d->drawing = new_layer(d, GR_LAYER_CONTENT);
    D_OK(gr_layer_set_frame(d->drawing, D_RECT(0, 0, CARD_WIDTH, CARD_HEIGHT)));
    d->drawing_picture = landscape(68, 48, 2);
    gr_drawer drawer = {d, draw, NULL};
    D_OK(gr_content_layer_set_drawer(d->drawing, &drawer));
    D_OK(gr_layer_add_child(card, d->drawing));
}
