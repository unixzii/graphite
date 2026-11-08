#include "internal.h"

GdkRGBA gr_gdk_color(gr_color c)
{
    return (GdkRGBA){c.r, c.g, c.b, c.a};
}

graphene_rect_t gr_graphene_rect(gr_rect r)
{
    graphene_rect_t out;
    graphene_rect_init(&out, (float)r.origin.x, (float)r.origin.y, (float)r.size.width, (float)r.size.height);
    return out;
}

graphene_point_t gr_graphene_point(gr_point p)
{
    return (graphene_point_t){(float)p.x, (float)p.y};
}

GskRoundedRect gr_gsk_rounded(gr_rect r, double radius, gr_corners corners)
{
    float rad = (float)fmin(fmax(0, radius), fmin(fabs(r.size.width), fabs(r.size.height)) / 2);
    graphene_size_t sizes[4];
    gr_corners flags[] = {GR_CORNER_TOP_LEFT, GR_CORNER_TOP_RIGHT, GR_CORNER_BOTTOM_RIGHT, GR_CORNER_BOTTOM_LEFT};
    for (size_t i = 0; i < 4; ++i)
    {
        float v = corners & flags[i] ? rad : 0;
        graphene_size_init(&sizes[i], v, v);
    }
    graphene_rect_t bounds = gr_graphene_rect(r);
    GskRoundedRect out;
    gsk_rounded_rect_init(&out, &bounds, &sizes[0], &sizes[1], &sizes[2], &sizes[3]);
    return out;
}

GskTransform *gr_gsk_transform(gr_affine m)
{
    graphene_point_t p = {(float)m.tx, (float)m.ty};
    if (m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1 && m.tx == 0 && m.ty == 0)
        return NULL;
    if (m.b == 0 && m.c == 0)
    {
        GskTransform *t = gsk_transform_translate(NULL, &p);
        return m.a == 1 && m.d == 1 ? t : gsk_transform_scale(t, (float)m.a, (float)m.d);
    }
    double sx = hypot(m.a, m.b), sy = (m.a * m.d - m.b * m.c) / sx;
    if (sx == 0 || sy == 0 || !isfinite(sy))
    {
        graphene_matrix_t matrix;
        graphene_matrix_init_from_2d(&matrix, m.a, m.b, m.c, m.d, m.tx, m.ty);
        return gsk_transform_matrix(NULL, &matrix);
    }
    double angle = atan2(m.b, m.a), shear = (m.a * m.c + m.b * m.d) / sx;
    GskTransform *t = gsk_transform_translate(NULL, &p);
    t = gsk_transform_rotate(t, (float)(angle * 180 / acos(-1.0)));
    if (shear != 0)
        t = gsk_transform_skew(t, (float)(atan(shear / sy) * 180 / acos(-1.0)), 0);
    return gsk_transform_scale(t, (float)sx, (float)sy);
}

GskPath *gr_gsk_path(const gr_path *path)
{
    GskPathBuilder *b = gsk_path_builder_new();
    gr_path_element_list es = gr_path_get_elements(path);
    for (size_t i = 0; i < es.count; ++i)
    {
        gr_path_element e = es.data[i];
        switch (e.verb)
        {
            case GR_PATH_MOVE:
                gsk_path_builder_move_to(b, (float)e.p[0].x, (float)e.p[0].y);
                break;
            case GR_PATH_LINE:
                gsk_path_builder_line_to(b, (float)e.p[0].x, (float)e.p[0].y);
                break;
            case GR_PATH_QUAD:
                gsk_path_builder_quad_to(b, (float)e.p[0].x, (float)e.p[0].y, (float)e.p[1].x, (float)e.p[1].y);
                break;
            case GR_PATH_CUBIC:
                gsk_path_builder_cubic_to(b, (float)e.p[0].x, (float)e.p[0].y, (float)e.p[1].x, (float)e.p[1].y,
                                          (float)e.p[2].x, (float)e.p[2].y);
                break;
            case GR_PATH_CLOSE:
                gsk_path_builder_close(b);
                break;
        }
    }
    return gsk_path_builder_free_to_path(b);
}

GskPath *gr_gsk_trim(GskPath *path, double start, double end)
{
    if (start <= 0 && end >= 1)
        return gsk_path_ref(path);
    if (end <= start)
        return NULL;
    GskPathMeasure *measure = gsk_path_measure_new(path);
    float length = gsk_path_measure_get_length(measure);
    GskPathPoint from, to;
    GskPath *result = NULL;
    if (gsk_path_measure_get_point(measure, (float)start * length, &from) &&
        gsk_path_measure_get_point(measure, (float)end * length, &to))
    {
        GskPathBuilder *b = gsk_path_builder_new();
        gsk_path_builder_add_segment(b, path, &from, &to);
        result = gsk_path_builder_free_to_path(b);
    }
    gsk_path_measure_unref(measure);
    return result;
}

GskStroke *gr_gsk_stroke(const gr_stroke_style *st)
{
    GskStroke *s = gsk_stroke_new((float)st->width);
    gsk_stroke_set_line_cap(s, st->cap == GR_CAP_ROUND    ? GSK_LINE_CAP_ROUND
                               : st->cap == GR_CAP_SQUARE ? GSK_LINE_CAP_SQUARE
                                                          : GSK_LINE_CAP_BUTT);
    gsk_stroke_set_line_join(s, st->join == GR_JOIN_ROUND   ? GSK_LINE_JOIN_ROUND
                                : st->join == GR_JOIN_BEVEL ? GSK_LINE_JOIN_BEVEL
                                                            : GSK_LINE_JOIN_MITER);
    gsk_stroke_set_miter_limit(s, (float)st->miter_limit);
    float *dash = g_new(float, st->dash.count);
    for (size_t i = 0; i < st->dash.count; ++i)
        dash[i] = (float)st->dash.data[i];
    gsk_stroke_set_dash(s, dash, st->dash.count);
    g_free(dash);
    gsk_stroke_set_dash_offset(s, (float)st->dash_phase);
    return s;
}

/* Images are immutable, so the texture borrows their pixels and holds a reference instead of copying them. */
GdkTexture *gr_gdk_texture(gr_image *image)
{
    gr_bytes pixels = gr_image_get_pixels(image);
    GBytes *bytes =
        g_bytes_new_with_free_func(pixels.data, pixels.count, (GDestroyNotify)gr_image_unref, gr_image_ref(image));
    GdkTexture *texture =
        gdk_memory_texture_new((int)gr_image_get_width(image), (int)gr_image_get_height(image),
                               GDK_MEMORY_R8G8B8A8_PREMULTIPLIED, bytes, (size_t)gr_image_get_width(image) * 4);
    g_bytes_unref(bytes);
    return texture;
}
