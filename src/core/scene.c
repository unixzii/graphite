#include "model.h"

gr_layer_props gr_model_defaults(void)
{
    return (gr_layer_props){
        .anchor_point = {0.5, 0.5}, .transform = {1, 0, 0, 1, 0, 0}, .opacity = 1, .masked_corners = GR_CORNERS_ALL};
}

gr_result gr_model_writable(const gr_scene *s)
{
    return s && (s->drawing || s->busy) ? GR_ERROR_REENTRANT : GR_OK;
}

static void dequeue(gr_layer *l)
{
    if (!l->queued)
        return;
    gr_scene *s = l->scene;
    if (l->dirty_previous)
        l->dirty_previous->dirty_next = l->dirty_next;
    else
        s->dirty_head = l->dirty_next;
    if (l->dirty_next)
        l->dirty_next->dirty_previous = l->dirty_previous;
    else
        s->dirty_tail = l->dirty_previous;
    l->dirty_previous = l->dirty_next = NULL;
    l->queued = false;
}

void gr_model_mark(gr_layer *l, gr_dirty_bits bits)
{
    if (!bits || !l->scene)
        return;
    l->dirty |= bits;
    if (!l->attached || l->queued)
        return;
    gr_scene *s = l->scene;
    l->queued = true;
    l->dirty_previous = s->dirty_tail;
    if (s->dirty_tail)
        s->dirty_tail->dirty_next = l;
    else
        s->dirty_head = l;
    s->dirty_tail = l;
}

void gr_model_attach(gr_layer *l, bool attached)
{
    gr_layer **stack = NULL;
    size_t count = 0, capacity = 0;
    void *data = stack;
    gr_reserve(&data, &capacity, 1, sizeof(*stack));
    stack = data;
    stack[count++] = l;
    while (count)
    {
        gr_layer *node = stack[--count];
        node->attached = attached;
        if (attached)
            gr_model_mark(node, node->realized ? node->dirty : GR_DIRTY_ALL);
        if (node->child_count > SIZE_MAX - count - 1)
            abort();
        data = stack;
        gr_reserve(&data, &capacity, count + node->child_count + 1, sizeof(*stack));
        stack = data;
        for (size_t i = 0; i < node->child_count; ++i)
            stack[count++] = node->children[i];
        if (node->mask)
            stack[count++] = node->mask;
    }
    free(stack);
}

void gr_model_drop_content(gr_layer *l)
{
    gr_drawer drawer = l->drawer;
    l->drawer = (gr_drawer){0};
    gr_path *shadow = l->props.has_shadow ? l->props.shadow.path : NULL;
    l->props = gr_model_defaults();
    gr_shape_props shape = l->shape;
    l->shape = (gr_shape_props){.stroke_end = 1};
    gr_image *image = l->image;
    gr_paint *gradient = l->gradient;
    l->image = NULL;
    l->gradient = NULL;
    l->gravity = GR_GRAVITY_RESIZE;
    gr_path_unref(shadow);
    gr_path_unref(shape.path);
    gr_paint_unref(shape.fill);
    gr_stroke_unref(shape.stroke);
    gr_image_unref(image);
    gr_paint_unref(gradient);
    if (drawer.destroy)
        drawer.destroy(drawer.userdata);
}

static void clear_edges(gr_layer *l)
{
    gr_layer **children = l->children;
    size_t count = l->child_count;
    gr_layer *mask = l->mask;
    l->children = NULL;
    l->child_count = l->child_capacity = 0;
    l->mask = NULL;
    for (size_t i = 0; i < count; ++i)
    {
        children[i]->parent = NULL;
        children[i]->mask_parent = false;
        gr_layer_unref(children[i]);
    }
    if (mask)
    {
        mask->parent = NULL;
        mask->mask_parent = false;
        gr_layer_unref(mask);
    }
    free(children);
}

/* Consumes a Scene pin after cleanup and scheduling; callers must not use s afterward. */
static void flush_owned(gr_scene *s)
{
    if (s->closing || s->busy || s->drawing || s->draining)
    {
        gr_scene_unref(s);
        return;
    }
    s->draining = true;
    while (s->pending)
    {
        gr_layer *l = s->pending;
        s->pending = l->pending_next;
        dequeue(l);
        if (l->previous)
            l->previous->next = l->next;
        else
            s->layers = l->next;
        if (l->next)
            l->next->previous = l->previous;
        --s->layer_count;
        l->scene = NULL;
        clear_edges(l);
        gr_model_drop_content(l);
        if (l->realized)
        {
            void *data = s->released;
            gr_reserve(&data, &s->released_capacity, s->released_count + 1, sizeof(*s->released));
            s->released = data;
            s->released[s->released_count++] = l;
        }
        else
            free(l);
    }
    s->draining = false;
    if (s->has_backend && !s->requested && (s->dirty_head || s->released_count))
    {
        s->requested = true;
        s->busy = true;
        s->vtable.request_commit(s->backend);
        s->busy = false;
    }
    gr_scene_unref(s);
}

void gr_model_flush(gr_scene *s)
{
    if (!s || s->closing || s->busy || s->drawing || s->draining)
        return;
    flush_owned(gr_scene_ref(s));
}

gr_scene *gr_scene_new(gr_size size)
{
    if (!gr_valid_rect((gr_rect){{0, 0}, size}))
        return NULL;
    gr_scene *s = gr_alloc(1, sizeof(*s));
    *s = (gr_scene){.refs = 1, .scale = 1, .snapping = true};
    s->link = gr_alloc(1, sizeof(*s->link));
    *s->link = (gr_scene_link){1, s};
    s->provider = gr_alloc(1, sizeof(*s->provider));
    *s->provider = (gr_drawer_provider){1, gr_scene_link_ref(s->link)};
    s->root = gr_layer_new(s, GR_LAYER_CONTAINER);
    s->root->props.bounds.size = size;
    s->root->attached = true;
    gr_model_mark(s->root, GR_DIRTY_ALL);
    return s;
}

gr_scene *gr_scene_ref(gr_scene *s)
{
    if (s)
        gr_local_ref(&s->refs);
    return s;
}

void gr_scene_unref(gr_scene *s)
{
    if (!s || !gr_local_unref(&s->refs))
        return;
    s->closing = true;
    s->link->scene = NULL;
    /* Pin every allocation before callbacks can release other handles. */
    for (gr_layer *l = s->layers; l; l = l->next)
    {
        if (l->refs == SIZE_MAX)
            abort();
        ++l->refs;
        l->scene = NULL;
        l->parent = NULL;
        l->mask_parent = false;
    }
    if (s->has_backend)
        s->vtable.destroy(s->backend);
    for (size_t i = 0; i < s->released_count; ++i)
        free(s->released[i]);
    free(s->released);
    /* All model links were invalidated together, before any user destruction. */
    gr_layer *l = s->layers;
    while (l)
    {
        gr_layer *next = l->next;
        clear_edges(l);
        gr_model_drop_content(l);
        if (l == s->root)
        {
            /* Release root ownership without dropping its teardown allocation pin. */
            if (l->refs < 2)
                abort();
            --l->refs;
        }
        gr_layer_unref(l);
        l = next;
    }
    gr_drawer_provider_unref(s->provider);
    gr_scene_link_unref(s->link);
    free(s);
}

gr_layer *gr_layer_new(gr_scene *s, gr_layer_kind kind)
{
    if (s->closing || gr_model_writable(s) != GR_OK || kind < GR_LAYER_CONTAINER || kind > GR_LAYER_GRADIENT)
        return NULL;
    gr_layer *l = gr_alloc(1, sizeof(*l));
    *l = (gr_layer){.refs = 1,
                    .scene = s,
                    .kind = kind,
                    .dirty = GR_DIRTY_ALL,
                    .props = gr_model_defaults(),
                    .shape = {.stroke_end = 1},
                    .gravity = GR_GRAVITY_RESIZE};
    l->next = s->layers;
    if (s->layers)
        s->layers->previous = l;
    s->layers = l;
    ++s->layer_count;
    return l;
}

gr_layer *gr_layer_ref(gr_layer *l)
{
    if (l)
        gr_local_ref(&l->refs);
    return l;
}

void gr_layer_unref(gr_layer *l)
{
    if (!l || !gr_local_unref(&l->refs))
        return;
    gr_scene *s = l->scene;
    if (!s)
    {
        free(l);
        return;
    }
    l->pending_next = s->pending;
    s->pending = l;
    gr_model_flush(s);
}

gr_scene_link *gr_scene_get_link(gr_scene *s)
{
    return gr_scene_link_ref(s->link);
}

gr_scene_link *gr_scene_link_ref(gr_scene_link *link)
{
    if (link)
        gr_local_ref(&link->refs);
    return link;
}

void gr_scene_link_unref(gr_scene_link *link)
{
    if (link && gr_local_unref(&link->refs))
        free(link);
}

gr_scene *gr_scene_link_lock(gr_scene_link *link)
{
    return gr_scene_ref(link->scene);
}

gr_drawer_provider *gr_drawer_provider_ref(gr_drawer_provider *p)
{
    if (p)
        gr_local_ref(&p->refs);
    return p;
}

void gr_drawer_provider_unref(gr_drawer_provider *p)
{
    if (!p || !gr_local_unref(&p->refs))
        return;
    gr_scene_link_unref(p->link);
    free(p);
}

gr_layer *gr_scene_get_root(const gr_scene *s)
{
    return s->root;
}

size_t gr_scene_get_layer_count(const gr_scene *s)
{
    return s->layer_count;
}

bool gr_scene_needs_commit(const gr_scene *s)
{
    return s->dirty_head || s->released_count;
}

double gr_scene_get_backing_scale(const gr_scene *s)
{
    return s->scale;
}

bool gr_scene_get_pixel_snapping(const gr_scene *s)
{
    return s->snapping;
}

static bool size_content(const gr_layer *l)
{
    return l->kind == GR_LAYER_GRADIENT || (l->kind == GR_LAYER_CONTENT && (l->image || l->drawer.draw));
}

gr_dirty_bits gr_model_corner_bits(const gr_layer *l)
{
    return l->props.has_shadow && !l->props.shadow.path ? GR_DIRTY_SHADOW : 0;
}

gr_dirty_bits gr_model_size_bits(const gr_layer *l)
{
    return gr_model_corner_bits(l) | (size_content(l) ? GR_DIRTY_CONTENT : 0);
}

static gr_dirty_bits snap_bits(const gr_layer *l)
{
    return GR_DIRTY_GEOMETRY | (l->props.has_border ? GR_DIRTY_APPEARANCE : 0) |
           (l->props.has_shadow ? GR_DIRTY_SHADOW : 0) | (size_content(l) ? GR_DIRTY_CONTENT : 0);
}

gr_result gr_scene_set_backing_scale(gr_scene *s, double scale)
{
    if (gr_model_writable(s) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (!isfinite(scale) || scale <= 0)
        return GR_ERROR_ARGUMENT;
    if (s->scale == scale)
        return GR_OK;
    s->scale = scale;
    for (gr_layer *l = s->layers; l; l = l->next)
        gr_model_mark(l, (s->snapping ? snap_bits(l) : 0) | (l->kind != GR_LAYER_CONTAINER ? GR_DIRTY_CONTENT : 0));
    gr_model_flush(s);
    return GR_OK;
}

gr_result gr_scene_set_pixel_snapping(gr_scene *s, bool enabled)
{
    if (gr_model_writable(s) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (s->snapping == enabled)
        return GR_OK;
    s->snapping = enabled;
    for (gr_layer *l = s->layers; l; l = l->next)
        gr_model_mark(l, snap_bits(l));
    gr_model_flush(s);
    return GR_OK;
}

gr_result gr_scene_set_backend(gr_scene *s, const gr_backend_vtable *vtable, void *backend)
{
    if (gr_model_writable(s) != GR_OK)
        return GR_ERROR_REENTRANT;
    if (s->has_backend)
        return GR_ERROR_STATE;
    if (!vtable || !vtable->apply || !vtable->destroy || !vtable->request_commit)
        return GR_ERROR_ARGUMENT;
    s->vtable = *vtable;
    s->backend = backend;
    s->has_backend = true;
    gr_model_flush(s);
    return GR_OK;
}

gr_result gr_drawer_provider_draw(gr_drawer_provider *p, gr_layer *l, uint64_t version, const gr_draw_info *info,
                                  gr_display_list **out)
{
    *out = NULL;
    gr_scene *s = p->link->scene;
    if (!s)
        return GR_ERROR_STATE;
    if (s->drawing)
        return GR_ERROR_REENTRANT;
    if (l->scene && l->scene != s)
        return GR_ERROR_SCENE;
    if (!l->refs || l->scene != s || !l->drawer.draw || l->display_version != version)
        return GR_ERROR_STATE;
    if (!info || !gr_valid_rect(info->bounds) || !isfinite(info->scale) || info->scale <= 0)
        return GR_ERROR_ARGUMENT;
    gr_scene_ref(s);
    gr_layer_ref(l);
    s->drawing = true;
    gr_canvas *canvas = gr_canvas_new();
    gr_drawer drawer = l->drawer;
    drawer.draw(drawer.userdata, canvas, info);
    *out = gr_canvas_finish(canvas);
    gr_canvas_unref(canvas);
    s->drawing = false;
    gr_layer_unref(l);
    flush_owned(s);
    return GR_OK;
}

gr_result gr_scene_commit(gr_scene *s)
{
    if (gr_model_writable(s) != GR_OK || s->draining)
        return GR_ERROR_REENTRANT;
    if (!s->has_backend)
        return GR_ERROR_STATE;
    if (!gr_scene_needs_commit(s))
        return GR_OK;
    gr_scene_ref(s);
    s->busy = true;
    size_t capacity = 0;
    for (gr_layer *l = s->dirty_head; l; l = l->dirty_next)
        ++capacity;
    gr_realized_layer *realized = gr_alloc(capacity, sizeof(*realized));
    gr_children_update *children = gr_alloc(capacity, sizeof(*children));
    gr_layer_update *updates = gr_alloc(capacity, sizeof(*updates));
    gr_commit_batch batch = {.backing_scale = s->scale, .drawers = s->provider};
    batch.realized.data = realized;
    batch.children.data = children;
    batch.updates.data = updates;
    while (s->dirty_head)
    {
        gr_layer *l = s->dirty_head;
        dequeue(l);
        if (!l->attached || !l->refs)
            continue;
        if (!l->realized)
        {
            l->realized = true;
            l->dirty = GR_DIRTY_ALL;
            realized[batch.realized.count++] = (gr_realized_layer){l, l->kind};
            if (l == s->root)
                batch.root = l;
        }
        if (l->dirty & GR_DIRTY_CHILDREN)
            children[batch.children.count++] = (gr_children_update){l, {l->children, l->child_count}};
        gr_dirty_bits changed = l->dirty & ~GR_DIRTY_CHILDREN;
        if (changed)
            updates[batch.updates.count++] = (gr_layer_update){l, changed, gr_model_resolve(l)};
        l->dirty = 0;
    }
    gr_layer **released = s->released;
    size_t count = s->released_count;
    s->released = NULL;
    s->released_count = s->released_capacity = 0;
    batch.released = (gr_layer_list){released, count};
    if (batch.root || batch.realized.count || batch.children.count || batch.updates.count || count)
        s->vtable.apply(s->backend, &batch);
    for (size_t i = 0; i < count; ++i)
        free(released[i]);
    free(released);
    free(realized);
    free(children);
    free(updates);
    s->busy = false;
    s->requested = false;
    flush_owned(s);
    return GR_OK;
}
