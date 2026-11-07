#include "demo.h"

gr_color hex(uint32_t rgb)
{
    return (gr_color){((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1};
}

gr_color alpha(gr_color c, float a)
{
    c.a = a;
    return c;
}

gr_layer *new_layer(demo *d, gr_layer_kind kind)
{
    if (d->layer_count == d->layer_capacity)
    {
        d->layer_capacity = d->layer_capacity ? d->layer_capacity * 2 : 64;
        void *p = realloc(d->layers, d->layer_capacity * sizeof(*d->layers));
        if (!p)
            abort();
        d->layers = p;
    }
    gr_layer *l = gr_layer_new(d->scene, kind);
    if (!l)
        abort();
    d->layers[d->layer_count++] = l;
    return l;
}

gr_layer *layer(demo *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = new_layer(d, GR_LAYER_CONTAINER);
    D_OK(gr_layer_set_frame(l, rect));
    D_OK(gr_layer_add_child(parent, l));
    return l;
}

gr_layer *pill(demo *d, gr_layer *parent, gr_rect rect, gr_color color, double radius)
{
    gr_layer *l = layer(d, parent, rect);
    background(l, color);
    D_OK(gr_layer_set_corner_radius(l, radius));
    return l;
}

gr_layer *shape(demo *d, gr_layer *parent, gr_rect rect)
{
    gr_layer *l = new_layer(d, GR_LAYER_SHAPE);
    D_OK(gr_layer_set_frame(l, rect));
    D_OK(gr_layer_add_child(parent, l));
    return l;
}

void background(gr_layer *l, gr_color c)
{
    D_OK(gr_layer_set_background(l, &c));
}

void border(gr_layer *l, double width, gr_color c)
{
    gr_border b = {width, c};
    D_OK(gr_layer_set_border(l, &b));
}

void demo_shadow(gr_layer *l, gr_color c, double x, double y, double sigma, gr_path *p)
{
    gr_shadow s = {.color = c, .offset = {x, y}, .blur_sigma = sigma, .path = p};
    D_OK(gr_layer_set_shadow(l, &s));
}

gr_paint *paint(gr_color c)
{
    gr_paint_desc desc = {.kind = GR_PAINT_SOLID, .value.solid = c};
    return gr_paint_new(&desc);
}

void fill(gr_layer *l, gr_color c)
{
    gr_paint *p = paint(c);
    D_OK(gr_shape_layer_set_fill(l, p));
    gr_paint_unref(p);
}

void set_path(gr_layer *l, gr_path *p)
{
    if (!p)
        abort();
    D_OK(gr_shape_layer_set_path(l, p));
    gr_path_unref(p);
}

void set_image(gr_layer *l, gr_image *image)
{
    D_OK(gr_content_layer_set_image(l, image));
}

void set_gradient(gr_layer *l, gr_paint *p)
{
    if (!p)
        abort();
    D_OK(gr_gradient_layer_set_gradient(l, p));
    gr_paint_unref(p);
}

gr_paint *gradient(bool radial, gr_point start, gr_point end, double radius, gr_gradient_stop_list stops)
{
    gr_paint_desc p = {.kind = radial ? GR_PAINT_RADIAL : GR_PAINT_LINEAR};
    if (radial)
    {
        p.value.radial.center = start;
        p.value.radial.radius = radius;
        p.value.radial.stops = stops;
    }
    else
    {
        p.value.linear.start = start;
        p.value.linear.end = end;
        p.value.linear.stops = stops;
    }
    gr_paint *result = gr_paint_new(&p);
    if (!result)
        abort();
    return result;
}

gr_stroke *stroke(gr_paint *p, double width, gr_line_cap cap, gr_line_join join, gr_double_list dash, double phase)
{
    gr_stroke_style style = gr_stroke_style_default();
    style.width = width;
    style.cap = cap;
    style.join = join;
    style.dash = dash;
    style.dash_phase = phase;
    gr_stroke *s = gr_stroke_new(p, &style);
    gr_paint_unref(p);
    if (!s)
        abort();
    return s;
}

void shape_stroke(gr_layer *l, gr_stroke *s)
{
    D_OK(gr_shape_layer_set_stroke(l, s));
    gr_stroke_unref(s);
}

void draw_fill(gr_canvas *c, gr_path *p, gr_fill_rule rule, gr_paint *color)
{
    D_OK(gr_canvas_fill_path(c, p, rule, color));
    gr_path_unref(p);
    gr_paint_unref(color);
}

void draw_stroke(gr_canvas *c, gr_path *p, gr_stroke *s)
{
    D_OK(gr_canvas_stroke_path(c, p, s));
    gr_path_unref(p);
    gr_stroke_unref(s);
}

void draw_rect(gr_canvas *c, gr_rect rect, gr_color color)
{
    gr_paint *p = paint(color);
    D_OK(gr_canvas_fill_rect(c, rect, p));
    gr_paint_unref(p);
}

void move_to(gr_path_builder *b, gr_point p)
{
    gr_path_element e = {GR_PATH_MOVE, {p}};
    D_OK(gr_path_builder_append(b, (gr_path_element_list){&e, 1}));
}

void line_to(gr_path_builder *b, gr_point p)
{
    gr_path_element e = {GR_PATH_LINE, {p}};
    D_OK(gr_path_builder_append(b, (gr_path_element_list){&e, 1}));
}

void curve_to(gr_path_builder *b, gr_point a, gr_point c, gr_point p)
{
    gr_path_element e = {GR_PATH_CUBIC, {a, c, p}};
    D_OK(gr_path_builder_append(b, (gr_path_element_list){&e, 1}));
}

void close_path(gr_path_builder *b)
{
    gr_path_element e = {.verb = GR_PATH_CLOSE};
    D_OK(gr_path_builder_append(b, (gr_path_element_list){&e, 1}));
}

gr_path *finish_path(gr_path_builder *b)
{
    gr_path *p = gr_path_builder_finish(b);
    gr_path_builder_unref(b);
    return p;
}

gr_path *circle(gr_point center, double radius)
{
    gr_path_builder *b = gr_path_builder_new();
    D_OK(gr_path_builder_add_ellipse(b, D_RECT(center.x - radius, center.y - radius, 2 * radius, 2 * radius)));
    return finish_path(b);
}

gr_path *rounded(gr_rect r, double radius, gr_corners corners)
{
    gr_path_builder *b = gr_path_builder_new();
    D_OK(gr_path_builder_add_rounded_rect(b, r, radius, corners));
    return finish_path(b);
}

gr_path *star_path(gr_point center, double outer, double inner, size_t points, double rotation_angle)
{
    gr_path_builder *b = gr_path_builder_new();
    for (size_t i = 0; i < points * 2; ++i)
    {
        double r = i % 2 == 0 ? outer : inner, a = rotation_angle - D_PI / 2 + (double)i * D_TAU / (double)(points * 2);
        gr_point p = {center.x + r * cos(a), center.y + r * sin(a)};
        if (i == 0)
            move_to(b, p);
        else
            line_to(b, p);
    }
    close_path(b);
    return finish_path(b);
}

gr_path *star_polygon(gr_point center, double radius, size_t points, size_t skip, double rotation_angle)
{
    gr_path_builder *b = gr_path_builder_new();
    for (size_t i = 0; i < points; ++i)
    {
        double a = rotation_angle - D_PI / 2 + (double)((i * skip) % points) * D_TAU / (double)points;
        gr_point p = {center.x + radius * cos(a), center.y + radius * sin(a)};
        if (i == 0)
            move_to(b, p);
        else
            line_to(b, p);
    }
    close_path(b);
    return finish_path(b);
}

gr_affine rotation(double a)
{
    return (gr_affine){cos(a), sin(a), -sin(a), cos(a), 0, 0};
}

gr_affine scaling(double s)
{
    return (gr_affine){s, 0, 0, s, 0, 0};
}

gr_affine translation(gr_point p)
{
    return (gr_affine){1, 0, 0, 1, p.x, p.y};
}

double smooth(double x)
{
    x = fmin(1, fmax(0, x));
    return x * x * (3 - 2 * x);
}

double wave(double t, double period, double offset)
{
    return 0.5 - 0.5 * cos(D_TAU * (t / period + offset));
}

gr_size demo_size(demo_kind kind)
{
    if (kind == DEMO_STRESS)
        return (gr_size){800, 800};
    return kind == DEMO_UI ? (gr_size){1000, 600} : (gr_size){960, 640};
}

demo *demo_new(demo_kind kind, gr_text_system *text)
{
    demo *d = calloc(1, sizeof(*d));
    if (!d)
        abort();
    d->kind = kind;
    d->text = gr_text_system_ref(text);
    d->scene = gr_scene_new(demo_size(kind));
    d->front = SIZE_MAX;
    if (kind == DEMO_UI)
        ui_build(d);
    else if (kind == DEMO_STRESS)
        stress_build(d);
    else
        showcase_build(d);
    demo_update(d, 0);
    return d;
}

void demo_update(demo *d, double t)
{
    d->time = t;
    if (d->kind == DEMO_UI)
        ui_update(d, t);
    else if (d->kind == DEMO_STRESS)
        stress_update(d, t);
    else
        showcase_update(d, t);
}

void demo_free(demo *d)
{
    gr_scene_unref(d->scene);
    for (size_t i = 0; i < d->layer_count; ++i)
        gr_layer_unref(d->layers[i]);
    gr_image_unref(d->drawing_picture);
    free(d->layers);
    if (d->stress_cells)
        free(d->stress_cells);
    gr_text_system_unref(d->text);
    free(d);
}
