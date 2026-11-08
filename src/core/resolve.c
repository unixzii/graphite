#include "model.h"

gr_affine gr_model_transform(const gr_layer_props *p, gr_size size)
{
    gr_affine m = p->transform;
    double x = p->anchor_point.x * size.width, y = p->anchor_point.y * size.height;
    m.tx += p->position.x - m.a * x - m.c * y;
    m.ty += p->position.y - m.b * x - m.d * y;
    return m;
}

static double snap(double x, double scale)
{
    return round(x * scale) / scale;
}

static double snap_length(double x, double scale)
{
    return x > 0 ? fmax(round(x * scale), 1) / scale : x;
}

static gr_rect place(gr_gravity gravity, gr_size content, gr_size size)
{
    double w = content.width, h = content.height;
    if (gravity == GR_GRAVITY_RESIZE)
        return (gr_rect){{0, 0}, size};
    if (gravity == GR_GRAVITY_ASPECT || gravity == GR_GRAVITY_ASPECT_FILL)
    {
        if (w <= 0 || h <= 0)
            return (gr_rect){0};
        double x = size.width / w, y = size.height / h;
        double scale = gravity == GR_GRAVITY_ASPECT ? fmin(x, y) : fmax(x, y);
        w *= scale;
        h *= scale;
        return (gr_rect){{(size.width - w) / 2, (size.height - h) / 2}, {w, h}};
    }
    double x = (size.width - w) / 2, y = (size.height - h) / 2;
    switch (gravity)
    {
        case GR_GRAVITY_LEFT:
        case GR_GRAVITY_TOP_LEFT:
        case GR_GRAVITY_BOTTOM_LEFT:
            x = 0;
            break;
        case GR_GRAVITY_RIGHT:
        case GR_GRAVITY_TOP_RIGHT:
        case GR_GRAVITY_BOTTOM_RIGHT:
            x = size.width - w;
            break;
        default:
            break;
    }
    switch (gravity)
    {
        case GR_GRAVITY_TOP:
        case GR_GRAVITY_TOP_LEFT:
        case GR_GRAVITY_TOP_RIGHT:
            y = 0;
            break;
        case GR_GRAVITY_BOTTOM:
        case GR_GRAVITY_BOTTOM_LEFT:
        case GR_GRAVITY_BOTTOM_RIGHT:
            y = size.height - h;
            break;
        default:
            break;
    }
    return (gr_rect){{x, y}, {w, h}};
}

gr_resolved gr_model_resolve(const gr_layer *l)
{
    const gr_layer_props *p = &l->props;
    double scale = l->scene->scale;
    bool axis_aligned = p->transform.a == 1 && p->transform.d == 1 && p->transform.b == 0 && p->transform.c == 0;
    gr_resolved r = {.props = *p,
                     .size = p->bounds.size,
                     .has_border = p->has_border,
                     .border = p->border,
                     .bounds_offset = {-p->bounds.origin.x, -p->bounds.origin.y},
                     .has_shadow = p->has_shadow};
    if (axis_aligned)
    {
        r.size.width = snap_length(r.size.width, scale);
        r.size.height = snap_length(r.size.height, scale);
    }
    if (r.has_border)
        r.border.width = snap_length(r.border.width, scale);
    r.transform = l == l->scene->root ? (gr_affine){1, 0, 0, 1, 0, 0} : gr_model_transform(p, r.size);
    {
        double tx = snap(r.transform.tx, scale), ty = snap(r.transform.ty, scale);
        r.snap_offset = (gr_point){tx - r.transform.tx, ty - r.transform.ty};
        r.transform.tx = tx;
        r.transform.ty = ty;
        r.bounds_offset.x = snap(r.bounds_offset.x, scale);
        r.bounds_offset.y = snap(r.bounds_offset.y, scale);
    }
    if (p->has_shadow)
    {
        if (p->shadow.path)
        {
            r.shadow_shape.kind = GR_SHADOW_PATH;
            r.shadow_shape.value.path = p->shadow.path;
        }
        else
        {
            r.shadow_shape.kind = GR_SHADOW_ROUNDED_RECT;
            r.shadow_shape.value.rounded_rect.rect = (gr_rect){{0, 0}, r.size};
            r.shadow_shape.value.rounded_rect.radius = p->corner_radius;
            r.shadow_shape.value.rounded_rect.corners = p->masked_corners;
        }
    }
    switch (l->kind)
    {
        case GR_LAYER_CONTENT:
            if (l->drawer.draw)
            {
                r.content.kind = GR_CONTENT_DRAWN;
                r.content.value.display_version = l->display_version;
            }
            else if (l->image)
            {
                r.content.kind = GR_CONTENT_IMAGE;
                r.content.value.image.image = l->image;
                double image_scale = gr_image_get_scale(l->image);
                gr_size size = {gr_image_get_width(l->image) / image_scale,
                                gr_image_get_height(l->image) / image_scale};
                r.content.value.image.rect = place(l->gravity, size, r.size);
            }
            break;
        case GR_LAYER_SHAPE:
            r.content.kind = GR_CONTENT_SHAPE;
            r.content.value.shape = l->shape;
            break;
        case GR_LAYER_GRADIENT:
            if (l->gradient)
            {
                r.content.kind = GR_CONTENT_GRADIENT;
                r.content.value.gradient = l->gradient;
            }
            break;
        case GR_LAYER_CONTAINER:
            break;
    }
    return r;
}

gr_draw_info gr_resolved_get_draw_info(const gr_resolved *r, double scale)
{
    return (gr_draw_info){{{-r->bounds_offset.x, -r->bounds_offset.y}, r->size}, scale};
}

gr_path *gr_shadow_shape_new_path(const gr_shadow_shape *shape, gr_point offset)
{
    if (!shape || !gr_valid_point(offset))
        return NULL;
    gr_path_builder *b = gr_path_builder_new();
    gr_result result = GR_ERROR_ARGUMENT;
    if (shape->kind == GR_SHADOW_ROUNDED_RECT)
    {
        gr_rect r = shape->value.rounded_rect.rect;
        double radius = shape->value.rounded_rect.radius;
        if (gr_valid_rect(r) && isfinite(radius) && radius >= 0)
            result = gr_path_builder_add_rounded_rect(b, r, radius, shape->value.rounded_rect.corners);
    }
    else if (shape->kind == GR_SHADOW_PATH && shape->value.path)
    {
        gr_path_element_list elements = gr_path_get_elements(shape->value.path);
        result = GR_OK;
        for (size_t i = 0; i < elements.count && result == GR_OK; ++i)
        {
            gr_path_element e = elements.data[i];
            unsigned count = e.verb == GR_PATH_CLOSE ? 0 : e.verb == GR_PATH_CUBIC ? 3 : e.verb == GR_PATH_QUAD ? 2 : 1;
            for (unsigned j = 0; j < count; ++j)
            {
                e.p[j].x += offset.x;
                e.p[j].y += offset.y;
            }
            result = gr_path_builder_append(b, (gr_path_element_list){&e, 1});
        }
    }
    gr_path *path = result == GR_OK ? gr_path_builder_finish(b) : NULL;
    gr_path_builder_unref(b);
    return path;
}
