#include "internal.h"

static void measure_shadow_blur(ctx *c, bool background)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTAINER);
    API(gr_layer_set_frame(l, RECT(10, 10, 40, 80)));
    if (background)
    {
        gr_color white = color(WHITE);
        API(gr_layer_set_background(l, &white));
    }
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    set_shadow(l, 0, 0, 4, 0);
    frame *f = render(c, s);
    profile p = frame_profile(f, POINT(50, 50), POINT(95, 50));
    expect_near(c, "distance to 15.87% alpha (= σ)", profile_crossing(p, 3, 0.1587), 4, 0.8);
    expect_near(c, "distance to 2.28% alpha (= 2σ), halved", profile_crossing(p, 3, 0.0228) / 2, 4, 0.8);
    free(p.data);
}

void shadow_blur_matches_sigma(ctx *c)
{
    measure_shadow_blur(c, true);
}

void shadow_blur_matches_sigma_without_background(ctx *c)
{
    measure_shadow_blur(c, false);
}

void shadow_spread_moves_the_edge(ctx *c)
{
    for (size_t i = 0; i < 2; ++i)
    {
        gr_scene *s = new_scene(c);
        gr_layer *l = new_layer(c, s, GR_LAYER_CONTAINER);
        API(gr_layer_set_frame(l, RECT(10, 10, 40, 80)));
        if (i == 0)
        {
            gr_color white = color(WHITE);
            API(gr_layer_set_background(l, &white));
        }
        set_shadow(l, 0, 0, 4, 6);
        API(gr_layer_add_child(gr_scene_get_root(s), l));
        frame *f = render(c, s);
        profile p = frame_profile(f, POINT(50, 50), POINT(95, 50));
        char label[100];
        snprintf(label, sizeof(label), "%s: distance to 50%% alpha (= spread)", i == 0 ? "opaque" : "hollow");
        expect_near(c, label, profile_crossing(p, 3, 0.5), 6, 0.6);
        free(p.data);
    }
}

void layer_blur_matches_sigma(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(10, 10, 40, 80), WHITE);
    API(gr_layer_set_blur(l, 4));
    frame *f = render(c, s);
    profile p = frame_profile(f, POINT(50, 50), POINT(95, 50));
    expect_near(c, "distance to 15.87% alpha (= σ)", profile_crossing(p, 3, 0.1587), 4, 0.8);
    expect_near(c, "distance to 2.28% alpha (= 2σ), halved", profile_crossing(p, 3, 0.0228) / 2, 4, 0.8);
    free(p.data);
}

void linear_gradient_interpolates_monotonically(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_GRADIENT);
    API(gr_layer_set_frame(l, RECT(10, 40, 80, 20)));
    set_gradient(l, false, POINT(0, 0.5), POINT(1, 0.5), 0);
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    frame *f = render(c, s);
    profile p = frame_profile(f, POINT(10, 50), POINT(90, 50));
    bool red_falls = true, blue_rises = true;
    for (size_t i = 1; i < p.count; ++i)
    {
        red_falls &= p.data[i].channels[0] <= p.data[i - 1].channels[0] + 0.02;
        blue_rises &= p.data[i].channels[2] + 0.02 >= p.data[i - 1].channels[2];
    }
    conf_check(c, red_falls && blue_rises, "red should fall and blue rise monotonically");
    expect_near(c, "red at the start", p.count ? p.data[0].channels[0] : NAN, 1, 0.05);
    expect_near(c, "blue at the end", p.count ? p.data[p.count - 1].channels[2] : NAN, 1, 0.05);
    rgba8 mid;
    bool found = frame_at(f, POINT(50, 50), &mid);
    expect_near(c, "red at the midpoint", found ? mid.v[0] / 255.0 : NAN, 0.5, 0.25);
    expect_near(c, "blue at the midpoint", found ? mid.v[2] / 255.0 : NAN, 0.5, 0.25);
    free(p.data);
}

void stroke_width_matches(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_SHAPE);
    API(gr_layer_set_frame(l, RECT(0, 0, 100, 100)));
    gr_path *path = line_path(POINT(10, 50.3), POINT(90, 50.3));
    API(gr_shape_layer_set_path(l, path));
    gr_path_unref(path);
    set_stroke(l, RED, 6);
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    frame *f = render(c, s);
    profile p = frame_profile(f, POINT(50, 40), POINT(50, 60));
    expect_near(c, "stroke coverage", profile_coverage(p, 3, f->scale), 6, 0.5);
    free(p.data);
}

void corner_radius_follows_a_circular_arc(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 60, 60), RED);
    API(gr_layer_set_corner_radius(l, 20));
    frame *f = render(c, s);
    profile p = frame_profile(f, POINT(0, 0), POINT(20, 20));
    expect_near(c, "50% coverage along the diagonal", profile_crossing(p, 3, 0.5), 20 * (sqrt(2) - 1), 0.75);
    free(p.data);
}

void group_opacity_halves_alpha(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 40, 40), RED);
    API(gr_layer_set_opacity(l, 0.5f));
    frame *f = render(c, s);
    rgba8 pixel;
    expect_near(c, "alpha", frame_at(f, POINT(20, 20), &pixel) ? pixel.v[3] / 255.0 : NAN, 0.5, 0.02);
}
