#include "internal.h"

#define EXPECT(label, x, y, value) expect_color(c, f, label, POINT(x, y), value, 8)
#define BACKGROUND(layer, value)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        gr_color v = color(value);                                                                                     \
        API(gr_layer_set_background(layer, &v));                                                                       \
    } while (0)
#define ADD(parent, child) API(gr_layer_add_child(parent, child))
#define FRAME(layer, x, y, w, h) API(gr_layer_set_frame(layer, RECT(x, y, w, h)))

void geometry_is_y_down(ctx *c)
{
    gr_scene *s = new_scene(c);
    solid(c, s, RECT(10, 10, 30, 20), RED);
    frame *f = render(c, s);
    EXPECT("inside", 15, 15, RED);
    EXPECT("below", 15, 85, CLEAR);
    EXPECT("right", 45, 15, CLEAR);
}

void transform_rotates_around_the_anchor_point(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *bar = solid(c, s, RECT(30, 45, 40, 10), RED);
    double angle = acos(-1.0) / 2;
    API(gr_layer_set_transform(bar, (gr_affine){cos(angle), sin(angle), -sin(angle), cos(angle), 0, 0}));
    frame *f = render(c, s);
    EXPECT("rotated bar", 50, 32, RED);
    EXPECT("former bar end", 35, 50, CLEAR);
}

void skew_transforms_apply(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *bar = solid(c, s, RECT(30, 40, 40, 20), RED);
    API(gr_layer_set_transform(bar, (gr_affine){1, 0, 1, 1, 0, 0}));
    frame *f = render(c, s);
    EXPECT("lower half, slid right", 75, 58, RED);
    EXPECT("upper half, slid left", 25, 42, RED);
    EXPECT("right of the upper half", 75, 42, CLEAR);
    EXPECT("left of the lower half", 25, 58, CLEAR);
}

void z_order_follows_sublayer_order(ctx *c)
{
    gr_scene *s = new_scene(c);
    solid(c, s, RECT(10, 10, 40, 40), RED);
    solid(c, s, RECT(30, 30, 40, 40), BLUE);
    gr_layer *below = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(below, 4, 4, 20, 20);
    BACKGROUND(below, WHITE);
    API(gr_layer_insert_child(gr_scene_get_root(s), below, 0));
    frame *f = render(c, s);
    EXPECT("later sibling on top", 40, 40, BLUE);
    EXPECT("inserted at 0 is below", 15, 15, RED);
    EXPECT("inserted at 0 visible elsewhere", 6, 6, WHITE);
}

void hidden_hides_the_subtree(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *parent = new_layer(c, s, GR_LAYER_CONTAINER), *child = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(parent, 0, 0, 50, 50);
    FRAME(child, 0, 0, 50, 50);
    BACKGROUND(child, RED);
    ADD(parent, child);
    ADD(gr_scene_get_root(s), parent);
    API(gr_layer_set_hidden(parent, true));
    solid(c, s, RECT(60, 60, 20, 20), BLUE);
    frame *f = render(c, s);
    EXPECT("hidden subtree", 25, 25, CLEAR);
    EXPECT("visible sibling", 70, 70, BLUE);
}

void masks_to_bounds_clips_sublayers(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *parent = new_layer(c, s, GR_LAYER_CONTAINER), *child = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(parent, 10, 10, 40, 40);
    API(gr_layer_set_masks_to_bounds(parent, true));
    FRAME(child, 20, 20, 60, 60);
    BACKGROUND(child, RED);
    ADD(parent, child);
    ADD(gr_scene_get_root(s), parent);
    frame *f = render(c, s);
    EXPECT("inside the parent", 40, 40, RED);
    EXPECT("outside the parent", 60, 60, CLEAR);
}

void masked_corners_round_only_selected_corners(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 40, 40), RED);
    API(gr_layer_set_corner_radius(l, 20));
    API(gr_layer_set_masked_corners(l, GR_CORNER_TOP_LEFT));
    frame *f = render(c, s);
    EXPECT("rounded top-left", 1, 1, CLEAR);
    EXPECT("square bottom-left", 1, 38, RED);
    EXPECT("square top-right", 38, 1, RED);
    EXPECT("center", 20, 20, RED);
}

void border_is_inside_and_above_sublayers(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTAINER), *fill = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(l, 10, 10, 40, 40);
    gr_border border = {4, color(BLUE)};
    API(gr_layer_set_border(l, &border));
    FRAME(fill, 0, 0, 40, 40);
    BACKGROUND(fill, RED);
    ADD(l, fill);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("border over the sublayer", 12, 30, BLUE);
    EXPECT("sublayer inside the border", 30, 30, RED);
    EXPECT("nothing outside the bounds", 8, 30, CLEAR);
}

void opacity_is_group_opacity(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *group = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(group, 0, 0, 40, 40);
    API(gr_layer_set_opacity(group, 0.5f));
    ADD(gr_scene_get_root(s), group);
    for (int i = 0; i < 2; ++i)
    {
        gr_layer *square = new_layer(c, s, GR_LAYER_CONTAINER);
        FRAME(square, i * 10, 0, 20, 20);
        BACKGROUND(square, RED);
        ADD(group, square);
    }
    frame *f = render(c, s);
    rgba8 single = {0}, overlap = {0};
    bool a = frame_at(f, POINT(5, 10), &single), b = frame_at(f, POINT(15, 10), &overlap);
    conf_check(c, a && single.v[3] > 0, "the group is invisible");
    conf_check(c, a == b && (!a || memcmp(single.v, overlap.v, 4) == 0), "overlap differs from a single square");
}

void masks_clip_by_alpha(ctx *c)
{
    if (!supports(c, GR_CONF_MASKS))
        return;
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 40, 40), RED), *mask = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(mask, 0, 0, 20, 40);
    BACKGROUND(mask, WHITE);
    API(gr_layer_set_mask(l, mask));
    frame *f = render(c, s);
    EXPECT("under the mask", 10, 20, RED);
    EXPECT("outside the mask", 30, 20, CLEAR);
}

void masks_keep_sublayers_outside_bounds(ctx *c)
{
    if (!supports(c, GR_CONF_MASKS))
        return;
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 40, 40), RED), *mask = new_layer(c, s, GR_LAYER_CONTAINER);
    gr_layer *inside = new_layer(c, s, GR_LAYER_CONTAINER), *outside = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(inside, 50, 0, 20, 20);
    BACKGROUND(inside, BLUE);
    ADD(l, inside);
    FRAME(outside, 85, 0, 10, 10);
    BACKGROUND(outside, BLUE);
    ADD(l, outside);
    FRAME(mask, 0, 0, 80, 40);
    BACKGROUND(mask, WHITE);
    API(gr_layer_set_mask(l, mask));
    frame *f = render(c, s);
    EXPECT("the layer", 10, 10, RED);
    EXPECT("sublayer under the mask, past the bounds", 60, 10, BLUE);
    EXPECT("sublayer outside the mask", 90, 5, CLEAR);
}

static void scroll_draw(void *data, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)data;
    (void)info;
    canvas_fill(canvas, RECT(0, 10, 20, 5), BLUE);
}

void bounds_origin_scrolls_sublayers_and_drawings(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *parent = new_layer(c, s, GR_LAYER_CONTAINER), *child = new_layer(c, s, GR_LAYER_CONTAINER),
             *drawn = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(parent, 0, 0, 50, 50);
    API(gr_layer_set_bounds(parent, RECT(0, 20, 50, 50)));
    BACKGROUND(child, RED);
    FRAME(child, 0, 20, 10, 10);
    ADD(parent, child);
    ADD(gr_scene_get_root(s), parent);
    FRAME(drawn, 60, 0, 20, 20);
    API(gr_layer_set_bounds(drawn, RECT(0, 10, 20, 20)));
    gr_drawer d = {NULL, scroll_draw, NULL};
    API(gr_content_layer_set_drawer(drawn, &d));
    ADD(gr_scene_get_root(s), drawn);
    frame *f = render(c, s);
    EXPECT("scrolled sublayer", 5, 5, RED);
    EXPECT("below the scrolled sublayer", 5, 25, CLEAR);
    EXPECT("scrolled drawing", 70, 2, BLUE);
    EXPECT("below the scrolled drawing", 70, 7, CLEAR);
}

static gr_image *red_over_blue(void)
{
    uint8_t bytes[] = {255, 0, 0, 255, 0, 0, 255, 255};
    return gr_image_new(1, 2, 1, (gr_bytes){bytes, 8});
}

void image_contents_are_upright(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(l, 0, 0, 40, 40);
    gr_image *image = red_over_blue();
    API(gr_content_layer_set_image(l, image));
    gr_image_unref(image);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("top half", 20, 5, RED);
    EXPECT("bottom half", 20, 35, BLUE);
}

void image_gravity_places_contents(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(l, 0, 0, 40, 40);
    uint8_t bytes[] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    gr_image *image = gr_image_new(2, 2, 1, (gr_bytes){bytes, 16});
    API(gr_content_layer_set_image(l, image));
    gr_image_unref(image);
    API(gr_content_layer_set_gravity(l, GR_GRAVITY_BOTTOM_RIGHT));
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("bottom-right corner", 39, 39, RED);
    EXPECT("left of the image", 36.5, 39, CLEAR);
    EXPECT("top-left", 1, 1, CLEAR);
}

static void upright_draw(void *data, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)data;
    gr_rect b = info->bounds;
    canvas_fill(canvas, RECT(b.origin.x, b.origin.y, b.size.width, b.size.height / 2), RED);
    gr_image *image = red_over_blue();
    API(gr_canvas_draw_image(canvas, image, RECT(b.origin.x + 20, b.origin.y + 20, 20, 20)));
    gr_image_unref(image);
}

void drawings_are_upright(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(l, 0, 0, 40, 40);
    gr_drawer d = {NULL, upright_draw, NULL};
    API(gr_content_layer_set_drawer(l, &d));
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("filled top half", 10, 5, RED);
    EXPECT("empty bottom half", 10, 35, CLEAR);
    EXPECT("drawn image top", 30, 22, RED);
    EXPECT("drawn image bottom", 30, 38, BLUE);
}

static void state_draw(void *data, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)data;
    (void)info;
    gr_canvas_save(canvas);
    API(gr_canvas_concat_transform(canvas, (gr_affine){1, 0, 0, 1, 20, 0}));
    canvas_fill(canvas, RECT(0, 0, 10, 10), RED);
    gr_canvas_restore(canvas);
    canvas_fill(canvas, RECT(0, 20, 10, 10), BLUE);
}

void canvas_state_is_saved_and_restored(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(l, 0, 0, 50, 50);
    gr_drawer d = {NULL, state_draw, NULL};
    API(gr_content_layer_set_drawer(l, &d));
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("translated fill", 25, 5, RED);
    EXPECT("untranslated origin", 5, 5, CLEAR);
    EXPECT("fill after restore", 5, 25, BLUE);
}

static void clip_draw(void *data, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)data;
    (void)info;
    gr_canvas_save(canvas);
    API(gr_canvas_clip_rect(canvas, RECT(0, 0, 20, 40)));
    canvas_fill(canvas, RECT(0, 0, 40, 40), RED);
    gr_canvas_restore(canvas);
    gr_canvas_save(canvas);
    API(gr_canvas_set_alpha(canvas, 0.5f));
    canvas_fill(canvas, RECT(0, 50, 20, 20), BLUE);
    gr_canvas_restore(canvas);
    canvas_fill(canvas, RECT(30, 50, 20, 20), BLUE);
}

void canvas_clip_and_alpha_apply(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(l, 0, 0, 60, 80);
    gr_drawer d = {NULL, clip_draw, NULL};
    API(gr_content_layer_set_drawer(l, &d));
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("inside the clip", 10, 10, RED);
    EXPECT("outside the clip", 30, 10, CLEAR);
    expect_color(c, f, "half alpha", POINT(10, 60), (rgba8){{0, 0, 128, 128}}, 10);
    EXPECT("alpha restored", 40, 60, BLUE);
}

void shapes_use_local_coordinates(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_SHAPE);
    FRAME(l, 10, 10, 40, 40);
    gr_path *p = rectangle_path(RECT(0, 0, 40, 10));
    API(gr_shape_layer_set_path(l, p));
    gr_path_unref(p);
    set_fill(l, RED);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("filled path", 30, 15, RED);
    EXPECT("rest of the layer", 30, 30, CLEAR);
}

void stroke_end_trims_the_path(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_SHAPE);
    FRAME(l, 0, 0, 100, 100);
    gr_path *p = line_path(POINT(10, 50), POINT(90, 50));
    API(gr_shape_layer_set_path(l, p));
    gr_path_unref(p);
    set_stroke(l, RED, 6);
    API(gr_shape_layer_set_stroke_end(l, 0.5));
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("stroked first half", 30, 50, RED);
    EXPECT("trimmed second half", 70, 50, CLEAR);
}

void linear_gradient_unit_space_is_y_down(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_GRADIENT);
    FRAME(l, 0, 0, 40, 40);
    set_gradient(l, false, POINT(0, 0), POINT(0, 1), 0);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    rgba8 top, bottom;
    conf_check(c, frame_at(f, POINT(20, 0.5), &top) && top.v[0] > 220 && top.v[2] < 35, "top should be red");
    conf_check(c, frame_at(f, POINT(20, 39.5), &bottom) && bottom.v[2] > 220 && bottom.v[0] < 35,
               "bottom should be blue");
}

void radial_gradient_is_centered(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_GRADIENT);
    FRAME(l, 0, 0, 80, 40);
    set_gradient(l, true, POINT(0.5, 0.5), POINT(0, 0), 0.5);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    rgba8 center, right, bottom;
    conf_check(c, frame_at(f, POINT(40, 20), &center) && center.v[0] > 220 && center.v[2] < 35, "center should be red");
    conf_check(c, frame_at(f, POINT(78.5, 20), &right) && right.v[2] > 200, "near the x radius should be blue");
    conf_check(c, frame_at(f, POINT(40, 38.5), &bottom) && bottom.v[2] > 200, "near the y radius should be blue");
}

void shadow_offset_is_y_down_and_escapes_masks_to_bounds(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(30, 30, 20, 20), WHITE);
    API(gr_layer_set_masks_to_bounds(l, true));
    API(gr_layer_set_corner_radius(l, 4));
    set_shadow(l, 0, 15, 0.5, 0);
    frame *f = render(c, s);
    EXPECT("the layer", 40, 40, WHITE);
    EXPECT("shadow below", 40, 60, BLACK);
    EXPECT("nothing above", 40, 20, CLEAR);
}

void shadow_is_drawn_under_layers_without_background(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(l, 30, 30, 40, 40);
    API(gr_layer_set_corner_radius(l, 6));
    set_shadow(l, 0, 0, 0.5, 0);
    ADD(gr_scene_get_root(s), l);
    frame *f = render(c, s);
    EXPECT("under the layer", 50, 50, BLACK);
    EXPECT("outside the shadow", 50, 20, CLEAR);
}

void fractional_positions_snap_to_device_pixels(ctx *c)
{
    double scale = c->scale, px = 1 / scale;
    gr_scene *s = new_scene(c);
    solid(c, s, RECT(10.3, 10, 20, 20), RED);
    gr_layer *parent = new_layer(c, s, GR_LAYER_CONTAINER), *child = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(parent, 0, 40.3, 60, 50);
    API(gr_layer_set_bounds(parent, RECT(0, 0.4, 60, 50)));
    FRAME(child, 10, 0.2, 20, 20);
    BACKGROUND(child, BLUE);
    ADD(parent, child);
    ADD(gr_scene_get_root(s), parent);
    frame *f = render(c, s);
    double left = round(10.3 * scale) / scale,
           top = round(40.3 * scale) / scale - round(0.4 * scale) / scale + round(0.2 * scale) / scale;
    expect_color(c, f, "first column", POINT(left + px / 2, 20), RED, 3);
    expect_color(c, f, "column before", POINT(left - px / 2, 20), CLEAR, 3);
    expect_color(c, f, "last column", POINT(left + 20 - px / 2, 20), RED, 3);
    expect_color(c, f, "column after", POINT(left + 20 + px / 2, 20), CLEAR, 3);
    expect_color(c, f, "nested first row", POINT(20, top + px / 2), BLUE, 3);
    expect_color(c, f, "nested row before", POINT(20, top - px / 2), CLEAR, 3);
}

void edges_and_borders_are_crisp(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *boxed = solid(c, s, RECT(10.3, 10.6, 40.45, 20.7), RED);
    gr_border border = {1, color(BLACK)};
    API(gr_layer_set_border(boxed, &border));
    gr_layer *parent = new_layer(c, s, GR_LAYER_CONTAINER), *child = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(parent, 5.2, 50.4, 80, 40);
    ADD(gr_scene_get_root(s), parent);
    FRAME(child, 3.3, 2.6, 30.7, 12.9);
    BACKGROUND(child, BLUE);
    ADD(parent, child);
    frame *f = render(c, s);
    const rgba8 flat[] = {CLEAR, RED, BLACK, BLUE};

    const struct
    {
        const char *label;
        gr_point from, to;
    } scans[] = {{"row through the bordered box", POINT(2, 21), POINT(60, 21)},
                 {"column through the bordered box", POINT(30, 2), POINT(30, 40)},
                 {"row through the nested box", POINT(2, 59.5), POINT(60, 59.5)},
                 {"column through the nested box", POINT(20, 45), POINT(20, 75)}};

    for (size_t i = 0; i < 4; ++i)
    {
        profile p = frame_profile(f, scans[i].from, scans[i].to);
        for (size_t j = 0; j < p.count; ++j)
        {
            bool matches = false;
            for (size_t k = 0; k < 4; ++k)
            {
                bool same = true;
                for (size_t n = 0; n < 4; ++n)
                    same &= fabs(flat[k].v[n] / 255.0 - p.data[j].channels[n]) <= 10.0 / 255.0;
                matches |= same;
            }
            if (!matches)
            {
                conf_check(c, false, "%s: partially covered pixel at %.2f pt from the start", scans[i].label,
                           p.data[j].distance);
                break;
            }
        }
        free(p.data);
    }
}

void shadow_spread_grows_the_shape(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *opaque = solid(c, s, RECT(20, 20, 30, 30), WHITE);
    set_shadow(opaque, 0, 0, 0.5, 8);
    gr_layer *hollow = new_layer(c, s, GR_LAYER_CONTAINER);
    FRAME(hollow, 60, 60, 30, 30);
    API(gr_layer_set_corner_radius(hollow, 4));
    set_shadow(hollow, 0, 0, 0.5, 8);
    ADD(gr_scene_get_root(s), hollow);
    frame *f = render(c, s);
    EXPECT("opaque: within the spread", 35, 16, BLACK);
    EXPECT("opaque: beyond the spread", 35, 6, CLEAR);
    EXPECT("opaque: the layer", 35, 35, WHITE);
    EXPECT("hollow: within the spread", 75, 56, BLACK);
    EXPECT("hollow: beyond the spread", 75, 46, CLEAR);
    EXPECT("hollow: under the layer", 75, 75, BLACK);
}

void updates_and_removals_reach_the_screen(ctx *c)
{
    gr_scene *s = new_scene(c);
    gr_layer *l = solid(c, s, RECT(0, 0, 20, 20), RED);
    frame *f = render(c, s);
    EXPECT("initial", 5, 5, RED);
    BACKGROUND(l, BLUE);
    API(gr_layer_set_position(l, POINT(70, 70)));
    f = render(c, s);
    EXPECT("old position", 5, 5, CLEAR);
    EXPECT("new position and color", 65, 65, BLUE);
    gr_layer *image = new_layer(c, s, GR_LAYER_CONTENT);
    FRAME(image, 0, 0, 20, 20);
    gr_image *pixels = red_over_blue();
    API(gr_content_layer_set_image(image, pixels));
    gr_image_unref(pixels);
    ADD(gr_scene_get_root(s), image);
    f = render(c, s);
    EXPECT("added contents", 10, 2, RED);
    API(gr_content_layer_set_image(image, NULL));
    API(gr_layer_remove_from_parent(l));
    for (size_t i = 0; i < c->layer_count; ++i)
        if (c->layers[i] == l)
        {
            c->layers[i] = NULL;
            break;
        }
    gr_layer_unref(l);
    f = render(c, s);
    EXPECT("cleared contents", 10, 2, CLEAR);
    EXPECT("removed layer", 65, 65, CLEAR);
}

static double alpha_at(const frame *f, gr_point point)
{
    rgba8 pixel;
    return frame_at(f, point, &pixel) ? pixel.v[3] / 255.0 : NAN;
}

void blur_covers_sublayers_and_bleeds_past_bounds(ctx *c)
{
    gr_scene *s = new_scene(c);
    for (size_t i = 0; i < 2; ++i)
    {
        double y = 10 + 50.0 * i;
        gr_layer *l = solid(c, s, RECT(10, y, 30, 30), RED), *child = new_layer(c, s, GR_LAYER_CONTAINER);
        FRAME(child, 30, 0, 30, 30);
        BACKGROUND(child, BLUE);
        ADD(l, child);
        API(gr_layer_set_masks_to_bounds(l, i == 1));
        API(gr_layer_set_blur(l, 2));
    }
    frame *f = render(c, s);
    expect_color(c, f, "blurred layer", POINT(20, 25), RED, 24);
    expect_color(c, f, "blurred sublayer past the bounds", POINT(60, 25), BLUE, 24);
    double bleed = alpha_at(f, POINT(20, 42));
    conf_check(c, bleed > 0.05 && bleed < 0.4, "blur should bleed one sigma below the layer (alpha %.3f)", bleed);
    expect_color(c, f, "clipped layer", POINT(20, 75), RED, 24);
    expect_color(c, f, "clipped sublayer", POINT(60, 75), CLEAR, 8);
    bleed = alpha_at(f, POINT(20, 92));
    conf_check(c, bleed > 0.05 && bleed < 0.4, "blur should bleed past a clip (alpha %.3f)", bleed);
}
