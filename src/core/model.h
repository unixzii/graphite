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

/* The properties every layer stores. gr_model_props builds the full gr_layer_props snapshot. */
typedef struct
{
    gr_rect bounds;
    gr_point position, anchor_point;
    gr_affine transform;
    gr_color background;
    double corner_radius;
    float opacity;
    uint8_t masked_corners;
    bool hidden : 1, masks_to_bounds : 1, has_background : 1;
} gr_model_props;

/* Properties most layers leave unset, allocated on first use and freed when all are unset again. */
typedef struct
{
    bool has_border, has_shadow, has_mask;
    gr_border border;
    gr_shadow shadow;
    gr_mask mask;
    double blur_sigma;
} gr_layer_extras;

struct gr_layer
{
    size_t refs;
    gr_scene *scene;
    gr_layer *previous, *next;             /* All layers of the Scene. */
    gr_layer *dirty_previous, *dirty_next; /* The Scene's dirty queue. */
    gr_layer *pending_next;                /* Released layers awaiting retirement. */
    gr_layer *parent;
    gr_layer **children;
    size_t child_count, child_capacity;
    uint8_t kind, gravity, dirty;
    bool queued : 1, attached : 1, realized : 1;
    gr_model_props props;
    gr_layer_extras *extras;

    /* By kind: content layers use image, drawer and display_version; shape layers shape; gradient layers gradient. */
    union
    {
        struct
        {
            gr_image *image;
            gr_drawer drawer;
            uint64_t display_version;
        };

        gr_shape_props shape;
        gr_paint *gradient;
    };
};

struct gr_scene
{
    size_t refs, layer_count;
    bool closing, busy, drawing, draining, requested, has_backend;
    gr_layer *root, *layers, *dirty_head, *dirty_tail, *pending;
    gr_layer **released;
    size_t released_count, released_capacity;
    double scale;
    gr_backend_vtable vtable;
    void *backend;
    gr_scene_link *link;
    gr_drawer_provider *provider;
};

extern const gr_layer_extras gr_model_no_extras;

gr_model_props gr_model_defaults(void);
gr_layer_props gr_model_props_of(const gr_layer *layer);
gr_result gr_model_writable(const gr_scene *s);
void gr_model_mark(gr_layer *layer, gr_dirty_bits bits);
void gr_model_flush(gr_scene *scene);
void gr_model_attach(gr_layer *layer, bool attached);
gr_dirty_bits gr_model_size_bits(const gr_layer *layer);
gr_dirty_bits gr_model_corner_bits(const gr_layer *layer);
void gr_model_drop_content(gr_layer *layer);
void gr_model_mask_refs(const gr_mask *mask, bool acquire);
gr_resolved gr_model_resolve(const gr_layer *layer);
gr_affine gr_model_transform(const gr_model_props *props, gr_size size);

/* Unset extras read as zero. */
static inline const gr_layer_extras *gr_model_extras(const gr_layer *l)
{
    return l->extras ? l->extras : &gr_model_no_extras;
}

static inline bool gr_model_has_drawer(const gr_layer *l)
{
    return l->kind == GR_LAYER_CONTENT && l->drawer.draw;
}

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
