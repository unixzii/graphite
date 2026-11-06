#include "internal.h"

struct gr_paint
{
    atomic_size_t refs;
    gr_paint_desc desc;
};

struct gr_stroke
{
    atomic_size_t refs;
    gr_paint *paint;
    gr_stroke_style style;
};

struct gr_image
{
    atomic_size_t refs;
    uint64_t id;
    uint32_t width, height;
    double scale;
    gr_bytes pixels;
};

static bool valid_stops(gr_gradient_stop_list stops)
{
    if (!stops.data || stops.count < 2 || stops.count > SIZE_MAX / sizeof(*stops.data))
        return false;
    for (size_t i = 0; i < stops.count; ++i)
    {
        float offset = stops.data[i].offset;
        if (!isfinite(offset) || offset < 0 || offset > 1 || !gr_valid_color(stops.data[i].color) ||
            (i && offset < stops.data[i - 1].offset))
            return false;
    }
    return true;
}

gr_paint *gr_paint_new(const gr_paint_desc *desc)
{
    if (!desc)
        return NULL;
    gr_gradient_stop_list stops = {0};
    switch (desc->kind)
    {
        case GR_PAINT_SOLID:
            if (!gr_valid_color(desc->value.solid))
                return NULL;
            break;
        case GR_PAINT_LINEAR:
            stops = desc->value.linear.stops;
            if (!gr_valid_point(desc->value.linear.start) || !gr_valid_point(desc->value.linear.end) ||
                !valid_stops(stops))
                return NULL;
            break;
        case GR_PAINT_RADIAL:
            stops = desc->value.radial.stops;
            if (!gr_valid_point(desc->value.radial.center) || !isfinite(desc->value.radial.radius) ||
                desc->value.radial.radius < 0 || !valid_stops(stops))
                return NULL;
            break;
        default:
            return NULL;
    }
    gr_paint *p = gr_alloc(1, sizeof(*p));
    atomic_init(&p->refs, 1);
    p->desc = *desc;
    if (stops.count)
    {
        stops.data = gr_copy(stops.data, stops.count, sizeof(*stops.data));
        if (desc->kind == GR_PAINT_LINEAR)
            p->desc.value.linear.stops = stops;
        else
            p->desc.value.radial.stops = stops;
    }
    return p;
}

gr_paint *gr_paint_ref(gr_paint *p)
{
    if (p)
        gr_shared_ref(&p->refs);
    return p;
}

void gr_paint_unref(gr_paint *p)
{
    if (!p || !gr_shared_unref(&p->refs))
        return;
    if (p->desc.kind == GR_PAINT_LINEAR)
        free((void *)p->desc.value.linear.stops.data);
    if (p->desc.kind == GR_PAINT_RADIAL)
        free((void *)p->desc.value.radial.stops.data);
    free(p);
}

gr_paint_desc gr_paint_get_desc(const gr_paint *p)
{
    return p->desc;
}

gr_stroke_style gr_stroke_style_default(void)
{
    return (gr_stroke_style){.width = 1, .cap = GR_CAP_BUTT, .join = GR_JOIN_MITER, .miter_limit = 10};
}

gr_stroke *gr_stroke_new(gr_paint *paint, const gr_stroke_style *style)
{
    if (!paint || !style || !isfinite(style->width) || style->width < 0 || !isfinite(style->miter_limit) ||
        style->miter_limit < 0 || !isfinite(style->dash_phase) || style->cap < GR_CAP_BUTT ||
        style->cap > GR_CAP_SQUARE || style->join < GR_JOIN_MITER || style->join > GR_JOIN_BEVEL ||
        (style->dash.count && !style->dash.data) || style->dash.count > SIZE_MAX / sizeof(double))
        return NULL;
    bool positive = false;
    for (size_t i = 0; i < style->dash.count; ++i)
    {
        double d = style->dash.data[i];
        if (!isfinite(d) || d < 0)
            return NULL;
        positive |= d > 0;
    }
    if (style->dash.count && !positive)
        return NULL;
    gr_stroke *s = gr_alloc(1, sizeof(*s));
    atomic_init(&s->refs, 1);
    s->paint = gr_paint_ref(paint);
    s->style = *style;
    s->style.dash.data = gr_copy(style->dash.data, style->dash.count, sizeof(double));
    return s;
}

gr_stroke *gr_stroke_ref(gr_stroke *s)
{
    if (s)
        gr_shared_ref(&s->refs);
    return s;
}

void gr_stroke_unref(gr_stroke *s)
{
    if (!s || !gr_shared_unref(&s->refs))
        return;
    gr_paint_unref(s->paint);
    free((void *)s->style.dash.data);
    free(s);
}

gr_paint *gr_stroke_get_paint(const gr_stroke *s)
{
    return s->paint;
}

gr_stroke_style gr_stroke_get_style(const gr_stroke *s)
{
    return s->style;
}

gr_image *gr_image_new(uint32_t width, uint32_t height, double scale, gr_bytes pixels)
{
    if (!isfinite(scale) || scale <= 0 || (width && height > SIZE_MAX / width))
        return NULL;
    size_t count = (size_t)width * height;
    if (count > SIZE_MAX / 4 || pixels.count != count * 4 || (pixels.count && !pixels.data))
        return NULL;
    gr_image *image = gr_alloc(1, sizeof(*image));
    atomic_init(&image->refs, 1);
    image->id = gr_next_id();
    image->width = width;
    image->height = height;
    image->scale = scale;
    image->pixels = (gr_bytes){gr_copy(pixels.data, pixels.count, 1), pixels.count};
    return image;
}

gr_image *gr_image_ref(gr_image *image)
{
    if (image)
        gr_shared_ref(&image->refs);
    return image;
}

void gr_image_unref(gr_image *image)
{
    if (!image || !gr_shared_unref(&image->refs))
        return;
    free((void *)image->pixels.data);
    free(image);
}

uint32_t gr_image_get_width(const gr_image *image)
{
    return image->width;
}

uint32_t gr_image_get_height(const gr_image *image)
{
    return image->height;
}

double gr_image_get_scale(const gr_image *image)
{
    return image->scale;
}

gr_bytes gr_image_get_pixels(const gr_image *image)
{
    return image->pixels;
}

uint64_t gr_image_get_id(const gr_image *image)
{
    return image->id;
}
