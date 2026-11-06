#ifndef GRAPHITE_MODEL_H
#define GRAPHITE_MODEL_H
#include "internal.h"

#define GR_DIRTY_ALL ((gr_dirty_bits)63)

struct gr_scene_link
{
    size_t refs;
    gr_scene *scene;
};

struct gr_drawer_provider
{
    size_t refs;
    gr_scene_link *link;
};

struct gr_layer
{
    size_t refs;
    gr_scene *scene;
    gr_layer_kind kind;
    gr_layer *previous, *next;
    gr_layer *dirty_previous, *dirty_next;
    gr_layer *pending_next;
    bool queued, attached, realized, mask_parent;
    gr_dirty_bits dirty;
    gr_layer *parent, *mask;
    gr_layer **children;
    size_t child_count, child_capacity;
    gr_layer_props props;
    gr_shape_props shape;
    gr_image *image;
    gr_paint *gradient;
    gr_gravity gravity;
    gr_drawer drawer;
    uint64_t display_version;
};

struct gr_scene
{
    size_t refs, layer_count;
    bool closing, busy, drawing, draining, requested, has_backend;
    gr_layer *root, *layers, *dirty_head, *dirty_tail, *pending;
    gr_layer **released;
    size_t released_count, released_capacity;
    double scale;
    bool snapping;
    gr_backend_vtable vtable;
    void *backend;
    gr_scene_link *link;
    gr_drawer_provider *provider;
};

gr_layer_props gr_model_defaults(void);
gr_result gr_model_writable(const gr_scene *s);
void gr_model_mark(gr_layer *layer, gr_dirty_bits bits);
void gr_model_flush(gr_scene *scene);
void gr_model_attach(gr_layer *layer, bool attached);
gr_dirty_bits gr_model_size_bits(const gr_layer *layer);
gr_dirty_bits gr_model_corner_bits(const gr_layer *layer);
void gr_model_drop_content(gr_layer *layer);
gr_resolved gr_model_resolve(const gr_layer *layer);
gr_affine gr_model_transform(const gr_layer_props *props, gr_size size);

static inline bool gr_point_equal(gr_point a, gr_point b)
{
    return a.x == b.x && a.y == b.y;
}

static inline bool gr_size_equal(gr_size a, gr_size b)
{
    return a.width == b.width && a.height == b.height;
}

static inline bool gr_rect_equal(gr_rect a, gr_rect b)
{
    return gr_point_equal(a.origin, b.origin) && gr_size_equal(a.size, b.size);
}

static inline bool gr_color_equal(gr_color a, gr_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static inline bool gr_affine_equal(gr_affine a, gr_affine b)
{
    return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.tx == b.tx && a.ty == b.ty;
}

#endif
