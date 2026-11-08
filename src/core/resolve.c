#include "model.h"

gr_affine gr_model_transform(const gr_model_props *p, gr_size size)
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

gr_layer_props gr_model_props_of(const gr_layer *l)
{
    const gr_model_props *m = &l->props;
    const gr_layer_extras *e = gr_model_extras(l);
    return (gr_layer_props){.bounds = m->bounds,
                            .position = m->position,
                            .anchor_point = m->anchor_point,
                            .transform = m->transform,
                            .opacity = m->opacity,
                            .hidden = m->hidden,
                            .masks_to_bounds = m->masks_to_bounds,
                            .has_background = m->has_background,
                            .background = m->background,
                            .corner_radius = m->corner_radius,
                            .masked_corners = m->masked_corners,
                            .has_border = e->has_border,
                            .border = e->border,
                            .has_shadow = e->has_shadow,
                            .shadow = e->shadow,
                            .has_mask = e->has_mask,
                            .mask = e->mask,
                            .blur_sigma = e->blur_sigma};
}

/* Snapping is local to each layer: translation, bounds origin, axis-aligned size and border width move to the
 * device-pixel grid, keeping at least one pixel for nonzero lengths. */
gr_resolved gr_model_resolve(const gr_layer *l)
{
    double scale = l->scene->scale;
    gr_resolved r = {.props = gr_model_props_of(l)};
    gr_layer_props *p = &r.props;
    if (p->transform.a == 1 && p->transform.d == 1 && p->transform.b == 0 && p->transform.c == 0)
    {
        p->bounds.size.width = snap_length(p->bounds.size.width, scale);
        p->bounds.size.height = snap_length(p->bounds.size.height, scale);
    }
    p->bounds.origin.x = snap(p->bounds.origin.x, scale);
    p->bounds.origin.y = snap(p->bounds.origin.y, scale);
    if (p->has_border)
        p->border.width = snap_length(p->border.width, scale);
    if (l == l->scene->root)
        r.transform = (gr_affine){1, 0, 0, 1, 0, 0};
    else
    {
        r.transform = gr_model_transform(&l->props, p->bounds.size);
        r.transform.tx = snap(r.transform.tx, scale);
        r.transform.ty = snap(r.transform.ty, scale);
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
                r.content.value.image.rect = place(l->gravity, size, p->bounds.size);
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
    return (gr_draw_info){r->props.bounds, scale};
}

gr_path *gr_resolved_new_shadow_path(const gr_resolved *r)
{
    const gr_layer_props *p = &r->props;
    if (!p->has_shadow)
        return NULL;
    gr_path_builder *b = gr_path_builder_new();
    gr_result result = GR_OK;
    if (!p->shadow.path)
        result =
            gr_path_builder_add_rounded_rect(b, (gr_rect){{0, 0}, p->bounds.size}, p->corner_radius, p->masked_corners);
    else
    {
        gr_path_element_list elements = gr_path_get_elements(p->shadow.path);
        for (size_t i = 0; i < elements.count && result == GR_OK; ++i)
        {
            gr_path_element e = elements.data[i];
            unsigned count = e.verb == GR_PATH_CLOSE ? 0 : e.verb == GR_PATH_CUBIC ? 3 : e.verb == GR_PATH_QUAD ? 2 : 1;
            for (unsigned j = 0; j < count; ++j)
            {
                e.p[j].x -= p->bounds.origin.x;
                e.p[j].y -= p->bounds.origin.y;
            }
            result = gr_path_builder_append(b, (gr_path_element_list){&e, 1});
        }
    }
    gr_path *path = result == GR_OK ? gr_path_builder_finish(b) : NULL;
    gr_path_builder_unref(b);
    return path;
}
