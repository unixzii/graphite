#include "model.h"

static gr_result changed(gr_layer *l, gr_dirty_bits bits)
{
    gr_model_mark(l, bits);
    gr_model_flush(l->scene);
    return GR_OK;
}

gr_layer_kind gr_layer_get_kind(const gr_layer *l)
{
    return l->kind;
}

bool gr_layer_is_alive(const gr_layer *l)
{
    return l->scene != NULL;
}

gr_scene *gr_layer_get_scene(const gr_layer *l)
{
    return l->scene;
}

gr_point gr_layer_get_position(const gr_layer *l)
{
    return l->scene ? l->props.position : ((gr_point){0, 0});
}

gr_result gr_layer_set_position(gr_layer *l, gr_point value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(gr_valid_point(value)))
        return GR_ERROR_ARGUMENT;
    if (gr_point_equal(l->props.position, value))
        return GR_OK;
    l->props.position = value;
    return changed(l, GR_DIRTY_GEOMETRY);
}

gr_point gr_layer_get_anchor_point(const gr_layer *l)
{
    return l->scene ? l->props.anchor_point : ((gr_point){0.5, 0.5});
}

gr_result gr_layer_set_anchor_point(gr_layer *l, gr_point value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(gr_valid_point(value)))
        return GR_ERROR_ARGUMENT;
    if (gr_point_equal(l->props.anchor_point, value))
        return GR_OK;
    l->props.anchor_point = value;
    return changed(l, GR_DIRTY_GEOMETRY);
}

gr_affine gr_layer_get_transform(const gr_layer *l)
{
    return l->scene ? l->props.transform : ((gr_affine){1, 0, 0, 1, 0, 0});
}

gr_result gr_layer_set_transform(gr_layer *l, gr_affine value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(isfinite(value.a) && isfinite(value.b) && isfinite(value.c) && isfinite(value.d) && isfinite(value.tx) &&
          isfinite(value.ty)))
        return GR_ERROR_ARGUMENT;
    if (gr_affine_equal(l->props.transform, value))
        return GR_OK;
    l->props.transform = value;
    return changed(l, GR_DIRTY_GEOMETRY);
}

float gr_layer_get_opacity(const gr_layer *l)
{
    return l->scene ? l->props.opacity : 1;
}

gr_result gr_layer_set_opacity(gr_layer *l, float value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(isfinite(value)))
        return GR_ERROR_ARGUMENT;
    value = fminf(1, fmaxf(0, value));
    if (l->props.opacity == value)
        return GR_OK;
    l->props.opacity = value;
    return changed(l, GR_DIRTY_APPEARANCE);
}

bool gr_layer_get_hidden(const gr_layer *l)
{
    return l->scene ? l->props.hidden : false;
}

gr_result gr_layer_set_hidden(gr_layer *l, bool value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(true))
        return GR_ERROR_ARGUMENT;
    if (l->props.hidden == value)
        return GR_OK;
    l->props.hidden = value;
    return changed(l, GR_DIRTY_APPEARANCE);
}

bool gr_layer_get_masks_to_bounds(const gr_layer *l)
{
    return l->scene ? l->props.masks_to_bounds : false;
}

gr_result gr_layer_set_masks_to_bounds(gr_layer *l, bool value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(true))
        return GR_ERROR_ARGUMENT;
    if (l->props.masks_to_bounds == value)
        return GR_OK;
    l->props.masks_to_bounds = value;
    return changed(l, GR_DIRTY_APPEARANCE);
}

double gr_layer_get_corner_radius(const gr_layer *l)
{
    return l->scene ? l->props.corner_radius : 0;
}

gr_result gr_layer_set_corner_radius(gr_layer *l, double value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(isfinite(value) && value >= 0))
        return GR_ERROR_ARGUMENT;
    if (l->props.corner_radius == value)
        return GR_OK;
    l->props.corner_radius = value;
    return changed(l, GR_DIRTY_APPEARANCE | gr_model_corner_bits(l));
}

gr_corners gr_layer_get_masked_corners(const gr_layer *l)
{
    return l->scene ? l->props.masked_corners : GR_CORNERS_ALL;
}

gr_result gr_layer_set_masked_corners(gr_layer *l, gr_corners value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(!(value & ~GR_CORNERS_ALL)))
        return GR_ERROR_ARGUMENT;
    if (l->props.masked_corners == value)
        return GR_OK;
    l->props.masked_corners = value;
    return changed(l, GR_DIRTY_APPEARANCE | gr_model_corner_bits(l));
}

gr_rect gr_layer_get_bounds(const gr_layer *l)
{
    return l->scene ? l->props.bounds : (gr_rect){0};
}

gr_result gr_layer_set_bounds(gr_layer *l, gr_rect value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!gr_valid_rect(value))
        return GR_ERROR_ARGUMENT;
    gr_rect old = l->props.bounds;
    if (gr_rect_equal(old, value))
        return GR_OK;
    gr_dirty_bits bits = GR_DIRTY_GEOMETRY;
    if (!gr_size_equal(old.size, value.size))
        bits |= gr_model_size_bits(l);
    if (!gr_point_equal(old.origin, value.origin))
    {
        if (l->props.has_shadow && l->props.shadow.path)
            bits |= GR_DIRTY_SHADOW;
        if (l->drawer.draw)
            bits |= GR_DIRTY_CONTENT;
    }
    l->props.bounds = value;
    return changed(l, bits);
}

gr_rect gr_layer_get_frame(const gr_layer *l)
{
    if (!l->scene)
        return (gr_rect){0};
    gr_size size = l->props.bounds.size;
    gr_affine m = gr_model_transform(&l->props, size);
    double x[] = {m.tx, m.a * size.width + m.tx, m.c * size.height + m.tx, m.a * size.width + m.c * size.height + m.tx};
    double y[] = {m.ty, m.b * size.width + m.ty, m.d * size.height + m.ty, m.b * size.width + m.d * size.height + m.ty};
    double x0 = x[0], x1 = x[0], y0 = y[0], y1 = y[0];
    for (unsigned i = 1; i < 4; ++i)
    {
        x0 = fmin(x0, x[i]);
        x1 = fmax(x1, x[i]);
        y0 = fmin(y0, y[i]);
        y1 = fmax(y1, y[i]);
    }
    return (gr_rect){{x0, y0}, {x1 - x0, y1 - y0}};
}

gr_result gr_layer_set_frame(gr_layer *l, gr_rect r)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!gr_valid_rect(r))
        return GR_ERROR_ARGUMENT;
    gr_point position = {r.origin.x + l->props.anchor_point.x * r.size.width,
                         r.origin.y + l->props.anchor_point.y * r.size.height};
    if (!gr_valid_point(position))
        return GR_ERROR_ARGUMENT;
    bool size_changed = !gr_size_equal(r.size, l->props.bounds.size);
    if (!size_changed && gr_point_equal(position, l->props.position))
        return GR_OK;
    l->props.position = position;
    l->props.bounds.size = r.size;
    return changed(l, GR_DIRTY_GEOMETRY | (size_changed ? gr_model_size_bits(l) : 0));
}

bool gr_layer_get_background(const gr_layer *l, gr_color *out)
{
    bool present = l->scene && l->props.has_background;
    *out = present ? l->props.background : (gr_color){0};
    return present;
}

gr_result gr_layer_set_background(gr_layer *l, const gr_color *value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (value && !(gr_valid_color(*value)))
        return GR_ERROR_ARGUMENT;
    if (l->props.has_background == (value != NULL) && (!value || (gr_color_equal(l->props.background, *value))))
        return GR_OK;
    gr_color next = value ? *value : (gr_color){0};
    l->props.has_background = value != NULL;
    l->props.background = next;
    return changed(l, GR_DIRTY_APPEARANCE);
}

bool gr_layer_get_border(const gr_layer *l, gr_border *out)
{
    bool present = l->scene && l->props.has_border;
    *out = present ? l->props.border : (gr_border){0};
    return present;
}

gr_result gr_layer_set_border(gr_layer *l, const gr_border *value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (value && !(isfinite(value->width) && value->width >= 0 && gr_valid_color(value->color)))
        return GR_ERROR_ARGUMENT;
    if (l->props.has_border == (value != NULL) &&
        (!value || (l->props.border.width == value->width && gr_color_equal(l->props.border.color, value->color))))
        return GR_OK;
    gr_border next = value ? *value : (gr_border){0};
    l->props.has_border = value != NULL;
    l->props.border = next;
    return changed(l, GR_DIRTY_APPEARANCE);
}

double gr_layer_get_blur(const gr_layer *l)
{
    return l->scene ? l->props.blur_sigma : 0;
}

gr_result gr_layer_set_blur(gr_layer *l, double value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!(isfinite(value) && value >= 0))
        return GR_ERROR_ARGUMENT;
    if (l->props.blur_sigma == value)
        return GR_OK;
    l->props.blur_sigma = value;
    return changed(l, GR_DIRTY_APPEARANCE);
}

bool gr_layer_get_shadow(const gr_layer *l, gr_shadow *out)
{
    bool present = l->scene && l->props.has_shadow;
    *out = present ? l->props.shadow : (gr_shadow){0};
    return present;
}

gr_result gr_layer_set_shadow(gr_layer *l, const gr_shadow *value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (value && !(gr_valid_color(value->color) && gr_valid_point(value->offset) && isfinite(value->blur_sigma) &&
                   value->blur_sigma >= 0))
        return GR_ERROR_ARGUMENT;
    if (l->props.has_shadow == (value != NULL) &&
        (!value || (gr_color_equal(l->props.shadow.color, value->color) &&
                    gr_point_equal(l->props.shadow.offset, value->offset) &&
                    l->props.shadow.blur_sigma == value->blur_sigma && l->props.shadow.path == value->path)))
        return GR_OK;
    gr_shadow next = value ? *value : (gr_shadow){0};
    gr_path *old = l->props.has_shadow ? l->props.shadow.path : NULL;
    gr_path_ref(next.path);
    l->props.has_shadow = value != NULL;
    l->props.shadow = next;
    gr_path_unref(old);
    return changed(l, GR_DIRTY_SHADOW);
}

bool gr_layer_get_mask(const gr_layer *l, gr_mask *out)
{
    bool present = l->scene && l->props.has_mask;
    *out = present ? l->props.mask : (gr_mask){0};
    return present;
}

static bool valid_mask(const gr_mask *m)
{
    switch (m->kind)
    {
        case GR_MASK_PATH:
            return m->value.path.path && gr_valid_rule(m->value.path.rule);
        case GR_MASK_GRADIENT:
            return m->value.gradient && gr_paint_get_desc(m->value.gradient).kind != GR_PAINT_SOLID;
    }
    return false;
}

static bool paint_equal(gr_paint *a, gr_paint *b);

static bool same_mask(const gr_mask *a, const gr_mask *b)
{
    if (a->kind != b->kind)
        return false;
    if (a->kind == GR_MASK_PATH)
        return a->value.path.path == b->value.path.path && a->value.path.rule == b->value.path.rule;
    return paint_equal(a->value.gradient, b->value.gradient);
}

void gr_model_mask_refs(const gr_mask *m, bool acquire)
{
    if (m->kind == GR_MASK_PATH)
    {
        if (acquire)
            gr_path_ref(m->value.path.path);
        else
            gr_path_unref(m->value.path.path);
    }
    else if (acquire)
        gr_paint_ref(m->value.gradient);
    else
        gr_paint_unref(m->value.gradient);
}

gr_result gr_layer_set_mask(gr_layer *l, const gr_mask *value)
{
    if (!l->scene)
        return GR_OK;
    if (gr_model_writable(l->scene) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (value && !valid_mask(value))
        return GR_ERROR_ARGUMENT;
    if (l->props.has_mask == (value != NULL) && (!value || same_mask(&l->props.mask, value)))
        return GR_OK;
    gr_mask old = l->props.mask;
    bool had = l->props.has_mask;
    if (value)
        gr_model_mask_refs(value, true);
    l->props.has_mask = value != NULL;
    l->props.mask = value ? *value : (gr_mask){0};
    if (had)
        gr_model_mask_refs(&old, false);
    return changed(l, GR_DIRTY_MASK);
}

static bool ancestor(gr_layer *node, gr_layer *candidate)
{
    for (; node; node = node->parent)
        if (node == candidate)
            return true;
    return false;
}

static void detach(gr_layer *l)
{
    gr_layer *parent = l->parent;
    if (!parent)
        return;
    size_t i = 0;
    while (i < parent->child_count && parent->children[i] != l)
        ++i;
    if (i == parent->child_count)
        abort();
    memmove(parent->children + i, parent->children + i + 1, (parent->child_count - i - 1) * sizeof(*parent->children));
    --parent->child_count;
    gr_model_mark(parent, GR_DIRTY_CHILDREN);
    l->parent = NULL;
    if (l->attached)
        gr_model_attach(l, false);
    gr_layer_unref(l);
}

size_t gr_layer_get_child_count(const gr_layer *l)
{
    return l->scene ? l->child_count : 0;
}

gr_layer *gr_layer_get_child(const gr_layer *l, size_t index)
{
    return l->scene && index < l->child_count ? l->children[index] : NULL;
}

gr_layer *gr_layer_get_parent(const gr_layer *l)
{
    return l->scene ? l->parent : NULL;
}

gr_result gr_layer_insert_child(gr_layer *parent, gr_layer *child, size_t index)
{
    gr_scene *s = parent->scene;
    if (!s || !child->scene)
        return GR_OK;
    if (gr_model_writable(s) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (child->scene != s)
        return GR_ERROR_SCENE;
    if (child == s->root)
        return GR_ERROR_STATE;
    if (ancestor(parent, child))
        return GR_ERROR_CYCLE;
    /* Transfer the old parent's ownership without invoking cleanup midway. */
    gr_layer_ref(child);
    detach(child);
    void *data = parent->children;
    gr_reserve(&data, &parent->child_capacity, parent->child_count + 1, sizeof(*parent->children));
    parent->children = data;
    if (index > parent->child_count)
        index = parent->child_count;
    memmove(parent->children + index + 1, parent->children + index,
            (parent->child_count - index) * sizeof(*parent->children));
    parent->children[index] = child;
    ++parent->child_count;
    child->parent = parent;
    gr_model_mark(parent, GR_DIRTY_CHILDREN);
    if (child->attached != parent->attached)
        gr_model_attach(child, parent->attached);
    gr_model_flush(s);
    return GR_OK;
}

gr_result gr_layer_add_child(gr_layer *parent, gr_layer *child)
{
    return gr_layer_insert_child(parent, child, SIZE_MAX);
}

gr_result gr_layer_remove_from_parent(gr_layer *l)
{
    gr_scene *s = l->scene;
    if (!s)
        return GR_OK;
    if (gr_model_writable(s) != GR_OK)
        return GR_ERROR_REENTRANT;
    /* Pin Scene across cleanup callbacks that may release their Scene owner. */
    gr_scene_ref(s);
    /* The explicit pin also permits passing a valid parent-borrowed child. */
    gr_layer_ref(l);
    detach(l);
    gr_layer_unref(l);
    gr_model_flush(s);
    gr_scene_unref(s);
    return GR_OK;
}

static gr_result kind_writable(gr_layer *l, gr_layer_kind kind)
{
    if (l->kind != kind)
        return GR_ERROR_KIND;
    return gr_model_writable(l->scene);
}

gr_image *gr_content_layer_get_image(const gr_layer *l)
{
    return l->scene ? l->image : NULL;
}

gr_gravity gr_content_layer_get_gravity(const gr_layer *l)
{
    return l->scene ? l->gravity : GR_GRAVITY_RESIZE;
}

gr_result gr_content_layer_set_gravity(gr_layer *l, gr_gravity gravity)
{
    gr_result result = kind_writable(l, GR_LAYER_CONTENT);
    if (result != GR_OK || !l->scene)
        return result;
    if (gravity < GR_GRAVITY_CENTER || gravity > GR_GRAVITY_ASPECT_FILL)
        return GR_ERROR_ARGUMENT;
    if (l->gravity == gravity)
        return GR_OK;
    l->gravity = gravity;
    return changed(l, GR_DIRTY_CONTENT);
}

static void destroy_drawer(gr_scene *s, gr_drawer drawer)
{
    gr_scene_ref(s);
    if (drawer.destroy)
        drawer.destroy(drawer.userdata);
    gr_model_flush(s);
    gr_scene_unref(s);
}

gr_result gr_content_layer_set_image(gr_layer *l, gr_image *image)
{
    gr_result result = kind_writable(l, GR_LAYER_CONTENT);
    if (result != GR_OK || !l->scene)
        return result;
    if (l->image == image && !l->drawer.draw)
        return GR_OK;
    gr_image *old_image = l->image;
    gr_drawer old = l->drawer;
    l->image = gr_image_ref(image);
    l->drawer = (gr_drawer){0};
    gr_model_mark(l, GR_DIRTY_CONTENT);
    gr_image_unref(old_image);
    destroy_drawer(l->scene, old);
    return GR_OK;
}

gr_result gr_content_layer_set_drawer(gr_layer *l, const gr_drawer *drawer)
{
    gr_result result = kind_writable(l, GR_LAYER_CONTENT);
    if (result != GR_OK)
        return result;
    if (!l->scene)
        return GR_ERROR_STATE;
    if (!drawer || !drawer->draw)
        return GR_ERROR_ARGUMENT;
    gr_drawer old = l->drawer;
    gr_image *image = l->image;
    l->drawer = *drawer;
    l->image = NULL;
    if (++l->display_version == 0)
        abort();
    gr_model_mark(l, GR_DIRTY_CONTENT);
    gr_image_unref(image);
    destroy_drawer(l->scene, old);
    return GR_OK;
}

gr_result gr_content_layer_clear_drawer(gr_layer *l)
{
    gr_result result = kind_writable(l, GR_LAYER_CONTENT);
    if (result != GR_OK || !l->scene)
        return result;
    if (!l->drawer.draw)
        return GR_OK;
    gr_drawer old = l->drawer;
    l->drawer = (gr_drawer){0};
    gr_model_mark(l, GR_DIRTY_CONTENT);
    destroy_drawer(l->scene, old);
    return GR_OK;
}

bool gr_content_layer_has_drawer(const gr_layer *l)
{
    return l->scene && l->drawer.draw;
}

gr_result gr_content_layer_set_needs_display(gr_layer *l)
{
    gr_result result = kind_writable(l, GR_LAYER_CONTENT);
    if (result != GR_OK || !l->scene)
        return result;
    if (!l->drawer.draw)
        return GR_OK;
    if (++l->display_version == 0)
        abort();
    return changed(l, GR_DIRTY_CONTENT);
}

static bool paint_equal(gr_paint *a, gr_paint *b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    gr_paint_desc x = gr_paint_get_desc(a), y = gr_paint_get_desc(b);
    if (x.kind != y.kind)
        return false;
    if (x.kind == GR_PAINT_SOLID)
        return gr_color_equal(x.value.solid, y.value.solid);
    gr_gradient_stop_list xs, ys;
    if (x.kind == GR_PAINT_LINEAR)
    {
        if (!gr_point_equal(x.value.linear.start, y.value.linear.start) ||
            !gr_point_equal(x.value.linear.end, y.value.linear.end))
            return false;
        xs = x.value.linear.stops;
        ys = y.value.linear.stops;
    }
    else
    {
        if (!gr_point_equal(x.value.radial.center, y.value.radial.center) ||
            x.value.radial.radius != y.value.radial.radius)
            return false;
        xs = x.value.radial.stops;
        ys = y.value.radial.stops;
    }
    if (xs.count != ys.count)
        return false;
    for (size_t i = 0; i < xs.count; ++i)
        if (xs.data[i].offset != ys.data[i].offset || !gr_color_equal(xs.data[i].color, ys.data[i].color))
            return false;
    return true;
}

static bool stroke_equal(gr_stroke *a, gr_stroke *b)
{
    if (a == b)
        return true;
    if (!a || !b || !paint_equal(gr_stroke_get_paint(a), gr_stroke_get_paint(b)))
        return false;
    gr_stroke_style x = gr_stroke_get_style(a), y = gr_stroke_get_style(b);
    if (x.width != y.width || x.cap != y.cap || x.join != y.join || x.miter_limit != y.miter_limit ||
        x.dash_phase != y.dash_phase || x.dash.count != y.dash.count)
        return false;
    for (size_t i = 0; i < x.dash.count; ++i)
        if (x.dash.data[i] != y.dash.data[i])
            return false;
    return true;
}

gr_path *gr_shape_layer_get_path(const gr_layer *l)
{
    return l->scene ? l->shape.path : NULL;
}

gr_result gr_shape_layer_set_path(gr_layer *l, gr_path *value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (l->shape.path == value)
        return GR_OK;
    gr_path *old = l->shape.path;
    l->shape.path = gr_path_ref(value);
    gr_path_unref(old);
    return changed(l, GR_DIRTY_CONTENT);
}

gr_paint *gr_shape_layer_get_fill(const gr_layer *l)
{
    return l->scene ? l->shape.fill : NULL;
}

gr_result gr_shape_layer_set_fill(gr_layer *l, gr_paint *value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (value && gr_paint_get_desc(value).kind != GR_PAINT_SOLID)
        return GR_ERROR_ARGUMENT;
    if (paint_equal(l->shape.fill, value))
        return GR_OK;
    gr_paint *old = l->shape.fill;
    l->shape.fill = gr_paint_ref(value);
    gr_paint_unref(old);
    return changed(l, GR_DIRTY_CONTENT);
}

gr_stroke *gr_shape_layer_get_stroke(const gr_layer *l)
{
    return l->scene ? l->shape.stroke : NULL;
}

gr_result gr_shape_layer_set_stroke(gr_layer *l, gr_stroke *value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (value && gr_paint_get_desc(gr_stroke_get_paint(value)).kind != GR_PAINT_SOLID)
        return GR_ERROR_ARGUMENT;
    if (stroke_equal(l->shape.stroke, value))
        return GR_OK;
    gr_stroke *old = l->shape.stroke;
    l->shape.stroke = gr_stroke_ref(value);
    gr_stroke_unref(old);
    return changed(l, GR_DIRTY_CONTENT);
}

gr_paint *gr_gradient_layer_get_gradient(const gr_layer *l)
{
    return l->scene ? l->gradient : NULL;
}

gr_result gr_gradient_layer_set_gradient(gr_layer *l, gr_paint *value)
{
    gr_result result = kind_writable(l, GR_LAYER_GRADIENT);
    if (result != GR_OK || !l->scene)
        return result;
    if (value && gr_paint_get_desc(value).kind == GR_PAINT_SOLID)
        return GR_ERROR_ARGUMENT;
    if (paint_equal(l->gradient, value))
        return GR_OK;
    gr_paint *old = l->gradient;
    l->gradient = gr_paint_ref(value);
    gr_paint_unref(old);
    return changed(l, GR_DIRTY_CONTENT);
}

gr_fill_rule gr_shape_layer_get_fill_rule(const gr_layer *l)
{
    return l->scene ? l->shape.fill_rule : GR_FILL_NONZERO;
}

gr_result gr_shape_layer_set_fill_rule(gr_layer *l, gr_fill_rule value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (!(gr_valid_rule(value)))
        return GR_ERROR_ARGUMENT;
    if (l->shape.fill_rule == value)
        return GR_OK;
    l->shape.fill_rule = value;
    return changed(l, GR_DIRTY_CONTENT);
}

double gr_shape_layer_get_stroke_start(const gr_layer *l)
{
    return l->scene ? l->shape.stroke_start : 0;
}

gr_result gr_shape_layer_set_stroke_start(gr_layer *l, double value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (!(isfinite(value)))
        return GR_ERROR_ARGUMENT;
    value = fmin(1, fmax(0, value));
    if (l->shape.stroke_start == value)
        return GR_OK;
    l->shape.stroke_start = value;
    return changed(l, GR_DIRTY_CONTENT);
}

double gr_shape_layer_get_stroke_end(const gr_layer *l)
{
    return l->scene ? l->shape.stroke_end : 1;
}

gr_result gr_shape_layer_set_stroke_end(gr_layer *l, double value)
{
    gr_result result = kind_writable(l, GR_LAYER_SHAPE);
    if (result != GR_OK || !l->scene)
        return result;
    if (!(isfinite(value)))
        return GR_ERROR_ARGUMENT;
    value = fmin(1, fmax(0, value));
    if (l->shape.stroke_end == value)
        return GR_OK;
    l->shape.stroke_end = value;
    return changed(l, GR_DIRTY_CONTENT);
}
