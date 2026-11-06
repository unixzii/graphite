#include "internal.h"

typedef struct
{
    gr_layer *id, *parent, *mask, *mask_owner;
    GPtrArray *children;
    gr_resolved resolved;
    bool has_resolved, cached_valid, has_drawing;
    uint64_t image_id, display_version;
    gr_draw_info draw_info;
    GdkTexture *image;
    GskRenderNode *drawing, *shape, *cached;
} node;

typedef struct
{
    uint64_t id;
    size_t uses;
    GdkTexture *texture;
} texture_entry;

static void resolved_refs(gr_resolved *r, bool acquire)
{
    if (r->props.has_shadow)
    {
        if (acquire)
            gr_path_ref(r->props.shadow.path);
        else
            gr_path_unref(r->props.shadow.path);
    }
    switch (r->content.kind)
    {
        case GR_CONTENT_IMAGE:
            if (acquire)
                gr_image_ref(r->content.value.image.image);
            else
                gr_image_unref(r->content.value.image.image);
            break;
        case GR_CONTENT_SHAPE:
        {
            gr_shape_props *p = &r->content.value.shape;
            if (acquire)
            {
                gr_path_ref(p->path);
                gr_paint_ref(p->fill);
                gr_stroke_ref(p->stroke);
            }
            else
            {
                gr_path_unref(p->path);
                gr_paint_unref(p->fill);
                gr_stroke_unref(p->stroke);
            }
            break;
        }
        case GR_CONTENT_GRADIENT:
            if (acquire)
                gr_paint_ref(r->content.value.gradient);
            else
                gr_paint_unref(r->content.value.gradient);
            break;
        default:
            break;
    }
}

static void clear_node(GskRenderNode **n)
{
    if (*n)
        gsk_render_node_unref(*n);
    *n = NULL;
}

static void free_node(gpointer data)
{
    node *n = data;
    if (n->has_resolved)
        resolved_refs(&n->resolved, false);
    g_ptr_array_unref(n->children);
    g_clear_object(&n->image);
    clear_node(&n->drawing);
    clear_node(&n->shape);
    clear_node(&n->cached);
    g_free(n);
}

static void free_texture(gpointer data)
{
    texture_entry *entry = data;
    g_object_unref(entry->texture);
    g_free(entry);
}

gr_gtk_shared *gr_gtk_shared_new(gr_scene *scene)
{
    gr_gtk_shared *s = g_new0(gr_gtk_shared, 1);
    s->refs = 1;
    s->scene = gr_scene_get_link(scene);
    g_weak_ref_init(&s->widget, NULL);
    s->nodes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_node);
    s->textures = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, free_texture);
    return s;
}

gr_gtk_shared *gr_gtk_shared_ref(gr_gtk_shared *s)
{
    if (s)
    {
        if (s->refs == SIZE_MAX)
            abort();
        ++s->refs;
    }
    return s;
}

void gr_gtk_shared_unref(gr_gtk_shared *s)
{
    if (!s || --s->refs)
        return;
    g_hash_table_unref(s->nodes);
    g_hash_table_unref(s->textures);
    g_weak_ref_clear(&s->widget);
    gr_scene_link_unref(s->scene);
    g_free(s);
}

void gr_gtk_tree_clear(gr_gtk_shared *s)
{
    g_hash_table_remove_all(s->nodes);
    g_hash_table_remove_all(s->textures);
    s->root = NULL;
    s->closed = true;
    s->pending = false;
}

static node *lookup(gr_gtk_shared *s, gr_layer *id)
{
    return id ? g_hash_table_lookup(s->nodes, id) : NULL;
}

static node *required(gr_gtk_shared *s, gr_layer *id)
{
    node *n = lookup(s, id);
    if (!n)
        abort();
    return n;
}

static void invalidate(gr_gtk_shared *s, gr_layer *id)
{
    node *n;
    while ((n = lookup(s, id)))
    {
        clear_node(&n->cached);
        n->cached_valid = false;
        id = n->parent ? n->parent : n->mask_owner;
    }
}

bool gr_gtk_tree_size(gr_gtk_shared *s, gr_size *out)
{
    node *n = lookup(s, s->root);
    bool valid = n && n->has_resolved;
    *out = valid ? n->resolved.props.bounds.size : (gr_size){0};
    return valid;
}

static GdkTexture *acquire_texture(gr_gtk_shared *s, gr_image *image)
{
    uint64_t id = gr_image_get_id(image);
    texture_entry *e = g_hash_table_lookup(s->textures, &id);
    if (!e)
    {
        e = g_new0(texture_entry, 1);
        e->id = id;
        e->texture = gr_gdk_texture(image);
        g_hash_table_insert(s->textures, &e->id, e);
    }
    ++e->uses;
    return g_object_ref(e->texture);
}

static void release_texture(gr_gtk_shared *s, uint64_t id)
{
    texture_entry *e = g_hash_table_lookup(s->textures, &id);
    if (e && !--e->uses)
        g_hash_table_remove(s->textures, &id);
}

static GskRenderNode *shape_node(const gr_shape_props *shape)
{
    if (!shape->path)
        return NULL;
    GskPath *path = gr_gsk_path(shape->path);
    GtkSnapshot *snap = gtk_snapshot_new();
    if (shape->fill)
        gr_gsk_paint_path(snap, path, shape->fill_rule, NULL, shape->fill);
    if (shape->stroke)
    {
        GskPath *trim = gr_gsk_trim(path, shape->stroke_start, shape->stroke_end);
        if (trim)
        {
            gr_stroke_style st = gr_stroke_get_style(shape->stroke);
            GskStroke *stroke = gr_gsk_stroke(&st);
            gr_gsk_paint_path(snap, trim, GR_FILL_NONZERO, stroke, gr_stroke_get_paint(shape->stroke));
            gsk_stroke_free(stroke);
            gsk_path_unref(trim);
        }
    }
    gsk_path_unref(path);
    return gtk_snapshot_free_to_node(snap);
}

static bool same_info(gr_draw_info a, gr_draw_info b)
{
    return a.scale == b.scale && a.bounds.origin.x == b.bounds.origin.x && a.bounds.origin.y == b.bounds.origin.y &&
           a.bounds.size.width == b.bounds.size.width && a.bounds.size.height == b.bounds.size.height;
}

static void update_node(gr_gtk_shared *s, const gr_layer_update *u, double scale, gr_drawer_provider *drawers)
{
    node *n = required(s, u->layer);
    const gr_resolved *r = &u->resolved;
    if ((u->changed & GR_DIRTY_MASK) && n->mask != r->mask)
    {
        node *old = lookup(s, n->mask);
        if (old)
            old->mask_owner = NULL;
        if (r->mask)
            required(s, r->mask)->mask_owner = u->layer;
        n->mask = r->mask;
    }
    if (u->changed & GR_DIRTY_CONTENT)
    {
        gr_image *image = r->content.kind == GR_CONTENT_IMAGE ? r->content.value.image.image : NULL;
        uint64_t id = image ? gr_image_get_id(image) : 0;
        if (n->image_id != id)
        {
            if (n->image_id)
                release_texture(s, n->image_id);
            g_clear_object(&n->image);
            n->image_id = id;
            if (image)
                n->image = acquire_texture(s, image);
        }
        if (r->content.kind != GR_CONTENT_DRAWN)
        {
            clear_node(&n->drawing);
            n->has_drawing = false;
        }
        clear_node(&n->shape);
        if (r->content.kind == GR_CONTENT_SHAPE)
            n->shape = shape_node(&r->content.value.shape);
    }
    if (r->content.kind == GR_CONTENT_DRAWN)
    {
        gr_draw_info info = gr_resolved_get_draw_info(r, scale);
        uint64_t version = r->content.value.display_version;
        if (!n->has_drawing || n->display_version != version || !same_info(info, n->draw_info))
        {
            gr_display_list *list = NULL;
            clear_node(&n->drawing);
            if (gr_drawer_provider_draw(drawers, u->layer, version, &info, &list) == GR_OK)
            {
                n->drawing = gr_gsk_recording(list);
                gr_display_list_unref(list);
            }
            n->has_drawing = true;
            n->display_version = version;
            n->draw_info = info;
        }
    }
    gr_resolved copy = *r;
    resolved_refs(&copy, true);
    if (n->has_resolved)
        resolved_refs(&n->resolved, false);
    n->resolved = copy;
    n->has_resolved = true;
    invalidate(s, u->layer);
}

static void remove_child(node *n, gr_layer *id)
{
    for (guint i = 0; i < n->children->len; ++i)
        if (g_ptr_array_index(n->children, i) == id)
        {
            g_ptr_array_remove_index(n->children, i);
            return;
        }
}

static void release_node(gr_gtk_shared *s, gr_layer *id)
{
    node *n = lookup(s, id);
    if (!n)
        return;
    if (n->image_id)
        release_texture(s, n->image_id);
    for (guint i = 0; i < n->children->len; ++i)
    {
        node *c = lookup(s, g_ptr_array_index(n->children, i));
        if (c && c->parent == id)
            c->parent = NULL;
    }
    node *mask = lookup(s, n->mask);
    if (mask)
        mask->mask_owner = NULL;
    node *owner = lookup(s, n->mask_owner);
    if (owner && owner->mask == id)
        owner->mask = NULL;
    invalidate(s, n->mask_owner);
    node *parent = lookup(s, n->parent);
    if (parent)
        remove_child(parent, id);
    invalidate(s, n->parent);
    g_hash_table_remove(s->nodes, id);
}

void gr_gtk_tree_apply(gr_gtk_shared *s, const gr_commit_batch *b)
{
    for (size_t i = 0; i < b->realized.count; ++i)
    {
        node *n = g_new0(node, 1);
        n->id = b->realized.data[i].layer;
        n->children = g_ptr_array_new();
        g_hash_table_insert(s->nodes, n->id, n);
    }
    if (b->root)
        s->root = b->root;
    for (size_t i = 0; i < b->updates.count; ++i)
        update_node(s, &b->updates.data[i], b->backing_scale, b->drawers);
    for (size_t i = 0; i < b->children.count; ++i)
    {
        const gr_children_update *u = &b->children.data[i];
        node *parent = required(s, u->parent);
        for (guint j = 0; j < parent->children->len; ++j)
        {
            gr_layer *id = g_ptr_array_index(parent->children, j);
            bool found = false;
            for (size_t k = 0; k < u->children.count; ++k)
                found |= id == u->children.data[k];
            node *child = required(s, id);
            if (!found && child->parent == u->parent)
                child->parent = NULL;
        }
    }
    for (size_t i = 0; i < b->children.count; ++i)
    {
        const gr_children_update *u = &b->children.data[i];
        node *parent = required(s, u->parent);
        for (size_t j = 0; j < u->children.count; ++j)
        {
            gr_layer *id = u->children.data[j];
            node *child = required(s, id);
            node *owner = lookup(s, child->mask_owner);
            if (owner && owner->mask == id)
                owner->mask = NULL;
            child->mask_owner = NULL;
            if (child->parent && child->parent != u->parent)
            {
                node *prev = lookup(s, child->parent);
                if (prev)
                {
                    remove_child(prev, id);
                    clear_node(&prev->cached);
                    prev->cached_valid = false;
                }
            }
            child->parent = u->parent;
        }
        g_ptr_array_set_size(parent->children, 0);
        for (size_t j = 0; j < u->children.count; ++j)
            g_ptr_array_add(parent->children, u->children.data[j]);
        invalidate(s, u->parent);
    }
    for (size_t i = 0; i < b->released.count; ++i)
        release_node(s, b->released.data[i]);
}

static GskRenderNode *build(gr_gtk_shared *shared, gr_layer *id);

static GskRenderNode *compose(gr_gtk_shared *shared, node *n)
{
    const gr_resolved *r = &n->resolved;
    const gr_layer_props *p = &r->props;
    gr_rect bounds = {{0, 0}, r->size};
    graphene_rect_t rect = gr_graphene_rect(bounds);
    graphene_point_t local = gr_graphene_point(r->bounds_offset);
    bool rounded = p->corner_radius > 0 && p->masked_corners;
    GskRoundedRect outline = gr_gsk_rounded(bounds, p->corner_radius, rounded ? p->masked_corners : 0);
    GtkSnapshot *s = gtk_snapshot_new();
    gtk_snapshot_save(s);
    GskTransform *transform = gr_gsk_transform(r->transform);
    gtk_snapshot_transform(s, transform);
    if (transform)
        gsk_transform_unref(transform);
    bool faded = p->opacity < 1;
    if (faded)
        gtk_snapshot_push_opacity(s, p->opacity);
    if (p->has_shadow && r->has_shadow)
    {
        const gr_shadow *shadow = &p->shadow;
        GdkRGBA color = gr_gdk_color(shadow->color);
        const gr_shadow_shape *shape = &r->shadow_shape;
        if (shape->kind == GR_SHADOW_ROUNDED_RECT && p->has_background && p->background.a >= 1 && !n->mask)
        {
            GskRoundedRect sr = gr_gsk_rounded(shape->value.rounded_rect.rect, shape->value.rounded_rect.radius,
                                               shape->value.rounded_rect.corners);
            gtk_snapshot_append_outset_shadow(s, &sr, &color, (float)shadow->offset.x, (float)shadow->offset.y,
                                              (float)shape->value.rounded_rect.spread, (float)(shadow->blur_sigma * 2));
        }
        else
        {
            gr_path *path = gr_shadow_shape_new_path(shape, r->bounds_offset);
            GskPath *native = gr_gsk_path(path);
            gr_path_unref(path);
            gtk_snapshot_save(s);
            graphene_point_t offset = gr_graphene_point(shadow->offset);
            gtk_snapshot_translate(s, &offset);
            if (shadow->blur_sigma > 0)
                gtk_snapshot_push_blur(s, shadow->blur_sigma * 2);
            gtk_snapshot_append_fill(s, native, GSK_FILL_RULE_WINDING, &color);
            if (shadow->blur_sigma > 0)
                gtk_snapshot_pop(s);
            gtk_snapshot_restore(s);
            gsk_path_unref(native);
        }
    }
    if (n->mask)
    {
        gtk_snapshot_push_mask(s, GSK_MASK_MODE_ALPHA);
        GskRenderNode *mask = build(shared, n->mask);
        if (mask)
        {
            gtk_snapshot_save(s);
            gtk_snapshot_translate(s, &local);
            gtk_snapshot_append_node(s, mask);
            gtk_snapshot_restore(s);
        }
        gtk_snapshot_pop(s);
    }
    if (p->masks_to_bounds)
    {
        if (rounded)
            gtk_snapshot_push_rounded_clip(s, &outline);
        else
            gtk_snapshot_push_clip(s, &rect);
    }
    if (p->has_background)
    {
        GdkRGBA c = gr_gdk_color(p->background);
        if (rounded)
            gtk_snapshot_push_rounded_clip(s, &outline);
        gtk_snapshot_append_color(s, &c, &rect);
        if (rounded)
            gtk_snapshot_pop(s);
    }
    switch (r->content.kind)
    {
        case GR_CONTENT_IMAGE:
            if (n->image)
            {
                graphene_rect_t dst = gr_graphene_rect(r->content.value.image.rect);
                gtk_snapshot_append_scaled_texture(s, n->image, GSK_SCALING_FILTER_LINEAR, &dst);
            }
            break;
        case GR_CONTENT_DRAWN:
            if (n->drawing)
            {
                gtk_snapshot_push_clip(s, &rect);
                gtk_snapshot_save(s);
                gtk_snapshot_translate(s, &local);
                gtk_snapshot_append_node(s, n->drawing);
                gtk_snapshot_restore(s);
                gtk_snapshot_pop(s);
            }
            break;
        case GR_CONTENT_SHAPE:
            if (n->shape)
            {
                gtk_snapshot_save(s);
                gtk_snapshot_translate(s, &local);
                gtk_snapshot_append_node(s, n->shape);
                gtk_snapshot_restore(s);
            }
            break;
        case GR_CONTENT_GRADIENT:
            gr_gsk_paint(s, r->content.value.gradient, &rect, r->size);
            break;
        case GR_CONTENT_NONE:
            break;
    }
    if (n->children->len)
    {
        gtk_snapshot_save(s);
        gtk_snapshot_translate(s, &local);
        for (guint i = 0; i < n->children->len; ++i)
        {
            GskRenderNode *child = build(shared, g_ptr_array_index(n->children, i));
            if (child)
                gtk_snapshot_append_node(s, child);
        }
        gtk_snapshot_restore(s);
    }
    if (r->has_border && r->border.width > 0)
    {
        float w = (float)r->border.width;
        float widths[] = {w, w, w, w};
        GdkRGBA c = gr_gdk_color(r->border.color), colors[] = {c, c, c, c};
        gtk_snapshot_append_border(s, &outline, widths, colors);
    }
    if (p->masks_to_bounds)
        gtk_snapshot_pop(s);
    if (n->mask)
        gtk_snapshot_pop(s);
    if (faded)
        gtk_snapshot_pop(s);
    gtk_snapshot_restore(s);
    return gtk_snapshot_free_to_node(s);
}

static GskRenderNode *build(gr_gtk_shared *shared, gr_layer *id)
{
    node *n = lookup(shared, id);
    if (!n)
        return NULL;
    if (n->cached_valid)
        return n->cached;
    n->cached = n->has_resolved && !n->resolved.props.hidden ? compose(shared, n) : NULL;
    n->cached_valid = true;
    return n->cached;
}

GskRenderNode *gr_gtk_tree_root(gr_gtk_shared *shared)
{
    GskRenderNode *root = build(shared, shared->root);
    return root ? gsk_render_node_ref(root) : NULL;
}
